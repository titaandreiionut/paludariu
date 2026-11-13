// Paludariu - calendarul de hranire si jurnalul crabilor (1.8.9)
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.
//
// Zilele de hranire sunt biti: luni = 1, marti = 2, miercuri = 4, joi = 8, vineri = 16, sambata = 32, duminica = 64.
// Placa tine ultimele 16 hraniri si ultimele 12 notite din jurnal (naparlire, pui, nota).

const int HR_N = 16, CRJ_N = 12;
uint32_t hrT[HR_N];             // cand ai hranit (epoch), cea mai noua la final
int hrN = 0;
struct CrJ { uint32_t t; uint8_t tip; char txt[43]; };   // tip: 1 naparlire, 2 pui, 3 nota
CrJ crj[CRJ_N];
int crjN = 0;
uint8_t hrAnuntZi = 0xFF;       // ziua (din luna) in care am trimis deja mementoul

void hranIncarca() {
  size_t L = prefs.getBytesLength("hrn");
  if (L && L % 4 == 0 && L <= sizeof(hrT)) { prefs.getBytes("hrn", hrT, L); hrN = L / 4; }
  L = prefs.getBytesLength("crj");
  if (L && L % sizeof(CrJ) == 0 && L <= sizeof(crj)) { prefs.getBytes("crj", crj, L); crjN = L / sizeof(CrJ); }
  hrAnuntZi = prefs.getUChar("hrz", 0xFF);
}
void hranSalveaza() { if (hrN) prefs.putBytes("hrn", hrT, hrN * 4); else prefs.remove("hrn"); }
void crjSalveaza() { if (crjN) prefs.putBytes("crj", crj, crjN * sizeof(CrJ)); else prefs.remove("crj"); }

int hranBit(const struct tm& t) { return 1 << ((t.tm_wday + 6) % 7); }   // luni = bitul 0
bool hranitAzi() {
  struct tm t, u;
  if (!hrN || !oraLocala(&t)) return false;
  time_t x = hrT[hrN - 1];
  localtime_r(&x, &u);
  return u.tm_year == t.tm_year && u.tm_yday == t.tm_yday;
}
bool hranit() {
  time_t n = time(nullptr);
  if (n < 1000000000) return false;
  if (hranitAzi() && (uint32_t)n - hrT[hrN - 1] < 3600) return true;   // apasat de doua ori: o singura hranire
  if (hrN == HR_N) { memmove(hrT, hrT + 1, (HR_N - 1) * 4); hrN--; }
  hrT[hrN++] = (uint32_t)n;
  hranSalveaza();
  memGata(1);   // si mementoul vechi "Hrănit crabii", ca sa ramana la zi
  return true;
}

String hranScurt() {   // pentru /s: programul si ultimele hraniri
  String j = "{\"z\":" + String(S.hranZile) + ",\"o\":" + String(S.hranOra) + ",\"f\":[";
  for (int i = 0; i < hrN; i++) { j += String(hrT[i]); if (i < hrN - 1) j += ","; }
  return j + "]}";
}

Rasp apiHran(const Arg& a) {
  bool sch = false;
  if (a.has("f")) {
    if (!hranit()) return Rasp{503, "text/plain; charset=utf-8", "Placa nu știe încă ora. Încearcă peste un minut."};
  }
  if (a.has("z")) { S.hranZile = constrain((int)a.get("z").toInt(), 0, 127); sch = true; }
  if (a.has("o")) { S.hranOra = constrain((int)a.get("o").toInt(), 0, 23); sch = true; }
  if (sch) { salveaza(); hrAnuntZi = 0xFF; }
  if (a.has("add")) {
    time_t n = time(nullptr);
    if (n < 1000000000) return Rasp{503, "text/plain; charset=utf-8", "Placa nu știe încă ora. Încearcă peste un minut."};
    int tip = constrain((int)a.get("add").toInt(), 1, 3);
    String tx = a.get("txt"); tx.trim();
    if (crjN == CRJ_N) { memmove(crj, crj + 1, (CRJ_N - 1) * sizeof(CrJ)); crjN--; }
    CrJ& e = crj[crjN++];
    e.t = (uint32_t)n; e.tip = tip;
    // taiem la 42 de octeti fara sa rupem o litera cu diacritice (UTF-8)
    int L = min((int)tx.length(), 42);
    while (L > 0 && L < (int)tx.length() && (tx[L] & 0xC0) == 0x80) L--;
    memcpy(e.txt, tx.c_str(), L); e.txt[L] = 0;
    crjSalveaza();
  }
  if (a.has("del")) {
    uint32_t t = strtoul(a.get("del").c_str(), nullptr, 10);
    for (int i = 0; i < crjN; i++) if (crj[i].t == t) { memmove(crj + i, crj + i + 1, (crjN - i - 1) * sizeof(CrJ)); crjN--; crjSalveaza(); break; }
  }
  String j = "{\"z\":" + String(S.hranZile) + ",\"o\":" + String(S.hranOra) + ",\"azi\":" + String(hranitAzi() ? 1 : 0) + ",\"f\":[";
  for (int i = 0; i < hrN; i++) { j += String(hrT[i]); if (i < hrN - 1) j += ","; }
  j += "],\"j\":[";
  for (int i = 0; i < crjN; i++) {
    j += "[" + String(crj[i].t) + "," + String(crj[i].tip) + ",\"" + jEsc(String(crj[i].txt)) + "\"]";
    if (i < crjN - 1) j += ",";
  }
  return Rasp{200, "application/json", j + "]}"};
}
void hHran() { if (!auth()) return; trimite(apiHran(Arg{"", true})); }

// memento pe Telegram in zilele de hranire, la ora aleasa, daca nu ai hranit inca
void hranLoop(const struct tm& t, time_t n) {
  (void)n;
  if (!(S.hranZile & hranBit(t)) || t.tm_hour < S.hranOra) return;
  if (hrAnuntZi == t.tm_mday || hranitAzi()) return;
  hrAnuntZi = t.tm_mday;
  prefs.putUChar("hrz", hrAnuntZi);
#if FOLOSESTE_TELEGRAM
  tg("\xF0\x9F\xA6\x80 Azi e zi de hrănit crabii. Când le-ai dat de mâncare, scrie /gata hranit sau apasă „Am hrănit azi” în pagină.");   // 🦀
#endif
}
