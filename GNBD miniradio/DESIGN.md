# miniradio 설계 문서

> ATS Mini (ESP32-S3 + SI4732-A10) 기반 올밴드 스마트 라디오
> 상태: 설계 초안 (v0.1) · 2026-10-19

---

## 1. 목표와 범위

### 1.1 제품 목표

| 목표 | 설명 | 측정 기준 |
|---|---|---|
| 깜빡임 없는 표시 | 화면 전체 재그리기 제거, 부분 갱신 | 엔코더 연속 회전 시 화면 지움/번쩍임 0회 |
| 올밴드 호환 | FM/MW/LW/SW + 햄 8밴드 + CB, SSB | 14개 대역 전환·복원 100% |
| 풍부한 기능 | RDS, 프리셋, ATS, 타이머, 배터리 | 아래 기능 명세 충족 |
| 하드웨어 호환 | ST7789/GC9307, NS4160/AD4150B, 클론 보드 | RDDID 자동 감지 + 보드 프로파일 |
| 안정성 | 전원 시퀀스, I2C 복구, 장시간 수신 | 8시간 연속 수신 무중단 |

### 1.2 비목표 (v1 범위 밖)

- WiFi/BLE/웹 제어/OTA (부트매니저와 역할 분리)
- 녹음/재생, SD 카드 미디어 플레이어
- 터치 UI

### 1.3 대상 하드웨어

- ESP32-S3-WROOM-1 (16MB flash, 8MB OPI PSRAM)
- SI4732-A10 (I²C 0x11, 32.768kHz 크리스탈)
- ST7789 170x320 8bit 8080 병렬 (GC9307 대체 패널)
- EC11 엔코더 + 푸시 버튼, NS4160/AD4150B 앰프
- 배터리 ADC (IO4), TP4056 충전

---

## 2. 현재 코드 진단

### 2.1 깜빡임 원인

| # | 원인 | 위치 | 영향 |
|---|---|---|---|
| F1 | 튜닝 1스텝마다 헤더+주파수 카드 전체 fill 후 재그리기 | `main/ui.c:110-146` | 회전 시 잔상/번쩍임 |
| F2 | 메뉴 진입/복귀 시 `display_fill_screen()` | `main/ui.c:228,269` | 화면 전체 플래시 |
| F3 | 프레임버퍼/스프라이트 없음 (직접 패널 기록) | `components/display/display.cpp` | 부분 갱신 불가 |
| F4 | PSRAM 비활성 | `sdkconfig.defaults:7-9` | 스프라이트 메모리 부족 |
| F5 | 백라이트 LEDC 5kHz | `display.cpp:224` | 저밝기 PWM 플리커 |

### 2.2 기능/정합성 결함

| # | 결함 | 위치 | 비고 |
|---|---|---|---|
| B1 | STEP 메뉴 값이 튜닝에 미적용 (`band_step_freq` 고정 스텝) | `main/main.c:639` | 사용자 설정 무효 |
| B2 | SW 대역(2.3–26.1MHz)이 햄 대역과 중복 → 재부팅 시 햄이 SW AM으로 복원 | `band.c:15,45` | SSB 설정 소실 |
| B3 | 튜닝마다 모드/대역폭/SSB 재적용 | `main.c:253-314` | 지연, 오디오 끊김 |
| B4 | MW 9kHz 간격 미지원 | `band.c`, seek spacing | 지역 방송 검색 누락 |
| B5 | RDS/스테레오/배터리 표시 없음 | `si4732.c`, `ui.c` | 기능 부족 |
| B6 | 빌드 설정 불일치 (2MB/160MHz/PSRAM off) | `sdkconfig` | 성능·메모리 제약 |
| B7 | I2C 오류 복구(버스 리셋) 없음 | `si4732.c` | 장시간 수신 취약 |

---

## 3. 시스템 아키텍처

### 3.1 컴포넌트 구조

```
main/
  main.c          앱 상태머신, 입력/저장/전원 시퀀스
  ui.c/.h         화면 렌더링 (스프라이트 기반)
components/
  si4732/         SI4732 드라이버 (FM/AM/SSB/RDS/RSQ)
  band/           대역 테이블, 스텝/모드 헬퍼
  display/        LovyanGFX 래퍼, 스프라이트, 백라이트
  encoder/        EC11 PCNT
  audio/          볼륨/뮤트
  storage/        NVS 설정/프리셋
```

### 3.2 작업/실행 모델

```
Core 0: main task (20ms 루프) ── 입력/UI/저장/RSQ
         └ render task (선택, 30fps 상한) ── dirty rect flush
Core 1: (예약) RDS 파서 / ATS 스캔 워커
```

- **단일 루프 유지**를 기본으로 하되, ATS 스캔·RDS 파싱은 별도 태스크로 분리.
- I2C 버스는 뮤텍스로 보호 (메인 루프 + 스캔 워커 공유).
- WDT: main task 등록, 스캔 워커는 1초 내 reset.

### 3.3 상태머신

```
BOOT → SPLASH → TUNE ⇄ VOL
                  ⇅
                MENU ⇄ EDIT
                  ↓
              SEEK / SCAN / PRESET
```

- `TUNE`: 엔코더=주파수, 숏=메뉴, 롱=볼륨
- `VOL`: 엔코더=볼륨, 숏=메뉴, 롱=복귀
- `MENU`: 엔코더=이동, 숏=편집/확인, 롱=뒤로
- `SEEK/SCAN`: 엔코더=취소, 버튼=취소

---

## 4. 표시 파이프라인 (깜빡임 제거)

### 4.1 렌더링 방식

1. **PSRAM 활성화**: `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`, 80MHz.
2. **LGFX_Sprite 2중 버퍼**: 320x170 RGB565 ≈ 108KB/장 → PSRAM 2장.
3. **dirty rect**: 위젯이 변경 시 자신의 사각형만 union.
4. **flush**: dirty rect 영역만 `pushSprite()` (DMA).
5. **텍스트 캐시**: 주파수 문자열이 동일하면 재그리기 생략.

```
ui_begin_frame()  → sprite clear(필요 영역만)
ui_widget_*()     → sprite에 그리기 + dirty union
ui_end_frame()    → dirty rect만 pushSprite (없으면 skip)
```

### 4.2 위젯 레이아웃 (320x170)

| 영역 | Y | 높이 | 갱신 주기 |
|---|---|---|---|
| 헤더 (밴드/모드/배터리) | 0–23 | 24 | 대역/모드 변경 시 |
| 주파수 카드 | 26–74 | 48 | 튜닝 시 (숫자 영역만) |
| S미터 | 76–105 | 30 | 500ms |
| 상태 카드 (VOL/STEP/RDS) | 106–146 | 40 | 값 변경 시 |
| 힌트바 | 147–170 | 23 | 이벤트 시 |

- 주파수 카드: 고정폭 숫자 폰트(등폭)로 자릿수 흔들림 방지.
- RDS 표시: 상태 카드 2행 또는 별도 스크롤 라인(우선순위 낮음).

### 4.3 백라이트

- LEDC 20kHz, 8bit → 가청/가시 플리커 제거.
- 밝기 0%에서도 완전 OFF가 아닌 1% 유지 옵션(취침 모드 제외).

---

## 5. 튜닝/오디오 파이프라인

### 5.1 입력 코얼레싱

```
엔코더 이벤트 → pending_step 누적
20ms 루프에서 40ms 경과 & 정지 시 1회 tune
연속 회전 시 최대 25Hz tune (40ms 간격)
```

- **모드/대역폭은 대역·모드 변경 시에만 적용**, 주파수 변경은 `TUNE_FREQ`만.
- SSB: BFO만 갱신, 패치/모드는 밴드 진입 시 1회.
- 시크 중에는 tune 금지, STC 폴링 100ms.

### 5.2 스텝 적용

```c
uint32_t step = effective_step(mode_slot);   // 사용자 STEP 우선
freq = band_step_freq(band, freq, steps, step);
```

- `band_step_freq()`에 step 인자 추가 (기본값 = 대역 step).
- FM 100/50kHz, AM 1/5/9/10kHz, SSB 1/5kHz 유지.

### 5.3 밴드 정합성

- `band_index_for_freq()`는 **구체 밴드 우선** (햄 > SW).
- 저장 시 `band_index`를 신뢰하되, 주파수가 범위를 벗어나면 재탐색.
- SW는 햄 대역을 제외한 세그먼트로 분할하거나, 복원 시 저장된 mode를 보존.

### 5.4 오디오

- 부팅: RADIO_EN → 50ms → I2C/SI4732 → TUNE 완료 → AMP_EN → MUTE 해제.
- 종료/대역 전환: MUTE → AMP off → (필요 시) POWER_DOWN.
- 볼륨: SI4732 RX_VOLUME 0–63, 저장 디바운스 2s.

---

## 6. 기능 명세

### 6.1 수신

| 기능 | 상세 | 우선순위 |
|---|---|---|
| FM | 64–108MHz, 10/50kHz, 스테레오, 디엠퍼시스 50/75µs | P0 |
| RDS | PS(8자), RT(64자), PTY, CT, AF, 신호 상태 | P1 |
| MW | 520–1710kHz, 9/10kHz, AGC/소프트뮤트 | P0 |
| LW | 153–279kHz | P0 |
| SW | 2.3–26.1MHz, 1/5kHz | P0 |
| SSB | LSB/USB, BFO ±5kHz, 대역폭 0.5–4kHz | P0 |
| 햄 | 160/80/40/30/20/17/15/12/10m + CB | P0 |
| 스켈치 | RSSI/SNR 임계값 | P2 |
| 노이즈 블랭커 | AM NB 프로퍼티 | P2 |

### 6.2 편의 기능

| 기능 | 상세 | 우선순위 |
|---|---|---|
| 프리셋 | 대역별 10개, NVS 저장, 단축 전환 | P1 |
| ATS | FM/AM 자동 스캔 후 저장 | P1 |
| 시계 | NTP 없음 → 수동 설정 + RDS CT | P2 |
| 타이머 | 취침(30/60/90분), 자동 꺼짐 | P1 |
| 배터리 | ADC %, 충전 표시, 저전압 경고 | P1 |
| 밝기/테마 | 0–100%, 자동 감광(선택) | P1 |
| 공장초기화 | 메뉴 5초 롱프레스 | P1 |
| 부팅 스플래시 | 로고 + 버전 | P2 |

### 6.3 메뉴 구조 (v1)

```
BAND      대역 선택
MODE      AM/LSB/USB
BW        대역폭
STEP      스텝
SEEK      자동 탐색
PRESET    프리셋 목록/저장
ATS       자동 스캔
RDS       RDS 표시 on/off
BRIGHT    밝기
BFO       SSB 미세 조정
TIMER     취침/꺼짐
BATT      배터리 표시
RESET     공장 초기화
EXIT
```

- 8행 화면 → 스크롤 메뉴(윈도 7행 + 스크롤 인디케이터).

---

## 7. 드라이버 확장

### 7.1 SI4732 API 추가

```c
/* RDS */
esp_err_t si4732_rds_enable(si4732_t *dev, bool enable);
esp_err_t si4732_rds_status(si4732_t *dev, si4732_rds_t *out);
esp_err_t si4732_rds_ps(si4732_t *dev, char ps[9]);
esp_err_t si4732_rds_rt(si4732_t *dev, char rt[65]);

/* FM/AM 제어 */
esp_err_t si4732_set_deemphasis(si4732_t *dev, bool fm_75us);
esp_err_t si4732_set_stereo(si4732_t *dev, bool stereo);
esp_err_t si4732_set_soft_mute(si4732_t *dev, uint8_t max_atten_db);
esp_err_t si4732_set_agc(si4732_t *dev, bool enable);

/* 진단/복구 */
esp_err_t si4732_bus_reset(si4732_t *dev);
esp_err_t si4732_get_status(si4732_t *dev, uint8_t *status);
```

### 7.2 RDS 파서

- 블록 동기(B) → 그룹 타입 → 0A/2A(PS/RT), 4A(CT), 14A(EON).
- 오류 정정: 신뢰도 높은 블록만 반영, 2회 연속 일치 시 확정.
- 태스크: 100ms 폴링, 수신 없으면 2초 후 표시 클리어.

### 7.3 배터리

- ADC1_CH3(IO4) 12bit, 8회 평균, 분압비 보정 상수.
- 리튬 3.0–4.2V → 0–100% 선형 테이블 + 히스테리시스.
- 3.3V 이하: 경고, 3.0V 이하: 자동 종료(딥슬립).

---

## 8. 저장 구조

```c
#define STORAGE_MAGIC_V2 0x4D523032  /* 'MR02' */

typedef struct {
    uint32_t magic;
    uint32_t freq_hz;
    uint8_t  band_index;
    uint8_t  mode;
    uint8_t  volume;
    uint8_t  brightness;
    uint8_t  flags;          /* bit0 RDS, bit1 stereo, ... */
    int16_t  bfo_hz;
    uint8_t  bw[3];
    int8_t   step_idx[3];
    uint8_t  deemphasis;     /* 0=50us, 1=75us */
    uint8_t  sleep_min;      /* 0=off */
    uint8_t  reserved[8];
} storage_settings_v2_t;

typedef struct {
    uint8_t  count;
    struct { uint32_t freq_hz; uint8_t band; uint8_t mode; } slot[10];
} preset_bank_t;   /* NVS key: preset_fm / preset_am */
```

- 마이그레이션: v1 로드 → v2 기본값으로 매핑 후 저장.
- 쓰기 디바운스 2s, 전원 종료 직전 flush.

---

## 9. 전원/안정성

### 9.1 부팅 시퀀스

```
1. GPIO 안전 상태 (AMP off, MUTE on, RADIO_EN off)
2. NVS init/load
3. PSRAM init 확인 (실패 시 스프라이트 축소 모드)
4. LCD init (RDDID) + 백라이트 0%
5. RADIO_EN on → 50ms → I2C → SI4732 POWER_UP
6. 대역/모드 복원 → TUNE → STC 대기
7. AMP on → MUTE off → 백라이트 fade-in
8. UI 첫 프레임
```

### 9.2 오류 처리

| 오류 | 대응 |
|---|---|
| I2C NACK/타임아웃 | 버스 리셋 1회 → SI4732 재초기화 → 실패 시 RADIO ERR UI |
| CTS 타임아웃 | 명령 재시도 1회, 100ms 지연 |
| PSRAM 없음 | 스프라이트 1장(또는 라인 버퍼) 폴백 |
| LCD ID 미상 | ST7789 기본 프로파일 + 경고 로그 |
| WDT | main 루프 reset, 스캔 워커는 청크 단위 |

### 9.3 전원 관리

- 딥슬립: 타이머 만료/저전압, 엔코더 버튼 wake.
- 무음 시 앰프 off로 대기 전류 절감(옵션).
- 충전 중 배터리 아이콘 애니메이션.

---

## 10. 빌드/배포

### 10.1 sdkconfig 목표

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

### 10.2 파티션 (16MB)

```
nvs       0x9000   0x5000
otadata   0xE000   0x2000
app0      0x10000  0x300000
app1      0x310000 0x300000
storage   0x610000 0x10000
coredump  0x620000 0x10000
```

> 기존 ATS Mini 부트매니저 파티션과 충돌하지 않도록 app0 오프셋(0x10000) 유지.
> 복구 펌웨어를 쓰지 않는 단독 배포 시 위 단순 테이블 사용.

### 10.3 검증

- `idf.py build`, `idf.py -p COMx flash monitor`
- 정적 분석: `clang-tidy` (가능 시), 경고 `-Werror` 옵션 검토
- 하드웨어 매트릭스: ST7789 원본/하이감마, GC9307, NS4160/AD4150B

---

## 11. 테스트 계획

| 구분 | 항목 | 방법 |
|---|---|---|
| 표시 | 깜빡임 없음 | 고속 회전 10초, 240fps 촬영 |
| 표시 | 부분 갱신 정확성 | 위젯별 스크린샷 비교 |
| 튜닝 | 14대역 전환/복원 | 자동 시나리오 + 재부팅 |
| 튜닝 | STEP 적용 | 각 모드별 스텝 순환 확인 |
| SSB | 패치/BFO | 햄 밴드 수신, BFO ±5kHz |
| RDS | PS/RT/PTY/CT | 지역 FM 방송 30분 |
| 안정성 | 8시간 연속 | 로그/메모리/온도 모니터 |
| 전원 | 부팅/종료 팝 | 오실로스코프, 청취 |
| 복구 | I2C 단선/재연결 | 런타임 오류 주입 |

---

## 12. 마일스톤

| 단계 | 내용 | 완료 기준 |
|---|---|---|
| M1 기반 | PSRAM/스프라이트/240MHz, 튜닝 코얼레싱, B1/B2/B7 수정 | 깜빡임 0, 스텝 적용, 8시간 안정 |
| M2 올밴드 | RDS, 스테레오, 디엠퍼시스, MW 9kHz, AGC | FM RDS 표시, 지역 방송 |
| M3 편의 | 프리셋, ATS, 타이머, 배터리, 메뉴 스크롤 | 전 기능 메뉴 동작 |
| M4 마감 | 테마/스플래시, 문서, 복구 가이드, 릴리스 | 하드웨어 매트릭스 통과 |

---

## 13. 오픈 이슈

1. **SW/햄 중복 정책**: SW를 햄 제외 세그먼트로 나눌지, 저장 mode를 우선할지 결정 필요.
2. **RDS 표시 위치**: 상태 카드 vs 별도 라인(가독성/깜빡임 trade-off).
3. **부트매니저 연동**: recovery 파티션 유지 여부와 OTA 슬롯 사용 정책.
4. **클론 보드 핀맵**: 보드 프로파일 테이블을 NVS로 선택할지 빌드 타임으로 둘지.
5. **PSRAM 실패 폴백**: 스프라이트 1장 vs 라인 버퍼(깜빡임 최소화 수준).
