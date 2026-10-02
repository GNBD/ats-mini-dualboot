# ATS Mini Dualboot

**Work in progress** — crashes, boot failures and data loss may occur.
[한국어](README.md)

**Boot manager** for the SI4732 (ESP32-S3) receiver. On power-on the boot
manager runs first and lets you choose whether to boot `app0` or `app1`.

```
power on ─> boot manager (recovery) ─> app0  (firmware A)
                                    └> app1  (firmware B)
```

---

## One-click web flasher (nothing to install)

### **<https://gnbd.github.io/ats-mini-dualboot/install.html>**

Open in Chrome/Edge → **Connect device** → (optional) app0/app1 firmware → accept the terms → **Flash!**
Flashes the boot manager (recovery) and app firmware **right from the browser**. (v4.1.0 DES; merged images are trimmed to the app automatically.)

Flashing guide: <https://gnbd.github.io/ats-mini-dualboot/flashing.html>

---

<img width="657" height="350" alt="Boot manager" src="https://github.com/user-attachments/assets/57f89dcb-ddf6-4baa-b854-d4935b872c86" />

## At a glance

| Part | Sketch | Hardware | recovery | Web/OTA | Status |
|---|---|---|---|---|---|
| Boot manager | `ats-mini-recovery-beta` | N16R8 (16MB) | `0x860000` | Yes | tested |
| Boot manager | `ats-mini-recovery-lite` | N8R2 (8MB) | `0x660000` | No | **untested** |

Three rules:

1. **Never mix the 16MB and 8MB tables.** Misaligned offsets will not boot.
2. The boot manager **binary**, its **partition table** and the **custom
   bootloader** must match as a set.
3. `lite` **cannot update itself** — always re-flash the whole set from outside.

## Boot manager variants

### `ats-mini-recovery-beta` — 16MB default (v3.1.1)

Default boot manager for current 16MB (N16R8) devices.

- Menu: Boot App0 / Boot App1 / Firmware Update / WiFi / Erase / About
- Serves a background web server when connected to a STA WiFi (file upload /
  firmware update); otherwise runs as an AP.
- Partition table: [`ats-mini-recovery-beta/partitions.csv`](ats-mini-recovery-beta/partitions.csv)

### `ats-mini-recovery-lite` — N8R2 8MB (v1.0.0)

Minimal boot manager for 8MB devices. No web server, WiFi or QR code.

- Menu: Boot App0 / Boot App1 / Factory Reset / About
- Partition table: [`ats-mini-recovery-lite/partitions.csv`](ats-mini-recovery-lite/partitions.csv)
- Install: [`flash-lite.ps1`](ats-mini-recovery-lite/flash-lite.ps1) → `.\flash-lite.ps1 -Install -Target 8MB -Port COM7`
- **Cannot update itself**: no web/OTA, so firmware + app must be flashed from
  outside as a whole set.
- **Untested**: not yet tested on N8R2 hardware. See the release notes.

## Hardware

| Target | Required hardware | Notes |
|---|---|---|
| `ats-mini-recovery-beta` | ESP32-S3 **N16R8** (16MB flash + 8MB PSRAM) | Other variants (N8R2, N8R8, N16R2, ...) untested |
| `ats-mini-recovery-lite` | ESP32-S3 **N8R2** (8MB flash + 2MB PSRAM) | Untested on hardware |

## Partitions and bootloader

| flash | Partition table | recovery | Custom bootloader |
|---|---|---|---|
| 16MB | [`beta`](ats-mini-recovery-beta/partitions.csv) | `0x860000` | [`beta/bootloader.bin`](ats-mini-recovery-beta/bootloader.bin) |
| 8MB | [`lite`](ats-mini-recovery-lite/partitions.csv) | `0x660000` | [`lite/bootloader.bin`](ats-mini-recovery-lite/bootloader.bin) |

A **recovery-first custom bootloader** makes the boot manager always run first.
It is built from ESP-IDF v5.5.5 for each flash size; see
[`ats-mini-recovery-beta/bootloader.md`](ats-mini-recovery-beta/bootloader.md).

**How it works:** the boot manager calls `esp_ota_set_boot_partition()`, marking
the target `ESP_OTA_IMG_NEW` for a **one-shot** boot. Once the target confirms
itself (`esp_ota_mark_app_valid_cancel_rollback()`) or fails, the next boot
returns to the boot manager.

## Download

- Releases: <https://github.com/GNBD/ats-mini-dualboot/releases> (`v1.0.0-lite` = N8R2 prerelease, untested)

## License

Several licenses apply, not a single one. See [NOTICE](NOTICE) for details and
third-party notices.

| Part | License |
|---|---|
| Boot manager source (sketches, partitions, flash scripts) | [MIT](LICENSE) |
| Bootloader **binaries** (`bootloader.bin`) | **Apache-2.0** (ESP-IDF derivative) |
| Application images (`.bin`) | **GPL-3.0** (contain `Rotary`) |
| Arduino-ESP32 core and external libraries | original terms retained (LGPL-2.1 etc.) |

Things to note:

- **Application images are GPL-3.0**: they link `Rotary` (GPL-3.0), so those
  images are covered by GPL-3.0. Provide the matching source when
  distributing.
- **OTA is distribution too**: a `.bin` sent over OTA carries the same
  obligations as a release download; OTA is not a separate license.
- **Third-party components**: LovyanGFX (BSD-2), LittleFS (Apache-2.0),
  ESP-IDF (Apache-2.0), the Arduino core (LGPL-2.1) and others keep their own
  terms. Full license texts are in [LICENSES/](LICENSES/) (MIT, Apache-2.0,
  GPL-3.0, LGPL-2.1).
- **Hardware/images**: may be CC BY-NC-SA 3.0 (non-commercial).
- This project is not an official product of the upstream projects or hardware
  authors.
