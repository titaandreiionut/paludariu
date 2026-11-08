// Paludariu - optimizare din istoric
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.
//
// Placa tine statistici pe zile (temperatura, umiditatea, cate porniri au avut aparatele) si invata
// cat de repede scade umiditatea in fiecare ora din zi. Din ele:
//  - propune setari mai bune (sfaturi), pe care le aplici tu din pagina;
//  - daca vrei, isi muta singura pragurile de ceata si ventilator (reglaj automat), cel mult 1% pe zi
//    si numai intre limitele puse de tine;
//  - daca vrei, porneste ceata cu cateva minute inainte sa coboare umiditatea sub prag (ceata din timp);
//  - duminica la 20:00 trimite un raport pe Telegram.
// Cu "Umiditate dorita" pornita (1.8.8), reglajul automat nu muta tinta: muta doar pragurile din jurul ei,
// cel mult 1% pe zi si cel mult 5% fata de pragurile calculate. Daca tot nu reuseste sa tina tinta, te anunta.
// Cat timp senzorul de umiditate pare blocat, placa nu invata din citirile lui.
// Totul porneste OPRIT. Invatarea incepe abia cand apesi "Paludariul e gata" in pagina, ca sa nu invete
// din aerul camerei. Primele OPT_ZILE zile doar aduna date.

#define OPT_ZILE 4          // cate zile aduna date inainte de sfaturi si reglaj
#define OZ_N 14             // cate zile de statistici tine minte

struct ZiOpt {              // o zi (ora locala)
  uint32_t zi;              // epoch / 86400 la miezul noptii local (0 = gol)
  uint8_t wday;             // 0 duminica ... 6 sambata
  int16_t tMin, tMax, hMin, hMax;   // x10
  int32_t tSum, hSum;       // x10, pentru medie
  uint16_t n;               // minute masurate
  uint16_t sub, peste;      // minute cu umiditatea sub / peste limitele tale
  uint16_t ceata, vent, pompa, ploi;   // porniri
  uint16_t ceataMin;        // minute de ceata
};
struct OptAj { uint32_t t; uint8_t p; uint8_t de, la, aut; };   // ajustare: parametru, valoare veche, noua, automata?
struct OptCfg {
  uint32_t start;           // cand a inceput invatarea (0 = oprita)
  uint32_t startZi;         // ziua in care a inceput (ca oz[].zi)
  uint8_t autoOn, early, weekly, earlyMin;
  uint8_t lo, hi;           // limitele umiditatii (%)
  uint8_t areOrig;          // pragurile de dinainte de reglajul automat sunt salvate
  uint8_t orig[4];          // mistOn, mistOff, fanOn, fanOff
  uint32_t ignZi;           // ziua in care ai apasat "Nu acum"
  uint8_t ign;              // ce sfaturi ai amanat azi (biti)
  OptAj aj[6];              // ultimele ajustari
  uint8_t ajN;
};
ZiOpt oz[OZ_N];
int ozCur = -1;             // ziua de azi in oz
OptCfg OC;
int16_t oSlope[24];         // cat scade umiditatea pe ora, in fiecare ora din zi (x10, %/h); 0 = inca nu stim
uint8_t oSlopeN[24];
bool ozSchimbat = false;
bool ceataDinTimp = false;  // ceata a pornit mai devreme (pentru jurnal)

const char* OPT_PN[4] = {"pornire ceață", "oprire ceață", "pornire ventilator", "oprire ventilator"};

static String onr(float v, int d) { String s = String(v, d); s.replace('.', ','); return s; }
static float* optParam(int p) { return p == 0 ? &S.mistOn : p == 1 ? &S.mistOff : p == 2 ? &S.fanOn : &S.fanOff; }
// limitele umiditatii pentru statistici: din tinta, daca e pornita; altfel cele puse de tine
static int optLo() { return S.umAuto ? S.umTinta - S.umStrict - 2 : OC.lo; }
static int optHi() { return S.umAuto ? min(99, S.umTinta + S.umStrict + 2) : OC.hi; }
uint8_t umNuTin = 0;        // 1 = reglajul a ajuns la margine si tot nu tine tinta
void optTintaNoua() { if (umNuTin) { umNuTin = 0; prefs.putUChar("unt", 0); } }

void optIncarca() {
  memset(&OC, 0, sizeof(OC));
  OC.lo = 75; OC.hi = 92; OC.earlyMin = 5;
  if (prefs.getBytesLength("oc") == sizeof(OC)) prefs.getBytes("oc", &OC, sizeof(OC));
  memset(oz, 0, sizeof(oz));
  if (prefs.getBytesLength("oz") == sizeof(oz)) prefs.getBytes("oz", oz, sizeof(oz));
  memset(oSlope, 0, sizeof(oSlope)); memset(oSlopeN, 0, sizeof(oSlopeN));
  if (prefs.getBytesLength("osl") == sizeof(oSlope)) prefs.getBytes("osl", oSlope, sizeof(oSlope));
  if (prefs.getBytesLength("osn") == sizeof(oSlopeN)) prefs.getBytes("osn", oSlopeN, sizeof(oSlopeN));
  umNuTin = prefs.getUChar("unt", 0);
}
void optSalveazaCfg() { prefs.putBytes("oc", &OC, sizeof(OC)); }
void optSalveazaZile() {
  if (!ozSchimbat) return;
  ozSchimbat = false;
  prefs.putBytes("oz", oz, sizeof(oz));
  prefs.putBytes("osl", oSlope, sizeof(oSlope));
  prefs.putBytes("osn", oSlopeN, sizeof(oSlopeN));
}

// ziua locala de azi (numar de zile de la 1970) si ziua din saptamana
static bool optAzi(uint32_t& zi, uint8_t& wd, struct tm& t) {
  time_t n = time(nullptr);
  if (n < 1000000000 || !oraLocala(&t)) return false;
  long off = (long)t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  zi = (uint32_t)(((long)n - off + 43200L) / 86400L);   // un numar unic pentru fiecare zi (dupa ora locala)
  wd = t.tm_wday;
  return true;
}
static ZiOpt* optZiua(uint32_t zi, uint8_t wd) {
  if (ozCur >= 0 && oz[ozCur].zi == zi) return &oz[ozCur];
  for (int i = 0; i < OZ_N; i++) if (oz[i].zi == zi) { ozCur = i; return &oz[i]; }
  int k = 0;   // o zi noua: luam locul celei mai vechi
  for (int i = 1; i < OZ_N; i++) if (oz[i].zi < oz[k].zi) k = i;
  memset(&oz[k], 0, sizeof(ZiOpt));
  oz[k].zi = zi; oz[k].wday = wd;
  oz[k].tMin = oz[k].hMin = INT16_MAX; oz[k].tMax = oz[k].hMax = INT16_MIN;
  ozCur = k; ozSchimbat = true;
  return &oz[k];
}

// Din logEvM(): numaram pornirile
void optEveniment(int i, bool on) {
  if (!on || i < 0 || i > 4) return;
  uint32_t zi; uint8_t wd; struct tm t;
  if (!optAzi(zi, wd, t)) return;
  ZiOpt* z = optZiua(zi, wd);
  if (i == 2) z->ceata++; else if (i == 3) z->vent++; else if (i == 0) z->pompa++; else if (i == 4) z->ploi++;
  ozSchimbat = true;
}

uint32_t optZileStrange() { time_t n = time(nullptr); return (OC.start && n > (time_t)OC.start) ? (uint32_t)((n - OC.start) / 86400UL) : 0; }
bool optGata() { return OC.start && optZileStrange() >= OPT_ZILE; }

// Media pe zilele complete de la pornirea invatarii (cel mult ultimele 7)
struct OptMed { int zile; float porniri, sub, peste, h, t, vent; int16_t hMin; };
static OptMed optMedii(bool doarInvatare) {
  OptMed m = {0, 0, 0, 0, 0, 0, 0, INT16_MAX};
  uint32_t azi; uint8_t wd; struct tm t;
  if (!optAzi(azi, wd, t)) return m;
  uint32_t de = OC.start ? OC.startZi + 1 : 0;   // prima zi intreaga dupa pornire
  long n = 0, sub = 0, pes = 0; double hs = 0, ts = 0;
  for (int i = 0; i < OZ_N; i++) {
    ZiOpt& z = oz[i];
    if (!z.zi || z.zi >= azi || z.zi + 7 < azi || z.n < 600) continue;   // doar zile intregi (macar 10 ore masurate)
    if (doarInvatare && z.zi < de) continue;
    m.zile++; m.porniri += z.ceata; m.vent += z.vent;
    n += z.n; sub += z.sub; pes += z.peste; hs += z.hSum; ts += z.tSum;
    if (z.hMin < m.hMin) m.hMin = z.hMin;
  }
  if (!m.zile || !n) return m;
  m.porniri /= m.zile; m.vent /= m.zile;
  m.sub = 100.0f * sub / n; m.peste = 100.0f * pes / n;
  m.h = hs / n / 10.0; m.t = ts / n / 10.0;
  return m;
}

// ---------- sfaturile ----------
struct Sfat { uint8_t id; int8_t p; float de, la; String titlu, text; };
static int optSfaturi(Sfat* out, int max) {
  int k = 0;
  if (!optGata()) return 0;
  OptMed m = optMedii(true);
  if (m.zile < 2) return 0;
  uint32_t azi; uint8_t wd; struct tm t;
  bool am = optAzi(azi, wd, t);
  uint8_t ign = (am && OC.ignZi == azi) ? OC.ign : 0;
  if (S.umAuto) ign |= 1 | 2 | 4 | 8;
  // 1. ceata porneste foarte des, dar umiditatea nu coboara: pornim ceata putin mai jos (interval mai larg)
  if (k < max && !(ign & 1) && m.porniri > 30 && m.sub < 2 && S.mistOn - 1 >= optLo() + 2) {
    out[k++] = Sfat{1, 0, S.mistOn, S.mistOn - 1, "Ceața pornește prea des",
      "A pornit în medie de " + String((int)lroundf(m.porniri)) + " de ori pe zi, iar umiditatea a stat aproape tot timpul peste " + String(optLo()) +
      "%. Dacă pornește la " + String((int)S.mistOn - 1) + "% în loc de " + String((int)S.mistOn) + "%, intervalul e mai larg și pornește mai rar."};
  }
  // 2. prea uscat: pornim ceata mai devreme
  if (k < max && !(ign & 2) && m.sub > 5 && S.mistOn + 1 <= S.mistOff - 3) {
    out[k++] = Sfat{2, 0, S.mistOn, S.mistOn + 1, "Aerul e prea des uscat",
      "Umiditatea a stat sub " + String(optLo()) + "% în " + String((int)lroundf(m.sub)) + "% din timp. Ceața ar porni la " +
      String((int)S.mistOn + 1) + "% în loc de " + String((int)S.mistOn) + "%."};
  }
  // 3. prea umed: oprim ceata mai devreme
  if (k < max && !(ign & 4) && m.peste > 5 && S.mistOff - 1 >= S.mistOn + 3) {
    out[k++] = Sfat{3, 1, S.mistOff, S.mistOff - 1, "Prea umed",
      "Umiditatea a trecut de " + String(optHi()) + "% în " + String((int)lroundf(m.peste)) + "% din timp. Ceața s-ar opri la " +
      String((int)S.mistOff - 1) + "% în loc de " + String((int)S.mistOff) + "%."};
  }
  // 4. prea umed si ventilatorul porneste tarziu
  if (k < max && !(ign & 8) && m.peste > 5 && S.fanOn > optHi() && S.fanOn - 1 >= S.mistOff + 2) {
    out[k++] = Sfat{4, 2, S.fanOn, S.fanOn - 1, "Ventilatorul pornește târziu",
      "Umiditatea trece des de " + String(optHi()) + "%, iar ventilatorul pornește abia la " + String((int)S.fanOn) + "%. Ar porni la " +
      String((int)S.fanOn - 1) + "%."};
  }
  // 5. ceata din timp ar ajuta
  if (k < max && !(ign & 16) && !OC.early) {
    int hm = -1; int16_t mn = 0;
    for (int h = 0; h < 24; h++) if (oSlopeN[h] >= 3 && oSlope[h] < mn) { mn = oSlope[h]; hm = h; }
    if (hm >= 0 && mn <= -30) {
      out[k++] = Sfat{5, -1, 0, 1, "Umiditatea scade repede la " + String(hm) + ":00",
        "Între " + String(hm) + ":00 și " + String((hm + 1) % 24) + ":00 umiditatea scade cu până la " + onr(-mn / 10.0f, 1) +
        "% pe oră. „Ceața din timp” ar porni ceața cu câteva minute înainte."};
    }
  }
  return k;
}

static void optAjustare(int p, float de, float la, bool aut) {
  if (OC.ajN < 6) OC.ajN++;
  memmove(&OC.aj[1], &OC.aj[0], sizeof(OptAj) * 5);
  OC.aj[0] = OptAj{acum(), (uint8_t)p, (uint8_t)lroundf(de), (uint8_t)lroundf(la), (uint8_t)(aut ? 1 : 0)};
  // in jurnal: 20 + parametrul; on = 1 automat / 0 aplicat de tine; man = valoarea noua; h = valoarea veche x10
  Ev& e = evl[evHead];
  e.t = acum(); e.out = 20 + p; e.on = aut; e.man = (uint8_t)lroundf(la); e.h = (int16_t)lroundf(de * 10);
  evHead = (evHead + 1) % EV_N;
  if (evCount < EV_N) evCount++;
  evSchimbat = true;
}

static bool optAplica(int id) {
  Sfat sf[5];
  int n = optSfaturi(sf, 5);
  for (int i = 0; i < n; i++) {
    if (sf[i].id != id) continue;
    if (sf[i].p < 0) { OC.early = 1; optSalveazaCfg(); return true; }
    float* v = optParam(sf[i].p);
    optAjustare(sf[i].p, *v, sf[i].la, false);
    *v = sf[i].la;
    if (S.fanOff >= S.fanOn) S.fanOff = S.fanOn - 2;
    salveaza(); optSalveazaCfg(); logica();
    return true;
  }
  return false;
}

// Reglajul pe tinta: tinta ramane fixa, se muta pragurile din jurul ei (1% pe zi, cel mult 5% de la cele calculate)
static void optReglajTinta(const OptMed& m) {
  float H = S.umTinta;
  bool sch = false, margine = false;
  auto poate = [](int p, float v) { float b = umBaza(p); return v >= b - 5 && v <= b + 5 && v >= 50 && v <= 99; };
  auto muta = [&](int p, float d) { float* v = optParam(p); optAjustare(p, *v, *v + d, true); *v += d; sch = true; };
  if (m.h < H - 1) {                                   // media zilei sub tinta: ceata porneste si se opreste mai sus
    if (poate(0, S.mistOn + 1) && S.mistOn + 1 <= S.mistOff - 2) muta(0, 1); else margine = true;
    if (poate(1, S.mistOff + 1) && S.mistOff + 1 <= S.fanOn - 2) muta(1, 1);
  } else if (m.h > H + 1) {                            // media peste tinta: ceata se opreste mai devreme
    if (poate(1, S.mistOff - 1) && S.mistOff - 1 >= S.mistOn + 2) muta(1, -1); else margine = true;
    if (m.h > H + 3 && poate(2, S.fanOn - 1) && S.fanOn - 1 >= S.mistOff + 2) {   // mult peste: si ventilatorul porneste mai devreme
      muta(2, -1);
      if (S.fanOff >= S.fanOn) S.fanOff = S.fanOn - 2;
    }
  } else if (m.porniri > 30 && poate(0, S.mistOn - 1) && S.mistOn - 1 >= 50) {
    muta(0, -1);                                       // tine tinta, dar ceata porneste foarte des: interval mai larg
  }
  uint8_t nt = (margine && fabsf(m.h - H) > 2) ? 1 : 0;
  if (nt != umNuTin) {
    umNuTin = nt; prefs.putUChar("unt", nt);
#if FOLOSESTE_TELEGRAM
    if (nt) tg("\xE2\x9A\xA0\xEF\xB8\x8F Nu reușesc să țin umiditatea la " + String(S.umTinta) + "%: media a fost " + String((int)lroundf(m.h)) +
               "%, iar pragurile au ajuns la margine. " + String(m.h < H ? "Verifică apa și generatorul de ceață." : "Verifică ventilatorul și aerisirea."));
#endif
  }
  if (sch) { salveaza(); logica(); }
}

// Reglajul automat: o data pe zi, cel mult un pas de 1% pe parametru, numai in limitele tale
static void optReglaj() {
  if (!OC.autoOn || !optGata()) return;
  OptMed m = optMedii(true);
  if (m.zile < 2) return;
  if (S.umAuto) {
    if (!OC.areOrig) { OC.areOrig = 1; for (int p = 0; p < 4; p++) OC.orig[p] = (uint8_t)lroundf(*optParam(p)); }
    optReglajTinta(m); optSalveazaCfg(); return;
  }
  if (!OC.areOrig) { OC.areOrig = 1; for (int p = 0; p < 4; p++) OC.orig[p] = (uint8_t)lroundf(*optParam(p)); }
  bool sch = false;
  if (m.sub > 5 && S.mistOn + 1 <= S.mistOff - 3 && S.mistOn + 1 <= OC.hi - 3) { optAjustare(0, S.mistOn, S.mistOn + 1, true); S.mistOn += 1; sch = true; }
  else if (m.porniri > 30 && m.sub < 2 && S.mistOn - 1 >= OC.lo + 2) { optAjustare(0, S.mistOn, S.mistOn - 1, true); S.mistOn -= 1; sch = true; }
  if (m.peste > 5 && S.mistOff - 1 >= S.mistOn + 3 && S.mistOff - 1 >= OC.lo + 5) { optAjustare(1, S.mistOff, S.mistOff - 1, true); S.mistOff -= 1; sch = true; }
  else if (m.peste < 1 && m.sub > 5 && S.mistOff + 1 <= OC.hi && S.mistOff + 1 <= S.fanOn - 2) { optAjustare(1, S.mistOff, S.mistOff + 1, true); S.mistOff += 1; sch = true; }
  if (m.peste > 5 && S.fanOn > OC.hi && S.fanOn - 1 >= S.mistOff + 2) {
    optAjustare(2, S.fanOn, S.fanOn - 1, true); S.fanOn -= 1;
    if (S.fanOff >= S.fanOn) S.fanOff = S.fanOn - 2;
    sch = true;
  }
  if (sch) { salveaza(); logica(); }
  optSalveazaCfg();
}

// ---------- ceata din timp: apelata din logica() ----------
// true = porneste ceata acum, pentru ca in cateva minute umiditatea ar cobori sub prag
bool optCeataDinTimp(float h) {
  if (!OC.early || !optGata() || isnan(h) || h >= S.mistOn + 3 || h < S.mistOn) return false;
  struct tm t;
  if (!oraLocala(&t)) return false;
  int hh = t.tm_hour;
  if (oSlopeN[hh] < 3 || oSlope[hh] > -10) return false;   // scade cu mai putin de 1%/ora: nu e nevoie
  float prev = h + oSlope[hh] / 10.0f * OC.earlyMin / 60.0f;
  return prev < S.mistOn;
}

// ---------- in fiecare minut: statistici si invatare ----------
void optLoop() {
  static unsigned long tMin = 0, tSal = 0;
  static float hStart = NAN; static int hOra = -1, minFeree = 0; static bool liber = true;
  if (millis() - tMin < 60000UL) return;
  tMin = millis();
  uint32_t zi; uint8_t wd; struct tm t;
  if (!optAzi(zi, wd, t)) return;
  ZiOpt* z = optZiua(zi, wd);
  if (!isnan(temp) && !isnan(hum) && !umBlocat) {   // senzor blocat: nu invatam din el
    int16_t tt = (int16_t)lroundf(temp * 10), hh = (int16_t)lroundf(hum * 10);
    if (tt < z->tMin) z->tMin = tt; if (tt > z->tMax) z->tMax = tt;
    if (hh < z->hMin) z->hMin = hh; if (hh > z->hMax) z->hMax = hh;
    z->tSum += tt; z->hSum += hh; z->n++;
    if (hum < optLo()) z->sub++; else if (hum > optHi()) z->peste++;
    if (stare[2]) z->ceataMin++;
    ozSchimbat = true;
    // invatam cat de repede se usuca aerul singur: ferestre de 10 minute fara ceata, ventilator si ploaie
    if (OC.start) {
      bool acum_ = !stare[2] && !stare[3] && !ploaieOn;
      if (hOra != t.tm_hour || !acum_ || isnan(hStart)) { hStart = hum; hOra = t.tm_hour; minFeree = 0; liber = acum_; }
      else if (++minFeree >= 10) {
        if (liber) {
          int16_t s = (int16_t)lroundf((hum - hStart) * 6 * 10);   // %/ora x10
          int h = hOra;
          oSlope[h] = oSlopeN[h] == 0 ? s : (int16_t)lroundf(oSlope[h] * 0.8f + s * 0.2f);
          if (oSlopeN[h] < 250) oSlopeN[h]++;
        }
        hStart = hum; minFeree = 0; liber = true;
      }
    }
  }
  // o data pe zi, la 6:00: reglajul automat
  static uint32_t ziReglaj = 0;
  if (t.tm_hour == 6 && ziReglaj != zi) { ziReglaj = zi; optReglaj(); }
#if FOLOSESTE_TELEGRAM
  static uint32_t ziRaport = 0;
  if (OC.weekly && t.tm_wday == 0 && t.tm_hour == 20 && ziRaport != zi) { ziRaport = zi; tg(optRaport()); }
#endif
  if (millis() - tSal > 3600000UL) { tSal = millis(); optSalveazaZile(); }
}

#if FOLOSESTE_TELEGRAM
String optRaport() {
  static const char* ZN[7] = {"duminică", "luni", "marți", "miercuri", "joi", "vineri", "sâmbătă"};
  uint32_t azi; uint8_t wd; struct tm t;
  optAzi(azi, wd, t);
  long n = 0; double hs = 0, ts = 0; int16_t tMin = INT16_MAX, tMax = INT16_MIN, hMin = INT16_MAX, hMax = INT16_MIN;
  int zc = -1, zu = -1; long ce = 0, ve = 0, po = 0, pl = 0; int zile = 0;
  for (int i = 0; i < OZ_N; i++) {
    ZiOpt& z = oz[i];
    if (!z.zi || z.zi + 7 <= azi || z.zi > azi || !z.n) continue;
    zile++; n += z.n; hs += z.hSum; ts += z.tSum;
    if (z.tMin < tMin) tMin = z.tMin;
    if (z.tMax > tMax) { tMax = z.tMax; zc = z.wday; }
    if (z.hMin < hMin) { hMin = z.hMin; zu = z.wday; }
    if (z.hMax > hMax) hMax = z.hMax;
    ce += z.ceata; ve += z.vent; po += z.pompa; pl += z.ploi;
  }
  String s = "\xF0\x9F\x93\x8A Raportul săptămânii\n";   // 📊
  if (!n) return s + "Încă nu am date pentru săptămâna asta.";
  s += "\xF0\x9F\x8C\xA1\xEF\xB8\x8F " + onr(tMin / 10.0f, 1) + " – " + onr(tMax / 10.0f, 1) + " °C, medie " + onr(ts / n / 10.0, 1) + " °C\n";   // 🌡️
  s += "\xF0\x9F\x92\xA7 " + String(hMin / 10) + " – " + String(hMax / 10) + "%, medie " + String((int)lround(hs / n / 10.0)) + "%\n";   // 💧
  if (zc >= 0) s += "Cea mai caldă: " + String(ZN[zc]) + " (" + onr(tMax / 10.0f, 1) + " °C)\n";
  if (zu >= 0) s += "Cea mai uscată: " + String(ZN[zu]) + " (" + String(hMin / 10) + "%)\n";
  s += "Porniri: ceață " + String(ce) + " (" + String(zile ? ce / zile : 0) + " pe zi), ventilator " + String(ve) + ", pompă " + String(po);
  if (pl) s += ", ploaie " + String(pl);
  s += "\n";
  int aut = 0;
  for (int i = 0; i < OC.ajN; i++) if (OC.aj[i].aut && acum() - OC.aj[i].t < 7 * 86400UL) aut++;
  if (aut) s += "\xF0\x9F\x94\xA7 Reglajul automat a schimbat pragurile de " + String(aut) + " ori.\n";   // 🔧
  Sfat sf[3];
  int k = optSfaturi(sf, 3);
  if (k) {
    s += "\xF0\x9F\x92\xA1 Sfaturi:\n";   // 💡
    for (int i = 0; i < k; i++) s += "• " + sf[i].titlu + "\n";
    s += "Le aplici din pagina plăcii: Setări → Optimizare din istoric.";
  } else if (!OC.start) {
    s += "Învățarea e oprită: o pornești din pagina plăcii, după ce placa e în paludariu.";
  }
  return s;
}
#endif

// Pentru /s: [etapa, zile, sfaturi, reglaj automat]. Etapa: 0 oprit, 1 aduna date, 2 gata
String optScurt() {
  int et = !OC.start ? 0 : optGata() ? 2 : 1;
  static int k = 0; static unsigned long tK = 0; static uint32_t cheie = 0;
  uint32_t ch = OC.start ^ (OC.ign << 24) ^ (uint32_t)lroundf(S.mistOn * 7 + S.mistOff * 13 + S.fanOn * 17) ^ OC.early;
  if (et != 2) k = 0;
  else if (!tK || millis() - tK > 60000UL || ch != cheie) {   // /s vine des: numaram sfaturile cel mult o data pe minut
    Sfat sf[5];
    k = optSfaturi(sf, 5); tK = millis(); cheie = ch;
  }
  return ",\"op\":[" + String(et) + "," + String(optZileStrange()) + "," + String(k) + "," + String(OC.autoOn) + "," + String(umNuTin) + "]";
}

// /opt                       -> starea
// /opt?start=1               -> "Paludariul e gata": invata de acum (si "incepe din nou de la zero")
// /opt?off=1                 -> opreste invatarea, reglajul si ceata din timp
// /opt?auto=0|1  early=0|1  weekly=0|1  em=minute  lo=%  hi=%
// /opt?apply=id  dismiss=id  undo=1
Rasp apiOpt(const Arg& a) {
  bool sch = false;
  if (a.has("start")) {
    OC.start = time(nullptr) > 1000000000 ? (uint32_t)time(nullptr) : 0;
    { uint32_t zi; uint8_t wd; struct tm t; OC.startZi = optAzi(zi, wd, t) ? zi : 0; }
    memset(oSlope, 0, sizeof(oSlope)); memset(oSlopeN, 0, sizeof(oSlopeN));
    OC.ign = 0; ozSchimbat = true; sch = true;
    if (!OC.start) return Rasp{503, "text/plain; charset=utf-8", "Placa încă nu știe ora. Mai încearcă peste un minut."};
  }
  if (a.has("off")) { OC.start = 0; OC.autoOn = 0; OC.early = 0; sch = true; }
  if (a.has("auto")) { OC.autoOn = a.get("auto").toInt() ? 1 : 0; if (OC.autoOn && !OC.areOrig) { OC.areOrig = 1; for (int p = 0; p < 4; p++) OC.orig[p] = (uint8_t)lroundf(*optParam(p)); } sch = true; }
  if (a.has("early")) { OC.early = a.get("early").toInt() ? 1 : 0; sch = true; }
  if (a.has("weekly")) { OC.weekly = a.get("weekly").toInt() ? 1 : 0; sch = true; }
  if (a.has("em")) { OC.earlyMin = constrain(a.get("em").toInt(), 1, 15); sch = true; }
  if (a.has("lo")) { OC.lo = constrain(a.get("lo").toInt(), 50, 95); sch = true; }
  if (a.has("hi")) { OC.hi = constrain(a.get("hi").toInt(), 55, 99); sch = true; }
  if (OC.hi < OC.lo + 5) OC.hi = min(99, OC.lo + 5);
  if (a.has("dismiss")) {
    uint32_t zi; uint8_t wd; struct tm t;
    if (optAzi(zi, wd, t)) { if (OC.ignZi != zi) { OC.ignZi = zi; OC.ign = 0; } int id = a.get("dismiss").toInt(); if (id >= 1 && id <= 8) OC.ign |= 1 << (id - 1); }
    sch = true;
  }
  if (a.has("apply") && !optAplica(a.get("apply").toInt())) return Rasp{409, "text/plain; charset=utf-8", "Sfatul nu mai e valabil. Reîncarcă pagina."};
  if (a.has("undo") && S.umAuto) {
    for (int p = 0; p < 4; p++) { float* v = optParam(p); float b = umBaza(p); if (lroundf(*v) != lroundf(b)) { optAjustare(p, *v, b, false); *v = b; } }
    OC.areOrig = 0; optTintaNoua(); salveaza(); logica(); sch = true;
  } else if (a.has("undo") && OC.areOrig) {
    for (int p = 0; p < 4; p++) { float* v = optParam(p); if (lroundf(*v) != OC.orig[p]) { optAjustare(p, *v, OC.orig[p], false); *v = OC.orig[p]; } }
    OC.areOrig = 0; salveaza(); logica(); sch = true;
  }
  if (sch) optSalveazaCfg();
  // raspunsul
  OptMed m = optMedii(OC.start != 0);
  String j = "{\"start\":" + String(OC.start) + ",\"zile\":" + String(optZileStrange()) + ",\"gata\":" + String(OPT_ZILE) +
             ",\"et\":" + String(!OC.start ? 0 : optGata() ? 2 : 1) + ",\"auto\":" + String(OC.autoOn) + ",\"early\":" + String(OC.early) +
             ",\"weekly\":" + String(OC.weekly) + ",\"em\":" + String(OC.earlyMin) + ",\"lo\":" + String(OC.lo) + ",\"hi\":" + String(OC.hi) +
             ",\"orig\":" + String(OC.areOrig) + ",\"tg\":" + String(FOLOSESTE_TELEGRAM ? 1 : 0) +
             ",\"um\":[" + String(S.umAuto) + "," + String(S.umTinta) + "," + String(S.umStrict) + "," + String(umNuTin) + "," + String(umBlocat ? 1 : 0) + "]" +
             ",\"m\":{\"z\":" + String(m.zile) + ",\"h\":" + String((int)lroundf(m.h)) + ",\"t\":" + String(m.t, 1) + ",\"c\":" + String((int)lroundf(m.porniri)) + "}";
  j += ",\"pr\":[" + String(S.mistOn, 0) + "," + String(S.mistOff, 0) + "," + String(S.fanOn, 0) + "," + String(S.fanOff, 0) + "]";
  j += ",\"sl\":[";
  for (int h = 0; h < 24; h++) { j += oSlopeN[h] >= 3 ? String(oSlope[h]) : String("null"); if (h < 23) j += ","; }
  j += "],\"aj\":[";
  for (int i = 0; i < OC.ajN; i++) {
    OptAj& x = OC.aj[i];
    j += "[" + String(x.t) + "," + String(x.p) + "," + String(x.de) + "," + String(x.la) + "," + String(x.aut) + "]";
    if (i < OC.ajN - 1) j += ",";
  }
  j += "],\"sf\":[";
  Sfat sf[5];
  int k = optSfaturi(sf, 5);
  for (int i = 0; i < k; i++) {
    j += "{\"id\":" + String(sf[i].id) + ",\"p\":" + String(sf[i].p) + ",\"de\":" + String(sf[i].de, 0) + ",\"la\":" + String(sf[i].la, 0) +
         ",\"t\":\"" + jEsc(sf[i].titlu) + "\",\"x\":\"" + jEsc(sf[i].text) + "\"}";
    if (i < k - 1) j += ",";
  }
  j += "]}";
  return Rasp{200, "application/json", j};
}
void hOpt() { if (!auth()) return; trimite(apiOpt(Arg{"", true})); }
