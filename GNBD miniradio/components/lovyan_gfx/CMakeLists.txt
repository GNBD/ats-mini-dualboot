# SPDX-License-Identifier: MIT
# LovyanGFX 1.2.29 (BSD-4-Clause) — ESP-IDF 컴포넌트.
# upstream boards.cmake/esp-idf.cmake와 동일한 SRCS 구성이나
# Fonts/efont·IPA 대용량 .c(117MB)는 제외 (기본 폰트 Font0/2/4 등은
# lgfx_fonts.cpp에 내장, efont/IPA 폰트는 미사용).
set(LGFX_SRC ${CMAKE_CURRENT_LIST_DIR}/src)

file(GLOB LGFX_SRCS
    ${LGFX_SRC}/lgfx/utility/*.c
    ${LGFX_SRC}/lgfx/v1/*.cpp
    ${LGFX_SRC}/lgfx/v1/lv_font/*.c
    ${LGFX_SRC}/lgfx/v1/misc/*.cpp
    ${LGFX_SRC}/lgfx/v1/panel/*.cpp
    ${LGFX_SRC}/lgfx/v1/platforms/arduino_default/*.cpp
    ${LGFX_SRC}/lgfx/v1/platforms/esp32/*.cpp
    ${LGFX_SRC}/lgfx/v1/platforms/esp32c3/*.cpp
    ${LGFX_SRC}/lgfx/v1/platforms/esp32s2/*.cpp
    ${LGFX_SRC}/lgfx/v1/platforms/esp32s3/*.cpp
    ${LGFX_SRC}/lgfx/v1/platforms/esp32p4/*.cpp
    ${LGFX_SRC}/lgfx/v1/touch/*.cpp
)

idf_component_register(SRCS ${LGFX_SRCS}
                       INCLUDE_DIRS ${LGFX_SRC}
                       REQUIRES nvs_flash efuse esp_lcd driver esp_timer esp_mm)
