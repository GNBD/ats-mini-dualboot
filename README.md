# DBFM — DualBoot Firmware Manager

**v4.2.2 (DES)** · ESP32-S3 **N16R8** · SI4732 receiver

<img width="320" height="170" alt="shot" src="https://github.com/user-attachments/assets/9f1d51cd-a125-4306-ba29-41f59d762d1d" />


DBFM is a standalone boot manager for the ATS Mini (ESP32-S3 + SI4732). It runs
from its own flash partition before the radio firmware, lets you choose between
two application slots, and provides recovery, update, partition and storage
tools.

```
Power on
  └─ DBFM (recovery, 0x860000)
       ├─ app0  (0x010000)  firmware A
       └─ app1  (0x390000)  firmware B
```

> **Work in progress.** This is a test build. Bugs, failed boots or data loss
> are possible. The partition layout changed from v3.x, so upgrading may reset
> internal storage. N16R8 (16MB flash + 8MB PSRAM) only.

---

## English

### Highlights

- **2 × 2 DBFM dashboard** — app slots, OTA, Partition, Settings.
- **Per-slot settings isolation (DES)** — app0 and app1 see the same `settings`
  partition name, but the active partition table points it at a different NVS
  region (`0xF9D000` / `0xFBD000`).
- **Firmware identity** — each slot shows its real firmware name, version and
  ACTIVE/STANDBY/EMPTY state.
- **App slot info** — Default boot / Hold mode / Info (build date, IDF, slot
  size, source file name and install time when recorded by an internal OTA).
- **OTA updates** — local file or network URL, then automatic flash.
- **Boot menu always** — skip the splash, open the manager, focus the last slot
  and boot it after 5 seconds without input. Any encoder action cancels the
  countdown and keeps the manager open.
- **Partition tools** — info, canonical repair, app0/app1 resize (only when both
  slots are empty).
- **Storage tools** — LittleFS file manager, settings backup/restore.
- **WiFi app** — OFF / AP / CONNECT / SCAN, RSSI and lock indicators.
- **Web UI** — status, firmware upload, files, network and system pages.
- **Screenshot endpoints** — `/shot.png` and `/shot.ppm` while the recovery web
  server is running.
- **Guided first-run tour** and per-item colours in a low-glare dark theme.

### Controls

| Action | Function |
|---|---|
| Turn | Move / adjust |
| Click | Open / select / confirm |
| Hold | Back / cancel |
| Boot (default) | No input for 1 s → boot the last slot |
| Hold during boot | Open DBFM |

A hold ring with a back chevron appears while a long press is being detected.

### Boot menu always

Enable `Settings → Boot menu always`:

1. Splash is skipped.
2. DBFM opens immediately.
3. The last used slot is focused.
4. With no input for 5 seconds, that slot boots.
5. Turning the encoder or pressing the button cancels the countdown and stays
   in the manager.

### Install

Requirements: ESP32-S3 **N16R8**, a data-capable USB cable, esptool (or
`uvx --from esptool`).

Back up the whole flash first:

```sh
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 921600 \
  read-flash 0x0 ALL original-flash.bin
```

Full install:

| File | Address |
|---|---|
| `ats-mini-bootloader-v4.bin` | `0x0` |
| `ats-mini-partitions-v4.bin` | `0x8000` |
| `boot_app0.bin` | `0xE000` |
| `ats-mini-recovery-4.2.2.bin` | `0x860000` |

```sh
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 \
  write-flash 0x0 ats-mini-bootloader-v4.bin \
              0x8000 ats-mini-partitions-v4.bin \
              0xE000 boot_app0.bin \
              0x860000 ats-mini-recovery-4.2.2.bin
```

Recovery-only update (bootloader and partition table unchanged):

```sh
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 \
  write-flash 0x860000 ats-mini-recovery-4.2.2.bin
```

### Apps and updates

- app0: `0x010000`, app1: `0x390000`, 3.5MB each.
- Use an **ESP32-S3 OTA app image**, not a merged/full-flash image.
- From DBFM: `OTA → source → slot → firmware`; network sources download and
  flash automatically.
- From the web UI: **Firmware → Upload**; choose “Save to storage”, “flash to
  App0” or “flash to App1”.
- From esptool: write `app0.bin` to `0x10000` or `app1.bin` to `0x390000`.

Internal OTA records the source file name and install time per slot. Info shows
those values only when the recorded firmware version matches the current slot;
external esptool flashes show `-`.

### Web UI

Open `http://<device-ip>/` while DBFM is running. Pages:

- **Status** — callsign, app slots, boot actions, storage.
- **Firmware** — upload and flash.
- **Files** — LittleFS download, flash and delete.
- **Network** — WiFi scan/connect, network and web account settings.
- **System** — device information and partition table.

The page is responsive: sidebar on desktop, top tabs on mobile.

### Partition layout (16MB, Table A / ENV0)

| Partition | Offset | Size |
|---|---|---|
| Table A | `0x008000` | 4K |
| `nvs` | `0x009000` | 20K |
| `otadata` | `0x00E000` | 8K |
| `app0` | `0x010000` | 3.5M |
| `app1` | `0x390000` | 3.5M |
| `ffat` | `0x710000` | 1.3M |
| `recovery` | `0x860000` | 1.625M |
| `littlefs` | `0xA00000` | 5.58M |
| `rec_settings` | `0xF95000` | 32K |
| `settings` (ENV0) | `0xF9D000` | 128K |
| `settings` (ENV1) | `0xFBD000` | 128K |
| `coredump` | `0xFDF000` | 128K |
| Table B | `0xFFF000` | 4K |

Table B differs only in the `settings` offset. DBFM rebuilds the canonical
table from the image if it detects a mismatch.

### Build

- Arduino-ESP32 core **3.3.11 / 3.3.12**, ESP-IDF **5.5.5**
- FQBN:
  `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,CPUFreq=80,USBMode=hwcdc,FlashMode=qio,PartitionScheme=custom,DebugLevel=none`
- Libraries: LovyanGFX, WiFi, WebServer, HTTPClient, LittleFS, Preferences
- The custom bootloader is in `bootloader.bin`; see `bootloader.md`.

### Notes

- Recovery **cannot self-OTA** while running from `ota_2`; reflash it at
  `0x860000` over USB. A dedicated updater partition is planned for v5.
- External full-flash/merged images can break the DES tables; use slot images.
- The web server is always on in recovery; the former on/off toggle was removed.
- Screenshot endpoints allocate PSRAM buffers and are intended for diagnostics.

### License

- Original DBFM source: **MIT**
- Encoder (`encoder.cpp/h`, miniradio-derived): **MIT**
- Bootloader binary: **Apache-2.0** (ESP-IDF derivative)
- Arduino-ESP32 core: **LGPL-2.1-or-later**
- ESP-IDF components: **Apache-2.0**
- No GPL-3.0 code is linked.

See `NOTICE`, `LICENSE`, and `LICENSES/`.

---

## 한국어

**DBFM(DualBoot Firmware Manager)** 은 ATS Mini(ESP32-S3 + SI4732)용
**부트 매니저(리커버리)** 입니다. 전원을 켜면 app0/app1보다 먼저 실행되어
슬롯 선택, 복구, 업데이트, 파티션·저장소 관리 기능을 제공합니다.

> **테스트 빌드입니다.** 크래시·부팅 실패·데이터 손실 가능성이 있습니다.
> v3.x와 파티션 구조가 달라 업그레이드 시 내부 저장소가 초기화될 수 있습니다.
> **N16R8(16MB + 8MB PSRAM) 전용**입니다.

### 주요 기능

- **2×2 대시보드** — app0/app1, OTA, Partition, Settings
- **DES 설정 격리** — app0/app1이 같은 `settings` 라벨을 쓰지만 실제 NVS
  주소(`0xF9D000` / `0xFBD000`)가 달라 설정이 섞이지 않음
- **펌웨어 정보** — 슬롯별 실제 펌웨어 이름·버전·ACTIVE/STANDBY/EMPTY
- **슬롯 Info** — 빌드 날짜, IDF, 슬롯 크기, 내부 OTA로 기록된 파일명·설치 시각
- **OTA** — 로컬 파일 또는 네트워크 URL 선택 후 자동 플래시
- **Boot menu always** — 스플래시 생략, 마지막 슬롯 자동 포커스, 5초 무입력 시
  자동 부팅. 엔코더 조작 시 카운트 취소 후 메뉴 유지
- **Partition** — 정보, 정본 복구, app0/app1 크기 조정(양 슬롯이 빌 때만)
- **저장소** — LittleFS 파일 관리, 설정 백업/복원
- **WiFi** — OFF / AP / CONNECT / SCAN, RSSI·잠금 표시
- **웹 UI** — 상태, 펌웨어 업로드, 파일, 네트워크, 시스템
- **스크린샷** — 리커버리 웹서버 실행 중 `/shot.png`, `/shot.ppm`
- **가이드** — 첫 실행 안내, 마지막 페이지에서 한 번 더 돌리면 종료

### 조작

| 동작 | 기능 |
|---|---|
| 회전 | 이동 / 값 조절 |
| 클릭 | 열기 / 선택 / 확인 |
| 홀드 | 뒤로 / 취소 |
| 기본 부팅 | 1초 무입력 시 마지막 슬롯 부팅 |
| 부팅 중 홀드 | DBFM 진입 |

홀드 판정 중에는 원형 진행 링과 BACK 표시가 나타납니다.

### 설치

N16R8 기기, 데이터 전송 가능한 USB 케이블, esptool이 필요합니다.
먼저 전체 flash를 백업하세요.

```sh
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 921600 \
  read-flash 0x0 ALL original-flash.bin
```

전체 설치:

| 파일 | 주소 |
|---|---|
| `ats-mini-bootloader-v4.bin` | `0x0` |
| `ats-mini-partitions-v4.bin` | `0x8000` |
| `boot_app0.bin` | `0xE000` |
| `ats-mini-recovery-4.2.2.bin` | `0x860000` |

```sh
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 \
  write-flash 0x0 ats-mini-bootloader-v4.bin \
              0x8000 ats-mini-partitions-v4.bin \
              0xE000 boot_app0.bin \
              0x860000 ats-mini-recovery-4.2.2.bin
```

리커버리만 갱신:

```sh
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 \
  write-flash 0x860000 ats-mini-recovery-4.2.2.bin
```

### 앱 슬롯

- app0 `0x010000`, app1 `0x390000`, 각 3.5MB
- **ESP32-S3 OTA 앱 이미지**만 사용하세요. merged/full-flash 이미지는 금지입니다.
- DBFM: `OTA → 소스 → 슬롯 → 펌웨어` 순서로 선택하면 자동 플래시됩니다.
- 웹: **Firmware → Upload**에서 저장/App0/App1 플래시를 선택합니다.
- esptool: `app0.bin`은 `0x10000`, `app1.bin`은 `0x390000`에 기록합니다.

내부 OTA로 플래시한 경우에만 파일명·설치 시각이 기록됩니다. 현재 슬롯의
펌웨어 버전과 기록된 버전이 다르면 Info에 `-`로 표시됩니다.

### 웹 UI

DBFM 실행 중 `http://<기기IP>/` 로 접속합니다.

- **Status** — Callsign, 앱 슬롯, 부팅, 저장소
- **Firmware** — 업로드/플래시
- **Files** — LittleFS 다운로드·플래시·삭제
- **Network** — WiFi 스캔/연결, 네트워크·웹 계정
- **System** — 기기 정보·파티션

데스크톱은 사이드바, 모바일은 상단 탭으로 표시됩니다.

### 파티션 (16MB, Table A / ENV0)

| 파티션 | offset | 크기 |
|---|---|---|
| Table A | `0x008000` | 4K |
| `nvs` | `0x009000` | 20K |
| `otadata` | `0x00E000` | 8K |
| `app0` | `0x010000` | 3.5M |
| `app1` | `0x390000` | 3.5M |
| `ffat` | `0x710000` | 1.3M |
| `recovery` | `0x860000` | 1.625M |
| `littlefs` | `0xA00000` | 5.58M |
| `rec_settings` | `0xF95000` | 32K |
| `settings` (ENV0) | `0xF9D000` | 128K |
| `settings` (ENV1) | `0xFBD000` | 128K |
| `coredump` | `0xFDF000` | 128K |
| Table B | `0xFFF000` | 4K |

Table B는 `settings` offset 한 줄만 다릅니다. DBFM은 테이블이 다르면 내장
정본으로 자동 복구합니다.

### 빌드

- Arduino-ESP32 core **3.3.11 / 3.3.12**, ESP-IDF **5.5.5**
- FQBN:
  `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,CPUFreq=80,USBMode=hwcdc,FlashMode=qio,PartitionScheme=custom,DebugLevel=none`
- 라이브러리: LovyanGFX, WiFi, WebServer, HTTPClient, LittleFS, Preferences
- 커스텀 부트로더는 `bootloader.bin`, 빌드 방법은 `bootloader.md` 참고

### 참고

- 리커버리는 실행 중인 `ota_2`를 **self-OTA할 수 없습니다.** USB로
  `0x860000`에 다시 플래시하세요. 전용 업데이터 파티션은 v5 예정입니다.
- 외부 full-flash/merged 이미지는 DES 테이블을 깨뜨릴 수 있습니다.
- 리커버리 웹서버는 항상 켜져 있습니다. 이전 on/off 토글은 제거했습니다.
- 스크린샷 엔드포인트는 PSRAM 버퍼를 사용하는 진단용 기능입니다.

### 라이선스

- DBFM 원본 소스: **MIT**
- 엔코더 (`encoder.cpp/h`, miniradio 파생): **MIT**
- 부트로더 바이너리: **Apache-2.0** (ESP-IDF 파생)
- Arduino-ESP32 core: **LGPL-2.1-or-later**
- ESP-IDF 구성요소: **Apache-2.0**
- GPL-3.0 코드는 링크하지 않습니다.

자세한 내용은 `NOTICE`, `LICENSE`, `LICENSES/`를 참고하세요.
