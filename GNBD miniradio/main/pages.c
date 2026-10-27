/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Home screen pages and app launching.
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

void page_radio(void)
{
    s_hmi = HMI_TUNE;
    s_ui.seek_mode = false;
    s_ui.list_mode = false;
    s_nav_ms = now_ms();
    ui_draw_all(&s_ui);
}

void page_menu(void)
{
    s_hmi = HMI_MENU;
    s_nav_ms = now_ms();
    s_input_lock = now_ms() + 300;
    rmenu_build();
    ui_draw_radio_panel(&s_ui);
    ui_draw_side_list(UI_TAB_MENU, &s_rmenu, &s_ui);
}

void page_set(void)
{
    s_hmi = HMI_SET;
    s_nav_ms = now_ms();
    s_input_lock = now_ms() + 300;
    settings_build();
    ui_draw_radio_panel(&s_ui);
    ui_draw_side_list(UI_TAB_SET, &s_settings, &s_ui);
}

static void power_radio_off(void)
{
    board_amp(false);
    if (s_radio_ok && s_powered) {
        si4732_power_down(&s_radio);
        s_powered = false;
        s_ssb_prepared = false;
    }
}

int app_find(const char *id)
{
    for (int i = 0; i < APP_COUNT; ++i) {
        if (strcmp(g_apps[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

void page_redraw(void)
{
    if (s_hmi == HMI_MENU) {
        rmenu_build();
        ui_draw_radio_panel(&s_ui);
        ui_draw_side_list(UI_TAB_MENU, &s_rmenu, &s_ui);
    } else if (s_hmi == HMI_SET) {
        settings_build();
        ui_draw_radio_panel(&s_ui);
        ui_draw_side_list(UI_TAB_SET, &s_settings, &s_ui);
    }
}

void enter_home(void)
{
    power_radio_off();
    s_hmi = HMI_HOME;
    s_home_sel = 0;
    s_nav_ms = now_ms();
    s_toast[0] = '\0';
    ui_draw_home(&s_ui, s_home_sel, g_apps, APP_COUNT);
}

void launch_app(int idx)
{
    if (idx < 0 || idx >= APP_COUNT) {
        enter_home();
        return;
    }
    if (g_apps[idx].needs_radio) {
        if (s_radio_ok) {
            radio_apply();
        }
    } else {
        power_radio_off();
    }
    if (g_apps[idx].launch) {
        g_apps[idx].launch();
    }
}

void app_open_radio(void)
{
    page_radio();
}

void app_open_clock(void)
{
    s_hmi = HMI_CLOCK;
    s_nav_ms = now_ms();
    ui_draw_clock(&s_ui);
}

void app_open_weather(void)
{
    weather_open();
}

void open_volume(bool from_menu)
{
    s_vol_from_menu = from_menu;
    s_hmi = HMI_VOL;
    s_vol_ms = now_ms();
    s_ui.volume = audio_get_volume();
    ui_draw_volume(&s_ui, true);
}

void close_volume(void)
{
    bool from_menu = s_vol_from_menu;
    s_vol_from_menu = false;
    if (from_menu) {
        s_hmi = HMI_MENU;
        s_nav_ms = now_ms();
        page_redraw();
    } else {
        s_hmi = HMI_TUNE;
        ui_draw_volume(&s_ui, false);
    }
}

void side_move(ui_list_t *l, ui_tab_t tab, int d)
{
    l->cursor = wrap(l->cursor + d, l->count);
    if (l->cursor < l->top) {
        l->top = l->cursor;
    } else if (l->cursor >= l->top + UI_SIDE_ROWS) {
        l->top = l->cursor - UI_SIDE_ROWS + 1;
    }
    ui_draw_side_list(tab, l, &s_ui);
}

void wifi_app_draw(void);
void scan_list_draw(void);

void ui_refresh_top(void)
{
    switch (s_hmi) {
    case HMI_TUNE:
    case HMI_SEEK:
    case HMI_DECODE:
    case HMI_FAV:
    case HMI_ATS_LIST:
        ui_draw_header(&s_ui);
        break;
    case HMI_HOME:
        ui_draw_home(&s_ui, s_home_sel, g_apps, APP_COUNT);
        break;
    case HMI_CLOCK:
        ui_draw_clock(&s_ui);
        break;
    case HMI_WEATHER:
        weather_redraw();
        break;
    case HMI_SCAN:
        ui_draw_spectrum(&s_ui, band_table[s_spec_band].name, s_spec_min,
                         s_spec_max, s_spec_rssi, s_spec_snr, s_spec_count,
                         s_spec_idx, s_spec_cursor, s_spec_done);
        break;
    case HMI_MENU:
        rmenu_build();
        ui_draw_radio_panel(&s_ui);
        ui_draw_side_list(UI_TAB_MENU, &s_rmenu, &s_ui);
        break;
    case HMI_SET:
        settings_build();
        ui_draw_radio_panel(&s_ui);
        ui_draw_side_list(UI_TAB_SET, &s_settings, &s_ui);
        break;
    case HMI_WIFIAPP:
        wifi_app_draw();
        break;
    case HMI_WIFISCAN:
    case HMI_SCANNING:
        scan_list_draw();
        break;
    case HMI_KEYPAD:
        ui_draw_keypad(&s_kp);
        break;
    default:
        break;
    }
}
