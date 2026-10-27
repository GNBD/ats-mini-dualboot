/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Settings snapshot / save / restore.
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

/* ------------------------------------------------------- 설정 스냅샷 */

void snapshot_fill(void)
{
    if (s_radio_ok) {
        s_cfg.freq_hz    = s_ui.freq_hz;
        s_cfg.band_index = (uint8_t)s_ui.band_idx;
        s_cfg.mode       = (uint8_t)s_ui.mode;
        s_cfg.volume     = audio_get_volume();
    }
    s_cfg.brightness = s_brightness;
    s_cfg.bfo_hz     = (int16_t)s_bfo;
    s_cfg.flags      = (s_rds_on ? 0x01 : 0) |
                       (s_stereo_force ? 0x02 : 0) |
                       (s_power_save ? 0x04 : 0) |
                       (s_auto_bright ? 0x08 : 0) |
                       ((s_theme & 0x07) << 4);
    s_cfg.deemphasis = s_deemph_75 ? 1 : 0;
    s_cfg.sleep_min  = (uint8_t)s_sleep_min;
    s_cfg.reserved[0] = s_weather_mode;
    s_cfg.reserved[1] = 1;
    for (int i = 0; i < 3; ++i) {
        s_cfg.bw[i] = s_bw[i];
        s_cfg.step_idx[i] = (int8_t)s_step_idx[i];
    }
    s_cfg.band_valid = 0;
    for (int i = 0; i < 16; ++i) {
        s_cfg.band_freq[i] = s_band_freq[i];
        if (s_band_mem_valid[i]) {
            s_cfg.band_valid |= (uint16_t)(1u << i);
        }
    }
    s_ext.agc_on = s_agc_on ? 1 : 0;
    s_ext.soft_mute = s_softmute;
    s_ext.avc_gain = s_avc;
    s_ext.squelch_on = s_squelch_on ? 1 : 0;
    for (int i = 0; i < 4; ++i) {
        s_ext.squelch_type[i] = s_squelch_type[i];
        s_ext.squelch_thr[i] = s_squelch_thr[i];
    }
    {
        int cpu_store = s_power_save ? s_cpu_saved : s_cpu_mhz;
        s_ext.reserved[0] = (uint8_t)((cpu_store >= 240) ? 2
                                      : (cpu_store >= 160 ? 1 : 0));
    }
    s_ext.utc_min = s_utc_min;
    s_ext.reserved2[0] = (uint8_t)((s_follow_browser ? 1u : 0u) |
                                   ((uint32_t)(s_sig_view & 0x07u) << 1));
    s_ext.batt_view = s_batt_view;
    s_ext.unit_mode = s_unit_mode;
    s_ext.screen_off = (uint8_t)(s_screen_sec > 120 ? 120 : s_screen_sec);
    s_ext.wifi_mode = s_wifi_mode;
    snprintf(s_ext.wifi_ssid, sizeof(s_ext.wifi_ssid), "%s", s_wifi_ssid);
    snprintf(s_ext.wifi_pass, sizeof(s_ext.wifi_pass), "%s", s_wifi_pass);
}

void do_save(void)
{
    snapshot_fill();
    esp_err_t e_ext = storage_save_ext(&s_ext);
    esp_err_t e_cfg = storage_save_settings(&s_cfg);
    esp_err_t e_pre = storage_save_presets(&s_presets);
    esp_err_t e_ats = storage_save_ats(&s_ats_list);
    esp_err_t e_rfg = storage_save_rf_gain(s_rfgain);
    if (e_ext != ESP_OK || e_cfg != ESP_OK || e_pre != ESP_OK ||
        e_ats != ESP_OK || e_rfg != ESP_OK) {
        ESP_LOGW(TAG, "save failed ext=%s cfg=%s pre=%s ats=%s rfg=%s",
                 esp_err_to_name(e_ext), esp_err_to_name(e_cfg),
                 esp_err_to_name(e_pre), esp_err_to_name(e_ats),
                 esp_err_to_name(e_rfg));
    }
    s_save_ms = now_ms();
    s_ui.save_active = true;
    s_dirty = false;
    s_tune_save_pending = false;
}

void wifi_app_draw(void);
void scan_list_draw(void);
void ui_refresh_top(void);

void save_flush(uint32_t now)
{
    if (s_seeking || s_ats == ATS_SEEK) {
        /* Seek/ATS 진행 중에는 NVS 저장을 미룸 */
        return;
    }
    if (!s_dirty || (now - s_change_ms) < SAVE_DELAY) {
        return;
    }
    do_save();
    ui_refresh_top();
}

/* ------------------------------------------------------ 설정 복원 */

void restore_settings(const storage_settings_t *cfg)
{
    s_cfg = *cfg;
    s_bw[0] = (cfg->bw[0] <= 4) ? cfg->bw[0] : 0;
    s_bw[1] = (cfg->bw[1] <= 6) ? cfg->bw[1] : 1;
    if (s_bw[1] == 3) {
        s_bw[1] = 1;
    }
    s_bw[2] = (cfg->bw[2] <= 5) ? cfg->bw[2] : 1;
    for (int i = 0; i < 3; ++i) {
        int n = step_count(i);
        int v = cfg->step_idx[i];
        s_step_idx[i] = (v >= 0 && v < n) ? v : -1;
    }
    s_bfo = cfg->bfo_hz;
    if (s_bfo >  BFO_MAX_HZ) { s_bfo =  BFO_MAX_HZ; }
    if (s_bfo < -BFO_MAX_HZ) { s_bfo = -BFO_MAX_HZ; }
    s_brightness = (cfg->brightness <= 100) ? cfg->brightness : 100;
    s_auto_bright = (cfg->flags & 0x08) != 0;
    s_theme = (uint8_t)((cfg->flags >> 4) & 0x07);
    if (s_theme > UI_THEME_CUSTOM) {
        s_theme = 0;
    }
    s_rds_on = (cfg->flags & 0x01) != 0;
    s_stereo_force = (cfg->flags & 0x02) != 0;
    s_power_save = (cfg->flags & 0x04) != 0;
    s_deemph_75 = cfg->deemphasis != 0;
    s_sleep_min = cfg->sleep_min;
    s_ui.volume = cfg->volume;
    s_weather_mode = (cfg->reserved[0] <= 1) ? cfg->reserved[0] : 0;

    for (int i = 0; i < 16; ++i) {
        s_band_freq[i] = cfg->band_freq[i];
        s_band_mem_valid[i] = (cfg->band_valid & (1u << i)) != 0;
    }

    storage_load_splash(&s_splash);
    {
        storage_quick_t qcfg;
        storage_load_quick(&qcfg);
        s_quick_mask = qcfg.mask;
    }
    storage_load_ext(&s_ext);
    s_agc_on = s_ext.agc_on != 0;
    storage_load_rf_gain(&s_rfgain);
    s_softmute = s_ext.soft_mute;
    if (s_softmute == 0) {
        s_softmute = 4;
    }
    s_avc = s_ext.avc_gain;
    s_squelch_on = s_ext.squelch_on != 0;
    for (int i = 0; i < 4; ++i) {
        s_squelch_type[i] = s_ext.squelch_type[i];
        s_squelch_thr[i] = s_ext.squelch_thr[i];
    }
    {
        int16_t batt_cal = 0;
        if (storage_load_batt_cal(&batt_cal) == ESP_OK) {
            s_batt_cal_mv = batt_cal;
        }
        battery_set_calibration(s_batt_cal_mv);
    }
    s_cpu_saved = (s_ext.reserved[0] == 1) ? 160
                : (s_ext.reserved[0] == 2) ? 240 : 80;
    s_cpu_mhz = s_power_save ? 80 : s_cpu_saved;
    s_wifi_mode = (s_ext.wifi_mode <= 2) ? s_ext.wifi_mode : 0;
    snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", s_ext.wifi_ssid);
    snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", s_ext.wifi_pass);
    s_utc_min = s_ext.utc_min;
    s_follow_browser = (s_ext.reserved2[0] & 0x01) != 0;
    s_sig_view = (uint8_t)((s_ext.reserved2[0] >> 1) & 0x07);
    if (s_sig_view > 1) {
        s_sig_view = 0;
    }


    s_batt_view = (s_ext.batt_view <= 4) ? s_ext.batt_view : 3;
    s_screen_sec = (s_ext.screen_off <= 120) ? s_ext.screen_off : 0;
    s_unit_mode = (s_ext.unit_mode <= 1) ? s_ext.unit_mode : 0;
    s_ui.unit_mode = s_unit_mode;
    s_ui.batt_view = s_batt_view;
}
