/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * Custom layout runtime (prototype).
 *
 * NEWPROJECT (Modder Studio) writes main/ui_custom_layout.h, which defines
 * a list of uc_widget_t. When UI_CUSTOM_ENABLE is 1, the radio screen is
 * rendered from that list instead of the built-in layout.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UC_RECT = 0, UC_TEXT, UC_BAR, UC_LINE_W,
    UC_FREQ, UC_RSQ, UC_RDS, UC_SIG, UC_BANDMAP, UC_CUSTOM,
    UC_MENU_GUIDE, UC_WIFI, UC_DECODE, UC_BATT_ICON
} uc_type_t;

typedef enum {
    UC_BG = 0, UC_PANEL, UC_FG, UC_MUTED, UC_LINE, UC_ACCENT, UC_ACCENTH,
    UC_OK, UC_WARN, UC_RED, UC_TRACK, UC_HSEL, UC_NONE
} uc_color_t;

typedef enum { UC_TINY = 0, UC_SMALL, UC_LARGE } uc_font_t;
typedef enum { UC_AL_L = 0, UC_AL_C, UC_AL_R } uc_align_t;

/* 표시 모드 마스크 (0 = 모든 모드) */
#define UC_M_FM   0x01
#define UC_M_AM   0x02
#define UC_M_SSB  0x04
#define UC_M_ALL  0x07

typedef enum {
    UC_B_NONE = 0, UC_B_FREQ, UC_B_MODE, UC_B_BAND, UC_B_RSSI, UC_B_SNR,
    UC_B_CLOCK, UC_B_DATE, UC_B_PS, UC_B_RT, UC_B_PTY, UC_B_PRESET,
    UC_B_PNAME, UC_B_ATS, UC_B_BATT, UC_B_VOL, UC_B_BW, UC_B_STEP,
    UC_B_BFO, UC_B_AGC, UC_B_SM, UC_B_AFC, UC_B_AVC, UC_B_SQ,
    UC_B_STEREO, UC_B_LINE1, UC_B_LINE2, UC_B_LINE3, UC_B_EIBI, UC_B_TOAST,
    UC_B_WIFI, UC_B_STATUS, UC_B_BATTV, UC_B_SLEEP, UC_B_DECODE_LEVEL
} uc_bind_t;

typedef struct {
    uint8_t  type;      /* uc_type_t */
    uint8_t  fill;      /* uc_color_t (rect/bar) */
    uint8_t  stroke;    /* uc_color_t (rect) */
    uint8_t  color;     /* uc_color_t (text/bar) */
    uint8_t  font;      /* uc_font_t */
    uint8_t  align;     /* uc_align_t */
    uint8_t  bind;      /* uc_bind_t */
    uint8_t  chip;      /* text: draw chip background */
    uint8_t  modes;     /* UC_M_* (0 = all) */
    uint8_t  scale;     /* percent 50..200 (0 = 100) */
    int16_t  x, y, w, h;
    uint16_t max;
    const char *text;
    uint16_t uid;       /* 위젯 고유 번호 (Lua 이벤트 바인딩용, 0=없음) */
    const char *events; /* 이벤트 핸들러 목록: "click=w1_click,rotate=w1_rot" */
    void (*fn)(int x, int y, int w, int h);   /* UC_CUSTOM */
} uc_widget_t;

/** @brief 커스텀 레이아웃이 활성인지 (레이아웃 헤더의 UI_CUSTOM_ENABLE) */
bool ui_custom_active(void);

void ui_widget_override_text(uint16_t uid, const char *text);
void ui_widget_override_visible(uint16_t uid, bool visible);
void ui_widget_override_color(uint16_t uid, uint8_t color);
int  ui_widget_override_hidden(uint16_t uid);
const char *ui_widget_override_text_of(uint16_t uid);
int  ui_widget_override_color_of(uint16_t uid, int def);
int  ui_widget_bind_value(const uc_widget_t *w);

/** @brief 커스텀 레이아웃으로 라디오 화면 전체를 그린다. */
void ui_custom_render(const ui_state_t *s);

#ifdef __cplusplus
}
#endif
