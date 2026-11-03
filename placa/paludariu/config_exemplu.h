// =====================================================================================
//  DATELE TALE. Le completezi o singura data, aici.
//  Cand primesti o versiune noua, inlocuiesti toate fisierele din folder, mai putin config.h.
// =====================================================================================
#pragma once

// ---------- WIFI ----------
// Optional: poti lasa "NUME_RETEA" si conecta placa din aplicatie (Adauga o placa > Placa noua).
// Ce configurezi din aplicatie are prioritate fata de ce scrie aici.
const char* WIFI_SSID = "NUME_RETEA";
const char* WIFI_PASS = "PAROLA";

// ---------- LOGIN PAGINA WEB (si in aplicatie) ----------
const char* WEB_USER = "admin";
const char* WEB_PASS = "schimba-ma";   // aceeasi parola o cere si Arduino IDE la actualizarea prin WiFi

// ---------- ACTUALIZARI DE PE GITHUB (optional) ----------
// Cheia publica facuta cu semnare.html (130 de caractere, incepe cu 04). Nu e secreta.
// Fara ea, placa nu instaleaza nimic de pe internet (actualizarea manuala cu .bin merge oricum).
#define CHEIE_ACTUALIZARE ""
// Depozitul GitHub, ex. "numele-tau/paludariu". Il poti pune si din pagina (Setari > Placa).
#define GITHUB_DEPOZIT ""

// ---------- RETEAUA DE CONFIGURARE (optional) ----------
// Parola retelei "Paludariu-Setup", folosita doar la "Placa noua". Daca o schimbi, aplicatia ti-o cere.
// #define SETUP_PAROLA "paludariu"

// ---------- TELEGRAM ----------
// Lasa ghilimelele goale ca sa opresti Telegram.
#define FOLOSESTE_TELEGRAM true
const char* TG_TOKEN = "";   // ex. "123456789:AAH...xyz"
const char* TG_CHAT = "";    // ex. "123456789"

// ---------- PLOAIE (pompa Tuya / Smart Life) ----------
// Cat timp TUYA_KEY e gol, ploaia nu apare nicaieri. Daca in cheie apare " sau \ pune un \ in fata lor.
#define FOLOSESTE_PLOAIE true
const char* TUYA_IP  = "";   // IP-ul pompei din router, ex. "192.168.1.20"
const char* TUYA_ID  = "";   // Device ID
const char* TUYA_KEY = "";   // local_key: exact 16 caractere. Nu o pune pe internet.
#define TUYA_VER 0           // 0 = placa afla singura versiunea (3.3, 3.4 sau 3.5)
#define TUYA_DP 1            // comanda pornit/oprit a pompei
#define TUYA_DP_TIMER 9      // temporizatorul din pompa (siguranta); 0 = nu-l folosi

// ---------- NUMELE PLACII ----------
const char* DEVICE_NAME = "Paludariu 1";   // apare in titlul paginii si in aplicatie
const char* MDNS_NAME = "paludariu1";      // adresa locala: http://paludariu1.local (litere mici, cifre, -)

// ---------- RELEE ----------
// true = releul porneste pe LOW (jumperi pe "Low" / SSR "Low Level"); false = jumperi pe "High".
//                          pompa  lumina ceata  ventilator
const bool ACTIV_LOW[4] = {true,  true,  true,  true};

// ---------- SENZORI ----------
#define FOLOSESTE_DHT22 true     // true = DHT22 pe GPIO4; false = SHT31 pe I2C (SDA 21, SCL 22)
#define FOLOSESTE_NIVEL false    // true = flotor de nivel apa intre GPIO32 si GND

// Radiatorul lampii: false = DS18B20 pe GPIO21 (cu 4,7k la 3V3); true = NTC pe GPIO34
#define SENZOR_LED_NTC false
const float NTC_R25 = 10000;     // NTC-ul la 25 grade ("10K" din anunt)
const float NTC_B = 3950;        // constanta B ("B3950", "3435" ...)
const float NTC_RFIX = 10000;    // rezistenta fixa din divizor (ex. 4700)
const float NTC_CORECTIE = 0;    // grade adaugate la citire, pentru reglaj fin

// Ventilatorul radiatorului: verificarea turatiei (firul galben "tach")
// Leaga firul galben la GPIO35 si pune o rezistenta de 10 kohm intre GPIO35 si 3V3.
// Daca ventilatorul trebuie sa mearga si nu se invarte: lumina se stinge si primesti mesaj pe Telegram.
#define FANLED_TACH false        // true = firul de turatie e legat (lasa false pana il legi, altfel lumina se stinge)
#define FANLED_4PIN false        // false = ventilator cu 3 fire (test de 10 s la 100% din 30 in 30 min)
                                 // true  = ventilator cu 4 fire PWM (turatia se citeste tot timpul)
#define FANLED_PULSURI 2         // impulsuri pe rotatie (2 la aproape toate ventilatoarele)
#define FANLED_RPM_MIN 300       // sub atat, ventilatorul e considerat oprit
#define FANLED_TEST_MIN 30       // la cate minute se face testul (doar la 3 fire)

// ---------- ECRAN TFT ----------
#define FOLOSESTE_DISPLAY false  // true = ai montat ecranul ILI9341 si ai instalat bibliotecile
#define DISPLAY_ST7789 false     // true = ecran ST7789 (fara touch)
#define FOLOSESTE_TOUCH true     // touch XPT2046 (ILI9341 cu touch)
// Calibrare touch: pune TOUCH_DEBUG true, atinge colturile si copiaza aici valorile din Serial Monitor.
#define TOUCH_DEBUG false
#define TOUCH_SWAP_XY false
#define TOUCH_INV_X false
#define TOUCH_INV_Y false
#define TOUCH_X0 300
#define TOUCH_X1 3800
#define TOUCH_Y0 300
#define TOUCH_Y1 3800

// ---------- BLUETOOTH (de obicei oprit) ----------
#define FOLOSESTE_BLE false
#define BLE_PIN "1234"
