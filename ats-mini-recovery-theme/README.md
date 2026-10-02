# ATS Mini Boot Manager (theme) — v4.1.0 (DES)

SI4732 (ESP32-S3) 수신기용 **부트 매니저(리커버리)** 의 테마 버전입니다.
전원을 켜면 부트 매니저가 먼저 실행되어 `app0` / `app1` 중 하나를 골라 부팅합니다.

```
전원 ON ─> 부트 매니저 (recovery) ─> app0  (펌웨어 A)
                                  └> app1  (펌웨어 B)
```

> **미완성 (work in progress) — 테스트 필요** — 아직 완성되지 않았고, 버그나 예상치
> 못한 문제가 발생할 수 있습니다. 크래시, 부팅 실패, 데이터 손실 가능성이 있으니
> 주의해서 사용하세요.
>
> - **기존 버전(v3.x 이하)과 호환되지 않습니다.** 파티션 테이블과 설정 영역이 모두
>   바뀌므로, 이 버전을 올리면 **기기 내부 저장소가 초기화될 수 있습니다.**
> - `app0` 과 `app1` 은 **서로 다른 저장소(`settings`)를 사용**합니다. 슬롯마다
>   WiFi 등 설정을 따로 입력해야 합니다.
> - 16MB(N16R8) 기기 전용이며 다른 flash 크기(N8R2 등)에 쓰지 마세요.

## 이 버전의 특징

- **타일 UI** — 메인 메뉴 / Settings / WiFi / 부팅 모드 / 펌웨어 대상·소스 / Erase 가
  모두 타일 그리드. WiFi 스캔·파일 목록·URL 선택은 행(row) 레이아웃.
- **PSRAM 스프라이트 버퍼** — 화면을 오프스크린으로 그린 뒤 한 번에 전송해 깜빡임 제거.
- **부팅 시 자동 점검** — 스플래시가 떠 있는 동안 부트로더(CRC32)와 파티션 테이블을
  빠르게 검사. 통과하면 조용히 넘어가고, 문제가 있으면 경고 팝업.
- **조작 규약** — 회전=이동, **길게=선택/열기**, **클릭=뒤로**.
- **자동 부팅** — 부팅 후 1초 안에 버튼을 누르지 않으면 마지막으로 부팅한 슬롯으로 바로
  부팅합니다. 마지막 슬롯은 리커버리 설정(`rec_settings`)에 저장되어 전원을 껐다 켜도
  유지됩니다.
- **설정 격리 (DES)** — app0 / app1 은 코드에서 같은 `settings` 이름을 찾지만, 활성화된
  파티션 테이블에 따라 물리 영역(`0xF9D000` / `0xFBD000`)이 달라져 **두 앱의 설정이
  섞이지 않습니다**.
- **Partition** — `Info`(파티션·사용량), `Repair`(정본 테이블 복구), `Resize`(app0/app1
  경계를 0.5M 단위, 각 1.0~6.0M 로 이동, **양 슬롯이 비었을 때만**).
- **설정 백업 (실험적 · 일부 펌웨어 미지원)** — `Settings > Backup` 에서 슬롯 설정
  (`settings`)을 `.nvs` 파일로 내보내고(LittleFS) 다시 불러옵니다. 앱이 설정을 다른 곳에
  저장하면 복원이 모든 설정을 되돌리지 못할 수 있습니다.
- **파일 관리 · 웹 계정** — `Settings > LittleFS` 에서 파일 이름변경/삭제, 웹(Network 탭)의
  계정(ID/PW)으로 웹 UI 를 보호.

메뉴 구성:

| 메뉴 | 항목 |
|---|---|
| App0 | Boot(기본) / Hold mode |
| App1 | Boot(기본) / Hold mode |
| Firmware Update | 대상 슬롯 → 소스(Local / Network) → 파일 선택 → 플래시 |
| Erase | 2단계: `Factory Reset`(App0 cfg / App1 cfg / Recovery) 또는 `Erase`(App0 / App1 / LittleFS). 클릭=체크, 길게=실행, 아무것도 안 고르고 길게=뒤로 |
| Partition | `Info` / `Resize` / `Repair`. Resize 는 app0/app1 이 **비어 있을 때만** |
| Settings | Network / Brightness / LittleFS / Backup / About |

## 요구 사항

| 항목 | 값 |
|---|---|
| 하드웨어 | ESP32-S3 **N16R8** (16MB flash + 8MB OPI PSRAM) |
| 인터페이스 | USB 케이블 (데이터 전송 가능한 것) |
| 도구 | esptool (또는 `flash-recovery.ps1`), Python 3 / uv |

## 파티션 · 오프셋

v4.1.0 은 **DES(Dual Environment System)** 파티션 테이블을 씁니다. `nvs` ~ `recovery`
까지는 기존 v3.x 와 주소가 같고, `littlefs` 가 6MB → 5.58MB 로 줄면서 꼬리 영역에
새 파티션이 들어갑니다.

| 파티션 | offset | 크기 | 비고 |
|---|---|---|---|
| **Table A** | `0x008000` | 4K | 파티션 테이블 (canonical) |
| `nvs` | `0x009000` | 20K | WiFi/BT (v3.x 동일) |
| `otadata` | `0x00E000` | 8K | v3.x 동일 |
| `app0` | `0x010000` | 3.5M | v3.x 동일 |
| `app1` | `0x390000` | 3.5M | v3.x 동일 |
| `ffat` | `0x710000` | 1.3M | v3.x 동일 |
| **`recovery`** | **`0x860000`** | 1.625M | 부트 매니저 |
| `littlefs` | `0xA00000` | 5.58M | 스플래시 (v3.x 는 6MB) |
| `rec_settings` | `0xF95000` | 32K | 리커버리 전용 NVS |
| `settings` | `0xF9D000` | 128K | **ENV0 전용** |
| `settings` (ENV1) | `0xFBD000` | 128K | Table B 가 가리키는 영역 |
| `des_meta_a` / `b` | `0xFDD000` / `0xFDE000` | 4K ×2 | raw (파티션 미등록) |
| `coredump` | `0xFDF000` | 128K | |
| **Table B** | `0xFFF000` | 4K | ENV1 백업 테이블 |

두 테이블의 차이는 **`settings` 오프셋 한 줄**뿐입니다. app 은 언제나 같은 `settings`
이름을 찾으므로, 어느 테이블이 활성화됐느냐에 따라 서로 다른 128KB NVS 를 쓰게 됩니다.

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
| `ats-mini-recovery-4.1.0.bin` | `0x860000` | 부트 매니저 (이 릴리즈) |

전부 한 번에 올리기 (esptool):

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 ats-mini-bootloader.bin 0x8000 ats-mini-partitions.bin 0xe000 boot_app0.bin 0x10000 app0.bin 0x860000 ats-mini-recovery-4.1.0.bin
```

부트 매니저만 바꾸기 (v3.0.0 / v3.1.x / v3.2.1 에서 업그레이드 — 설정·앱 슬롯 유지):

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 --before default-reset --after hard-reset write-flash 0x860000 ats-mini-recovery-4.1.0.bin
```

`flash-recovery.ps1` 를 쓰면 포트·리셋을 자동으로 처리합니다:

```powershell
.\flash-recovery.ps1 -Offset 0x860000 -Image .\ats-mini-recovery-4.1.0.bin -Port COM7
```

> 부트 매니저 자신은 실행 중인 슬롯을 덮어쓸 수 없으므로, 새 부트 매니저는 위처럼
> `0x860000` 에 올립니다.

### v3.2.1 → v4.x 업그레이드

파티션 테이블이 바뀌므로 **리커버리만 올리면 안 됩니다**. 새 테이블을 `0x8000` 에,
ENV1 백업 테이블을 `0xFFF000` 에 올리고 리커버리를 `0x860000` 에 올립니다.

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 --before default-reset --after hard-reset write-flash 0x8000 ats-mini-partitions.bin 0xFFF000 ats-mini-partitions.bin 0x860000 ats-mini-recovery-4.1.0.bin
```

`littlefs` 는 **첫 부팅에 리커버리가 알아서 처리**합니다. 줄어든 파티션보다 큰
옛 슈퍼블록을 읽고 `LITTLEFS / Old format found` 를 띄운 뒤(확인 버튼 하나뿐),
약 20초 동안 영역을 지우고 부팅을 계속합니다. 이게 없으면 앱이 `lfs_fs_grow_`
assert 로 재부팅 루프에 빠집니다. 먼저 직접 비우면 팝업 없이 바로 넘어갑니다.

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 erase-region 0xA00000 0x595000
```

`littlefs` 를 비우면 내장메모리가 초기화됩니다. 이어서 `settings`(0xF9D000) 와
`rec_settings`(0xF95000) 도 같은 옛 `littlefs` 위에 얹혀 있으므로, init이 거부될 때만
원인을 띄운 뒤 확인 버튼으로 그 영역을 비웁니다(정상 영역은 건드리지 않습니다).
기존 `settings`(0xFD0000) 데이터는 새 영역으로 자동 이전되지 않으므로 WiFi 등 설정을
다시 입력하세요.

## 앱 올리기 (app0 / app1)

앱(라디오 펌웨어)은 `app0` 또는 `app1` 슬롯에 넣습니다. 어느 슬롯에 넣든 부트
매니저에서 그 슬롯을 Boot 하면 됩니다.

**1) 부트 매니저에서 (기기에서 바로)**

Firmware Update → 대상 슬롯(App0/App1) → 소스 선택:

- **Local files** — 기기에 저장된 `.bin` 목록에서 선택
- **Network** — 등록된 URL 목록에서 다운로드

파일을 고르고 확인하면 플래시가 시작됩니다. 진행 패널에 `파일명 -> App0` 처럼
**무엇을 어디에** 쓰는지 표시됩니다.

**2) 웹 업로더로 (esptool 없이 · 권장)**

1. 기기에서 엔코더를 **길게 눌러 메뉴**로 들어갑니다. 웹서버가 켜지면 메뉴 하단 오른쪽에
   **웹 주소(IP)** 가 표시됩니다 (예: `192.168.0.13`, AP 모드면 `192.168.4.1`).
2. 폰/PC 를 같은 WiFi 에 연결합니다. WiFi 가 없으면 기기의 AP `ats-recovery` 에 접속
   (비밀번호 `12345678`)합니다.
3. 브라우저에서 표시된 주소를 엽니다: `http://192.168.0.13/`
4. **Firmware** 탭 → 파일 선택(`.bin`) → 대상 선택 → **Upload**:
   - `Upload & flash to App0` / `App1` — 업로드 후 그 슬롯으로 플래시하고 재부팅
   - `Save to storage` — 플래시 없이 `.bin`/`.txt`/`.nvs` 를 내장메모리에 저장
5. **진행 모달**이 뜨고 100%가 되면 자동으로 그 슬롯으로 재부팅됩니다.

- 웹 계정(Network 탭 → **Web account**)을 켜 두면 접속 시 **ID/PW** 를 묻습니다.
- Erase(초기화)는 **웹에서 할 수 없습니다** — LCD 에서만 가능합니다.

**3) esptool 로 (PC에서)**

`app0` 은 `0x10000`, `app1` 은 `0x390000` 에 씁니다.

```
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 write-flash 0x10000 app0.bin
```

주의:

- 슬롯당 최대 **3.5MB** 입니다.
- **ESP32-S3 OTA 이미지(앱 전용 바이너리)만** 넣을 수 있습니다. 부트로더·파티션
  테이블이 함께 들어 있는 통합(merged) 이미지나 전체 flash 덤프는 넣지 마세요.

## 조작

| 동작 | 기능 |
|---|---|
| 회전 | 항목 이동 / 값 조절 |
| 길게 (0.3초~) | 선택 / 열기 / 실행 |
| 클릭 | 뒤로 |
| Erase 화면 | 모드 선택(길게=열기, 클릭=뒤로) → 목록에서 클릭=체크 토글, 길게=삭제, 아무것도 안 고르고 길게=뒤로 |
| 부팅 1초 | 버튼을 누르면 메뉴, 안 누르면 현재 슬롯 자동 부팅 |

## 문제 해결

### 포트가 안 보임

- 데이터 전송이 되는 USB 케이블인지 확인 (충전 전용 케이블 불가)
- 장치 관리자 → 포트(COM & LPT)에서 COM 번호 확인 후 `-Port COM<번호>` 지정

### 부팅 시 "FLASH CHECK" 경고

매 부팅 스플래시에서 부트로더 CRC와 파티션 테이블을 1초 안에 검사합니다.

- **Bootloader: MISMATCH** — 부트로더(`0x0`)가 다르면 경고만 합니다. 위 표의
  주소로 다시 올리세요.
- **Partitions: WRONG** — 파티션 테이블이 다르면 내장된 정본으로 `0x8000`을
  재구성한 뒤 자동 재시작합니다. `CLICK=SKIP`으로 건너뛸 수 있습니다.
- **LITTLEFS / Old format found** — 옛 크기로 포맷된 `littlefs`가 남아 있으면
  뜹니다. 확인하면 영역을 비우고(약 20초) 계속 부팅하고, 거절은 할 수 없습니다.
  내장메모리가 초기화됩니다.
- **REC SETTINGS / SETTINGS** — v4.0.0 에 추가된 두 NVS 영역이 옛 `littlefs`
  위에 얹혀 있으면(업그레이드 시) init이 거부됩니다. 이 경우 `No free pages` 등
  원인을 띄운 뒤 확인 버튼 하나로 그 영역만 지우고 다시 만듭니다. 정상적으로
  열리는 영역은 건드리지 않으므로 설정이 보존됩니다.

## 라이선스

자세한 내용과 제3자 고지는 저장소 루트의 [NOTICE](../../NOTICE) 참고.

| 대상 | 라이선스 |
|---|---|
| 부트 매니저 소스 (스케치·파티션·flash 스크립트) | [MIT](../../LICENSE) |
| 부트로더 **바이너리** (`bootloader.bin`) | **Apache-2.0** (ESP-IDF 파생) |

- **배포 바이너리는 GPL-3.0**: `Rotary`(GPL-3.0)와 링크되므로 배포 시 해당 커밋/태그의
  소스를 함께 제공하세요.
- 빌드 방법은 [`bootloader.md`](bootloader.md) 참고.
