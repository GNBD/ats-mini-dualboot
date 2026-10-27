/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * On-screen keypad.
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


void open_keypad(const char *ssid)
{
    s_kp_mode = KP_MODE_WIFI;
    snprintf(s_kp_ssid, sizeof(s_kp_ssid), "%s", ssid ? ssid : "");
    memset(&s_kp, 0, sizeof(s_kp));
    s_kp.title = "PASSWORD";
    s_kp.subtitle = s_kp_ssid;
    s_hmi = HMI_KEYPAD;
    s_nav_ms = now_ms();
    ui_draw_keypad(&s_kp);
}

void open_weather_keypad(void)
{    s_kp_mode = KP_MODE_WEATHER;
    memset(&s_kp, 0, sizeof(s_kp));
    s_kp.title = "LOCATION";
    s_kp.subtitle = "CITY OR PLACE";
    snprintf(s_kp.text, sizeof(s_kp.text), "%s", s_weather_location);
    s_kp.len = (int)strlen(s_kp.text);
    s_hmi = HMI_KEYPAD;
    s_nav_ms = now_ms();
    ui_draw_keypad(&s_kp);
}

void open_splash_app(void)
{
    s_hmi = HMI_SPLASH;
    s_nav_ms = now_ms();
    ui_draw_splash_app(s_splash_cursor, s_splash.on, s_splash.mode,
                       s_splash.callsign, s_splash.welcome);
}

void open_splash_keypad(int welcome)
{
    s_kp_mode = welcome ? KP_MODE_SPLASH_WEL : KP_MODE_SPLASH_CALL;
    memset(&s_kp, 0, sizeof(s_kp));
    s_kp.title = welcome ? "WELCOME" : "CALLSIGN";
    s_kp.subtitle = welcome ? "WELCOME MESSAGE" : "YOUR CALLSIGN";
    const char *pre = welcome ? s_splash.welcome : s_splash.callsign;
    snprintf(s_kp.text, sizeof(s_kp.text), "%s", pre);
    s_kp.len = (int)strlen(s_kp.text);
    s_hmi = HMI_KEYPAD;
    s_nav_ms = now_ms();
    ui_draw_keypad(&s_kp);
}

void open_quick_cfg(void)
{
    s_qcfg_cur = 0;
    s_qcfg_top = 0;
    s_hmi = HMI_QUICKCFG;
    s_nav_ms = now_ms();
    ui_draw_quick_cfg(s_qcfg_cur, s_qcfg_top, s_quick_mask);
}

void keypad_move(int d)
{
    s_kp.cursor = wrap(s_kp.cursor + d, UI_KEY_COUNT);
    ui_draw_keypad(&s_kp);
}

void keypad_act(void)
{
    int idx = s_kp.cursor;
    if (idx < 40) {
        char c = ui_keypad_char(idx, s_kp.shift, s_kp.sym);
        if (c && s_kp.len < UI_KEY_TEXT_MAX - 1) {
            s_kp.text[s_kp.len++] = c;
            s_kp.text[s_kp.len] = '\0';
        }
    } else if (idx == UI_KEY_DEL) {
        if (s_kp.len > 0) {
            s_kp.text[--s_kp.len] = '\0';
        }
    } else if (idx == UI_KEY_SHIFT) {
        s_kp.shift = !s_kp.shift;
    } else if (idx == UI_KEY_SYM) {
        s_kp.sym = !s_kp.sym;
    } else if (idx == UI_KEY_SPACE) {
        if (s_kp.len < UI_KEY_TEXT_MAX - 1) {
            s_kp.text[s_kp.len++] = ' ';
            s_kp.text[s_kp.len] = '\0';
        }
    } else if (idx == UI_KEY_OK) {
        if (s_kp_mode == KP_MODE_WEATHER) {
            char loc[UI_KEY_TEXT_MAX];
            snprintf(loc, sizeof(loc), "%s", s_kp.text);
            char *p = loc;
            while (*p == ' ') {
                ++p;
            }
            char *e = p + strlen(p);
            while (e > p && e[-1] == ' ') {
                *--e = '\0';
            }
            snprintf(s_weather_location, sizeof(s_weather_location), "%s",
                     p[0] ? p : "Seoul");
            storage_save_weather_location(s_weather_location);
            s_weather_mode = 1;
            weather_set_mode(1);
            weather_location_changed();
            mark_change();
            weather_open();
            return;
        }
        if (s_kp_mode == KP_MODE_SPLASH_CALL || s_kp_mode == KP_MODE_SPLASH_WEL) {
            char t[UI_KEY_TEXT_MAX];
            snprintf(t, sizeof(t), "%s", s_kp.text);
            char *p = t;
            while (*p == ' ') {
                ++p;
            }
            char *e = p + strlen(p);
            while (e > p && e[-1] == ' ') {
                *--e = '\0';
            }
            if (s_kp_mode == KP_MODE_SPLASH_CALL) {
                snprintf(s_splash.callsign, sizeof(s_splash.callsign), "%s", p);
            } else {
                snprintf(s_splash.welcome, sizeof(s_splash.welcome), "%s", p);
            }
            storage_save_splash(&s_splash);
            open_splash_app();
            return;
        }
        snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", s_kp.text);
        snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", s_kp_ssid);
        s_wifi_mode = 2;
        mark_change();
        esp_err_t e = wifi_sta_connect();
        open_wifi_app();
        toast(e == ESP_OK ? "CONNECTING" : "WIFI ERR");
        return;
    }
    ui_draw_keypad(&s_kp);
}
