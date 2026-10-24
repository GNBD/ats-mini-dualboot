/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * iOS / macOS 스타일 메인 UI (320x170).
 *
 *   RADIO : 상단 세그먼트 탭 + 주파수 카드 + 상태 바 + S/N 카드 + RDS 카드
 *   MENU  : 라디오 메뉴 (iOS 설정 그룹 리스트)
 *   SET   : 라디오 설정
 * 값을 바꾸는 바텀시트 형태의 옵션 목록으로 전체 선택지를 보여준다
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "display.h"
#include "ui.h"
#include "eibi.h"

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
extern uint16_t s_scan_n;
extern bool     s_scan_running;
extern const char *s_scan_ptr[16];
extern bool     s_scan_secure[16];
extern int      s_scan_rssi[16];
extern int      s_scan_cursor;
extern int      s_scan_top;
extern volatile uint32_t s_radio_draw_seq;
extern uint32_t now_ms(void);
static void ui_draw_all_dx3(const ui_state_t *s);
static void band_meta(const ui_state_t *s, char *buf, size_t n);

#define ADV_HIST 64
static uint8_t s_hist_rssi[ADV_HIST];
static uint8_t s_hist_snr[ADV_HIST];
static int s_hist_n;
static int s_hist_pos;
static uint32_t s_eibi_freq;
static int64_t  s_eibi_utc_min = -1;
static char     s_eibi_name[96];
extern int16_t  s_utc_min;
extern int      s_cpu_mhz;
extern uint16_t s_quick_mask;
extern const char *const QA_LABELS[];
#define UI_QUICK_MAX 6
#define UI_QA_COUNT  13

#define UI_LAYOUT_DX   1

static int ui_mode_slot(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return 0;
    case RADIO_MODE_AM:  return 1;
    case RADIO_MODE_LSB: return 2;
    default:             return 3;
    }
}

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


#define OPT_SHEET_Y  28
#define OPT_SHEET_H  (UI_H - OPT_SHEET_Y)
#define OPT_W        320
#define OPT_HDR      (OPT_SHEET_Y + 38)   /* 옵션 제목 시작 y */
#define OPT_RH       16

static const char *mode_str(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return "FM";
    case RADIO_MODE_AM:  return "AM";
    case RADIO_MODE_LSB: return "LSB";
    case RADIO_MODE_USB: return "USB";
    }
    return "?";
}

static const char *bw_label(const ui_state_t *s)
{
    static const char *fm[]  = {"AUTO", "110K", "84K", "60K", "40K"};
    static const char *am[]  = {"6K", "4K", "3K", "2K", "1K", "1.8K", "2.5K"};
    static const char *ssb[] = {"1.2K", "2.2K", "3K", "4K", "500", "1K"};
    int slot = (s->mode == RADIO_MODE_FM) ? 0
             : (s->mode == RADIO_MODE_AM ? 1 : 2);
    switch (slot) {
    case 0:  return fm[s->bw_code % 5];
    case 1:  return am[s->bw_code % 7];
    default: return ssb[s->bw_code % 6];
    }
}

/* ------------------------------------------------------------- 공통 */

static uint16_t fit_text(char *out, size_t outsz, const char *text,
                         display_font_t font, int maxw)
{
    if (!out || outsz == 0) {
        return 0;
    }
    snprintf(out, outsz, "%s", text ? text : "");
    if (maxw <= 0) {
        out[0] = '\0';
        return 0;
    }
    size_t n = strlen(out);
    while (n > 0 && display_text_width(out, font) > (uint16_t)maxw) {
        out[--n] = '\0';
    }
    return display_text_width(out, font);
}

static void panel(int x, int y, int w, int h, int r, uint16_t fill,
                  uint16_t border)
{
    display_fill_round_rect((int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h,
                            (uint16_t)r, fill);
    display_draw_round_rect((int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h,
                            (uint16_t)r, border);
}

static void pill(int x, int y, const char *text, int maxw,
                 uint16_t fg, uint16_t bg)
{
    char buf[24];
    uint16_t tw = fit_text(buf, sizeof(buf), text, DISPLAY_FONT_TINY, maxw - 14);
    uint16_t w = (uint16_t)(tw + 14);
    display_fill_round_rect((int16_t)x, (int16_t)y, w, 14, 7, bg);
    display_draw_text((int16_t)(x + w / 2), (int16_t)(y + 3), buf,
                      DISPLAY_FONT_TINY, fg, bg, DISPLAY_DATUM_TC);
}

static void battery_pill(int x, int y, const battery_state_t *b)
{
    uint16_t col = UI_OK;
    if (b->percent <= 15) {
        col = UI_WARN;
    } else if (b->percent <= 35) {
        col = UI_GOLD;
    }
    panel(x, y, 26, 11, 3, UI_CELL, UI_LINE);
    display_fill_rect((int16_t)(x + 26), (int16_t)(y + 3), 2, 5, UI_LINE);
    int fill = (b->percent * 20) / 100;
    if (b->charging) {
        fill = ((now_ms() / 350) % 3 + 1) * 20 / 3;
    }
    if (fill > 0) {
        display_fill_round_rect((int16_t)(x + 2), (int16_t)(y + 2),
                                (uint16_t)fill, 7, 2, col);
    }
}

static void bar(int x, int y, int w, int h, int value, int max_value,
                uint16_t color)
{
    display_fill_round_rect((int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h,
                            (uint16_t)(h / 2), UI_TRACK);
    if (max_value <= 0) {
        max_value = 1;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > max_value) {
        value = max_value;
    }
    int fill = (w - 2) * value / max_value;
    if (fill > 0) {
        display_fill_round_rect((int16_t)(x + 1), (int16_t)(y + 1),
                                (uint16_t)fill, (uint16_t)(h - 2),
                                (uint16_t)((h - 2) / 2), color);
    }
}


/* ------------------------------------------------------------- 헤더 */

static void wifi_icon(int x, int y, uint16_t color, int bars)
{
    /* 배터리 아이콘 호출 막대: 호출 시마다 개수 변경 */
    static const uint8_t heights[4] = {3, 5, 7, 9};
    if (bars < 0) { bars = 0; }
    if (bars > 4) { bars = 4; }
    for (int i = 0; i < bars; ++i) {
        int h = heights[i];
        display_fill_rect((int16_t)(x + i * 3), (int16_t)(y + 9 - h), 2,
                          (uint16_t)h, color);
    }
}

static void draw_status_right(const ui_state_t *s, int right)
{
    if (!s) {
        return;
    }

    char txt[12];
    bool show_icon = false;
    bool show_text = false;
    txt[0] = '\0';

    if (s->have_battery) {
        switch (s->batt_view) {
        case 0:
            show_icon = true;
            break;
        case 1:
            show_text = true;
            snprintf(txt, sizeof(txt), "%u.%02uV",
                     (unsigned)(s->battery.millivolt / 1000),
                     (unsigned)((s->battery.millivolt % 1000) / 10));
            break;
        case 2:
            show_text = true;
            snprintf(txt, sizeof(txt), "%u%%", s->battery.percent);
            break;
        case 4:
            show_text = true;
            show_icon = true;
            snprintf(txt, sizeof(txt), "%u.%02uV",
                     (unsigned)(s->battery.millivolt / 1000),
                     (unsigned)((s->battery.millivolt % 1000) / 10));
            break;
        default:
            show_text = true;
            show_icon = true;
            snprintf(txt, sizeof(txt), "%u%%", s->battery.percent);
            break;
        }
    }

    /* 오른쪽에서 왼쪽으로 배치: 배터리/아이콘/상태/Wi-Fi */
    int cursor = right - 6;
    if (show_icon) {
        display_fill_rect((int16_t)(cursor - 28), 5, 28, 11, UI_BG);
        battery_pill((int16_t)(right - 28), 5, &s->battery);
        cursor = right - 28 - 6;
    }
    if (show_text) {
        int tw = display_text_width(txt, DISPLAY_FONT_TINY);
        int tx = cursor - tw;
        if (tx < 0) {
            tx = 0;
        }
        display_draw_text((int16_t)tx, 6, txt, DISPLAY_FONT_TINY, UI_MUTED,
                          UI_BG, DISPLAY_DATUM_TL);
        cursor = tx - 6;
    }

    if (s->have_battery && s->battery.millivolt > 0 &&
        s->battery.millivolt <= 3700) {
        int tw = display_text_width("LOW", DISPLAY_FONT_TINY);
        int tx = cursor - tw;
        if (tx < 0) {
            tx = 0;
        }
        display_draw_text((int16_t)tx, 6, "LOW", DISPLAY_FONT_TINY, UI_WARN,
                          UI_BG, DISPLAY_DATUM_TL);
        cursor = tx - 6;
    }

    if (s->wifi_state == 1) {
        int tw = display_text_width("ap", DISPLAY_FONT_TINY);
        display_draw_text((int16_t)(cursor - tw), 6, "ap", DISPLAY_FONT_TINY,
                          UI_WARN, UI_BG, DISPLAY_DATUM_TL);
        return;
    }
    if (!s_sta_connected || s->wifi_state != 2) {
        return;
    }
    uint16_t wc;
    if (s->wifi_state == 0) {
        wc = UI_MUTED;
    } else if (s->wifi_state == 2) {
        wc = UI_ACC_HI;
    } else {
        wc = UI_GOLD;
    }
    int bars = 0;
    if (s->wifi_state == 1) {
        bars = 4;
    } else if (s->wifi_state == 2) {
        int r = s->wifi_rssi;
        bars = (r >= -55) ? 4 : (r >= -65) ? 3 : (r >= -75) ? 2 : 1;
    }
    const char *lbl = (s->wifi_state == 0) ? NULL
                    : (s->wifi_state == 1) ? "ap" : "sta";
    int lw = lbl ? display_text_width(lbl, DISPLAY_FONT_TINY) : 0;
    int gx = cursor - (12 + (lbl ? 3 + lw : 0));
    if (gx < 0) {
        gx = 0;
    }
    wifi_icon(gx, 5, wc, bars);
    if (lbl) {
        display_draw_text((int16_t)(gx + 15), 6, lbl, DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
    }
}

static void draw_header_tabs(ui_tab_t tab, const ui_state_t *s)
{
    (void)tab;
    display_fill_rect(0, 0, UI_W, HDR_H, UI_BG);

    if (s) {
        if (s_power_save) {
            display_fill_round_rect(4, 7, 7, 9, 3, UI_OK);
            display_draw_line(4, 15, 10, 8, UI_BG);
        } else {
            display_fill_round_rect(5, 7, 2, 8, 1, UI_ACC);
        }
        if (s->clock[0]) {
            display_draw_text(12, 6, s->clock, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                              DISPLAY_DATUM_TL);
        }
        if (s->save_active) {
            display_draw_text(44, 6, "SAVE", DISPLAY_FONT_TINY, UI_ACC_HI,
                              UI_BG, DISPLAY_DATUM_TL);
        }
        draw_status_right(s, UI_W);
    }
    display_fill_rect(0, HDR_LINE_Y, UI_W, 1, UI_LINE);
}

static void draw_header_msg(const ui_state_t *s)
{
    const char *msg = NULL;
    uint16_t col = UI_MUTED;
    char seek_buf[20];
    if (!s->radio_ok) {
        msg = "ERR";
        col = UI_WARN;
    } else if (s->seeking) {
        int pct = 0;
        if (s->sw_max > 0) {
            pct = (s->sw_steps * 100) / s->sw_max;
            if (pct > 100) { pct = 100; }
        }
        snprintf(seek_buf, sizeof(seek_buf), "SEEK%d%%", pct);
        msg = seek_buf;
        col = UI_ACC;
    } else if (s->seek_mode) {
        msg = "SEEK";
        col = UI_MUTED;
    } else if (s->patch_missing) {
        msg = "SSB";
        col = UI_WARN;
    }
    if (s->save_active) {
        display_fill_rect(74, 0, 96, HDR_H - 1, UI_BG);
        if (msg) {
            display_draw_text(78, 6, msg, DISPLAY_FONT_TINY, col, UI_BG,
                              DISPLAY_DATUM_TL);
        }
    } else {
        display_fill_rect(46, 0, 124, HDR_H - 1, UI_BG);
        if (msg) {
            display_draw_text(48, 6, msg, DISPLAY_FONT_TINY, col, UI_BG,
                              DISPLAY_DATUM_TL);
        }
    }
}

/* ------------------------------------------------------------- RADIO */

uint16_t ui_pal[UI_P_COUNT];

void ui_theme_set(int theme)
{
    switch (theme > 5 ? 0 : theme) {
    case 1:  /* BLACK: true black + white, no color */
        ui_pal[UI_P_BG]      = UI_RGB(0, 0, 0);
        ui_pal[UI_P_PANEL]   = UI_RGB(2, 4, 2);
        ui_pal[UI_P_HL]      = UI_RGB(3, 6, 3);
        ui_pal[UI_P_HSEL]    = UI_RGB(8, 16, 8);
        ui_pal[UI_P_CELL]    = UI_RGB(2, 4, 2);
        ui_pal[UI_P_LINE]    = UI_RGB(12, 24, 12);
        ui_pal[UI_P_TRACK]   = UI_RGB(6, 12, 6);
        ui_pal[UI_P_FG]      = UI_RGB(31, 63, 31);
        ui_pal[UI_P_MUTED]   = UI_RGB(16, 32, 16);
        ui_pal[UI_P_ACC]     = UI_RGB(24, 48, 24);
        ui_pal[UI_P_ACC_HI]  = UI_RGB(31, 63, 31);
        ui_pal[UI_P_GOLD]    = UI_RGB(20, 40, 20);
        ui_pal[UI_P_OK]      = UI_RGB(26, 52, 26);
        ui_pal[UI_P_WARN]    = UI_RGB(20, 40, 20);
        ui_pal[UI_P_RED]     = UI_RGB(28, 56, 28);
        ui_pal[UI_P_BLUE]    = UI_RGB(31, 63, 31);
        break;
    case 2:  /* WHITE: true white + black, no color */
        ui_pal[UI_P_BG]      = UI_RGB(31, 63, 31);
        ui_pal[UI_P_PANEL]   = UI_RGB(28, 56, 28);
        ui_pal[UI_P_HL]      = UI_RGB(26, 52, 26);
        ui_pal[UI_P_HSEL]    = UI_RGB(22, 44, 22);
        ui_pal[UI_P_CELL]    = UI_RGB(30, 60, 30);
        ui_pal[UI_P_LINE]    = UI_RGB(16, 32, 16);
        ui_pal[UI_P_TRACK]   = UI_RGB(24, 48, 24);
        ui_pal[UI_P_FG]      = UI_RGB(0, 0, 0);
        ui_pal[UI_P_MUTED]   = UI_RGB(10, 20, 10);
        ui_pal[UI_P_ACC]     = UI_RGB(8, 16, 8);
        ui_pal[UI_P_ACC_HI]  = UI_RGB(0, 0, 0);
        ui_pal[UI_P_GOLD]    = UI_RGB(12, 24, 12);
        ui_pal[UI_P_OK]      = UI_RGB(6, 12, 6);
        ui_pal[UI_P_WARN]    = UI_RGB(10, 20, 10);
        ui_pal[UI_P_RED]     = UI_RGB(6, 12, 6);
        ui_pal[UI_P_BLUE]    = UI_RGB(0, 0, 0);
        break;
    case 3:  /* LIGHT: white background with color accents */
        ui_pal[UI_P_BG]      = UI_RGB(31, 63, 31);
        ui_pal[UI_P_PANEL]   = UI_RGB(28, 56, 28);
        ui_pal[UI_P_HL]      = UI_RGB(26, 52, 30);
        ui_pal[UI_P_HSEL]    = UI_RGB(20, 48, 58);
        ui_pal[UI_P_CELL]    = UI_RGB(30, 60, 30);
        ui_pal[UI_P_LINE]    = UI_RGB(18, 36, 20);
        ui_pal[UI_P_TRACK]   = UI_RGB(24, 48, 26);
        ui_pal[UI_P_FG]      = UI_RGB(0, 0, 0);
        ui_pal[UI_P_MUTED]   = UI_RGB(10, 20, 14);
        ui_pal[UI_P_ACC]     = UI_RGB(0, 28, 50);
        ui_pal[UI_P_ACC_HI]  = UI_RGB(0, 40, 63);
        ui_pal[UI_P_GOLD]    = UI_RGB(28, 40, 0);
        ui_pal[UI_P_OK]      = UI_RGB(0, 44, 0);
        ui_pal[UI_P_WARN]    = UI_RGB(31, 12, 0);
        ui_pal[UI_P_RED]     = UI_RGB(31, 0, 0);
        ui_pal[UI_P_BLUE]    = UI_RGB(0, 10, 45);
        break;
    case 4:  /* DARK: black background with color accents */
        ui_pal[UI_P_BG]      = UI_RGB(0, 0, 0);
        ui_pal[UI_P_PANEL]   = UI_RGB(2, 4, 3);
        ui_pal[UI_P_HL]      = UI_RGB(3, 8, 8);
        ui_pal[UI_P_HSEL]    = UI_RGB(6, 20, 20);
        ui_pal[UI_P_CELL]    = UI_RGB(1, 3, 2);
        ui_pal[UI_P_LINE]    = UI_RGB(8, 18, 18);
        ui_pal[UI_P_TRACK]   = UI_RGB(4, 10, 10);
        ui_pal[UI_P_FG]      = UI_RGB(31, 63, 31);
        ui_pal[UI_P_MUTED]   = UI_RGB(14, 30, 28);
        ui_pal[UI_P_ACC]     = UI_RGB(2, 40, 44);
        ui_pal[UI_P_ACC_HI]  = UI_RGB(8, 60, 60);
        ui_pal[UI_P_GOLD]    = UI_RGB(30, 46, 0);
        ui_pal[UI_P_OK]      = UI_RGB(0, 50, 10);
        ui_pal[UI_P_WARN]    = UI_RGB(31, 18, 0);
        ui_pal[UI_P_RED]     = UI_RGB(31, 4, 4);
        ui_pal[UI_P_BLUE]    = UI_RGB(8, 20, 31);
        break;
    case 5:
        ui_pal[UI_P_BG]      = UI_RGB(0, 0, 0);
        ui_pal[UI_P_PANEL]   = UI_RGB(0, 8, 0);
        ui_pal[UI_P_HL]      = UI_RGB(0, 12, 8);
        ui_pal[UI_P_HSEL]    = UI_RGB(0, 16, 16);
        ui_pal[UI_P_CELL]    = UI_RGB(0, 6, 0);
        ui_pal[UI_P_LINE]    = UI_RGB(0, 28, 0);
        ui_pal[UI_P_TRACK]   = UI_RGB(0, 16, 0);
        ui_pal[UI_P_FG]      = UI_RGB(30, 60, 30);
        ui_pal[UI_P_MUTED]   = UI_RGB(0, 48, 0);
        ui_pal[UI_P_ACC]     = UI_RGB(1, 33, 31);
        ui_pal[UI_P_ACC_HI]  = UI_RGB(3, 54, 50);
        ui_pal[UI_P_GOLD]    = UI_RGB(31, 39, 2);
        ui_pal[UI_P_OK]      = UI_RGB(6, 52, 11);
        ui_pal[UI_P_WARN]    = UI_RGB(31, 17, 7);
        ui_pal[UI_P_RED]     = UI_RGB(31, 0, 0);
        ui_pal[UI_P_BLUE]    = UI_RGB(4, 10, 31);
        break;
    default:  /* OFF / default */
        ui_pal[UI_P_BG]      = UI_RGB(1, 3, 2);
        ui_pal[UI_P_PANEL]   = UI_RGB(3, 6, 4);
        ui_pal[UI_P_HL]      = UI_RGB(2, 10, 8);
        ui_pal[UI_P_HSEL]    = UI_RGB(3, 16, 13);
        ui_pal[UI_P_CELL]    = UI_RGB(2, 5, 3);
        ui_pal[UI_P_LINE]    = UI_RGB(5, 12, 7);
        ui_pal[UI_P_TRACK]   = UI_RGB(4, 10, 6);
        ui_pal[UI_P_FG]      = UI_RGB(30, 60, 30);
        ui_pal[UI_P_MUTED]   = UI_RGB(17, 35, 19);
        ui_pal[UI_P_ACC]     = UI_RGB(1, 33, 31);
        ui_pal[UI_P_ACC_HI]  = UI_RGB(3, 54, 50);
        ui_pal[UI_P_GOLD]    = UI_RGB(31, 39, 2);
        ui_pal[UI_P_OK]      = UI_RGB(6, 52, 11);
        ui_pal[UI_P_WARN]    = UI_RGB(31, 17, 7);
        ui_pal[UI_P_RED]     = UI_RGB(31, 0, 0);
        ui_pal[UI_P_BLUE]    = UI_RGB(4, 10, 31);
        break;
    }
}

void ui_init(void)
{
    ui_theme_set(s_theme);
    display_fill_screen(UI_BG);
    display_flush();
}

void ui_draw_header(const ui_state_t *s)
{
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);
}

static void fmt_freq(const ui_state_t *s, char *num, size_t n,
                     const char **unit)
{
    if (s->mode == RADIO_MODE_FM) {
        /* FM은 MHz 단위로 소수점 두 자리 표시 */
        snprintf(num, n, "%lu.%02lu",
                 (unsigned long)(s->freq_hz / 1000000),
                 (unsigned long)((s->freq_hz / 10000) % 100));
        *unit = "MHz";
    } else if (s->unit_mode == 1) {
        if (band_is_ssb(s->mode)) {
            snprintf(num, n, "%lu.%02lu",
                     (unsigned long)(s->freq_hz / 1000),
                     (unsigned long)((s->freq_hz % 1000) / 10));
        } else {
            snprintf(num, n, "%lu", (unsigned long)(s->freq_hz / 1000));
        }
        *unit = "kHz";
    } else {
        if (band_is_ssb(s->mode)) {
            snprintf(num, n, "%lu.%05lu",
                     (unsigned long)(s->freq_hz / 1000000),
                     (unsigned long)((s->freq_hz / 10) % 100000));
        } else {
            snprintf(num, n, "%lu.%03lu",
                     (unsigned long)(s->freq_hz / 1000000),
                     (unsigned long)((s->freq_hz / 1000) % 1000));
        }
        *unit = "MHz";
    }
}

void ui_draw_freq_saved(void)
{
    display_fill_rect(0, HERO_Y - 2, UI_W, HERO_H + 6, UI_BG);
    panel(HERO_X, HERO_Y, HERO_W, HERO_H, 16, UI_PANEL, UI_HL);
    display_draw_text(UI_W / 2, HERO_Y + 15, "-- SAVED --",
                      DISPLAY_FONT_LARGE, UI_OK, UI_PANEL, DISPLAY_DATUM_TC);
}

void ui_draw_freq(const ui_state_t *s)
{
    s_radio_draw_seq++;
    display_fill_rect(0, HERO_Y - 2, UI_W, HERO_H + 6, UI_BG);
    panel(HERO_X, HERO_Y, HERO_W, HERO_H, 16, UI_PANEL, UI_HL);

    int meta_x = HERO_X + 10;
    int meta_y = HERO_Y + 5;
    uint16_t mw = display_draw_text((int16_t)meta_x, (int16_t)meta_y,
                                    band_table[s->band_idx].name,
                                    DISPLAY_FONT_TINY, UI_ACC_HI, UI_PANEL,
                                    DISPLAY_DATUM_TL);
    mw += display_draw_text((int16_t)(meta_x + mw + 4), (int16_t)meta_y, "/",
                            DISPLAY_FONT_TINY, UI_LINE, UI_PANEL,
                            DISPLAY_DATUM_TL);
    display_draw_text((int16_t)(meta_x + mw + 7), (int16_t)meta_y,
                      mode_str(s->mode), DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);

    char num[20];
    const char *unit;
    fmt_freq(s, num, sizeof(num), &unit);

    display_font_t font = DISPLAY_FONT_HERO;
    uint16_t fw = display_text_width(num, font);
    if (fw > HERO_W - 20) {
        font = DISPLAY_FONT_LARGE;
        fw = display_text_width(num, font);
    }
    uint16_t fh = display_font_height(font);
    int ty = HERO_Y + (HERO_H - (int)fh) / 2 + 1;
    display_draw_text(UI_W / 2, (int16_t)ty, num, font, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TC);

    display_draw_text(HERO_X + HERO_W - 10, HERO_Y + 5, unit,
                      DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TR);

    int badge_x = HERO_X + 8;
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(badge_x, HERO_Y + HERO_H - 18, p, 50, UI_BLACK, UI_ACC);
        badge_x += display_text_width(p, DISPLAY_FONT_TINY) + 18;
    }
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(badge_x, HERO_Y + HERO_H - 18, a, 50, UI_BLACK, UI_GOLD);
    }
    if (s->bfo_hz != 0 && band_is_ssb(s->mode)) {
        char b[16];
        snprintf(b, sizeof(b), "BFO %+d", (int)s->bfo_hz);
        uint16_t bw = display_text_width(b, DISPLAY_FONT_TINY);
        pill(HERO_X + HERO_W - (int)bw - 22, HERO_Y + HERO_H - 18, b, 110,
             UI_GOLD, UI_PANEL);
    }
}

static void chip_row(const ui_state_t *s)
{
    display_fill_rect(0, META_Y, UI_W, META_H, UI_BG);

    char st[12];
    if (s->step_hz >= 1000 && s->step_hz % 1000 == 0) {
        snprintf(st, sizeof(st), "%dK", s->step_hz / 1000);
    } else {
        snprintf(st, sizeof(st), "%d", s->step_hz);
    }

    int y = META_Y + 4;
    int x = 10;
    uint16_t w = display_draw_text((int16_t)x, (int16_t)y, "BW",
                                   DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                                   DISPLAY_DATUM_TL);
    display_draw_text((int16_t)(x + w + 5), (int16_t)y, bw_label(s),
                      DISPLAY_FONT_TINY, UI_FG, UI_BG, DISPLAY_DATUM_TL);

    uint16_t vw = display_text_width(st, DISPLAY_FONT_TINY);
    uint16_t lw = display_text_width("STEP", DISPLAY_FONT_TINY);
    int rx = UI_W - 10 - (int)vw;
    display_draw_text((int16_t)(rx - (int)lw - 5), (int16_t)y, "STEP",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
    display_draw_text((int16_t)rx, (int16_t)y, st, DISPLAY_FONT_TINY, UI_FG,
                      UI_BG, DISPLAY_DATUM_TL);

    display_fill_rect(UI_W / 2, (int16_t)(META_Y + 5), 1, 6, UI_LINE);
}

void ui_draw_smeter(const ui_state_t *s)
{
    s_radio_draw_seq++;
    ui_draw_adv_signal4b(s);
    return;
    if (s->ui_mode == UI_LAYOUT_DX) {
        ui_draw_adv_signal4b(s);
        return;
    }
    ui_draw_all_dx3(s);
    return;
    display_fill_rect(0, SIG_Y, UI_W, SIG_H + 2, UI_BG);
    panel(SIG_X, SIG_Y, SIG_W, SIG_H, 12, UI_PANEL, UI_HL);

    char sv[16];
    char nv[16];
    uint16_t ncol = (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_ACC;
    if (s->have_rsq) {
        snprintf(sv, sizeof(sv), "RSSI %d%s", s->rsq.rssi,
                 s->rsq.rssi > 63 ? "+" : "");
        snprintf(nv, sizeof(nv), "SNR %u", s->rsq.snr);
    } else {
        snprintf(sv, sizeof(sv), "RSSI --");
        snprintf(nv, sizeof(nv), "SNR --");
        ncol = UI_MUTED;
    }
    const char *lbl = "SIGNAL";
    uint16_t lcol = UI_MUTED;
    char seek_lbl[20];
    if (s->seeking) {
        int pct = 0;
        if (s->sw_max > 0) {
            pct = (s->sw_steps * 100) / s->sw_max;
            if (pct > 100) { pct = 100; }
            if (pct < 0) { pct = 0; }
        }
        snprintf(seek_lbl, sizeof(seek_lbl), "SEEK %d%%", pct);
        lbl = seek_lbl;
        lcol = UI_ACC_HI;
    }
    display_draw_text(SIG_X + 10, SIG_Y + 5, lbl, DISPLAY_FONT_TINY,
                      lcol, UI_PANEL, DISPLAY_DATUM_TL);
    int right = SIG_X + SIG_W - 10;
    int nw = (int)display_text_width(nv, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)right, SIG_Y + 5, nv, DISPLAY_FONT_TINY, ncol,
                      UI_PANEL, DISPLAY_DATUM_TR);
    int divx = right - nw - 8;
    display_fill_rect((int16_t)divx, SIG_Y + 7, 1, 6, UI_LINE);
    display_draw_text((int16_t)(divx - 6), SIG_Y + 5, sv,
                      DISPLAY_FONT_TINY, UI_OK, UI_PANEL, DISPLAY_DATUM_TR);

    int bar_y = SIG_Y + 15;
    bar(SIG_X + 10, bar_y, SIG_W - 20, 4,
        s->have_rsq ? s->rsq.rssi : 0, 63, UI_OK);
    bar_y += 5;
    bar(SIG_X + 10, bar_y, SIG_W - 20, 4,
        s->have_rsq ? s->rsq.snr : 0, 63,
        (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_ACC);
}

void ui_draw_rds(const ui_state_t *s)
{
    s_radio_draw_seq++;
    ui_draw_adv_signal4b(s);
    return;
    if (s->ui_mode == UI_LAYOUT_DX) {
        ui_draw_adv_signal4b(s);
        return;
    }
    ui_draw_all_dx3(s);
    return;
    display_fill_rect(0, RDS_Y, UI_W, UI_H - RDS_Y, UI_BG);
    panel(6, RDS_Y, 308, RDS_H, 11, UI_PANEL, UI_HL);

    bool fm = (s->mode == RADIO_MODE_FM);
    const char *tag = fm ? "RDS" : "EiBi";
    uint16_t tagc = !fm ? UI_GOLD : (s->rds_on ? UI_ACC_HI : UI_MUTED);
    display_draw_text(16, RDS_Y + 7, tag, DISPLAY_FONT_TINY, tagc, UI_PANEL,
                      DISPLAY_DATUM_TL);
    display_fill_rect(48, RDS_Y + 5, 1, RDS_H - 10, UI_LINE);

    int cx = 56;
    int right = 304;
    int avail = right - cx;

    if (!fm) {
        static int st_off;
        const char *line = (s->station && s->station[0]) ? s->station
                                                         : "...";
        char rot[128];
        size_t len = strlen(line);
        if (s->station && len > 3 &&
            display_text_width(line, DISPLAY_FONT_TINY) > (uint16_t)avail) {
            int off = st_off % (int)len;
            for (size_t i = 0; i < len; ++i) {
                rot[i] = line[(off + i) % len];
            }
            rot[len] = '\0';
            line = rot;
            st_off++;
        }
        char buf[128];
        fit_text(buf, sizeof(buf), line, DISPLAY_FONT_TINY, avail);
        display_draw_text(cx, RDS_Y + 7, buf, DISPLAY_FONT_TINY,
                          s->station ? UI_OK : UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
        return;
    }

    if (!s->rds_on) {
        display_draw_text(cx, RDS_Y + 7, "OFF", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);
        return;
    }
    const rds_info_t *r = s->rds;
    if (!r || (!r->ps_valid && !r->rt_valid)) {
        display_draw_text(cx, RDS_Y + 7, "...",
                          DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
        return;
    }

    int ptyw = 0;
    const char *pty = rds_pty_name(r->pty);
    if (pty && pty[0] && strcmp(pty, "NONE") != 0) {
        char pb[24];
        ptyw = fit_text(pb, sizeof(pb), pty, DISPLAY_FONT_TINY, 72);
        display_draw_text(right, RDS_Y + 7, pb, DISPLAY_FONT_TINY, UI_ACC,
                          UI_PANEL, DISPLAY_DATUM_TR);
        ptyw += 10;
    }

    char line[80];
    const char *src = (r->rt_valid && r->rt[0]) ? r->rt : r->ps;

    static int s_rt_off;
    char rot[80];
    size_t len = strlen(src);
    int textw = avail - ptyw;
    if (textw < 24) {
        textw = 24;
    }
    if ((r->rt_valid && r->rt[0]) && len > 3 && len < sizeof(rot) &&
        display_text_width(src, DISPLAY_FONT_SMALL) > (uint16_t)textw) {
        int off = s_rt_off % (int)len;
        for (size_t i = 0; i < len; ++i) {
            rot[i] = src[(off + i) % len];
        }
        rot[len] = '\0';
        src = rot;
        s_rt_off++;
    }

    fit_text(line, sizeof(line), src, DISPLAY_FONT_SMALL, textw);
    display_draw_text(cx, RDS_Y + 3, line, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
}

void ui_draw_status(const ui_state_t *s)
{
    draw_header_msg(s);
}

void ui_draw_loading(const char *msg)
{
    panel(60, 55, 200, 60, 12, UI_PANEL, UI_ACC);
    display_draw_text(UI_W / 2, 68, msg ? msg : "LOADING",
                      DISPLAY_FONT_SMALL, UI_FG, UI_PANEL, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 88, "PLEASE WAIT", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TC);
    display_flush();
}

void ui_draw_volume(const ui_state_t *s, bool show)
{
    if (!show) {
        ui_draw_all(s);
        return;
    }
    display_fill_round_rect(44, 50, 232, 70, 18, UI_PANEL);
    display_draw_round_rect(44, 50, 232, 70, 18, UI_LINE);

    display_draw_text(60, 58, "Volume", DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    char v[8];
    snprintf(v, sizeof(v), "%u", s->volume);
    display_draw_text(260, 55, v, DISPLAY_FONT_LARGE, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TR);
    bar(60, 96, 200, 8, s->volume, 63, UI_ACC);
}

void ui_draw_home(const ui_state_t *s, int cursor, const ui_app_t *apps,
                  int count)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 7, 3, 12, 1, UI_ACC);
    display_draw_text(16, 6, "APPS", DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    if (s->clock[0]) {
        display_draw_text(UI_W / 2, 6, s->clock, DISPLAY_FONT_SMALL, UI_ACC_HI,
                          UI_BG, DISPLAY_DATUM_TC);
    }
    draw_status_right(s, UI_W);
    display_fill_rect(0, 26, UI_W, 1, UI_LINE);

    if (count <= 0) {
        return;
    }

    const int cols = 3;
    const int rows = (count + cols - 1) / cols;
    const int icon = (rows <= 1) ? 64 : 46;
    const int step = UI_W / cols;
    const int y0 = (rows <= 1) ? 44 : 36;
    const int row_h = (rows <= 1) ? 0 : 60;

    for (int i = 0; i < count; ++i) {
        int cx = step / 2 + (i % cols) * step;
        int x = cx - icon / 2;
        int y = y0 + (i / cols) * row_h;
        bool sel = (i == cursor);

        panel(x - 3, y - 3, icon + 6, icon + 6, 18,
              sel ? UI_HSEL : UI_BG, sel ? UI_ACC : UI_LINE);
        panel(x, y, icon, icon, 16, UI_PANEL, sel ? UI_ACC_HI : UI_LINE);

        const char *g = apps[i].glyph;
        display_font_t gf = (display_font_t)apps[i].glyph_font;
        int gw = display_text_width(g, gf);
        int gh = display_font_height(gf);
        display_draw_text((int16_t)(cx - gw / 2),
                          (int16_t)(y + icon / 2 - gh / 2), g, gf,
                          sel ? UI_FG : UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);

        char name[20];
        fit_text(name, sizeof(name), apps[i].name, DISPLAY_FONT_TINY,
                 step - 8);
        display_draw_text(cx, y + icon + 6, name, DISPLAY_FONT_TINY,
                          sel ? UI_FG : UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    }

    display_draw_text(UI_W / 2, 160,
                      "turn: select   press: open   hold: radio",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

void ui_draw_setup_welcome(const char *name, const char *version,
                           int step, int total, int selected)
{
    display_fill_screen(UI_BG);

    char b[56];
    char hdr[56];
    char fit[56];
    snprintf(hdr, sizeof(hdr), "%s  v%s", name ? name : "GNBD miniradio",
             version ? version : "?");
    fit_text(fit, sizeof(fit), hdr, DISPLAY_FONT_TINY, 220);
    display_draw_text(12, 6, fit, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    snprintf(b, sizeof(b), "%d/%d", step, total);
    display_draw_text(UI_W - 12, 6, b, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TR);
    display_fill_rect(12, 18, UI_W - 24, 1, UI_LINE);

    display_fill_round_rect(8, 24, 3, 26, 1, UI_ACC);
    display_draw_text(16, 22, "SETUP", DISPLAY_FONT_LARGE, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    for (int i = 0; i < 2; ++i) {
        int x = 12;
        int w = UI_W - 24;
        int y = 66 + i * 30;
        bool cur = (i == selected);
        if (cur) {
            display_fill_round_rect((int16_t)x, (int16_t)y, (uint16_t)w,
                                    26, 7, UI_PANEL);
            display_fill_rect((int16_t)(x + 2), (int16_t)(y + 6), 2, 14,
                              UI_ACC);
        } else {
            display_fill_rect((int16_t)x, (int16_t)(y + 26),
                              (uint16_t)w, 1, UI_LINE);
        }
        display_draw_text((int16_t)(x + 12), (int16_t)(y + 5),
                          i == 0 ? "Start setup" : "Skip setup",
                          DISPLAY_FONT_SMALL,
                          cur ? UI_FG : UI_MUTED,
                          cur ? UI_PANEL : UI_BG, DISPLAY_DATUM_TL);
        display_draw_text((int16_t)(x + w - 10), (int16_t)(y + 8), ">",
                          DISPLAY_FONT_TINY,
                          cur ? UI_ACC_HI : UI_MUTED,
                          cur ? UI_PANEL : UI_BG, DISPLAY_DATUM_TR);
    }

    display_draw_text(12, 130, "WEB SETUP", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    display_draw_text(UI_W - 12, 130, "192.168.4.1", DISPLAY_FONT_TINY,
                      UI_ACC_HI, UI_BG, DISPLAY_DATUM_TR);

    display_draw_text(UI_W / 2, 144, "turn: select   press: ok",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    bar(12, 160, UI_W - 24, 3, step, total, UI_ACC);
}

void ui_draw_clock(const ui_state_t *s)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 7, 3, 12, 1, UI_ACC);
    display_draw_text(16, 6, "CLOCK", DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    draw_status_right(s, UI_W);
    display_fill_rect(0, 26, UI_W, 1, UI_LINE);

    display_font_t f = DISPLAY_FONT_HERO;
    if (display_text_width(s->clock, f) > 300) {
        f = DISPLAY_FONT_DIGIT;
    }
    display_draw_text(UI_W / 2, 38, s->clock, f, UI_FG, UI_BG,
                      DISPLAY_DATUM_TC);

    if (s->date[0]) {
        display_draw_text(UI_W / 2, 104, s->date, DISPLAY_FONT_SMALL,
                          UI_ACC_HI, UI_BG, DISPLAY_DATUM_TC);
    }
    display_draw_text(UI_W / 2, 126, "LOCAL TIME", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 160, "press: apps   hold: radio",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

static void weather_icon(int x, int y, const char *cond)
{
    char c[40] = {0};
    int n = 0;
    for (const char *p = cond; p && *p && n < (int)sizeof(c) - 1; ++p) {
        c[n++] = (char)tolower((unsigned char)*p);
    }
    c[n] = '\0';

    bool rain = strstr(c, "rain") || strstr(c, "drizzle") ||
                strstr(c, "shower");
    bool snow = strstr(c, "snow") || strstr(c, "sleet");
    bool storm = strstr(c, "thunder") || strstr(c, "storm");
    bool fog = strstr(c, "fog") || strstr(c, "mist") || strstr(c, "haze");
    bool cloud = strstr(c, "cloud") || strstr(c, "overcast") ||
                 rain || snow || storm;

    if (!cloud && !fog) {
        display_fill_round_rect(x + 18, y + 16, 26, 26, 13, UI_GOLD);
        display_fill_rect(x + 30, y + 4, 2, 8, UI_GOLD);
        display_fill_rect(x + 30, y + 46, 2, 8, UI_GOLD);
        display_fill_rect(x + 6, y + 28, 8, 2, UI_GOLD);
        display_fill_rect(x + 48, y + 28, 8, 2, UI_GOLD);
        return;
    }

    if (fog && !rain && !snow && !storm) {
        for (int i = 0; i < 4; ++i) {
            display_fill_round_rect(x + 8 + (i % 2) * 4, y + 20 + i * 7,
                                    38 - (i % 2) * 6, 3, 1, UI_MUTED);
        }
        return;
    }

    display_fill_round_rect(x + 8, y + 24, 22, 15, 7, UI_MUTED);
    display_fill_round_rect(x + 20, y + 14, 26, 25, 12, UI_MUTED);
    display_fill_round_rect(x + 40, y + 24, 18, 15, 7, UI_MUTED);

    if (rain || snow || storm) {
        if (storm) {
            display_fill_rect(x + 28, y + 40, 10, 3, UI_GOLD);
            display_fill_rect(x + 24, y + 43, 6, 3, UI_GOLD);
            display_fill_rect(x + 34, y + 46, 10, 3, UI_GOLD);
        } else if (snow) {
            for (int i = 0; i < 3; ++i) {
                display_fill_round_rect(x + 16 + i * 12, y + 43, 4, 4, 2,
                                        UI_ACC_HI);
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                display_fill_round_rect(x + 16 + i * 12, y + 42, 3, 9, 1,
                                        UI_ACC_HI);
            }
        }
    }
}

void ui_draw_weather(const ui_state_t *s, int state, int page,
                     const char *city, const char *resolved, const char *temp,
                     const char *cond, const char *humidity, const char *wind)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 7, 3, 12, 1, UI_ACC);
    display_draw_text(16, 6, "WEATHER", DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    draw_status_right(s, UI_W);
    display_fill_rect(0, 26, UI_W, 1, UI_LINE);

    if (city && city[0]) {
        char citybuf[40];
        fit_text(citybuf, sizeof(citybuf), city, DISPLAY_FONT_TINY, 220);
        display_draw_text(UI_W / 2, 31, citybuf, DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_BG, DISPLAY_DATUM_TC);
    }

    if (state == 0) {
        display_draw_text(UI_W / 2, 84, "LOADING...", DISPLAY_FONT_SMALL,
                          UI_ACC_HI, UI_BG, DISPLAY_DATUM_TC);
    } else if (state == 2) {
        weather_icon(128, 38, "cloud");
        display_draw_text(UI_W / 2, 104, "WEATHER UNAVAILABLE",
                          DISPLAY_FONT_SMALL, UI_WARN, UI_BG,
                          DISPLAY_DATUM_TC);
        display_draw_text(UI_W / 2, 124, "CHECK WI-FI", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    } else {
        panel(20, 42, 92, 92, 18, UI_PANEL, UI_LINE);
        weather_icon(36, 54, cond);

        display_font_t tf = DISPLAY_FONT_HERO;
        if (display_text_width(temp, tf) > 160) {
            tf = DISPLAY_FONT_TEXT48;
        }
        display_draw_text(220, 42, temp, tf, UI_FG, UI_BG, DISPLAY_DATUM_TC);

        char line[40];
        fit_text(line, sizeof(line), cond ? cond : "", DISPLAY_FONT_SMALL, 176);
        display_draw_text(220, 94, line, DISPLAY_FONT_SMALL, UI_ACC_HI, UI_BG,
                          DISPLAY_DATUM_TC);

        snprintf(line, sizeof(line), "HUM %s   WIND %s",
                 humidity ? humidity : "-", wind ? wind : "-");
        char shown[48];
        fit_text(shown, sizeof(shown), line, DISPLAY_FONT_TINY, 176);
        display_draw_text(220, 118, shown, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                          DISPLAY_DATUM_TC);
    }

    if (state == 1 && resolved && resolved[0]) {
        char shown[48];
        fit_text(shown, sizeof(shown), resolved, DISPLAY_FONT_TINY, 280);
        display_draw_text(UI_W / 2, 136, shown, DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_BG, DISPLAY_DATUM_TC);
    }

    display_fill_round_rect(UI_W / 2 - 10, 149, 8, 2, 1,
                            page == 0 ? UI_ACC : UI_TRACK);
    display_fill_round_rect(UI_W / 2 + 2, 149, 8, 2, 1,
                            page == 1 ? UI_ACC : UI_TRACK);

    display_draw_text(UI_W / 2, 160,
                      page == 0 ? "turn: page   press: manual   hold: apps"
                                : "turn: page   press: location   hold: apps",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

static void scan_freq_str(uint32_t hz, char *buf, size_t n)
{
    if (hz >= 1000000) {
        snprintf(buf, n, "%lu.%02lu", (unsigned long)(hz / 1000000),
                 (unsigned long)((hz / 10000) % 100));
    } else {
        snprintf(buf, n, "%lu", (unsigned long)(hz / 1000));
    }
}

static void scan_graph(int x, int y, int w, int h, const uint8_t *values,
                       int count, uint16_t color)
{
    display_fill_round_rect(x, y, w, h, 4, UI_BG);
    for (int i = 1; i < 3; ++i) {
        display_fill_rect(x, y + h * i / 3, (uint16_t)w, 1, UI_LINE);
    }
    if (count <= 0 || w <= 0) {
        return;
    }

    /* 관측 최소/최대에 맞춰 자동 스케일 → 낮은 신호와 높은 신호 대비 강조 */
    int vmin = 255;
    int vmax = 0;
    for (int i = 0; i < count; ++i) {
        if (values[i] < vmin) { vmin = values[i]; }
        if (values[i] > vmax) { vmax = values[i]; }
    }
    if (vmax - vmin < 6) {
        vmax = vmin + 6;
    }
    int span = vmax - vmin;
    if (span < 1) {
        span = 1;
    }

    for (int px = 0; px < w; ++px) {
        int i0 = px * count / w;
        int i1 = (px + 1) * count / w;
        if (i1 <= i0) {
            i1 = i0 + 1;
        }
        int peak = vmin;
        for (int i = i0; i < i1 && i < count; ++i) {
            if (values[i] > peak) {
                peak = values[i];
            }
        }
        int rel = peak - vmin;
        if (rel < 0) {
            rel = 0;
        }
        int bh = rel * (h - 1) / span;
        if (bh > 0) {
            display_fill_rect((int16_t)(x + px), (int16_t)(y + h - bh), 1,
                              (uint16_t)bh, color);
        }
    }
}

void ui_draw_spectrum(const ui_state_t *s, const char *band, uint32_t min_hz,
                      uint32_t max_hz, const uint8_t *rssi,
                      const uint8_t *snr, int count, int index, int cursor,
                      bool done)
{
    (void)band;
    ui_draw_all(s);

    display_fill_rect(42, 0, 62, HDR_H - 1, UI_BG);
    display_draw_text(44, 6, "SCAN", DISPLAY_FONT_TINY, UI_ACC, UI_BG,
                      DISPLAY_DATUM_TL);
    char pct[8];
    int p = done ? 100 : (count > 0 ? index * 100 / count : 0);
    if (p < 0) { p = 0; }
    if (p > 100) { p = 100; }
    snprintf(pct, sizeof(pct), "%d%%", p);
    display_draw_text(72, 6, pct, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    display_fill_rect(0, SIG_Y, UI_W, UI_H - SIG_Y, UI_BG);

    panel(6, 106, 308, 25, 8, UI_PANEL, UI_LINE);
    display_draw_text(10, 108, "S", DISPLAY_FONT_TINY, UI_ACC_HI, UI_PANEL,
                      DISPLAY_DATUM_TL);
    scan_graph(18, 110, 294, 18, rssi, count, UI_ACC_HI);

    panel(6, 133, 308, 25, 8, UI_PANEL, UI_LINE);
    display_draw_text(10, 135, "N", DISPLAY_FONT_TINY, UI_GOLD, UI_PANEL,
                      DISPLAY_DATUM_TL);
    scan_graph(18, 137, 294, 18, snr, count, UI_GOLD);

    /* 커서 위치 (자유 이동) */
    int gx = 18;
    int gw = 294;
    int px = gx;
    if (count > 1) {
        int c = cursor;
        if (c < 0) { c = 0; }
        if (c >= count) { c = count - 1; }
        px = gx + c * (gw - 1) / (count - 1);
    }
    display_fill_rect((int16_t)px, 110, 1, 18, UI_FG);
    display_fill_rect((int16_t)px, 137, 1, 18, UI_FG);

    char lo[16];
    char hi[16];
    scan_freq_str(min_hz, lo, sizeof(lo));
    scan_freq_str(max_hz, hi, sizeof(hi));
    display_draw_text(10, 160, lo, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    display_draw_text(UI_W - 10, 160, hi, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);
    if (done) {
        bool empty = true;
        for (int i = 0; i < count; ++i) {
            if (rssi[i] > 10 || snr[i] > 5) {
                empty = false;
                break;
            }
        }
        if (empty) {
            display_draw_text(UI_W / 2, 148, "NO STATIONS",
                              DISPLAY_FONT_TINY, UI_WARN, UI_BG,
                              DISPLAY_DATUM_TC);
        }
    }
}

void ui_draw_hint(const ui_state_t *s, const char *toast)
{
    if (!toast) {
        if (s->ui_mode == UI_LAYOUT_DX) {
            ui_draw_all(s);
            return;
        }
        display_fill_rect(0, SIG_Y, UI_W, UI_H - SIG_Y, UI_BG);
        ui_draw_smeter(s);
        ui_draw_rds(s);
        return;
    }
    panel(60, 122, 200, 40, 16, UI_PANEL, UI_LINE);
    char buf[32];
    fit_text(buf, sizeof(buf), toast, DISPLAY_FONT_SMALL, 184);
    display_draw_text(160, 134, buf, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TC);
}

void ui_draw_quick(const ui_state_t *s, int cursor)
{
    ui_draw_all(s);

    uint8_t list[UI_QUICK_MAX];
    int n = 0;
    for (int i = 0; i < UI_QA_COUNT && n < UI_QUICK_MAX; ++i) {
        if (s_quick_mask & (1u << i)) {
            list[n++] = (uint8_t)i;
        }
    }
    if (n == 0) {
        return;
    }
    if (cursor < 0 || cursor >= n) {
        cursor = 0;
    }

    int h = 34 + n * 14;
    int y0 = (UI_H - h) / 2;
    if (y0 < HDR_H + 2) {
        y0 = HDR_H + 2;
    }

    display_fill_round_rect(44, (int16_t)y0, 232, (uint16_t)h, 18, UI_PANEL);
    display_draw_round_rect(44, (int16_t)y0, 232, (uint16_t)h, 18, UI_LINE);

    display_draw_text(60, (int16_t)(y0 + 8), "QUICK", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d/%d", cursor + 1, n);
    display_draw_text(260, (int16_t)(y0 + 8), buf, DISPLAY_FONT_TINY,
                      UI_ACC_HI, UI_PANEL, DISPLAY_DATUM_TR);

    for (int i = 0; i < n; ++i) {
        int y = y0 + 24 + i * 14;
        bool cur = (i == cursor);
        if (cur) {
            display_fill_round_rect(56, (int16_t)y, 208, 12, 4, UI_HSEL);
            display_fill_round_rect(60, (int16_t)(y + 3), 2, 6, 1, UI_ACC);
        }
        display_draw_text(68, (int16_t)(y + 2), QA_LABELS[list[i]],
                          DISPLAY_FONT_TINY, cur ? UI_FG : UI_MUTED,
                          cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TL);
    }
}

void ui_draw_quick_cfg(int cursor, int top, uint16_t mask)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "QUICK SET", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    int cnt = 0;
    for (int i = 0; i < UI_QA_COUNT; ++i) {
        if (mask & (1u << i)) {
            ++cnt;
        }
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", cnt, UI_QUICK_MAX);
    display_draw_text(UI_W - 8, 7, buf, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TR);
    display_fill_rect(0, HDR_LINE_Y, UI_W, 1, UI_LINE);

    const int y0 = 24;
    const int rh = 26;
    for (int i = 0; i < 5; ++i) {
        int idx = top + i;
        if (idx >= UI_QA_COUNT) {
            break;
        }
        int y = y0 + i * rh;
        bool cur = (idx == cursor);
        if (cur) {
            display_fill_round_rect(6, (int16_t)y, UI_W - 12, 24, 9, UI_HSEL);
            display_fill_round_rect(10, (int16_t)(y + 7), 2, 10, 1, UI_ACC);
        }
        display_draw_text(24, (int16_t)(y + 4), QA_LABELS[idx],
                          DISPLAY_FONT_SMALL, cur ? UI_FG : UI_ACC_HI,
                          cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TL);
        bool on = (mask & (1u << idx)) != 0;
        display_draw_text(UI_W - 16, (int16_t)(y + 7), on ? "ON" : "OFF",
                          DISPLAY_FONT_TINY, on ? UI_OK : UI_MUTED,
                          cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TR);
    }

    display_draw_text(UI_W / 2, UI_H - 8,
                      "turn: move   press: toggle   hold: back",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

static void ui_fmt_step(const ui_state_t *s, char *buf, size_t n);

static void dx_kv(int x, int xr, int y, const char *label, const char *val,
                  uint16_t vcol)
{
    display_draw_text((int16_t)x, (int16_t)y, label, DISPLAY_FONT_TINY,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
    if (val) {
        display_draw_text((int16_t)xr, (int16_t)y, val, DISPLAY_FONT_TINY,
                          vcol, UI_BG, DISPLAY_DATUM_TR);
    }
}

static void dx_sig_geom(int *fy, int *div1, int *rssi, int *snr, int *div2)
{
    uint16_t fh = display_font_height(DISPLAY_FONT_LARGE);
    int f = 24;
    int yb = f + (int)fh + 2;
    *fy = f;
    *div1 = yb + 16;
    *rssi = *div1 + 2;
    *snr = *div1 + 12;
    *div2 = *div1 + 22;
}

void ui_draw_dx_signal(const ui_state_t *s)
{
    int fy, div1, rssi, snr, div2;
    dx_sig_geom(&fy, &div1, &rssi, &snr, &div2);
    (void)fy;

    display_fill_rect(0, (int16_t)div1, UI_W, (uint16_t)(div2 - div1), UI_BG);
    display_fill_rect(0, (int16_t)div1, UI_W, 1, UI_LINE);

    char v1[16];
    char v2[16];
    if (s->have_rsq) {
        snprintf(v1, sizeof(v1), "%d", s->rsq.rssi);
        snprintf(v2, sizeof(v2), "%u", s->rsq.snr);
    } else {
        snprintf(v1, sizeof(v1), "--");
        snprintf(v2, sizeof(v2), "--");
    }
    display_draw_text(8, (int16_t)rssi, "RSSI", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(48, rssi + 1, 192, 6, s->have_rsq ? s->rsq.rssi : 0, 63, UI_OK);
    display_draw_text(312, (int16_t)rssi, v1, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);
    display_draw_text(8, (int16_t)snr, "SNR", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(48, snr + 1, 192, 6, s->have_rsq ? s->rsq.snr : 0, 63,
        (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_ACC);
    display_draw_text(312, (int16_t)snr, v2, DISPLAY_FONT_TINY,
                      (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_FG,
                      UI_BG, DISPLAY_DATUM_TR);
    display_fill_rect(0, (int16_t)div2, UI_W, 1, UI_LINE);
}

static void ui_draw_all_dx(const ui_state_t *s)
{
    display_fill_screen(UI_BG);
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);

    char buf[96];
    char vb[8][24];
    int vi = 0;
    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));

    char num[20];
    const char *unit;
    fmt_freq(s, num, sizeof(num), &unit);
    int fy;
    int y_div1;
    int y_rssi;
    int y_snr;
    int y_div2;
    dx_sig_geom(&fy, &y_div1, &y_rssi, &y_snr, &y_div2);
    int y_badge = y_div1 - 16;
    int y_set = y_div2 + 2;

    display_draw_text(10, (int16_t)fy, num, DISPLAY_FONT_LARGE, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    display_draw_text(10 + (int)display_text_width(num, DISPLAY_FONT_LARGE) + 5,
                      (int16_t)(fy + 2), unit, DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);

    int rx = 164;
    display_draw_text(rx, 26, band_table[s->band_idx].name, DISPLAY_FONT_TINY,
                      UI_ACC_HI, UI_BG, DISPLAY_DATUM_TL);
    display_draw_text(rx + display_text_width(band_table[s->band_idx].name,
                      DISPLAY_FONT_TINY) + 6, 26, mode_str(s->mode),
                      DISPLAY_FONT_TINY, UI_FG, UI_BG, DISPLAY_DATUM_TL);
    snprintf(buf, sizeof(buf), "BW %s  STEP %s", bw_label(s), stp);
    display_draw_text(rx, 38, buf, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    int bxs = 10;
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bxs, y_badge, p, 46, UI_BLACK, UI_ACC);
        bxs += display_text_width(p, DISPLAY_FONT_TINY) + 16;
    }
    if (s->ats_slot >= 0) {
        char a2[16];
        snprintf(a2, sizeof(a2), "A%d", s->ats_slot + 1);
        pill(bxs, y_badge, a2, 46, UI_BLACK, UI_GOLD);
    }
    if (s->have_rsq) {
        snprintf(buf, sizeof(buf), "FO %+d", s->rsq.freq_offset);
        display_draw_text(UI_W - 8, (int16_t)(y_badge + 3), buf,
                          DISPLAY_FONT_TINY,
                          s->rsq.afc_rail ? UI_WARN : UI_MUTED, UI_BG,
                          DISPLAY_DATUM_TR);
    }

    char v1[16];
    char v2[16];
    if (s->have_rsq) {
        snprintf(v1, sizeof(v1), "%d", s->rsq.rssi);
        snprintf(v2, sizeof(v2), "%u", s->rsq.snr);
    } else {
        snprintf(v1, sizeof(v1), "--");
        snprintf(v2, sizeof(v2), "--");
    }
    display_draw_text(8, (int16_t)y_rssi, "RSSI", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(48, y_rssi + 1, 192, 6, s->have_rsq ? s->rsq.rssi : 0, 63, UI_OK);
    display_draw_text(312, (int16_t)y_rssi, v1, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);
    display_draw_text(8, (int16_t)y_snr, "SNR", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(48, y_snr + 1, 192, 6, s->have_rsq ? s->rsq.snr : 0, 63,
        (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_ACC);
    display_draw_text(312, (int16_t)y_snr, v2, DISPLAY_FONT_TINY,
                      (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_FG,
                      UI_BG, DISPLAY_DATUM_TR);
    display_fill_rect(0, (int16_t)y_div2, UI_W, 1, UI_LINE);

    typedef struct { const char *l; const char *v; uint16_t c; } kv_t;
    kv_t L[6];
    kv_t R[6];
    int ln = 0;
    int rn = 0;

    int sq = ui_mode_slot(s->mode);
    char sqv[24];
    snprintf(sqv, sizeof(sqv), "%s %u",
             s_squelch_type[sq] ? "SNR" : "RSSI", s_squelch_thr[sq]);
    uint16_t sqcol = (s_squelch_on && s_squelch_muted) ? UI_WARN : UI_FG;

    L[ln].l = "AGC"; L[ln].v = s_agc_on ? "ON" : "OFF"; L[ln].c = UI_FG; ++ln;
    snprintf(vb[vi], sizeof(vb[vi]), "%u", s_avc);
    L[ln].l = "AVC"; L[ln].v = vb[vi++]; L[ln].c = UI_FG; ++ln;
    snprintf(vb[vi], sizeof(vb[vi]), "%u", s_softmute);
    L[ln].l = "SM"; L[ln].v = vb[vi++]; L[ln].c = UI_FG; ++ln;
    if (band_is_ssb(s->mode)) {
        snprintf(vb[vi], sizeof(vb[vi]), "%+ld", (long)s_bfo);
        L[ln].l = "BFO"; L[ln].v = vb[vi++]; L[ln].c = UI_FG; ++ln;
    }
    L[ln].l = "SQ";
    L[ln].v = s_squelch_on ? sqv : "OFF";
    L[ln].c = s_squelch_on ? sqcol : UI_MUTED;
    ++ln;

    if (s->mode == RADIO_MODE_FM) {
        R[rn].l = "STEREO";
        R[rn].v = s_stereo_force ? "MONO" : "AUTO"; R[rn].c = UI_FG; ++rn;
        R[rn].l = "DEEMPH";
        R[rn].v = s_deemph_75 ? "75us" : "50us"; R[rn].c = UI_FG; ++rn;
    }
    snprintf(vb[vi], sizeof(vb[vi]), "%u", s->volume);
    R[rn].l = "VOLUME"; R[rn].v = vb[vi++]; R[rn].c = UI_FG; ++rn;
    if (s->have_rsq) {
        R[rn].l = "AFC"; R[rn].v = s->rsq.afc_rail ? "RA" : "OK";
        R[rn].c = (s->rsq.afc_rail || s->rsq.freq_offset != 0)
                  ? UI_RED : UI_MUTED; ++rn;
        R[rn].l = "SOFTMUTE"; R[rn].v = s->rsq.soft_mute ? "ON" : "OFF";
        R[rn].c = s->rsq.soft_mute ? UI_WARN : UI_FG; ++rn;
    } else {
        R[rn].l = "AFC"; R[rn].v = "--"; R[rn].c = UI_MUTED; ++rn;
        R[rn].l = "SOFTMUTE"; R[rn].v = "--"; R[rn].c = UI_MUTED; ++rn;
    }

    display_fill_rect(160, (int16_t)y_set, 1, 46, UI_LINE);
    for (int i = 0; i < ln; ++i) {
        dx_kv(8, 152, y_set + i * 10, L[i].l, L[i].v, L[i].c);
    }
    for (int i = 0; i < rn; ++i) {
        dx_kv(168, 312, y_set + i * 10, R[i].l, R[i].v, R[i].c);
    }

    int y_foot = y_set + 50;
    display_fill_rect(0, (int16_t)y_foot, UI_W, 1, UI_LINE);

    const rds_info_t *r = (s->mode == RADIO_MODE_FM && s->rds_on) ? s->rds
                                                                  : NULL;
    if (s->seeking) {
        int pct = 0;
        if (s->sw_max > 0) {
            pct = (s->sw_steps * 100) / s->sw_max;
            if (pct > 100) { pct = 100; }
            if (pct < 0) { pct = 0; }
        }
        snprintf(buf, sizeof(buf), "SCAN  %d%%   %d/%d", pct, s->sw_steps,
                 s->sw_max);
        display_draw_text(UI_W / 2, (int16_t)(y_foot + 8), buf,
                          DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                          DISPLAY_DATUM_TC);
    } else if (s->mode != RADIO_MODE_FM) {
        const char *nm = (s->station && s->station[0]) ? s->station : "...";
        char lb[80];
        snprintf(lb, sizeof(lb), "EiBi  %s", nm);
        fit_text(buf, sizeof(buf), lb, DISPLAY_FONT_TINY, UI_W - 16);
        display_draw_text(8, (int16_t)(y_foot + 3), buf, DISPLAY_FONT_TINY,
                          UI_OK, UI_BG, DISPLAY_DATUM_TL);
    } else if (r) {
        const char *pty = rds_pty_name(r->pty);
        char pi[12];
        snprintf(pi, sizeof(pi), "%04X", r->pi);
        snprintf(buf, sizeof(buf), "PS %s  PI %s  PTY %s  AF %u",
                 (r->ps_valid && r->ps[0]) ? r->ps : "--", pi,
                 (pty && pty[0]) ? pty : "-", r->af_count);
        char lb[96];
        fit_text(lb, sizeof(lb), buf, DISPLAY_FONT_TINY, UI_W - 16);
        display_draw_text(8, (int16_t)(y_foot + 3), lb, DISPLAY_FONT_TINY,
                          UI_FG, UI_BG, DISPLAY_DATUM_TL);

        char rb[96];
        snprintf(rb, sizeof(rb), "RT %s",
                 (r->rt_valid && r->rt[0]) ? r->rt : "...");
        fit_text(buf, sizeof(buf), rb, DISPLAY_FONT_TINY,
                 r->ct_valid ? UI_W - 90 : UI_W - 16);
        display_draw_text(8, (int16_t)(y_foot + 13), buf, DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
        if (r->ct_valid) {
            snprintf(buf, sizeof(buf), "CT %02u:%02u", r->hour, r->minute);
            display_draw_text(UI_W - 8, (int16_t)(y_foot + 13), buf,
                              DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                              DISPLAY_DATUM_TR);
        }
    } else {
        display_draw_text(8, (int16_t)(y_foot + 3),
                          s->rds_on ? "RDS ..." : "RDS off",
                          DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                          DISPLAY_DATUM_TL);
    }
}

#ifndef CP_W
#define CP_W 224
#endif

void ui_draw_dx_signal2(const ui_state_t *s)
{
    panel(8, 84, 304, 34, 10, UI_PANEL, UI_HL);

    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    char v[12];

    display_draw_text(16, 88, "RSSI", DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    bar(52, 90, 188, 6, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_draw_text(302, 88, v, DISPLAY_FONT_TINY, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TR);

    display_draw_text(16, 102, "SNR", DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    bar(52, 104, 188, 6, snr, 63, snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_draw_text(302, 102, v, DISPLAY_FONT_TINY,
                      snr < 10 ? UI_WARN : UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TR);
}

static void ui_draw_all_dx2(const ui_state_t *s)
{
    display_fill_screen(UI_BG);
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);

    char num[20];
    const char *unit = "";
    fmt_freq(s, num, sizeof(num), &unit);

    panel(8, 24, 304, 56, 12, UI_PANEL, UI_HL);

    char meta[48];
    band_meta(s, meta, sizeof(meta));
    display_draw_text(18, 29, meta, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);

    int bx = 312;
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(bx - 46, 27, a, 46, UI_BLACK, UI_GOLD);
        bx -= 52;
    }
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bx - 46, 27, p, 46, UI_BLACK, UI_ACC);
        bx -= 52;
    }
    if (s->patch_missing) {
        pill(bx - 46, 27, "SSB!", 46, UI_WARN, UI_BG);
    }

    display_draw_text(18, 40, num, DISPLAY_FONT_LARGE, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    uint16_t fw = display_text_width(num, DISPLAY_FONT_LARGE);
    display_draw_text((int16_t)(18 + fw + 5), 52, unit, DISPLAY_FONT_SMALL,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);

    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));
    char line[64];
    snprintf(line, sizeof(line), "BW %s   STEP %s", bw_label(s), stp);
    display_draw_text(18, 70, line, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);

    if (s->seeking && s->sw_max > 0) {
        char sk[24];
        int pct = s->sw_steps * 100 / s->sw_max;
        if (pct > 100) { pct = 100; }
        snprintf(sk, sizeof(sk), "SEEK %d%%", pct);
        display_draw_text(312, 70, sk, DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_PANEL, DISPLAY_DATUM_TR);
    } else if (s->stereo) {
        display_draw_text(312, 70, "STEREO", DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_PANEL, DISPLAY_DATUM_TR);
    }

    ui_draw_dx_signal2(s);

    panel(8, 124, 304, 42, 10, UI_PANEL, UI_HL);
    display_fill_rect(104, 128, 1, 34, UI_LINE);
    display_fill_rect(200, 128, 1, 34, UI_LINE);

    char vals[6][20];
    snprintf(vals[0], sizeof(vals[0]), "%s", mode_str(s->mode));
    snprintf(vals[1], sizeof(vals[1]), "%s", bw_label(s));
    snprintf(vals[2], sizeof(vals[2]), "%s", stp);
    snprintf(vals[3], sizeof(vals[3]), "%s", s_agc_on ? "ON" : "OFF");
    snprintf(vals[4], sizeof(vals[4]), "%u", s_avc);
    int sq = ui_mode_slot(s->mode);
    if (s_squelch_on) {
        snprintf(vals[5], sizeof(vals[5]), "%s %u",
                 s_squelch_type[sq] ? "SNR" : "RSSI", s_squelch_thr[sq]);
    } else {
        snprintf(vals[5], sizeof(vals[5]), "OFF");
    }

    static const char *const names[6] = {
        "MODE", "BW", "STEP", "AGC", "AVC", "SQ"
    };
    for (int i = 0; i < 6; ++i) {
        int col = i % 3;
        int row = i / 3;
        int x = 16 + col * 96;
        int y = 130 + row * 16;
        display_draw_text((int16_t)x, (int16_t)y, names[i],
                          DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
        display_draw_text((int16_t)(x + 42), (int16_t)y, vals[i],
                          DISPLAY_FONT_TINY, UI_FG, UI_PANEL,
                          DISPLAY_DATUM_TL);
    }
}

static void ui_draw_panel_dx2(const ui_state_t *s)
{
    display_fill_rect(0, 0, CP_W, UI_H, UI_BG);
    display_fill_round_rect(5, 7, 2, 8, 1, UI_ACC);
    if (s->clock[0]) {
        display_draw_text(12, 6, s->clock, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                          DISPLAY_DATUM_TL);
    }
    draw_status_right(s, CP_W);
    display_fill_rect(0, 21, CP_W, 1, UI_LINE);

    char num[20];
    const char *unit = "";
    fmt_freq(s, num, sizeof(num), &unit);

    panel(8, 26, CP_W - 16, 54, 10, UI_PANEL, UI_HL);
    display_draw_text(16, 32, num, DISPLAY_FONT_LARGE, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    uint16_t fw = display_text_width(num, DISPLAY_FONT_LARGE);
    display_draw_text((int16_t)(16 + fw + 4), 44, unit, DISPLAY_FONT_SMALL,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);

    char meta[32];
    band_meta(s, meta, sizeof(meta));
    display_draw_text(16, 66, meta, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);

    panel(8, 84, CP_W - 16, 52, 10, UI_PANEL, UI_HL);
    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    char v[12];

    display_draw_text(16, 90, "RSSI", DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    bar(52, 92, CP_W - 16 - 60, 6, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_draw_text(CP_W - 10, 90, v, DISPLAY_FONT_TINY, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TR);

    display_draw_text(16, 108, "SNR", DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    bar(52, 110, CP_W - 16 - 60, 6, snr, 63, snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_draw_text(CP_W - 10, 108, v, DISPLAY_FONT_TINY,
                      snr < 10 ? UI_WARN : UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TR);

    int bx = CP_W - 8;
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(bx - 46, 142, a, 46, UI_BLACK, UI_GOLD);
        bx -= 52;
    }
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bx - 46, 142, p, 46, UI_BLACK, UI_ACC);
    }
    if (s->seeking && s->sw_max > 0) {
        char sk[24];
        int pct = s->sw_steps * 100 / s->sw_max;
        if (pct > 100) { pct = 100; }
        snprintf(sk, sizeof(sk), "SEEK %d%%", pct);
        display_draw_text(16, 145, sk, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                          DISPLAY_DATUM_TL);
    } else if (s->rds && s->rds->ps_valid && s->rds->ps[0]) {
        display_draw_text(16, 145, s->rds->ps, DISPLAY_FONT_TINY, UI_FG,
                          UI_BG, DISPLAY_DATUM_TL);
    }
}

void ui_draw_dx_signal3(const ui_state_t *s)
{
    display_fill_rect(8, 88, 304, 1, UI_LINE);

    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    char v[12];

    display_draw_text(12, 94, "RSSI", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(56, 98, 196, 8, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_fill_rect(286, 94, 28, 16, UI_BG);
    display_draw_text(312, 94, v, DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);

    display_draw_text(12, 114, "SNR", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(56, 118, 196, 8, snr, 63, snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_fill_rect(286, 114, 28, 16, UI_BG);
    display_draw_text(312, 114, v, DISPLAY_FONT_SMALL,
                      snr < 10 ? UI_WARN : UI_FG, UI_BG, DISPLAY_DATUM_TR);
}

static void band_meta(const ui_state_t *s, char *buf, size_t n)
{
    const char *bn = band_table[s->band_idx].name;
    if (strcmp(bn, "SW") == 0) {
        uint32_t khz = s->freq_hz / 1000;
        int m = khz ? (int)((300000u + khz / 2) / khz) : 0;
        snprintf(buf, n, "%s %s %02dM", bn, mode_str(s->mode), m);
    } else {
        snprintf(buf, n, "%s %s", bn, mode_str(s->mode));
    }
}

static void draw_saved_badges(const ui_state_t *s)
{
    char vb[24];
    snprintf(vb, sizeof(vb), "VOL %u  BRT %u", s->volume, s_brightness);
    int bx = 312 - display_text_width(vb, DISPLAY_FONT_TINY) - 8;
    if (bx < 52) {
        bx = 52;
    }
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(bx - 46, 24, a, 46, UI_BLACK, UI_GOLD);
        bx -= 52;
    }
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bx - 46, 24, p, 46, UI_BLACK, UI_ACC);
    }
}

void ui_draw_band_meta(const ui_state_t *s)
{
    display_fill_rect(0, 22, META_CLEAR_W, 18, UI_BG);

    char meta[48];
    band_meta(s, meta, sizeof(meta));
    uint16_t mw = display_draw_text(12, 24, meta, DISPLAY_FONT_SMALL,
                                    UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
    if (s->mode == RADIO_MODE_FM && s->stereo) {
        display_draw_text((int16_t)(12 + mw + 6), 29, "STEREO",
                          DISPLAY_FONT_TINY, UI_OK, UI_BG, DISPLAY_DATUM_TL);
    }
}

static void draw_freq_text(int x, int y, const char *num, display_font_t font,
                           uint16_t bg)
{
    display_draw_text((int16_t)x, (int16_t)y, num, font, UI_FG, bg,
                      DISPLAY_DATUM_TL);
    if (!s_digit_mode) {
        return;
    }
    int dc = 0;
    for (int i = 0; num[i]; ++i) {
        if (isdigit((unsigned char)num[i])) {
            dc++;
        }
    }
    int target = dc - 1 - s_digit_place;
    if (target < 0 || target >= dc) {
        return;
    }
    int k = 0;
    int ci = -1;
    for (int i = 0; num[i]; ++i) {
        if (isdigit((unsigned char)num[i])) {
            if (k == target) {
                ci = i;
                break;
            }
            k++;
        }
    }
    if (ci < 0) {
        return;
    }
    char pre[24];
    int pl = ci;
    if (pl > (int)sizeof(pre) - 1) {
        pl = (int)sizeof(pre) - 1;
    }
    memcpy(pre, num, (size_t)pl);
    pre[pl] = '\0';
    int dx = x + display_text_width(pre, font);
    char one[2] = { num[ci], '\0' };
    display_draw_text((int16_t)dx, (int16_t)y, one, font, UI_BLUE, bg,
                      DISPLAY_DATUM_TL);
}

static void ui_draw_all_dx3(const ui_state_t *s)
{
    s_radio_draw_seq++;
    display_fill_screen(UI_BG);
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);

    char num[20];
    const char *unit = "";
    fmt_freq(s, num, sizeof(num), &unit);

    char meta[48];
    band_meta(s, meta, sizeof(meta));
    display_draw_text(12, 24, meta, DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    int bx = 312;
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(bx - 46, 24, a, 46, UI_BLACK, UI_GOLD);
        bx -= 52;
    }
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bx - 46, 24, p, 46, UI_BLACK, UI_ACC);
        bx -= 52;
    }
    if (s->patch_missing) {
        pill(bx - 46, 24, "SSB!", 46, UI_WARN, UI_BG);
    }

    display_font_t ff = DISPLAY_FONT_HERO;
    uint16_t fw = display_text_width(num, ff);
    if (fw > 280) {
        ff = DISPLAY_FONT_LARGE;
        fw = display_text_width(num, ff);
    }
    draw_freq_text(12, 40, num, ff, UI_BG);
    int fh = (ff == DISPLAY_FONT_HERO) ? 48 : 32;
    display_draw_text((int16_t)(12 + fw + 5), (int16_t)(40 + fh / 3), unit,
                      DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    ui_draw_dx_signal3(s);

    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));
    char l1[64];
    snprintf(l1, sizeof(l1), "BW %s    STEP %s    AGC %s", bw_label(s), stp,
             s_agc_on ? "ON" : "OFF");
    display_fill_rect(12, 134, 300, 16, UI_BG);
    display_draw_text(12, 134, l1, DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    char l2[64];
    if (s->seeking && s->sw_max > 0) {
        int pct = s->sw_steps * 100 / s->sw_max;
        if (pct > 100) { pct = 100; }
        snprintf(l2, sizeof(l2), "SEEK %d%%", pct);
    } else {
        int sq = ui_mode_slot(s->mode);
        char sqv[20];
        if (s_squelch_on) {
            snprintf(sqv, sizeof(sqv), "%s %u",
                     s_squelch_type[sq] ? "SNR" : "RSSI", s_squelch_thr[sq]);
        } else {
            snprintf(sqv, sizeof(sqv), "OFF");
        }
        snprintf(l2, sizeof(l2), "AVC %u    SQ %s%s", s_avc, sqv,
                 s->stereo ? "    STEREO" : "");
    }
    display_fill_rect(12, 152, 300, 16, UI_BG);
    display_draw_text(12, 152, l2, DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
}

static void ui_draw_panel_dx3(const ui_state_t *s)
{
    display_fill_rect(0, 0, CP_W, UI_H, UI_BG);
    display_fill_round_rect(5, 7, 2, 8, 1, UI_ACC);
    if (s->clock[0]) {
        display_draw_text(12, 6, s->clock, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                          DISPLAY_DATUM_TL);
    }
    draw_status_right(s, CP_W);
    display_fill_rect(0, 21, CP_W, 1, UI_LINE);

    char num[20];
    const char *unit = "";
    fmt_freq(s, num, sizeof(num), &unit);

    display_font_t ff = DISPLAY_FONT_HERO;
    uint16_t fw = display_text_width(num, ff);
    if (fw > CP_W - 24) {
        ff = DISPLAY_FONT_LARGE;
        fw = display_text_width(num, ff);
    }
    int fh = (ff == DISPLAY_FONT_HERO) ? 48 : 32;
    draw_freq_text(12, 28, num, ff, UI_PANEL);
    display_draw_text((int16_t)(12 + fw + 4), (int16_t)(28 + fh / 3), unit,
                      DISPLAY_FONT_SMALL, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);

    int my = 28 + fh + 2;
    char meta[32];
    band_meta(s, meta, sizeof(meta));
    display_draw_text(12, (int16_t)my, meta, DISPLAY_FONT_SMALL, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);

    int dy = my + 18;
    display_fill_rect(8, (int16_t)dy, CP_W - 16, 1, UI_LINE);

    int ry = dy + 6;
    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    if (rssi > 63) { rssi = 63; }
    if (snr > 63) { snr = 63; }
    char v[12];

    display_draw_text(12, (int16_t)ry, "RSSI", DISPLAY_FONT_SMALL, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(52, (int16_t)(ry + 4), CP_W - 52 - 40, 8, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_fill_rect(CP_W - 40, (int16_t)ry, 36, 16, UI_BG);
    display_draw_text(CP_W - 8, (int16_t)ry, v, DISPLAY_FONT_SMALL, UI_FG,
                      UI_BG, DISPLAY_DATUM_TR);

    ry += 20;
    display_draw_text(12, (int16_t)ry, "SNR", DISPLAY_FONT_SMALL, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(52, (int16_t)(ry + 4), CP_W - 52 - 40, 8, snr, 63,
        snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_fill_rect(CP_W - 40, (int16_t)ry, 36, 16, UI_BG);
    display_draw_text(CP_W - 8, (int16_t)ry, v, DISPLAY_FONT_SMALL,
                      snr < 10 ? UI_WARN : UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);

    int by = ry + 20;
    int bx = CP_W - 8;
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(bx - 46, (int16_t)by, a, 46, UI_BLACK, UI_GOLD);
        bx -= 52;
    }
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bx - 46, (int16_t)by, p, 46, UI_BLACK, UI_ACC);
    }
    if (s->seeking && s->sw_max > 0) {
        char sk[24];
        int pct = s->sw_steps * 100 / s->sw_max;
        if (pct > 100) { pct = 100; }
        snprintf(sk, sizeof(sk), "SEEK %d%%", pct);
        display_draw_text(12, (int16_t)by, sk, DISPLAY_FONT_SMALL, UI_ACC_HI,
                          UI_BG, DISPLAY_DATUM_TL);
    } else if (s->rds && s->rds->ps_valid && s->rds->ps[0]) {
        display_draw_text(12, (int16_t)by, s->rds->ps, DISPLAY_FONT_SMALL,
                          UI_FG, UI_BG, DISPLAY_DATUM_TL);
    }
}

static void adv_hist_add(const ui_state_t *s)
{
    if (!s->have_rsq) {
        return;
    }
    int r = s->rsq.rssi;
    int n = s->rsq.snr;
    if (r > 63) { r = 63; }
    if (n > 63) { n = 63; }
    s_hist_rssi[s_hist_pos] = (uint8_t)r;
    s_hist_snr[s_hist_pos] = (uint8_t)n;
    s_hist_pos = (s_hist_pos + 1) % ADV_HIST;
    if (s_hist_n < ADV_HIST) {
        s_hist_n++;
    }
}

void ui_draw_adv_signal4(const ui_state_t *s)
{
    adv_hist_add(s);

    char fnum[20];
    const char *funit = "";
    fmt_freq(s, fnum, sizeof(fnum), &funit);
    int ix = 12 + display_text_width(fnum, DISPLAY_FONT_LARGE) + 5 +
             display_text_width(funit, DISPLAY_FONT_SMALL) + 10;
    if (ix < 176) { ix = 176; }
    if (ix < 260) {
        int iw = 312 - ix;
        display_fill_rect((int16_t)ix, 36, (uint16_t)iw, 38, UI_BG);
        char ib[72];
        if (s->mode == RADIO_MODE_FM) {
            if (s->rds && s->rds->ps_valid) {
                fit_text(ib, sizeof(ib), s->rds->ps, DISPLAY_FONT_SMALL, iw);
                display_draw_text((int16_t)ix, 38, ib, DISPLAY_FONT_SMALL,
                                  UI_FG, UI_BG, DISPLAY_DATUM_TL);
            }
            if (s->rds && s->rds->rt_valid && s->rds->rt[0]) {
                fit_text(ib, sizeof(ib), s->rds->rt, DISPLAY_FONT_TINY, iw);
                display_draw_text((int16_t)ix, 56, ib, DISPLAY_FONT_TINY,
                                  UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
            }
            if (s->rds) {
                char cb[32];
                if (s->rds->ct_valid) {
                    snprintf(cb, sizeof(cb), "CT %02d:%02d",
                             (int)s->rds->hour, (int)s->rds->minute);
                } else {
                    snprintf(cb, sizeof(cb), "PTY %s",
                             rds_pty_name(s->rds->pty));
                }
                fit_text(ib, sizeof(ib), cb, DISPLAY_FONT_TINY, iw);
                display_draw_text((int16_t)ix, 64, ib, DISPLAY_FONT_TINY,
                                  UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
            }
        } else {
            int64_t utc = eibi_utc_now();
            int64_t utc_min = utc / 60;
            if (s->freq_hz != s_eibi_freq || utc_min != s_eibi_utc_min) {
                s_eibi_freq = s->freq_hz;
                s_eibi_utc_min = utc_min;
                s_eibi_name[0] = '\0';
                if (eibi_available()) {
                    eibi_lookup((uint16_t)(s->freq_hz / 1000), utc,
                                s_eibi_name, sizeof(s_eibi_name));
                }
            }
            if (s_eibi_name[0]) {
                fit_text(ib, sizeof(ib), s_eibi_name, DISPLAY_FONT_SMALL, iw);
                display_draw_text((int16_t)ix, 38, ib, DISPLAY_FONT_SMALL,
                                  UI_FG, UI_BG, DISPLAY_DATUM_TL);
            }
        }
    }

    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    if (rssi > 63) { rssi = 63; }
    if (snr > 63) { snr = 63; }
    char v[16];

    display_draw_text(12, 74, "RSSI", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(56, 78, 196, 8, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_fill_rect(286, 74, 28, 16, UI_BG);
    display_draw_text(312, 74, v, DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);

    display_draw_text(12, 94, "SNR", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(56, 98, 196, 8, snr, 63, snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_fill_rect(286, 94, 28, 16, UI_BG);
    display_draw_text(312, 94, v, DISPLAY_FONT_SMALL,
                      snr < 10 ? UI_WARN : UI_FG, UI_BG, DISPLAY_DATUM_TR);

    display_fill_rect(8, 118, 304, 1, UI_LINE);

    char pre[48];
    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));
    const char *afc = !s->have_rsq ? "--" : (s->rsq.afc_rail ? "RA" : "OK");
    bool afc_active = s->have_rsq &&
                      (s->rsq.afc_rail || s->rsq.freq_offset != 0);
    snprintf(pre, sizeof(pre), "BW %s  ST %s  AFC ", bw_label(s), stp);
    display_fill_rect(8, 122, 224, 14, UI_BG);
    display_draw_text(8, 122, pre, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    int vx = 8 + display_text_width(pre, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)vx, 122, afc, DISPLAY_FONT_TINY,
                      afc_active ? UI_RED : UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    char smv[8];
    if (s_softmute == 0) {
        snprintf(smv, sizeof(smv), "OFF");
    } else {
        snprintf(smv, sizeof(smv), "%u", s_softmute);
    }
    snprintf(pre, sizeof(pre), "BFO %+ld  AGC %s  SM ", (long)s_bfo,
             s_agc_on ? "ON" : "OFF");
    display_fill_rect(8, 136, 224, 14, UI_BG);
    display_draw_text(8, 136, pre, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    vx = 8 + display_text_width(pre, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)vx, 136, smv, DISPLAY_FONT_TINY,
                      s_softmute ? UI_RED : UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    int sq = ui_mode_slot(s->mode);
    char sqv[16];
    if (s_squelch_on) {
        snprintf(sqv, sizeof(sqv), "%s %u",
                 s_squelch_type[sq] ? "SNR" : "RSSI", s_squelch_thr[sq]);
    } else {
        snprintf(sqv, sizeof(sqv), "OFF");
    }
    snprintf(pre, sizeof(pre), "AVC %u  SQ ", s_avc);
    display_fill_rect(8, 150, 224, 14, UI_BG);
    display_draw_text(8, 150, pre, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    vx = 8 + display_text_width(pre, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)vx, 150, sqv, DISPLAY_FONT_TINY,
                      (s_squelch_on && s_squelch_muted) ? UI_RED : UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    if (s->stereo) {
        vx += display_text_width(sqv, DISPLAY_FONT_TINY) + 12;
        display_draw_text((int16_t)vx, 150, "STEREO", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
    }

    int gx = 236;
    int gy = 124;
    int gw = 76;
    int gh = 40;
    display_fill_rect(gx, gy, gw, gh, UI_BG);
    display_fill_rect(gx + gw / 2, gy, 1, (uint16_t)gh, UI_TRACK);
    display_fill_rect(gx, gy + gh - 1, gw, 1, UI_LINE);
    if (s_hist_n >= 2) {
        int start = (s_hist_pos - s_hist_n + ADV_HIST) % ADV_HIST;

        int px = -1;
        int py = 0;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_rssi[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_OK);
            }
            px = x;
            py = y;
        }

        px = -1;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_snr[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_ACC);
            }
            px = x;
            py = y;
        }
    }
}

static const char *eibi_current(const ui_state_t *s)
{
    int64_t utc = eibi_utc_now();
    int64_t utc_min = utc / 60;
    if (s->freq_hz != s_eibi_freq || utc_min != s_eibi_utc_min) {
        s_eibi_freq = s->freq_hz;
        s_eibi_utc_min = utc_min;
        s_eibi_name[0] = '\0';
        if (eibi_available()) {
            eibi_lookup((uint16_t)(s->freq_hz / 1000), utc,
                        s_eibi_name, sizeof(s_eibi_name));
        }
    }
    return s_eibi_name;
}

static void marquee_line(int x, int y, int w, const char *text,
                         display_font_t font, uint16_t fg, uint16_t bg,
                         int *off)
{
    if (!text || !*text || w <= 0) {
        return;
    }
    int tw = display_text_width(text, font);
    if (tw <= w) {
        display_draw_text((int16_t)x, (int16_t)y, text, font, fg, bg,
                          DISPLAY_DATUM_TL);
        return;
    }
    int span = tw + 24;
    *off = (*off + 1) % span;
    display_set_clip((int16_t)x, (int16_t)y, (uint16_t)w, 16);
    display_draw_text((int16_t)(x - *off), (int16_t)y, text, font, fg, bg,
                      DISPLAY_DATUM_TL);
    display_draw_text((int16_t)(x - *off + span), (int16_t)y, text, font, fg,
                      bg, DISPLAY_DATUM_TL);
    display_clear_clip();
}

static int adv_kv(int x, int y, const char *label, const char *value,
                  uint16_t value_color)
{
    display_draw_text((int16_t)x, (int16_t)y, label, DISPLAY_FONT_TINY, UI_FG,
                      UI_BG, DISPLAY_DATUM_TL);
    x += display_text_width(label, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)x, (int16_t)y, value, DISPLAY_FONT_TINY,
                      value_color, UI_BG, DISPLAY_DATUM_TL);
    return x + display_text_width(value, DISPLAY_FONT_TINY);
}

void ui_draw_adv_signal4b(const ui_state_t *s)
{
    s_radio_draw_seq++;
    adv_hist_add(s);

    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    if (rssi > 63) { rssi = 63; }
    if (snr > 63) { snr = 63; }
    char v[16];

    display_draw_text(12, 74, "RSSI", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(52, 78, 88, 8, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_fill_rect(144, 74, 34, 16, UI_BG);
    display_draw_text(174, 74, v, DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);

    display_draw_text(12, 94, "SNR", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(52, 98, 88, 8, snr, 63, snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_fill_rect(144, 94, 34, 16, UI_BG);
    display_draw_text(174, 94, v, DISPLAY_FONT_SMALL,
                      snr < 10 ? UI_WARN : UI_FG, UI_BG, DISPLAY_DATUM_TR);

    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));
    char pb[16];
    char smv[8];
    char sqv[16];
    display_fill_rect(176, 72, 138, 46, UI_BG);

    const char *afc = !s->have_rsq ? "--" : (s->rsq.afc_rail ? "RA" : "OK");
    bool afc_active = s->have_rsq &&
                      (s->rsq.afc_rail || s->rsq.freq_offset != 0);
    uint16_t afc_col = afc_active ? UI_RED : UI_MUTED;
    if (s_softmute == 0) {
        snprintf(smv, sizeof(smv), "OFF");
    } else {
        snprintf(smv, sizeof(smv), "%u", s_softmute);
    }
    int sq = ui_mode_slot(s->mode);
    if (s_squelch_on) {
        snprintf(sqv, sizeof(sqv), "%s%u",
                 s_squelch_type[sq] ? "S" : "R", s_squelch_thr[sq]);
    } else {
        snprintf(sqv, sizeof(sqv), "OFF");
    }

    int x = 180;
    x = adv_kv(x, 74, "BW ", bw_label(s), UI_MUTED);
    adv_kv(x + 6, 74, "ST ", stp, UI_MUTED);

    x = 180;
    snprintf(pb, sizeof(pb), "%+ld", (long)s_bfo);
    x = adv_kv(x, 90, "BFO ", pb, UI_MUTED);
    x = adv_kv(x + 6, 90, "AGC ", s_agc_on ? "ON" : "OFF", UI_MUTED);
    adv_kv(x + 6, 90, "SM ", smv, s_softmute ? UI_RED : UI_MUTED);

    x = 180;
    x = adv_kv(x, 106, "AFC ", afc, afc_col);
    snprintf(pb, sizeof(pb), "%u", s_avc);
    x = adv_kv(x + 6, 106, "AVC ", pb, UI_MUTED);
    adv_kv(x + 6, 106, "SQ ", sqv,
           (s_squelch_on && s_squelch_muted) ? UI_RED : UI_MUTED);

    display_fill_rect(8, 118, 304, 1, UI_LINE);

    int cy = 122;
    int ch = 46;
    int cw = 200;

    display_fill_rect(8, cy, cw, ch, UI_PANEL);
    display_draw_rect(8, cy, cw, ch, UI_LINE);
    if (s->mode == RADIO_MODE_FM) {
        static int rds_off;
        static int rt_off;
        display_draw_text(12, cy + 3, "RDS", DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_PANEL, DISPLAY_DATUM_TL);
        if (s->rds) {
            const rds_info_t *r = s->rds;
            int sx = 8 + cw - 4;
            if (r->eon_valid) {
                char eb[24];
                char tmp[24];
                if (r->eon_ps_valid && r->eon_ps[0]) {
                    snprintf(tmp, sizeof(tmp), "EON %s", r->eon_ps);
                } else {
                    snprintf(tmp, sizeof(tmp), "EON");
                }
                fit_text(eb, sizeof(eb), tmp, DISPLAY_FONT_TINY, 88);
                uint16_t ew = display_text_width(eb, DISPLAY_FONT_TINY);
                display_draw_text((int16_t)sx, cy + 3, eb,
                                  DISPLAY_FONT_TINY,
                                  r->eon_ta ? UI_WARN : UI_ACC_HI,
                                  UI_PANEL, DISPLAY_DATUM_TR);
                sx -= (int)ew + 6;
            }
            if (r->ta) {
                display_draw_text((int16_t)sx, cy + 3, "TA",
                                  DISPLAY_FONT_TINY, UI_WARN, UI_PANEL,
                                  DISPLAY_DATUM_TR);
                sx -= 14;
            }
            if (r->tp) {
                display_draw_text((int16_t)sx, cy + 3, "TP",
                                  DISPLAY_FONT_TINY, UI_OK, UI_PANEL,
                                  DISPLAY_DATUM_TR);
            }

            const char *ps = (r->ps_valid && r->ps[0]) ? r->ps : "--";
            marquee_line(12, cy + 16, cw - 8, ps, DISPLAY_FONT_TINY, UI_FG,
                         UI_PANEL, &rds_off);
            if (r->rt_valid && r->rt[0]) {
                marquee_line(12, cy + 30, cw - 8, s->rds->rt,
                             DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL, &rt_off);
            }
        } else {
            display_draw_text(12, cy + 16, "RDS --", DISPLAY_FONT_TINY,
                              UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);
        }
    } else {
        static int ei_off;
        display_draw_text(12, cy + 3, "EIBI", DISPLAY_FONT_TINY, UI_GOLD,
                          UI_PANEL, DISPLAY_DATUM_TL);
        const char *en = eibi_current(s);
        if (en && en[0]) {
            marquee_line(12, cy + 16, cw - 8, en, DISPLAY_FONT_TINY, UI_FG,
                         UI_PANEL, &ei_off);
        } else {
            display_draw_text(12, cy + 16, "NO DATA", DISPLAY_FONT_TINY,
                              UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);
        }
    }

    int sqx = 212;
    int sw = 98;
    display_fill_rect(sqx, cy, sw, ch, UI_PANEL);
    display_draw_rect(sqx, cy, sw, ch, UI_LINE);
    display_draw_text(sqx + 4, cy + 3, "SIG", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_PANEL, DISPLAY_DATUM_TL);

    int gx = sqx + 4;
    int gy = cy + 14;
    int gw = sw - 8;
    int gh = ch - 18;
    display_fill_rect(gx, gy, gw, gh, UI_PANEL);
    display_fill_rect(gx + gw / 2, gy, 1, (uint16_t)gh, UI_TRACK);
    display_fill_rect(gx, gy + gh - 1, gw, 1, UI_LINE);
    if (s_hist_n >= 2) {
        int start = (s_hist_pos - s_hist_n + ADV_HIST) % ADV_HIST;

        int px = -1;
        int py = 0;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_rssi[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_OK);
            }
            px = x;
            py = y;
        }

        px = -1;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_snr[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_ACC);
            }
            px = x;
            py = y;
        }
    }
}

static void ui_draw_all_adv4(const ui_state_t *s)
{
    s_radio_draw_seq++;
    display_fill_screen(UI_BG);
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);

    char num[20];
    const char *unit = "";
    fmt_freq(s, num, sizeof(num), &unit);

    ui_draw_band_meta(s);

    char vb[24];
    snprintf(vb, sizeof(vb), "VOL %u  BRT %u", s->volume, s_brightness);
    display_draw_text(312, 24, vb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);

    draw_saved_badges(s);

    draw_freq_text(12, 40, num, DISPLAY_FONT_LARGE, UI_BG);
    uint16_t fw = display_text_width(num, DISPLAY_FONT_LARGE);
    display_draw_text((int16_t)(12 + fw + 5), 50, unit, DISPLAY_FONT_SMALL,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TL);

    ui_draw_adv_signal4b(s);
}

void ui_draw_all(const ui_state_t *s)
{
    s_radio_draw_seq++;
    ui_draw_all_adv4(s);
    return;
    if (s->ui_mode == UI_LAYOUT_DX) {
        ui_draw_all_adv4(s);
        return;
    }
    ui_draw_all_dx3(s);
    return;
    display_fill_screen(UI_BG);
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);
    ui_draw_freq(s);
    chip_row(s);
    ui_draw_smeter(s);
    ui_draw_rds(s);
}

void ui_draw_radio_freq(const ui_state_t *s)
{
    s_radio_draw_seq++;
    ui_draw_all(s);
    return;
    if (s->ui_mode != UI_LAYOUT_DX) {
        ui_draw_all_dx3(s);
        return;
    }
    if (s->ui_mode == UI_LAYOUT_DX) {
        ui_draw_all(s);
    } else {
        ui_draw_freq(s);
    }
}


static void __attribute__((unused))
scrollbar(int x, int top, int count, int rows, int y0, int h)
{
    if (count <= rows) {
        return;
    }
    int thumb_h = h * rows / count;
    if (thumb_h < 12) {
        thumb_h = 12;
    }
    int thumb_y = y0 + (h - thumb_h) * top / (count - rows);
    display_fill_rect((int16_t)x, (int16_t)y0, 2, (uint16_t)h, UI_TRACK);
    display_fill_rect((int16_t)x, (int16_t)thumb_y, 2, (uint16_t)thumb_h,
                      UI_MUTED);
}


/* --------------------------------------------------- 좌우 분할 (가운데 70%) */

#define CP_W 224

static void ui_fmt_step(const ui_state_t *s, char *buf, size_t n)
{
    if (s->step_hz <= 0) {
        snprintf(buf, n, "BAND");
    } else if (s->step_hz == 10) {
        snprintf(buf, n, "0.01K");
    } else if (s->step_hz == 100) {
        snprintf(buf, n, "0.1K");
    } else if (s->step_hz % 1000 == 0) {
        snprintf(buf, n, "%dK", s->step_hz / 1000);
    } else {
        snprintf(buf, n, "%dH", s->step_hz);
    }
}

static void ui_draw_panel_dx(const ui_state_t *s)
{
    display_fill_rect(0, 0, CP_W, UI_H, UI_BG);
    char buf[64];
    char vb[32];
    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));

    if (s->clock[0]) {
        display_draw_text(6, 4, s->clock, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                          DISPLAY_DATUM_TL);
    }
    draw_status_right(s, CP_W);
    display_fill_rect(0, 20, CP_W, 1, UI_LINE);

    char num[20];
    const char *unit;
    fmt_freq(s, num, sizeof(num), &unit);
    uint16_t fh = display_font_height(DISPLAY_FONT_LARGE);
    int fy = 22;
    int y_badge = fy + (int)fh + 2;
    int y_div1 = y_badge + 14;
    int y_rssi = y_div1 + 2;
    int y_snr = y_div1 + 12;
    int y_div2 = y_div1 + 22;
    int y_set = y_div2 + 2;
    int y_foot = y_set + 50;

    uint16_t fw = display_text_width(num, DISPLAY_FONT_LARGE);
    display_draw_text(8, (int16_t)fy, num, DISPLAY_FONT_LARGE, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    display_draw_text((int16_t)(8 + fw + 4), (int16_t)(fy + 2), unit,
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL);

    int rx = 150;
    display_draw_text(rx, 24, band_table[s->band_idx].name, DISPLAY_FONT_TINY,
                      UI_ACC_HI, UI_BG, DISPLAY_DATUM_TL);
    display_draw_text(rx + display_text_width(band_table[s->band_idx].name,
                      DISPLAY_FONT_TINY) + 4, 24, mode_str(s->mode),
                      DISPLAY_FONT_TINY, UI_FG, UI_BG, DISPLAY_DATUM_TL);
    snprintf(buf, sizeof(buf), "BW %s", bw_label(s));
    display_draw_text(rx, 34, buf, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    snprintf(buf, sizeof(buf), "STEP %s", stp);
    display_draw_text(rx, 44, buf, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    int bxs = 8;
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bxs, y_badge, p, 40, UI_BLACK, UI_ACC);
        bxs += display_text_width(p, DISPLAY_FONT_TINY) + 14;
    }
    if (s->ats_slot >= 0) {
        char a2[16];
        snprintf(a2, sizeof(a2), "A%d", s->ats_slot + 1);
        pill(bxs, y_badge, a2, 40, UI_BLACK, UI_GOLD);
    }
    if (s->have_rsq) {
        snprintf(buf, sizeof(buf), "FO %+d", s->rsq.freq_offset);
        display_draw_text(CP_W - 6, (int16_t)(y_badge + 3), buf,
                          DISPLAY_FONT_TINY,
                          s->rsq.afc_rail ? UI_WARN : UI_MUTED, UI_BG,
                          DISPLAY_DATUM_TR);
    }
    display_fill_rect(0, (int16_t)y_div1, CP_W, 1, UI_LINE);

    if (s->have_rsq) {
        snprintf(vb, sizeof(vb), "%d", s->rsq.rssi);
        display_draw_text(CP_W - 8, (int16_t)y_rssi, vb, DISPLAY_FONT_TINY,
                          UI_OK, UI_BG, DISPLAY_DATUM_TR);
        snprintf(vb, sizeof(vb), "%u", s->rsq.snr);
        display_draw_text(CP_W - 8, (int16_t)y_snr, vb, DISPLAY_FONT_TINY,
                          s->rsq.snr < 10 ? UI_WARN : UI_ACC, UI_BG,
                          DISPLAY_DATUM_TR);
    }
    display_draw_text(8, (int16_t)y_rssi, "RSSI", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(48, y_rssi + 1, CP_W - 78, 5, s->have_rsq ? s->rsq.rssi : 0, 63,
        UI_OK);
    display_draw_text(8, (int16_t)y_snr, "SNR", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    bar(48, y_snr + 1, CP_W - 78, 5, s->have_rsq ? s->rsq.snr : 0, 63,
        (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_ACC);
    display_fill_rect(0, (int16_t)y_div2, CP_W, 1, UI_LINE);

    int sq = ui_mode_slot(s->mode);
    char sqv[24];
    snprintf(sqv, sizeof(sqv), "%s %u",
             s_squelch_type[sq] ? "SNR" : "RSSI", s_squelch_thr[sq]);
    uint16_t sqcol = (s_squelch_on && s_squelch_muted) ? UI_WARN : UI_FG;

    /* ??: ?? ???? */
    dx_kv(8, 104, y_set + 0, "AGC", s_agc_on ? "ON" : "OFF", UI_FG);
    snprintf(vb, sizeof(vb), "%u", s_avc);
    dx_kv(8, 104, y_set + 10, "AVC", vb, UI_FG);
    snprintf(vb, sizeof(vb), "%u", s_softmute);
    dx_kv(8, 104, y_set + 20, "SM", vb, UI_FG);
    if (band_is_ssb(s->mode)) {
        snprintf(vb, sizeof(vb), "%+ld", (long)s_bfo);
        dx_kv(8, 104, y_set + 30, "BFO", vb, UI_FG);
    } else {
        dx_kv(8, 104, y_set + 30, "SQ", s_squelch_on ? sqv : "OFF",
              s_squelch_on ? sqcol : UI_MUTED);
        dx_kv(8, 104, y_set + 40, "AME", "", UI_MUTED);
    }
    if (band_is_ssb(s->mode)) {
        dx_kv(8, 104, y_set + 40, "SQ", s_squelch_on ? sqv : "OFF",
              s_squelch_on ? sqcol : UI_MUTED);
    }

    /* ??: ?? */
    if (s->mode == RADIO_MODE_FM) {
        dx_kv(116, CP_W - 8, y_set + 0, "STEREO",
              s_stereo_force ? "MONO" : "AUTO", UI_FG);
        dx_kv(116, CP_W - 8, y_set + 10, "DEEMPH",
              s_deemph_75 ? "75us" : "50us", UI_FG);
    }
    snprintf(vb, sizeof(vb), "%u", s->volume);
    dx_kv(116, CP_W - 8, y_set + 20, "VOL", vb, UI_FG);
    if (s->have_rsq) {
        dx_kv(116, CP_W - 8, y_set + 30, "AFC",
              s->rsq.afc_rail ? "RA" : "OK",
              (s->rsq.afc_rail || s->rsq.freq_offset != 0)
              ? UI_RED : UI_MUTED);
        dx_kv(116, CP_W - 8, y_set + 40, "SMUTE",
              s->rsq.soft_mute ? "ON" : "OFF",
              s->rsq.soft_mute ? UI_WARN : UI_FG);
    } else {
        dx_kv(116, CP_W - 8, y_set + 30, "AFC", "--", UI_MUTED);
        dx_kv(116, CP_W - 8, y_set + 40, "SMUTE", "--", UI_MUTED);
    }

    display_fill_rect(0, (int16_t)y_foot, CP_W, 1, UI_LINE);

    const char *rds = NULL;
    if (s->mode != RADIO_MODE_FM) {
        rds = (s->station && s->station[0]) ? s->station : "...";
    } else if (s->rds_on && s->rds) {
        rds = (s->rds->rt_valid && s->rds->rt[0]) ? s->rds->rt : s->rds->ps;
        if (!rds || !rds[0]) {
            rds = "...";
        }
    } else {
        rds = "RDS off";
    }
    char rb[40];
    fit_text(rb, sizeof(rb), rds, DISPLAY_FONT_TINY, CP_W - 16);
    display_draw_text(6, (int16_t)(y_foot + 3), rb, DISPLAY_FONT_TINY, UI_FG,
                      UI_BG, DISPLAY_DATUM_TL);
}

void ui_draw_radio_panel(const ui_state_t *s)
{
    ui_draw_all_adv4(s);
    return;
    if (s->ui_mode == UI_LAYOUT_DX) {
        ui_draw_all_adv4(s);
        return;
    }
    ui_draw_all_dx3(s);
    return;
    if (s->ui_mode == UI_LAYOUT_DX) {
        ui_draw_panel_dx3(s);
        return;
    }
    display_fill_rect(0, 0, CP_W, UI_H, UI_BG);

    char buf[28];
    if (s->clock[0]) {
        display_draw_text(6, 6, s->clock, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                          DISPLAY_DATUM_TL);
    }

    const char *st = NULL;
    uint16_t stc = UI_MUTED;
    char seek_buf[20];
    if (s->seeking) {
        int pct = 0;
        if (s->sw_max > 0) {
            pct = (s->sw_steps * 100) / s->sw_max;
            if (pct > 100) { pct = 100; }
        }
        snprintf(seek_buf, sizeof(seek_buf), "SEEK%d%%", pct);
        st = seek_buf;
        stc = UI_ACC;
    } else if (s->patch_missing) {
        st = "SSB";
        stc = UI_WARN;
    }
    if (s->save_active) {
        display_draw_text(44, 6, "SAVE", DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                          DISPLAY_DATUM_TL);
    }
    if (st) {
        int sx = s->save_active ? 80 : 44;
        display_draw_text(sx, 6, st, DISPLAY_FONT_TINY, stc, UI_BG,
                          DISPLAY_DATUM_TL);
    }
    draw_status_right(s, CP_W);
    display_fill_rect(0, HDR_LINE_Y, CP_W, 1, UI_LINE);

    /* 주파수 카드: 상단 상태 바와 동일 위치/사이즈 */
    panel(4, HERO_Y, 216, HERO_H, 16, UI_PANEL, UI_HL);
    char num[20];
    const char *unit;
    fmt_freq(s, num, sizeof(num), &unit);
    display_font_t f = DISPLAY_FONT_HERO;
    if (display_text_width(num, f) > 204) {
        f = DISPLAY_FONT_LARGE;
    }
    uint16_t fh = display_font_height(f);
    int ty = HERO_Y + (HERO_H - (int)fh) / 2 + 1;
    display_draw_text(CP_W / 2, (int16_t)ty, num, f, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TC);
    display_draw_text(CP_W - 10, HERO_Y + 5, unit, DISPLAY_FONT_TINY, UI_MUTED,
                      UI_PANEL, DISPLAY_DATUM_TR);

    int badge_x = 12;
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(badge_x, HERO_Y + HERO_H - 19, p, 50, UI_BLACK, UI_ACC);
        badge_x += display_text_width(p, DISPLAY_FONT_TINY) + 18;
    }
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(badge_x, HERO_Y + HERO_H - 19, a, 50, UI_BLACK, UI_GOLD);
    }

    const char *chips[3];
    int chip_n = 0;
    const char *band_name = band_table[s->band_idx].name;
    const char *mode_name = mode_str(s->mode);
    if (strcmp(band_name, mode_name) != 0) {
        chips[chip_n++] = band_name;
    }
    chips[chip_n++] = mode_name;
    chips[chip_n++] = bw_label(s);

    int cx = 8;
    for (int i = 0; i < chip_n; ++i) {
        uint16_t tw = display_text_width(chips[i], DISPLAY_FONT_TINY);
        int w = (int)tw + 12;
        if (cx + w > CP_W - 6) {
            break;
        }
        pill(cx, META_Y, chips[i], w, i ? UI_MUTED : UI_ACC, UI_CELL);
        cx += w + 5;
    }

    /* 신호 카드: 상단 상태 바와 동일 위치/사이즈 */
    panel(SIG_X - 2, SIG_Y, 216, SIG_H, 12, UI_PANEL, UI_HL);
    char sv[16];
    char nv[16];
    uint16_t ncol = (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_ACC;
    if (s->have_rsq) {
        snprintf(sv, sizeof(sv), "RSSI %d%s", s->rsq.rssi,
                 s->rsq.rssi > 63 ? "+" : "");
        snprintf(nv, sizeof(nv), "SNR %u", s->rsq.snr);
    } else {
        snprintf(sv, sizeof(sv), "RSSI --");
        snprintf(nv, sizeof(nv), "SNR --");
        ncol = UI_MUTED;
    }
    const char *lbl = "SIGNAL";
    uint16_t lcol = UI_MUTED;
    char seek_lbl[20];
    if (s->seeking) {
        int pct = 0;
        if (s->sw_max > 0) {
            pct = (s->sw_steps * 100) / s->sw_max;
            if (pct > 100) { pct = 100; }
            if (pct < 0) { pct = 0; }
        }
        snprintf(seek_lbl, sizeof(seek_lbl), "SEEK %d%%", pct);
        lbl = seek_lbl;
        lcol = UI_ACC_HI;
    }
    display_draw_text(SIG_X + 8, SIG_Y + 5, lbl, DISPLAY_FONT_TINY,
                      lcol, UI_PANEL, DISPLAY_DATUM_TL);
    int nw = (int)display_text_width(nv, DISPLAY_FONT_TINY);
    display_draw_text(212, SIG_Y + 5, nv, DISPLAY_FONT_TINY, ncol, UI_PANEL,
                      DISPLAY_DATUM_TR);
    display_draw_text((int16_t)(212 - nw - 6), SIG_Y + 5, sv,
                      DISPLAY_FONT_TINY, UI_OK, UI_PANEL, DISPLAY_DATUM_TR);
    bar(SIG_X + 8, SIG_Y + 15, 200, 4,
        s->have_rsq ? s->rsq.rssi : 0, 63, UI_OK);
    bar(SIG_X + 8, SIG_Y + 20, 200, 4,
        s->have_rsq ? s->rsq.snr : 0, 63,
        (!s->have_rsq || s->rsq.snr < 10) ? UI_WARN : UI_ACC);

    /* RDS 카드: 상단 상태 바와 동일 위치/사이즈 */
    panel(4, RDS_Y, 216, RDS_H, 11, UI_PANEL, UI_HL);
    const char *rds = NULL;
    uint16_t rc = UI_FG;
    if (s->mode != RADIO_MODE_FM) {
        if (s->station && s->station[0]) {
            rds = s->station;
            rc = UI_OK;
        } else {
            rds = "...";
            rc = UI_MUTED;
        }
    } else if (s->rds_on && s->rds) {
        rds = (s->rds->rt_valid && s->rds->rt[0]) ? s->rds->rt : s->rds->ps;
        if (!rds || !rds[0]) {
            rds = "...";
        }
    } else {
        rds = "RDS off";
    }
    fit_text(buf, sizeof(buf), rds, DISPLAY_FONT_TINY, 200);
    display_draw_text(12, RDS_Y + 5, buf, DISPLAY_FONT_TINY, rc, UI_PANEL,
                      DISPLAY_DATUM_TL);
}

void ui_draw_side_list_title(const char *title, const ui_list_t *l,
                             const ui_state_t *s)
{
    const int X = CP_W - 12;
    const int R = UI_W - 10;
    const int W = R - X;
    draw_header_tabs(UI_TAB_RADIO, s);

    display_fill_rect(X, HDR_H, W, UI_H - HDR_H, UI_BG);
    display_fill_rect(X, HDR_H, 1, UI_H - HDR_H, UI_LINE);

    display_fill_round_rect(X + 6, HDR_H + 5, 3, 12, 1, UI_ACC);
    display_draw_text(X + 12, HDR_H + 7, title, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", l->cursor + 1, l->count);
    display_draw_text(R - 4, HDR_H + 7, buf, DISPLAY_FONT_TINY, UI_ACC_HI,
                      UI_BG, DISPLAY_DATUM_TR);

    const int y0 = HDR_H + 18;
    const int rh = 26;

    for (int i = 0; i < UI_SIDE_ROWS; ++i) {
        int idx = l->top + i;
        if (idx >= l->count) {
            break;
        }
        int y = y0 + i * rh;
        bool cur = (idx == l->cursor);
        if (cur) {
            display_fill_round_rect(X + 4, (int16_t)y, W - 8, 24, 9,
                                    UI_HSEL);
            display_fill_round_rect(X + 7, (int16_t)(y + 7), 2, 10, 1, UI_ACC);
        }
        fit_text(buf, sizeof(buf), l->labels[idx], DISPLAY_FONT_SMALL,
                 W - 22);
        display_draw_text(X + 13, (int16_t)(y + 1), buf, DISPLAY_FONT_SMALL,
                          UI_FG, cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TL);
        fit_text(buf, sizeof(buf), l->values[idx], DISPLAY_FONT_TINY,
                 W - 22);
        display_draw_text(X + 13, (int16_t)(y + 16), buf, DISPLAY_FONT_TINY,
                          cur ? UI_ACC_HI : UI_MUTED, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TL);
    }

    if (l->count > UI_SIDE_ROWS) {
        int h = UI_SIDE_ROWS * rh;
        int thumb_h = h * UI_SIDE_ROWS / l->count;
        if (thumb_h < 12) {
            thumb_h = 12;
        }
        int sy = y0 + 20;
        int sh = h - 20;
        int thumb_y = sy + (sh - thumb_h) * l->top /
                           (l->count - UI_SIDE_ROWS);
        display_fill_rect(R - 3, (int16_t)sy, 2, (uint16_t)sh, UI_TRACK);
        display_fill_rect(R - 3, (int16_t)thumb_y, 2, (uint16_t)thumb_h,
                          UI_MUTED);
    }

    if (l->top > 0) {
        display_draw_text((int16_t)(X + W / 2), (int16_t)(y0 + 2), "^",
                          DISPLAY_FONT_TINY, UI_ACC_HI,
                          (l->cursor == l->top) ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TC);
    }
    if (l->top + UI_SIDE_ROWS < l->count) {
        display_draw_text((int16_t)(X + W / 2), 158, "v",
                          DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                          DISPLAY_DATUM_TC);
    }
}

void ui_draw_side_list(ui_tab_t tab, const ui_list_t *l, const ui_state_t *s)
{
    ui_draw_side_list_title((tab == UI_TAB_MENU) ? "MENU" : "SETTINGS", l, s);
}

void ui_draw_settings_page(const char *title, const ui_state_t *s,
                           const ui_list_t *l)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 7, 3, 12, 1, UI_ACC);
    display_draw_text(16, 6, title, DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    if (s->clock[0]) {
        display_draw_text(UI_W / 2, 6, s->clock, DISPLAY_FONT_SMALL, UI_ACC_HI,
                          UI_BG, DISPLAY_DATUM_TC);
    }
    draw_status_right(s, UI_W);
    display_fill_rect(0, 26, UI_W, 1, UI_LINE);

    const int x = 8;
    const int w = 304;
    const int y0 = 34;
    const int rh = 26;
    const int rows = 5;
    char buf[32];

    for (int i = 0; i < rows; ++i) {
        int idx = l->top + i;
        if (idx >= l->count) {
            break;
        }
        int y = y0 + i * rh;
        bool sel = (idx == l->cursor);
        uint16_t bg = sel ? UI_HSEL : UI_PANEL;

        panel(x, y, w, 24, 9, bg, sel ? UI_ACC : UI_LINE);
        if (sel) {
            display_fill_round_rect(x + 7, y + 7, 2, 10, 1, UI_ACC);
        }

        fit_text(buf, sizeof(buf), l->labels[idx], DISPLAY_FONT_SMALL, 196);
        display_draw_text(x + 16, y + 4, buf, DISPLAY_FONT_SMALL, UI_FG, bg,
                          DISPLAY_DATUM_TL);

        fit_text(buf, sizeof(buf), l->values[idx] ? l->values[idx] : "",
                 DISPLAY_FONT_TINY, 92);
        display_draw_text(x + w - 12, y + 5, buf, DISPLAY_FONT_TINY,
                          sel ? UI_ACC_HI : UI_MUTED, bg, DISPLAY_DATUM_TR);
    }

    if (l->count > rows) {
        int h = rows * rh;
        int thumb_h = h * rows / l->count;
        if (thumb_h < 12) {
            thumb_h = 12;
        }
        int thumb_y = y0 + (h - thumb_h) * l->top / (l->count - rows);
        display_fill_round_rect(UI_W - 6, y0, 2, h, 1, UI_TRACK);
        display_fill_round_rect(UI_W - 6, thumb_y, 2, thumb_h, 1, UI_MUTED);
    }
}

/* ------------------------------------------------------------- 옵션 시트 */

#define OPT_OV_W   284
#define OPT_OV_RH  16

void ui_draw_options(const ui_options_t *o)
{
    /* 옵션 시트: 볼륨처럼 화면 중앙에 띄우는 바텀시트 */
    char buf[28];

    /* 라디오: 260x92, 목록: 260 x (헤더+행) 사이즈 */
    int rows = o->slider ? 0 : (o->count < UI_OPT_ROWS ? o->count : UI_OPT_ROWS);
    int h = o->slider ? 92 : (34 + rows * OPT_OV_RH + 6);
    int x = (UI_W - OPT_OV_W) / 2;
    int y = (UI_H - h) / 2;
    if (y < HDR_H + 2) {
        y = HDR_H + 2;
    }

    display_fill_round_rect((int16_t)x, (int16_t)y, OPT_OV_W, (uint16_t)h, 16,
                            UI_PANEL);
    display_draw_round_rect((int16_t)x, (int16_t)y, OPT_OV_W, (uint16_t)h, 16,
                            UI_LINE);

    fit_text(buf, sizeof(buf), o->title ? o->title : "", DISPLAY_FONT_SMALL,
             OPT_OV_W - 36);
    display_draw_text((int16_t)(x + 12), (int16_t)(y + 6), buf,
                      DISPLAY_FONT_SMALL, UI_ACC_HI, UI_PANEL,
                      DISPLAY_DATUM_TL);

    if (o->slider) {
        if (o->value == 0 && o->zero_label) {
            snprintf(buf, sizeof(buf), "%s", o->zero_label);
        } else {
            snprintf(buf, sizeof(buf), "%d%s", o->value,
                     o->unit ? o->unit : "");
        }
        display_draw_text((int16_t)(x + OPT_OV_W - 12), (int16_t)(y + 6), buf,
                          DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                          DISPLAY_DATUM_TR);

        int bx = x + 16;
        int bw = OPT_OV_W - 32;
        int by = y + 48;
        int bh = 8;
        int span = (o->max > o->min) ? (o->max - o->min) : 1;
        int level = o->value - o->min;
        if (level < 0) { level = 0; }
        if (level > span) { level = span; }
        bar(bx, by, bw, bh, level, span, UI_ACC);

        int pos = bx + (bw - 1) * level / span;
        display_fill_round_rect((int16_t)(pos - 5), (int16_t)(by - 3), 10,
                                (uint16_t)(bh + 6), 5, UI_FG);

        snprintf(buf, sizeof(buf), "%d%s", o->min, o->unit ? o->unit : "");
        display_draw_text((int16_t)bx, (int16_t)(by + 16), buf,
                          DISPLAY_FONT_SMALL, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
        snprintf(buf, sizeof(buf), "%d%s", o->max, o->unit ? o->unit : "");
        display_draw_text((int16_t)(bx + bw), (int16_t)(by + 16), buf,
                          DISPLAY_FONT_SMALL, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TR);
        return;
    }

    snprintf(buf, sizeof(buf), "%d/%d", o->cursor + 1, o->count);
    display_draw_text((int16_t)(x + OPT_OV_W - 12), (int16_t)(y + 6), buf,
                      DISPLAY_FONT_SMALL, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TR);

    int ry0 = y + 28;
    for (int i = 0; i < rows; ++i) {
        int idx = o->top + i;
        if (idx >= o->count) {
            break;
        }
        int ry = ry0 + i * OPT_OV_RH;
        bool cur = (idx == o->cursor);
        if (cur) {
            display_fill_round_rect((int16_t)(x + 6), (int16_t)ry,
                                    OPT_OV_W - 12, OPT_OV_RH - 1, 6, UI_HSEL);
            display_fill_round_rect((int16_t)(x + 9),
                                    (int16_t)(ry + 3), 2, OPT_OV_RH - 7, 1,
                                    UI_ACC);
        }
        uint16_t bg = cur ? UI_HSEL : UI_PANEL;
        uint16_t fg = cur ? UI_FG : UI_MUTED;
        fit_text(buf, sizeof(buf), o->labels[idx], DISPLAY_FONT_SMALL,
                 OPT_OV_W - 48);
        display_draw_text((int16_t)(x + 16), (int16_t)ry, buf,
                          DISPLAY_FONT_SMALL, fg, bg, DISPLAY_DATUM_TL);
        if (idx == o->cursor) {
            display_fill_round_rect((int16_t)(x + OPT_OV_W - 20),
                                    (int16_t)(ry + 3), 7, 7, 3, UI_OK);
        } else if (idx == o->sel) {
            display_fill_round_rect((int16_t)(x + OPT_OV_W - 20),
                                    (int16_t)(ry + 4), 7, 7, 3, UI_OK);
            display_fill_round_rect((int16_t)(x + OPT_OV_W - 18),
                                    (int16_t)(ry + 6), 3, 3, 1, bg);
        }
    }
}
/* ------------------------------------------------------------- 키패드 */

static const char *const KP_ROWS[4] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl-",
    "zxcvbnm._/",
};

static const char *const KP_SYMS[4] = {
    "!@#$%^&*()",
    "-_=+[]{}\\|;",
    ":;'\"<>?,./",
    "~`|\\-_=+{}",
};

char ui_keypad_char(int idx, bool upper, bool sym)
{
    if (idx < 0 || idx >= 40) {
        return 0;
    }
    if (sym) {
        return KP_SYMS[idx / 10][idx % 10];
    }
    char c = KP_ROWS[idx / 10][idx % 10];
    if (upper && c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    return c;
}

void ui_draw_keypad(const ui_keypad_t *k)
{
    display_fill_screen(UI_BG);

    display_draw_text(10, 6, k->title ? k->title : "PASSWORD",
                      DISPLAY_FONT_TINY, UI_FG, UI_BG, DISPLAY_DATUM_TL);
    if (k->subtitle) {
        char sub[40];
        fit_text(sub, sizeof(sub), k->subtitle, DISPLAY_FONT_TINY, 160);
        display_draw_text(310, 6, sub, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                          DISPLAY_DATUM_TR);
    }

    display_fill_round_rect(6, 20, 308, 22, 10, UI_PANEL);
    display_draw_round_rect(6, 20, 308, 22, 10, UI_LINE);
    char shown[UI_KEY_TEXT_MAX + 4];
    snprintf(shown, sizeof(shown), "%s_", k->text);
    char fit[UI_KEY_TEXT_MAX + 4];
    fit_text(fit, sizeof(fit), shown, DISPLAY_FONT_SMALL, 292);
    display_draw_text(12, 23, fit, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);

    for (int idx = 0; idx < 40; ++idx) {
        int col = idx % 10;
        int row = idx / 10;
        int x = 6 + col * 31;
        int y = 46 + row * 24;
        bool cur = (idx == k->cursor);
        display_fill_round_rect((int16_t)x, (int16_t)y, 29, 21, 6,
                                cur ? UI_HSEL : UI_CELL);
        if (cur) {
            display_draw_round_rect((int16_t)x, (int16_t)y, 29, 21, 6, UI_ACC);
        }
        char b[2] = { ui_keypad_char(idx, k->shift, k->sym), 0 };
        display_draw_text((int16_t)(x + 14), (int16_t)(y + 7), b,
                          DISPLAY_FONT_SMALL, cur ? UI_FG : UI_MUTED,
                          cur ? UI_HSEL : UI_CELL, DISPLAY_DATUM_TC);
    }

    static const char *const fn[5] = {"DEL", "aA", "!@#", "SPC", "OK"};
    for (int i = 0; i < 5; ++i) {
        int idx = 40 + i;
        int x = 3 + i * 63;
        int y = 146;
        bool cur = (idx == k->cursor);
        bool active = (i == 1 && k->shift) || (i == 2 && k->sym);
        display_fill_round_rect((int16_t)x, (int16_t)y, 60, 18, 8,
                                cur ? UI_HSEL : UI_CELL);
        if (cur) {
            display_draw_round_rect((int16_t)x, (int16_t)y, 60, 18, 8, UI_ACC);
        }
        const char *lbl = fn[i];
        if (i == 1 && k->shift) {
            lbl = "A";
        } else if (i == 2 && k->sym) {
            lbl = "abc";
        }
        display_draw_text((int16_t)(x + 30), (int16_t)(y + 5), lbl,
                          DISPLAY_FONT_TINY,
                          cur ? UI_FG : (active ? UI_ACC_HI : UI_MUTED),
                          cur ? UI_HSEL : UI_CELL, DISPLAY_DATUM_TC);
    }
}
/* ------------------------------------------------------------- Wi-Fi UI */

static void lock_icon(int x, int y, uint16_t color)
{
    display_draw_rect((int16_t)(x + 2), (int16_t)y, 5, 4, color);
    display_fill_rect((int16_t)x, (int16_t)(y + 3), 9, 6, color);
}

void ui_draw_wifi_app(int cursor, uint8_t mode, const char *ssid,
                      const char *ip, int utc_min, const char *err)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "WI-FI", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    wifi_icon(296, 5, (mode != 0) ? UI_ACC_HI : UI_MUTED, 4);

    panel(6, 22, 308, 34, 10, UI_PANEL, UI_LINE);
    const char *ms = (mode == 1) ? "ACCESS POINT"
                   : (mode == 2) ? "STATION" : "OFF";
    display_draw_text(16, 26, ms, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    if (err && err[0]) {
        display_draw_text(302, 28, err, DISPLAY_FONT_TINY, UI_RED, UI_PANEL,
                          DISPLAY_DATUM_TR);
    }

    char info[80];
    if (mode == 1) {
        snprintf(info, sizeof(info), "SSID MINI-radio  PW %s", s_ap_pass);
    } else if (mode == 2) {
        snprintf(info, sizeof(info), "SSID %s  IP %s",
                 ssid && ssid[0] ? ssid : "-",
                 ip && ip[0] ? ip : "connecting");
    } else {
        snprintf(info, sizeof(info), "Wi-Fi is off");
    }
    char fitted[sizeof(info)];
    fit_text(fitted, sizeof(fitted), info, DISPLAY_FONT_TINY, 288);
    display_draw_text(16, 42, fitted, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);

    static const char *const items[4] = {"OFF", "AP", "CONNECT", "SCAN"};
    char right[8];
    for (int i = 0; i < 4; ++i) {
        int y = 68 + i * 23;
        bool cur = (i == cursor);
        if (cur) {
            display_fill_round_rect(10, (int16_t)y, 300, 21, 9, UI_HSEL);
            display_fill_round_rect(15, (int16_t)(y + 6), 3, 9, 1, UI_ACC);
        }
        display_draw_text(26, (int16_t)(y + 3), items[i], DISPLAY_FONT_SMALL,
                          UI_FG, cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TL);
        if (i == 2 && err && err[0]) {
            snprintf(right, sizeof(right), "%s", "ERR");
        } else if (i == 1) {
            snprintf(right, sizeof(right), "%s", (mode == 1) ? "ON" : "");
        } else if (i == 2) {
            snprintf(right, sizeof(right), "%s",
                     (mode == 2 && ssid && ssid[0]) ? "LINK" : "");
        } else {
            right[0] = '\0';
        }
        if (right[0]) {
            uint16_t rc = (i == 2 && err && err[0])
                          ? UI_RED
                          : (cur ? UI_ACC_HI : UI_MUTED);
            display_draw_text(302, (int16_t)(y + 4), right, DISPLAY_FONT_TINY,
                              rc, cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TR);
        }
    }

    char utc[16];
    snprintf(utc, sizeof(utc), "UTC%+d", utc_min / 60);
    display_draw_text(302, 158, utc, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);
    display_draw_text(10, 158, "press: select   hold: back",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
}

void ui_draw_wifi_app2(int cursor, uint8_t mode, const char *ssid,
                       const char *ip, int utc_min, const char *err,
                       bool sel, int focus)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "WI-FI", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    wifi_icon(296, 5, (mode != 0) ? UI_ACC_HI : UI_MUTED, 4);

    bool on = (mode != 0);
    panel(8, 24, 304, 30, 10, UI_PANEL,
          (focus == 0 && !sel) ? UI_ACC : UI_LINE);
    display_draw_text(16, 32, on ? "WI-FI ON" : "WI-FI OFF",
                      DISPLAY_FONT_SMALL, on ? UI_FG : UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    int sx = 276;
    int sy = 31;
    int sw = 32;
    int sh = 16;
    display_fill_round_rect(sx, sy, sw, sh, 8, on ? UI_OK : UI_TRACK);
    display_fill_round_rect(on ? sx + sw - 12 : sx + 2, sy + 2, 12, 12, 6,
                            UI_FG);
    display_draw_text(268, 33, on ? "ON" : "OFF", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TR);

    panel(8, 58, 304, 52, 10, UI_PANEL, UI_LINE);
    const char *ms = (mode == 1) ? "AP" : (mode == 2) ? "STA"
                   : (mode == 3) ? "AP+STA" : "OFF";
    display_draw_text(16, 62, ms, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    if (err && err[0]) {
        display_draw_text(302, 64, err, DISPLAY_FONT_TINY, UI_RED, UI_PANEL,
                          DISPLAY_DATUM_TR);
    }

    char l1[80] = "";
    char l2[80] = "";
    if (mode == 1) {
        snprintf(l1, sizeof(l1), "SSID MINI-radio");
        snprintf(l2, sizeof(l2), "PASS %s", s_ap_pass);
    } else if (mode == 2) {
        snprintf(l1, sizeof(l1), "SSID %s",
                 ssid && ssid[0] ? ssid : "-");
        snprintf(l2, sizeof(l2), "IP %s",
                 ip && ip[0] ? ip : "connecting");
    } else if (mode == 3) {
        snprintf(l1, sizeof(l1), "AP MINI-radio  PASS %s", s_ap_pass);
        snprintf(l2, sizeof(l2), "STA %s  %s",
                 ssid && ssid[0] ? ssid : "-",
                 ip && ip[0] ? ip : "connecting");
    } else {
        snprintf(l1, sizeof(l1), "Wi-Fi is off");
    }
    if (l1[0]) {
        display_draw_text(16, 80, l1, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
    }
    if (l2[0]) {
        display_draw_text(16, 92, l2, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
    }

    if (mode == 2 || mode == 3) {
        panel(8, 114, 304, 26, 10, UI_PANEL,
              (focus == 1 && !sel) ? UI_ACC : UI_LINE);
        display_draw_text(16, 120, "NETWORKS", DISPLAY_FONT_SMALL, UI_FG,
                          UI_PANEL, DISPLAY_DATUM_TL);
        char cnt[16];
        if (s_scan_running) {
            snprintf(cnt, sizeof(cnt), "SCANNING");
        } else {
            snprintf(cnt, sizeof(cnt), "%d FOUND", s_scan_n);
        }
        display_draw_text(302, 121, cnt, DISPLAY_FONT_TINY, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TR);
    }

    if (sel) {
        int ox = 60;
        int oy = 40;
        int ow = 200;
        int oh = 4 * 24 + 16;
        panel(ox, oy, ow, oh, 12, UI_PANEL, UI_ACC);
        static const char *const names[4] = {
            "AP", "STA", "AP + STA", "OFF"
        };
        for (int i = 0; i < 4; ++i) {
            int ry = oy + 8 + i * 24;
            if (i == cursor) {
                display_fill_round_rect(ox + 8, (int16_t)ry, ow - 16, 20, 7,
                                        UI_HSEL);
            }
            display_draw_text(ox + 18, (int16_t)(ry + 4), names[i],
                              DISPLAY_FONT_SMALL,
                              i == cursor ? UI_FG : UI_MUTED,
                              i == cursor ? UI_HSEL : UI_PANEL,
                              DISPLAY_DATUM_TL);
        }
    }

    char utc[16];
    snprintf(utc, sizeof(utc), "UTC%+d", utc_min / 60);
    display_draw_text(302, 158, utc, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);
}

void ui_draw_wifi_app3(int cursor, uint8_t mode, const char *ssid,
                       const char *ip, int utc_min, const char *err,
                       bool sel, int focus)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "WI-FI", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    wifi_icon(296, 5, (mode != 0) ? UI_ACC_HI : UI_MUTED, 4);

    bool on = (mode != 0);
    panel(8, 24, 304, 26, 9, UI_PANEL,
          (focus == 0 && !sel) ? UI_ACC : UI_LINE);
    display_draw_text(16, 30, on ? "WI-FI ON" : "WI-FI OFF",
                      DISPLAY_FONT_SMALL, on ? UI_FG : UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    int sx = 276;
    int sy = 28;
    display_fill_round_rect(sx, sy, 32, 16, 8, on ? UI_OK : UI_TRACK);
    display_fill_round_rect(on ? sx + 20 : sx + 2, sy + 2, 12, 12, 6,
                            UI_FG);
    display_draw_text(268, 30, on ? "ON" : "OFF", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TR);

    panel(8, 54, 304, 38, 9, UI_PANEL, UI_LINE);
    const char *ms = (mode == 1) ? "AP" : (mode == 2) ? "STA"
                   : (mode == 3) ? "AP+STA" : "OFF";
    display_draw_text(16, 57, ms, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    if (err && err[0]) {
        display_draw_text(302, 59, err, DISPLAY_FONT_TINY, UI_RED, UI_PANEL,
                          DISPLAY_DATUM_TR);
    }
    char l1[80] = "";
    char l2[80] = "";
    if (mode == 1) {
        snprintf(l1, sizeof(l1), "SSID MINI-radio  PASS %s", s_ap_pass);
    } else if (mode == 2) {
        snprintf(l1, sizeof(l1), "SSID %s",
                 ssid && ssid[0] ? ssid : "-");
        snprintf(l2, sizeof(l2), "IP %s",
                 ip && ip[0] ? ip : "connecting");
    } else if (mode == 3) {
        snprintf(l1, sizeof(l1), "AP MINI-radio  PASS %s", s_ap_pass);
        snprintf(l2, sizeof(l2), "STA %s  %s",
                 ssid && ssid[0] ? ssid : "-",
                 ip && ip[0] ? ip : "connecting");
    } else {
        snprintf(l1, sizeof(l1), "Wi-Fi is off");
    }
    if (l1[0]) {
        display_draw_text(16, 75, l1, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
    }
    if (l2[0]) {
        display_draw_text(16, 84, l2, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
    }

    if (mode == 2 || mode == 3) {
        if (s_scan_running) {
            display_draw_text(16, 100, "SCANNING...", DISPLAY_FONT_TINY,
                              UI_ACC_HI, UI_BG, DISPLAY_DATUM_TL);
        } else if (s_scan_n == 0) {
            display_draw_text(16, 100, "NO NETWORKS", DISPLAY_FONT_TINY,
                              UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
        } else {
            int rows = s_scan_n > 4 ? 4 : (int)s_scan_n;
            for (int i = 0; i < rows; ++i) {
                int idx = s_scan_top + i;
                if (idx >= (int)s_scan_n) {
                    break;
                }
                int y = 96 + i * 15;
                bool cur = (idx == s_scan_cursor);
                if (cur) {
                    display_fill_round_rect(8, (int16_t)y, 304, 14, 6,
                                            UI_HSEL);
                }
                char nb[34];
                fit_text(nb, sizeof(nb), s_scan_ptr[idx],
                         DISPLAY_FONT_TINY, 240);
                display_draw_text(16, (int16_t)(y + 3), nb, DISPLAY_FONT_TINY,
                                  cur ? UI_FG : UI_MUTED,
                                  cur ? UI_HSEL : UI_BG,
                                  DISPLAY_DATUM_TL);
                char rv[12];
                snprintf(rv, sizeof(rv), "%d", s_scan_rssi[idx]);
                display_draw_text(302, (int16_t)(y + 3), rv,
                                  DISPLAY_FONT_TINY,
                                  cur ? UI_ACC_HI : UI_MUTED,
                                  cur ? UI_HSEL : UI_BG,
                                  DISPLAY_DATUM_TR);
            }
        }
    }

    if (sel) {
        int ox = 60;
        int oy = 40;
        int ow = 200;
        int oh = 4 * 24 + 16;
        panel(ox, oy, ow, oh, 12, UI_PANEL, UI_ACC);
        static const char *const names[4] = {
            "AP", "STA", "AP + STA", "OFF"
        };
        for (int i = 0; i < 4; ++i) {
            int ry = oy + 8 + i * 24;
            if (i == cursor) {
                display_fill_round_rect(ox + 8, (int16_t)ry, ow - 16, 20, 7,
                                        UI_HSEL);
            }
            display_draw_text(ox + 18, (int16_t)(ry + 4), names[i],
                              DISPLAY_FONT_SMALL,
                              i == cursor ? UI_FG : UI_MUTED,
                              i == cursor ? UI_HSEL : UI_PANEL,
                              DISPLAY_DATUM_TL);
        }
    }

    char utc[16];
    snprintf(utc, sizeof(utc), "UTC%+d", utc_min / 60);
    display_draw_text(302, 158, utc, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);
}

void ui_draw_wifi_scanning(int dots)
{
    display_fill_screen(UI_BG);
    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "WI-FI SCAN", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    panel(6, 58, 308, 66, 18, UI_PANEL, UI_LINE);
    display_draw_text(UI_W / 2, 68, "SCANNING", DISPLAY_FONT_LARGE, UI_FG,
                      UI_PANEL, DISPLAY_DATUM_TC);

    for (int i = 0; i < 3; ++i) {
        int on = (dots % 4) > i;
        display_fill_round_rect((int16_t)(144 + i * 14), 100, 6, 6, 3,
                                on ? UI_ACC_HI : UI_LINE);
    }

    display_draw_text(UI_W / 2, 134, "searching networks...",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

void ui_draw_wifi_list(const char *title, const char *hint,
                       const char *const *names, const bool *secure,
                       const int *rssi, int count, int cursor, int top)
{
    display_fill_screen(UI_BG);
    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, title ? title : "SELECT NETWORK",
                      DISPLAY_FONT_TINY, UI_FG, UI_BG, DISPLAY_DATUM_TL);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", cursor + 1, count);
    display_draw_text(302, 7, buf, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TR);
    display_fill_rect(0, 21, UI_W, 1, UI_LINE);

    for (int i = 0; i < 5; ++i) {
        int idx = top + i;
        if (idx >= count) {
            break;
        }
        int y = 24 + i * 27;
        bool cur = (idx == cursor);
        if (cur) {
            display_fill_round_rect(6, (int16_t)y, 308, 24, 9, UI_HSEL);
            display_fill_round_rect(11, (int16_t)(y + 7), 3, 10, 1, UI_ACC);
        }
        fit_text(buf, sizeof(buf), names[idx], DISPLAY_FONT_SMALL, 210);
        display_draw_text(20, (int16_t)(y + 4), buf, DISPLAY_FONT_SMALL,
                          UI_FG, cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TL);

        if (rssi) {
            snprintf(buf, sizeof(buf), "%d", rssi[idx]);
            display_draw_text(276, (int16_t)(y + 9), buf, DISPLAY_FONT_TINY,
                              cur ? UI_ACC_HI : UI_MUTED,
                              cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TR);
        }
        if (secure && secure[idx]) {
            lock_icon(292, (int16_t)(y + 8), cur ? UI_ACC_HI : UI_MUTED);
        }
    }

    display_draw_text(UI_W / 2, UI_H - 10,
                      hint ? hint : "turn: move   press: connect   hold: back",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}
/* ------------------------------------------------------------- 시간 설정 */

void ui_draw_time_set(int year, int month, int day, int hour, int minute,
                      int field, int utc_min)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "SET DATE/TIME", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    char buf[16];
    snprintf(buf, sizeof(buf), "UTC%+d", utc_min / 60);
    display_draw_text(310, 7, buf, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);

    panel(16, 36, 288, 74, 18, UI_PANEL, UI_LINE);

    /* 날짜: YYYY-MM-DD */
    int x = 120;
    int y = 46;
    snprintf(buf, sizeof(buf), "%04d", year);
    display_draw_text((int16_t)x, (int16_t)y, buf, DISPLAY_FONT_SMALL,
                      field == 0 ? UI_ACC_HI : UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    x += 32;
    display_draw_text((int16_t)x, (int16_t)y, "-", DISPLAY_FONT_SMALL, UI_MUTED,
                      UI_PANEL, DISPLAY_DATUM_TL);
    x += 8;
    snprintf(buf, sizeof(buf), "%02d", month);
    display_draw_text((int16_t)x, (int16_t)y, buf, DISPLAY_FONT_SMALL,
                      field == 1 ? UI_ACC_HI : UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    x += 16;
    display_draw_text((int16_t)x, (int16_t)y, "-", DISPLAY_FONT_SMALL, UI_MUTED,
                      UI_PANEL, DISPLAY_DATUM_TL);
    x += 8;
    snprintf(buf, sizeof(buf), "%02d", day);
    display_draw_text((int16_t)x, (int16_t)y, buf, DISPLAY_FONT_SMALL,
                      field == 2 ? UI_ACC_HI : UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);

    /* 시간: HH:MM */
    int hw = display_text_width("00", DISPLAY_FONT_LARGE);
    int cw = display_text_width(":", DISPLAY_FONT_LARGE);
    int total = hw * 2 + cw;
    x = UI_W / 2 - total / 2;
    y = 66;
    snprintf(buf, sizeof(buf), "%02d", hour);
    display_draw_text((int16_t)x, (int16_t)y, buf, DISPLAY_FONT_LARGE,
                      field == 3 ? UI_ACC_HI : UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    x += hw;
    display_draw_text((int16_t)x, (int16_t)y, ":", DISPLAY_FONT_LARGE, UI_MUTED,
                      UI_PANEL, DISPLAY_DATUM_TL);
    x += cw;
    snprintf(buf, sizeof(buf), "%02d", minute);
    display_draw_text((int16_t)x, (int16_t)y, buf, DISPLAY_FONT_LARGE,
                      field == 4 ? UI_ACC_HI : UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);

    static const char *const names[5] = {"YEAR", "MONTH", "DAY", "HOUR",
                                         "MINUTE"};
    display_draw_text(UI_W / 2, 118, names[field], DISPLAY_FONT_SMALL, UI_FG,
                      UI_BG, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 136, "turn: adjust   press: next",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 150, "hold: save", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

/* ------------------------------------------------------------- EiBi UI */

void ui_draw_eibi_app(int cursor, bool ready, int entries)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "EIBI SCHEDULE", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    panel(6, 22, 308, 40, 12, UI_PANEL, UI_LINE);
    char buf[48];
    if (ready) {
        snprintf(buf, sizeof(buf), "STORED  %d entries", entries);
        display_draw_text(16, 30, buf, DISPLAY_FONT_SMALL, UI_OK, UI_PANEL,
                          DISPLAY_DATUM_TL);
    } else {
        display_draw_text(16, 30, "NO SCHEDULE", DISPLAY_FONT_SMALL, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TL);
    }
    display_draw_text(16, 48, "shortwave broadcast schedule", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);

    static const char *const items[2] = {"DOWNLOAD", "VIEW ALL"};
    static const char *const hints[2] = {"internet", "browse"};
    for (int i = 0; i < 2; ++i) {
        int y = 72 + i * 30;
        bool cur = (i == cursor);
        if (cur) {
            display_fill_round_rect(10, (int16_t)y, 300, 23, 10, UI_HSEL);
            display_fill_round_rect(15, (int16_t)(y + 7), 3, 10, 1, UI_ACC);
        }
        display_draw_text(28, (int16_t)(y + 4), items[i], DISPLAY_FONT_SMALL,
                          cur ? UI_FG : UI_MUTED, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TL);
        display_draw_text(300, (int16_t)(y + 6), hints[i], DISPLAY_FONT_TINY,
                          UI_ACC_HI, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TR);
    }

    display_draw_text(10, UI_H - 10, "turn: select   press: ok   hold: back",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
}

#define EIBI_VIEW_ROWS 5
#define EIBI_VIEW_RH   24

void ui_draw_eibi_view(int cursor, int top, int total)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "EIBI SCHEDULE", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    char hb[24];
    snprintf(hb, sizeof(hb), "%d/%d", total > 0 ? cursor + 1 : 0, total);
    display_draw_text(UI_W - 8, 7, hb, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TR);
    display_fill_rect(0, HDR_LINE_Y, UI_W, 1, UI_LINE);

    for (int i = 0; i < EIBI_VIEW_ROWS; ++i) {
        int idx = top + i;
        if (idx >= total) {
            break;
        }
        eibi_rec_t rec;
        if (!eibi_read_record(idx, &rec)) {
            break;
        }
        int y = 24 + i * EIBI_VIEW_RH;
        bool cur = (idx == cursor);
        if (cur) {
            display_fill_round_rect(6, (int16_t)y, UI_W - 12, 22, 8, UI_HSEL);
            display_fill_round_rect(10, (int16_t)(y + 7), 2, 10, 1, UI_ACC);
        }
        char fb[20];
        snprintf(fb, sizeof(fb), "%u kHz", (unsigned)rec.freq);
        display_draw_text(16, (int16_t)(y + 1), fb, DISPLAY_FONT_TINY,
                          cur ? UI_FG : UI_OK, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TL);
        char tb[24];
        snprintf(tb, sizeof(tb), "%02u:%02u-%02u:%02u",
                 rec.sh, rec.sm, rec.eh, rec.em);
        display_draw_text(UI_W - 12, (int16_t)(y + 1), tb, DISPLAY_FONT_TINY,
                          cur ? UI_ACC_HI : UI_MUTED, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TR);
        char nb[40];
        fit_text(nb, sizeof(nb), rec.name, DISPLAY_FONT_TINY, UI_W - 30);
        display_draw_text(16, (int16_t)(y + 11), nb, DISPLAY_FONT_TINY,
                          cur ? UI_FG : UI_MUTED, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TL);
    }

    if (total > EIBI_VIEW_ROWS) {
        int h = EIBI_VIEW_ROWS * EIBI_VIEW_RH;
        int thumb_h = h * EIBI_VIEW_ROWS / total;
        if (thumb_h < 12) {
            thumb_h = 12;
        }
        int span = total - EIBI_VIEW_ROWS;
        int thumb_y = 24 + (h - thumb_h) * top / (span > 0 ? span : 1);
        display_fill_rect(UI_W - 3, 24, 2, (uint16_t)h, UI_TRACK);
        display_fill_rect(UI_W - 3, (int16_t)thumb_y, 2, (uint16_t)thumb_h,
                          UI_MUTED);
    }

    display_draw_text(8, UI_H - 8, "turn: page   press: back",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
}

void ui_draw_splash_app(int cursor, bool on, int mode, const char *callsign,
                        const char *welcome)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "SPLASH", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    display_draw_text(UI_W - 8, 7, "power-on screen", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TR);
    display_fill_rect(0, HDR_LINE_Y, UI_W, 1, UI_LINE);

    const char *labels[4] = {"SPLASH", "MODE", "CALLSIGN", "WELCOME"};
    char v0[8];
    snprintf(v0, sizeof(v0), "%s", on ? "ON" : "OFF");
    const char *values[4];
    values[0] = v0;
    values[1] = (mode == 1) ? "WELCOME" : "CALLSIGN";
    values[2] = (callsign && callsign[0]) ? callsign : "(none)";
    values[3] = (welcome && welcome[0]) ? welcome : "(none)";

    for (int i = 0; i < 4; ++i) {
        int y = 26 + i * 30;
        bool cur = (i == cursor);
        if (cur) {
            display_fill_round_rect(8, (int16_t)y, UI_W - 16, 26, 9, UI_HSEL);
            display_fill_round_rect(12, (int16_t)(y + 8), 3, 10, 1, UI_ACC);
        }
        display_draw_text(24, (int16_t)(y + 4), labels[i], DISPLAY_FONT_SMALL,
                          cur ? UI_FG : UI_ACC_HI, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TL);
        char vb[48];
        fit_text(vb, sizeof(vb), values[i], DISPLAY_FONT_TINY, 150);
        display_draw_text(UI_W - 14, (int16_t)(y + 7), vb, DISPLAY_FONT_TINY,
                          cur ? UI_FG : UI_MUTED, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TR);
    }

    display_draw_text(UI_W / 2, UI_H - 8,
                      "turn: select   press: ok   hold: back",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

void ui_draw_eibi_dl(int entries, int bytes, int dots)
{
    display_fill_screen(UI_BG);
    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    display_draw_text(16, 7, "EIBI DOWNLOAD", DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    panel(6, 42, 308, 84, 18, UI_PANEL, UI_LINE);
    display_draw_text(UI_W / 2, 54, "DOWNLOADING", DISPLAY_FONT_LARGE, UI_FG,
                      UI_PANEL, DISPLAY_DATUM_TC);

    for (int i = 0; i < 3; ++i) {
        int on = (dots % 4) > i;
        display_fill_round_rect((int16_t)(144 + i * 14), 88, 6, 6, 3,
                                on ? UI_ACC_HI : UI_LINE);
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "%d entries  %d KB", entries, bytes / 1024);
    display_draw_text(UI_W / 2, 104, buf, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TC);

    display_draw_text(UI_W / 2, 144, "from eibispace.de", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

void ui_draw_confirm(const char *title, const char *msg, bool yes)
{
    display_fill_screen(UI_BG);
    panel(30, 44, 260, 86, 18, UI_PANEL, UI_LINE);

    display_draw_text(UI_W / 2, 56, title ? title : "CONFIRM",
                      DISPLAY_FONT_SMALL, UI_FG, UI_PANEL, DISPLAY_DATUM_TC);

    char buf[80];
    fit_text(buf, sizeof(buf), msg ? msg : "", DISPLAY_FONT_TINY, 230);
    display_draw_text(UI_W / 2, 76, buf, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TC);

    static const char *const opts[2] = {"NO", "YES"};
    for (int i = 0; i < 2; ++i) {
        int x = 48 + i * 120;
        bool cur = (yes == (i == 1));
        display_fill_round_rect((int16_t)x, 96, 104, 22, 10,
                                cur ? UI_ACC : UI_CELL);
        if (!cur) {
            display_draw_round_rect((int16_t)x, 96, 104, 22, 10, UI_LINE);
        }
        display_draw_text((int16_t)(x + 52), 102, opts[i], DISPLAY_FONT_SMALL,
                          UI_FG, cur ? UI_ACC : UI_CELL, DISPLAY_DATUM_TC);
    }

    display_draw_text(UI_W / 2, UI_H - 12,
                      "turn: select   press: ok   hold: back",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
}

void ui_draw_about(const char *version, const char *idf, const char *build,
                   int page)
{
    display_fill_screen(UI_BG);

    display_fill_round_rect(8, 5, 3, 12, 1, UI_ACC);
    const char *title = (page == 0) ? "GITHUB"
                       : (page == 1) ? "ABOUT" : "LICENSES";
    display_draw_text(16, 7, title, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    char pg[16];
    snprintf(pg, sizeof(pg), "%d/3", page + 1);
    display_draw_text(UI_W - 16, 7, pg, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);

    panel(6, 24, 308, 128, 14, UI_PANEL, UI_LINE);

    if (page == 0) {
        static const char *const url = "https://gnbdatsmini.netlify.app/";
        display_fill_round_rect(10, 28, 124, 124, 10, 0xFFFF);
        if (display_draw_qr(url, 28, 46, 3, 0x0000, 0xFFFF) != ESP_OK) {
            display_draw_text(72, 80, "QR ERR", DISPLAY_FONT_SMALL,
                              UI_RED, 0xFFFF, DISPLAY_DATUM_TC);
        }

        display_draw_text(142, 34, "GITHUB", DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(142, 50, "GNBD miniradio", DISPLAY_FONT_TINY,
                          UI_FG, UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(142, 68, "gnbdatsmini", DISPLAY_FONT_TINY,
                          UI_ACC, UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(142, 80, "netlify.app", DISPLAY_FONT_TINY,
                          UI_ACC, UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(142, 102, "SCAN TO OPEN", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);

        display_draw_text(UI_W / 2, UI_H - 10,
                          "turn: pages   press: back", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    } else if (page == 1) {
        display_draw_text(UI_W / 2, 28, "GNBD", DISPLAY_FONT_LARGE, UI_FG,
                          UI_PANEL, DISPLAY_DATUM_TC);
        display_draw_text(UI_W / 2, 64, "MINI-RADIO FIRMWARE",
                          DISPLAY_FONT_TINY, UI_ACC_HI, UI_PANEL,
                          DISPLAY_DATUM_TC);
        display_fill_rect(64, 78, 192, 1, UI_LINE);

        char buf[64];
        char val[64];
        display_draw_text(24, 84, "VERSION", DISPLAY_FONT_TINY, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(296, 84, version ? version : "-", DISPLAY_FONT_TINY,
                          UI_FG, UI_PANEL, DISPLAY_DATUM_TR);

        display_draw_text(24, 98, "BUILD", DISPLAY_FONT_TINY, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(296, 98, build ? build : "-", DISPLAY_FONT_TINY,
                          UI_FG, UI_PANEL, DISPLAY_DATUM_TR);

        display_draw_text(24, 112, "ESP-IDF", DISPLAY_FONT_TINY, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TL);
        snprintf(buf, sizeof(buf), "%s", idf ? idf : "-");
        fit_text(val, sizeof(val), buf, DISPLAY_FONT_TINY, 200);
        display_draw_text(296, 112, val, DISPLAY_FONT_TINY, UI_FG, UI_PANEL,
                          DISPLAY_DATUM_TR);

        display_draw_text(24, 126, "CHIP", DISPLAY_FONT_TINY, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(296, 126, "ESP32-S3 + SI4732", DISPLAY_FONT_TINY,
                          UI_FG, UI_PANEL, DISPLAY_DATUM_TR);

        display_draw_text(24, 140, "SSB PATCH", DISPLAY_FONT_TINY, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TL);
        display_draw_text(296, 140, "PU2CLR / Silicon Labs", DISPLAY_FONT_TINY,
                          UI_FG, UI_PANEL, DISPLAY_DATUM_TR);

        display_draw_text(UI_W / 2, UI_H - 10,
                          "turn: pages   press: back", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    } else {
        display_fill_round_rect(20, 31, 3, 9, 1, UI_ACC);
        display_draw_text(28, 31, "THIRD-PARTY", DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_PANEL, DISPLAY_DATUM_TL);

        static const char *const names[6] = {
            "DISPLAY", "PU2CLR SI4735", "LOVYANGFX",
            "LITTLEFS", "ESP-IDF", "SI4732"
        };
        static const char *const texts[6] = {
            "MIT - ESP32-SI4732 Radio / Max Arnold",
            "MIT - patch data Silicon Labs AN332",
            "BSD - license.txt (TFT_eSPI FreeBSD)",
            "MIT - joltwallet esp_littlefs",
            "Apache-2.0",
            "MIT - miniradio Contributors"
        };
        for (int i = 0; i < 6; ++i) {
            int y = 46 + i * 17;
            display_fill_round_rect(20, (int16_t)(y + 2), 2, 5, 1, UI_ACC);
            display_draw_text(28, (int16_t)y, names[i], DISPLAY_FONT_TINY,
                              UI_FG, UI_PANEL, DISPLAY_DATUM_TL);
            display_draw_text(28, (int16_t)(y + 8), texts[i],
                              DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                              DISPLAY_DATUM_TL);
        }
        display_draw_text(UI_W / 2, 146, "Full texts: LICENSE / NOTICE",
                          DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TC);
        display_draw_text(UI_W / 2, UI_H - 10,
                          "turn: pages   press: back", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    }
}
