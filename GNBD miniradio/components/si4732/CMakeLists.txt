# SPDX-License-Identifier: MIT
#
# SSB 패치 데이터 빌드 옵션:
#   - 기본: si4732_patch_data.c (패치 없음, size=0 → SSB 미지원)
#   - 사용자가 ssb_patch_user.c를 이 디렉터리에 두면 대신 빌드됨
#     (예제: ssb_patch_user.example.c 참고 — 라이선스 책임 사용자 부담)
set(srcs "si4732.c" "si4732_patch.c")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/ssb_patch_user.c")
    list(APPEND srcs "ssb_patch_user.c")
else()
    list(APPEND srcs "si4732_patch_data.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include" "."
                       REQUIRES esp_driver_i2c esp_driver_gpio)
