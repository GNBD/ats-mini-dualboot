Correct the third-party license labels shown on the About screen: LovyanGFX is
BSD-2-Clause (FreeBSD), not MIT, and the LittleFS wrapper from Arduino-ESP32 is
Apache-2.0, not MIT. About > License now also lists the prebuilt bootloader
(Apache-2.0) and About > Libraries lists the QR code component (Apache-2.0).
The license texts and notices now describe each release by component
(application images GPL-3.0, bootloaders Apache-2.0, Arduino core LGPL-2.1)
and state that OTA transfers carry the same distribution obligations as
release downloads.
