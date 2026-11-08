// Paludariu - alertele si comenzile pe Telegram
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.

// ======================= TELEGRAM (alerte pe telefon, de oriunde) =======================
// Mesajele se trimit dintr-un fir separat, ca pagina web si releele sa nu se blocheze cat dureaza trimiterea.
#if FOLOSESTE_TELEGRAM
struct TgMsg { char t[700]; };
struct TgCmd { int8_t i; int8_t v; };      // i = -1: cere starea; altfel accesoriul i primeste modul v (0 auto, 1 on, 2 off)
QueueHandle_t tgQ = nullptr, tgCmdQ = nullptr;
long long tgOffset = 0;
const char* TG_NUME[4] = {"Pompă", "Lumină", "Ceață", "Ventilator"};
const char* TG_CHEI[4] = {"pompa", "lumina", "ceata", "ventilator"};

// Butoanele care apar in Telegram sub caseta de scris
const char* TG_TASTATURA =
  "{\"keyboard\":[[\"/stare\"],"
  "[\"/lumina on\",\"/lumina off\",\"/lumina auto\"],"
  "[\"/ceata on\",\"/ceata off\",\"/ceata auto\"],"
  "[\"/pompa on\",\"/pompa off\",\"/pompa auto\"],"
  "[\"/ventilator on\",\"/ventilator off\",\"/ventilator auto\"],"
  "[\"/ploaie\",\"/ploaie off\"],"
  "[\"/intretinere\",\"/raport\"],"
  "[\"/vacanta on\",\"/vacanta off\"]],"
  "\"resize_keyboard\":true,\"is_persistent\":true}";

bool tgConfigurat() { return strlen(TG_TOKEN) > 20 && strlen(TG_CHAT) > 0; }

String jsonEsc(const char* s) {
  String o;
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

String tgUrl(const char* metoda) { return String("https://api.telegram.org/bot") + TG_TOKEN + "/" + metoda; }

bool tgPost(const char* metoda, const String& body) {
  if (!tgConfigurat() || WiFi.status() != WL_CONNECTED) return false;
  TlsRand rand(30000);            // o singura legatura criptata odata (memoria)
  if (!rand.ok) return false;
  WiFiClientSecure cl;
  cl.setInsecure();               // fara verificarea certificatului: simplu; mesajele circula tot criptat
  HTTPClient h;
  h.setTimeout(8000);
  if (!h.begin(cl, tgUrl(metoda))) return false;
  h.addHeader("Content-Type", "application/json");
  int code = h.POST(body);
  h.end();
  if (code != 200) Serial.printf("Telegram %s: eroare %d\n", metoda, code);
  return code == 200;
}

bool tgTrimiteAcum(const char* text, const char* butoane = nullptr) {
  String body = String("{\"chat_id\":\"") + jsonEsc(TG_CHAT) + "\",\"text\":\"" + jsonEsc(text) + "\"";
  if (butoane) body += String(",\"reply_markup\":") + butoane;
  body += "}";
  return tgPost("sendMessage", body);
}

// ---------- citirea comenzilor (fara biblioteci in plus) ----------
String jsonText(const String& s, int i) {      // citeste un text JSON care incepe dupa ghilimele
  String o;
  while (i < (int)s.length() && s[i] != '"') {
    char c = s[i++];
    if (c != '\\' || i >= (int)s.length()) { o += c; continue; }
    char e = s[i++];
    if (e == 'n') o += '\n';
    else if (e == 'u' && i + 4 <= (int)s.length()) {
      unsigned cp = strtoul(s.substring(i, i + 4).c_str(), nullptr, 16);
      i += 4;
      if (cp < 0x80) o += (char)cp;
      else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
      else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    } else o += e;
  }
  return o;
}

String jsonNumar(const String& s, int i) {
  int j = i;
  while (j < (int)s.length() && (s[j] == '-' || (s[j] >= '0' && s[j] <= '9'))) j++;
  return s.substring(i, j);
}

String fara_diacritice(String t) {
  const char* d[][2] = {{"ă", "a"}, {"â", "a"}, {"î", "i"}, {"ș", "s"}, {"ş", "s"}, {"ț", "t"}, {"ţ", "t"},
                        {"Ă", "a"}, {"Â", "a"}, {"Î", "i"}, {"Ș", "s"}, {"Ş", "s"}, {"Ț", "t"}, {"Ţ", "t"}};
  for (auto& p : d) t.replace(p[0], p[1]);
  return t;
}

const char* TG_AJUTOR =
  "Comenzile tale:\n"
  "/stare: temperatura, umiditatea și ce e pornit\n"
  "/lumina on, /lumina off, /lumina auto\n"
  "La fel pentru /ceata, /pompa și /ventilator.\n"
  "/ploaie: plouă acum; /ploaie off: oprește ploaia; /ploaie auto sau /ploaie pauza: programul automat\n"
  "on și off pornesc sau opresc manual; auto readuce programul automat.\n"
  "/intretinere: ce urmează de făcut; /gata hranit (sau geam, apa, ceata, pompa) când ai terminat\n"
  "/vacanta on, /vacanta off: modul vacanță; /raport: raportul zilei\n"
  "Folosește butoanele de mai jos.";

void tgComanda(String text) {
  text = fara_diacritice(text);
  text.trim();
  text.toLowerCase();
  if (text.startsWith("/")) text.remove(0, 1);
  int at = text.indexOf('@');                    // "/stare@numele_botului" (din meniu sau din grupuri)
  if (at >= 0) {
    int sp0 = text.indexOf(' ', at);
    text = text.substring(0, at) + (sp0 < 0 ? String("") : text.substring(sp0));
  }
  text.replace("_", " ");                        // "/lumina_on" = "/lumina on"
  int sp = text.indexOf(' ');
  String cmd = sp < 0 ? text : text.substring(0, sp);
  String arg = sp < 0 ? String("") : text.substring(sp + 1);
  arg.trim();

  if (cmd == "start" || cmd == "ajutor" || cmd == "help" || cmd == "comenzi") { tgTrimiteAcum(TG_AJUTOR, TG_TASTATURA); return; }
  if (cmd == "stare" || cmd == "status") { TgCmd c = {-1, 0}; xQueueSend(tgCmdQ, &c, 0); return; }
  if (cmd == "intretinere" || cmd == "mementouri") { TgCmd c = {-2, 0}; xQueueSend(tgCmdQ, &c, 0); return; }
  if (cmd == "raport") { TgCmd c = {-5, 0}; xQueueSend(tgCmdQ, &c, 0); return; }
  if (cmd == "gata" || cmd == "facut") {
    int k = arg.indexOf("ceata") >= 0 ? 0 : (arg.indexOf("hran") >= 0 || arg.indexOf("crab") >= 0 || arg.indexOf("mancare") >= 0) ? 1
          : arg.indexOf("geam") >= 0 ? 2 : arg.indexOf("apa") >= 0 ? 3 : arg.indexOf("pomp") >= 0 ? 4 : -1;
    if (k < 0) { tgTrimiteAcum("Scrie de exemplu /gata hranit, /gata geam, /gata apa, /gata ceata sau /gata pompa."); return; }
    TgCmd c = {-3, (int8_t)k};
    xQueueSend(tgCmdQ, &c, 0);
    return;
  }
  if (cmd == "vacanta") {
    int v = (arg == "on" || arg == "pornit" || arg == "porneste") ? 1 : (arg == "off" || arg == "oprit" || arg == "opreste") ? 0 : 2;
    TgCmd c = {-4, (int8_t)v};
    xQueueSend(tgCmdQ, &c, 0);
    return;
  }
  if (cmd == "ploaie") {   // i = 4: 1 ploua acum, 2 opreste, 3 program automat pornit, 4 program automat oprit
    int v = (arg == "" || arg == "on" || arg == "acum" || arg == "porneste") ? 1 : (arg == "off" || arg == "stop" || arg == "opreste") ? 2
          : (arg == "auto") ? 3 : (arg == "pauza" || arg == "manual") ? 4 : -1;
    if (v < 0) { tgTrimiteAcum("Scrie /ploaie, /ploaie off, /ploaie auto sau /ploaie pauza."); return; }
    TgCmd c = {4, (int8_t)v};
    xQueueSend(tgCmdQ, &c, 0);
    return;
  }
  for (int i = 0; i < 4; i++) {
    if (cmd != TG_CHEI[i]) continue;
    int v = (arg == "auto") ? 0 : (arg == "on" || arg == "pornit" || arg == "porneste") ? 1
          : (arg == "off" || arg == "oprit" || arg == "opreste") ? 2 : -1;
    if (v < 0) {
      tgTrimiteAcum((String("Scrie de exemplu /") + TG_CHEI[i] + " on, /" + TG_CHEI[i] + " off sau /" + TG_CHEI[i] + " auto.").c_str());
      return;
    }
    TgCmd c = {(int8_t)i, (int8_t)v};
    xQueueSend(tgCmdQ, &c, 0);
    return;
  }
  tgTrimiteAcum("Nu cunosc comanda asta. Scrie /ajutor ca să vezi butoanele.");
}

/** Asteapta pana la 20 s comenzi noi (long polling). La primul apel doar sare peste comenzile vechi. */
void tgCitesteComenzi() {
  static bool pregatit = false;
  if (!tgConfigurat() || WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(3000)); return; }
  char off[24];
  if (pregatit) snprintf(off, sizeof off, "%lld", tgOffset); else strcpy(off, "-1");
  // Cu accesul de la distanta conectat, nu mai tinem legatura deschisa (ar ocupa ~40 KB tot timpul):
  // intrebam scurt si inchidem; tgFir() intreaba din nou peste ~15 s.
  const char* asteapta = (!pregatit || tgScurt()) ? "0" : mqConfigurat() ? "8" : "20";
  String url = tgUrl("getUpdates") + "?allowed_updates=%5B%22message%22%5D&timeout=" + asteapta +
               "&offset=" + off;
  int code;
  String body;
  {
    TlsRand rand(30000);
    if (!rand.ok) return;
    WiFiClientSecure cl;
    cl.setInsecure();
    HTTPClient h;
    h.setTimeout(25000);
    if (!h.begin(cl, url)) { rand.lasa(); vTaskDelay(pdMS_TO_TICKS(5000)); return; }
    code = h.GET();
    body = code == 200 ? h.getString() : String("");
    h.end();
  }   // legatura s-a inchis: memoria e libera pentru altii
  if (code != 200) { vTaskDelay(pdMS_TO_TICKS(code == 409 ? 30000 : 5000)); return; }

  int pos = 0;
  const char* K_UPD = "\"update_id\":";
  while ((pos = body.indexOf(K_UPD, pos)) >= 0) {
    int urm = body.indexOf(K_UPD, pos + 1);
    String bucata = body.substring(pos, urm < 0 ? body.length() : urm);
    long long id = atoll(jsonNumar(bucata, strlen(K_UPD)).c_str());
    if (id >= tgOffset) tgOffset = id + 1;
    pos = urm < 0 ? body.length() : urm;
    if (!pregatit) continue;                              // comenzi trimise cat placa era oprita: le ignoram
    int c = bucata.indexOf("\"chat\":{\"id\":");
    int t = bucata.indexOf("\"text\":\"");
    if (c < 0 || t < 0) continue;
    if (jsonNumar(bucata, c + 13) != String(TG_CHAT)) continue;   // doar tu poti comanda placa
    tgComanda(jsonText(bucata, t + 8));
  }
  if (!pregatit) {
    pregatit = true;
    tgPost("setMyCommands",
           "{\"commands\":[{\"command\":\"stare\",\"description\":\"Temperatura, umiditatea și ce e pornit\"},"
           "{\"command\":\"ajutor\",\"description\":\"Butoanele pentru comenzi\"}]}");
  }
}

// Accesul de la distanta e conectat: Telegram intreaba scurt, din 15 in 15 s, ca placa sa aiba memorie libera.
bool tgScurt() { return mqConfigurat() && mqttCod() == 2; }

void tgFir(void*) {
  TgMsg m;
  for (;;) {
    while (xQueueReceive(tgQ, &m, 0) == pdTRUE) {   // intai mesajele de trimis (alerte, raspunsuri)
      for (int i = 0; i < 4; i++) {                    // reincearca daca WiFi-ul tocmai lipseste
        if (tgTrimiteAcum(m.t)) break;
        vTaskDelay(pdMS_TO_TICKS(15000));
      }
      vTaskDelay(pdMS_TO_TICKS(1100));                 // Telegram cere maxim un mesaj pe secunda
    }
    tgCitesteComenzi();
    if (tgScurt())   // pauza, dar mesajele de trimis (alerte) pleaca imediat
      for (int i = 0; i < 150 && !uxQueueMessagesWaiting(tgQ) && tgScurt(); i++) vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void tgInit() {
  tgQ = xQueueCreate(8, sizeof(TgMsg));
  tgCmdQ = xQueueCreate(8, sizeof(TgCmd));
  xTaskCreate(tgFir, "telegram", 12288, nullptr, 1, nullptr);
}

/** Pune un mesaj in coada; antetul cu numele placii se adauga automat. */
void tg(const String& s) {
  if (!tgQ || !tgConfigurat()) return;
  TgMsg m;
  String full = String("🦀 ") + numePlaca + "\n" + s;
  strncpy(m.t, full.c_str(), sizeof(m.t) - 1);
  m.t[sizeof(m.t) - 1] = 0;
  xQueueSend(tgQ, &m, 0);
}

// ---------- alertele de pe placa ----------
// O problema trebuie sa dureze 2 minute inainte de alerta (fara alarme false); la fel si revenirea.
enum { AL_SENZOR, AL_FRIG, AL_CALD, AL_USCAT, AL_UMED, AL_LEDCALD, AL_LEDSENZ, AL_LEDFAN, AL_UMBLOC, AL_N };
const unsigned long AL_ASTEPTARE = 120000UL;
bool alActiv[AL_N] = {false};
unsigned long alDin[AL_N] = {0};

String nr(float v, int d) { String s = String(v, d); s.replace('.', ','); return s; }

void verificaAlerte() {
  bool senzorOk = !isnan(temp) && !isnan(hum);
  bool cond[AL_N];
  cond[AL_SENZOR] = !senzorOk;
  // in vacanta alertele sunt mai stricte: anunta mai devreme ca ceva o ia razna
  float tMin = vacanta ? 21.0f : 20.0f, tMax = vacanta ? 31.0f : 32.0f;
  float mUscat = vacanta ? 3.0f : 5.0f, mUmed = vacanta ? 2.0f : 3.0f;
  cond[AL_FRIG] = senzorOk && temp < tMin;
  cond[AL_CALD] = senzorOk && temp > tMax;
  cond[AL_USCAT] = senzorOk && hum < S.mistOn - mUscat;
  cond[AL_UMED] = senzorOk && hum > S.fanOn + mUmed;
  cond[AL_LEDCALD] = ledOverheat;
  cond[AL_LEDSENZ] = isnan(tLed) && !ledOverheat;
  cond[AL_LEDFAN] = fanFail;
  cond[AL_UMBLOC] = umBlocat;

  unsigned long now = millis();
  for (int i = 0; i < AL_N; i++) {
    if (cond[i] == alActiv[i]) { alDin[i] = 0; continue; }
    if (alDin[i] == 0) { alDin[i] = now ? now : 1; continue; }
    unsigned long astept = (i == AL_LEDCALD || i == AL_LEDFAN) ? 0 : AL_ASTEPTARE;   // supraincalzirea: imediat
    if (now - alDin[i] < astept) continue;
    alActiv[i] = cond[i];
    alDin[i] = 0;
    String w = "\xE2\x9A\xA0\xEF\xB8\x8F ", ok = "\xE2\x9C\x85 ";   // ⚠️ / ✅
    switch (i) {
      case AL_SENZOR:
        tg(cond[i] ? w + "Senzorul de temperatură și umiditate nu răspunde. Verifică firul DHT22."
                   : ok + "Senzorul de temperatură și umiditate merge din nou.");
        break;
      case AL_FRIG:
        tg(cond[i] ? w + "Temperatura a scăzut la " + nr(temp, 1) + " \xC2\xB0" "C."
                   : ok + "Temperatura a revenit la " + nr(temp, 1) + " \xC2\xB0" "C.");
        break;
      case AL_CALD:
        tg(cond[i] ? w + "Temperatura a urcat la " + nr(temp, 1) + " \xC2\xB0" "C."
                   : ok + "Temperatura a revenit la " + nr(temp, 1) + " \xC2\xB0" "C.");
        break;
      case AL_USCAT:
        tg(cond[i] ? w + "Umiditatea a scăzut la " + nr(hum, 0) + "% (ceața pornește sub " + nr(S.mistOn, 0) +
                         "%). Verifică apa din generatorul de ceață."
                   : ok + "Umiditatea a revenit la " + nr(hum, 0) + "%.");
        break;
      case AL_UMED:
        tg(cond[i] ? w + "Umiditatea a urcat la " + nr(hum, 0) + "% (ventilatorul pornește peste " + nr(S.fanOn, 0) +
                         "%). Verifică ventilatorul."
                   : ok + "Umiditatea a revenit la " + nr(hum, 0) + "%.");
        break;
      case AL_LEDCALD:
        tg(cond[i] ? w + "Radiatorul LED s-a supraîncălzit (" + nr(tLed, 1) + " \xC2\xB0" "C), așa că lumina s-a oprit. "
                         "Verifică ventilatorul de răcire."
                   : ok + "Radiatorul LED s-a răcit, lumina merge din nou după program.");
        break;
      case AL_LEDSENZ:
        tg(cond[i] ? w + "Senzorul radiatorului LED nu răspunde, deci ventilatorul de răcire merge la maxim."
                   : ok + "Senzorul radiatorului LED merge din nou.");
        break;
      case AL_LEDFAN:
        tg(cond[i] ? w + "Ventilatorul de răcire al LED-ului nu se învârte (" + String(fanRpm < 0 ? 0 : fanRpm) +
                         " rpm), așa că lumina s-a oprit. Verifică ventilatorul și firul de turație."
                   : ok + "Ventilatorul de răcire se învârte din nou (" + String(fanRpm) + " rpm), lumina merge după program.");
        break;
      case AL_UMBLOC:
        tg(cond[i] ? w + "Senzorul de umiditate pare blocat: " + String(umBlocatMotiv == 1 ? "arată peste 98% de aproape o oră" : "arată aceeași valoare de 3 ore") +
                         " (" + nr(hum, 1) + "%). Șterge-l de picături și ține-l departe de ceață și de geam. Până își revine, optimizarea nu învață din el."
                   : ok + "Senzorul de umiditate pare să meargă din nou (" + nr(hum, 0) + "%).");
        break;
    }
  }
}

// ---------- comenzile se executa in loop(), langa restul logicii ----------
String tgLinieAccesoriu(int i) {
  const char* em[4] = {"🔄", "💡", "🌫️", "🌀"};
  bool fem = i < 3;   // pompa, lumina, ceata: "pornita"; ventilatorul: "pornit"
  return String(em[i]) + " " + TG_NUME[i] + ": " + (stare[i] ? (fem ? "pornită" : "pornit") : (fem ? "oprită" : "oprit")) +
         (S.mod[i] == 0 ? " (automat)" : " (manual)");
}

String tgLiniePloaie() {
  String s = String("🌧️ Ploaie: ") + (ploaieOn ? "plouă acum" : "oprită");
  if (S.plAuto) { String u = plUrmatoarea(); s += u.length() ? ", următoarea la " + u : ", nicio oră setată"; }
  else s += ", programul automat e oprit";
#if FOLOSESTE_PLOAIE
  if (!plComOk) s += " (pompa nu a răspuns la ultima comandă)";
#endif
  return s;
}

String tgStare() {
  String s = "📊 Starea acum\n";
  s += "🌡️ Temperatură: " + (isnan(temp) ? String("senzor indisponibil") : nr(temp, 1) + " °C") + "\n";
  s += "💧 Umiditate: " + (isnan(hum) ? String("senzor indisponibil") : nr(hum, 0) + "%") + "\n";
  for (int i = 0; i < 4; i++) s += tgLinieAccesoriu(i) + "\n";
  if (tuyaConfigurat()) s += tgLiniePloaie() + "\n";
  s += "🔆 Radiator LED: " + (isnan(tLed) ? String("senzor indisponibil") : nr(tLed, 1) + " °C") +
       ", răcire " + String(fanLedPct) + "%";
  if (FANLED_TACH && fanRpm >= 0) s += ", " + String(fanRpm) + " rpm";
  if (ledOverheat) s += " (supraîncălzit, lumina e oprită)";
  if (fanFail) s += " (ventilatorul nu se învârte, lumina e oprită)";
  return s;
}

void tgExecutaComenzi() {
  TgCmd c;
  while (tgCmdQ && xQueueReceive(tgCmdQ, &c, 0) == pdTRUE) {
    if (c.i == -2) { tg(memTelegram()); continue; }
    if (c.i == -3) {
      if (memGata(c.v)) tg("\xE2\x9C\x85 Am notat: " + String(MEM_NUME[c.v]) + (M.zile[c.v] > 0 ? ". Următoarea dată peste " + String(M.zile[c.v]) + (M.zile[c.v] == 1 ? " zi." : " zile.") : String(".")));
      else tg("Placa nu știe încă ora. Încearcă peste un minut.");
      continue;
    }
    if (c.i == -4) {
      if (c.v == 2) tg(vacanta ? "\xF0\x9F\x8F\x9D\xEF\xB8\x8F Modul vacanță e pornit. /vacanta off îl oprește." : "Modul vacanță e oprit. /vacanta on îl pornește.");
      else if ((c.v == 1) == vacanta) tg(vacanta ? "Modul vacanță era deja pornit." : "Modul vacanță era deja oprit.");
      else vacSeteaza(c.v == 1);
      continue;
    }
    if (c.i == -5) { tg(raportZilnic()); continue; }
    if (c.i < 0) { tg(tgStare()); continue; }
    if (c.i == 4) {
      if (!tuyaConfigurat()) { tg("🌧️ Ploaia nu e configurată. Completează TUYA_IP, TUYA_ID și TUYA_KEY în cod."); continue; }
      if (c.v == 1) ploaieStart(S.plDur, true);
      else if (c.v == 2) ploaieStop(true);
      else { S.plAuto = c.v == 3; salveaza(); }
      tg("✅ " + tgLiniePloaie());
      continue;
    }
    if (c.i > 3 || c.v < 0 || c.v > 2) continue;
    S.mod[c.i] = c.v;
    salveaza();
    logica();
    tg("✅ " + tgLinieAccesoriu(c.i));
  }
}

// ---------- intretinerea si raportul zilei, ca text pentru Telegram ----------
String memTelegram() {
  String s = "\xF0\x9F\x93\x9D Întreținere\n";   // 📝
  bool vreunul = false;
  for (int i = 0; i < MEM_N; i++) {
    if (M.zile[i] <= 0) continue;
    vreunul = true;
    long r;
    s += "• " + String(MEM_NUME[i]) + ": ";
    if (!memRamas(i, r)) s += "pornește când placa află ora";
    else if (r <= 0) { long z = -r / 86400L; s += z == 0 ? String("e timpul, azi \xE2\x9A\xA0\xEF\xB8\x8F") : "trebuia acum " + String(z) + (z == 1 ? " zi \xE2\x9A\xA0\xEF\xB8\x8F" : " zile \xE2\x9A\xA0\xEF\xB8\x8F"); }
    else { long z = (r + 86399L) / 86400L; s += z <= 1 ? String("în următoarele 24 de ore") : "peste " + String(z) + " zile"; }
    s += "\n";
  }
  if (!vreunul) return "\xF0\x9F\x93\x9D Niciun memento pornit. Le pornești din pagina plăcii, la Setări.";
  return s + "Când termini: /gata hranit (sau geam, apa, ceata, pompa)";
}

String raportZilnic() {
  int16_t tMin = INT16_MAX, tMax = INT16_MIN, hMin = INT16_MAX, hMax = INT16_MIN;
  for (int i = 0; i < histCount; i++) {
    int k = (histHead - histCount + i + H_N) % H_N;
    if (histT[k] != INT16_MIN) { tMin = min(tMin, histT[k]); tMax = max(tMax, histT[k]); }
    if (histH[k] != INT16_MIN) { hMin = min(hMin, histH[k]); hMax = max(hMax, histH[k]); }
  }
  uint32_t de = acum() - 86400UL;
  int ploi = 0, ceata = 0;
  for (int k = 0; k < evCount; k++) {
    Ev& e = evl[(evHead - evCount + k + EV_N) % EV_N];
    if (e.t < de || !e.on) continue;
    if (e.out == 4) ploi++;
    if (e.out == 2) ceata++;
  }
  String s = String(vacanta ? "\xF0\x9F\x8F\x9D\xEF\xB8\x8F Raportul zilei (mod vacanță)\n" : "\xF0\x9F\x93\x8B Raportul zilei\n");   // 🏝️ / 📋
  s += "\xF0\x9F\x8C\xA1\xEF\xB8\x8F Temperatură: acum " + (isnan(temp) ? String("--") : nr(temp, 1)) + " °C";
  if (tMin != INT16_MAX) s += ", în 24 h între " + nr(tMin / 10.0f, 1) + " și " + nr(tMax / 10.0f, 1) + " °C";
  s += "\n\xF0\x9F\x92\xA7 Umiditate: acum " + (isnan(hum) ? String("--") : nr(hum, 0)) + "%";
  if (hMin != INT16_MAX) s += ", în 24 h între " + nr(hMin / 10.0f, 0) + " și " + nr(hMax / 10.0f, 0) + "%";
  s += "\n";
  if (tuyaConfigurat()) s += "\xF0\x9F\x8C\xA7\xEF\xB8\x8F Ploi: " + String(ploi) + " în ultimele 24 h\n";
  s += "\xE2\x98\x81\xEF\xB8\x8F Ceața a pornit de " + String(ceata) + " ori\n";
  s += "\xF0\x9F\x92\xA1 Lumina: " + String(stare[1] ? "aprinsă" : "stinsă") + "\n";
#if FOLOSESTE_NIVEL
  s += String(digitalRead(PIN_NIVEL) == LOW ? "\xE2\x9C\x85 Apă la ceață: suficientă\n" : "\xE2\x9A\xA0\xEF\xB8\x8F Apă la ceață: lipsește\n");
#endif
  String pr = "";
  if (alActiv[AL_SENZOR]) pr += "senzorul de climă nu răspunde; ";
  if (alActiv[AL_FRIG]) pr += "e prea frig; ";
  if (alActiv[AL_CALD]) pr += "e prea cald; ";
  if (alActiv[AL_USCAT]) pr += "e prea uscat; ";
  if (alActiv[AL_UMED]) pr += "e prea umed; ";
  if (alActiv[AL_LEDCALD]) pr += "radiatorul LED e supraîncălzit; ";
  if (alActiv[AL_LEDFAN]) pr += "ventilatorul LED nu se învârte; ";
  s += pr.length() ? "\xE2\x9A\xA0\xEF\xB8\x8F Probleme: " + pr.substring(0, pr.length() - 2) + "\n" : String("\xE2\x9C\x85 Nicio problemă\n");
  int dat = 0;
  for (int i = 0; i < MEM_N; i++) { long r; if (memRamas(i, r) && r <= 0) dat++; }
  if (dat) s += "\xF0\x9F\x93\x9D De făcut: " + String(dat) + (dat == 1 ? " lucru" : " lucruri") + " la întreținere (/intretinere)\n";
  return s;
}

// In modul vacanta, raportul pleaca singur in fiecare seara la 20:00
void raportLoop() {
  static unsigned long t0 = 0;
  static int ziuaTrimis = -1;
  if (!vacanta || millis() - t0 < 30000) return;
  t0 = millis();
  struct tm t;
  if (!oraLocala(&t) || t.tm_hour != 20 || t.tm_yday == ziuaTrimis) return;
  ziuaTrimis = t.tm_yday;
  tg(raportZilnic());
}

Rasp apiTgTest(const Arg& a) {
  (void)a;
  if (!tgConfigurat()) {
    return Rasp{503, "text/plain; charset=utf-8", "Telegram nu e configurat. Completează TG_TOKEN și TG_CHAT în cod."};
  }
  if (WiFi.status() != WL_CONNECTED) {
    return Rasp{503, "text/plain; charset=utf-8", "Placa nu e conectată la internet."};
  }
  tg("\xE2\x9C\x85 Test reușit. Alertele paludariului vor veni aici.");
  return Rasp{200, "text/plain; charset=utf-8", "Mesaj trimis. Verifică Telegram în câteva secunde."};
}
void hTgTest() { if (!auth()) return; trimite(apiTgTest(Arg{"", true})); }
#endif
