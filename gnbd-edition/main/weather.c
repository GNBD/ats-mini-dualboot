/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Lightweight weather app using wttr.in's one-line format.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "app.h"
#include "ui.h"
#include "weather.h"

static const char *TAG = "weather";

#define WX_STALE_MS  (10u * 60u * 1000u)

static volatile int  s_wx_state = 3;
static volatile bool s_wx_running;
static volatile bool s_wx_dirty;
static uint32_t      s_wx_ms;
static uint32_t      s_wx_gen;
static char          s_wx_loc[48];
static char          s_wx_temp[16];
static char          s_wx_cond[40];
static char          s_wx_hum[16];
static char          s_wx_wind[20];

static void clean_ascii(char *s)
{
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7e) {
            *s = ' ';
        }
    }
}

static void trim(char **p)
{
    while (**p == ' ' || **p == '\t') {
        ++*p;
    }
    char *e = *p + strlen(*p);
    while (e > *p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ||
                      e[-1] == '\n')) {
        *--e = '\0';
    }
}

static void weather_parse(char *buf)
{
    char *parts[5] = { NULL, NULL, NULL, NULL, NULL };
    int n = 0;
    char *save = NULL;
    char *tok = strtok_r(buf, "|", &save);
    while (tok && n < 5) {
        trim(&tok);
        parts[n++] = tok;
        tok = strtok_r(NULL, "|", &save);
    }
    if (n < 5 || parts[1][0] == '\0') {
        return;
    }
    snprintf(s_wx_loc, sizeof(s_wx_loc), "%s", parts[0]);
    snprintf(s_wx_temp, sizeof(s_wx_temp), "%s", parts[1]);
    snprintf(s_wx_cond, sizeof(s_wx_cond), "%s", parts[2]);
    snprintf(s_wx_hum, sizeof(s_wx_hum), "%s", parts[3]);
    snprintf(s_wx_wind, sizeof(s_wx_wind), "%s", parts[4]);
    clean_ascii(s_wx_loc);
    clean_ascii(s_wx_temp);
    clean_ascii(s_wx_cond);
    clean_ascii(s_wx_hum);
    clean_ascii(s_wx_wind);
    s_wx_ms = now_ms();
    s_wx_state = 1;
}

static void url_encode(const char *src, char *dst, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 3 < n;
         ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
            *p == '.' || *p == '~') {
            dst[o++] = (char)*p;
        } else if (*p == ' ') {
            dst[o++] = '+';
        } else {
            dst[o++] = '%';
            dst[o++] = hex[*p >> 4];
            dst[o++] = hex[*p & 0x0f];
        }
    }
    dst[o] = '\0';
}

static void weather_task(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    char url[192];
    char loc[96];
    char buf[192] = {0};

    if (s_weather_mode == 0) {
        snprintf(url, sizeof(url),
                 "http://wttr.in/?format=%%l|%%t|%%C|%%h|%%w");
    } else {
        if (s_weather_location[0] == '\0') {
            snprintf(s_weather_location, sizeof(s_weather_location), "Seoul");
        }
        url_encode(s_weather_location, loc, sizeof(loc));
        snprintf(url, sizeof(url),
                 "http://wttr.in/%s?format=%%l|%%t|%%C|%%h|%%w", loc);
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 8000,
        .buffer_size = 512,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = c ? esp_http_client_open(c, 0) : ESP_FAIL;
    if (err == ESP_OK) {
        int hdr = esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        int len = 0;
        if (hdr >= 0 && status == 200) {
            int n = 0;
            while (len < (int)sizeof(buf) - 1 &&
                   (n = esp_http_client_read(c, buf + len,
                                             sizeof(buf) - 1 - len)) > 0) {
                len += n;
            }
            if (n < 0) {
                len = -1;
            }
        }
        ESP_LOGI(TAG, "HTTP %d len=%d", status, len);
        if (len > 0 && gen == s_wx_gen) {
            buf[len] = '\0';
            weather_parse(buf);
        }
        esp_http_client_close(c);
    } else {
        ESP_LOGW(TAG, "request failed (%s)", esp_err_to_name(err));
    }
    if (c) {
        esp_http_client_cleanup(c);
    }
    if (gen == s_wx_gen) {
        if (s_wx_state != 1) {
            s_wx_state = 2;
        }
        s_wx_dirty = true;
    }
    s_wx_running = false;
    vTaskDelete(NULL);
}

static void weather_start(void)
{
    if (s_wx_running) {
        return;
    }
    s_wx_state = 0;
    s_wx_loc[0] = '\0';
    s_wx_running = true;
    ++s_wx_gen;
    uint32_t gen = s_wx_gen;
    if (xTaskCreate(weather_task, "weather", 6144,
                    (void *)(uintptr_t)gen, 4, NULL) != pdPASS) {
        s_wx_running = false;
        s_wx_state = 2;
    }
}

void weather_redraw(void)
{
    const char *label = (s_weather_mode == 0)
                        ? "CURRENT LOCATION" : s_weather_location;
    ui_draw_weather(&s_ui, s_wx_state, s_weather_mode, label, s_wx_loc,
                    s_wx_temp, s_wx_cond, s_wx_hum, s_wx_wind);
}

void weather_open(void)
{
    s_hmi = HMI_WEATHER;
    s_nav_ms = now_ms();
    if (s_weather_location[0] == '\0') {
        snprintf(s_weather_location, sizeof(s_weather_location), "Seoul");
    }

    bool stale = (s_wx_state != 1) ||
                 (now_ms() - s_wx_ms > WX_STALE_MS);
    if (stale) {
        weather_start();
    }
    weather_redraw();
}

void weather_set_mode(int mode)
{
    mode = mode ? 1 : 0;
    if (mode == s_weather_mode) {
        return;
    }
    s_weather_mode = (uint8_t)mode;
    s_wx_state = 0;
    s_wx_ms = 0;
    ++s_wx_gen;
    if (s_hmi == HMI_WEATHER) {
        weather_start();
        weather_redraw();
    }
}

void weather_location_changed(void)
{
    s_wx_state = 0;
    s_wx_ms = 0;
    ++s_wx_gen;
}

void weather_poll(void)
{
    if (s_hmi == HMI_WEATHER && !s_wx_running && s_wx_state == 0) {
        weather_start();
    }
    if (s_wx_dirty) {
        s_wx_dirty = false;
        if (s_hmi == HMI_WEATHER) {
            weather_redraw();
        }
    }
}
