// Paludariu - pompa de ploaie Tuya, comandata local prin WiFi
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.

// ======================= PLOAIE (pompa Tuya, comandata local prin WiFi) =======================
// Placa vorbeste direct cu pompa, in reteaua de acasa: nu trece prin serverele Tuya si merge si fara internet.
// Comenzile se trimit dintr-un fir separat, ca pagina web si releele sa nu astepte dupa pompa.
bool tuyaConfigurat() {
#if FOLOSESTE_PLOAIE
  return strlen(TUYA_KEY) == 16 && strlen(TUYA_ID) > 0 && strlen(TUYA_IP) >= 7;
#else
  return false;
#endif
}

#if FOLOSESTE_PLOAIE
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"

static void tcAes(const uint8_t* key, const uint8_t* in, uint8_t* out, size_t n, bool enc) {
  mbedtls_aes_context c;
  mbedtls_aes_init(&c);
  if (enc) mbedtls_aes_setkey_enc(&c, key, 128); else mbedtls_aes_setkey_dec(&c, key, 128);
  for (size_t i = 0; i < n; i += 16) mbedtls_aes_crypt_ecb(&c, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, in + i, out + i);
  mbedtls_aes_free(&c);
}
static void tcHmac(const uint8_t* key, const uint8_t* d, size_t n, uint8_t* out) {
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, 16, d, n, out);
}
static bool tcGcm(const uint8_t* key, const uint8_t* iv, const uint8_t* aad, size_t aadn,
                  const uint8_t* in, size_t n, uint8_t* out, uint8_t* tag, bool enc) {
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 128) == 0;
  if (ok) ok = enc ? mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, iv, 12, aad, aadn, in, out, 16, tag) == 0
                   : mbedtls_gcm_auth_decrypt(&g, n, iv, 12, aad, aadn, tag, 16, in, out) == 0;
  mbedtls_gcm_free(&g);
  return ok;
}
static void tcRand(uint8_t* b, size_t n) {
  for (size_t i = 0; i < n; i += 4) { uint32_t r = esp_random(); memcpy(b + i, &r, n - i < 4 ? n - i : 4); }
}
static WiFiClient tcCl;
static bool tcConnect() {
  IPAddress ip;
  if (!ip.fromString(TUYA_IP)) return false;
  if (!tcCl.connect(ip, 6668, 3000)) return false;
  tcCl.setNoDelay(true);
  return true;
}
static int tcAvail() { return tcCl.available(); }
static bool tcConnected() { return tcCl.connected(); }
static int tcRead(uint8_t* b, size_t n) { return tcCl.read(b, n); }
static size_t tcWrite(const uint8_t* b, size_t n) { return tcCl.write(b, n); }
static void tcClose() { tcCl.stop(); }

// --- protocolul local Tuya (3.3 / 3.4 / 3.5), fara servere: placa vorbeste direct cu pompa pe portul 6668 ---
static uint8_t tKey[16];          // cheia locala (TUYA_KEY)
static uint8_t tSess[16];         // cheia de sesiune (3.4 / 3.5)
static uint32_t tSeq = 1;
static int tVer = 0;              // versiunea care a mers (33, 34 sau 35); 0 = inca nestiuta
static uint8_t tBuf[640];

static void tPut32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t tGet32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t tCrc(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1))); }
  return ~c;
}

static bool tReadN(uint8_t* b, size_t n, uint32_t tmo) {
  unsigned long t0 = millis();
  size_t got = 0;
  while (got < n) {
    int a = tcAvail();
    if (a > 0) {
      int r = tcRead(b + got, n - got);
      if (r > 0) got += r;
    } else {
      if (!tcConnected()) return false;
      if (millis() - t0 > tmo) return false;
      delay(5);
    }
  }
  return true;
}

// ver 33: cadru 55AA + CRC;  34: 55AA + HMAC-SHA256;  35: 6699 + AES-GCM
static bool tSend(int ver, uint32_t cmd, const uint8_t* pl, size_t n, const uint8_t* key) {
  size_t tot;
  if (ver == 35) {
    tot = 18 + 12 + n + 16 + 4;
    if (tot > sizeof tBuf) return false;
    tPut32(tBuf, 0x00006699); tBuf[4] = 0; tBuf[5] = 0;
    tPut32(tBuf + 6, tSeq++); tPut32(tBuf + 10, cmd); tPut32(tBuf + 14, 12 + n + 16);
    tcRand(tBuf + 18, 12);
    tcGcm(key, tBuf + 18, tBuf + 4, 14, pl, n, tBuf + 30, tBuf + 30 + n, true);
    tPut32(tBuf + tot - 4, 0x00009966);
  } else {
    size_t end = ver == 34 ? 36 : 8;
    tot = 16 + n + end;
    if (tot > sizeof tBuf) return false;
    tPut32(tBuf, 0x000055AA); tPut32(tBuf + 4, tSeq++); tPut32(tBuf + 8, cmd); tPut32(tBuf + 12, n + end);
    memcpy(tBuf + 16, pl, n);
    if (ver == 34) tcHmac(key, tBuf, 16 + n, tBuf + 16 + n);
    else tPut32(tBuf + 16 + n, tCrc(tBuf, 16 + n));
    tPut32(tBuf + tot - 4, 0x0000AA55);
  }
  return tcWrite(tBuf, tot) == tot;
}

// Citeste un mesaj de la pompa. Intoarce lungimea datelor decriptate (fara retcode si fara antetul "3.x"),
// -1 = conexiune inchisa / timp expirat / mesaj stricat, -2 = cheie gresita.
static int tRecv(int ver, const uint8_t* key, uint32_t* cmd, uint32_t* ret, uint8_t* out, size_t outMax, uint32_t tmo) {
  uint8_t h[18];
  if (!tReadN(h, 4, tmo)) return -1;
  for (int g = 0; g < 256 && tGet32(h) != 0x000055AA && tGet32(h) != 0x00006699; g++) {
    memmove(h, h + 1, 3);
    if (!tReadN(h + 3, 1, tmo)) return -1;
  }
  bool g66 = tGet32(h) == 0x00006699;
  if (g66 != (ver == 35)) return -1;
  size_t hl = g66 ? 18 : 16;
  if (!tReadN(h + 4, hl - 4, tmo)) return -1;
  *cmd = tGet32(h + (g66 ? 10 : 8));
  uint32_t len = tGet32(h + (g66 ? 14 : 12));
  size_t rest = g66 ? len + 4 : len;
  if (rest + hl > sizeof tBuf || len < (g66 ? 28u : 8u)) return -1;
  memcpy(tBuf, h, hl);
  uint8_t* b = tBuf + hl;
  if (!tReadN(b, rest, tmo)) return -1;
  *ret = 0;
  uint8_t* p;
  size_t n;
  if (g66) {
    n = len - 28;
    if (n > outMax) return -1;
    if (!tcGcm(key, b, tBuf + 4, 14, b + 12, n, out, b + 12 + n, false)) return -2;
    p = out;
    if (n >= 4) { *ret = tGet32(p); p += 4; n -= 4; }
  } else {
    bool hm = ver == 34;
    size_t end = hm ? 36 : 8;
    if (len < end) return -1;
    size_t reg = len - end;
    if (hm) {
      uint8_t mac[32];
      tcHmac(key, tBuf, 16 + reg, mac);
      if (memcmp(mac, b + reg, 32) != 0) return -2;
    } else if (tGet32(b + reg) != tCrc(tBuf, 16 + reg)) return -1;
    p = b; n = reg;
    if (n >= 4 && (!hm || n % 16 == 4)) { *ret = tGet32(p); p += 4; n -= 4; }
    if (!hm && n >= 15 && p[0] == '3' && p[1] == '.') { p += 15; n -= 15; }   // 3.3: antetul e in clar
    if (n % 16 != 0) {                                                          // text simplu (ex. mesaj de eroare)
      if (n > outMax) return -1;
      memmove(out, p, n);
      return (int)n;
    }
    if (n > outMax) return -1;
    if (n) {
      tcAes(key, p, out, n, false);
      uint8_t pad = out[n - 1];
      if (pad >= 1 && pad <= 16 && pad <= n) n -= pad;
    }
    p = out;
  }
  if (*cmd != 4 && n >= 15 && p[0] == '3' && p[1] == '.') { p += 15; n -= 15; }   // antetul "3.x" criptat
  if (p != out) memmove(out, p, n);
  return (int)n;
}

// 3.4 / 3.5: trei mesaje prin care placa si pompa stabilesc o cheie noua pentru conexiunea asta
static bool tNegociaza(int ver) {
  uint8_t ln[16], rn[16], mac[32], pl[48], out[96];
  uint32_t cmd, ret;
  tcRand(ln, 16);
  if (ver == 34) {
    memcpy(pl, ln, 16); memset(pl + 16, 16, 16);
    tcAes(tKey, pl, pl, 32, true);
    if (!tSend(34, 3, pl, 32, tKey)) return false;
  } else if (!tSend(35, 3, ln, 16, tKey)) return false;
  int n = tRecv(ver, tKey, &cmd, &ret, out, sizeof out, 3000);
  if (n < 48 || cmd != 4) return false;
  tcHmac(tKey, ln, 16, mac);
  if (memcmp(mac, out + 16, 32) != 0) return false;
  memcpy(rn, out, 16);
  tcHmac(tKey, rn, 16, mac);
  if (ver == 34) {
    memcpy(pl, mac, 32); memset(pl + 32, 16, 16);
    tcAes(tKey, pl, pl, 48, true);
    if (!tSend(34, 5, pl, 48, tKey)) return false;
  } else if (!tSend(35, 5, mac, 32, tKey)) return false;
  uint8_t x[16];
  for (int i = 0; i < 16; i++) x[i] = ln[i] ^ rn[i];
  if (ver == 34) tcAes(tKey, x, tSess, 16, true);
  else { uint8_t tag[16]; tcGcm(tKey, ln, nullptr, 0, x, 16, tSess, tag, true); }
  return true;
}

static bool tComanda(int ver, bool on, int sec) {
  char dps[48], js[220];
  if (on && TUYA_DP_TIMER > 0) snprintf(dps, sizeof dps, "{\"%d\":true,\"%d\":%d}", TUYA_DP, TUYA_DP_TIMER, sec);
  else snprintf(dps, sizeof dps, "{\"%d\":%s}", TUYA_DP, on ? "true" : "false");
  long t = (long)time(nullptr);
  uint8_t pl[300];
  size_t n;
  uint32_t cmd;
  if (ver == 33) {
    int m = snprintf(js, sizeof js, "{\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%ld\",\"dps\":%s}", TUYA_ID, TUYA_ID, t, dps);
    if (m <= 0 || m >= (int)sizeof js) return false;
    size_t pn = (m / 16 + 1) * 16;
    memcpy(pl, "3.3", 3); memset(pl + 3, 0, 12);
    memcpy(pl + 15, js, m); memset(pl + 15 + m, pn - m, pn - m);
    tcAes(tKey, pl + 15, pl + 15, pn, true);
    n = 15 + pn; cmd = 7;
    if (!tSend(33, cmd, pl, n, tKey)) return false;
  } else {
    int m = snprintf(js, sizeof js, "{\"protocol\":5,\"t\":%ld,\"data\":{\"dps\":%s}}", t, dps);
    if (m <= 0 || m >= (int)sizeof js) return false;
    memcpy(pl, ver == 34 ? "3.4" : "3.5", 3); memset(pl + 3, 0, 12);
    memcpy(pl + 15, js, m);
    n = 15 + m; cmd = 13;
    if (ver == 34) {
      size_t pn = (n / 16 + 1) * 16;
      memset(pl + n, pn - n, pn - n);
      tcAes(tSess, pl, pl, pn, true);
      n = pn;
    }
    if (!tSend(ver, cmd, pl, n, tSess)) return false;
  }
  unsigned long t0 = millis();
  while (millis() - t0 < 3000) {
    uint32_t c, r;
    uint8_t out[480];
    int k = tRecv(ver, ver == 33 ? tKey : tSess, &c, &r, out, sizeof out, 3000 - (millis() - t0));
    if (k < 0) return false;
    if (r != 0) return false;                         // pompa a refuzat comanda
    if (c == cmd || c == 8 || c == 16) return true;   // confirmare sau starea noua
  }
  return false;
}

// Trimite pornit/oprit. La prima folosire incearca pe rand versiunile 3.3, 3.4 si 3.5 si o tine minte pe cea buna.
static bool tuyaSet(bool on, int sec) {
  const int vs[3] = {33, 34, 35};
  for (int k = 0; k < 3; k++) {
    int v = TUYA_VER ? TUYA_VER : (tVer ? tVer : vs[k]);
    if (!tcConnect()) return false;
    bool ok = (v == 33 || tNegociaza(v)) && tComanda(v, on, sec);
    tcClose();
    if (ok) { tVer = v; return true; }
    if (TUYA_VER || tVer) return false;
    delay(300);
  }
  return false;
}

struct PlCmd { bool on; int16_t sec; };
QueueHandle_t plQ = nullptr;
volatile bool plComOk = true;    // ultima comanda a ajuns la pompa

void plFir(void*) {
  PlCmd c, u;
  for (;;) {
    if (xQueueReceive(plQ, &c, portMAX_DELAY) != pdTRUE) continue;
    while (xQueueReceive(plQ, &u, 0) == pdTRUE) c = u;   // daca s-au strans mai multe, conteaza ultima
    bool ok = false;
    for (int i = 0; i < (c.on ? 3 : 6) && !ok; i++) {    // oprirea o incercam mai insistent
      if (WiFi.status() == WL_CONNECTED) ok = tuyaSet(c.on, c.sec);
      if (!ok) {
        if (uxQueueMessagesWaiting(plQ)) break;           // a venit alta comanda: o luam pe aia
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
    }
    plComOk = ok;
    Serial.printf("Ploaie: %s %s (protocol %d.%d)\n", c.on ? "pornire" : "oprire", ok ? "OK" : "fara raspuns", tVer / 10, tVer % 10);
  }
}

void plInit() {
  if (!tuyaConfigurat()) return;
  memcpy(tKey, TUYA_KEY, 16);
  plQ = xQueueCreate(4, sizeof(PlCmd));
  xTaskCreate(plFir, "ploaie", 8192, nullptr, 1, nullptr);
  PlCmd c = {false, 0};            // la pornirea placii ne asiguram ca pompa e oprita
  xQueueSend(plQ, &c, 0);
}
#else
void plInit() {}
#endif

void ploaieStart(int sec, bool manual) {
#if FOLOSESTE_PLOAIE
  if (!tuyaConfigurat() || !plQ) return;
  sec = constrain(sec, 5, 300);
  if (!ploaieOn) logEvM(4, true, manual);
  ploaieOn = true; ploaieT0 = millis(); ploaieMs = (unsigned long)sec * 1000UL; dupaActiv = false;
  PlCmd c = {true, (int16_t)(sec + 5)};
  xQueueSend(plQ, &c, 0);
  logica();
#endif
}

void ploaieStop(bool manual) {
#if FOLOSESTE_PLOAIE
  if (!tuyaConfigurat() || !plQ) return;
  if (ploaieOn) {
    logEvM(4, false, manual);
    if (S.plVent > 0) { dupaActiv = true; dupaT0 = millis(); }
  }
  ploaieOn = false;
  PlCmd c = {false, 0};
  xQueueSend(plQ, &c, 0);
  logica();
#endif
}

// Se apeleaza des din loop(): opreste ploaia cand i-a trecut timpul
void ploaieLoop() {
  if (ploaieOn && millis() - ploaieT0 >= ploaieMs) ploaieStop(false);
}

// Se apeleaza la 2 secunde: porneste ploaia la orele din setari
void ploaieProgram() {
  if (!tuyaConfigurat() || !S.plAuto) return;
  struct tm t;
  if (!oraLocala(&t)) return;
  static int ziua[3] = {-1, -1, -1};
  int h = t.tm_hour * 60 + t.tm_min;
  for (int k = 0; k < 3; k++) {
    if (S.plOra[k] >= 1440 || h != S.plOra[k] || ziua[k] == t.tm_yday) continue;
    if (vacanta && k > 0) continue;                // in vacanta: o singura ploaie pe zi
    ziua[k] = t.tm_yday;
    if (!isnan(hum) && hum >= S.plMax) continue;   // e deja destul de umed: sarim peste ploaia asta
    if (!ploaieOn) ploaieStart(S.plDur, false);
  }
}

String plUrmatoarea() {   // ora urmatoarei ploi programate, ex. "13:00"
  struct tm t;
  if (!oraLocala(&t)) return "";
  int h = t.tm_hour * 60 + t.tm_min, best = -1, bestD = 99999;
  for (int k = 0; k < 3; k++) {
    if (S.plOra[k] >= 1440) continue;
    int d = (S.plOra[k] - h + 1440) % 1440;
    if (d == 0) d = 1440;
    if (d < bestD) { bestD = d; best = S.plOra[k]; }
  }
  if (best < 0) return "";
  char b[6];
  snprintf(b, sizeof b, "%02d:%02d", best / 60, best % 60);
  return b;
}

String plJson() {
  if (!tuyaConfigurat()) return "null";
  long rest = ploaieOn ? (long)(ploaieMs - (millis() - ploaieT0)) / 1000 : 0;
  if (rest < 0) rest = 0;
  return String("{\"on\":") + (ploaieOn ? "true" : "false") + ",\"auto\":" + (S.plAuto ? "true" : "false") +
#if FOLOSESTE_PLOAIE
         ",\"ok\":" + (plComOk ? "true" : "false") +
#else
         ",\"ok\":false" +
#endif
         ",\"rest\":" + String(rest) + ",\"next\":\"" + plUrmatoarea() + "\"}";
}

Rasp apiPl(const Arg& a) {
  String c = a.get("c");
  if (c == "start") ploaieStart(S.plDur, true);
  else if (c == "stop") ploaieStop(true);
  else if (c == "auto" || c == "pauza") { S.plAuto = c == "auto"; salveaza(); }
  return Rasp{200, "text/plain", "ok"};
}
void hPl() { if (!auth()) return; trimite(apiPl(Arg{"", true})); }
