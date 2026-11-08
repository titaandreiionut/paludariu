// Paludariu - actualizarea placii de pe GitHub (Releases), cu semnatura
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.
//
// Placa intreaba GitHub care e ultima versiune publicata (o data pe zi si cand apesi "Verifica").
// La "Actualizeaza", descarca singura paludariu.ino.bin si paludariu.ino.bin.sig, verifica semnatura
// cu cheia publica din config.h (CHEIE_ACTUALIZARE) si abia apoi instaleaza. Un fisier nesemnat cu
// cheia ta e refuzat, chiar daca cineva ar avea parola paginii sau ar umbla la descarcare.

#include "mbedtls/sha256.h"
#include "mbedtls/ecdsa.h"

#ifndef CHEIE_ACTUALIZARE
#define CHEIE_ACTUALIZARE ""   // cheia publica facuta cu semnare.html (130 de caractere, incepe cu 04)
#endif
#ifndef GITHUB_DEPOZIT
#define GITHUB_DEPOZIT ""      // ex. "andrei/paludariu"; se poate pune si din pagina (Setari > Placa)
#endif

String ghRepo;
volatile int ghSt = 0;        // 0 nu s-a verificat, 1 verific, 2 e la zi, 3 versiune noua, 4 descarc, 5 instalata, 6 eroare
volatile int ghPct = 0;
volatile bool ghLucreaza = false;
String ghNou, ghBin, ghSig, ghErr, ghAnuntat;

bool ghCheieOk() { return strlen(CHEIE_ACTUALIZARE) == 130; }

void ghIncarca() {
  ghRepo = prefs.getString("gh", GITHUB_DEPOZIT);
  ghAnuntat = prefs.getString("gha", "");
}

String ghJsonScurt() {   // pentru "sys" din /s
  return ",\"gh\":\"" + jEsc(ghRepo) + "\",\"ghn\":\"" + (ghSt == 3 ? jEsc(ghNou) : String("")) + "\",\"mq\":" + String(mqttCod()) + ",\"mqe\":\"" + jEsc(mqttErr()) + "\"";
}

// "v1.8.2" -> 1, 8, 2 ; intoarce >0 daca a e mai noua decat b
int versCompara(const String& a, const String& b) {
  int ia = 0, ib = 0;
  for (int k = 0; k < 4; k++) {
    while (ia < (int)a.length() && !isdigit((unsigned char)a[ia])) ia++;
    while (ib < (int)b.length() && !isdigit((unsigned char)b[ib])) ib++;
    long x = 0, y = 0;
    while (ia < (int)a.length() && isdigit((unsigned char)a[ia])) x = x * 10 + (a[ia++] - '0');
    while (ib < (int)b.length() && isdigit((unsigned char)b[ib])) y = y * 10 + (b[ib++] - '0');
    if (x != y) return x > y ? 1 : -1;
  }
  return 0;
}

// cauta in raspunsul GitHub adresa fisierului care se termina cu "sfarsit"
String ghUrl(const String& j, const char* sfarsit) {
  const char* k = "\"browser_download_url\":\"";
  int i = 0;
  while ((i = j.indexOf(k, i)) >= 0) {
    i += strlen(k);
    int e = j.indexOf('"', i);
    if (e < 0) break;
    String u = j.substring(i, e);
    if (u.endsWith(sfarsit)) return u;
    i = e;
  }
  return "";
}

bool ghVerifica() {
  if (ghRepo.indexOf('/') <= 0) { ghErr = "Scrie întâi depozitul GitHub, de exemplu numele-tau/paludariu."; return false; }
  TlsRand rand(60000);   // o singura legatura criptata odata (memoria)
  if (!rand.ok) { ghErr = "Placa e ocupată cu altă legătură. Mai încearcă peste un minut."; return false; }
  WiFiClientSecure cl;
  cl.setInsecure();   // integritatea o garanteaza semnatura, verificata inainte de instalare
  HTTPClient h;
  h.setUserAgent("paludariu-placa");
  h.setTimeout(15000);
  if (!h.begin(cl, "https://api.github.com/repos/" + ghRepo + "/releases/latest")) { ghErr = "Nu pot ajunge la GitHub."; return false; }
  h.addHeader("Accept", "application/vnd.github+json");
  int c = h.GET();
  if (c != 200) {
    ghErr = c == 404 ? String("Nu găsesc depozitul sau nu are încă nicio versiune publicată (Releases).") : "GitHub a răspuns cu eroarea " + String(c) + ".";
    h.end();
    return false;
  }
  String j = h.getString();
  h.end();
  int t = j.indexOf("\"tag_name\":\"");
  if (t < 0) { ghErr = "Răspuns neașteptat de la GitHub."; return false; }
  t += 12;
  ghNou = j.substring(t, j.indexOf('"', t));
  ghBin = ghUrl(j, ".bin");
  ghSig = ghUrl(j, ".bin.sig");
  if (versCompara(ghNou, VERSIUNE) <= 0) { ghSt = 2; return true; }
  if (!ghBin.length()) { ghErr = "Versiunea " + ghNou + " nu are fișierul .bin atașat."; return false; }
  ghSt = 3;
  return true;
}

bool semnaturaOk(const uint8_t hash[32], const uint8_t sig[64]) {
  uint8_t pub[65];
  if (!hexLa(String(CHEIE_ACTUALIZARE), pub, 65)) return false;
  mbedtls_ecp_group grp; mbedtls_ecp_point Q; mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
            mbedtls_ecp_point_read_binary(&grp, &Q, pub, 65) == 0 &&
            mbedtls_mpi_read_binary(&r, sig, 32) == 0 && mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
            mbedtls_ecdsa_verify(&grp, hash, 32, &Q, &r, &s) == 0;
  mbedtls_mpi_free(&r); mbedtls_mpi_free(&s); mbedtls_ecp_point_free(&Q); mbedtls_ecp_group_free(&grp);
  return ok;
}

bool ghInstaleaza() {
  if (!ghCheieOk()) { ghErr = "Lipsește cheia pentru semnături (CHEIE_ACTUALIZARE în config.h). Fără ea placa nu instalează nimic de pe internet."; return false; }
  if (!otaPosibil()) { ghErr = "Placa nu are loc pentru actualizare (Partition Scheme: Minimal SPIFFS with OTA)."; return false; }
  if (!ghSig.length()) { ghErr = "Versiunea " + ghNou + " nu are semnătura (.bin.sig). N-o instalez."; return false; }
  TlsRand rand(60000);   // o singura legatura criptata odata (memoria)
  if (!rand.ok) { ghErr = "Placa e ocupată cu altă legătură. Mai încearcă peste un minut."; return false; }
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient h;
  h.setUserAgent("paludariu-placa");
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.setTimeout(20000);
  // 1. semnatura: 128 de caractere hex (r si s)
  if (!h.begin(cl, ghSig) || h.GET() != 200) { h.end(); ghErr = "Nu pot descărca semnătura."; return false; }
  String sh = h.getString(); sh.trim();
  h.end();
  uint8_t sig[64];
  if (!hexLa(sh, sig, 64)) { ghErr = "Semnătura nu are formatul așteptat."; return false; }
  // 2. fisierul, scris direct in a doua jumatate a memoriei, cu amprenta SHA-256 calculata pe drum
  if (!h.begin(cl, ghBin)) { ghErr = "Nu pot descărca fișierul."; return false; }
  int c = h.GET();
  int len = h.getSize();
  if (c != 200 || len <= 0) { h.end(); ghErr = "Descărcarea n-a mers (" + String(c) + ")."; return false; }
  if (!Update.begin(len)) { h.end(); ghErr = String("Nu pot începe: ") + Update.errorString(); return false; }
  mbedtls_sha256_context sc;
  mbedtls_sha256_init(&sc);
  mbedtls_sha256_starts(&sc, 0);
  NetworkClient* st = h.getStreamPtr();
  static uint8_t buf[1024];
  int got = 0;
  unsigned long tUltim = millis();
  while (got < len && millis() - tUltim < 30000) {
    size_t av = st->available();
    if (!av) { if (!h.connected()) break; delay(5); continue; }
    int r = st->readBytes(buf, min(av, sizeof(buf)));
    if (r <= 0) continue;
    if (Update.write(buf, r) != (size_t)r) break;
    mbedtls_sha256_update(&sc, buf, r);
    got += r;
    ghPct = (int)(100LL * got / len);
    tUltim = millis();
  }
  h.end();
  uint8_t hash[32];
  mbedtls_sha256_finish(&sc, hash);
  mbedtls_sha256_free(&sc);
  if (got != len) { Update.abort(); ghErr = "Descărcarea s-a întrerupt. Încearcă din nou."; return false; }
  if (!semnaturaOk(hash, sig)) { Update.abort(); ghErr = "Semnătura nu se potrivește: fișierul nu e semnat cu cheia ta. Nu l-am instalat."; return false; }
  if (!Update.end(true)) { ghErr = String("Instalarea n-a mers: ") + Update.errorString(); return false; }
  return true;
}

void ghFir(void* p) {
  int mod = (int)(intptr_t)p;   // 1 verifica, 2 verifica si instaleaza, 3 verificare zilnica (fara zgomot)
  ghErr = "";
  ghSt = 1;
  if (mqttCod() == 2) vTaskDelay(pdMS_TO_TICKS(1500));   // lasam sa plece raspunsul, daca ai cerut prin internet
  mqPauza(true);   // legatura de la distanta tine ~35 KB; o oprim cat vorbim cu GitHub
  bool ok = ghVerifica();
  if (ok && mod == 2 && ghSt == 3) {
    ghSt = 4; ghPct = 0;
    ok = ghInstaleaza();
    if (ok) ghSt = 5;
  }
  if (!ok) ghSt = 6;
  if (!ok && !ghErr.length()) ghErr = "Verificarea n-a reușit (memorie puțină). Mai încearcă o dată.";
  if (ghSt != 5) mqPauza(false);   // dupa instalare placa reporneste oricum
#if FOLOSESTE_TELEGRAM
  if (ok && ghSt == 3 && ghAnuntat != ghNou) {
    ghAnuntat = ghNou;
    prefs.putString("gha", ghNou);
    tg("\xE2\xAC\x86\xEF\xB8\x8F Versiune nouă pentru placă: " + ghNou + " (acum ai v" VERSIUNE "). O instalezi din pagina plăcii: Setări → Placa → Actualizează.");   // ⬆️
  }
#endif
  ghLucreaza = false;
  vTaskDelete(nullptr);
}

void ghPorneste(int mod) {
  if (ghLucreaza || WiFi.status() != WL_CONNECTED) return;
  ghLucreaza = true;
  if (xTaskCreate(ghFir, "github", 12288, (void*)(intptr_t)mod, 1, nullptr) != pdPASS) ghLucreaza = false;
}

// Se apeleaza din loop(): verificarea zilnica si repornirea dupa instalare
void ghLoop() {
  static unsigned long tUltim = 0;
  static bool prima = true;
  if (ghSt == 5 && !restartLa) {
    prefs.putUChar("otaOk", 1);
    histSalveaza();
    evSalveaza();
    optSalveazaZile();
    restartLa = millis() + 2000;
  }
  if (ghRepo.indexOf('/') <= 0 || WiFi.status() != WL_CONNECTED) return;
  unsigned long pas = prima ? 120000UL : 86400000UL;   // prima data la 2 minute dupa pornire, apoi o data pe zi
  if (millis() - tUltim >= pas) { tUltim = millis(); prima = false; ghPorneste(3); }
}

// /gh                    -> starea
// /gh?repo=user/depozit  -> tine minte depozitul
// /gh?do=verifica        -> verifica acum
// /gh?do=instaleaza      -> descarca, verifica semnatura si instaleaza
Rasp apiGh(const Arg& a) {
  if (a.has("repo")) {
    String r = a.get("repo"); r.trim();
    if (r.startsWith("https://github.com/")) r = r.substring(19);
    while (r.endsWith("/")) r.remove(r.length() - 1);
    ghRepo = r;
    prefs.putString("gh", ghRepo);
    ghSt = 0; ghNou = ""; ghErr = "";
  }
  String d = a.get("do");
  if (d == "verifica") ghPorneste(1);
  else if (d == "instaleaza") ghPorneste(2);
  String j = "{\"repo\":\"" + jEsc(ghRepo) + "\",\"st\":" + String(ghSt) + ",\"cur\":\"" VERSIUNE "\",\"nou\":\"" + jEsc(ghNou) +
             "\",\"pct\":" + String(ghPct) + ",\"err\":\"" + jEsc(ghErr) + "\",\"cheie\":" + String(ghCheieOk() ? 1 : 0) +
             ",\"lucreaza\":" + String(ghLucreaza ? 1 : 0) + "}";
  return Rasp{200, "application/json", j};
}
void hGh() { if (!auth()) return; trimite(apiGh(Arg{"", true})); }
