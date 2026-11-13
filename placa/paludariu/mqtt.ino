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
#include "esp_tls_errors.h"
#define ARE_MQTT 1
#else
#define ARE_MQTT 0   // ESP32 Arduino core 2.x: accesul de la distanta nu e disponibil
#endif

String mqHost, mqUser, mqPass, mqId;
uint8_t mqKey[32];
bool mqCheie = false;
volatile int mqCod = 0;   // 0 nesetat, 1 se conecteaza, 2 conectat, 3 server a refuzat parola, 4 nu ajunge la server
unsigned long mqPauzaT = 0;
unsigned long mqUrmT = 0;          // cand incercam din nou conectarea
volatile bool mqTine = false;      // tinem randul la legaturile criptate (pana se termina conectarea)
volatile bool mqJos = false;       // legatura a cazut: o refacem din loop(), cand ne vine randul
unsigned long mqTineT = 0;
void mqElibereaza() { if (mqTine) { mqTine = false; tlsLasa(); } }
volatile bool mqOprit = false;     // GitHub are nevoie de memorie: legatura sta oprita cat lucreaza
char mqErr[200] = "";     // de ce n-a mers ultima incercare, pe intelesul omului (gol cand merge)

// "xxx.emqxsl.com", "xxx.emqxsl.com:8883", "mqtts://xxx:8883/" -> "mqtts://xxx.emqxsl.com:8883"
// Porturile 1883 / 8083 / 8084 nu sunt pentru legatura criptata direct, asa ca folosim 8883.
String mqUriDin(String h, String* gazda = nullptr) {
  h.trim();
  int s = h.indexOf("://");
  if (s >= 0) h = h.substring(s + 3);
  int sl = h.indexOf('/');
  if (sl >= 0) h = h.substring(0, sl);
  int c = h.lastIndexOf(':');
  String port = "8883";
  if (c > 0) {
    String p = h.substring(c + 1);
    h = h.substring(0, c);
    if (p.toInt() > 0 && p != "1883" && p != "8083" && p != "8084") port = String(p.toInt());
  }
  h.trim();
  if (gazda) *gazda = h;
  return "mqtts://" + h + ":" + port;
}

int mqttCod() { return mqCod; }
// Pentru actualizari: opreste legatura MQTT (elibereaza ~35 KB) si asteapta sa se inchida; false = repornire.
void mqPauza(bool da) {
  mqOprit = da;
  if (da) for (int i = 0; i < 60 && mqActiv(); i++) vTaskDelay(pdMS_TO_TICKS(100));   // loop() o inchide
}
String mqttErr() { return String(mqErr); }
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
      mqErr[0] = 0;
      mqJos = false;
      mqElibereaza();
      esp_mqtt_client_subscribe(e->client, tCer.c_str(), 0);
      esp_mqtt_client_publish(e->client, tOn.c_str(), "1", 1, 1, 1);   // "placa e pornita" (pastrat pe server)
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (mqCod == 2) mqCod = 1;
      mqJos = true;
      mqElibereaza();
      break;
    case MQTT_EVENT_ERROR: {
      esp_mqtt_error_codes_t* x = e->error_handle;
      mqJos = true;
      mqElibereaza();
      if (x && x->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        mqCod = 3;
        snprintf(mqErr, sizeof mqErr, "Serverul a refuzat contul plăcii (%s). Verifică utilizatorul și parola în EMQX: Access Control → Authentication. (cod %d)",
                 mqUser.c_str(), (int)x->connect_return_code);
        break;
      }
      if (mqCod == 2) break;   // o cadere dupa ce a mers; loop() reface legatura
      mqCod = 4;
      if (!x) { snprintf(mqErr, sizeof mqErr, "Eroare necunoscută."); break; }
      esp_err_t t = x->esp_tls_last_esp_err;
      int st = abs(x->esp_tls_stack_err), so = x->esp_transport_sock_errno;   // codul mbedtls, fara semn
      unsigned kb = ESP.getFreeHeap() / 1024, mb = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024;
      const char* m;
      if (t == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) m = "Adresa serverului nu există. Copiaz-o din nou din EMQX, de la Overview → Address.";
      else if (t == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST || t == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) m = "Serverul nu răspunde pe portul 8883. Verifică adresa; dacă e bună, rețeaua blochează portul.";
      else if (t == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED && (st == 0x2700 || x->esp_tls_cert_verify_flags)) m = "Certificatul serverului n-a putut fi verificat.";
      else if (st == 0x7F00 || st == 0x2880 || st == 0x0010 || st == 0x3F80 || st == 0x4D80 || st == 0x5180 || st == 0x6180 || t == ESP_ERR_NO_MEM || mb < 20) m = "Plăcii nu i-a ajuns memoria pentru legătura criptată.";
      else if (t == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED) m = "Legătura criptată cu serverul n-a reușit.";
      else if (t == ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT) m = "Serverul n-a răspuns la timp la legătura criptată.";
      else m = "Legătura cu serverul s-a întrerupt.";
      snprintf(mqErr, sizeof mqErr, "%s (tls 0x%x, mbedtls -0x%x, cert 0x%x, sock %d, memorie %u/%u KB)",
               m, (unsigned)t, (unsigned)st, (unsigned)x->esp_tls_cert_verify_flags, so, kb, mb);
      break;
    }
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

bool mqActiv() { return mqCli != nullptr; }

void mqStop() {
  if (!mqCli) return;
  esp_mqtt_client_stop(mqCli);
  esp_mqtt_client_destroy(mqCli);
  mqCli = nullptr;
  mqJos = false;
  mqElibereaza();
  mqCod = mqConfigurat() ? 1 : 0;
}

void mqStart() {
  if (mqCli || !mqConfigurat() || WiFi.status() != WL_CONNECTED) return;
  if (!mqQ) mqQ = xQueueCreate(16, sizeof(MqMsg));   // pagina cere mai multe lucruri deodata
  mqUri = mqUriDin(mqHost);
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
  c.network.disable_auto_reconnect = true;   // refacem noi legatura, cand ne vine randul (memoria)
  c.network.timeout_ms = 10000;
  c.task.stack_size = 6144;
  mqCli = esp_mqtt_client_init(&c);
  if (!mqCli) { mqCod = 4; return; }
  esp_mqtt_client_register_event(mqCli, MQTT_EVENT_ANY, mqEvent, nullptr);
  mqCod = 1;
  mqJos = false;
  if (esp_mqtt_client_start(mqCli) != ESP_OK) {
    esp_mqtt_client_destroy(mqCli);
    mqCli = nullptr;
    mqCod = 4;
    snprintf(mqErr, sizeof mqErr, "Plăcii nu i-a ajuns memoria ca să pornească legătura (memorie %u KB).", (unsigned)(ESP.getFreeHeap() / 1024));
  }
}

// ---------- criptarea cap-coada: nonce (12) | text criptat | eticheta (16) ----------
bool mqDecripteaza(const uint8_t* d, int n, const String& aad, String& out, const uint8_t* cheie) {
  if (n < 12 + 16 + 2) return false;
  int clen = n - 28;
  uint8_t* pt = (uint8_t*)malloc(clen + 1);
  if (!pt) return false;
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, cheie, 256);
  if (!r) r = mbedtls_gcm_auth_decrypt(&g, clen, d, 12, (const uint8_t*)aad.c_str(), aad.length(), d + 12 + clen, 16, d + 12, pt);
  mbedtls_gcm_free(&g);
  if (r) { free(pt); return false; }
  pt[clen] = 0;
  out = String((const char*)pt);
  free(pt);
  return true;
}

uint8_t* mqCripteaza(const String& plain, const String& aad, size_t& outLen, const uint8_t* cheie) {
  size_t n = plain.length();
  outLen = 12 + n + 16;
  uint8_t* o = (uint8_t*)malloc(outLen);
  if (!o) return nullptr;
  esp_fill_random(o, 12);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, cheie, 256);
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
  // "doar privit": numai citire
  if (nivelCerere == 1 && !privitPermis(p, a.q.length() == 0)) return Rasp{403, "text/plain; charset=utf-8", "Ai acces doar să privești: nu poți schimba nimic."};
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
  if (p == "/opt") return apiOpt(a);
  if (p == "/hran") return apiHran(a);
  if (p == "/fan") return apiFan(a);
  if (p == "/noapte") return apiNoapte(a);
  if (p == "/privit") return apiPrivit(a, false);
#if FOLOSESTE_TELEGRAM
  if (p == "/tgtest") return apiTgTest(a);
#endif
  return Rasp{403, "text/plain; charset=utf-8", "De la distanță nu se poate face asta. Încearcă de acasă."};
}

void mqServeste(const uint8_t* d, int n) {
  String cer;
  // cheia proprietarului; altfel cheia "doar privit" (familia)
  const uint8_t* cheie = mqKey;
  uint8_t niv = 0;
  if (!mqDecripteaza(d, n, tCer, cer, mqKey)) {
    if (!mqCheie2 || !mqDecripteaza(d, n, tCer, cer, mqKey2)) return;   // nu e de la aplicatia noastra (cheie gresita sau modificat pe drum)
    cheie = mqKey2; niv = 1;
  }
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
  else {
    // un raspuns mare (grafice, istoric) ocupa de ~3 ori marimea lui cat il criptam; fara loc, raspundem scurt
    if (ESP.getMaxAllocHeap() < 24000) r = Rasp{503, "text/plain; charset=utf-8", "Placa are prea puțină memorie liberă acum. Mai încearcă peste câteva secunde."};
    else {
      nivelCerere = niv;
      if (niv) vizNoteaza(2);
      r = apiCerere(cale);
      nivelCerere = 0;
    }
    if (r.cod == 200 && (size_t)ESP.getMaxAllocHeap() < r.corp.length() * 2 + 8000)
      r = Rasp{503, "text/plain; charset=utf-8", "Placa are prea puțină memorie liberă acum. Mai încearcă peste câteva secunde."};
  }
  String plain;
  plain.reserve(r.corp.length() + 96);
  plain = "{\"n\":\"" + id + "\",\"c\":" + String(r.cod) + ",\"y\":\"" + String(r.tip) + "\"}\n";
  plain += r.corp;
  r.corp = String();   // eliberam copia inainte de criptare
  size_t len;
  uint8_t* o = mqCripteaza(plain, tRasp, len, cheie);
  if (!o) return;
  if (mqCli) esp_mqtt_client_publish(mqCli, tRasp.c_str(), (const char*)o, len, 0, 0);
  free(o);
}
#else
void mqStop() {}
void mqStart() {}
bool mqActiv() { return false; }
#endif

// Se apeleaza din loop(): porneste legatura cand e WiFi si raspunde la cererile primite
void mqLoop() {
#if ARE_MQTT
  if (mqTine && millis() - mqTineT > 40000UL) { mqElibereaza(); mqJos = true; }   // siguranta: nu tinem randul la nesfarsit
  if (mqCod == 3 && mqCli) { mqStop(); mqCod = 3; mqPauzaT = millis(); }   // parola refuzata: nu insistam
  if (mqCli && mqJos && !mqTine) { int k = mqCod; mqStop(); mqCod = k; }   // a cazut: o refacem de la zero
  if (mqCli && mqOprit && !mqTine) mqStop();                                  // GitHub lucreaza: facem loc
  bool pauza = mqCod == 3 && millis() - mqPauzaT < 600000UL;
  bool vrem = !mqCli && !pauza && !mqOprit && mqConfigurat() && WiFi.status() == WL_CONNECTED && (long)(millis() - mqUrmT) >= 0;
  if (vrem) {
    tlsCerut = true;              // Telegram si GitHub nu mai incep legaturi noi pana ne conectam
    if (tlsIa(0)) {
      tlsCerut = false;
      mqTine = true;
      mqTineT = millis();
      mqUrmT = millis() + 30000UL;
      mqStart();
      if (!mqCli) mqElibereaza();   // n-a pornit (mqStart a scris de ce)
    }
  } else tlsCerut = false;
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
    mqHost = mqUser = mqPass = ""; mqCheie = false; mqCod = 0; mqErr[0] = 0;
  } else if (server.hasArg("host")) {
    uint8_t k[32];
    if (!hexLa(server.arg("key"), k, 32) || !server.arg("user").length()) {
      server.send(400, "text/plain; charset=utf-8", "Date incomplete.");
      return;
    }
    mqStop();
    String g;
    String u = mqUriDin(server.arg("host"), &g);
    if (!g.length()) { server.send(400, "text/plain; charset=utf-8", "Adresa serverului lipsește."); return; }
    mqHost = u.substring(8);   // pastram "adresa:port", fara "mqtts://"
    mqUser = server.arg("user"); mqPass = server.arg("pass");
    memcpy(mqKey, k, 32); mqCheie = true;
    prefs.putString("mqh", mqHost); prefs.putString("mqu", mqUser); prefs.putString("mqp", mqPass);
    prefs.putBytes("mqk", mqKey, 32);
    mqCod = 1;
    mqErr[0] = 0;
    mqPauzaT = 0;
    mqUrmT = 0;   // loop() porneste legatura imediat ce ii vine randul
  }
  server.send(200, "application/json", "{\"conf\":" + String(mqConfigurat() ? 1 : 0) + ",\"host\":\"" + jEsc(mqHost) +
              "\",\"user\":\"" + jEsc(mqUser) + "\",\"id\":\"" + mqId + "\",\"cod\":" + String(mqCod) + ",\"err\":\"" + jEsc(String(mqErr)) + "\",\"are\":" + String(ARE_MQTT) + "}");
}
