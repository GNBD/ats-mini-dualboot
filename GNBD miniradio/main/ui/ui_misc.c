/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Time set, EiBi views, splash, confirm and about.
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
