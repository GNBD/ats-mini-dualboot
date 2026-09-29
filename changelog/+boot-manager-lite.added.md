Add `ats-mini-recovery-lite` (Boot Manager Lite) for ESP32-S3 N8R2 boards with an 8MB
flash: a minimal recovery menu (Boot App0 / Boot App1 / Factory Reset / About), its own
8MB partition table (recovery at `0x660000`) kept separate from the 16MB tables, and a
matching custom bootloader built from ESP-IDF v5.5.5 with the 8MB flash-size header.
**Not tested on N8R2 hardware yet.**
**Internal updates are not possible:** Boot Manager Lite ships without a web server or
OTA, so nothing can be updated from inside the device. Firmware *and* application have to
be flashed from outside as a whole set (`flash-lite.ps1 -Install`, or write
`0x0`/`0x8000`/`0xE000`/`0x10000`/`0x660000` manually with esptool).
