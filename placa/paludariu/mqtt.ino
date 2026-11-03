// Paludariu - acces de la distanta prin MQTT (de exemplu EMQX Serverless)
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.
//
// Placa tine o legatura criptata (TLS, portul 8883) cu un server MQTT din internet. Cand nu esti acasa,
// aplicatia trimite cererile pe "pal/<id>/cer", iar placa raspunde pe "pal/<id>/rasp".
// Peste TLS, fiecare mesaj e criptat cap-coada (AES-256-GCM) cu o cheie pe care o stiu doar placa si
// aplicatia: serverul doar transporta mesajele, nu le poate citi si nici modifica.
// Cererile vechi (mai vechi de 5 minute) sau repetate sunt refuzate.
// Setarile (adresa, utilizatorul, parola, cheia) se fac din aplicatie si stau in memoria placii.

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/gcm.h"
#define ARE_MQTT 1
#else
#define ARE_MQTT 0   // ESP32 Arduino core 2.x: accesul de la distanta nu e disponibil
#endif

String mqHost, mqUser, mqPass, mqId;
uint8_t mqKey[32];
bool mqCheie = false;
volatile int mqCod = 0;   // 0 nesetat, 1 se conecteaza, 2 conectat, 3 server a refuzat parola, 4 nu ajunge la server
unsigned long mqPauzaT = 0;

int mqttCod() { return mqCod; }
bool mqConfigurat() { return ARE_MQTT && mqHost.length() && mqUser.length() && mqCheie; }

void mqIncarca() {
  mqHost = prefs.getString("mqh", "");
  mqUser = prefs.getString("mqu", "");
  mqPass = prefs.getString("mqp", "");
  mqCheie = prefs.getBytes("mqk", mqKey, 32) == 32;
  mqId = prefs.getString("mqi", "");
  if (mqId.length() != 10) {   // un nume de canal greu de ghicit, ales o singura data
    char b[11];
    snprintf(b, sizeof b, "%08lx%02x", (unsigned long)esp_random(), (unsigned)(esp_random() & 0xff));
    mqId = b;
    prefs.putString("mqi", mqId);
  }
  mqCod = mqConfigurat() ? 1 : 0;
}

#if ARE_MQTT
esp_mqtt_client_handle_t mqCli = nullptr;
String mqUri, mqCid, tCer, tRasp, tOn;
struct MqMsg { uint8_t* d; uint16_t n; };
QueueHandle_t mqQ = nullptr;
uint8_t mqAsm[2048];   // mesajele mai mari vin in bucati; le lipim aici

static void mqEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
  esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
  switch (id) {
    case MQTT_EVENT_CONNECTED:
      mqCod = 2;
      esp_mqtt_client_subscribe(e->client, tCer.c_str(), 0);
      esp_mqtt_client_publish(e->client, tOn.c_str(), "1", 1, 1, 1);   // "placa e pornita" (pastrat pe server)
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (mqCod == 2) mqCod = 1;
      break;
    case MQTT_EVENT_ERROR:
      if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) mqCod = 3;
      else if (mqCod != 2) mqCod = 4;
      break;
    case MQTT_EVENT_DATA: {
      if (e->total_data_len > (int)sizeof(mqAsm) || e->current_data_offset + e->data_len > (int)sizeof(mqAsm)) break;
      memcpy(mqAsm + e->current_data_offset, e->data, e->data_len);
      if (e->current_data_offset + e->data_len < e->total_data_len) break;   // mai vin bucati
      MqMsg m;
      m.n = e->total_data_len;
      m.d = (uint8_t*)malloc(m.n);
      if (!m.d) break;
      memcpy(m.d, mqAsm, m.n);
      if (xQueueSend(mqQ, &m, 0) != pdTRUE) free(m.d);
      break;
    }
    default: break;
  }
}

void mqStop() {
  if (!mqCli) return;
  esp_mqtt_client_stop(mqCli);
  esp_mqtt_client_destroy(mqCli);
  mqCli = nullptr;
  mqCod = mqConfigurat() ? 1 : 0;
}

void mqStart() {
  if (mqCli || !mqConfigurat() || WiFi.status() != WL_CONNECTED) return;
  if (!mqQ) mqQ = xQueueCreate(4, sizeof(MqMsg));
  mqUri = mqHost.indexOf("://") > 0 ? mqHost : "mqtts://" + mqHost + ":8883";
  mqCid = "pal-" + mqId;
  tCer = "pal/" + mqId + "/cer";
  tRasp = "pal/" + mqId + "/rasp";
  tOn = "pal/" + mqId + "/on";
  esp_mqtt_client_config_t c = {};
  c.broker.address.uri = mqUri.c_str();
  c.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;   // verifica certificatul serverului
  c.credentials.username = mqUser.c_str();
  c.credentials.client_id = mqCid.c_str();
  c.credentials.authentication.password = mqPass.c_str();
  c.session.keepalive = 60;
  c.session.last_will.topic = tOn.c_str();   // daca placa dispare, serverul anunta "0"
  c.session.last_will.msg = "0";
  c.session.last_will.msg_len = 1;
  c.session.last_will.qos = 1;
  c.session.last_will.retain = 1;
  c.buffer.size = 2048;
  c.buffer.out_size = 2048;
  c.network.reconnect_timeout_ms = 15000;
  c.network.timeout_ms = 10000;
  c.task.stack_size = 6144;
  mqCli = esp_mqtt_client_init(&c);
  if (!mqCli) { mqCod = 4; return; }
  esp_mqtt_client_register_event(mqCli, MQTT_EVENT_ANY, mqEvent, nullptr);
  mqCod = 1;
  esp_mqtt_client_start(mqCli);
}

// ---------- criptarea cap-coada: nonce (12) | text criptat | eticheta (16) ----------
bool mqDecripteaza(const uint8_t* d, int n, const String& aad, String& out) {
  if (n < 12 + 16 + 2) return false;
  int clen = n - 28;
  uint8_t* pt = (uint8_t*)malloc(clen + 1);
  if (!pt) return false;
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, mqKey, 256);
  if (!r) r = mbedtls_gcm_auth_decrypt(&g, clen, d, 12, (const uint8_t*)aad.c_str(), aad.length(), d + 12 + clen, 16, d + 12, pt);
  mbedtls_gcm_free(&g);
  if (r) { free(pt); return false; }
  pt[clen] = 0;
  out = String((const char*)pt);
  free(pt);
  return true;
}

uint8_t* mqCripteaza(const String& plain, const String& aad, size_t& outLen) {
  size_t n = plain.length();
  outLen = 12 + n + 16;
  uint8_t* o = (uint8_t*)malloc(outLen);
  if (!o) return nullptr;
  esp_fill_random(o, 12);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, mqKey, 256);
  if (!r) r = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, o, 12, (const uint8_t*)aad.c_str(), aad.length(),
                                        (const uint8_t*)plain.c_str(), o + 12, 16, o + 12 + n);
  mbedtls_gcm_free(&g);
  if (r) { free(o); return nullptr; }
  return o;
}

// valoarea unui camp dintr-un JSON simplu, ex. {"n":"a1b2","t":1793700000,"p":"/s"}
String jCamp(const String& j, const char* k) {
  String cheie = String("\"") + k + "\":";
  int i = j.indexOf(cheie);
  if (i < 0) return "";
  i += cheie.length();
  if (i < (int)j.length() && j[i] == '"') {
    int e = j.indexOf('"', i + 1);
    return e < 0 ? String("") : j.substring(i + 1, e);
  }
  int e = i;
  while (e < (int)j.length() && j[e] != ',' && j[e] != '}') e++;
  return j.substring(i, e);
}

// Ce poate cere aplicatia prin internet. Schimbarea retelei WiFi, actualizarea manuala si setarile
// accesului de la distanta raman doar pentru cand esti acasa.
Rasp apiCerere(const String& cale) {
  int qi = cale.indexOf('?');
  String p = qi < 0 ? cale : cale.substring(0, qi);
  Arg a{qi < 0 ? String("") : cale.substring(qi + 1), false};
  if (p == "/s") return apiStatus(a);
  if (p == "/h") return apiHist(a);
  if (p == "/h30") return apiHist30(a);
  if (p == "/e") return apiEv(a);
  if (p == "/m") return apiMod(a);
  if (p == "/save") return apiSave(a);
  if (p == "/pl") return apiPl(a);
  if (p == "/mem") return apiMem(a);
  if (p == "/cfg") return apiCfg(a);
  if (p == "/vac") return apiVac(a);
  if (p == "/gh") return apiGh(a);
#if FOLOSESTE_TELEGRAM
  if (p == "/tgtest") return apiTgTest(a);
#endif
  return Rasp{403, "text/plain; charset=utf-8", "De la distanță nu se poate face asta. Încearcă de acasă."};
}

void mqServeste(const uint8_t* d, int n) {
  String cer;
  if (!mqDecripteaza(d, n, tCer, cer)) return;   // nu e de la aplicatia noastra (cheie gresita sau modificat pe drum)
  String id = jCamp(cer, "n");
  long t = jCamp(cer, "t").toInt();
  String cale = jCamp(cer, "p");
  if (!id.length() || id.length() > 24 || !cale.startsWith("/")) return;
  // refuzam cererile vechi sau repetate (cineva le-ar putea retrimite)
  static String vazute[24];
  static int vi = 0;
  for (auto& v : vazute) if (v == id) return;
  vazute[vi] = id; vi = (vi + 1) % 24;
  time_t acumT = time(nullptr);
  Rasp r;
  if (acumT > 1000000000 && labs((long)acumT - t) > 300) r = Rasp{408, "text/plain; charset=utf-8", "Ora telefonului diferă prea mult de a plăcii."};
  else r = apiCerere(cale);
  String plain = "{\"n\":\"" + id + "\",\"c\":" + String(r.cod) + ",\"y\":\"" + String(r.tip) + "\"}\n" + r.corp;
  size_t len;
  uint8_t* o = mqCripteaza(plain, tRasp, len);
  if (!o) return;
  if (mqCli) esp_mqtt_client_publish(mqCli, tRasp.c_str(), (const char*)o, len, 0, 0);
  free(o);
}
#else
void mqStop() {}
void mqStart() {}
#endif

// Se apeleaza din loop(): porneste legatura cand e WiFi si raspunde la cererile primite
void mqLoop() {
#if ARE_MQTT
  static unsigned long tIncerc = 0;
  if (mqCod == 3 && mqCli) { mqStop(); mqCod = 3; mqPauzaT = millis(); }   // parola refuzata: nu insistam
  bool pauza = mqCod == 3 && millis() - mqPauzaT < 600000UL;
  if (!mqCli && !pauza && mqConfigurat() && WiFi.status() == WL_CONNECTED && millis() - tIncerc > 30000UL) {
    tIncerc = millis();
    mqStart();
  }
  MqMsg m;
  while (mqQ && xQueueReceive(mqQ, &m, 0) == pdTRUE) {
    mqServeste(m.d, m.n);
    free(m.d);
  }
#endif
}

// ---------- setarile, trimise de aplicatie cand esti acasa (doar prin WiFi, cu parola paginii) ----------
bool hexLa(const String& h, uint8_t* out, size_t n) {
  if (h.length() != n * 2) return false;
  for (size_t i = 0; i < n; i++) {
    char b[3] = {h[2 * i], h[2 * i + 1], 0};
    if (!isxdigit((unsigned char)b[0]) || !isxdigit((unsigned char)b[1])) return false;
    out[i] = (uint8_t)strtol(b, nullptr, 16);
  }
  return true;
}

void hMqttCfg() {
  if (!auth()) return;
  if (server.hasArg("off")) {
    mqStop();
    prefs.remove("mqh"); prefs.remove("mqu"); prefs.remove("mqp"); prefs.remove("mqk");
    mqHost = mqUser = mqPass = ""; mqCheie = false; mqCod = 0;
  } else if (server.hasArg("host")) {
    uint8_t k[32];
    if (!hexLa(server.arg("key"), k, 32) || !server.arg("user").length()) {
      server.send(400, "text/plain; charset=utf-8", "Date incomplete.");
      return;
    }
    mqStop();
    mqHost = server.arg("host"); mqHost.trim();
    mqUser = server.arg("user"); mqPass = server.arg("pass");
    memcpy(mqKey, k, 32); mqCheie = true;
    prefs.putString("mqh", mqHost); prefs.putString("mqu", mqUser); prefs.putString("mqp", mqPass);
    prefs.putBytes("mqk", mqKey, 32);
    mqCod = 1;
    mqPauzaT = 0;
    mqStart();
  }
  server.send(200, "application/json", "{\"conf\":" + String(mqConfigurat() ? 1 : 0) + ",\"host\":\"" + jEsc(mqHost) +
              "\",\"user\":\"" + jEsc(mqUser) + "\",\"id\":\"" + mqId + "\",\"cod\":" + String(mqCod) + ",\"are\":" + String(ARE_MQTT) + "}");
}
