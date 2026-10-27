/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Encoder / button input handling.
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

static int freq_digit_count(void)
{
    uint32_t v = (s_ui.mode == RADIO_MODE_FM) ? s_ui.freq_hz / 10000u
                                              : s_ui.freq_hz / 1000u;
    int n = 1;
    while (v >= 10u) {
        v /= 10u;
        n++;
    }
    return n;
}

static uint32_t digit_step_hz(void)
{
    uint32_t step = (s_ui.mode == RADIO_MODE_FM) ? 10000u : 1000u;
    for (int i = 0; i < s_digit_place; ++i) {
        if (step > 100000000u / 10u) {
            break;
        }
        step *= 10u;
    }
    return step;
}

/* --------------------------------------------------------- 입력 처리 */

void tune_commit(uint32_t now)
{
    if (s_seeking || s_ats == ATS_SEEK) {
        s_pending_steps = 0;
        return;
    }
    if (s_pending_steps == 0) {
        return;
    }
    if ((now - s_last_tune_ms) < TUNE_COALESCE_MS) {
        return;
    }
    int32_t steps = s_pending_steps;
    s_pending_steps = 0;
    s_last_tune_ms = now;

    uint32_t step = eff_step_hz();
    uint32_t base = band_snap_freq(s_ui.band_idx, s_ui.freq_hz, step);
    s_ui.freq_hz = band_step_freq(s_ui.band_idx, base, steps, step);
    if (radio_tune() != ESP_OK) {
        ESP_LOGW(TAG, "tune failed, retrying");
    }
    s_freq_dirty = true;
    s_ui.preset_slot = preset_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
    s_ui.ats_slot = ats_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
    s_tune_save_pending = true;
    s_tune_save_ms = now;
}

void on_encoder(int d)
{
    if (d == 0) {
        return;
    }
    uint32_t now = now_ms();
    s_last_input_ms = now;
    if (s_screen_off) {
        s_screen_off = false;
        brightness_apply();
        return;
    }
    if ((int32_t)(now - s_input_lock) < 0) {
        return;
    }
    if (s_hmi == HMI_TUNE && !s_seeking && s_radio_ok && s_ats != ATS_SEEK) {
        bool pressed = gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0;
        if (pressed) {
            int n = freq_digit_count();
            if (!s_digit_mode) {
                s_digit_mode = true;
                s_digit_place = 0;
            }
            s_digit_swallow = true;
            if (n > 0) {
                s_digit_place = wrap(s_digit_place - d, n);
            }
            ui_draw_radio_freq(&s_ui);
            return;
        }
        if (s_digit_mode) {
            int n = freq_digit_count();
            if (s_digit_place >= n) {
                s_digit_place = n - 1;
            }
            int32_t steps = accel_counts(d);
            uint32_t step = digit_step_hz();
            uint32_t base = band_snap_freq(s_ui.band_idx, s_ui.freq_hz, step);
            s_ui.freq_hz = band_step_freq(s_ui.band_idx, base, steps, step);
            if (s_digit_place >= freq_digit_count()) {
                s_digit_place = freq_digit_count() - 1;
            }
            if (radio_tune() != ESP_OK) {
                ESP_LOGW(TAG, "digit tune failed");
            }
            s_freq_dirty = true;
            s_ui.preset_slot = preset_index_for_freq(s_ui.freq_hz,
                                                     s_ui.band_idx);
            s_ui.ats_slot = ats_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
            s_tune_save_pending = true;
            s_tune_save_ms = now;
            ui_draw_radio_freq(&s_ui);
            return;
        }
    }
    switch (s_hmi) {
    case HMI_TUNE:
        if (s_seeking || !s_radio_ok || s_ats == ATS_SEEK) {
            break;
        }
        s_pending_steps += accel_counts(d);
        if ((now - s_last_tune_ms) >= TUNE_COALESCE_MS) {
            tune_commit(now);
            ui_draw_radio_freq(&s_ui);
        }
        break;
    case HMI_VOL:
        s_vol_ms = now;
        {
            int v = accel_counts(d);
            audio_change_volume((int8_t)((v > 127) ? 127 : (v < -127 ? -127 : v)));
        }
        s_ui.volume = audio_get_volume();
        ui_draw_volume(&s_ui, true);
        mark_change();
        break;
    case HMI_HOME:
        s_nav_ms = now;
        s_home_sel = wrap(s_home_sel + d, APP_COUNT);
        ui_draw_home(&s_ui, s_home_sel, g_apps, APP_COUNT);
        break;
    case HMI_CLOCK:
    case HMI_WEATHER:
        s_nav_ms = now;
        weather_set_mode(!s_weather_mode);
        mark_change();
        break;
    case HMI_SCAN:
        if (s_spec_done && s_spec_count > 0) {
            s_spec_cursor = wrap(s_spec_cursor + d, s_spec_count);
            s_ui.freq_hz = s_spec_min +
                           (uint32_t)s_spec_cursor * s_spec_step;
            if (s_ui.freq_hz > s_spec_max) {
                s_ui.freq_hz = s_spec_max;
            }
            s_spec_freq = s_ui.freq_hz;
            seek_tune(s_spec_freq);
            s_ui.preset_slot = preset_index_for_freq(s_ui.freq_hz,
                                                     s_ui.band_idx);
            ui_draw_spectrum(&s_ui, band_table[s_spec_band].name, s_spec_min,
                             s_spec_max, s_spec_rssi, s_spec_snr,
                             s_spec_count, s_spec_count, s_spec_cursor, true);
        }
        break;
    case HMI_QUICK: {
        s_nav_ms = now;
        uint8_t q[QUICK_MAX];
        int n = quick_build(q);
        if (n > 0) {
            s_quick_cursor = wrap(s_quick_cursor + d, n);
            ui_draw_quick(&s_ui, s_quick_cursor);
        }
        break;
    }
    case HMI_QUICKCFG:
        s_nav_ms = now;
        s_qcfg_cur = wrap(s_qcfg_cur + d, QA_COUNT);
        if (s_qcfg_cur < s_qcfg_top) {
            s_qcfg_top = s_qcfg_cur;
        } else if (s_qcfg_cur >= s_qcfg_top + 5) {
            s_qcfg_top = s_qcfg_cur - 5 + 1;
        }
        ui_draw_quick_cfg(s_qcfg_cur, s_qcfg_top, s_quick_mask);
        break;
    case HMI_MENU:
        s_nav_ms = now;
        side_move(&s_rmenu, UI_TAB_MENU, d);
        break;
    case HMI_SET:
        s_nav_ms = now;
        side_move(&s_settings, UI_TAB_SET, d);
        break;
    case HMI_OPT:
        s_nav_ms = now;
        options_move(d);
        break;
    case HMI_DECODE:
        decode_cycle_gpio(d);
        ui_decode_refresh();
        break;
    case HMI_SEEK: {
        if (!s_radio_ok) {
            break;
        }
        bool pressed = gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0;
        if (pressed || band_is_ssb(s_ui.mode)) {
            /* ats-mini와 동일: 시크 모드에서도 눌러서 회전하면 수동 튜닝,
               SSB는 시크 대신 가속 스텝 튜닝 */
            if (s_seeking) {
                seek_stop();
            }
            s_pending_steps += accel_counts(d);
            if ((now - s_last_tune_ms) >= TUNE_COALESCE_MS) {
                tune_commit(now);
                ui_draw_radio_freq(&s_ui);
            }
            break;
        }
        if (s_seeking) {
            seek_reverse(d > 0);
            break;
        }
        seek_start(d > 0);
        break;
    }
    case HMI_WIFIAPP:
        s_nav_ms = now;
        s_wifi_cursor = wrap(s_wifi_cursor + d, 4);
        wifi_app_draw();
        break;
    case HMI_SCANNING:
        break;
    case HMI_WIFISCAN:
        s_nav_ms = now;
        scan_list_move(d);
        break;
    case HMI_KEYPAD:
        s_nav_ms = now;
        keypad_move(d);
        break;
    case HMI_TIMESET:
        s_nav_ms = now;
        timeset_move(d);
        break;
    case HMI_FAV:
        s_nav_ms = now;
        fav_move(d);
        break;
    case HMI_ATS_LIST:
        s_nav_ms = now;
        ats_list_move(d);
        break;
    case HMI_EIBI:
        s_nav_ms = now;
        s_eibi_cursor = (s_eibi_cursor + (d > 0 ? 1 : -1) + 2) % 2;
        ui_draw_eibi_app(s_eibi_cursor, eibi_available(),
                         eibi_record_count());
        break;
    case HMI_EIBI_VIEW: {
        s_nav_ms = now;
        int total = eibi_record_count();
        if (total > 0 && d != 0) {
            int a = accel_counts(d);
            if (a == 0) {
                a = (d > 0) ? 1 : -1;
            }
            int top = s_eibi_view_top + a * 5;
            if (top >= total) {
                top = 0;
            } else if (top < 0) {
                top = ((total - 1) / 5) * 5;
            }
            s_eibi_view_top = top;
            s_eibi_view_cur = top;
            ui_draw_eibi_view(s_eibi_view_cur, s_eibi_view_top, total);
        }
        break;
    }
    case HMI_SPLASH:
        s_nav_ms = now;
        s_splash_cursor = wrap(s_splash_cursor + d, 4);
        ui_draw_splash_app(s_splash_cursor, s_splash.on, s_splash.mode,
                           s_splash.callsign, s_splash.welcome);
        break;
    case HMI_ABOUT:
        s_nav_ms = now;
        about_page_move(d);
        break;
    case HMI_EIBI_CONFIRM:
        s_nav_ms = now;
        s_eibi_yes = !s_eibi_yes;
        ui_draw_confirm("EIBI DOWNLOAD", "Download schedule now?",
                        s_eibi_yes);
        break;
    case HMI_EIBI_DL:
        break;
    }
}

void on_button(bool long_press)
{
    uint32_t now = now_ms();
    s_last_input_ms = now;
    s_nav_ms = now;
    if (s_screen_off) {
        s_screen_off = false;
        brightness_apply();
        return;
    }
    if ((int32_t)(now - s_input_lock) < 0) {
        return;
    }
    if (s_digit_mode) {
        if (s_digit_swallow) {
            s_digit_swallow = false;
        } else {
            s_digit_mode = false;
            ui_draw_radio_freq(&s_ui);
        }
        return;
    }
    if (s_hmi == HMI_FAV) {
        if (long_press) {
            fav_long();
        } else {
            fav_apply();
        }
        return;
    }
    if (s_hmi == HMI_ATS_LIST) {
        if (long_press) {
            ats_list_long();
        } else {
            ats_list_apply();
        }
        return;
    }
    if (s_hmi == HMI_EIBI) {
        if (long_press) {
            page_set();
        } else if (s_eibi_cursor == 0) {
            open_eibi_confirm();
        } else {
            open_eibi_view();
        }
        return;
    }
    if (s_hmi == HMI_EIBI_VIEW) {
        (void)long_press;
        open_eibi_app();
        return;
    }
    if (s_hmi == HMI_SPLASH) {
        if (long_press) {
            page_set();
            return;
        }
        if (s_splash_cursor == 0) {
            s_splash.on = s_splash.on ? 0 : 1;
            storage_save_splash(&s_splash);
        } else if (s_splash_cursor == 1) {
            s_splash.mode = s_splash.mode ? 0 : 1;
            storage_save_splash(&s_splash);
        } else if (s_splash_cursor == 2) {
            open_splash_keypad(0);
            return;
        } else {
            open_splash_keypad(1);
            return;
        }
        ui_draw_splash_app(s_splash_cursor, s_splash.on, s_splash.mode,
                           s_splash.callsign, s_splash.welcome);
        return;
    }
    if (s_hmi == HMI_QUICKCFG) {
        if (long_press) {
            page_set();
            return;
        }
        uint16_t bit = (uint16_t)(1u << s_qcfg_cur);
        int cnt = quick_pop(s_quick_mask);
        if (s_quick_mask & bit) {
            if (cnt <= 1) {
                toast("MIN 1");
            } else {
                s_quick_mask &= (uint16_t)~bit;
            }
        } else if (cnt >= QUICK_MAX) {
            toast("MAX 6");
        } else {
            s_quick_mask |= bit;
        }
        storage_quick_t q = { .magic = STORAGE_MAGIC_QUICK, .mask = s_quick_mask };
        storage_save_quick(&q);
        ui_draw_quick_cfg(s_qcfg_cur, s_qcfg_top, s_quick_mask);
        return;
    }
    if (s_hmi == HMI_EIBI_CONFIRM) {
        if (long_press) {
            open_eibi_app();
        } else if (s_eibi_yes) {
            eibi_app_action();
        } else {
            open_eibi_app();
        }
        return;
    }
    if (s_hmi == HMI_ABOUT) {
        page_set();
        return;
    }
    if (s_hmi == HMI_EIBI_DL) {
        return;
    }
    if (s_hmi == HMI_TIMESET) {
        if (long_press) {
            timeset_save();
        } else {
            s_ts_field = (s_ts_field + 1) % 5;
            timeset_draw();
        }
        return;
    }
    if (s_hmi == HMI_WIFIAPP) {
        if (long_press) {
            page_set();
        } else {
            wifi_app_action();
        }
        return;
    }
    if (s_hmi == HMI_SCANNING) {
        if (!long_press) {
            /* 스캔 중에는 클릭으로 취소 */
            s_scan_running = false;
            open_wifi_app();
        }
        return;
    }
    if (s_hmi == HMI_WIFISCAN) {
        if (long_press) {
            open_wifi_app();
        } else if (s_scan_cursor < (int)s_scan_n) {
            open_keypad(s_scan_ptr[s_scan_cursor]);
        }
        return;
    }
    if (s_hmi == HMI_KEYPAD) {
        if (long_press) {
            if (s_kp_mode == KP_MODE_WEATHER) {
                weather_open();
            } else if (s_scan_n > 0) {
                s_hmi = HMI_WIFISCAN;
                s_nav_ms = now;
                scan_list_draw();
            } else {
                open_wifi_app();
            }
        } else {
            keypad_act();
        }
        return;
    }
    if (s_hmi == HMI_SEEK) {
        s_hmi = HMI_TUNE;
        s_ui.seek_mode = false;
        s_nav_ms = now;
        if (s_seeking) {
            seek_stop();
        } else {
            ui_draw_all(&s_ui);
        }
        return;
    }
    if (s_hmi == HMI_DECODE) {
        if (long_press) {
            decode_stop();
            s_hmi = HMI_TUNE;
            s_nav_ms = now;
            page_radio();
        } else {
            decode_cycle_mode();
            ui_decode_refresh();
        }
        return;
    }
    if (s_ats == ATS_SEEK) {
        ats_cancel();
        return;
    }
    if (s_seeking) {
        seek_cancel();
        return;
    }
    switch (s_hmi) {
    case HMI_TUNE:
        if (long_press) {
            s_quick_cursor = 0;
            s_hmi = HMI_QUICK;
            s_nav_ms = now;
            s_input_lock = now + 300;
            ui_draw_quick(&s_ui, s_quick_cursor);
        } else {
            page_menu();
        }
        break;
    case HMI_QUICK:
        if (long_press) {
            s_hmi = HMI_TUNE;
            s_nav_ms = now;
            ui_draw_all(&s_ui);
        } else {
            uint8_t q[QUICK_MAX];
            int n = quick_build(q);
            if (n > 0) {
                if (s_quick_cursor >= n) {
                    s_quick_cursor = 0;
                }
                quick_exec(q[s_quick_cursor]);
            }
        }
        break;
    case HMI_QUICKCFG:
        break;
    case HMI_VOL:
        close_volume();
        break;
    case HMI_HOME:
        launch_app(long_press ? app_find("radio") : s_home_sel);
        break;
    case HMI_CLOCK:
        if (long_press) {
            launch_app(app_find("radio"));
        } else {
            enter_home();
        }
        break;
    case HMI_WEATHER:
        if (long_press) {
            enter_home();
        } else {
            weather_set_mode(1);
            mark_change();
            open_weather_keypad();
        }
        break;
    case HMI_SCAN:
        if (s_spec_active) {
            spectrum_scan_cancel();
        } else {
            spectrum_scan_apply();
        }
        break;
    case HMI_MENU:
        s_nav_ms = now;
        if (long_press) {
            page_radio();
        } else if (menu_item_is_action(s_rmenu.cursor)) {
            menu_action(s_rmenu.cursor);
        } else {
            options_open(HMI_MENU, menu_id_at(s_rmenu.cursor));
        }
        break;
    case HMI_SET: {
        s_nav_ms = now;
        int sid = settings_id_at(s_settings.cursor);
        if (long_press) {
            page_menu();
        } else if (sid == ST_BACK) {
            page_radio();
        } else if (sid == ST_TIME) {
            open_time_set();
        } else if (sid == ST_WIFI) {
            open_wifi_app();
        } else if (sid == ST_EIBI) {
            open_eibi_app();
        } else if (sid == ST_QUICKSET) {
            open_quick_cfg();
        } else if (sid == ST_ABOUT) {
            open_about();
        } else if (sid == ST_BRIGHT) {
            options_open(HMI_SET, ST_BRIGHT_MENU);
        } else if (sid >= ST_MOD_A && sid <= ST_MOD_H) {
            if (!mod_runtime_settings_event(sid)) {
                toast("NO MOD");
            }
        } else {
            options_open(HMI_SET, sid);
        }
        break;
    }
    case HMI_OPT:
        s_nav_ms = now;
        if (long_press && s_opt_page == HMI_MENU &&
            s_opt_item == RM_SQUELCH && s_opt.slider) {
            int sq = mode_dsp_slot(s_ui.mode);
            s_squelch_type[sq] ^= 1;
            s_opt.title = s_squelch_type[sq] ? "SQUELCH SNR" : "SQUELCH RSSI";
            opt_apply(HMI_MENU, RM_SQUELCH, s_opt.value);
            ui_draw_options(&s_opt);
            break;
        }
        options_close(!long_press);
        break;
    case HMI_SEEK:
    case HMI_DECODE:
        break;
    case HMI_WIFIAPP:
    case HMI_SCANNING:
    case HMI_WIFISCAN:
    case HMI_KEYPAD:
        break;
    case HMI_TIMESET:
        break;
    case HMI_FAV:
        break;
    case HMI_ATS_LIST:
        break;
    case HMI_EIBI:
        break;
    case HMI_EIBI_VIEW:
        break;
    case HMI_EIBI_DL:
        break;
    case HMI_EIBI_CONFIRM:
        break;
    case HMI_SPLASH:
        break;
    case HMI_ABOUT:
        break;
    }
}

void button_poll(uint32_t now)
{
    static bool raw = false, stable = false;
    static bool long_fired = false;
    static uint32_t raw_ms = 0, press_ms = 0;

    bool pressed = (gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0);
    if (pressed != raw) {
        raw = pressed;
        raw_ms = now;
    }
    if (raw != stable && (now - raw_ms) >= 30) {
        stable = raw;
        if (stable) {
            press_ms = now;
            long_fired = false;
        } else {
            if (!long_fired) {
                on_button((now - press_ms) >= 800);
            }
            long_fired = false;
        }
    }

    /* 길게 누르면 볼륨 조정 대신 길게 누름 동작을 바로 시작 */
    if (stable && pressed && !long_fired && (now - press_ms) >= 800) {
        long_fired = true;
        if (s_digit_mode) {
            s_digit_swallow = false;
        } else {
            on_button(true);
        }
    }
}
