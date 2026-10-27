/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Home / setup welcome / clock / weather screens.
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
