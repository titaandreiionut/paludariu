// Paludariu - mementourile de intretinere
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.

// ---------- mementouri: pe Telegram o data pe zi, intre 9 si 21, cat timp nu e facut ----------
void memLoop() {
  static unsigned long tm0 = 0;
  if (millis() - tm0 < 30000) return;
  tm0 = millis();
  time_t n = time(nullptr);
  struct tm t;
  if (n < 1000000000 || !oraLocala(&t)) return;
  bool schimbat = false;
  for (int i = 0; i < MEM_N; i++) {
    if (M.zile[i] > 0 && M.ult[i] == 0) { M.ult[i] = (uint32_t)n; schimbat = true; }   // prima pornire: numaram de azi
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
}
