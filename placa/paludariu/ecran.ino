// Paludariu - ecranul TFT optional (FOLOSESTE_DISPLAY in config.h)
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.

// ======================= DISPLAY TFT (optional) =======================
// 5 pagini, navigare prin glisare stanga-dreapta (la fel ca pagina web):
//   0 Acasa (antet cu crabul, ceasuri, 4 accesorii cu comutator)   1 Grafice 24 h   2 Jurnal
//   3 Praguri   4 Timpi
// Standby: dupa S.standbyMin minute fara atingere: acvariu cu ora, pesti si bule, lumina de fundal scazuta.
// Pe Acasa: atingi un card = ON/OFF manual; tii apasat (~1 s) = revine la AUTO.
#if FOLOSESTE_DISPLAY
#define C565(r,g,b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xFF) >> 3)))
const uint16_t cBG = C565(11,22,16), cCARD = C565(19,37,27), cLINE = C565(42,72,52), cTXT = C565(232,241,231),
  cMUT = C565(147,169,154), cCY = C565(92,198,192), cGR = C565(134,207,142), cAM = C565(242,180,90),
  cRD = C565(239,111,94), cBL = C565(60,140,136), cOFF = C565(52,70,58), cPIPE = C565(70,96,80),
  cONB = C565(30,61,42), cTRK = C565(28,48,36), cROCK = C565(107,130,114), cCRAB = C565(229,134,79);

// Pini ecran (SPI hardware: SCK = GPIO18, MOSI = GPIO23, MISO = GPIO19 doar pentru touch)
#define TFT_CS 5
#define TFT_DC 27
#define TFT_RST 26
// Modulele ST7789 FARA pin CS merg de obicei doar cu SPI_MODE3
#define TFT_SPI_MODE SPI_MODE0

// Lumina de fundal (pinul LED / BL al ecranului) legata la BL_PIN, ca sa poata fi redusa (PWM).
// Pune -1 daca LED-ul ramane legat direct la 3V3 (atunci standby arata doar ora, fara estompare).
#define BL_PIN 14
#define BL_CH 0              // canal PWM (folosit doar la ESP32 Arduino core 2.x)
#define BL_FULL 255          // luminozitate normala (0-255)
#define BL_STANDBY 14        // luminozitate in standby (~5%), cat sa se vada si pestii
// Timpul de standby (minute fara atingere) se regleaza din pagina "Timpi". 0 = dezactivat.

#if FOLOSESTE_TOUCH
// Touch XPT2046 (pe acelasi SPI): T_CS = GPIO33. T_IRQ nu e necesar.
#define TOUCH_CS 33
// Calibrarea touch-ului se face din config.h (TOUCH_DEBUG, TOUCH_X0 ...); valorile de mai jos sunt doar rezerva.
#ifndef TOUCH_DEBUG
#define TOUCH_DEBUG false
#endif
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY false
#endif
#ifndef TOUCH_INV_X
#define TOUCH_INV_X false
#endif
#ifndef TOUCH_INV_Y
#define TOUCH_INV_Y false
#endif
#ifndef TOUCH_X0
#define TOUCH_X0 300
#endif
#ifndef TOUCH_X1
#define TOUCH_X1 3800
#endif
#ifndef TOUCH_Y0
#define TOUCH_Y0 300
#endif
#ifndef TOUCH_Y1
#define TOUCH_Y1 3800
#endif
#ifndef TOUCH_MIN_Z
#define TOUCH_MIN_Z 300
#endif
XPT2046_Touchscreen ts(TOUCH_CS);
#endif

#if DISPLAY_ST7789
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
#else
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
#endif
GFXcanvas16 *cvT = nullptr, *cvG = nullptr, *cvH = nullptr;   // card 112x82, ceas 120x98, antet 240x34
GFXcanvas16 *cvF = nullptr, *cvB = nullptr;                  // standby: un peste 32x16, o bula 10x12
const int NP = 5;
enum { PG_HOME = 0, PG_GRAF = 1, PG_JUR = 2, PG_PRAG = 3, PG_TIMP = 4 };
const int CARD_X[4] = {4, 124, 4, 124}, CARD_Y[4] = {134, 134, 216, 216};   // pozitia cardurilor de pe Acasa
int pagina = PG_HOME;
bool dispRedraw = true, dirty = false;
unsigned long tDirty = 0, tAct = 0;
bool standby = false, standbyDirty = false;

void blSet(int d) {
#if BL_PIN >= 0
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BL_PIN, d);
#else
  ledcWrite(BL_CH, d);
#endif
#endif
}

uint16_t mix(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) << 3, ag = ((a >> 5) & 63) << 2, ab = (a & 31) << 3;
  int br = (b >> 11) << 3, bg = ((b >> 5) & 63) << 2, bb = (b & 31) << 3;
  return C565(ar + (int)((br - ar) * t), ag + (int)((bg - ag) * t), ab + (int)((bb - ab) * t));
}

void ctext(GFXcanvas16* c, const char* s, int cx, int y, uint8_t sz, uint16_t col) {
  c->setTextSize(sz); c->setTextColor(col);
  c->setCursor(cx - (int)strlen(s) * 6 * sz / 2, y); c->print(s);
}

void textCentrat(Adafruit_GFX* g, const GFXfont* font, const char* s, int cx, int base, uint16_t col) {
  g->setFont(font); g->setTextSize(1); g->setTextColor(col);
  int16_t x1, y1; uint16_t w, h;
  g->getTextBounds(s, 0, base, &x1, &y1, &w, &h);
  g->setCursor(cx - (int)w / 2 - x1, base); g->print(s);
  g->setFont(nullptr);
}

void arcDots(GFXcanvas16* c, int cx, int cy, int r, float a0, float a1, uint16_t col) {
  for (float a = a0; a <= a1; a += 3) {
    float rad = a * 0.0174533f;
    c->fillCircle(cx + (int)(r * cosf(rad)), cy + (int)(r * sinf(rad)), 3, col);
  }
}

// ---------- avertizare pe ecran (aceleasi praguri ca alertele) ----------
bool alertaEcran(char* b, size_t n) {
  b[0] = 0;
  if (millis() < 15000) return false;                       // primele citiri dupa pornire
  if (isnan(temp) || isnan(hum)) { snprintf(b, n, "Senzor DHT22 indisponibil"); return true; }
  if (ledOverheat) { snprintf(b, n, "Radiator LED supraincalzit"); return true; }
  if (fanFail) { snprintf(b, n, "Ventilator LED oprit"); return true; }
  if (hum < S.mistOn - 5) { snprintf(b, n, "Umiditate scazuta: %d%%", (int)lroundf(hum)); return true; }
  if (hum > S.fanOn + 3) { snprintf(b, n, "Umiditate mare: %d%%", (int)lroundf(hum)); return true; }
  if (temp < 20) { snprintf(b, n, "Prea frig: %.1f C", temp); return true; }
  if (temp > 32) { snprintf(b, n, "Prea cald: %.1f C", temp); return true; }
  if (isnan(tLed)) { snprintf(b, n, "Senzor radiator LED lipsa"); return true; }
  return false;
}

// Crabul pe piatra (ca iconita aplicatiei), cam 40x30, coltul stanga-sus in (x, y)
void drawCrab(GFXcanvas16* c, int x, int y) {
  c->fillRoundRect(x + 1, y + 19, 36, 11, 5, cROCK);
  c->fillCircle(x + 13, y + 21, 8, cROCK);
  c->fillCircle(x + 25, y + 22, 7, cROCK);
  c->drawFastHLine(x + 8, y + 25, 5, cBG);
  c->drawFastHLine(x + 23, y + 26, 5, cBG);
  c->fillTriangle(x + 33, y + 21, x + 40, y + 7, x + 37, y + 21, cGR);           // frunza
  c->fillTriangle(x + 33, y + 21, x + 40, y + 7, x + 32, y + 14, cGR);
  for (int k = 0; k < 3; k++) {                                                    // picioare
    c->drawLine(x + 13, y + 13 + k * 2, x + 7 - k, y + 17 + k * 2, cCRAB);
    c->drawLine(x + 26, y + 13 + k * 2, x + 32 + k, y + 17 + k * 2, cCRAB);
  }
  c->drawLine(x + 14, y + 10, x + 9, y + 5, cCRAB);                               // brate si clesti
  c->drawLine(x + 25, y + 10, x + 30, y + 5, cCRAB);
  c->fillCircle(x + 8, y + 4, 3, cCRAB);
  c->fillCircle(x + 31, y + 4, 3, cCRAB);
  c->fillTriangle(x + 7, y + 0, x + 9, y + 0, x + 8, y + 4, cBG);
  c->fillTriangle(x + 30, y + 0, x + 32, y + 0, x + 31, y + 4, cBG);
  c->fillRoundRect(x + 10, y + 8, 20, 10, 5, cCRAB);                             // carapacea
  c->drawLine(x + 17, y + 8, x + 16, y + 4, cCRAB);                               // ochii
  c->drawLine(x + 23, y + 8, x + 24, y + 4, cCRAB);
  c->fillCircle(x + 16, y + 3, 2, cTXT);
  c->fillCircle(x + 24, y + 3, 2, cTXT);
  c->drawPixel(x + 16, y + 3, cBG);
  c->drawPixel(x + 24, y + 3, cBG);
}

// text cu spatiu intre litere (semnatura)
void spatiat(GFXcanvas16* c, const char* s, int x, int y, int sp, uint16_t col) {
  c->setTextSize(1); c->setTextColor(col);
  for (; *s; s++) { c->setCursor(x, y); c->print(*s); x += 6 + sp; }
}

char ultimaAlerta[40] = "";

void drawHeader() {
  GFXcanvas16* c = cvH;
  c->fillScreen(cBG);
  drawCrab(c, 3, 2);
  c->setFont(&FreeSerifBold12pt7b);
  c->setTextSize(1); c->setTextColor(cTXT);
  c->setCursor(48, 19); c->print("Paludariu");
  c->setFont(nullptr);
  spatiat(c, "by Andrei T", 239 - 11 * 7, 3, 1, cMUT);
  char al[40];
  if (alertaEcran(al, sizeof al)) {
    int w = (int)strlen(al) * 6 + 10;
    c->fillRoundRect(46, 22, w, 11, 5, mix(cBG, cAM, 0.25f));
    c->setTextColor(cAM); c->setCursor(51, 24); c->print(al);
  } else {
    struct tm t; char d[24];
    if (oraLocala(&t)) snprintf(d, sizeof d, "%02d.%02d.%04d  %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
    else strcpy(d, "ora nesincronizata");
    c->setTextColor(cMUT); c->setCursor(48, 24); c->print(d);
  }
  c->fillCircle(234, 27, 2, WiFi.status() == WL_CONNECTED ? cGR : cRD);           // WiFi
  strcpy(ultimaAlerta, al);
  tft.drawRGBBitmap(0, 0, cvH->getBuffer(), 240, 34);
}

// ---------- pagina Acasa: ceasuri ----------
void drawGauge(int x, int y, float v, float mn, float mx, uint16_t col, const char* val,
               const char* lbl, const char* sMin, const char* sMax) {
  GFXcanvas16* c = cvG;
  c->fillScreen(cBG);
  const int cx = 60, cy = 40, r = 34;
  arcDots(c, cx, cy, r, 135, 405, cLINE);
  if (!isnan(v)) {
    float f = constrain((v - mn) / (mx - mn), 0.0f, 1.0f);
    float a = 135 + 270 * f, rad = a * 0.0174533f;
    arcDots(c, cx, cy, r, 135, a + 0.01f, col);
    c->drawLine(cx, cy, cx + (int)((r - 10) * cosf(rad)), cy + (int)((r - 10) * sinf(rad)), cTXT);
  }
  c->fillCircle(cx, cy, 4, cBG); c->drawCircle(cx, cy, 4, cTXT);
  textCentrat(c, &FreeSerifBold12pt7b, val, cx, 83, col);
  ctext(c, sMin, 24, 70, 1, cMUT);
  ctext(c, sMax, 96, 70, 1, cMUT);
  ctext(c, lbl, cx, 88, 1, cMUT);
  tft.drawRGBBitmap(x, y, cvG->getBuffer(), 120, 98);
}

// ---------- pagina Acasa: carduri in forma de frunza, cu pictograma animata si comutator ----------
void leafCard(GFXcanvas16* c, int w, int h, uint16_t border) {
  const int r = 10;
  c->fillScreen(cBG);
  c->fillRoundRect(0, 0, w, h, r, cCARD);
  c->drawRoundRect(0, 0, w, h, r, border);
  c->fillRect(0, h - r, r, r, cCARD);                    // coltul stanga-jos ascutit, ca o frunza
  c->drawFastVLine(0, h - r, r, border);
  c->drawFastHLine(0, h - 1, r, border);
}

void drawSwitch(GFXcanvas16* c, int x, int y, int w, bool on, uint16_t acc) {
  const int h = 22, r = 11;
  c->fillRoundRect(x, y, w, h, r, on ? mix(cCARD, acc, 0.28f) : cTRK);
  c->drawRoundRect(x, y, w, h, r, on ? acc : cLINE);
  c->fillCircle(on ? x + w - r : x + r, y + r, 8, on ? acc : cMUT);
  int tx = on ? x + (w - 2 * r) / 2 + 2 : x + 2 * r + (w - 2 * r) / 2 - 2;
  ctext(c, on ? "Pornit" : "Oprit", tx, y + 7, 1, on ? cTXT : cMUT);
}

void punctat(GFXcanvas16* c, int x0, int y0, int x1, int y1, uint16_t col) {   // raze de lumina
  int n = max(abs(x1 - x0), abs(y1 - y0));
  for (int i = 0; i <= n; i++) if ((i % 6) < 3) c->drawPixel(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, col);
}

void drawTile(int idx, bool on, int f) {
  GFXcanvas16* c = cvT;
  const uint16_t ACC[4] = {cCY, cAM, cCY, cGR};
  bool man = S.mod[idx] != 0;
  leafCard(c, 112, 82, man ? cAM : cLINE);
  c->setTextSize(1); c->setTextColor(cTXT); c->setCursor(7, 5); c->print(NUME[idx]);
  const char* ml = man ? "MAN" : "AUTO";
  c->setTextColor(man ? cAM : cMUT);
  c->setCursor(105 - (int)strlen(ml) * 6, 5); c->print(ml);
  const int ix = 56, iy = 34;
  if (idx == 3) {                                   // ventilator: 5 pale care se rotesc
    c->drawRoundRect(ix - 21, iy - 19, 42, 39, 9, on ? cLINE : cOFF);
    c->drawCircle(ix, iy, 16, mix(cCARD, cLINE, 0.6f));
    float base = on ? f * 0.3f : 0;
    for (int k = 0; k < 5; k++) {
      float a0 = base + k * 1.2566f, a1 = a0 + 0.6f;
      c->fillTriangle(ix, iy, ix + (int)(15 * cosf(a0)), iy + (int)(15 * sinf(a0)),
                      ix + (int)(15 * cosf(a1)), iy + (int)(15 * sinf(a1)), on ? cGR : cOFF);
    }
    c->fillCircle(ix, iy, 3, cCARD); c->drawCircle(ix, iy, 3, on ? cTXT : cOFF);
  } else if (idx == 2) {                            // ceata: vas cu nori care urca
    c->fillRoundRect(ix - 16, iy + 9, 32, 9, 4, on ? cROCK : cOFF);
    c->drawFastHLine(ix - 14, iy + 9, 28, on ? cCY : cOFF);
    if (on) for (int k = 0; k < 3; k++) {
      float p = ((f + k * 8) % 24) / 24.0f;
      c->fillCircle(ix + (k - 1) * 9, iy + 6 - (int)(p * 26), 3 + (int)(p * 5), mix(cCY, cCARD, p));
    }
  } else if (idx == 1) {                            // lumina: lampa cu con de lumina
    float pu = on ? (0.5f + 0.5f * sinf(f * 0.4f)) : 0;
    uint16_t con = on ? mix(cCARD, cAM, 0.16f + 0.14f * pu) : mix(cCARD, cOFF, 0.5f);
    c->fillTriangle(ix - 14, iy - 14, ix + 14, iy - 14, ix + 23, iy + 13, con);
    c->fillTriangle(ix - 14, iy - 14, ix + 23, iy + 13, ix - 23, iy + 13, con);
    c->fillRoundRect(ix - 17, iy - 19, 34, 5, 2, on ? cTXT : cOFF);
    if (on) for (int k = -1; k <= 1; k++) punctat(c, ix + k * 8, iy - 12, ix + k * 14, iy + 6, mix(cCARD, cAM, 0.35f + 0.55f * pu));
    c->fillTriangle(ix, iy + 18, ix - 11, iy + 9, ix - 3, iy + 18, on ? cGR : cOFF);    // planta
    c->fillTriangle(ix, iy + 18, ix + 11, iy + 9, ix + 3, iy + 18, on ? cGR : cOFF);
  } else {                                          // pompa: cascada de pe piatra
    c->fillRoundRect(ix - 32, iy - 6, 32, 18, 8, on ? cROCK : cOFF);
    c->fillCircle(ix - 20, iy - 6, 9, on ? cROCK : cOFF);
    c->fillTriangle(ix - 26, iy - 13, ix - 19, iy - 24, ix - 21, iy - 13, on ? cGR : cOFF);
    c->fillRoundRect(ix - 8, iy + 14, 40, 5, 2, on ? mix(cCARD, cCY, 0.45f) : cOFF);
    if (on) {
      for (int i = 0; i <= 18; i++) {                // firul de apa
        float p = i / 18.0f;
        c->drawPixel(ix - 1 + (int)(9 * p * p), iy - 4 + (int)(18 * p), mix(cCARD, cCY, 0.35f));
      }
      for (int k = 0; k < 4; k++) {                  // picaturile care cad
        float p = ((f + k * 3) % 12) / 12.0f;
        c->fillCircle(ix - 1 + (int)(9 * p * p), iy - 4 + (int)(18 * p), 1, cCY);
      }
      float q = (f % 12) / 12.0f; int w = (int)(q * 10);
      c->drawFastHLine(ix + 8 - w, iy + 16, 2 * w + 1, mix(cCY, cCARD, q));
    }
  }
  drawSwitch(c, 6, 56, 100, on, ACC[idx]);
  tft.drawRGBBitmap(CARD_X[idx], CARD_Y[idx], cvT->getBuffer(), 112, 82);
}

// ---------- antet si puncte de pagina ----------
void drawPageHead(const char* title) {
  tft.fillRect(0, 0, 240, 25, cBG);
  tft.setFont(&FreeSerifBold12pt7b); tft.setTextSize(1); tft.setTextColor(cTXT);
  tft.setCursor(6, 19); tft.print(title);
  tft.setFont(nullptr);
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("Fara WiFi");
  tft.setTextSize(1); tft.setTextColor(cMUT); tft.setCursor(234 - (int)ip.length() * 6, 8); tft.print(ip);
  tft.drawFastHLine(0, 24, 240, cLINE);
}

void drawDots() {
#if FOLOSESTE_TOUCH
  tft.fillRect(0, 298, 240, 22, cBG);
  for (int k = 0; k < NP; k++) {
    int x = 120 + (2 * k - (NP - 1)) * 7;
    tft.fillCircle(x, 309, k == pagina ? 4 : 2, k == pagina ? cCY : cOFF);
  }
#endif
}

void setPagina(int p) {
  pagina = p; dispRedraw = true;
  tft.fillRect(0, 0, 240, 298, cBG);
  drawDots();
}

// ---------- standby ca un acvariu: ora, pesti si bule, lumina de fundal scazuta ----------
// Fiecare peste/bula se deseneaza intr-o bucata mica de memorie cu margine neagra si se muta cate 1 pixel,
// asa ca marginea sterge urma veche: fara palpaire si cu foarte putina memorie.
struct Peste { float x; int lane, dir; float faza; uint16_t col; };
struct Bula { float x, y, v; int r; float faza; };
Peste pesti[3];
Bula bule[5];
const uint16_t cNEGRU = 0x0000, cCEAS = C565(170, 195, 185), cBULA = C565(110, 165, 158);

void desenPeste(GFXcanvas16* c, int dir, uint16_t col) {
  c->fillScreen(cNEGRU);
  int bx = dir > 0 ? 9 : 5;
  c->fillRoundRect(bx, 4, 18, 8, 4, col);
  if (dir > 0) c->fillTriangle(3, 3, 3, 13, 11, 8, col);
  else c->fillTriangle(28, 3, 28, 13, 20, 8, col);
  c->drawFastHLine(bx + 3, 7, 11, mix(col, cCY, 0.7f));                 // dunga neon
  c->fillRect(dir > 0 ? bx + 2 : bx + 9, 9, 7, 2, mix(col, cRD, 0.6f)); // burta rosiatica
  c->drawPixel(dir > 0 ? bx + 14 : bx + 3, 6, cNEGRU);                  // ochiul
}

void bulaNoua(Bula& b, bool oriunde) {
  b.x = 12 + random(0, 216);
  b.y = oriunde ? 222 + random(0, 94) : 318 + random(0, 30);
  b.v = 0.5f + random(0, 70) / 100.0f;
  b.r = 2 + random(0, 2);
  b.faza = random(0, 628) / 100.0f;
}

void drawStandby(unsigned long now) {
  static unsigned long tF = 0;
  static int lastMin = -2, cadru = 0;
  static char lastAl[40] = "x";
  if (standbyDirty) {
    standbyDirty = false; lastMin = -2; strcpy(lastAl, "x");
    tft.fillScreen(cNEGRU);
    const int LANE[3] = {40, 68, 96};
    const uint16_t COL[3] = {C565(150, 205, 196), C565(125, 185, 178), C565(165, 215, 205)};
    for (int i = 0; i < 3; i++) pesti[i] = {(float)random(-30, 240), LANE[i], (i % 2) ? -1 : 1, random(0, 628) / 100.0f, COL[i]};
    for (auto& b : bule) bulaNoua(b, true);
  }
  // ora si avertizarea (doar cand se schimba)
  struct tm t; bool ok = oraLocala(&t);
  int mi = ok ? t.tm_hour * 60 + t.tm_min : -1;
  if (mi != lastMin) {
    lastMin = mi;
    char b[8], z[32];
    if (ok) snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min); else strcpy(b, "--:--");
    static const char* ZI[7] = {"Duminica", "Luni", "Marti", "Miercuri", "Joi", "Vineri", "Sambata"};
    static const char* LUNA[12] = {"ianuarie", "februarie", "martie", "aprilie", "mai", "iunie", "iulie",
                                   "august", "septembrie", "octombrie", "noiembrie", "decembrie"};
    if (ok) snprintf(z, sizeof z, "%s, %d %s", ZI[t.tm_wday], t.tm_mday, LUNA[t.tm_mon]); else z[0] = 0;
    tft.fillRect(0, 122, 240, 76, cNEGRU);
    textCentrat(&tft, &FreeSerifBold24pt7b, b, 120, 170, cCEAS);
    tft.setTextSize(1); tft.setTextColor(C565(120, 145, 135));
    tft.setCursor(120 - (int)strlen(z) * 3, 184); tft.print(z);
  }
  char al[40];
  alertaEcran(al, sizeof al);
  if (strcmp(al, lastAl) != 0) {
    strcpy(lastAl, al);
    tft.fillRect(0, 199, 240, 14, cNEGRU);
    if (al[0]) { tft.setTextSize(1); tft.setTextColor(cAM); tft.setCursor(120 - (int)strlen(al) * 3, 202); tft.print(al); }
  }
  // animatia, cam 9 cadre pe secunda
  if (now - tF < 110) return;
  tF = now; cadru++;
  for (auto& p : pesti) {
    p.x += p.dir;
    if (p.dir > 0 && p.x > 250) p.dir = -1;          // iese din ecran, se intoarce si revine
    if (p.dir < 0 && p.x < -40) p.dir = 1;
    int y = p.lane + (int)lroundf(2 * sinf(cadru * 0.06f + p.faza));
    desenPeste(cvF, p.dir, p.col);
    tft.drawRGBBitmap((int)p.x, y, cvF->getBuffer(), 32, 16);
  }
  for (auto& b : bule) {
    b.y -= b.v;
    if (b.y < 222) {                                   // a ajuns sub ora: sterge-o si porneste alta de jos
      tft.fillRect((int)b.x - 5, (int)b.y - 1, 10, 14, cNEGRU);
      bulaNoua(b, false);
      continue;
    }
    int x = (int)lroundf(b.x + sinf(b.y * 0.08f + b.faza));
    cvB->fillScreen(cNEGRU);
    cvB->drawCircle(5, 6, b.r, cBULA);
    cvB->drawPixel(4, 5, cCEAS);                        // luciul
    tft.drawRGBBitmap(x - 5, (int)b.y, cvB->getBuffer(), 10, 12);
  }
}
void enterStandby() { standby = true; standbyDirty = true; tft.fillScreen(0x0000); blSet(BL_STANDBY); }
void wake() { standby = false; blSet(BL_FULL); setPagina(PG_HOME); }

// ---------- pagina Grafice ----------
void drawChart(int y0, const char* name, int16_t* a, uint16_t col, float minSpan, float lo0, float hi0) {
  tft.fillRect(0, y0, 240, 122, cBG);
  int n = histCount;
  float mn = 1e9f, mx = -1e9f;
  for (int i = 0; i < n; i++) {
    int16_t v = a[(histHead - n + i + H_N) % H_N];
    if (v == INT16_MIN) continue;
    float fv = v / 10.0f;
    if (fv < mn) mn = fv;
    if (fv > mx) mx = fv;
  }
  tft.setTextSize(1); tft.setTextColor(col); tft.setCursor(6, y0 + 2); tft.print(name);
  if (mx < mn) {
    tft.setTextColor(cMUT); tft.setCursor(6, y0 + 50); tft.print("Se aduna date (1 punct / 5 min)...");
    return;
  }
  char sb[32]; snprintf(sb, sizeof sb, "min %.1f max %.1f", mn, mx);
  tft.setTextColor(cMUT); tft.setCursor(234 - (int)strlen(sb) * 6, y0 + 2); tft.print(sb);
  float lo = floorf(mn - 0.5f), hi = ceilf(mx + 0.5f);
  if (hi - lo < minSpan) { float m = (hi + lo) / 2; lo = floorf(m - minSpan / 2); hi = lo + minSpan; }
  if (lo < lo0) lo = lo0;
  if (hi < lo + minSpan) hi = lo + minSpan;
  if (hi > hi0) hi = hi0;
  const int px0 = 30, px1 = 236, py0 = y0 + 16, py1 = y0 + 104;
  for (int k = 0; k < 3; k++) {
    int y = py0 + (py1 - py0) * k / 2;
    tft.drawFastHLine(px0, y, px1 - px0, cLINE);
    char lb[8]; snprintf(lb, sizeof lb, (hi - lo) >= 8 ? "%.0f" : "%.1f", hi - (hi - lo) * k / 2);
    tft.setTextColor(cMUT); tft.setCursor(0, y - 3); tft.print(lb);
  }
  int px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    int16_t v = a[(histHead - n + i + H_N) % H_N];
    if (v == INT16_MIN) { px = -1; continue; }
    int x = px0 + (int)((long)(px1 - px0) * (H_N - n + i) / (H_N - 1));
    int y = py1 - (int)(((v / 10.0f) - lo) / (hi - lo) * (py1 - py0));
    if (px >= 0) { tft.drawLine(px, py, x, y, col); tft.drawLine(px, py + 1, x, y + 1, col); }
    else tft.drawPixel(x, y, col);
    px = x; py = y;
  }
}

// ---------- pagina Jurnal: cronologie 24 h + lista evenimentelor ----------
void drawJurnal() {
  tft.fillRect(0, 0, 240, 298, cBG);
  drawPageHead("Jurnal");
  const long W = 86400;
  long now = (long)acum(), up = (long)(millis() / 1000);
  if (up > W) up = W;
  long t0 = now - up;
  bool absNow = now >= 1000000000L;
  const uint16_t jc[4] = {cCY, cAM, cBL, cGR};
  for (int i = 0; i < 4; i++) {
    int y = 30 + i * 18;
    tft.setTextSize(1); tft.setTextColor(cMUT); tft.setCursor(4, y + 1); tft.print(NUME[i]);
    tft.fillRect(60, y, 176, 10, cLINE);
    bool on = stare[i];
    for (int k = 0; k < evCount; k++) {                    // starea la inceputul ferestrei
      Ev& e = evl[(evHead - evCount + k + EV_N) % EV_N];
      if (e.out == i && ((e.t >= 1000000000UL) == absNow) && (long)e.t >= t0) { on = !e.on; break; }
    }
    long t = t0;
    for (int k = 0; k <= evCount; k++) {
      long te = now; bool ne = on;
      if (k < evCount) {
        Ev& e = evl[(evHead - evCount + k + EV_N) % EV_N];
        if (e.out != i || ((e.t >= 1000000000UL) != absNow) || (long)e.t < t0) continue;
        te = (long)e.t; ne = e.on;
      }
      if (on && te > t) {
        int x = 60 + (int)(176L * (t - (now - W)) / W);
        int w = (int)(176L * (te - t) / W); if (w < 1) w = 1;
        tft.fillRect(x, y, w, 10, jc[i]);
      }
      on = ne; t = te;
    }
  }
  tft.setTextColor(cMUT); tft.setCursor(60, 102); tft.print("-24h");
  tft.setCursor(236 - 24, 102); tft.print("acum");
  tft.drawFastHLine(0, 113, 240, cLINE);
  int rows = 0;
  for (int k = evCount - 1; k >= 0 && rows < 15; k--, rows++) {
    Ev& e = evl[(evHead - evCount + k + EV_N) % EV_N];
    char tb[12], row[40];
    if (e.t >= 1000000000UL) {
      time_t tt = (time_t)e.t; struct tm tmv; localtime_r(&tt, &tmv);
      snprintf(tb, sizeof tb, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    } else snprintf(tb, sizeof tb, "-%ldm", (now - (long)e.t) / 60);
    const char* nm = e.out < 4 ? NUME[e.out] : e.out == 4 ? "Ploaie" : "Intretinere";
    if (e.out >= 10) snprintf(row, sizeof row, "%-5s Intretinere: gata", tb);
    else if (e.h > 0) snprintf(row, sizeof row, "%-5s %-10s %-3s %c %d%%", tb, nm, e.on ? "ON" : "OFF", e.man ? 'M' : 'A', e.h / 10);
    else snprintf(row, sizeof row, "%-5s %-10s %-3s %c", tb, nm, e.on ? "ON" : "OFF", e.man ? 'M' : 'A');
    tft.setTextColor(e.on ? cGR : cMUT); tft.setCursor(4, 118 + rows * 12); tft.print(row);
  }
  if (evCount == 0) { tft.setTextColor(cMUT); tft.setCursor(4, 130); tft.print("Niciun eveniment inca."); }
}

// ---------- pagini Praguri si Timpi ----------
const char* PL[11] = {"Ceata ON sub %", "Ceata OFF peste %", "Ventil. ON peste %", "Ventil. OFF sub %",
                      "Lumina ON", "Lumina OFF", "Pompa ON (min)", "Pompa OFF (min)",
                      "Circul. la (min)", "Circul. dur. (min)", "Standby ecran (min)"};

void addP(int id, int d) {
  switch (id) {
    case 0: S.mistOn = constrain(S.mistOn + d, 50.0f, 100.0f); if (S.mistOn > S.mistOff) S.mistOff = S.mistOn; break;
    case 1: S.mistOff = constrain(S.mistOff + d, 50.0f, 100.0f); if (S.mistOff < S.mistOn) S.mistOn = S.mistOff; break;
    case 2: S.fanOn = constrain(S.fanOn + d, 50.0f, 100.0f); if (S.fanOn < S.fanOff) S.fanOff = S.fanOn; break;
    case 3: S.fanOff = constrain(S.fanOff + d, 50.0f, 100.0f); if (S.fanOff > S.fanOn) S.fanOn = S.fanOff; break;
    case 4: S.lumOra = (S.lumOra + d * 15 + 1440) % 1440; break;
    case 5: S.lumStop = (S.lumStop + d * 15 + 1440) % 1440; break;
    case 6: S.pompaOn = constrain(S.pompaOn + d, 1, 60); break;
    case 7: S.pompaOff = constrain(S.pompaOff + d, 0, 60); break;
    case 8: S.circLa = constrain(S.circLa + d * 5, 0, 240); break;
    case 9: S.circDur = constrain(S.circDur + d, 0, 60); break;
    case 10: S.standbyMin = constrain(S.standbyMin + d, 0, 60); break;
  }
  dirty = true; tDirty = millis();
}

void fmtP(int id, char* b, size_t n) {
  switch (id) {
    case 0: snprintf(b, n, "%.0f%%", S.mistOn); break;
    case 1: snprintf(b, n, "%.0f%%", S.mistOff); break;
    case 2: snprintf(b, n, "%.0f%%", S.fanOn); break;
    case 3: snprintf(b, n, "%.0f%%", S.fanOff); break;
    case 4: snprintf(b, n, "%02d:%02d", S.lumOra / 60, S.lumOra % 60); break;
    case 5: snprintf(b, n, "%02d:%02d", S.lumStop / 60, S.lumStop % 60); break;
    case 6: snprintf(b, n, "%d", S.pompaOn); break;
    case 7: if (S.pompaOff == 0) strcpy(b, "continuu"); else snprintf(b, n, "%d", S.pompaOff); break;
    case 8: if (S.circLa == 0) strcpy(b, "oprit"); else snprintf(b, n, "%d", S.circLa); break;
    case 9: snprintf(b, n, "%d", S.circDur); break;
    default: if (S.standbyMin == 0) strcpy(b, "oprit"); else snprintf(b, n, "%d", S.standbyMin); break;
  }
}

void drawRow(int pg, int slot) {
  int id = (pg == PG_PRAG ? 0 : 4) + slot, rowH = pg == PG_PRAG ? 66 : 38, y0 = 26 + slot * rowH;
  tft.fillRect(0, y0, 240, rowH, cBG);
  tft.drawFastHLine(8, y0 + rowH - 1, 224, cLINE);
  tft.setTextSize(1); tft.setTextColor(cTXT); tft.setCursor(6, y0 + rowH / 2 - 4); tft.print(PL[id]);
  int by = y0 + (rowH - 30) / 2;
  tft.fillRoundRect(118, by, 28, 30, 5, cLINE);
  tft.fillRoundRect(206, by, 28, 30, 5, cLINE);
  tft.fillRect(124, by + 14, 16, 3, cTXT);                          // minus
  tft.fillRect(212, by + 14, 16, 3, cTXT);                          // plus
  tft.fillRect(218, by + 8, 3, 15, cTXT);
  char b[12]; fmtP(id, b, sizeof b);
  int len = (int)strlen(b); uint8_t sz = len <= 5 ? 2 : 1;
  tft.setTextSize(sz); tft.setTextColor(cCY);
  tft.setCursor(176 - len * 6 * sz / 2, y0 + rowH / 2 - 4 * sz); tft.print(b);
}

void drawSettings() {
  drawPageHead(pagina == PG_PRAG ? "Praguri" : "Timpi");
  int cnt = pagina == PG_PRAG ? 4 : 7;
  for (int s = 0; s < cnt; s++) drawRow(pagina, s);
}

// ---------- touch si glisare ----------
#if FOLOSESTE_TOUCH
bool touchXY(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < TOUCH_MIN_Z) return false;
  int rx = p.x, ry = p.y;
  if (TOUCH_SWAP_XY) { int t = rx; rx = ry; ry = t; }
  x = map(rx, TOUCH_X0, TOUCH_X1, 0, 239);
  y = map(ry, TOUCH_Y0, TOUCH_Y1, 0, 319);
  if (TOUCH_INV_X) x = 239 - x;
  if (TOUCH_INV_Y) y = 319 - y;
  x = constrain(x, 0, 239); y = constrain(y, 0, 319);
  if (TOUCH_DEBUG) Serial.printf("touch raw %d %d -> ecran %d %d\n", (int)p.x, (int)p.y, x, y);
  return true;
}

// paginile Praguri/Timpi: id-ul parametrului si sensul (-1/+1) daca (x,y) cade pe un buton, altfel -1
int stepButton(int x, int y, int& dir) {
  if (pagina != PG_PRAG && pagina != PG_TIMP) return -1;
  int cnt = pagina == PG_PRAG ? 4 : 7, rowH = pagina == PG_PRAG ? 66 : 38;
  if (y < 26) return -1;
  int slot = (y - 26) / rowH;
  if (slot >= cnt) return -1;
  if (x >= 114 && x < 150) dir = -1; else if (x >= 202 && x < 238) dir = 1; else return -1;
  return (pagina == PG_PRAG ? 0 : 4) + slot;
}

void onTap(int x, int y, bool lung) {
  if (pagina != PG_HOME) return;
  for (int i = 0; i < 4; i++)
    if (x >= CARD_X[i] && x < CARD_X[i] + 112 && y >= CARD_Y[i] && y < CARD_Y[i] + 82) {
      S.mod[i] = lung ? 0 : (stare[i] ? 2 : 1);   // atingere: ON/OFF manual; apasare lunga: AUTO
      dirty = true; tDirty = millis(); logica();
    }
}

void touchLoop(unsigned long now) {
  static bool down = false, consumed = false, swallow = false;
  static int sx = 0, sy = 0, lx = 0, ly = 0, up = 0;
  static unsigned long tPress = 0, tRep = 0, tPoll = 0;
  if (now - tPoll < 20) return;
  tPoll = now;
  int x, y;
  if (touchXY(x, y)) {
    up = 0; tAct = now;
    if (standby) { wake(); swallow = true; }        // prima atingere doar trezeste ecranul
    if (swallow) return;
    if (!down) {
      down = true; consumed = false; sx = lx = x; sy = ly = y; tPress = tRep = now;
      int dir; int id = stepButton(sx, sy, dir);
      if (id >= 0) { addP(id, dir); drawRow(pagina, id - (pagina == PG_PRAG ? 0 : 4)); consumed = true; }
    } else {
      lx = x; ly = y;
      if (consumed && now - tPress > 400 && now - tRep > 120) {      // apasat lung pe - / +: repeta
        tRep = now;
        int dir; int id = stepButton(sx, sy, dir);
        if (id >= 0) { addP(id, dir); drawRow(pagina, id - (pagina == PG_PRAG ? 0 : 4)); }
      }
    }
  } else if (swallow) {
    swallow = false;
  } else if (down && ++up >= 2) {                                    // degetul a fost ridicat
    down = false; up = 0;
    int dx = lx - sx, dy = ly - sy;
    if (abs(dx) > 40 && abs(dx) > abs(dy)) {                          // glisare
      int p = pagina + (dx < 0 ? 1 : -1);
      if (p >= 0 && p < NP) setPagina(p);
    } else if (!consumed) {
      bool lung = (now - tPress) > 800 && abs(dx) < 15 && abs(dy) < 15;
      onTap(sx, sy, lung);                                            // atingere
    }
  }
}
#endif

void displayInit() {
#if DISPLAY_ST7789
  tft.init(240, 320, TFT_SPI_MODE);
#else
  tft.begin();
#endif
  tft.setRotation(0);
  tft.fillScreen(cBG);
#if BL_PIN >= 0
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BL_PIN, 5000, 8);
#else
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(BL_PIN, BL_CH);
#endif
#endif
  blSet(BL_FULL);
  tAct = millis();
  cvT = new GFXcanvas16(112, 82);
  cvG = new GFXcanvas16(120, 98);
  cvH = new GFXcanvas16(240, 34);
  cvF = new GFXcanvas16(32, 16);
  cvB = new GFXcanvas16(10, 12);
#if FOLOSESTE_TOUCH
  ts.begin();
  ts.setRotation(0);
#endif
  drawDots();
}

bool schimbat(float a, float b, float prag) {
  if (isnan(a) || isnan(b)) return isnan(a) != isnan(b);
  return fabsf(a - b) >= prag;
}

void displayLoop() {
  static unsigned long tF = 0, tS = 0, tCh = 0, tJ = 0;
  static int f = 0, lastMin = -2, lastEv = -1;
  static bool lastW = false, prevOn[4] = {false, false, false, false};
  static uint8_t prevMode[4] = {9, 9, 9, 9};
  static float shT = -999, shH = -999;
  unsigned long now = millis();
#if FOLOSESTE_TOUCH
  touchLoop(now);
#endif
  if (dirty && now - tDirty > 2000) { salveaza(); dirty = false; }   // salvare in flash dupa 2 s fara modificari
#if FOLOSESTE_TOUCH
  if (!standby && S.standbyMin > 0 && now - tAct > (unsigned long)S.standbyMin * 60000UL) enterStandby();
  if (standby) { drawStandby(now); return; }
#endif
  if (now - tS < 50) return;
  tS = now;
  bool first = dispRedraw;

  switch (pagina) {
    case PG_HOME: {
      struct tm t; bool ok = oraLocala(&t);
      int mi = ok ? t.tm_min : -1;
      bool w = WiFi.status() == WL_CONNECTED;
      char alNou[40]; alertaEcran(alNou, sizeof alNou);
      if (first || mi != lastMin || w != lastW || strcmp(alNou, ultimaAlerta) != 0) { drawHeader(); lastMin = mi; lastW = w; }
      if (first || schimbat(temp, shT, 0.1f)) {
        shT = temp;
        uint16_t col = isnan(temp) ? cMUT : (temp < 22 ? cBL : temp <= 29 ? cGR : temp <= 32 ? cAM : cRD);
        char vb[8]; if (isnan(temp)) strcpy(vb, "--"); else snprintf(vb, sizeof vb, "%.1f", temp);
        drawGauge(0, 34, temp, 20, 35, col, vb, "Temperatura \xF7""C", "20", "35");
      }
      if (first || schimbat(hum, shH, 0.5f)) {
        shH = hum;
        uint16_t col = isnan(hum) ? cMUT : ((hum >= S.mistOn && hum <= S.fanOn) ? cGR : cAM);
        char vb[8]; if (isnan(hum)) strcpy(vb, "--"); else snprintf(vb, sizeof vb, "%.0f", hum);
        drawGauge(120, 34, hum, 40, 100, col, vb, "Umiditate %", "40", "100");
      }
      bool tick = (now - tF >= 120);
      if (tick) { tF = now; f++; }
      for (int i = 0; i < 4; i++) {
        bool chg = (prevOn[i] != stare[i]) || prevMode[i] != S.mod[i];
        if (stare[i]) { if (tick || chg || first) drawTile(i, true, f); }
        else if (chg || first) drawTile(i, false, 0);
        prevOn[i] = stare[i]; prevMode[i] = S.mod[i];
      }
      break;
    }
    case PG_GRAF:
      if (first || now - tCh > 30000) {
        tCh = now;
        drawPageHead("Grafice 24 h");
        drawChart(26, "Temperatura C", histT, cAM, 4, -10, 60);
        drawChart(148, "Umiditate %", histH, cCY, 10, 0, 100);
        tft.setTextSize(1); tft.setTextColor(cMUT);
        tft.setCursor(30, 272); tft.print("-24h"); tft.setCursor(210, 272); tft.print("acum");
      }
      break;
    case PG_JUR:
      if (first || evHead != lastEv || now - tJ > 30000) { tJ = now; lastEv = evHead; drawJurnal(); }
      break;
    default:                                                           // praguri / timpi
      if (first) drawSettings();
      break;
  }
  dispRedraw = false;
}
#endif
