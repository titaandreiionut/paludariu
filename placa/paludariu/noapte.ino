// Paludariu - rezumatul noptii (1.8.9), pentru notificarea de dimineata din aplicatie
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.
//
// Intre 22:00 si 08:00 placa tine minte: temperatura si umiditatea minime/maxime, confortul mediu,
// de cate ori a pornit ceata, ce alerte au aparut si de cate ori a repornit. La 08:00 noaptea se inchide,
// iar /noapte o da aplicatiei.

// struct Noapte e in paludariu.ino (Arduino IDE are nevoie de el inaintea functiilor)
Noapte NC, NU;                // NC = noaptea care se aduna acum, NU = ultima noapte incheiata
bool noapteActiv = false;

void noapteGol(Noapte& x) { memset(&x, 0, sizeof(x)); x.tMin = x.hMin = INT16_MAX; x.tMax = x.hMax = INT16_MIN; }
bool eNoapte(const struct tm& t) { return t.tm_hour >= 22 || t.tm_hour < 8; }

void noapteIncarca() {
  noapteGol(NC); noapteGol(NU);
  Noapte x;
  if (prefs.getBytes("nu", &x, sizeof(x)) == sizeof(x)) NU = x;
  if (prefs.getBytes("nc", &x, sizeof(x)) == sizeof(x) && x.start) { NC = x; NC.rep++; noapteActiv = true; }   // placa a repornit in timpul noptii
}

// confortul unei citiri, la fel ca pagina (noaptea: 21-24 °C, umiditate 75-90 %)
int noapteConfort(float t, float h) {
  float d = t < 21 ? 21 - t : t > 24 ? t - 24 : 0;
  float st = t >= 30 ? 0 : max(0.0f, 100 - 25 * d);
  d = h < 75 ? 75 - h : h > 90 ? h - 90 : 0;
  float sh = max(0.0f, 100 - 5 * d);
  if (h < 65) sh = min(sh, 30.0f);
  float w = min(st, sh);
  return (int)lroundf((0.4f * st + 0.4f * sh + 20) * (0.5f + 0.5f * w / 100));
}

// din bucla senzorilor (la 2 secunde)
void noapteAdauga() {
  if (!noapteActiv || isnan(temp) || isnan(hum)) return;
  int16_t t = (int16_t)lroundf(temp * 10), h = (int16_t)lroundf(hum * 10);
  if (t < NC.tMin) { NC.tMin = t; NC.tMinT = acum(); }
  if (t > NC.tMax) NC.tMax = t;
  if (h < NC.hMin) NC.hMin = h;
  if (h > NC.hMax) NC.hMax = h;
  NC.cfSum += noapteConfort(temp, hum); NC.cfN++;
}
void noapteCeata() { if (noapteActiv && NC.ceata < 65535) NC.ceata++; }
void noapteAlerta(int i) { if (noapteActiv && i < 16) NC.alerte |= 1 << i; }

// din memLoop (la 30 de secunde), cand placa stie ora
void noapteLoop(const struct tm& t) {
  static unsigned long tSalv = 0;
  uint32_t n = acum();
  bool e = eNoapte(t);
  if (noapteActiv && NC.start && n - NC.start > 14UL * 3600UL) { noapteGol(NC); noapteActiv = false; prefs.remove("nc"); }   // ramasa de demult
  if (e && !noapteActiv) {
    noapteGol(NC); NC.start = n; noapteActiv = true;
    tSalv = millis(); prefs.putBytes("nc", &NC, sizeof(NC));
  } else if (!e && noapteActiv) {
    NC.gata = n; NU = NC; noapteActiv = false;
    prefs.putBytes("nu", &NU, sizeof(NU)); prefs.remove("nc");
    noapteGol(NC);
    return;
  }
  if (noapteActiv && millis() - tSalv > 1800000UL) { tSalv = millis(); prefs.putBytes("nc", &NC, sizeof(NC)); }   // la 30 min
}

const char* AL_TEXT[] = {"senzorul de climă nu a răspuns", "a fost prea frig", "a fost prea cald", "a fost prea uscat",
                         "a fost prea umed", "radiatorul LED s-a supraîncălzit", "senzorul radiatorului nu a răspuns",
                         "ventilatorul lămpii nu s-a învârtit", "senzorul de umiditate părea blocat"};
String noapteJson(const Noapte& x, bool inCurs) {
  if (!x.start) return "{\"ok\":0}";
  auto v = [](int16_t a) { return (a == INT16_MAX || a == INT16_MIN) ? String("null") : String(a / 10.0, 1); };
  String j = "{\"ok\":1,\"inc\":" + String(inCurs ? 1 : 0) + ",\"de\":" + String(x.start) + ",\"la\":" + String(x.gata) +
             ",\"tMin\":" + v(x.tMin) + ",\"tMinO\":" + String(x.tMinT) + ",\"tMax\":" + v(x.tMax) +
             ",\"hMin\":" + v(x.hMin) + ",\"hMax\":" + v(x.hMax) + ",\"cf\":" + (x.cfN ? String(x.cfSum / x.cfN) : String("null")) +
             ",\"ceata\":" + String(x.ceata) + ",\"rep\":" + String(x.rep) + ",\"pb\":[";
  bool prim = true;
  for (int i = 0; i < (int)(sizeof(AL_TEXT) / sizeof(AL_TEXT[0])); i++) {
    if (!(x.alerte & (1 << i))) continue;
    j += String(prim ? "" : ",") + "\"" + AL_TEXT[i] + "\""; prim = false;
  }
  if (x.rep) { j += String(prim ? "" : ",") + "\"placa a repornit" + (x.rep == 1 ? String("") : " de " + String(x.rep) + " ori") + "\""; prim = false; }
  return j + "]}";
}
// /noapte = ultima noapte incheiata; /noapte?acum=1 = cea de acum (daca e noapte)
Rasp apiNoapte(const Arg& a) {
  if (a.has("acum") && noapteActiv) return Rasp{200, "application/json", noapteJson(NC, true)};
  return Rasp{200, "application/json", noapteJson(NU, false)};
}
void hNoapte() { if (!auth()) return; trimite(apiNoapte(Arg{"", true})); }
