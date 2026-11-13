// Paludariu - mementourile de intretinere
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.

// ---------- intretinere dupa ore de mers (1.8.9): membrana de ceata, pompa + sita, ventilatorul lampii ----------
// Placa numara minutele in care merge fiecare; la limita (Setari > Intretinere) apare bara plina si un memento.
const char* ORE_NUME[ORE_N] = {"Curăță membrana de ceață", "Curăță pompa și sita", "Șterge praful de pe ventilatorul lămpii"};
Ore O = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
bool oreSchimbat = false;
void oreSalveaza() { prefs.putBytes("ore", &O, sizeof(O)); oreSchimbat = false; }
void oreIncarca() {
  Ore tmp;
  if (prefs.getBytes("ore", &tmp, sizeof(tmp)) == sizeof(tmp)) O = tmp;
}
// [[minute, limita in ore, secunde de la ultima curatare], ...]
String oreJson() {
  String j = "[";
  uint32_t n = time(nullptr) > 1000000000 ? (uint32_t)time(nullptr) : 0;
  for (int k = 0; k < ORE_N; k++) {
    j += "[" + String(O.min[k]) + "," + String(S.oreLim[k]) + "," + String(n && O.ult[k] && n > O.ult[k] ? n - O.ult[k] : 0) + "]";
    if (k < ORE_N - 1) j += ",";
  }
  return j + "]";
}
bool oreGata(int k) {
  if (k < 0 || k >= ORE_N) return false;
  time_t n = time(nullptr);
  O.min[k] = 0; O.anuntat[k] = 0;
  O.ult[k] = n > 1000000000 ? (uint32_t)n : 0;
  oreSalveaza();
  logEvM(15 + k, true, true);   // in jurnal: 15, 16, 17
  return true;
}
// se apeleaza o data pe minut: adauga un minut la ce a mers
void oreNumara() {
  static int deSalvat = 0;
  bool m = false;
  if (stare[2]) { O.min[0]++; m = true; }      // ceata
  if (stare[0]) { O.min[1]++; m = true; }      // pompa
  if (fanAplicat > 0) { O.min[2]++; m = true; }   // ventilatorul radiatorului LED
  if (m) oreSchimbat = true;
  if (oreSchimbat && ++deSalvat >= 60) { deSalvat = 0; oreSalveaza(); }   // o data pe ora
}

// ---------- mementouri: pe Telegram o data pe zi, intre 9 si 21, cat timp nu e facut ----------
// "Hrănit crabii" (1) are acum calendarul lui (hranire.ino), iar "Curățat pompa" (4) merge dupa ore.
bool memAscuns(int i) { return i == 1 || i == 4; }
void memLoop() {
  static unsigned long tm0 = 0, tMin = 0;
  if (!tMin) tMin = millis();
  if (millis() - tMin >= 60000UL) { tMin += 60000UL; oreNumara(); }
  if (millis() - tm0 < 30000) return;
  tm0 = millis();
  modExpVerifica();
  time_t n = time(nullptr);
  struct tm t;
  if (n < 1000000000 || !oraLocala(&t)) return;
  noapteLoop(t);
  hranLoop(t, n);
  bool schimbat = false;
  for (int i = 0; i < MEM_N; i++) {
    if (M.zile[i] > 0 && M.ult[i] == 0) { M.ult[i] = (uint32_t)n; schimbat = true; }   // prima pornire: numaram de azi
    if (memAscuns(i)) continue;
    long r;
    if (!memRamas(i, r) || r > 0) continue;
    if (t.tm_hour < 9 || t.tm_hour >= 21) continue;
    if (M.anuntat[i] && (long)n - (long)M.anuntat[i] < 86400L - 600) continue;
    long zile = ((long)n - (long)M.ult[i]) / 86400L;
#if FOLOSESTE_TELEGRAM
    tg("\xF0\x9F\x93\x9D E timpul: " + String(MEM_NUME[i]) + ". Ultima dată: " +
       (zile <= 0 ? String("azi") : zile == 1 ? String("ieri") : String("acum " + String(zile) + " zile")) +
       ". Când termini, apasă „Gata” la Întreținere, în pagina plăcii.");   // 📝
#endif
    M.anuntat[i] = (uint32_t)n; schimbat = true;
  }
  if (schimbat) memSalveaza();
  // limitele de ore
  bool os = false;
  for (int k = 0; k < ORE_N; k++) {
    if (S.oreLim[k] == 0 || O.min[k] < (uint32_t)S.oreLim[k] * 60UL) continue;
    if (t.tm_hour < 9 || t.tm_hour >= 21) continue;
    if (O.anuntat[k] && (long)n - (long)O.anuntat[k] < 86400L - 600) continue;
#if FOLOSESTE_TELEGRAM
    tg("\xF0\x9F\x93\x9D E timpul: " + String(ORE_NUME[k]) + ". A mers " + String(O.min[k] / 60) + " ore de la ultima curățare (limita: " +
       String(S.oreLim[k]) + "). Când termini, apasă butonul de la Întreținere, în pagina plăcii.");   // 📝
#endif
    O.anuntat[k] = (uint32_t)n; os = true;
  }
  if (os) oreSalveaza();
}
