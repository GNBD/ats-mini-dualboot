/*
 * SPDX-FileCopyrightText: 2025 ESP32-SI4732 Radio
 * SPDX-FileCopyrightText: 2026 Max Arnold
 * SPDX-FileCopyrightText: miniradio Contributors
 * SPDX-License-Identifier: MIT
 *
 * ST7789 LCD 드라이버 — LovyanGFX 백엔드 (8080 병렬 8비트).
 * ATS Mini 보드: WR/RD + D0~D7 + CS/DC/RST/BL.
 * 패널 자동 감지(RDDID): ST7789 원본/하이감마 + GC9307 미러패널.
 *
 * 깜빡임 방지: PSRAM 스프라이트에 그린 뒤 변경 영역(dirty rect)만 flush.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pin_wr;
    int pin_rd;       /* 미사용 시 -1 */
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pin_bl;       /* 미사용 시 -1 */
    int pin_d[8];     /* D0~D7 */
    uint16_t width;   /* 논리 폭 (rotate_landscape면 가로 모드 폭) */
    uint16_t height;  /* 논리 높이 */
    bool rotate_landscape; /* true: setRotation(3) = MADCTL MV|MY (원본 0xA0) */
    uint16_t x_gap;   /* 미사용 */
    uint16_t y_gap;
    bool use_psram;   /* 스프라이트를 PSRAM에 할당 */
} display_config_t;

typedef enum {
    DISPLAY_FONT_TINY  = 0,   /* lgfx Font0 (GLCD 8px급) */
    DISPLAY_FONT_SMALL = 1,   /* lgfx Font2 (16px) */
    DISPLAY_FONT_LARGE = 2,   /* lgfx Font4 (32px) */
    DISPLAY_FONT_DIGIT = 3,   /* lgfx Font7 (7-seg 48px) */
    DISPLAY_FONT_HERO  = 4,   /* lgfx Font6 (48px 숫자 전용) */
    DISPLAY_FONT_TEXT48 = 5,  /* lgfx AsciiFont24x48 (24x48 영문) */
} display_font_t;

typedef enum {
    DISPLAY_DATUM_TL = 0,     /* 좌상단 */
    DISPLAY_DATUM_TC,         /* 상단 중앙 */
    DISPLAY_DATUM_TR,         /* 우상단 */
    DISPLAY_DATUM_ML,         /* 좌중앙 */
    DISPLAY_DATUM_MC,         /* 중앙 */
    DISPLAY_DATUM_MR,         /* 우중앙 */
    DISPLAY_DATUM_BL,         /* 좌하단 */
    DISPLAY_DATUM_BC,         /* 하단 중앙 */
    DISPLAY_DATUM_BR,         /* 우하단 */
} display_datum_t;

/** @brief 8080 병렬 버스 + 패널 초기화 (RDDID 자동 감지) */
esp_err_t display_init(const display_config_t *cfg);

/** @brief 스프라이트 사용 가능 여부 */
bool display_has_sprite(void);

/** @brief 화면 전체를 dirty로 표시 (다음 flush에서 전체 전송) */
void display_invalidate_all(void);

/** @brief 누적된 dirty 영역을 패널로 전송 */
esp_err_t display_flush(void);

/** @brief dirty 영역 수동 추가 */
void display_mark_dirty(int16_t x, int16_t y, uint16_t w, uint16_t h);

/** @brief 누적된 dirty 영역을 전송하지 않고 초기화 (애니메이션용) */
void display_clear_dirty(void);

/** @brief 전체 화면 채우기 (RGB565) */
esp_err_t display_fill_screen(uint16_t color_rgb565);

/** @brief 사각형 채우기 (화면 밖 클리핑) */
esp_err_t display_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            uint16_t color_rgb565);

/** @brief RGB565 버퍼를 그대로 블릿 (부팅 이미지 등) */
esp_err_t display_draw_rgb565(int16_t x, int16_t y, uint16_t w, uint16_t h,
                              const uint16_t *data);

/** @brief QR 코드를 지정한 배율/색상으로 그립니다. */
esp_err_t display_draw_qr(const char *text, int16_t x, int16_t y,
                          uint8_t scale, uint16_t fg_rgb565,
                          uint16_t bg_rgb565);

/** @brief 사각형 테두리 (1px) */
esp_err_t display_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            uint16_t color_rgb565);

/** @brief 직선 (1px) */
esp_err_t display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            uint16_t color_rgb565);

/** @brief 클리핑 영역 설정/해제 */
esp_err_t display_set_clip(int16_t x, int16_t y, uint16_t w, uint16_t h);
void display_clear_clip(void);

/** @brief 라운드 사각형 채우기 */
esp_err_t display_fill_round_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                  uint16_t r, uint16_t color_rgb565);

/** @brief 라운드 사각형 테두리 (1px) */
esp_err_t display_draw_round_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                  uint16_t r, uint16_t color_rgb565);

/**
 * @brief 텍스트 출력 (불투명 bg, datum 기준 정렬)
 * @return 실제 그려진 폭(px)
 */
uint16_t display_draw_text(int16_t x, int16_t y, const char *text,
                           display_font_t font, uint16_t fg, uint16_t bg,
                           display_datum_t datum);

/** @brief 텍스트 폭(px) (현재 폰트 메트릭 기준) */
uint16_t display_text_width(const char *text, display_font_t font);

/** @brief 배율을 적용한 텍스트 출력 */
uint16_t display_draw_text_scaled(int16_t x, int16_t y, const char *text,
                                  display_font_t font, uint16_t fg, uint16_t bg,
                                  display_datum_t datum, uint8_t scale);
uint16_t display_draw_text_pct(int16_t x, int16_t y, const char *text,
                               display_font_t font, uint16_t fg, uint16_t bg,
                               display_datum_t datum, uint16_t pct);

/** @brief 배율을 적용한 텍스트 폭(px) */
uint16_t display_text_width_scaled(const char *text, display_font_t font,
                                   uint8_t scale);
uint16_t display_text_width_pct(const char *text, display_font_t font,
                                uint16_t pct);

/** @brief 폰트 높이(px) */
uint16_t display_font_height(display_font_t font);

/** @brief 백라이트 밝기 (0–100, LEDC PWM) */
esp_err_t display_set_backlight_level(uint8_t percent);

/** @brief 백라이트 on/off (100/0%) */
esp_err_t display_set_backlight(bool on);

esp_err_t display_capture_png(void **data, size_t *len);
void display_capture_free(void *data);

#define RGB565(r, g, b) \
    ((uint16_t)(((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))))

#ifdef __cplusplus
}
#endif
