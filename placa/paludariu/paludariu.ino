// Automatizare paludariu - ESP32 + DHT22/SHT31 + 4 canale + ploaie Tuya + pagina web
// Placa: ESP32 Dev Module, Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)
//   (cu "Huge APP" merge tot, dar fara actualizare prin WiFi; schimbarea se face o singura data, prin cablu)
//
// DATELE TALE (WiFi, parole, Telegram, Tuya, relee, senzori) stau in config.h, in acelasi folder.
// Cand primesti o versiune noua, inlocuiesti toate fisierele din folder, mai putin config.h.
#include "config.h"
#define VERSIUNE "1.8.9"
struct Arg; struct Rasp;   // cererile paginii (vezi mai jos)
struct ZiOpt; struct OptMed; struct Sfat;   // optimizare.ino
extern bool ceataDinTimp;

struct Bula;   // folosita de ecran; declarata devreme ca Arduino IDE sa poata face prototipurile

#include <WiFi.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <OneWire.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DallasTemperature.h>
#include <Wire.h>

#if FOLOSESTE_BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#endif

// ---------- ECRAN TFT (optional, se alege din config.h) ----------
#if FOLOSESTE_DISPLAY
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerifBold12pt7b.h>   // vin cu biblioteca Adafruit GFX
#include <Fonts/FreeSerifBold24pt7b.h>
#if DISPLAY_ST7789
#include <Adafruit_ST7789.h>
#else
#include <Adafruit_ILI9341.h>
#endif
#if FOLOSESTE_TOUCH
#include <XPT2046_Touchscreen.h>
#endif
#endif

#if FOLOSESTE_DHT22
#include <DHT.h>
#else
#include <Adafruit_SHT31.h>
#endif
#include <time.h>

// Memorie luata doar cat e nevoie (in loc de buffere tinute tot timpul); se elibereaza singura.
template <class T> struct Tmp {
  T* p;
  explicit Tmp(size_t n) : p((T*)malloc(n * sizeof(T))) {}
  ~Tmp() { free(p); }
  T& operator[](size_t i) { return p[i]; }
};

// O singura legatura criptata (TLS) se deschide odata: fiecare cere ~45 KB de memorie la deschidere.
// Telegram, GitHub si serverul MQTT isi asteapta randul aici.
SemaphoreHandle_t tlsSem = nullptr;
volatile bool tlsCerut = false;   // MQTT asteapta: ceilalti nu mai incep legaturi noi
bool tlsIa(uint32_t ms) {
  if (!tlsSem) return true;
  return xSemaphoreTake(tlsSem, pdMS_TO_TICKS(ms)) == pdTRUE;
}
void tlsLasa() { if (tlsSem) xSemaphoreGive(tlsSem); }
// Pentru Telegram si GitHub: lasa intai MQTT (cel mult 15 s), apoi asteapta randul.
struct TlsRand {
  bool ok;
  explicit TlsRand(uint32_t ms) {
    for (int i = 0; i < 150 && tlsCerut; i++) vTaskDelay(pdMS_TO_TICKS(100));
    ok = tlsIa(ms);
  }
  void lasa() { if (ok) { ok = false; tlsLasa(); } }
  ~TlsRand() { lasa(); }
};

// ---------- PINI ----------
const int PIN_SDA = 21, PIN_SCL = 22;
//                       pompa lumina ceata ventilator
const int  PIN[4]       = {16,   17,   13,   25};   // ceata = GPIO13, ventilator = GPIO25 (GPIO18/19 raman libere pentru ecran)
// ACTIV_LOW (cum se aprind releele) se alege din config.h

// Senzor nivel apa (optional, se porneste din config.h): flotor intre GPIO32 si GND, contact inchis = apa OK.
const int PIN_NIVEL = 32;

const char* NUME[4] = {"Pompa", "Lumina", "Ceata", "Ventilator"};

// ---------- RACIRE LED (radiator) ----------
const int PIN_TLED = 21;     // DS18B20 pe radiatorul LED-ului (DATA); rezistenta 4,7 kohm intre 3V3 si DATA
// NTC in loc de DS18B20: se alege din config.h (SENZOR_LED_NTC si valorile NTC_...)
const int PIN_NTC = 34;      // doar 32-39 merg cu WiFi pornit; 34, 35, 36, 39 sunt libere
const int PIN_FANLED = 22;   // PWM catre ventilatorul radiatorului (pinul PWM al ventilatorului cu 4 fire sau modul MOSFET)
#define FANLED_CH 2          // canal PWM (ESP32 Arduino core 2.x); nu-l pune pe 0/1, folosite de lumina ecranului
#define FANLED_FREQ 25000    // 25 kHz = standardul ventilatoarelor PC cu 4 fire
#define FANLED_ONOFF false   // true = ventilator comandat prin SSR/releu: doar pornit/oprit, fara reglare de viteza
// Turatia ventilatorului (firul galben, "tach") pe GPIO35, cu rezistenta 10 kohm intre GPIO35 si 3V3.
// Optiunile se pun in config.h; daca lipsesc de acolo, se folosesc valorile de mai jos.
#ifndef FANLED_TACH
#define FANLED_TACH false    // true = firul de turatie e legat si placa verifica daca ventilatorul se invarte
#endif
#ifndef FANLED_4PIN
#define FANLED_4PIN false    // true = ventilator cu 4 fire (PWM): turatia se citeste la orice viteza
#endif                       // false = ventilator cu 3 fire: turatia se citeste doar la 100%, cu un test scurt periodic
#ifndef FANLED_PULSURI
#define FANLED_PULSURI 2     // impulsuri pe rotatie (aproape toate ventilatoarele PC: 2)
#endif
#ifndef FANLED_RPM_MIN
#define FANLED_RPM_MIN 300   // sub atatea rotatii pe minut ventilatorul e considerat oprit
#endif
#ifndef FANLED_TEST_MIN
#define FANLED_TEST_MIN 30   // ventilator cu 3 fire: la cate minute se face testul de 10 secunde la 100%
#endif
const int PIN_TACH = 35;
OneWire owLed(PIN_TLED);
DallasTemperature dsLed(&owLed);

// ---------- SETARI (salvate in flash) ----------
struct Setari {
  uint8_t ver;
  float mistOn, mistOff;     // ceata: porneste sub mistOn, opreste peste mistOff
  float fanOn, fanOff;       // ventilator: porneste peste fanOn, opreste sub fanOff
  int lumOra, lumStop;       // lumina: pornire / oprire, in minute de la miezul noptii (08:00 = 480)
  int pompaOn, pompaOff;     // minute; pompaOff = 0 => continuu
  int circLa, circDur;       // circulatie aer: la fiecare X min, timp Y min (0 = dezactivat)
  int standbyMin;            // ecran: standby dupa X minute fara atingere (0 = dezactivat)
  int ledFanOn, ledFanMax, ledCutoff;   // racire LED: pornire ventilator, viteza maxima, oprire lumina (grade C)
  uint8_t mod[4];            // 0 = auto, 1 = pornit, 2 = oprit
  // Ploaie: adaugate la sfarsit, ca setarile de dinainte sa se pastreze la actualizare
  uint8_t plAuto;            // 1 = ploua singura la orele de mai jos
  int plOra[3];              // orele de ploaie, in minute de la miezul noptii; 1440 = oprit
  int plDur;                 // cate secunde ploua
  int plMax;                 // nu ploua daca umiditatea e deja peste atat (%)
  int plVent;                // minute de ventilator dupa ploaie (0 = deloc)
  // Umiditate dorita (1.8.8): o singura valoare, iar pragurile de ceata si ventilator se calculeaza din ea
  uint8_t umAuto;            // 1 = pragurile se calculeaza singure din umTinta
  uint8_t umStrict;          // cat de strict: 2, 3 sau 4 (%)
  int umTinta;               // umiditatea dorita (70-95 %)
  // 1.8.9
  uint8_t ledFanMin;         // viteza minima a ventilatorului lampii cat timp merge (0-60 %)
  uint8_t hranZile;          // zilele de hranire (biti: luni = 1 ... duminica = 64)
  uint8_t hranOra;           // ora mementoului de hranire (0-23)
  uint8_t rez1;
  uint16_t oreLim[3];        // intretinere dupa ore: membrana ceata, pompa + sita, ventilator lampa (0 = oprit)
};
Setari S;
const uint8_t VER = 7;

Preferences prefs;
WebServer server(80);

// Parola paginii. Dupa 5 parole gresite la rand, placa nu mai primeste incercari 30 de secunde.
uint8_t authEsec = 0;
unsigned long authBlocT = 0;
// "Doar privit" (privit.ino): a doua parola, pentru familie
String vPass = "";              // parola "doar privit" (goala = oprit)
uint8_t mqKey2[32];             // cheia lor pentru internet
bool mqCheie2 = false;
uint32_t vizUlt = 0;            // ultima lor vizita (epoch)
uint8_t vizCale = 0;            // 1 = prin WiFi, 2 = prin internet
uint8_t nivelCerere = 0;        // cererea de acum: 0 = proprietarul, 1 = doar privit
bool privitPermis(const String& p, bool faraArgs);
void vizNoteaza(uint8_t cale);
bool auth() {
  nivelCerere = 0;
  if (authBlocT && millis() - authBlocT < 30000UL) {
    server.send(429, "text/plain; charset=utf-8", "Prea multe parole greșite. Încearcă din nou peste 30 de secunde.");
    return false;
  }
  authBlocT = 0;
  if (server.authenticate(WEB_USER, WEB_PASS)) { authEsec = 0; return true; }
  if (vPass.length() && server.authenticate("privit", vPass.c_str())) {
    authEsec = 0; nivelCerere = 1; vizNoteaza(1);
    if (privitPermis(server.uri(), server.args() == 0)) return true;
    server.send(403, "text/plain; charset=utf-8", "Ai acces doar să privești: nu poți schimba nimic.");
    return false;
  }
  if (server.hasHeader("Authorization") && ++authEsec >= 5) {   // a trimis o parola si a gresit-o
    authEsec = 0; authBlocT = millis() ? millis() : 1;
    server.send(429, "text/plain; charset=utf-8", "Prea multe parole greșite. Încearcă din nou peste 30 de secunde.");
    return false;
  }
  server.requestAuthentication(BASIC_AUTH, "Paludariu");
  return false;
}

// ---------- cererile paginii: aceleasi pe WiFi (server web) si prin internet (MQTT) ----------
String urlDec(const String& v) {
  String o;
  for (unsigned i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '+') o += ' ';
    else if (c == '%' && i + 2 < v.length()) { o += (char)strtol(v.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2; }
    else o += c;
  }
  return o;
}
struct Arg {
  String q;      // parametrii "a=1&b=2", la cererile venite prin internet
  bool web;      // true = cererea vine prin serverul web, iar parametrii sunt in server.arg()
  bool gaseste(const String& k, String* out) const {
    int i = 0, n = q.length();
    while (i < n) {
      int amp = q.indexOf('&', i); if (amp < 0) amp = n;
      int eq = q.indexOf('=', i);
      bool cuValoare = eq >= 0 && eq < amp;
      if (q.substring(i, cuValoare ? eq : amp) == k) {
        if (out) *out = cuValoare ? urlDec(q.substring(eq + 1, amp)) : String("");
        return true;
      }
      i = amp + 1;
    }
    return false;
  }
  bool has(const String& k) const { return web ? server.hasArg(k) : gaseste(k, nullptr); }
  String get(const String& k) const { if (web) return server.arg(k); String v; gaseste(k, &v); return v; }
};
struct Rasp { int cod; const char* tip; String corp; };
void trimite(const Rasp& r) { server.send(r.cod, r.tip, r.corp); }
#if FOLOSESTE_DHT22
const int PIN_DHT = 4;
DHT dht(PIN_DHT, DHT22);
float citesteTemp() { return dht.readTemperature(); }
float citesteUmid() { return dht.readHumidity(); }
#else
Adafruit_SHT31 sht;
float citesteTemp() { return sht.readTemperature(); }
float citesteUmid() { return sht.readHumidity(); }
#endif

float temp = NAN, hum = NAN;
float tLed = NAN;            // temperatura radiatorului LED
int fanLedPct = 0;           // viteza ventilatorului de racire (0-100)
bool ledOverheat = false;    // supraincalzire: lumina fortata oprita
int fanRpm = -1;             // ultima turatie masurata a ventilatorului de racire (-1 = nemasurata)
bool fanRpmLive = false;     // true = turatia de mai sus e masurata chiar acum
bool fanFail = false;        // ventilatorul trebuia sa se invarta si nu se invarte: lumina fortata oprita

// Istoric 24 h: un punct la 5 minute, tinut in RAM (se goleste la repornire)
const int H_N = 288;
int16_t histT[H_N], histH[H_N];   // valori x10; INT16_MIN = lipsa
int histCount = 0, histHead = 0;
unsigned long tHist = 0;
uint32_t acum();
void h30Adauga(uint32_t e, float t, float h);
// Istoricul se salveaza in memoria permanenta o data pe ora, ca sa nu se piarda la o pana de curent sau la un upload.
uint32_t histEpoch = 0;       // ora reala (epoch) a ultimului punct
bool histAliniat = true;      // dupa pornire: golul cat placa a fost oprita se completeaza cu "lipsa"
int histDeSalvat = 0;
void histPune(int16_t t, int16_t h) {
  histT[histHead] = t; histH[histHead] = h;
  histHead = (histHead + 1) % H_N;
  if (histCount < H_N) histCount++;
}
void histSalveaza() {
  if (histEpoch < 1000000000UL || histCount == 0) return;
  Tmp<int16_t> buf(2 * H_N + 4);
  if (!buf.p) return;
  buf[0] = histEpoch & 0xFFFF; buf[1] = histEpoch >> 16; buf[2] = histCount; buf[3] = 0;
  for (int i = 0; i < histCount; i++) {
    int k = (histHead - histCount + i + H_N) % H_N;
    buf[4 + i] = histT[k]; buf[4 + H_N + i] = histH[k];
  }
  prefs.putBytes("hist", buf.p, (2 * H_N + 4) * sizeof(int16_t));
  histDeSalvat = 0;
}
void histIncarca() {
  Tmp<int16_t> buf(2 * H_N + 4);
  const size_t L = (2 * H_N + 4) * sizeof(int16_t);
  if (!buf.p || prefs.getBytes("hist", buf.p, L) != L) return;
  int n = buf[2];
  if (n <= 0 || n > H_N) return;
  histEpoch = (uint16_t)buf[0] | ((uint32_t)(uint16_t)buf[1] << 16);
  histCount = 0; histHead = 0;
  for (int i = 0; i < n; i++) histPune(buf[4 + i], buf[4 + H_N + i]);
  histAliniat = false;   // completam golul cand aflam ora (dupa NTP)
}
void inregistreaza() {
  uint32_t e = acum();
  bool oraOk = e > 1000000000UL;
  if (!histAliniat && oraOk) {
    long gol = histEpoch ? ((long)e - (long)histEpoch) / 300 : 0;
    if (gol >= H_N) { histCount = 0; histHead = 0; }
    else for (long g = 1; g < gol; g++) histPune(INT16_MIN, INT16_MIN);
    histAliniat = true;
  }
  histPune(isnan(temp) ? INT16_MIN : (int16_t)lroundf(temp * 10), isnan(hum) ? INT16_MIN : (int16_t)lroundf(hum * 10));
  if (oraOk) histEpoch = e;
  if (++histDeSalvat >= 12) histSalveaza();   // o data pe ora
  if (oraOk) h30Adauga(e, temp, hum);
}
bool stare[4] = {false, false, false, false};

// ---------- ISTORIC 30 DE ZILE: o medie pe ora, salvata in memoria permanenta la fiecare ora ----------
const int H30_N = 720;
int16_t h30T[H30_N], h30H[H30_N];   // valori x10; INT16_MIN = lipsa
int h30Count = 0, h30Head = 0;
uint32_t h30Last = 0;               // ora (epoch / 3600) ultimului punct salvat
uint32_t h30Ora = 0;                // ora care se aduna acum
int32_t h30ST = 0, h30SH = 0;
int h30NT = 0, h30NH = 0;
void h30Pune(int16_t t, int16_t h) {
  h30T[h30Head] = t; h30H[h30Head] = h;
  h30Head = (h30Head + 1) % H30_N;
  if (h30Count < H30_N) h30Count++;
}
void h30Salveaza() {
  Tmp<int16_t> buf(2 * H30_N + 4);
  if (!buf.p) return;
  buf[0] = h30Last & 0xFFFF; buf[1] = h30Last >> 16; buf[2] = h30Count; buf[3] = 0;
  for (int i = 0; i < h30Count; i++) {
    int k = (h30Head - h30Count + i + H30_N) % H30_N;
    buf[4 + i] = h30T[k]; buf[4 + H30_N + i] = h30H[k];
  }
  prefs.putBytes("h30", buf.p, (2 * H30_N + 4) * sizeof(int16_t));
}
void h30Incarca() {
  Tmp<int16_t> buf(2 * H30_N + 4);
  const size_t L = (2 * H30_N + 4) * sizeof(int16_t);
  if (!buf.p || prefs.getBytes("h30", buf.p, L) != L) return;
  int n = buf[2];
  if (n <= 0 || n > H30_N) return;
  h30Last = (uint16_t)buf[0] | ((uint32_t)(uint16_t)buf[1] << 16);
  h30Count = 0; h30Head = 0;
  for (int i = 0; i < n; i++) h30Pune(buf[4 + i], buf[4 + H30_N + i]);
}
void h30Inchide(uint32_t ora) {     // ora s-a terminat: pune media ei in istoric
  if (ora > h30Last) {
    if (h30Last) {
      uint32_t gol = ora - h30Last - 1;   // orele in care placa a fost oprita
      if (gol >= (uint32_t)H30_N) { h30Count = 0; h30Head = 0; }
      else for (uint32_t g = 0; g < gol; g++) h30Pune(INT16_MIN, INT16_MIN);
    }
    h30Pune(h30NT ? (int16_t)(h30ST / h30NT) : INT16_MIN, h30NH ? (int16_t)(h30SH / h30NH) : INT16_MIN);
    h30Last = ora;
    h30Salveaza();
  }
  h30ST = h30SH = 0; h30NT = h30NH = 0;
}
void h30Adauga(uint32_t e, float t, float h) {
  uint32_t ora = e / 3600;
  if (h30Ora && ora != h30Ora) h30Inchide(h30Ora);
  h30Ora = ora;
  if (!isnan(t)) { h30ST += lroundf(t * 10); h30NT++; }
  if (!isnan(h)) { h30SH += lroundf(h * 10); h30NH++; }
}

// ---------- INTRETINERE: mementouri (apa la ceata, hranire, geam, apa, pompa) ----------
const int MEM_N = 5;
const char* MEM_NUME[MEM_N] = {"Apă la ceață", "Hrănit crabii", "Curățat geamul", "Schimbat apa", "Curățat pompa"};
struct Mem { int16_t zile[MEM_N]; uint32_t ult[MEM_N]; uint32_t anuntat[MEM_N]; };
Mem M = {{3, 2, 7, 14, 30}, {0}, {0}};
// Mod vacanta: o singura ploaie pe zi (prima ora din program), alerte mai stricte si raport zilnic pe Telegram la 20:00
bool vacanta = false;   // zile intre doua dati (0 = oprit), cand s-a facut ultima data
void memSalveaza() { prefs.putBytes("mem", &M, sizeof(M)); }
void memIncarca() {
  Mem tmp;
  if (prefs.getBytes("mem", &tmp, sizeof(tmp)) == sizeof(tmp)) M = tmp;
}
// secunde pana trebuie facut (negativ = a trecut termenul); false daca e oprit sau nu stim ora
bool memRamas(int i, long& r) {
  time_t n = time(nullptr);
  if (M.zile[i] <= 0 || n < 1000000000 || M.ult[i] == 0) return false;
  r = (long)M.ult[i] + (long)M.zile[i] * 86400L - (long)n;
  return true;
}
String memJson() {
  String j = "[";
  for (int i = 0; i < MEM_N; i++) {
    long r;
    j += "{\"n\":\"" + String(MEM_NUME[i]) + "\",\"z\":" + String(M.zile[i]) + ",\"r\":" + (memRamas(i, r) ? String(r) : String("null")) + "}";
    if (i < MEM_N - 1) j += ",";
  }
  return j + "]";
}
void logEvM(int i, bool on, uint8_t man);
// rezumatul noptii (noapte.ino)
struct Noapte {
  uint32_t start;             // cand a inceput noaptea (epoch); 0 = nimic adunat
  uint32_t gata;              // cand s-a inchis (epoch)
  int16_t tMin, tMax, hMin, hMax;   // x10
  uint32_t tMinT;             // la ce ora a fost cel mai frig
  uint32_t cfSum, cfN;        // confortul, adunat la fiecare citire
  uint16_t ceata;             // porniri de ceata
  uint16_t alerte;            // biti AL_... (telegram.ino)
  uint8_t rep;                // reporniri in timpul noptii
  uint8_t rez[3];
};

void noapteGol(Noapte& x);
String noapteJson(const Noapte& x, bool inCurs);
// intretinere dupa ore de mers (intretinere.ino)
const int ORE_N = 3;
struct Ore { uint32_t min[ORE_N]; uint32_t ult[ORE_N]; uint32_t anuntat[ORE_N]; };   // minute de mers, ultima curatare, ultimul memento
extern Ore O;
String oreJson();
bool oreGata(int k);
void oreSalveaza();
bool memGata(int i) {
  time_t n = time(nullptr);
  if (i < 0 || i >= MEM_N || n < 1000000000) return false;
  M.ult[i] = (uint32_t)n; M.anuntat[i] = 0;
  memSalveaza();
  logEvM(10 + i, true, true);   // in jurnal: 10 + numarul mementoului
  return true;
}

// ---------- WiFi configurat din aplicatie ----------
// Fara retea salvata (sau daca nu se poate conecta), placa isi face reteaua "Paludariu-Setup",
// iar aplicatia ii trimite WiFi-ul de acasa. Datele primite au prioritate fata de config.h.
#define SETUP_SSID "Paludariu-Setup"
#ifndef SETUP_PAROLA
#define SETUP_PAROLA "paludariu"   // se poate schimba din config.h; atunci aplicatia o cere la "Placa noua"
#endif
#define SETUP_PASS SETUP_PAROLA
const int PIN_BOOT = 0;       // butonul BOOT de pe placa: tinut apasat 5 s = uita WiFi-ul
String wSsid, wPass;          // reteaua primita din aplicatie
String numePlaca, mdnsNume;   // numele placii (din aplicatie sau din config.h)
bool modSetup = false;        // reteaua Paludariu-Setup e pornita
int setupSt = 0;              // 0 = asteapta, 1 = se conecteaza, 2 = conectat, 3 = n-a mers
String setupSsid, setupPass, setupNume;
unsigned long setupT0 = 0, restartLa = 0;
bool staConfigurat = false;  // placa are o retea de acasa la care sa incerce

// Ploaie (pompa Tuya). Starea e aici, sus, ca s-o vada si logica ventilatorului si a cetii.
bool ploaieOn = false;                      // ploua acum
unsigned long ploaieT0 = 0, ploaieMs = 0;   // cand a inceput si cat dureaza
bool dupaActiv = false;                     // ventilatorul merge dupa ploaie
unsigned long dupaT0 = 0;
bool tuyaConfigurat();
void ploaieStart(int sec, bool manual);
void ploaieStop(bool manual);
String plJson();
void logEvM(int i, bool on, uint8_t man);

// Jurnal evenimente: ultimele 120 porniri/opriri, tinute in RAM (se golesc la repornire)
struct Ev { uint32_t t; uint8_t out, on, man; int16_t h; };
const int EV_N = 120;
Ev evl[EV_N];
int evCount = 0, evHead = 0;
uint32_t acum() {   // ora reala (epoch) daca e sincronizata, altfel secunde de la pornire
  time_t n = time(nullptr);
  return n > 1000000000 ? (uint32_t)n : (uint32_t)(millis() / 1000);
}
// man: 0 automat, 1 manual, 2 ceata pornita din timp (optimizare)
void logEv(int i, bool on) { logEvM(i, on, (i < 4 && S.mod[i] != 0) ? 1 : (i == 2 && on && ceataDinTimp) ? 2 : 0); }
bool evSchimbat = false;      // jurnalul are intrari noi, nesalvate inca
void logEvM(int i, bool on, uint8_t man) {
  evSchimbat = true;
  if (i < 5) optEveniment(i, on);
  if (i == 2 && on) noapteCeata();
  Ev& e = evl[evHead];
  e.t = acum(); e.out = i; e.on = on; e.man = man;
  e.h = isnan(hum) ? -1 : (int16_t)lroundf(hum * 10);
  evHead = (evHead + 1) % EV_N;
  if (evCount < EV_N) evCount++;
}
// Jurnalul se salveaza in memoria permanenta la 10 minute (si inainte de o repornire facuta de placa),
// ca sa nu se piarda la o pana de curent. Se pastreaza doar intrarile cu ora reala.
void evSalveaza() {
  Tmp<Ev> buf(EV_N);
  if (!buf.p) return;
  int n = 0;
  for (int k = 0; k < evCount; k++) {
    Ev& e = evl[(evHead - evCount + k + EV_N) % EV_N];
    if (e.t > 1000000000UL) buf[n++] = e;
  }
  if (n) prefs.putBytes("ev", buf.p, n * sizeof(Ev)); else prefs.remove("ev");
  evSchimbat = false;
}
void evIncarca() {
  size_t len = prefs.getBytesLength("ev");
  if (!len || len % sizeof(Ev) || len > sizeof(evl)) return;
  Tmp<Ev> buf(EV_N);
  if (!buf.p || prefs.getBytes("ev", buf.p, len) != len) return;
  for (size_t i = 0; i < len / sizeof(Ev); i++) {
    evl[evHead] = buf[i];
    evHead = (evHead + 1) % EV_N;
    if (evCount < EV_N) evCount++;
  }
}
bool mistState = false, fanState = false;

// Senzorul de umiditate pare blocat: arata peste 98% de 45 de minute, sau exact aceeasi valoare de 3 ore.
// Atunci avertizam (pagina, ecran, Telegram), iar optimizarea nu invata din citirile lui.
bool umBlocat = false;
uint8_t umBlocatMotiv = 0;   // 1 = mereu peste 98%, 2 = aceeasi valoare de mult timp
void umBlocatVerifica() {
  static unsigned long tSus = 0, tLa = 0; static float hLa = NAN;
  unsigned long now = millis();
  if (isnan(hum)) { tSus = 0; tLa = 0; hLa = NAN; umBlocat = false; umBlocatMotiv = 0; return; }
  if (hum >= 98.0f) { if (!tSus) tSus = now ? now : 1; } else tSus = 0;
  if (isnan(hLa) || fabsf(hum - hLa) >= 0.05f) { hLa = hum; tLa = now ? now : 1; }
  bool sus = tSus && now - tSus >= 45UL * 60000UL;
  bool fix = tLa && now - tLa >= 3UL * 3600000UL;
  umBlocat = sus || fix;
  umBlocatMotiv = sus ? 1 : fix ? 2 : 0;
}
unsigned long tSenzor = 0;

void valoriImplicite() {
  S.ver = VER;
  S.mistOn = 85; S.mistOff = 92;
  S.fanOn = 95;  S.fanOff = 90;
  S.lumOra = 480;  S.lumStop = 1200;
  S.pompaOn = 15; S.pompaOff = 0;
  S.circLa = 60; S.circDur = 5; S.standbyMin = 10;
  S.ledFanOn = 40; S.ledFanMax = 60; S.ledCutoff = 75;
  for (int i = 0; i < 4; i++) S.mod[i] = 0;
  S.plAuto = 1; S.plOra[0] = 540; S.plOra[1] = 780; S.plOra[2] = 1080;   // 09:00, 13:00, 18:00
  S.plDur = 30; S.plMax = 95; S.plVent = 5;
  S.umAuto = 1; S.umStrict = 3; S.umTinta = 86;
  S.ledFanMin = 30; S.hranZile = 1 | 4 | 16; S.hranOra = 19; S.rez1 = 0;   // luni, miercuri, vineri, seara
  S.oreLim[0] = 300; S.oreLim[1] = 500; S.oreLim[2] = 2000;
  umCalc();
}

// Pragurile calculate din umiditatea dorita H si cat de strict T:
// ceata porneste sub H-T si se opreste peste H+T; ventilatorul porneste peste H+T+4 si se opreste sub H+T+1
float umBaza(int p) {
  int H = S.umTinta, T = S.umStrict, fOn = min(99, H + T + 4);
  return p == 0 ? H - T : p == 1 ? H + T : p == 2 ? fOn : min(fOn - 2, H + T + 1);
}
void umCalc() {
  S.umTinta = constrain(S.umTinta, 70, 95);
  S.umStrict = constrain((int)S.umStrict, 2, 4);
  S.mistOn = umBaza(0); S.mistOff = umBaza(1); S.fanOn = umBaza(2); S.fanOff = umBaza(3);
}

void salveaza() { prefs.putBytes("s", &S, sizeof(S)); }

void incarca() {
  valoriImplicite();
  Setari tmp = S;
  size_t n = prefs.getBytesLength("s");
  if (n == sizeof(tmp)) {
    if (prefs.getBytes("s", &tmp, n) == n && tmp.ver == VER) S = tmp;
  } else if (n > 0 && n < sizeof(tmp)) {
    // setari salvate de versiunea de dinainte: le pastram, iar campurile noi raman la valorile implicite
    if (prefs.getBytes("s", &tmp, n) == n && (tmp.ver == VER - 1 || tmp.ver == VER - 2)) {
      bool din187 = tmp.ver == VER - 2;
      tmp.ver = VER; S = tmp;
      // de la 1.8.7: tinta porneste din pragurile pe care le aveai, ca umiditatea sa ramana cam la fel
      if (din187) { S.umAuto = 1; S.umStrict = 3; S.umTinta = (int)lroundf((S.mistOn + S.mistOff) / 2); umCalc(); }
      salveaza();
    }
  }
}

// Ora locala, fara asteptare. Nu folosim getLocalTime(&t, 0): din cand in cand raspunde "nu stiu ora"
// (daca trece exact o milisecunda in timpul apelului), iar lumina pe AUTO se stingea o clipa.
bool oraLocala(struct tm* t) {
  time_t acum = time(nullptr);
  localtime_r(&acum, t);
  return t->tm_year > (2016 - 1900);
}

void scrieIesire(int i, bool on) {
  if (stare[i] != on) logEv(i, on);
  stare[i] = on;
  digitalWrite(PIN[i], (on == ACTIV_LOW[i]) ? LOW : HIGH);
}

// ---------- LOGICA DE CONTROL ----------
void fanLedWrite(int pct) {
  int duty = constrain(pct, 0, 100) * 255 / 100;
  if (FANLED_ONOFF) duty = pct > 0 ? 255 : 0;   // SSR/releu: fara PWM
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_FANLED, duty);
#else
  ledcWrite(FANLED_CH, duty);
#endif
}

// NTC pe radiator: media a 16 citiri, apoi formula cu constanta B. -127 = fir rupt sau scurtcircuit.
float citesteNtc() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_NTC);
  float v = mv / 16.0f;
  if (v < 40 || v > 3200) return -127;
  float r = NTC_RFIX * v / (3300.0f - v);
  float t = 1.0f / (1.0f / 298.15f + logf(r / NTC_R25) / NTC_B) - 273.15f;
  return t + NTC_CORECTIE;
}

String ledSrcJson() {   // ce senzor e pe radiator, pentru pagina "Paludariul tau"
  if (!SENZOR_LED_NTC) return "";
  String r = ",\"src\":\"NTC · GPIO";
  r += PIN_NTC;
  r += "\"";
  return r;
}

// ---------- turatia ventilatorului de racire ----------
volatile uint32_t tachPulsuri = 0, tachUltim = 0;
void IRAM_ATTR tachIsr() {
  uint32_t m = micros();
  if (m - tachUltim >= 2000) { tachPulsuri++; tachUltim = m; }   // filtru: ignora zgomotul mai des de 2 ms
}
int fanAplicat = -1;                 // viteza trimisa efectiv ventilatorului
unsigned long fanPornitT = 0;        // cand a pornit (sau a trecut la 100%): asteptam sa prinda turatie
bool fanTest = false;                // test in curs (ventilator cu 3 fire)
// 1.8.9: viteza aleasa de mana (revine pe automat dupa o ora) si proba de 10 secunde la maxim
int fanMan = -1;                     // -1 = automat, altfel 0-100 %
unsigned long fanManT = 0;
const unsigned long FAN_MAN_MS = 3600000UL;
bool fanProba = false;
unsigned long fanProbaT = 0;
int fanProbaRpm = -2;                // -2 = nefacuta, -1 = fara fir de turatie, altfel rpm
unsigned long fanTestT0 = 0, fanTestUltim = 0, tachT = 0;
int tachRau = 0, tachBun = 0;

void fanAplica() {
  int p = (fanTest || fanFail || fanProba) ? 100 : fanLedPct;
  if (FANLED_ONOFF && p > 0) p = 100;
  if (p == fanAplicat) return;
  if ((fanAplicat <= 0 && p > 0) || (p == 100 && fanAplicat != 100)) fanPornitT = millis();
  fanAplicat = p;
  fanLedWrite(p);
}

// Se apeleaza din loop(): numara impulsurile o data pe secunda si decide daca ventilatorul s-a oprit
void tachLoop() {
#if FANLED_TACH
  unsigned long now = millis();
  // Ventilator cu 3 fire: un test de 10 s la 100%, cand e nevoie de el (lumina aprinsa sau radiator cald)
  bool simplu = !FANLED_4PIN && !FANLED_ONOFF;
  bool nevoie = stare[1] || fanLedPct > 0;
  if (simplu && !fanTest && !fanFail && nevoie && fanAplicat < 100 &&
      (fanTestUltim == 0 || now - fanTestUltim >= (unsigned long)FANLED_TEST_MIN * 60000UL) && now > 20000) {
    fanTest = true; fanTestT0 = now; tachRau = 0;
    fanAplica();
  }
  if (fanTest && now - fanTestT0 >= 10000) {
    fanTest = false; fanTestUltim = now;
    fanAplica();
  }
  if (now - tachT < 1000) return;
  unsigned long dt = now - tachT;
  tachT = now;
  noInterrupts();
  uint32_t p = tachPulsuri; tachPulsuri = 0;
  interrupts();
  int rpm = (int)(p * 60000UL / dt / FANLED_PULSURI);
  bool masurabil = (simplu ? fanAplicat == 100 : fanAplicat > 0) && now - fanPornitT >= 4000;
  if (fanAplicat == 0) { fanRpm = 0; fanRpmLive = true; tachRau = 0; return; }
  fanRpmLive = masurabil;
  if (!masurabil) return;
  fanRpm = rpm;
  if (rpm < FANLED_RPM_MIN) { tachBun = 0; if (++tachRau >= 5 && !fanFail) { fanFail = true; fanAplica(); } }
  else { tachRau = 0; if (fanFail && ++tachBun >= 10) { fanFail = false; tachBun = 0; fanTestUltim = now; fanAplica(); } }
#endif
}

int fanAuto = 0;                     // ce ar alege automatul acum
void controlLed() {
  int mn = constrain((int)S.ledFanMin, 0, 60);
  if (isnan(tLed)) {
    fanAuto = 100;                         // senzor lipsa: ventilatorul merge la maxim (siguranta)
  } else {
    float on = S.ledFanOn, mx = S.ledFanMax;
    if (mx <= on) mx = on + 1;
    if (tLed >= mx) fanAuto = 100;
    else if (tLed >= on) fanAuto = mn + (int)((100 - mn) * (tLed - on) / (mx - on));
    else if (fanAuto > 0 && tLed > on - 3) fanAuto = mn;   // histerezis 3 grade
    else fanAuto = 0;
    if (tLed >= S.ledCutoff) ledOverheat = true;                        // supraincalzire: opreste lumina
    else if (tLed <= S.ledCutoff - 10) ledOverheat = false;
  }
  fanLedPct = fanAuto;
  // viteza de mana: o ora, apoi inapoi pe automat. Siguranta ramane: radiator la "viteza maxima" sau supraincalzit = 100 %
  if (fanMan >= 0 && millis() - fanManT >= FAN_MAN_MS) fanMan = -1;
  if (fanMan >= 0) {
    bool fierbinte = !isnan(tLed) && (tLed >= S.ledFanMax || ledOverheat);
    if (!fierbinte) fanLedPct = fanMan;
  }
  fanAplica();
}
// proba de 10 secunde la maxim: se masoara turatia (daca firul de turatie e legat)
void fanLoop() {
  if (!fanProba) return;
  if (FANLED_TACH && fanRpmLive && millis() - fanProbaT >= 5000 && fanRpm > fanProbaRpm) fanProbaRpm = fanRpm;
  if (millis() - fanProbaT < 10000) return;
  fanProba = false;
  if (!FANLED_TACH) fanProbaRpm = -1;
  else if (fanProbaRpm < 0) fanProbaRpm = 0;
  fanAplica();
}
String fanJson() {
  long r = fanMan >= 0 ? (long)((FAN_MAN_MS - (millis() - fanManT)) / 1000) : 0;
  return ",\"man\":" + String(fanMan) + ",\"mr\":" + String(r < 0 ? 0 : r) + ",\"au\":" + String(fanAuto) +
         ",\"pr\":" + String(fanProba ? (long)((10000 - min(10000UL, millis() - fanProbaT)) / 1000) + 1 : 0) + ",\"prpm\":" + String(fanProbaRpm) +
         ",\"min\":" + String(S.ledFanMin) + ",\"onoff\":" + String(FANLED_ONOFF ? 1 : 0);
}
// /fan?p=0..100 = viteza de mana pentru o ora; /fan?auto=1 = inapoi pe automat; /fan?test=1 = 10 secunde la maxim
Rasp apiFan(const Arg& a) {
  if (a.has("p")) { fanMan = constrain((int)a.get("p").toInt(), 0, 100); fanManT = millis(); controlLed(); }
  if (a.has("auto")) { fanMan = -1; controlLed(); }
  if (a.has("test") && !fanProba) {
    fanProba = true; fanProbaT = millis(); fanProbaRpm = -2;
#if FANLED_TACH
    fanRpmLive = false;
#endif
    fanAplica();
  }
  return Rasp{200, "application/json", "{\"f\":" + String(fanAplicat < 0 ? 0 : fanAplicat) + ",\"rpm\":" + String(fanRpm) + ",\"rl\":" + String(fanRpmLive ? 1 : 0) + fanJson() + "}"};
}
void hFan() { if (!auth()) return; trimite(apiFan(Arg{"", true})); }

// ---------- pornit/oprit de mana pentru cateva minute (butoanele rapide din aplicatie) ----------
uint32_t modExp[4] = {0, 0, 0, 0};          // cand revine pe automat (epoch); 0 = nu revine singur
unsigned long modExpMs[4] = {0, 0, 0, 0};   // la fel, in millis (cand placa nu stie inca ora)
void modExpVerifica() {
  bool sch = false;
  time_t n = time(nullptr);
  for (int i = 0; i < 4; i++) {
    bool gata = (modExp[i] && n > 1000000000 && (uint32_t)n >= modExp[i]) || (!modExp[i] && modExpMs[i] && (long)(millis() - modExpMs[i]) >= 0);
    if (!gata) continue;
    modExp[i] = 0; modExpMs[i] = 0;
    if (S.mod[i] != 0) { S.mod[i] = 0; sch = true; }
  }
  if (sch) { salveaza(); logica(); prefs.putBytes("mexp", modExp, sizeof(modExp)); }
}
long modRamas(int i) {   // secunde pana revine pe automat
  time_t n = time(nullptr);
  if (modExp[i] && n > 1000000000) return max(0L, (long)modExp[i] - (long)n);
  if (modExpMs[i]) return max(0L, (long)(modExpMs[i] - millis()) / 1000);
  return 0;
}

void logica() {
  bool senzorOk = !isnan(temp) && !isnan(hum);
  struct tm t;
  bool oraOk = oraLocala(&t);
  unsigned long min = millis() / 60000UL;

  // Histerezis (ventilatorul are prioritate peste ceata)
  if (senzorOk) {
    if (hum < S.mistOn) mistState = true;
    else if (hum > S.mistOff) mistState = false;
    if (!mistState && optCeataDinTimp(hum)) { mistState = true; ceataDinTimp = true; }   // optimizare: porneste inainte de prag
    if (!mistState) ceataDinTimp = false;
    if (hum > S.fanOn) fanState = true;
    else if (hum < S.fanOff) fanState = false;
  } else {
    mistState = false;  // senzor defect: oprim ceata si ventilatorul
    fanState = false;
  }

  bool circ = false;
  if (S.circLa > 0 && S.circDur > 0) circ = (min % S.circLa) < (unsigned long)S.circDur;

  bool apaOk = true;
#if FOLOSESTE_NIVEL
  apaOk = (digitalRead(PIN_NIVEL) == LOW);
#endif

  bool aut[4];
  // Pompa
  if (S.pompaOff <= 0) aut[0] = true;
  else {
    unsigned long ciclu = (unsigned long)(S.pompaOn + S.pompaOff);
    aut[0] = (min % ciclu) < (unsigned long)S.pompaOn;
  }
  // Lumina
  if (oraOk) {
    int h = t.tm_hour * 60 + t.tm_min;
    aut[1] = (S.lumOra <= S.lumStop) ? (h >= S.lumOra && h < S.lumStop)
                                     : (h >= S.lumOra || h < S.lumStop);
  } else aut[1] = false;
  // Ventilator
  bool dupa = dupaActiv && (millis() - dupaT0 < (unsigned long)S.plVent * 60000UL);
  if (dupaActiv && !dupa) dupaActiv = false;
  aut[3] = fanState || circ || dupa;   // dupa ploaie ventilatorul usuca geamul cateva minute
  // Ceata (nu merge cat timp ventilatorul sau circulatia sunt active, sau fara apa)
  aut[2] = mistState && !aut[3] && apaOk && !ploaieOn;

  for (int i = 0; i < 4; i++) {
    bool on = (S.mod[i] == 1) ? true : (S.mod[i] == 2) ? false : aut[i];
    if (i == 2 && !apaOk) on = false;  // siguranta: ceata niciodata fara apa
    if (i == 1 && (ledOverheat || fanFail)) on = false;  // siguranta: radiator supraincalzit sau ventilator oprit
    scrieIesire(i, on);
  }
}

#include "pagina.h"   // pagina web, manifestul si iconitele (date mari, tinute separat)

void hManifest() { server.sendHeader("Cache-Control", "max-age=86400"); server.send_P(200, "application/manifest+json", MANIFEST); }
void hIcon192() { server.sendHeader("Cache-Control", "max-age=604800"); server.send_P(200, "image/png", (const char*)ICON192, sizeof(ICON192)); }
void hIcon512() { server.sendHeader("Cache-Control", "max-age=604800"); server.send_P(200, "image/png", (const char*)ICON512, sizeof(ICON512)); }

void hRoot() {
  if (!auth()) return;
  server.sendHeader("Content-Encoding", "gzip");   // pagina e tinuta comprimata: ~4 ori mai mica, se deschide mai repede
  server.send_P(200, "text/html", (const char*)PAGINA_GZ, sizeof(PAGINA_GZ));
}

// ---------- informatii despre placa (versiune, de cand merge, WiFi, ultima repornire) ----------
uint32_t wifiCaderi = 0;            // de cate ori a cazut WiFi-ul de la pornire
uint32_t heapMin = 0xFFFFFFFF;      // cea mai mica memorie libera vazuta
String repMotiv = "";
const char* motivRepornire(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "pornire sau pană de curent";
    case ESP_RST_EXT:      return "butonul de reset";
    case ESP_RST_SW:       return "repornire cerută din program";
    case ESP_RST_PANIC:    return "o eroare în program";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "s-a blocat, iar paza (watchdog) a repornit-o";
    case ESP_RST_BROWNOUT: return "tensiune prea mică (alimentare slabă)";
    case ESP_RST_DEEPSLEEP: return "trezire din somn";
    default:               return "motiv necunoscut";
  }
}
bool otaPosibil() { return esp_ota_get_next_update_partition(NULL) != NULL; }
String sysJson() {
  return "{\"v\":\"" VERSIUNE "\",\"up\":" + String(millis() / 1000) + ",\"rssi\":" +
         String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",\"rr\":\"" + repMotiv + "\",\"ota\":" + String(otaPosibil() ? 1 : 0) +
         ",\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",\"hmin\":" + String(heapMin == 0xFFFFFFFF ? 0 : heapMin / 1024) +
         ",\"wc\":" + String(wifiCaderi) + ghJsonScurt() + optScurt() + "}";
}

// ---------- actualizare prin WiFi: fisierul .bin trimis din pagina (Setari > Actualizează placa) ----------
bool updOk = false;
String updErr = "";
void hUpdateGata() {
  if (!auth()) return;
  if (updOk) {
    server.send(200, "text/plain", "Gata. Placa repornește cu codul nou, în cam 15 secunde.");
    prefs.putUChar("otaOk", 1);
    histSalveaza();
    evSalveaza();
    optSalveazaZile();
    restartLa = millis() + 1500;
  } else {
    server.send(500, "text/plain", updErr.length() ? updErr : String("Actualizarea nu a mers."));
  }
}
void hUpdateDate() {
  HTTPUpload& u = server.upload();
  esp_task_wdt_reset();   // incarcarea dureaza: hranim paza ca sa nu reporneasca placa
  if (u.status == UPLOAD_FILE_START) {
    updOk = false; updErr = "";
    if (!server.authenticate(WEB_USER, WEB_PASS)) { updErr = "Parola paginii nu se potrivește."; return; }
    if (!otaPosibil()) { updErr = "Placa nu are loc pentru actualizare prin WiFi. Încarcă o dată prin cablu cu Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)."; return; }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) updErr = String("Nu pot începe actualizarea: ") + Update.errorString();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (updErr.length() == 0 && Update.write(u.buf, u.currentSize) != u.currentSize)
      updErr = String("Fișierul nu e bun (") + Update.errorString() + "). Alege paludariu.ino.bin.";
  } else if (u.status == UPLOAD_FILE_END) {
    if (updErr.length() == 0) {
      if (Update.end(true)) updOk = true;
      else updErr = String("Fișierul nu e bun (") + Update.errorString() + "). Alege paludariu.ino.bin.";
    } else if (Update.isRunning()) Update.abort();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    updErr = "Încărcarea s-a întrerupt. Încearcă din nou.";
  }
}

// ---------- actualizare din Arduino IDE prin retea (Tools > Port > portul de retea al placii) ----------
bool otaPornit = false;
void otaStart() {
  if (otaPornit || !otaPosibil()) return;
  ArduinoOTA.setHostname(mdnsNume.c_str());
  ArduinoOTA.setPassword(WEB_PASS);       // aceeasi parola ca pagina (Arduino IDE o cere la Upload)
  ArduinoOTA.setMdnsEnabled(false);       // mDNS il porneste placa; mai jos doar anuntam portul pentru Arduino IDE
  ArduinoOTA.onStart([]() { histSalveaza(); evSalveaza(); optSalveazaZile(); });
  ArduinoOTA.onProgress([](unsigned int, unsigned int) { esp_task_wdt_reset(); });
  ArduinoOTA.onEnd([]() { prefs.putUChar("otaOk", 1); });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true);
  otaPornit = true;
}

// ---------- paza retelei si a memoriei ----------
// - daca WiFi-ul lipseste 10 minute, placa reporneste (automatizarea merge si fara WiFi, dar asa se reface legatura)
// - la 2 minute verifica daca routerul raspunde; daca nu raspunde de 3 ori la rand, reface legatura WiFi,
//   iar daca tot nu merge dupa 12 minute, reporneste placa (WiFi "agatat": conectat, dar fara trafic)
// - daca memoria libera scade periculos de mult un minut intreg, reporneste curat
#if FOLOSESTE_TELEGRAM
void tg(const String& s);
#endif
unsigned long wifiPierdutT = 0, memJosT = 0;
int gwRau = 0;
bool gwVazut = false;               // routerul a raspuns macar o data (altfel nu luam masuri)
void repornesteCurat(const char* motiv) {
  prefs.putString("rmotiv", motiv);
  histSalveaza();
  evSalveaza();
  optSalveazaZile();
  h30Salveaza();
  oreSalveaza();
  delay(200);
  ESP.restart();
}
bool routerRaspunde() {
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress(0, 0, 0, 0)) return false;
  const uint16_t porturi[2] = {80, 53};
  for (int i = 0; i < 2; i++) {
    WiFiClient c;
    if (c.connect(gw, porturi[i], 1500)) { c.stop(); return true; }
  }
  return false;
}
void reteaLoop() {
  static unsigned long t0 = 0, tGw = 0;
  unsigned long now = millis();
  if (now - t0 < 10000) return;
  t0 = now;
  uint32_t h = ESP.getFreeHeap();
  if (h < heapMin) heapMin = h;
  if (h < 20000 || ESP.getMaxAllocHeap() < 8000) {
    if (!memJosT) memJosT = now;
    else if (now - memJosT > 60000) repornesteCurat("memoria s-a umplut, placa a repornit singură");
  } else memJosT = 0;
  if (!staConfigurat || setupSt != 0) return;
  bool conectat = WiFi.status() == WL_CONNECTED;
  if (!conectat) {
    if (!wifiPierdutT) { wifiPierdutT = now; wifiCaderi++; }
    else if (now - wifiPierdutT > 600000UL && !(modSetup && WiFi.softAPgetStationNum() > 0))
      repornesteCurat("WiFi-ul a lipsit 10 minute, placa a repornit singură");
    return;
  }
  if (wifiPierdutT) {
    unsigned long d = now - wifiPierdutT;
    wifiPierdutT = 0;
#if FOLOSESTE_TELEGRAM
    if (d > 60000) tg("\xF0\x9F\x93\xB6 WiFi-ul a lipsit " + String(d / 60000) + " min și a revenit.");   // 📶
#endif
  }
  if (now - tGw >= 120000UL) {
    tGw = now;
    if (routerRaspunde()) { gwVazut = true; gwRau = 0; }
    else if (gwVazut) {
      gwRau++;
      if (gwRau == 3) { WiFi.disconnect(false); delay(100); WiFi.reconnect(); }
      else if (gwRau >= 6) repornesteCurat("legătura WiFi s-a blocat, placa a repornit singură");
    }
  }
}

// ---------- paza (watchdog): daca programul se blocheaza 30 de secunde, placa reporneste singura ----------
// 1.8.9: inainte de fiecare pas din loop() notam unde suntem, intr-o memorie care nu se sterge la repornire.
// Dupa o repornire facuta de paza, mesajul de pe Telegram spune unde s-a blocat si cata memorie era libera.
RTC_NOINIT_ATTR uint32_t pasMagic, pasCod, pasHeap;
const uint32_t PAS_MAGIC = 0x50A1D589;
#define PAS(n) (pasCod = (n))
const char* PAS_NUME[] = {"pornire", "pagina web", "actualizarea prin Arduino IDE", "mementouri și rezumatul nopții", "verificarea rețelei",
                          "legătura prin internet (EMQX)", "căutarea actualizărilor pe GitHub", "optimizare", "salvarea jurnalului",
                          "raportul Telegram", "ploaia (pompa Tuya)", "turația ventilatorului", "WiFi", "comenzile Telegram",
                          "Bluetooth", "ecran", "mesajul de pornire Telegram", "citirea senzorilor", "comanda aparatelor", "alertele Telegram"};
String pasText() {
  if (pasMagic != PAS_MAGIC || pasCod >= sizeof(PAS_NUME) / sizeof(PAS_NUME[0])) return "";
  return String(PAS_NUME[pasCod]) + ". Memorie liberă atunci: " + String(pasHeap) + " KB";
}
void pazaPorneste() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t c = {};
  c.timeout_ms = 30000;
  c.idle_core_mask = 1;          // ca inainte: urmareste si nucleul 0
  c.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&c) != ESP_OK) esp_task_wdt_init(&c);
#else
  esp_task_wdt_init(30, true);
#endif
  esp_task_wdt_add(NULL);        // bucla principala (loop)
}

String setariJson() {   // toate setarile, ca JSON (pagina, copia de rezerva)
  String j = "{";
  for (int i = 0; i < MEM_N; i++) j += "\"m" + String(i) + "\":" + String(M.zile[i]) + ",";
  j += "\"mistOn\":" + String(S.mistOn, 0) + ",\"mistOff\":" + String(S.mistOff, 0);
  j += ",\"fanOn\":" + String(S.fanOn, 0) + ",\"fanOff\":" + String(S.fanOff, 0);
  j += ",\"lumOra\":" + String(S.lumOra) + ",\"lumStop\":" + String(S.lumStop);
  j += ",\"pompaOn\":" + String(S.pompaOn) + ",\"pompaOff\":" + String(S.pompaOff);
  j += ",\"plO1\":" + String(S.plOra[0]) + ",\"plO2\":" + String(S.plOra[1]) + ",\"plO3\":" + String(S.plOra[2]);
  j += ",\"plDur\":" + String(S.plDur) + ",\"plMax\":" + String(S.plMax) + ",\"plVent\":" + String(S.plVent);
  j += ",\"circLa\":" + String(S.circLa) + ",\"circDur\":" + String(S.circDur) + ",\"standbyMin\":" + String(S.standbyMin) + ",\"ledFanOn\":" + String(S.ledFanOn) + ",\"ledFanMax\":" + String(S.ledFanMax) + ",\"ledCutoff\":" + String(S.ledCutoff);
  j += ",\"umA\":" + String(S.umAuto) + ",\"umT\":" + String(S.umTinta) + ",\"umS\":" + String(S.umStrict);
  j += ",\"ledFanMin\":" + String(S.ledFanMin) + ",\"hz\":" + String(S.hranZile) + ",\"ho\":" + String(S.hranOra);
  for (int k = 0; k < 3; k++) j += ",\"ol" + String(k) + "\":" + String(S.oreLim[k]);
  j += "}";
  return j;
}

Rasp apiStatus(const Arg& a) {
  struct tm t;
  String ora = "", data = "";
  if (oraLocala(&t)) {
    char b[24];
    strftime(b, sizeof(b), "%H:%M:%S", &t); ora = b;
    strftime(b, sizeof(b), "%d.%m.%Y", &t); data = b;
  }
  String j = "{";
  j += "\"nume\":\"" + jEsc(numePlaca) + "\",";
  j += "\"t\":" + (isnan(temp) ? String("null") : String(temp, 1));
  j += ",\"h\":" + (isnan(hum) ? String("null") : String(hum, 1));
  j += ",\"ora\":\"" + ora + "\",\"data\":\"" + data + "\",\"st\":[";
  for (int i = 0; i < 4; i++) { j += stare[i] ? "true" : "false"; if (i < 3) j += ","; }
  j += "],\"md\":[";
  for (int i = 0; i < 4; i++) { j += String(S.mod[i]); if (i < 3) j += ","; }
#if FOLOSESTE_NIVEL
  j += "],\"apa\":" + String(digitalRead(PIN_NIVEL) == LOW ? "true" : "false");
#else
  j += "],\"apa\":null";
#endif
  j += ",\"led\":{\"t\":" + (isnan(tLed) ? String("null") : String(tLed, 1)) + ",\"f\":" + String(fanLedPct) + ",\"oh\":" + String(ledOverheat ? 1 : 0) +
       ",\"rpm\":" + String(fanRpm) + ",\"rl\":" + String(fanRpmLive ? 1 : 0) + ",\"ff\":" + String(fanFail ? 1 : 0) +
       ",\"tach\":" + String(FANLED_TACH ? (FANLED_4PIN || FANLED_ONOFF ? 2 : 1) : 0) + ledSrcJson() + fanJson() + "},\"pl\":" + plJson() + ",\"mem\":" + memJson() + ",\"ore\":" + oreJson() + ",\"hr\":" + hranScurt() + ",\"sys\":" + sysJson() + ",\"vac\":" + String(vacanta ? 1 : 0) + ",\"hb\":" + String(umBlocatMotiv) + ",\"mp\":[" +
       String(modRamas(0)) + "," + String(modRamas(1)) + "," + String(modRamas(2)) + "," + String(modRamas(3)) + "]" +
       (nivelCerere == 1 ? String(",\"vz\":1") : String(",\"pv\":[") + String(vPass.length() ? 1 : 0) + "," + String(vizUlt) + "," + String(vizCale) + "]") +
       ",\"s\":" + setariJson() + "}";
  return Rasp{200, "application/json", j};
}
void hStatus() { if (!auth()) return; trimite(apiStatus(Arg{"", true})); }

Rasp apiMod(const Arg& a) {
  int i = a.get("i").toInt(), v = a.get("v").toInt();
  if (i >= 0 && i < 4 && v >= 0 && v <= 2) {
    S.mod[i] = v;
    modExp[i] = 0; modExpMs[i] = 0;
    int mn = a.has("min") ? constrain((int)a.get("min").toInt(), 0, 720) : 0;   // &min=5 = doar 5 minute, apoi automat
    if (mn > 0 && v != 0) {
      time_t n = time(nullptr);
      if (n > 1000000000) modExp[i] = (uint32_t)n + mn * 60UL;
      modExpMs[i] = millis() + mn * 60000UL;
    }
    prefs.putBytes("mexp", modExp, sizeof(modExp));
    salveaza(); logica();
  }
  return Rasp{200, "text/plain", "ok"};
}
void hMod() { if (!auth()) return; trimite(apiMod(Arg{"", true})); }

Rasp apiSave(const Arg& a) {
  if (a.has("mistOn")) S.mistOn = a.get("mistOn").toFloat();
  if (a.has("mistOff")) S.mistOff = a.get("mistOff").toFloat();
  if (a.has("fanOn")) S.fanOn = a.get("fanOn").toFloat();
  if (a.has("fanOff")) S.fanOff = a.get("fanOff").toFloat();
  if (a.has("lumOra")) S.lumOra = constrain(a.get("lumOra").toInt(), 0, 1439);
  if (a.has("lumStop")) S.lumStop = constrain(a.get("lumStop").toInt(), 0, 1439);
  if (a.has("pompaOn")) S.pompaOn = max(1, (int)a.get("pompaOn").toInt());
  if (a.has("pompaOff")) S.pompaOff = max(0, (int)a.get("pompaOff").toInt());
  if (a.has("circLa")) S.circLa = max(0, (int)a.get("circLa").toInt());
  if (a.has("circDur")) S.circDur = max(0, (int)a.get("circDur").toInt());
  if (a.has("standbyMin")) S.standbyMin = constrain((int)a.get("standbyMin").toInt(), 0, 60);
  if (a.has("ledFanOn")) S.ledFanOn = constrain((int)a.get("ledFanOn").toInt(), 20, 70);
  if (a.has("ledFanMax")) S.ledFanMax = constrain((int)a.get("ledFanMax").toInt(), 30, 90);
  if (a.has("ledCutoff")) S.ledCutoff = constrain((int)a.get("ledCutoff").toInt(), 40, 110);
  if (a.has("ledFanMin")) S.ledFanMin = constrain((int)a.get("ledFanMin").toInt(), 0, 60);
  if (a.has("hz")) S.hranZile = constrain((int)a.get("hz").toInt(), 0, 127);
  if (a.has("ho")) S.hranOra = constrain((int)a.get("ho").toInt(), 0, 23);
  for (int k = 0; k < 3; k++) { String key = "ol" + String(k); if (a.has(key)) S.oreLim[k] = constrain((int)a.get(key).toInt(), 0, 5000); }
  const char* plK[3] = {"plO1", "plO2", "plO3"};
  for (int k = 0; k < 3; k++) if (a.has(plK[k])) S.plOra[k] = constrain((int)a.get(plK[k]).toInt(), 0, 1440);
  if (a.has("plDur")) S.plDur = constrain((int)a.get("plDur").toInt(), 5, 180);
  if (a.has("plMax")) S.plMax = constrain((int)a.get("plMax").toInt(), 50, 100);
  if (a.has("plVent")) S.plVent = constrain((int)a.get("plVent").toInt(), 0, 30);
  // umiditatea dorita: cand se schimba, pragurile se calculeaza din nou
  bool umSch = false;
  if (a.has("umA")) { uint8_t v = a.get("umA").toInt() ? 1 : 0; if (v != S.umAuto) { S.umAuto = v; umSch = true; } }
  if (a.has("umT")) { int v = constrain((int)a.get("umT").toInt(), 70, 95); if (v != S.umTinta) { S.umTinta = v; umSch = true; } }
  if (a.has("umS")) { int v = constrain((int)a.get("umS").toInt(), 2, 4); if (v != S.umStrict) { S.umStrict = v; umSch = true; } }
  if (umSch && S.umAuto) { umCalc(); optTintaNoua(); }
  bool memSchimbat = false;
  for (int i = 0; i < MEM_N; i++) {
    String k = "m" + String(i);
    if (a.has(k)) {
      int z = constrain((int)a.get(k).toInt(), 0, 90);
      if (z != M.zile[i]) {
        if (M.zile[i] == 0 && z > 0) { time_t n = time(nullptr); if (n > 1000000000) M.ult[i] = (uint32_t)n; }   // pornit acum: numaram de azi
        M.zile[i] = z; memSchimbat = true;
      }
    }
  }
  if (memSchimbat) memSalveaza();
  if (S.ledFanMax < S.ledFanOn + 5) S.ledFanMax = S.ledFanOn + 5;
  if (S.ledCutoff < S.ledFanMax + 5) S.ledCutoff = S.ledFanMax + 5;
  salveaza(); controlLed(); logica();
  return Rasp{200, "text/plain", "ok"};
}
void hSave() { if (!auth()) return; trimite(apiSave(Arg{"", true})); }

Rasp apiEv(const Arg& a) {
  String j = "{\"now\":" + String(acum()) + ",\"up\":" + String(millis() / 1000) + ",\"cur\":[";
  j.reserve(4200);
  for (int i = 0; i < 4; i++) { j += stare[i] ? "1" : "0"; if (i < 3) j += ","; }
  if (tuyaConfigurat()) j += ploaieOn ? ",1" : ",0";
  j += "],\"e\":[";
  for (int k = 0; k < evCount; k++) {
    Ev& e = evl[(evHead - evCount + k + EV_N) % EV_N];
    j += "[" + String(e.t) + "," + String(e.out) + "," + String(e.on) + "," + String(e.man) + "," + String(e.h) + "]";
    if (k < evCount - 1) j += ",";
  }
  j += "]}";
  return Rasp{200, "application/json", j};
}
void hEv() { if (!auth()) return; trimite(apiEv(Arg{"", true})); }

Rasp apiMem(const Arg& a) {   // /mem?i=2 = "Gata" la mementoul 2; /mem?o=0 = am curatat (dupa ore); /mem = starea
  if (a.has("o")) { oreGata(a.get("o").toInt()); return Rasp{200, "text/plain", "ok"}; }
  if (!a.has("i")) return Rasp{200, "application/json", "{\"mem\":" + memJson() + ",\"ore\":" + oreJson() + "}"};
  if (memGata(a.get("i").toInt())) return Rasp{200, "text/plain", "ok"};
  else return Rasp{503, "text/plain; charset=utf-8", "Placa nu știe încă ora. Încearcă peste un minut."};
}
void hMem() { if (!auth()) return; trimite(apiMem(Arg{"", true})); }

Rasp apiHist30(const Arg& a) {
  String j = "{\"cap\":" + String(H30_N) + ",\"last\":" + String(h30Last * 3600UL) + ",\"t\":[";
  j.reserve(9000);
  for (int pas = 0; pas < 2; pas++) {
    int16_t* a = pas == 0 ? h30T : h30H;
    for (int i = 0; i < h30Count; i++) {
      int16_t v = a[(h30Head - h30Count + i + H30_N) % H30_N];
      j += (v == INT16_MIN) ? String("null") : String(v / 10.0, 1);
      if (i < h30Count - 1) j += ",";
    }
    j += pas == 0 ? "],\"h\":[" : "]}";
  }
  return Rasp{200, "application/json", j};
}
void hHist30() { if (!auth()) return; trimite(apiHist30(Arg{"", true})); }

Rasp apiHist(const Arg& a) {
  uint32_t e = acum();
  long ago = (histEpoch > 1000000000UL && e > histEpoch) ? (long)(e - histEpoch) : (long)((millis() - tHist) / 1000);
  String j = "{\"cap\":" + String(H_N) + ",\"ago\":" + String(ago) + ",\"t\":[";
  j.reserve(4200);
  for (int pas = 0; pas < 2; pas++) {
    int16_t* a = pas == 0 ? histT : histH;
    for (int i = 0; i < histCount; i++) {
      int16_t v = a[(histHead - histCount + i + H_N) % H_N];
      j += (v == INT16_MIN) ? String("null") : String(v / 10.0, 1);
      if (i < histCount - 1) j += ",";
    }
    j += pas == 0 ? "],\"h\":[" : "]}";
  }
  return Rasp{200, "application/json", j};
}
// Copia de rezerva a setarilor: un fisier JSON pe care pagina il poate incarca inapoi (prin /save si /m)
Rasp apiCfg(const Arg& a) {
  (void)a;
  struct tm t; char d[12] = "";
  if (oraLocala(&t)) strftime(d, sizeof d, "%Y-%m-%d", &t);
  String j = "{\"tip\":\"paludariu-setari\",\"v\":\"" VERSIUNE "\",\"nume\":\"" + jEsc(numePlaca) + "\",\"data\":\"" + d + "\",\"md\":[";
  for (int i = 0; i < 4; i++) { j += String(S.mod[i]); if (i < 3) j += ","; }
  j += "],\"vac\":" + String(vacanta ? 1 : 0) + ",\"s\":" + setariJson() + "}";
  return Rasp{200, "application/json", j};
}
void hCfg() { if (!auth()) return; trimite(apiCfg(Arg{"", true})); }

void vacSeteaza(bool on) {
  if (vacanta == on) return;
  vacanta = on;
  prefs.putUChar("vac", on ? 1 : 0);
#if FOLOSESTE_TELEGRAM
  tg(on ? "\xF0\x9F\x8F\x9D\xEF\xB8\x8F Modul vacanță e pornit: o ploaie pe zi, alerte mai stricte și raport zilnic la 20:00."
        : "\xF0\x9F\x8F\xA0 Modul vacanță e oprit. Totul merge din nou după programul obișnuit.");   // 🏝️ / 🏠
#endif
}
Rasp apiVac(const Arg& a) {
  if (a.has("on")) vacSeteaza(a.get("on") == "1");
  return Rasp{200, "application/json", String("{\"vac\":") + (vacanta ? 1 : 0) + "}"};
}
void hVac() { if (!auth()) return; trimite(apiVac(Arg{"", true})); }

void hHist() { if (!auth()) return; trimite(apiHist(Arg{"", true})); }

// ---------- SETUP / LOOP ----------
// Anunta placa in retea: adresa paludariu1.local + serviciul "_paludariu._tcp" pentru aplicatia Android
void mdnsStart() {
  static bool pornit = false;
  if (pornit) return;
  if (MDNS.begin(mdnsNume.c_str())) {
    MDNS.setInstanceName(numePlaca.c_str());   // numele afisat in aplicatie
    MDNS.addService("paludariu", "tcp", 80);
    MDNS.addService("http", "tcp", 80);
    pornit = true;
    otaStart();   // actualizarea din Arduino IDE prin retea
  }
}

// Numele pentru adresa locala (http://nume.local): litere mici, cifre si "-"
String slug(const String& n) {
  String o;
  for (unsigned i = 0; i < n.length() && o.length() < 24; i++) {
    char c = n[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c;
    else if (o.length() && o[o.length() - 1] != '-') o += '-';
  }
  while (o.endsWith("-")) o.remove(o.length() - 1);
  return o;
}

void wifiIncarca() {
  wSsid = prefs.getString("wssid", "");
  wPass = prefs.getString("wpass", "");
  numePlaca = prefs.getString("nume", DEVICE_NAME);
  String sl = prefs.isKey("nume") ? slug(numePlaca) : String("");
  mdnsNume = sl.length() ? sl : String(MDNS_NAME);
}

void setupPorneste() {
  if (modSetup) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);   // fara economie de energie: placa raspunde mereu la PC (ping, pagina, actualizare)
  WiFi.softAP(SETUP_SSID, SETUP_PASS);
  modSetup = true;
  Serial.printf("Reteaua \"%s\" e pornita (parola %s). Adauga placa din aplicatie: Placa noua.\n", SETUP_SSID, SETUP_PASS);
}

void setupOpreste() {
  if (!modSetup) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // fara economie de energie: placa raspunde mereu la PC (ping, pagina, actualizare)
  modSetup = false;
  Serial.println("Reteaua Paludariu-Setup e oprita.");
}

void conectareWifi() {
  String ss = wSsid, pw = wPass;
  bool cfg = strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "NUME_RETEA") != 0 && !prefs.getBool("farcfg", false);
  if (!ss.length() && cfg) { ss = WIFI_SSID; pw = WIFI_PASS; }
  if (!ss.length()) {
    Serial.println("Nicio retea WiFi salvata.");
    setupPorneste();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // fara economie de energie: placa raspunde mereu la PC (ping, pagina, actualizare)
  WiFi.setAutoReconnect(true);
  WiFi.begin(ss.c_str(), pw.c_str());
  staConfigurat = true;
  Serial.print("WiFi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Adresa IP: "); Serial.println(WiFi.localIP());
    mdnsStart();
  } else {
    // routerul poate porni mai greu decat placa dupa o pana de curent: incercam in continuare reteaua salvata
    Serial.println("Fara WiFi - automatizarea ruleaza oricum. Pornesc si Paludariu-Setup.");
    WiFi.begin(ss.c_str(), pw.c_str());
    setupPorneste();
  }
}

String jEsc(const String& v) {
  String o;
  for (unsigned i = 0; i < v.length(); i++) {
    char c = v[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

// ---- cererile aplicatiei, doar cat e pornita reteaua Paludariu-Setup ----
bool setupActiv() {
  if (modSetup) return true;
  server.send(404, "text/plain", "nu e in modul de configurare");
  return false;
}

void hSetupInfo() {
  if (!setupActiv()) return;
  server.send(200, "application/json", "{\"setup\":true,\"nume\":\"" + jEsc(numePlaca) + "\",\"mac\":\"" + WiFi.macAddress() + "\"}");
}

void hSetupScan() {
  if (!setupActiv()) return;
  int n = WiFi.scanNetworks();
  int idx[30], m = 0;
  for (int i = 0; i < n && m < 30; i++) {
    String s = WiFi.SSID(i);
    if (!s.length() || s == SETUP_SSID) continue;
    bool dublu = false;
    for (int k = 0; k < m; k++) if (WiFi.SSID(idx[k]) == s) { dublu = true; if (WiFi.RSSI(i) > WiFi.RSSI(idx[k])) idx[k] = i; break; }
    if (!dublu) idx[m++] = i;
  }
  for (int a = 0; a < m; a++)   // cele mai puternice primele
    for (int b = a + 1; b < m; b++)
      if (WiFi.RSSI(idx[b]) > WiFi.RSSI(idx[a])) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
  String j = "{\"n\":[";
  for (int k = 0; k < m; k++) {
    int i = idx[k];
    j += String(k ? "," : "") + "{\"s\":\"" + jEsc(WiFi.SSID(i)) + "\",\"r\":" + String(WiFi.RSSI(i)) +
         ",\"l\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1) + "}";
  }
  j += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

void hSetupSave() {
  if (!setupActiv()) return;
  String ss = server.arg("ssid"), pw = server.arg("pass"), nm = server.arg("nume");
  nm.replace("\"", ""); nm.replace("\\", ""); nm.trim();
  if (!ss.length()) { server.send(400, "application/json", "{\"ok\":false}"); return; }
  setupSsid = ss; setupPass = pw; setupNume = nm.length() ? nm.substring(0, 32) : numePlaca;
  server.send(200, "application/json", "{\"ok\":true}");
  delay(100);
  WiFi.disconnect(false);
  WiFi.begin(setupSsid.c_str(), setupPass.c_str());
  setupSt = 1; setupT0 = millis();
  Serial.printf("Ma conectez la \"%s\"...\n", setupSsid.c_str());
}

void hSetupStatus() {
  if (!setupActiv()) return;
  String j = "{\"st\":" + String(setupSt);
  if (setupSt == 2) j += ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"nume\":\"" + jEsc(numePlaca) + "\"";
  server.send(200, "application/json", j + "}");
}

void hSetupDone() {
  if (!setupActiv()) return;
  server.send(200, "application/json", "{\"ok\":true}");
  restartLa = millis() + 1500;   // repornim curat, conectati la WiFi-ul de acasa
}

void wifiUita() {
  prefs.remove("wssid"); prefs.remove("wpass");
  prefs.putBool("farcfg", true);   // nici WiFi-ul din config.h nu-l mai folosim, pana il configurezi din aplicatie
  Serial.println("WiFi uitat. Repornesc in modul Paludariu-Setup.");
  restartLa = millis() + 1500;
}

void hWifiReset() {
  if (!auth()) return;
  server.send(200, "text/plain; charset=utf-8", "Placa repornește și își face rețeaua Paludariu-Setup.");
  wifiUita();
}

// Se apeleaza din loop(): urmareste conectarea ceruta din aplicatie, butonul BOOT si repornirea programata
void wifiLoop() {
  if (setupSt == 1) {
    wl_status_t w = WiFi.status();
    if (w == WL_CONNECTED) {
      prefs.putString("wssid", setupSsid); prefs.putString("wpass", setupPass);
      prefs.putString("nume", setupNume); prefs.putBool("farcfg", false);
      wSsid = setupSsid; wPass = setupPass; numePlaca = setupNume;
      setupSt = 2; setupT0 = millis();
      Serial.print("Conectat din aplicatie! Adresa IP: "); Serial.println(WiFi.localIP());
    } else if (millis() - setupT0 > 20000 || w == WL_CONNECT_FAILED || w == WL_NO_SSID_AVAIL) {
      if (millis() - setupT0 > 4000) {   // WL_CONNECT_FAILED poate aparea o clipa la inceput
        WiFi.disconnect(false);
        setupSt = 3;
        Serial.println("Nu m-am putut conecta (parola gresita sau retea prea departe).");
      }
    }
  }
  if (setupSt == 2 && !restartLa && millis() - setupT0 > 60000) restartLa = millis() + 500;   // aplicatia n-a confirmat
  if (setupSt == 3 && millis() - setupT0 > 140000) {   // nimeni n-a mai incercat: revenim la reteaua salvata, daca exista
    setupSt = 0;
    if (wSsid.length()) WiFi.begin(wSsid.c_str(), wPass.c_str());
  }
  // conectat la reteaua salvata fara ca cineva sa configureze ceva: oprim Paludariu-Setup
  if (modSetup && setupSt == 0 && WiFi.status() == WL_CONNECTED && WiFi.softAPgetStationNum() == 0) { setupOpreste(); mdnsStart(); }
  static unsigned long tBoot = 0;
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!tBoot) tBoot = millis();
    else if (millis() - tBoot > 5000 && !restartLa) wifiUita();
  } else tBoot = 0;
  if (restartLa && (long)(millis() - restartLa) > 0) ESP.restart();
}

void setup() {
  Serial.begin(115200);
  tlsSem = xSemaphoreCreateBinary();
  xSemaphoreGive(tlsSem);

  // Setam nivelul OPRIT inainte de pinMode, ca sa nu avem pulsuri la pornire
  for (int i = 0; i < 4; i++) {
    digitalWrite(PIN[i], ACTIV_LOW[i] ? HIGH : LOW);
    pinMode(PIN[i], OUTPUT);
    digitalWrite(PIN[i], ACTIV_LOW[i] ? HIGH : LOW);
  }
#if FOLOSESTE_NIVEL
  pinMode(PIN_NIVEL, INPUT_PULLUP);
#endif
#if FANLED_TACH
  pinMode(PIN_TACH, INPUT);   // GPIO35 nu are rezistenta interna: 10 kohm la 3V3 pe placa
  attachInterrupt(digitalPinToInterrupt(PIN_TACH), tachIsr, FALLING);
#endif

#if FOLOSESTE_DISPLAY
  displayInit();
#endif
  prefs.begin("paludariu", false);
  wifiIncarca();
  pinMode(PIN_BOOT, INPUT_PULLUP);
  incarca();
  histIncarca();
  h30Incarca();
  memIncarca();
  evIncarca();
  optIncarca();
  mqIncarca();
  ghIncarca();
  vacanta = prefs.getUChar("vac", 0);
  oreIncarca();
  hranIncarca();
  noapteIncarca();
  privitIncarca();
  if (prefs.getBytes("mexp", modExp, sizeof(modExp)) != sizeof(modExp)) memset(modExp, 0, sizeof(modExp));
  repMotiv = motivRepornire(esp_reset_reason());
  {
    esp_reset_reason_t rr = esp_reset_reason();
    String unde = pasText();
    if (unde.length() && (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT)) repMotiv += ". Se bloca la: " + unde;
    else if (unde.length() && rr == ESP_RST_PANIC) repMotiv += ". Eroarea a apărut la: " + unde;
    pasMagic = PAS_MAGIC; pasCod = 0; pasHeap = 0;
  }
  if (prefs.isKey("rmotiv")) { repMotiv = prefs.getString("rmotiv", repMotiv); prefs.remove("rmotiv"); }   // repornire facuta de paza retelei/memoriei

#if FOLOSESTE_DHT22
  dht.begin();
  dsLed.begin(); dsLed.setWaitForConversion(false); dsLed.requestTemperatures();
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_FANLED, FANLED_FREQ, 8);
#else
  ledcSetup(FANLED_CH, FANLED_FREQ, 8);
  ledcAttachPin(PIN_FANLED, FANLED_CH);
#endif
  fanLedPct = 100;   // pana la prima citire, ventilatorul radiatorului merge la maxim
  fanAplica();
#else
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!sht.begin(0x44)) Serial.println("SHT31 negasit! Verifica cablajul.");
#endif

#if FOLOSESTE_BLE
  bleInit();
#endif
#if FOLOSESTE_TELEGRAM
  tgInit();
#endif
  conectareWifi();
  configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.google.com");
  plInit();

  server.on("/", hRoot);
  server.on("/s", hStatus);
  server.on("/m", hMod);
  server.on("/save", hSave);
  server.on("/h", hHist);
  server.on("/h30", hHist30);
  server.on("/mem", hMem);
  server.on("/cfg", hCfg);
  server.on("/vac", hVac);
  server.on("/gh", hGh);
  server.on("/opt", hOpt);
  server.on("/mqttcfg", hMqttCfg);
  server.on("/update", HTTP_POST, hUpdateGata, hUpdateDate);
  server.on("/e", hEv);
  server.on("/pl", hPl);
  server.on("/fan", hFan);
  server.on("/hran", hHran);
  server.on("/noapte", hNoapte);
  server.on("/privit", hPrivit);
  server.on("/setup/info", hSetupInfo);
  server.on("/setup/scan", hSetupScan);
  server.on("/setup/save", hSetupSave);
  server.on("/setup/status", hSetupStatus);
  server.on("/setup/done", hSetupDone);
  server.on("/wifireset", hWifiReset);
#if FOLOSESTE_TELEGRAM
  server.on("/tgtest", hTgTest);
#endif
  server.on("/manifest.json", hManifest);
  server.on("/icon-192.png", hIcon192);
  server.on("/icon-512.png", hIcon512);
  server.begin();
  pazaPorneste();
}

void loop() {
  esp_task_wdt_reset();   // paza: bucla merge
  pasMagic = PAS_MAGIC; pasHeap = ESP.getFreeHeap() / 1024;
  PAS(1); server.handleClient();
  PAS(2); if (otaPornit) ArduinoOTA.handle();
  PAS(3); memLoop();
  PAS(4); reteaLoop();
  PAS(5); mqLoop();
  PAS(6); ghLoop();
  PAS(7); optLoop();
  PAS(8);
  { static unsigned long tEv = 0; if (evSchimbat && millis() - tEv > 600000UL) { tEv = millis(); evSalveaza(); } }
#if FOLOSESTE_TELEGRAM
  PAS(9); raportLoop();
#endif
  PAS(10); ploaieLoop();
  PAS(11); tachLoop(); fanLoop();
  PAS(12); wifiLoop();
#if FOLOSESTE_TELEGRAM
  PAS(13); tgExecutaComenzi();   // comenzile primite pe Telegram
#endif
#if FOLOSESTE_BLE
  PAS(14); bleLoop();
#endif
#if FOLOSESTE_DISPLAY
  PAS(15); displayLoop();
#endif
  PAS(16);

  static bool eraConectat = (WiFi.status() == WL_CONNECTED);
  bool conectat = (WiFi.status() == WL_CONNECTED);
  if (conectat && !eraConectat) {
    mdnsStart();
    Serial.print("Conectat! Adresa IP: "); Serial.println(WiFi.localIP());
  }
#if FOLOSESTE_TELEGRAM
  static bool anuntatPornire = false;
  if (conectat && !anuntatPornire) {
    anuntatPornire = true;
    if (prefs.getUChar("otaOk", 0)) {
      prefs.remove("otaOk");
      tg("\xE2\x9C\x85 Placa a fost actualizată la v" VERSIUNE " și merge din nou.");   // ✅
    } else {
      tg("\xF0\x9F\x94\x8C Placa a pornit (v" VERSIUNE "). Motiv: " + repMotiv + ".");   // 🔌
    }
  }
#endif
  eraConectat = conectat;

  if (!conectat && staConfigurat && setupSt == 0 && !(modSetup && WiFi.softAPgetStationNum() > 0)) {
    static unsigned long tw = 0;
    if (millis() - tw > 30000) { tw = millis(); WiFi.reconnect(); }
  }

  if (millis() - tSenzor > 2000) {
    tSenzor = millis();
    PAS(17);
    float t = citesteTemp(), h = citesteUmid();
    static int esecuri = 0;
    if (isnan(t) || isnan(h)) {
      // o citire esuata ocazionala e normala (mai ales la DHT22): o ignoram
      // si declaram senzorul defect abia dupa 5 esecuri la rand (~10 s)
      if (++esecuri >= 5) { temp = NAN; hum = NAN; }
    } else {
      esecuri = 0; temp = t; hum = h;
    }
    umBlocatVerifica();
    noapteAdauga();
    if ((histCount == 0 && !isnan(temp)) || (histCount > 0 && millis() - tHist >= 300000UL)) {
      tHist = millis();
      inregistreaza();
    }
#if SENZOR_LED_NTC
    float tl = citesteNtc();
#else
    float tl = dsLed.getTempCByIndex(0);   // citire DS18B20 (cererea pentru urmatoarea se trimite acum)
    dsLed.requestTemperatures();
#endif
    static int esecL = 0;
    if (tl <= -100.0f || tl == 85.0f) { if (++esecL >= 5) tLed = NAN; }   // 85.0 = valoarea de pornire a senzorului
    else { esecL = 0; tLed = tl; }
    PAS(18);
    controlLed();
    ploaieProgram();
    logica();
#if FOLOSESTE_TELEGRAM
    PAS(19); verificaAlerte();
#endif
    PAS(0);
  }
}
