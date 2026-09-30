# ATS Mini Boot Manager (theme) — v3.2.1

SI4732 (ESP32-S3) 수신기용 **부트 매니저(리커버리)** 의 테마 버전입니다.
전원을 켜면 부트 매니저가 먼저 실행되어 `app0` / `app1` 중 하나를 골라 부팅합니다.

```
전원 ON ─> 부트 매니저 (recovery) ─> app0  (펌웨어 A)
                                  └> app1  (펌웨어 B)
```

> **미완성 (work in progress)** — 크래시, 부팅 실패, 데이터 손실이 발생할 수 있습니다.
> 16MB(N16R8) 기기 전용입니다. 다른 flash 크기(N8R2 등)에 쓰지 마세요.

## 이 버전의 특징

- **타일 UI** — 메인 메뉴 / Settings / WiFi / 부팅 모드 / 펌웨어 대상·소스 / Erase 가
  모두 타일 그리드. WiFi 스캔·파일 목록·URL 선택은 행(row) 레이아웃.
- **PSRAM 스프라이트 버퍼** — 화면을 오프스크린으로 그린 뒤 한 번에 전송해 깜빡임 제거.
- **부팅 시 자동 점검** — 스플래시가 떠 있는 동안 부트로더(CRC32)와 파티션 테이블을
  빠르게 검사. 통과하면 조용히 넘어가고, 문제가 있으면 경고 팝업.
- **조작 규약** — 회전=이동, **길게=선택/열기**, **클릭=뒤로**.
- **자동 부팅** — 부팅 후 1초 안에 버튼을 누르지 않으면 현재 슬롯으로 바로 부팅.

메뉴 구성:

| 메뉴 | 항목 |
|---|---|
| App0 | Boot(기본) / Hold mode |
| App1 | Boot(기본) / Hold mode |
| Firmware Update | 대상 슬롯 → 소스(Local / Network) → 파일 선택 → 플래시 |
| Erase | App0 / App1 / Factory Reset / LittleFS (클릭=체크, 길게=실행) |
| Settings | WiFi / Brightness / About |

## 요구 사항

| 항목 | 값 |
|---|---|
| 하드웨어 | ESP32-S3 **N16R8** (16MB flash + 8MB OPI PSRAM) |
| 인터페이스 | USB 케이블 (데이터 전송 가능한 것) |
| 도구 | esptool (또는 `flash-recovery.ps1`), Python 3 / uv |

## 파티션 · 오프셋

이 버전은 기존 v3.0.0 / v3.1.x 와 **동일한 파티션 테이블**을 씁니다.

| 파티션 | offset | 크기 |
|---|---|---|
| `nvs` | `0x009000` | 20K |
| `otadata` | `0x00E000` | 8K |
| `app0` | `0x010000` | 3.5M |
| `app1` | `0x390000` | 3.5M |
| `ffat` | `0x710000` | 1.3M |
| **`recovery`** | **`0x860000`** | 1.625M |
| `littlefs` | `0xA00000` | 5.8M |
| `settings` | `0xFD0000` | 64K |
| `coredump` | `0xFE0000` | 128K |

리커버리 이미지는 항상 **`0x860000`** 에 올립니다.

## 플래싱

### 0. 원본 백업 (권장)

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 921600 read-flash 0x0 ALL original-flash.bin
```

`COM7` 은 실제 포트로 바꾸세요 (Windows: 장치 관리자 → 포트).

### 방법 A — v3.0.0 / v3.1.x 에서 업그레이드 (권장)

파티션 테이블과 부트로더가 같으므로 **리커버리만 교체**하면 됩니다.
설정(NVS/settings)과 앱 슬롯은 그대로 유지됩니다.

```powershell
.\flash-recovery.ps1 -Offset 0x860000 -Image .\ats-mini-recovery-3.2.1.bin -Port COM7
```

esptool 직접 실행:

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 --before default-reset --after hard-reset write-flash 0x860000 ats-mini-recovery-3.2.1.bin
```

> 실행 중인 부트 매니저가 hwcdc(USB Serial/JTAG)로 뜨면 `--before default-reset` 로
> 바로 플래시됩니다. 부트 매니저가 뜨지 않는 상태라면 BOOT+RESET 로 ROM 모드에
> 들어간 뒤 같은 명령을 실행하세요 (아래 "ROM 다운로드 모드" 참고).

### 방법 B — 신규 설치 / v2.0.1 이하에서 올리는 경우

v2.0.1 이하는 파티션 배치가 달라 **전체 복구 플래시**가 필요합니다.
아래 4개(+앱)를 한 번에 올립니다. `appN.bin` 은 부팅할 앱 슬롯 이미지입니다.

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 write-flash \
  0x0      ats-mini-bootloader.bin \
  0x8000   ats-mini-partitions.bin \
  0xe000   boot_app0.bin \
  0x10000  app0.bin \
  0x860000 ats-mini-recovery-3.2.1.bin
```

- `bootloader.bin` (`0x0`) — recovery-first 커스텀 부트로더 (변경 없음)
- `partitions.bin` (`0x8000`) — 파티션 테이블
- `boot_app0.bin` (`0xe000`) — otadata 초기화
- `app0.bin` (`0x10000`) — 첫 앱 (없으면 빈 슬롯으로 두고 나중에 Firmware Update)
- `recovery` (`0x860000`) — 이 부트 매니저

### 방법 C — 기기에서 업데이트 (OTA)

부트 매니저 부팅 후:

- **Firmware Update → 대상 슬롯 → Network/Local → 파일 선택** 으로 앱을 플래시
- STA WiFi 연결 시 백그라운드 웹서버(`http://<IP>/`)에서 파일 업로드·플래시

> 부트 매니저 **자신**을 통째로 바꾸려면 방법 A 로 `0x860000` 에 새 이미지를 올리는 것이
> 가장 안전합니다 (실행 중인 슬롯은 OTA 로 덮어쓸 수 없습니다).

## 조작

| 동작 | 기능 |
|---|---|
| 회전 | 항목 이동 / 값 조절 |
| 길게 (0.3초~) | 선택 / 열기 / 실행 |
| 클릭 | 뒤로 |
| Erase 화면 | 클릭=체크 토글, 길게=삭제 (아무것도 안 골랐으면 뒤로) |
| 부팅 1초 | 버튼을 누르면 메뉴, 안 누르면 현재 슬롯 자동 부팅 |

## 문제 해결

### ROM 다운로드 모드

플래시가 안 되거나 포트가 안 잡히면 수동으로 ROM 모드에 들어갑니다.

1. 수신기 전원을 끕니다
2. **BOOT** 버튼을 누른 채로
3. **RESET** 버튼을 눌렀다 뗍니다
4. **BOOT** 버튼을 뗍니다
5. 다시 플래시를 시도합니다

BOOT 버튼이 없는 기종은 USB 연결 → BOOT 누른 채 전원 ON → BOOT 떼기 순서입니다.
BOOT 패드가 아예 없으면 ESP32-S3 GPIO0(핀 27)을 GND에 잠깐 단락시킵니다.

### 포트가 안 보임

- 데이터 전송이 되는 USB 케이블인지 확인 (충전 전용 케이블 불가)
- 장치 관리자 → 포트(COM & LPT)에서 COM 번호 확인 후 `-Port COM<번호>` 지정

### 부팅 시 "FLASH CHECK" 경고

부트로더 CRC 또는 파티션 테이블이 예상과 다를 때 뜹니다.
방법 B 의 전체 복구 플래시로 부트로더·파티션을 다시 올리면 해결됩니다.

## 라이선스

자세한 내용과 제3자 고지는 저장소 루트의 [NOTICE](../../NOTICE) 참고.

| 대상 | 라이선스 |
|---|---|
| 부트 매니저 소스 (스케치·파티션·flash 스크립트) | [MIT](../../LICENSE) |
| 부트로더 **바이너리** (`bootloader.bin`) | **Apache-2.0** (ESP-IDF 파생) |

- **배포 바이너리는 GPL-3.0**: `Rotary`(GPL-3.0)와 링크되므로 배포 시 해당 커밋/태그의
  소스를 함께 제공하세요.
- 빌드 방법은 [`bootloader.md`](bootloader.md) 참고.
