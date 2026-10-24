# GNBD miniradio

<img width="320" height="170" alt="miniradio" src="https://github.com/user-attachments/assets/6efc7f51-b688-4274-865f-c736e9fb025a" />

https://gnbdatsmini.netlify.app/

GNBD miniradio의 **Dual Boot + Theme Version** 펌웨어 소스입니다.

- Product: **GNBD miniradio**
- Build: **Dual Boot + Theme Version**
- Firmware version: **1.0.14**
- Target: **ESP32-S3 N16R8 + SI4732**
- Flash layout: ATS Mini Dualboot 16MB (`app0` / `app1` / `recovery`)

`main/`은 ESP-IDF 애플리케이션 컴포넌트 이름입니다. 빌드 경로와 CMake 구성이
`main`을 기준으로 잡혀 있으므로 폴더명을 바꾸지 마세요.

## Build

ESP-IDF v5.5.x 기준입니다.

```sh
idf.py set-target esp32s3
idf.py build
```

앱 영역만 갱신하려면:

```sh
esptool.py --chip esp32s3 -p COM7 write_flash 0x10000 build/miniradio.bin
```

## Layout

- `main/` — 애플리케이션 소스
- `components/` — 디스플레이, SI4732, 저장소, 인코더 등
- `partitions.csv` — 16MB dual-boot 파티션
- `sdkconfig.defaults` — 기본 빌드 설정

## License

이 폴더의 프로젝트 코드는 MIT License입니다. 자세한 내용은 [LICENSE](LICENSE)를
참고하세요.

서드파티 고지는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)와 각
컴포넌트의 라이선스 파일을 따릅니다.
