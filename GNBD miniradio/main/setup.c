/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Factory setup (service) mode.
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_littlefs.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "board.h"
#include "si4732.h"
#include "si4732_patch.h"
#include "band.h"
#include "display.h"
#include "ui.h"
#include "splash.h"
#include "encoder.h"
#include "audio.h"
#include "storage.h"
#include "battery.h"
#include "rds.h"
#include "app.h"
#include "menu_data.h"
#include "mod_runtime.h"
#include "ui_runtime.h"
#include "decode.h"
#include "weather.h"
#include "hmi.h"

static bool s_setup_btn_raw;
static bool s_setup_btn_stable;
static bool s_setup_btn_long;
static uint32_t s_setup_btn_raw_ms;
static uint32_t s_setup_btn_press_ms;

static bool setup_button_event(bool *long_press)
{
    uint32_t now = now_ms();
    bool pressed = (gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0);
    if (pressed != s_setup_btn_raw) {
        s_setup_btn_raw = pressed;
        s_setup_btn_raw_ms = now;
    }
    if (s_setup_btn_raw != s_setup_btn_stable &&
        (now - s_setup_btn_raw_ms) >= 30) {
        s_setup_btn_stable = s_setup_btn_raw;
        if (s_setup_btn_stable) {
            s_setup_btn_press_ms = now;
            s_setup_btn_long = false;
        } else {
            bool lp = s_setup_btn_long || (now - s_setup_btn_press_ms) >= 800;
            if (long_press) {
                *long_press = lp;
            }
            return true;
        }
    }
    if (s_setup_btn_stable && pressed && !s_setup_btn_long &&
        (now - s_setup_btn_press_ms) >= 800) {
        s_setup_btn_long = true;
    }
    return false;
}

static void setup_service(void)
{
    web_poll();
    if (s_scan_running && s_scan_done) {
        s_scan_running = false;
        build_scan_arrays();
    }
    if (s_setup_reboot_ms &&
        (int32_t)(now_ms() - s_setup_reboot_ms) >= 0) {
        esp_restart();
    }
}

static void setup_adjust_top(int *top, int cursor, int count, int rows)
{
    if (cursor < *top) {
        *top = cursor;
    }
    if (cursor >= *top + rows) {
        *top = cursor - rows + 1;
    }
    if (*top > count - rows) {
        *top = count - rows;
    }
    if (*top < 0) {
        *top = 0;
    }
}

static void setup_progress(int step, int total)
{
    char b[24];
    snprintf(b, sizeof(b), "%d LEFT", total - step);
    display_draw_text(6, 4, b, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    snprintf(b, sizeof(b), "%d/%d", step, total);
    display_draw_text(UI_W - 6, 4, b, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TR);
}

static void setup_progress_bottom(int step, int total)
{
    char b[24];
    snprintf(b, sizeof(b), "%d/%d", step, total);
    display_draw_text(6, UI_H - 10, b, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TL);
    snprintf(b, sizeof(b), "%d LEFT", total - step);
    display_draw_text(UI_W - 6, UI_H - 10, b, DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TR);
}

static void setup_draw_options(const char *title, const char *const *labels,
                               int count, int cursor, int top,
                               const char *hint, int step, int total)
{
    display_fill_screen(UI_BG);
    ui_options_t o;
    memset(&o, 0, sizeof(o));
    o.title = title;
    o.labels = labels;
    o.count = count;
    o.cursor = cursor;
    o.top = top;
    o.sel = cursor;
    ui_draw_options(&o);
    setup_progress(step, total);
    if (hint) {
        display_draw_text(UI_W / 2, UI_H - 10, hint, DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    }
}

static bool setup_wait_options(const char *title, const char *const *labels,
                               int count, int *value, const char *hint,
                               bool *cancelled, int step, int total)
{
    int cursor = *value;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor >= count) {
        cursor = count - 1;
    }
    int top = 0;
    setup_adjust_top(&top, cursor, count, UI_OPT_ROWS);
    int drawn = -1;
    int drawn_top = -1;
    while (1) {
        setup_service();
        int d = encoder_get_delta();
        if (d) {
            cursor = wrap(cursor + d, count);
            setup_adjust_top(&top, cursor, count, UI_OPT_ROWS);
        }
        if (cursor != drawn || top != drawn_top) {
            setup_draw_options(title, labels, count, cursor, top, hint,
                               step, total);
            display_flush();
            drawn = cursor;
            drawn_top = top;
        }
        bool lp = false;
        if (setup_button_event(&lp)) {
            if (lp) {
                if (cancelled) {
                    *cancelled = true;
                    return false;
                }
                continue;
            }
            *value = cursor;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool setup_keypad(const char *ssid, char *out, size_t n)
{
    ui_keypad_t kp;
    memset(&kp, 0, sizeof(kp));
    kp.title = "WI-FI PASSWORD";
    kp.subtitle = ssid;
    ui_draw_keypad(&kp);
    display_flush();
    while (1) {
        setup_service();
        int d = encoder_get_delta();
        if (d) {
            kp.cursor = wrap(kp.cursor + d, UI_KEY_COUNT);
            ui_draw_keypad(&kp);
            display_flush();
        }
        bool lp = false;
        if (setup_button_event(&lp)) {
            if (lp) {
                return false;
            }
            int idx = kp.cursor;
            if (idx < 40) {
                char c = ui_keypad_char(idx, kp.shift, kp.sym);
                if (c && kp.len < UI_KEY_TEXT_MAX - 1) {
                    kp.text[kp.len++] = c;
                    kp.text[kp.len] = '\0';
                }
            } else if (idx == UI_KEY_DEL) {
                if (kp.len > 0) {
                    kp.text[--kp.len] = '\0';
                }
            } else if (idx == UI_KEY_SHIFT) {
                kp.shift = !kp.shift;
            } else if (idx == UI_KEY_SYM) {
                kp.sym = !kp.sym;
            } else if (idx == UI_KEY_SPACE) {
                if (kp.len < UI_KEY_TEXT_MAX - 1) {
                    kp.text[kp.len++] = ' ';
                    kp.text[kp.len] = '\0';
                }
            } else if (idx == UI_KEY_OK) {
                snprintf(out, n, "%s", kp.text);
                return true;
            }
            ui_draw_keypad(&kp);
            display_flush();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void setup_scan_wait(void)
{
    ui_draw_wifi_scanning(0);
    setup_progress_bottom(3, 5);
    display_flush();
    wifi_scan_start();
    uint32_t t0 = now_ms();
    int dots = -1;
    while (!s_scan_done && (now_ms() - t0) < 12000u) {
        setup_service();
        int nd = (int)((now_ms() - t0) / 250u);
        if (nd != dots) {
            dots = nd;
            ui_draw_wifi_scanning(nd);
            setup_progress_bottom(3, 5);
            display_flush();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!s_scan_done) {
        s_scan_running = false;
    }
    build_scan_arrays();
}

void setup_mode(void)
{
    if (wifi_start_apsta() != ESP_OK) {
        s_wifi_mode = 0;
    }

    int welcome_sel = 0;
    (void)encoder_get_delta();
    ui_draw_setup_welcome("GNBD miniradio", app_version(), 1, 5,
                          welcome_sel);
    display_flush();
    while (1) {
        setup_service();
        int wd = encoder_get_delta();
        if (wd) {
            welcome_sel = (welcome_sel + (wd > 0 ? 1 : -1) + 2) % 2;
            ui_draw_setup_welcome("GNBD miniradio", app_version(), 1, 5,
                                  welcome_sel);
            display_flush();
        }
        bool lp = false;
        if (setup_button_event(&lp)) {
            if (welcome_sel == 1) {
                s_wifi_mode = 0;
                s_setup_done = true;
                storage_save_setup_done(true);
                do_save();
                esp_restart();
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    static char tzbuf[27][12];
    static const char *tzlab[27];
    for (int i = 0; i < 27; ++i) {
        snprintf(tzbuf[i], sizeof(tzbuf[i]), "UTC%+d", i - 12);
        tzlab[i] = tzbuf[i];
    }
    int tz = s_utc_min / 60 + 12;
    if (tz < 0) tz = 0;
    if (tz > 26) tz = 26;
    setup_wait_options("TIME ZONE", tzlab, 27, &tz,
                       "turn: change  press: next  web: 192.168.4.1",
                       NULL, 2, 5);
    s_utc_min = (int16_t)((tz - 12) * 60);

    bool wifi_used = false;
    static const char *const wifi_choices[] = {"CONNECT", "SKIP"};
    int choice = 0;
    setup_wait_options("WI-FI", wifi_choices, 2, &choice,
                       "turn: change  press: next", NULL, 3, 5);
    if (choice == 0) {
        setup_scan_wait();
        const char *names[16];
        bool secure[16];
        int rssi[16];
        int count = 0;
        bool no_nets = false;
        for (int i = 0; i < 16 && i < (int)s_scan_n; ++i) {
            names[count] = s_scan_buf[i];
            secure[count] = s_scan_secure[i];
            rssi[count] = s_scan_rssi[i];
            ++count;
        }
        if (count == 0) {
            names[0] = "NO NETWORKS";
            secure[0] = false;
            rssi[0] = 0;
            count = 1;
            no_nets = true;
        }
        int cursor = 0;
        int top = 0;
        int drawn = -1;
        int drawn_top = -1;
        while (1) {
            setup_service();
            int d = encoder_get_delta();
            if (d) {
                cursor = wrap(cursor + d, count);
                setup_adjust_top(&top, cursor, count, 5);
            }
            if (cursor != drawn || top != drawn_top) {
                ui_draw_wifi_list("WI-FI",
                                  "turn: move  press: connect  hold: skip",
                                  names, secure, rssi, count, cursor, top);
                setup_progress_bottom(3, 5);
                display_flush();
                drawn = cursor;
                drawn_top = top;
            }
            bool lp = false;
            if (!setup_button_event(&lp)) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (lp || no_nets) {
                wifi_used = false;
                break;
            }
            char ssid[33];
            snprintf(ssid, sizeof(ssid), "%s", names[cursor]);
            char pass[65] = "";
            if (secure[cursor]) {
                if (!setup_keypad(ssid, pass, sizeof(pass))) {
                    drawn = -1;
                    continue;
                }
            }
            snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", ssid);
            snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", pass);
            if (wifi_setup_connect(s_wifi_ssid, s_wifi_pass) == ESP_OK) {
                wifi_used = true;
                display_fill_screen(UI_BG);
                display_draw_text(UI_W / 2, 60, "CONNECTING...",
                                  DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                                  DISPLAY_DATUM_TC);
                display_draw_text(UI_W / 2, 84, ssid, DISPLAY_FONT_TINY,
                                  UI_ACC_HI, UI_BG, DISPLAY_DATUM_TC);
                setup_progress_bottom(3, 5);
                display_flush();
                uint32_t t0 = now_ms();
                while (!s_sta_connected && (now_ms() - t0) < 7000u) {
                    setup_service();
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            break;
        }
    }

    static const char *const unit_labels[] = {"MHz", "kHz"};
    int unit = s_unit_mode ? 1 : 0;
    setup_wait_options("FREQUENCY UNIT", unit_labels, 2, &unit,
                       "turn: change  press: next", NULL, 4, 5);
    s_unit_mode = unit ? 1 : 0;
    s_ui.unit_mode = s_unit_mode;

    static const char *const batt_labels[] = {
        "ICON", "VOLT", "PERCENT", "% + ICON", "V + ICON"
    };
    int batt = s_batt_view;
    setup_wait_options("BATTERY DISPLAY", batt_labels, 5, &batt,
                       "turn: change  press: next", NULL, 5, 5);
    if (batt < 0) batt = 0;
    if (batt > 4) batt = 4;
    s_batt_view = (uint8_t)batt;
    s_ui.batt_view = s_batt_view;

    s_wifi_mode = wifi_used ? 2 : 0;
    s_setup_done = true;
    storage_save_setup_done(true);
    do_save();
    esp_restart();
}
