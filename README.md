# Paludariu

Automatizare pentru paludariu cu ESP32: pompă, lumină, ceață, ventilator, ploaie (pompă Tuya),
senzori de temperatură și umiditate, pagină web pe placă și aplicație Android.

## Ce e aici
- `placa/paludariu/` – codul plăcii, toate taburile (ESP32 Dev Module,
  Partition Scheme: Minimal SPIFFS 1.9MB APP with OTA). Deschizi `paludariu.ino` în Arduino IDE.
- `placa/paludariu/config_exemplu.h` – model de setări. Îl copiezi în același folder cu numele `config.h`
  și îl completezi. **`config.h` nu se publică niciodată.**
- `aplicatie/PaludariuApp` – aplicația Android (Android Studio).
- `unelte/semnare.html` – face cheia de semnare și semnează `paludariu.ino.bin` înainte de publicare.

## Versiuni
Fișierele gata făcute sunt la **Releases**. Tag-ul e versiunea, de exemplu `v1.8`.
- `paludariu.ino.bin` + `paludariu.ino.bin.sig` – placa le găsește singură și le instalează
  doar dacă semnătura se potrivește cu cheia ei.
- `Paludariu.apk` – aplicația pentru telefon (o anunță singură; merge și cu Obtainium).

© 2026 Andrei T
