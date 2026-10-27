/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * UI internal interface.
 * ui.c 를 기능별 파일로 나누면서 공유되는 전역변수/함수 선언만 모았다.
 * (ui.h 는 모듈 간 인터페이스, 이 헤더는 UI 내부 공유 선언)
 *
 * This source file is licensed under the MIT License.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "display.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DX 화면용 상태 (app.h의 bw_label/mode_str와 이름 충돌 회피) */
extern bool     s_agc_on;
extern uint8_t  s_avc;
extern uint8_t  s_softmute;
extern bool     s_squelch_on;
extern uint8_t  s_squelch_type[4];
extern uint8_t  s_squelch_thr[4];
extern bool     s_squelch_muted;
extern bool     s_stereo_force;
extern bool     s_deemph_75;
extern int32_t  s_bfo;
extern bool     s_digit_mode;
extern int      s_digit_place;
extern uint8_t  s_brightness;
extern bool     s_power_save;
extern uint8_t  s_theme;
extern bool     s_sta_connected;
extern char     s_ap_pass[9];
extern uint8_t  s_sig_view;
extern uint16_t s_scan_n;
extern bool     s_scan_running;
extern const char *s_scan_ptr[16];
extern bool     s_scan_secure[16];
extern int      s_scan_rssi[16];
extern int      s_scan_cursor;
extern int      s_scan_top;
extern volatile uint32_t s_radio_draw_seq;
extern int16_t  s_utc_min;
extern int      s_cpu_mhz;
extern uint32_t now_ms(void);
extern uint16_t s_quick_mask;
extern uint16_t s_sleep_min;
extern uint32_t s_sleep_deadline;
extern bool     s_sleep_armed;
extern const char *const QA_LABELS[];

/* ---- 공유 매크로 ---- */
#define UI_QUICK_MAX 6
#define UI_QA_COUNT  13

#define HDR_H        22
#define HDR_LINE_Y   21
#define META_CLEAR_W 104

#define HERO_X       6
#define HERO_Y       24
#define HERO_W       308
#define HERO_H       62

#define META_Y       90
#define META_H       16

#define SIG_X        6
#define SIG_Y        108
#define SIG_W        308
#define SIG_H        30

#define RDS_Y        142
#define RDS_H        20

#define CP_W         224

/* ---- UI 내부 공유 함수 (분할 파일 간 호출) ---- */
const char *preset_name_at(int idx);
void fmt_freq_short(uint32_t hz, int mode, char *buf, size_t n);
int band_marks(int band, uint32_t *out, int max);
int ui_mode_slot(radio_mode_t m);
const char *ui_mode_str(radio_mode_t m);
const char *ui_bw_label(const ui_state_t *s);
uint16_t fit_text(char *out, size_t outsz, const char *text,
                  display_font_t font, int maxw);
void panel(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border);
void pill(int x, int y, const char *text, int maxw, uint16_t fg, uint16_t bg);
void bar(int x, int y, int w, int h, int value, int max_value, uint16_t color);
void wifi_icon(int x, int y, uint16_t color, int bars);
void draw_status_right(const ui_state_t *s, int right);
void draw_header_tabs(ui_tab_t tab, const ui_state_t *s);
void draw_header_msg(const ui_state_t *s);
void ui_fmt_freq(const ui_state_t *s, char *num, size_t n, const char **unit);
void ui_fmt_step(const ui_state_t *s, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
