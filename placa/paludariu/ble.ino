// Paludariu - Bluetooth LE optional (FOLOSESTE_BLE in config.h)
// Face parte din proiectul paludariu.ino; Arduino IDE il deschide ca tab.

// ======================= BLUETOOTH LE (optional) =======================
// Control din telefon prin Bluetooth, fara WiFi. Foloseste serviciul "Nordic UART", deci merge cu orice
// aplicatie de tip terminal BLE (ex. "Serial Bluetooth Terminal" in modul BLE, "nRF Toolbox" > UART).
// Comenzi (termina fiecare comanda cu Enter / newline):
//   PIN 1234                       (obligatoriu prima data, dupa fiecare conectare)
//   STATUS                         (temperaturi, umiditate, starea accesoriilor)
//   POMPA|LUMINA|CEATA|VENTILATOR ON|OFF|AUTO
#if FOLOSESTE_BLE
#define NUS_SVC "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   // telefon -> placa (scriere)
#define NUS_TX  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   // placa -> telefon (notificari)
#define BLE_STATUS_S 30                                  // status automat la fiecare 30 s dupa autentificare

BLECharacteristic* bleTx = nullptr;
volatile bool bleConn = false;
bool bleAuth = false;
String bleOut = "", bleIn = "";

class BleSrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) { bleConn = true; bleAuth = false; bleOut = ""; bleIn = ""; }
  void onDisconnect(BLEServer*) { bleConn = false; bleAuth = false; BLEDevice::startAdvertising(); }
};
class BleRxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) {
    auto v = c->getValue();
    bleIn += String(v.c_str());
  }
};

void bleSay(const String& s) { bleOut += s; }

String bleStatus() {
  String s = "T=" + (isnan(temp) ? String("--") : String(temp, 1)) + "C H=" + (isnan(hum) ? String("--") : String(hum, 0)) +
             "% LED=" + (isnan(tLed) ? String("--") : String(tLed, 1)) + "C RAC=" + String(fanLedPct) + "%\n";
  for (int i = 0; i < 4; i++)
    s += String(NUME[i]) + ": " + (stare[i] ? "ON" : "OFF") + (S.mod[i] == 0 ? " (auto)" : " (manual)") + "\n";
  return s;
}

void bleHandle(String L) {
  L.trim(); L.toUpperCase();
  if (L.length() == 0) return;
  if (!bleAuth) {
    if (L == String("PIN ") + BLE_PIN) { bleAuth = true; bleSay("OK\n" + bleStatus()); }
    else bleSay("Trimite: PIN <cod>\n");
    return;
  }
  if (L == "STATUS" || L == "S") { bleSay(bleStatus()); return; }
  if (L == "HELP" || L == "?") { bleSay("STATUS | POMPA|LUMINA|CEATA|VENTILATOR ON|OFF|AUTO\n"); return; }
  int sp = L.indexOf(' ');
  String nm = sp > 0 ? L.substring(0, sp) : L, arg = sp > 0 ? L.substring(sp + 1) : String("");
  arg.trim();
  int idx = nm == "POMPA" ? 0 : nm == "LUMINA" ? 1 : nm == "CEATA" ? 2 : nm == "VENTILATOR" ? 3 : -1;
  int v = arg == "AUTO" ? 0 : arg == "ON" ? 1 : arg == "OFF" ? 2 : -1;
  if (idx < 0 || v < 0) { bleSay("Comanda necunoscuta. HELP\n"); return; }
  S.mod[idx] = v; salveaza(); logica();
  bleSay("OK " + nm + " " + arg + "\n");
}

void bleInit() {
  BLEDevice::init(DEVICE_NAME);
  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new BleSrvCb());
  BLEService* svc = srv->createService(NUS_SVC);
  bleTx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  bleTx->addDescriptor(new BLE2902());
  BLECharacteristic* rx = svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new BleRxCb());
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SVC);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.printf("Bluetooth pornit: \"%s\". Heap liber: %u octeti\n", DEVICE_NAME, (unsigned)ESP.getFreeHeap());
}

void bleLoop() {
  static unsigned long tPump = 0, tSt = 0;
  unsigned long now = millis();
  int nl;                                                   // comenzi complete (terminate cu Enter)
  while ((nl = bleIn.indexOf('\n')) >= 0 || (nl = bleIn.indexOf('\r')) >= 0) {
    String line = bleIn.substring(0, nl);
    bleIn.remove(0, nl + 1);
    bleHandle(line);
  }
  if (bleIn.length() > 80) bleIn = "";                      // protectie la date nedorite
  if (bleConn && bleAuth && now - tSt > BLE_STATUS_S * 1000UL) { tSt = now; bleSay(bleStatus()); }
  if (bleConn && bleOut.length() > 0 && now - tPump >= 25) {   // trimite cate 20 de caractere (limita BLE implicita)
    tPump = now;
    String chunk = bleOut.substring(0, 20);
    bleOut.remove(0, chunk.length());
    bleTx->setValue((uint8_t*)chunk.c_str(), chunk.length());
    bleTx->notify();
  }
}
#endif
