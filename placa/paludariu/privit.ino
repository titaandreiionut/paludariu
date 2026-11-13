// Paludariu - accesul "doar privit" (1.8.9)
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.
//
// O a doua parola, pentru familie: cine intra cu ea (utilizator "privit") vede tot, dar nu poate porni
// sau opri nimic si nu intra in Setari. Prin internet foloseste o a doua cheie de criptare (mqKey2),
// pe care aplicatia o pune in codul de invitatie. Parola si cheia se sterg cu "Opreste".

// variabilele (vPass, mqKey2, mqCheie2, vizUlt, vizCale, nivelCerere) sunt in paludariu.ino, langa auth()

void privitIncarca() {
  vPass = prefs.getString("vpw", "");
  mqCheie2 = prefs.getBytes("mqk2", mqKey2, 32) == 32;
  vizUlt = prefs.getULong("vzt", 0);
  vizCale = prefs.getUChar("vzc", 0);
}

// ce poate deschide cineva care doar priveste: paginile de citit si starea (cereri fara parametri)
bool privitPermis(const String& p, bool faraArgs) {
  if (p == "/" || p == "/s" || p == "/h" || p == "/h30" || p == "/e" || p == "/noapte" || p == "/manifest.json") return true;
  if (faraArgs && (p == "/opt" || p == "/mem" || p == "/hran" || p == "/gh" || p == "/pl" || p == "/fan" || p == "/vac")) return true;
  return false;
}

void vizNoteaza(uint8_t cale) {
  static unsigned long tSalv = 0;
  uint32_t n = time(nullptr) > 1000000000 ? (uint32_t)time(nullptr) : 0;
  if (!n) return;
  vizUlt = n; vizCale = cale;
  if (!tSalv || millis() - tSalv > 600000UL) { tSalv = millis() ? millis() : 1; prefs.putULong("vzt", vizUlt); prefs.putUChar("vzc", vizCale); }
}

// /privit           = starea (pornit, parola, cheia pentru internet, ultima vizita)
// /privit?pw=...    = parola noua (4-24 caractere)
// /privit?key=hex   = cheia pentru internet (doar de acasa, din aplicatie)
// /privit?off=1     = opreste (sterge parola si cheia)
Rasp apiPrivit(const Arg& a, bool local) {
  if (a.has("off")) {
    vPass = ""; mqCheie2 = false; memset(mqKey2, 0, 32);
    prefs.remove("vpw"); prefs.remove("mqk2");
  }
  if (a.has("pw")) {
    String p = a.get("pw"); p.trim();
    if (p.length() < 4 || p.length() > 24 || p == WEB_PASS) return Rasp{400, "text/plain; charset=utf-8", "Parola trebuie să aibă 4-24 de caractere și să fie alta decât parola ta."};
    vPass = p; prefs.putString("vpw", vPass);
  }
  if (a.has("key")) {
    if (!local) return Rasp{403, "text/plain; charset=utf-8", "Cheia se pune doar de acasă, din aplicație."};
    uint8_t k[32];
    if (!hexLa(a.get("key"), k, 32)) return Rasp{400, "text/plain; charset=utf-8", "Cheie greșită."};
    memcpy(mqKey2, k, 32); mqCheie2 = true; prefs.putBytes("mqk2", mqKey2, 32);
  }
  return Rasp{200, "application/json", "{\"on\":" + String(vPass.length() ? 1 : 0) + ",\"pw\":\"" + jEsc(vPass) + "\",\"k2\":" + String(mqCheie2 ? 1 : 0) +
                                       ",\"viz\":" + String(vizUlt) + ",\"vc\":" + String(vizCale) + "}"};
}
void hPrivit() { if (!auth()) return; trimite(apiPrivit(Arg{"", true}, true)); }
