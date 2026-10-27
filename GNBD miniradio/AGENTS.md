응답 언어 규칙



모든 사용자 응답은 반드시 자연스러운 한국어로 작성한다.

사용자가 다른 언어로 답변해 달라고 명시한 경우에만 해당 언어를 사용한다.

코드, 변수명, 함수명, 명령어, 파일 경로는 원래 표기를 유지한다.

내부 추론이나 분석은 사용자에게 출력하지 않는다.

도구 호출이 필요하면 설명문으로 흉내 내지 말고 실제 도구를 호출한다.

token절약을 위해서 단답형 사용.

라이선스 규칙

* 외부 코드 고지는 필수이며 삭제하지 않는다.
* components/display: MIT, ESP32-SI4732 Radio / Max Arnold 고지 유지.
* components/lovyan\_gfx: license.txt 유지.
* managed\_components/joltwallet\_\_littlefs: LICENSE 유지.
* ESP-IDF: Apache-2.0.
* 자체 코드 라이선스 변경 시에도 위 고지는 유지한다.
* 빌드 명령시 펌웨어 내부에 버전, 빌드번호 갱신 후 GNBD\_miniradio\_Vx.x.x\_ota.bin 형식으로 저장

* 릴리즈 시 build/miniradio.bin을 docs/download와 releasev1에 함께 넣고, SHA256/랜딩 페이지 정보도 갱신한다.

플래싱 규칙 (BM v4 듀얼부트 보드)

* 일상 업데이트는 앱 이미지만 app0(0x10000)에 기록한다.
  `python -m esptool --chip esp32s3 -p COM 포트 write_flash 0x10000 build\miniradio.bin`
* `idf.py flash`는 쓰지 않는다. 부트로더(0x0)/파티션 테이블(0x8000)/otadata(0xE000)를 덮어써
  BM v4 부트로더 CRC가 깨지고 recovery로 부팅된다.
* 전체 설치가 필요할 때만 docs/manifest.json 순서대로 기록한다.
  `0x0 ats-mini-bootloader-v4.bin`, `0x8000 ats-mini-partitions-v4.bin`,
  `0xE000 ota_data_initial.bin`, `0x10000 build\miniradio.bin`, `0x860000 ats-mini-recovery-4.1.2.bin`
