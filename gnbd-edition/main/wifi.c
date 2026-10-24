/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Wi-Fi lifecycle, scan and SNTP integration.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_netif_sntp.h"
#include "app.h"

static const char *TAG = "wifi";
static uint8_t s_sta_retry;

void sntp_start(void);

void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_SCAN_DONE) {
            uint16_t n = 0;
            esp_wifi_scan_get_ap_num(&n);
            if (n > 16) {
                n = 16;
            }
            if (n) {
                esp_wifi_scan_get_ap_records(&n, s_scan_recs);
            }
            s_scan_n = n;
            s_scan_done = true;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *e =
                (wifi_event_sta_disconnected_t *)data;
            s_sta_connected = false;
            s_wifi_got_ip = false;
            if ((s_wifi_mode == 2 || s_wifi_mode == 3) && s_wifi_ssid[0]) {
                if (e && (e->reason == WIFI_REASON_AUTH_FAIL ||
                          e->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                          e->reason == WIFI_REASON_AUTH_EXPIRE)) {
                    snprintf(s_wifi_err, sizeof(s_wifi_err), "WRONG PASSWORD");
                } else if (e && e->reason == WIFI_REASON_NO_AP_FOUND) {
                    snprintf(s_wifi_err, sizeof(s_wifi_err), "AP NOT FOUND");
                } else {
                    snprintf(s_wifi_err, sizeof(s_wifi_err), "CONNECT FAILED");
                }
            }
            if ((s_wifi_mode == 2 || s_wifi_mode == 3) && s_wifi_ssid[0] &&
                s_wifi_started) {
                if (s_sta_retry < 5) {
                    s_sta_retry++;
                    esp_wifi_connect();
                } else {
                    s_sta_retry = 0;
                }
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_sta_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected, IP %s", s_sta_ip);
        s_wifi_got_ip = true;
        s_sta_retry = 0;
        s_wifi_err[0] = '\0';
        sntp_start();
    }
}

void sntp_start(void)
{
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        s_sntp_started = true;
    }
}

esp_err_t wifi_init_once(void)
{
    if (s_wifi_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        return e;
    }
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&ic), TAG, "wifi init");
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);
    esp_wifi_set_max_tx_power(56);  /* 14 dBm (0.25 dBm 단위) */
    s_wifi_ready = true;
    return ESP_OK;
}

void wifi_off(void)
{
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }
    http_stop();
    s_sta_connected = false;
    s_scan_running = false;
    s_scan_done = false;
    s_wifi_mode = 0;
    s_ui.wifi_state = 0;
    s_wifi_err[0] = '\0';
}

esp_err_t wifi_boot(wifi_mode_t mode)
{
    ESP_RETURN_ON_ERROR(wifi_init_once(), TAG, "wifi init");
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }
    http_stop();
    return esp_wifi_set_mode(mode);
}

esp_err_t wifi_start_now(void)
{
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(56);
    s_wifi_started = true;
    return ESP_OK;
}

esp_err_t wifi_start_ap(void)
{
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    snprintf((char *)wc.ap.ssid, sizeof(wc.ap.ssid), "MINI-radio");
    wc.ap.ssid_len = (uint8_t)strlen((char *)wc.ap.ssid);
    wc.ap.channel = 1;
    snprintf((char *)wc.ap.password, sizeof(wc.ap.password), "%s",
             s_ap_pass);
    wc.ap.max_connection = 3;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(wifi_boot(WIFI_MODE_AP), TAG, "ap mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wc), TAG, "ap cfg");
    ESP_RETURN_ON_ERROR(wifi_start_now(), TAG, "wifi start");
    s_wifi_mode = 1;
    s_ui.wifi_state = 1;
    s_wifi_err[0] = '\0';
    http_start();
    return ESP_OK;
}

esp_err_t wifi_start_apsta(void)
{
    wifi_config_t ap;
    wifi_config_t sta;
    memset(&ap, 0, sizeof(ap));
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "MINI-radio");
    ap.ap.ssid_len = (uint8_t)strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", s_ap_pass);
    ap.ap.max_connection = 3;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    memset(&sta, 0, sizeof(sta));
    if (s_wifi_ssid[0]) {
        size_t sl = strnlen(s_wifi_ssid, sizeof(sta.sta.ssid) - 1);
        memcpy(sta.sta.ssid, s_wifi_ssid, sl);
        size_t pl = strnlen(s_wifi_pass, sizeof(sta.sta.password) - 1);
        memcpy(sta.sta.password, s_wifi_pass, pl);
        sta.sta.threshold.authmode = (s_wifi_pass[0] == '\0')
                                     ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    }

    ESP_RETURN_ON_ERROR(wifi_boot(WIFI_MODE_APSTA), TAG, "apsta mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "ap cfg");
    if (s_wifi_ssid[0]) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta),
                            TAG, "sta cfg");
    }
    ESP_RETURN_ON_ERROR(wifi_start_now(), TAG, "wifi start");
    if (s_wifi_ssid[0]) {
        ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");
    }
    s_wifi_mode = 3;
    s_ui.wifi_state = 1;
    s_wifi_err[0] = '\0';
    http_start();
    return ESP_OK;
}

esp_err_t wifi_sta_connect(void)
{
    if (s_wifi_ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    s_wifi_err[0] = '\0';
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    size_t sl = strnlen(s_wifi_ssid, sizeof(wc.sta.ssid) - 1);
    memcpy(wc.sta.ssid, s_wifi_ssid, sl);
    size_t pl = strnlen(s_wifi_pass, sizeof(wc.sta.password) - 1);
    memcpy(wc.sta.password, s_wifi_pass, pl);
    wc.sta.threshold.authmode = (s_wifi_pass[0] == '\0') ? WIFI_AUTH_OPEN
                                                         : WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(wifi_boot(WIFI_MODE_STA), TAG, "sta mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "sta cfg");
    ESP_RETURN_ON_ERROR(wifi_start_now(), TAG, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "wifi connect");
    s_wifi_mode = 2;
    s_ui.wifi_state = 2;
    http_start();
    return ESP_OK;
}

esp_err_t wifi_setup_connect(const char *ssid, const char *pass)
{
    if (!ssid || !pass || !s_wifi_started || s_wifi_mode != 3) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    size_t sl = strnlen(ssid, sizeof(wc.sta.ssid) - 1);
    memcpy(wc.sta.ssid, ssid, sl);
    size_t pl = strnlen(pass, sizeof(wc.sta.password) - 1);
    memcpy(wc.sta.password, pass, pl);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc),
                        TAG, "setup sta cfg");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "setup connect");
    s_sta_retry = 0;
    s_wifi_err[0] = '\0';
    s_ui.wifi_state = 1;
    return ESP_OK;
}

void wifi_scan_start(void)
{
    if (!s_wifi_started || (s_wifi_mode != 2 && s_wifi_mode != 3)) {
        if (wifi_boot(WIFI_MODE_STA) != ESP_OK ||
            wifi_start_now() != ESP_OK) {
            toast("WIFI ERR");
            return;
        }
        s_wifi_mode = 2;
        s_ui.wifi_state = 2;
    }
    s_scan_done = false;
    s_scan_n = 0;
    s_scan_running = true;
    if (esp_wifi_scan_start(NULL, false) != ESP_OK) {
        s_scan_running = false;
        toast("SCAN ERR");
    }
}

/* ------------------------------------------------------------- Wi-Fi 스캔 */

