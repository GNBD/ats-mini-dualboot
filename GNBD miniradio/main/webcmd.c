/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Web command execution.
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

/* ------------------------------------------------------------- 웹 명령 */

void web_cmd_execute(const web_cmd_t *c)
{
    switch (c->type) {
    case WEB_CMD_TUNE_STEP:
        if (!s_radio_ok || s_seeking || s_ats == ATS_SEEK) {
            break;
        }
        {
            uint32_t step = eff_step_hz();
            if (step == 0) {
                break;
            }
            uint32_t base = band_snap_freq(s_ui.band_idx, s_ui.freq_hz, step);
            s_ui.freq_hz = band_step_freq(s_ui.band_idx, base, c->a, step);
            if (radio_tune() != ESP_OK) {
                ESP_LOGW(TAG, "web tune failed");
            }
            s_freq_dirty = true;
            mark_change();
            page_radio();
        }
        break;

    case WEB_CMD_TUNE_FREQ:
        if (!s_radio_ok || c->a <= 0) {
            break;
        }
        s_ui.freq_hz = band_clamp_freq(s_ui.band_idx, (uint32_t)c->a);
        apply_step_snap();
        if (radio_tune() != ESP_OK) {
            ESP_LOGW(TAG, "web freq failed");
        }
        s_freq_dirty = true;
        mark_change();
        page_radio();
        break;

    case WEB_CMD_VOLUME: {
        int v = c->a;
        if (v < 0) v = 0;
        if (v > AUDIO_VOLUME_MAX) v = AUDIO_VOLUME_MAX;
        audio_set_volume((uint8_t)v);
        s_ui.volume = audio_get_volume();
        mark_change();
        if (s_hmi == HMI_TUNE || s_hmi == HMI_VOL) {
            open_volume(false);
        } else {
            ui_draw_status(&s_ui);
        }
        break;
    }

    case WEB_CMD_BAND:
        set_band(c->a);
        page_radio();
        break;

    case WEB_CMD_MODE: {
        radio_mode_t m = (radio_mode_t)c->a;
        if (band_table[s_ui.band_idx].default_mode == RADIO_MODE_FM) {
            m = RADIO_MODE_FM;
        } else if (m == RADIO_MODE_FM) {
            m = RADIO_MODE_AM;
        } else if (m > RADIO_MODE_USB) {
            m = RADIO_MODE_AM;
        }
        if (m != s_ui.mode) {
            s_ui.mode = m;
            sync_step();
            apply_step_snap();
            radio_apply();
            mark_change();
            s_freq_dirty = true;
            page_radio();
        }
    } break;

    case WEB_CMD_PRESET_TUNE:
        if (c->a >= 0 && c->a < s_presets.count) {
            tune_entry(&s_presets.slot[c->a]);
            page_radio();
        }
        break;

    case WEB_CMD_PRESET_SAVE:
        if (preset_index_for_freq(s_ui.freq_hz, s_ui.band_idx) >= 0) {
            toast("ALREADY SAVED");
        } else if (s_presets.count < STORAGE_PRESET_SLOTS) {
            int i = s_presets.count++;
            s_presets.slot[i].freq_hz = s_ui.freq_hz;
            s_presets.slot[i].band = (uint8_t)s_ui.band_idx;
            s_presets.slot[i].mode = (uint8_t)s_ui.mode;
            s_pnames.name[i][0] = '\0';
            storage_save_presets(&s_presets);
            storage_save_names(&s_pnames);
            mark_change();
            toast("PRESET SAVED");
        } else {
            toast("PRESET FULL");
        }
        break;

    case WEB_CMD_PRESET_ADD: {
        int bi = (int)((c->b >> 8) & 0xFF);
        int mode = (int)(c->b & 0xFF);
        uint32_t freq = (uint32_t)c->a;
        if (preset_index_for_freq(freq, bi) >= 0) {
            toast("ALREADY SAVED");
        } else if (s_presets.count < STORAGE_PRESET_SLOTS) {
            int i = s_presets.count++;
            s_presets.slot[i].freq_hz = freq;
            s_presets.slot[i].band = (uint8_t)bi;
            s_presets.slot[i].mode = (uint8_t)mode;
            s_pnames.name[i][0] = '\0';
            storage_save_presets(&s_presets);
            storage_save_names(&s_pnames);
            mark_change();
            toast("PRESET ADDED");
        } else {
            toast("PRESET FULL");
        }
        if (s_hmi == HMI_FAV) {
            fav_side_draw();
        }
    } break;

    case WEB_CMD_PRESET_DEL:
        if (c->a >= 0 && c->a < s_presets.count) {
            for (int i = c->a; i < s_presets.count - 1; ++i) {
                s_presets.slot[i] = s_presets.slot[i + 1];
                memcpy(s_pnames.name[i], s_pnames.name[i + 1],
                       STORAGE_NAME_LEN);
            }
            s_presets.count--;
            memset(&s_presets.slot[s_presets.count], 0,
                   sizeof(s_presets.slot[s_presets.count]));
            memset(s_pnames.name[s_presets.count], 0, STORAGE_NAME_LEN);
            storage_save_presets(&s_presets);
            storage_save_names(&s_pnames);
            mark_change();
            if (s_hmi == HMI_FAV) {
                fav_side_draw();
            }
            toast("PRESET DELETED");
        }
        break;

    case WEB_CMD_WIFI_OFF:
        wifi_off();
        do_save();
        ui_draw_status(&s_ui);
        toast("WI-FI OFF");
        break;

    case WEB_CMD_WIFI_AP:
        if (wifi_start_ap() != ESP_OK) {
            toast("WIFI ERR");
        } else {
            do_save();
            ui_draw_status(&s_ui);
            toast("WI-FI AP");
        }
        break;

    case WEB_CMD_WIFI_STA:
        if (c->s1[0]) {
            snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", c->s1);
            snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", c->s2);
            if (wifi_sta_connect() != ESP_OK) {
                toast("WIFI ERR");
            } else {
                s_wifi_mode = 2;
                s_ui.wifi_state = 2;
                do_save();
                ui_draw_status(&s_ui);
                if (s_hmi == HMI_WIFIAPP) {
                    wifi_app_draw();
                }
                toast("STA MODE");
            }
        }
        break;

    case WEB_CMD_WIFI_SCAN:
        if ((s_wifi_mode == 2 || s_wifi_mode == 3) && s_wifi_started) {
            s_scan_done = false;
            s_scan_running = true;
            if (esp_wifi_scan_start(NULL, false) != ESP_OK) {
                s_scan_running = false;
                toast("SCAN ERR");
            }
        } else {
            toast("SCAN: STA ONLY");
        }
        break;

    case WEB_CMD_RADIO:
        if (c->a == RM_BFO &&
            (!band_is_ssb(s_ui.mode) || si4732_ssb_patch_size == 0)) {
            break;
        }
        opt_apply(HMI_MENU, c->a, c->b);
        if (c->a != RM_ATS) {
            page_radio();
        }
        break;

    case WEB_CMD_UTC: {
        int v = c->a;
        if (v < -720) {
            v = -720;
        }
        if (v > 840) {
            v = 840;
        }
        s_utc_min = (int16_t)v;
        clock_update();
        mark_change();
    } break;

    case WEB_CMD_TIME: {
        long long e = strtoll(c->s1, NULL, 10);
        if (e > 1700000000LL && e < 4102444800LL) {
            s_clock_base = (int64_t)e;
            s_clock_ms0 = now_ms();
            s_clock_set = true;
            clock_update();
            clock_save_now();
        }
    } break;

    case WEB_CMD_FOLLOW:
        s_follow_browser = (c->a != 0);
        mark_change();
        break;

    case WEB_CMD_SETUP_FINISH:
        s_wifi_mode = c->a ? 2 : 0;
        s_setup_done = true;
        storage_save_setup_done(true);
        do_save();
        s_setup_reboot_ms = now_ms() + 1500;
        break;

    case WEB_CMD_UI_EVENT: {
        int eid = -1;
        if (strncmp(c->s1, "RM_MOD_", 7) == 0 && c->s1[7] >= 'A' && c->s1[7] <= 'H') {
            eid = RM_MOD_A + (c->s1[7] - 'A');
            if (!mod_runtime_ui_event(eid)) toast("NO MOD");
        } else if (strncmp(c->s1, "ST_MOD_", 7) == 0 && c->s1[7] >= 'A' && c->s1[7] <= 'H') {
            eid = ST_MOD_A + (c->s1[7] - 'A');
            if (!mod_runtime_settings_event(eid)) toast("NO MOD");
        }
    } break;

    case WEB_CMD_UI_RELOAD:
        if (s_theme == UI_THEME_CUSTOM) {
            (void)ui_runtime_activate_active();
            ui_runtime_apply_theme();
        }
        page_radio();
        ui_draw_all(&s_ui);
        display_flush();
        break;

    default:
        break;
    }
}

void web_poll(void)
{
    web_cmd_t c;
    if (!s_web_q) {
        return;
    }
    while (xQueueReceive(s_web_q, &c, 0) == pdTRUE) {
        web_cmd_execute(&c);
    }
}
