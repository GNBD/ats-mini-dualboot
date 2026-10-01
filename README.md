# ATS Mini Dualboot

**미완성 (work in progress)** — 크래시, 부팅 실패, 데이터 손실이 발생할 수 있습니다.
[English](README.en.md)

«Important Notice:
This software was originally designed with switching between the HJB firmware and the standard firmware in mind. It was not designed to handle configuration conflicts or shared settings between custom firmwares based on the same firmware source. As a result, settings or other stored data may overlap between such firmwares.»

SI4732 (ESP32-S3) 수신기용 **부트 매니저** 저장소입니다. 전원을 켜면 부트 매니저가
먼저 실행되어 `app0` / `app1` 중 무엇을 부팅할지 고를 수 있습니다.

```
전원 ON ─> 부트 매니저 (recovery) ─> app0  (펌웨어 A)
                                  └> app1  (펌웨어 B)
```

<img width="657" height="350" alt="Boot manager" src="https://github.com/user-attachments/assets/a04bad85-3edb-43bd-9c66-b7643d7b9e33" />

## 한눈에 보기

| 구성 | 스케치 | 대상 하드웨어 | recovery | 웹/OTA | 상태 |
|---|---|---|---|---|---|
| 부트 매니저 | `ats-mini-recovery-beta` | N16R8 (16MB) | `0x860000` | O | 테스트됨 |
| 부트 매니저 | `ats-mini-recovery-lite` | N8R2 (8MB) | `0x660000` | X | **미검증** |

핵심 규칙 세 가지:

1. **16MB 테이블과 8MB 테이블을 절대 섞지 마세요.** 오프셋이 어긋나 부팅되지 않습니다.
2. 부트 매니저 **바이너리**와 **파티션 테이블**, **커스텀 부트로더**는 세트로 맞춰야 합니다.
3. `lite`는 **내부에서 업데이트할 수 없습니다** — 항상 외부에서 전체를 다시 플래시합니다.

## 부트 매니저 변종

### `ats-mini-recovery-beta` — 16MB 기본 (v3.1.0)

현재 16MB(N16R8) 기기의 기본 부트 매니저.

- 메뉴: Boot App0 / Boot App1 / Firmware Update / WiFi / Erase / About
- STA WiFi 연결 시 백그라운드 웹서버로 파일 업로드·펌웨어 업데이트, 미연결 시 AP
- 파티션: [`ats-mini-recovery-beta/partitions.csv`](ats-mini-recovery-beta/partitions.csv)

### `ats-mini-recovery-lite` — N8R2 8MB (v1.0.0)

8MB 기기용 최소 부트 매니저. 웹서버·WiFi·QR이 없어 가볍습니다.

- 메뉴: Boot App0 / Boot App1 / Factory Reset / About
- 파티션: [`ats-mini-recovery-lite/partitions.csv`](ats-mini-recovery-lite/partitions.csv)
- 설치: [`flash-lite.ps1`](ats-mini-recovery-lite/flash-lite.ps1) → `.\flash-lite.ps1 -Install -Target 8MB -Port COM7`
- **내부 업데이트 불가**: 웹/OTA가 없어 펌웨어+앱을 통째로 외부 플래시해야 합니다.
- **미검증**: N8R2 실기기에서 아직 테스트하지 않았습니다. 릴리즈 노트 참고.

## 하드웨어

| 대상 | 필요 하드웨어 | 비고 |
|---|---|---|
| `ats-mini-recovery-beta` | ESP32-S3 **N16R8** (16MB flash + 8MB PSRAM) | 다른 버전(N8R2, N8R8, N16R2 등) 미검증 |
| `ats-mini-recovery-lite` | ESP32-S3 **N8R2** (8MB flash + 2MB PSRAM) | 실기기 미검증 |

## 파티션 · 부트로더

| flash | 파티션 테이블 | recovery | 커스텀 부트로더 |
|---|---|---|---|
| 16MB | [`beta`](ats-mini-recovery-beta/partitions.csv) | `0x860000` | [`beta/bootloader.bin`](ats-mini-recovery-beta/bootloader.bin) |
| 8MB | [`lite`](ats-mini-recovery-lite/partitions.csv) | `0x660000` | [`lite/bootloader.bin`](ats-mini-recovery-lite/bootloader.bin) |

부트 매니저가 항상 먼저 실행되도록 **recovery-first 커스텀 부트로더**를 사용합니다.
ESP-IDF v5.5.5에서 각 flash 크기에 맞게 빌드하며, 빌드 방법은
[`ats-mini-recovery-beta/bootloader.md`](ats-mini-recovery-beta/bootloader.md)에 있습니다.

**동작 원리:** 부트 매니저는 `esp_ota_set_boot_partition()`로 대상을 `ESP_OTA_IMG_NEW`로
지정해 **one-shot** 부팅을 시킵니다. 대상이 스스로 확인하거나(`esp_ota_mark_app_valid_cancel_rollback()`)
실패하면 다음 부팅은 다시 부트 매니저로 돌아옵니다.

## 다운로드

- Releases: <https://github.com/GNBD/ats-mini-dualboot/releases> (`v1.0.0-lite` = N8R2 프리릴리즈, 미검증)

## 라이선스

하나가 아니라 여러 라이선스가 함께 적용됩니다. 자세한 내용과 제3자 고지는 [NOTICE](NOTICE) 참고.

| 대상 | 라이선스 |
|---|---|
| 부트 매니저 소스 (스케치·파티션·flash 스크립트) | [MIT](LICENSE) |
| 부트로더 **바이너리** (`bootloader.bin`) | **Apache-2.0** (ESP-IDF 파생) |
| 앱 이미지 (`.bin`) | **GPL-3.0** 적용 (`Rotary` 포함) |
| Arduino-ESP32 코어·외부 라이브러리 | 원본 라이선스 유지 (LGPL-2.1 등) |

주의할 점:

- **앱 이미지(.bin)는 GPL-3.0**: `Rotary`(GPL-3.0)와 링크되므로 해당 이미지는 GPL-3.0을 따릅니다.
  배포 시 해당 커밋/태그의 소스를 함께 제공하세요.
- **OTA도 똑같이 배포입니다**: OTA로 기기에 보내는 `.bin`도 GitHub Release 내려받기와 동일한
  의무가 적용되며, OTA가 별도 라이선스인 것은 아닙니다.
- **제3자 구성요소**: LovyanGFX(BSD-2), LittleFS(Apache-2.0), ESP-IDF(Apache-2.0), 코어(LGPL-2.1) 등은
  각각 원본 라이선스를 따릅니다. 라이선스 전문은 [LICENSES/](LICENSES/)에 있습니다
  (MIT, Apache-2.0, GPL-3.0, LGPL-2.1).
- **하드웨어/이미지**: CC BY-NC-SA 3.0(비상업) 가능성.
- 이 프로젝트는 원본 프로젝트·하드웨어 저작자의 공식 제품이 아닙니다.
