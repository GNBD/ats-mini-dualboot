# ATS Mini Boot Manager (theme) — v3.2.1

SI4732 (ESP32-S3) 수신기용 **부트 매니저(리커버리)** 의 테마 버전입니다.
전원을 켜면 부트 매니저가 먼저 실행되어 `app0` / `app1` 중 하나를 골라 부팅합니다.

```
전원 ON ─> 부트 매니저 (recovery) ─> app0  (펌웨어 A)
                                  └> app1  (펌웨어 B)
```

> **미완성 (work in progress)** — 아직 완성되지 않았고, 버그나 예상치 못한 문제가
> 발생할 수 있습니다. 크래시, 부팅 실패, 데이터 손실 가능성이 있으니 주의해서
> 사용하세요. 16MB(N16R8) 기기 전용이며 다른 flash 크기(N8R2 등)에 쓰지 마세요.

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

### 원본 백업 (권장)

플래시 전에 현재 flash 전체를 백업해 두세요.

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 921600 read-flash 0x0 ALL original-flash.bin
```

`COM7` 은 실제 포트로 바꾸세요 (Windows: 장치 관리자 → 포트).

### 어디에 무엇을 올리나

파일마다 정해진 주소가 있습니다. **부트 매니저만 바꾸려면 `0x860000` 한 줄만**,
처음 설치하거나 초기화하려면 아래 표를 전부 올리면 됩니다.

| 파일 | 주소 | 설명 |
|---|---|---|
| `ats-mini-bootloader.bin` | `0x0` | recovery-first 커스텀 부트로더 |
| `ats-mini-partitions.bin` | `0x8000` | 파티션 테이블 |
| `boot_app0.bin` | `0xe000` | otadata 초기화 (부팅 슬롯 초기값) |
| `app0.bin` | `0x10000` | 앱 슬롯 0 (앱 슬롯 1은 `0x390000`) |
| `ats-mini-recovery-3.2.1.bin` | `0x860000` | 부트 매니저 (이 릴리즈) |

전부 한 번에 올리기 (esptool):

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 ats-mini-bootloader.bin 0x8000 ats-mini-partitions.bin 0xe000 boot_app0.bin 0x10000 app0.bin 0x860000 ats-mini-recovery-3.2.1.bin
```

부트 매니저만 바꾸기 (v3.0.0 / v3.1.x 에서 업그레이드 — 설정·앱 슬롯 유지):

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 --before default-reset --after hard-reset write-flash 0x860000 ats-mini-recovery-3.2.1.bin
```

`flash-recovery.ps1` 를 쓰면 포트·리셋을 자동으로 처리합니다:

```powershell
.\flash-recovery.ps1 -Offset 0x860000 -Image .\ats-mini-recovery-3.2.1.bin -Port COM7
```

> 부트 매니저 자신은 실행 중인 슬롯을 덮어쓸 수 없으므로, 새 부트 매니저는 위처럼
> `0x860000` 에 올립니다.

## 앱 올리기 (app0 / app1)

앱(라디오 펌웨어)은 `app0` 또는 `app1` 슬롯에 넣습니다. 어느 슬롯에 넣든 부트
매니저에서 그 슬롯을 Boot 하면 됩니다.

**1) 부트 매니저에서 (기기에서 바로)**

Firmware Update → 대상 슬롯(App0/App1) → 소스 선택:

- **Local files** — 기기에 저장된 `.bin` 목록에서 선택
- **Network** — 등록된 URL 목록에서 다운로드

파일을 고르고 확인하면 플래시가 시작됩니다. 진행 패널에 `파일명 -> App0` 처럼
**무엇을 어디에** 쓰는지 표시됩니다.

**2) 웹서버에서**

WiFi 연결 후 브라우저로 `http://<IP>/` 접속 → `.bin` 업로드 → 슬롯(App0/App1)
지정 → Flash.

**3) esptool 로 (PC에서)**

`app0` 은 `0x10000`, `app1` 은 `0x390000` 에 씁니다.

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 write-flash 0x10000 app0.bin
```

주의:

- 슬롯당 최대 **3.5MB** 입니다.
- ESP32-S3 앱 이미지여야 합니다 (첫 바이트 `0xE9`). 부트로더·파티션까지 들어 있는
  통합(merged) 이미지를 넣어도 앱 부분만 추출해 씁니다.

## 조작

| 동작 | 기능 |
|---|---|
| 회전 | 항목 이동 / 값 조절 |
| 길게 (0.3초~) | 선택 / 열기 / 실행 |
| 클릭 | 뒤로 |
| Erase 화면 | 클릭=체크 토글, 길게=삭제 (아무것도 안 골랐으면 뒤로) |
| 부팅 1초 | 버튼을 누르면 메뉴, 안 누르면 현재 슬롯 자동 부팅 |

## 문제 해결

### 포트가 안 보임

- 데이터 전송이 되는 USB 케이블인지 확인 (충전 전용 케이블 불가)
- 장치 관리자 → 포트(COM & LPT)에서 COM 번호 확인 후 `-Port COM<번호>` 지정

### 부팅 시 "FLASH CHECK" 경고

부트로더 CRC 또는 파티션 테이블이 예상과 다를 때 뜹니다.
`0x0`(부트로더)과 `0x8000`(파티션)을 위 표의 주소로 다시 올리면 해결됩니다.

## 라이선스

자세한 내용과 제3자 고지는 저장소 루트의 [NOTICE](../../NOTICE) 참고.

| 대상 | 라이선스 |
|---|---|
| 부트 매니저 소스 (스케치·파티션·flash 스크립트) | [MIT](../../LICENSE) |
| 부트로더 **바이너리** (`bootloader.bin`) | **Apache-2.0** (ESP-IDF 파생) |

- **배포 바이너리는 GPL-3.0**: `Rotary`(GPL-3.0)와 링크되므로 배포 시 해당 커밋/태그의
  소스를 함께 제공하세요.
- 빌드 방법은 [`bootloader.md`](bootloader.md) 참고.
