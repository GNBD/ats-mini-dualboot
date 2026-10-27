/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Spectrum, decode refresh and hint overlay.
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

void ui_decode_refresh(void)
{
    display_fill_rect(6, 72, 308, 96, UI_BG);
    panel(6, 72, 308, 96, 8, UI_PANEL, UI_LINE);

    char title[32];
    snprintf(title, sizeof(title), "DECODE %s  GPIO %d",
             decode_mode_str(), decode_gpio());
    display_draw_text(10, 74, title, DISPLAY_FONT_TINY, UI_GOLD, UI_PANEL,
                      DISPLAY_DATUM_TL);

    const char *st = strncmp(decode_text(), "ADC ERR", 7) == 0
                     ? "ERR" : "RUN";
    display_draw_text(UI_W - 10, 74, st, DISPLAY_FONT_TINY,
                      st[0] == 'E' ? UI_RED : UI_OK, UI_PANEL,
                      DISPLAY_DATUM_TR);

    display_draw_text(10, 86, "SIG", DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    bar(32, 88, 278, 4, decode_level(), 100, UI_OK);

    display_fill_rect(8, 96, 304, 54, UI_PANEL);
    const char *txt = decode_text();
    int len = (int)strlen(txt);
    const int per = 44;
    int start = len > per * 4 ? len - per * 4 : 0;
    if (len == 0) {
        display_draw_text(10, 98, "WAIT...", DISPLAY_FONT_SMALL, UI_MUTED,
                          UI_PANEL, DISPLAY_DATUM_TL);
    } else {
        for (int i = 0; i < 4 && start < len; ++i) {
            int n = len - start;
            if (n > per) {
                n = per;
            }
            char buf[48];
            memcpy(buf, txt + start, (size_t)n);
            buf[n] = '\0';
            display_draw_text(10, 98 + i * 13, buf, DISPLAY_FONT_SMALL,
                              UI_FG, UI_PANEL, DISPLAY_DATUM_TL);
            start += n;
        }
    }

    display_draw_text(10, 154, "TURN:GPIO  CLICK:MODE  HOLD:EXIT",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
}

void ui_draw_decode(const ui_state_t *s)
{
    ui_draw_all(s);
    ui_decode_refresh();
}

void ui_draw_hint(const ui_state_t *s, const char *toast)
{
    if (!toast) {
        ui_draw_adv_signal4b(s);
        return;
    }
    panel(60, 122, 200, 40, 16, UI_PANEL, UI_LINE);
    char buf[32];
    fit_text(buf, sizeof(buf), toast, DISPLAY_FONT_SMALL, 184);
    display_draw_text(160, 134, buf, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TC);
}
