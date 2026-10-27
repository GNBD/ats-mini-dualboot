/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Side list, settings page, options sheet and keypad.
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

void ui_draw_side_list_title(const char *title, const ui_list_t *l,
                             const ui_state_t *s)
{
    bool settings = (strcmp(title, "SETTINGS") == 0);
    int X, Y, W, H, orient = 0;
    bool themed = ui_runtime_panel(settings, &X, &Y, &W, &H, &orient);
    if (!themed) {
        X = CP_W - 12;
        Y = HDR_H;
        W = (UI_W - 10) - X;
        H = UI_H - HDR_H;
    }
    if (W < 40) W = 40;
    if (H < 24) H = 24;
    const int R = X + W;
    if (!themed) draw_header_tabs(UI_TAB_RADIO, s);

    display_fill_rect(X, Y, W, H, UI_BG);
    display_fill_rect(X, Y, 1, H, UI_LINE);

    display_fill_round_rect(X + 6, Y + 5, 3, 12, 1, UI_ACC);
    display_draw_text(X + 12, Y + 7, title, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", l->cursor + 1, l->count);
    display_draw_text(R - 4, Y + 7, buf, DISPLAY_FONT_TINY, UI_ACC_HI,
                      UI_BG, DISPLAY_DATUM_TR);

    if (orient == 1) {
        /* 가로 패널: 항목 폭을 실제 글자폭 비율로 계산해
         * 포커스가 오른쪽 끝을 넘을 때만 한 항목씩 왼쪽으로 밀어 스크롤한다. */
        static int hstart = 0;
        static const char *const *hkeys = NULL;
        static int hcount = 0;
        int cw[UI_OPT_MAX];
        char tb[24];
        int n = l->count;
        if (n > UI_OPT_MAX) n = UI_OPT_MAX;
        for (int i = 0; i < n; ++i) {
            fit_text(tb, sizeof(tb), l->labels[i], DISPLAY_FONT_TINY, 72);
            cw[i] = (int)display_text_width(tb, DISPLAY_FONT_TINY) + 14;
        }
        if (hkeys != l->labels || hcount != l->count) {
            hkeys = l->labels;
            hcount = l->count;
            hstart = 0;
        }
        int avail = W - 20 - 10;
        if (n > 0 && avail < cw[0]) avail = cw[0];
        if (l->cursor < hstart) hstart = l->cursor;
        for (;;) {
            int used = 0;
            for (int i = hstart; i <= l->cursor && i < n; ++i) {
                used += cw[i] + 4;
            }
            if (used <= avail || hstart >= l->cursor) break;
            ++hstart;
        }
        if (hstart >= n) hstart = n > 0 ? n - 1 : 0;
        int start = hstart;
        int x = X + 10;
        const int cy = Y + 18;
        for (int i = start; i < n; ++i) {
            fit_text(tb, sizeof(tb), l->labels[i], DISPLAY_FONT_TINY, 72);
            int tw = cw[i];
            if (i > start && x + tw > R - 16) break;
            bool cur = (i == l->cursor);
            if (cur) {
                display_fill_round_rect(x, cy, tw, 18, 6, UI_HSEL);
            }
            display_draw_text(x + 7, cy + 5, tb, DISPLAY_FONT_TINY,
                              cur ? UI_ACC_HI : UI_FG,
                              cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TL);
            x += tw + 4;
        }
        if (start > 0) {
            display_draw_text(X + 2, cy + 5, "<", DISPLAY_FONT_TINY,
                              UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
        }
        if (x < R - 14 || l->cursor < l->count - 1) {
            display_draw_text(R - 6, cy + 5, ">", DISPLAY_FONT_TINY,
                              UI_MUTED, UI_BG, DISPLAY_DATUM_TR);
        }
        if (H >= 42 && l->values && l->values[l->cursor]) {
            fit_text(buf, sizeof(buf), l->values[l->cursor],
                     DISPLAY_FONT_TINY, W - 16);
            display_draw_text(X + 8, Y + H - 12, buf, DISPLAY_FONT_TINY,
                              UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
        }
        return;
    }

    const int y0 = Y + 18;
    const int rh = 24;
    int rows = (H - 18) / rh;
    if (rows < 1) rows = 1;
    if (rows > UI_SIDE_ROWS) rows = UI_SIDE_ROWS;

    for (int i = 0; i < rows; ++i) {
        int idx = l->top + i;
        if (idx >= l->count) {
            break;
        }
        int y = y0 + i * rh;
        bool cur = (idx == l->cursor);
        if (cur) {
            display_fill_round_rect(X + 4, (int16_t)y, W - 8, 22, 8,
                                    UI_HSEL);
            display_fill_round_rect(X + 7, (int16_t)(y + 6), 2, 10, 1, UI_ACC);
        }
        fit_text(buf, sizeof(buf), l->labels[idx], DISPLAY_FONT_SMALL, W - 22);
        display_draw_text(X + 13, (int16_t)(y + 1), buf, DISPLAY_FONT_SMALL,
                          UI_FG, cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TL);
        fit_text(buf, sizeof(buf), l->values[idx], DISPLAY_FONT_TINY, W - 22);
        display_draw_text(X + 13, (int16_t)(y + 15), buf, DISPLAY_FONT_TINY,
                          cur ? UI_ACC_HI : UI_MUTED, cur ? UI_HSEL : UI_BG,
                          DISPLAY_DATUM_TL);
    }

    if (l->count > rows) {
        int h = rows * rh;
        int thumb_h = h * rows / l->count;
        if (thumb_h < 12) {
            thumb_h = 12;
        }
        int sy = y0 + 20;
        int sh = h - 20;
        int thumb_y = sy + (sh - thumb_h) * l->top /
                           (l->count - rows);
        display_fill_round_rect(R - 5, (int16_t)thumb_y, 3, thumb_h, 1,
                                UI_ACC);
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
            snprintf(buf, sizeof(buf), "%d%s", o->value + o->value_offset,
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

        if (o->min == 0 && o->zero_label) {
            snprintf(buf, sizeof(buf), "%s", o->zero_label);
        } else {
            snprintf(buf, sizeof(buf), "%d%s", o->min + o->value_offset,
                     o->unit ? o->unit : "");
        }
        display_draw_text((int16_t)bx, (int16_t)(by + 16), buf,
                          DISPLAY_FONT_SMALL, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TL);
        snprintf(buf, sizeof(buf), "%d%s", o->max + o->value_offset,
                 o->unit ? o->unit : "");
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
        const char *lbl = o->labels[idx];
        const char *tab = lbl ? strchr(lbl, '\t') : NULL;
        if (tab) {
            char left[24];
            char right[24];
            int ln = (int)(tab - lbl);
            if (ln > (int)sizeof(left) - 1) {
                ln = (int)sizeof(left) - 1;
            }
            memcpy(left, lbl, (size_t)ln);
            left[ln] = '\0';
            snprintf(right, sizeof(right), "%s", tab + 1);
            display_draw_text((int16_t)(x + 16), (int16_t)ry, left,
                              DISPLAY_FONT_SMALL, fg, bg,
                              DISPLAY_DATUM_TL);
            display_draw_text((int16_t)(x + 120), (int16_t)ry,
                              right, DISPLAY_FONT_SMALL, fg, bg,
                              DISPLAY_DATUM_TL);
        } else {
            fit_text(buf, sizeof(buf), lbl, DISPLAY_FONT_SMALL,
                     OPT_OV_W - 48);
            display_draw_text((int16_t)(x + 16), (int16_t)ry, buf,
                              DISPLAY_FONT_SMALL, fg, bg,
                              DISPLAY_DATUM_TL);
        }
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
