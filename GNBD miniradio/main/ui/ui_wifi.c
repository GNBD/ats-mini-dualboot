/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Wi-Fi app screens and scan list.
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
