/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Clock, calendar and auto brightness.
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

/* ------------------------------------------------------------- 자동 밝기 */

int64_t days_from_civil(int y, int m, int d)
{
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp + (mp < 10 ? 3 : -9);
    *y = yy + (mm <= 2);
    *m = (int)mm;
    *d = (int)dd;
}

int days_in_month(int y, int m)
{
    static const uint8_t dm[12] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
        return 29;
    }
    if (m < 1 || m > 12) {
        return 31;
    }
    return dm[m - 1];
}

void local_datetime(int *y, int *mo, int *d, int *h, int *mi)
{
    int64_t t = s_clock_base +
                (int64_t)((now_ms() - s_clock_ms0) / 1000) +
                (int64_t)s_utc_min * 60;
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    civil_from_days(days, y, mo, d);
    *h = (int)(rem / 3600);
    *mi = (int)((rem / 60) % 60);
}

static uint8_t auto_brightness_level(int hour, int minute)
{
    int mins = hour * 60 + minute;
    int pct;
    if (mins >= 22 * 60) {
        pct = 100 - (mins - 22 * 60) * 60 / 120;
    } else if (mins < 6 * 60) {
        pct = 40;
    } else if (mins < 8 * 60) {
        pct = 40 + (mins - 6 * 60) * 60 / 120;
    } else {
        pct = 100;
    }
    int v = (int)s_brightness * pct / 100;
    if (v < 1) { v = 1; }
    if (v > 100) { v = 100; }
    return (uint8_t)v;
}

void brightness_apply(void)
{
    if (s_screen_off) {
        return;
    }
    uint8_t level = s_brightness;
    if (s_auto_bright && s_clock_set) {
        int y, mo, d, h, mi;
        local_datetime(&y, &mo, &d, &h, &mi);
        level = auto_brightness_level(h, mi);
    }
    if (s_power_save && level > 65) {
        level = 65;
    }
    display_set_backlight_level(level);
}

void timeset_draw(void)
{
    ui_draw_time_set(s_ts_year, s_ts_mon, s_ts_day, s_ts_hour, s_ts_min,
                     s_ts_field, s_utc_min);
}

void open_time_set(void)
{
    local_datetime(&s_ts_year, &s_ts_mon, &s_ts_day, &s_ts_hour, &s_ts_min);
    if (s_ts_year < 2000 || s_ts_year > 2099) {
        s_ts_year = 2026;
    }
    s_ts_field = 0;
    s_hmi = HMI_TIMESET;
    s_nav_ms = now_ms();
    timeset_draw();
}

void timeset_move(int d)
{
    switch (s_ts_field) {
    case 0:
        s_ts_year += d;
        if (s_ts_year < 2000) { s_ts_year = 2099; }
        if (s_ts_year > 2099) { s_ts_year = 2000; }
        break;
    case 1:
        s_ts_mon += d;
        if (s_ts_mon < 1) { s_ts_mon = 12; }
        if (s_ts_mon > 12) { s_ts_mon = 1; }
        if (s_ts_day > days_in_month(s_ts_year, s_ts_mon)) {
            s_ts_day = days_in_month(s_ts_year, s_ts_mon);
        }
        break;
    case 2: {
        int dim = days_in_month(s_ts_year, s_ts_mon);
        s_ts_day += d;
        while (s_ts_day < 1) { s_ts_day += dim; }
        while (s_ts_day > dim) { s_ts_day -= dim; }
        break;
    }
    case 3:
        s_ts_hour = (s_ts_hour + d + 24) % 24;
        break;
    default:
        s_ts_min = (s_ts_min + d + 60) % 60;
        break;
    }
    timeset_draw();
}

void timeset_save(void)
{
    int64_t epoch = days_from_civil(s_ts_year, s_ts_mon, s_ts_day) * 86400 +
                    (int64_t)s_ts_hour * 3600 + (int64_t)s_ts_min * 60 -
                    (int64_t)s_utc_min * 60;
    s_clock_base = epoch;
    s_clock_ms0 = now_ms();
    s_clock_set = true;
    clock_update();
    clock_save_now();
    page_set();
    toast("TIME SET");
}

int64_t clock_utc_now(void)
{
    return s_clock_base + (int64_t)((now_ms() - s_clock_ms0) / 1000);
}

void clock_save_now(void)
{
    if (!s_clock_set) {
        return;
    }
    int64_t utc = clock_utc_now();
    storage_save_clock(utc);
    s_clock_saved_utc = utc;
}

void clock_update(void)
{
    int64_t t = s_clock_base +
                (int64_t)((now_ms() - s_clock_ms0) / 1000) +
                (int64_t)s_utc_min * 60;
    int day = (int)(((t % 86400) + 86400) % 86400);
    char b[6];
    snprintf(b, sizeof(b), "%02d:%02d", day / 3600, (day / 60) % 60);
    if (strcmp(b, s_ui.clock) != 0) {
        memcpy(s_ui.clock, b, sizeof(b));
        int yy, mo, dd, hh, mm;
        local_datetime(&yy, &mo, &dd, &hh, &mm);
        snprintf(s_ui.date, sizeof(s_ui.date), "%04d-%02d-%02d", yy, mo, dd);
        ui_refresh_top();
    }
}
