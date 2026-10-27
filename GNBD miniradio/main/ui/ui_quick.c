/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Quick action bar and quick config sheet.
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
