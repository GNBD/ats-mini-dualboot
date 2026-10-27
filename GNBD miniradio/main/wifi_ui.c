/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Wi-Fi app screen and scan list UI.
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
static bool s_wifi_sel;
static int s_wifi_focus;
static uint32_t s_wifi_scan_ms;

/* ------------------------------------------------------------- Wi-Fi UI */

void wifi_app_draw(void)
{
    ui_draw_wifi_app(s_wifi_cursor, s_wifi_mode, s_wifi_ssid,
                     s_sta_connected ? s_sta_ip : "", s_utc_min, s_wifi_err);
}

void open_wifi_app(void)
{
    s_hmi = HMI_WIFIAPP;
    s_wifi_cursor = 0;
    s_wifi_sel = false;
    s_wifi_focus = 0;
    s_wifi_scan_ms = 0;
    s_nav_ms = now_ms();
    wifi_app_draw();
}

void start_scan_page(void)
{
    s_ui.wifi_state = 3;
    s_hmi = HMI_SCANNING;
    s_nav_ms = now_ms();
    s_scan_anim_ms = now_ms();
    ui_draw_wifi_scanning(0);
    wifi_scan_start();
}

void build_scan_arrays(void)
{
    int n = 0;
    for (uint16_t i = 0; i < s_scan_n && n < 16; ++i) {
        if (s_scan_recs[i].ssid[0] == '\0') {
            continue;
        }
        snprintf(s_scan_buf[n], sizeof(s_scan_buf[n]), "%s",
                 (char *)s_scan_recs[i].ssid);
        s_scan_ptr[n] = s_scan_buf[n];
        s_scan_secure[n] = (s_scan_recs[i].authmode != WIFI_AUTH_OPEN);
        s_scan_rssi[n] = s_scan_recs[i].rssi;
        ++n;
    }
    s_scan_n = (uint16_t)n;
}

void scan_list_draw(void)
{
    ui_draw_wifi_list("SELECT NETWORK",
                      "turn: move   press: connect   hold: back",
                      s_scan_ptr, s_scan_secure,
                      s_scan_rssi, (int)s_scan_n, s_scan_cursor, s_scan_top);
}

void open_scan_list(void)
{
    build_scan_arrays();
    if (s_scan_n == 0) {
        toast("NO AP");
        open_wifi_app();
        return;
    }
    s_scan_cursor = 0;
    s_scan_top = 0;
    s_hmi = HMI_WIFISCAN;
    s_nav_ms = now_ms();
    scan_list_draw();
}

void scan_list_move(int d)
{
    int n = (int)s_scan_n;
    s_scan_cursor = wrap(s_scan_cursor + d, n);
    if (s_scan_cursor < s_scan_top) {
        s_scan_top = s_scan_cursor;
    } else if (s_scan_cursor >= s_scan_top + 5) {
        s_scan_top = s_scan_cursor - 4;
    }
    scan_list_draw();
}

static void wifi_apply_mode(int idx)
{
    switch (idx) {
    case 0:
        if (wifi_start_ap() == ESP_OK) {
            toast("AP ON");
        } else {
            toast("WIFI ERR");
        }
        break;
    case 1:
        if (s_wifi_ssid[0]) {
            if (wifi_sta_connect() == ESP_OK) {
                toast("CONNECTING");
            } else {
                toast("WIFI ERR");
            }
        } else {
            s_wifi_sel = false;
            wifi_app_draw();
            start_scan_page();
            return;
        }
        break;
    case 2:
        if (wifi_start_apsta() == ESP_OK) {
            toast("AP+STA ON");
        } else {
            toast("WIFI ERR");
        }
        break;
    default:
        wifi_off();
        toast("WIFI OFF");
        break;
    }
    s_wifi_sel = false;
    s_wifi_focus = 0;
    s_wifi_scan_ms = 0;
    mark_change();
    wifi_app_draw();
}

void wifi_app_action(void)
{
    switch (s_wifi_cursor) {
    case 0:
        wifi_off();
        s_ui.wifi_state = 0;
        toast("WIFI OFF");
        break;
    case 1:
        if (wifi_start_ap() == ESP_OK) {
            s_ui.wifi_state = 1;
            toast("AP MINI-radio");
        } else {
            toast("WIFI ERR");
        }
        break;
    case 2:
        if (s_wifi_ssid[0]) {
            if (wifi_sta_connect() == ESP_OK) {
                s_ui.wifi_state = 2;
                toast("CONNECTING");
            } else {
                toast("WIFI ERR");
            }
        } else {
            start_scan_page();
            return;
        }
        break;
    default:
        start_scan_page();
        return;
    }
    mark_change();
    wifi_app_draw();
}
