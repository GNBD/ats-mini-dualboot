/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * UI core: theme, init, header and shared draw helpers.
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "display.h"
#include "ui.h"
#include "ui_custom.h"
#include "ui_runtime.h"
#include "eibi.h"
#include "decode.h"
#include "ui_int.h"



int ui_mode_slot(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return 0;
    case RADIO_MODE_AM:  return 1;
    case RADIO_MODE_LSB: return 2;
    default:             return 3;
    }
}


const char *ui_mode_str(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return "FM";
    case RADIO_MODE_AM:  return "AM";
    case RADIO_MODE_LSB: return "LSB";
    case RADIO_MODE_USB: return "USB";
    }
    return "?";
}

const char *ui_bw_label(const ui_state_t *s)
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

uint16_t fit_text(char *out, size_t outsz, const char *text,
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

void panel(int x, int y, int w, int h, int r, uint16_t fill,
                  uint16_t border)
{
    display_fill_round_rect((int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h,
                            (uint16_t)r, fill);
    display_draw_round_rect((int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h,
                            (uint16_t)r, border);
}

void pill(int x, int y, const char *text, int maxw,
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
    if (fill > 0) {
        display_fill_round_rect((int16_t)(x + 2), (int16_t)(y + 2),
                                (uint16_t)fill, 7, 2, col);
    }
}

void bar(int x, int y, int w, int h, int value, int max_value,
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

void wifi_icon(int x, int y, uint16_t color, int bars)
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

void draw_status_right(const ui_state_t *s, int right)
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

void draw_header_tabs(ui_tab_t tab, const ui_state_t *s)
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

void draw_header_msg(const ui_state_t *s)
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
    if (ui_custom_active()) {
        return;
    }
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);
}

void ui_fmt_freq(const ui_state_t *s, char *num, size_t n,
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




/* --------------------------------------------------- 좌우 분할 (가운데 70%) */


void ui_fmt_step(const ui_state_t *s, char *buf, size_t n)
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
