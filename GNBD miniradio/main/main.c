/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * miniradio main firmware.
 * HMI state machine, input handling, RDS/battery/power management.
 *
 * Input map:
 *   encoder turn        = frequency / volume / menu navigation
 *   encoder short press = enter menu action (menu/option/edit/confirm)
 *   encoder long press  = volume mode (back/cancel)
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
#include "ui_exlu.h"
#include "ui_exlu_app.h"
#include "ui_lua.h"
#include "decode.h"
#include "weather.h"
#include "hmi.h"

const char *TAG = "main";
bool     s_tune_save_pending;
uint32_t s_tune_save_ms;



int s_quick_cursor;
int64_t s_clock_saved_utc;
bool s_auto_bright;
int  s_cpu_saved = 80;
bool s_digit_swallow;
uint32_t s_input_lock;
static uint32_t s_overlay_seq;
uint32_t s_setup_reboot_ms;
void power_save_set(bool on)
{
    if (on) {
        if (!s_power_save) {
            s_cpu_saved = s_cpu_mhz;
        }
        s_power_save = true;
        s_cpu_mhz = 80;
    } else {
        s_power_save = false;
        s_cpu_mhz = (s_cpu_saved >= 80) ? s_cpu_saved : 80;
    }
    cpu_apply(s_cpu_mhz);
    brightness_apply();
    wifi_update_tx_power();
}

const ui_app_t g_apps[] = {
    { "radio",   "RADIO",   "FM", DISPLAY_FONT_SMALL, true,  app_open_radio },
    { "clock",   "CLOCK",   "12", DISPLAY_FONT_SMALL, false, app_open_clock },
    { "weather", "WEATHER", "WX", DISPLAY_FONT_TINY,  false, app_open_weather },
};






int mode_slot(radio_mode_t m)
{
    return (m == RADIO_MODE_FM) ? 0 : (m == RADIO_MODE_AM ? 1 : 2);
}

const char *mode_str(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return "FM";
    case RADIO_MODE_AM:  return "AM";
    case RADIO_MODE_LSB: return "LSB";
    case RADIO_MODE_USB: return "USB";
    }
    return "?";
}

int step_count(int slot)
{
    return (slot == 0) ? FM_STEP_COUNT
         : (slot == 1 ? AM_STEP_COUNT : SSB_STEP_COUNT);
}

uint32_t step_at(int slot, int idx)
{
    switch (slot) {
    case 0:  return FM_STEPS[idx % FM_STEP_COUNT];
    case 1:  return AM_STEPS[idx % AM_STEP_COUNT];
    default: return SSB_STEPS[idx % SSB_STEP_COUNT];
    }
}

uint32_t eff_step_hz(void)
{
    int slot = mode_slot(s_ui.mode);
    int idx = s_step_idx[slot];
    if (idx < 0) {
        return band_table[s_ui.band_idx].step_hz;
    }
    return step_at(slot, idx);
}

void sync_step(void)
{
    s_ui.step_hz = (int)eff_step_hz();
    s_ui.bw_code = s_bw[mode_slot(s_ui.mode)];
    s_ui.bfo_hz = s_bfo;
    s_ui.preset_slot = preset_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
    s_ui.ats_slot = ats_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
}

void mark_change(void)
{
    s_ui.preset_slot = preset_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
    s_ui.ats_slot = ats_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
    s_dirty = true;
    s_change_ms = now_ms();
    if (s_sleep_min > 0) {
        s_sleep_deadline = s_change_ms + (uint32_t)s_sleep_min * 60000u;
        s_sleep_armed = true;
    }
}

void toast(const char *text)
{
    snprintf(s_toast, sizeof(s_toast), "%s", text);
    s_toast_ms = now_ms();
    ui_draw_hint(&s_ui, s_toast);
}

bool toast_active(void)
{
    return s_toast[0] && (now_ms() - s_toast_ms) < TOAST_TIME;
}

void toast_redraw(void)
{
    if (toast_active()) {
        ui_draw_hint(&s_ui, s_toast);
    }
}

/* ---------------------------------------------------------- 오디오 제어 */

esp_err_t radio_set_volume(void)
{
    return audio_set_volume(s_ui.volume);
}

int mode_dsp_slot(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return 0;
    case RADIO_MODE_AM:  return 1;
    case RADIO_MODE_LSB: return 2;
    default:             return 3;
    }
}

/**
 * @brief FM RF LNA 이득 적용 (s_rfgain: 0=AUTO, 1..27 = LNA 인덱스+1)
 *
 * AUTO는 RF AGC를 다시 켜고, 그 외에는 AGC를 정지시켜 이득을 고정한다.
 */
void rfgain_apply(void)
{
    if (!s_radio_ok || s_radio.func != SI4732_FUNC_FM) {
        return;
    }
    esp_err_t err = (s_rfgain == 0)
                    ? si4732_fm_agc_override(&s_radio, false, 0)
                    : si4732_fm_agc_override(&s_radio, true,
                                             (uint8_t)(s_rfgain - 1));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fm agc override failed (%s)", esp_err_to_name(err));
    }
}

void dsp_apply(void)
{
    if (!s_radio_ok) {
        return;
    }
    if (s_radio.func == SI4732_FUNC_FM) {
        si4732_set_fm_soft_mute(&s_radio, 16);
        rfgain_apply();
    } else {
        si4732_set_am_agc(&s_radio, s_agc_on);
        si4732_set_am_avc_max_gain(&s_radio, s_avc);
        si4732_set_am_soft_mute(&s_radio, s_softmute);
    }
}

esp_err_t cpu_apply(int mhz)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t cfg = {
        .max_freq_mhz = mhz,
        .min_freq_mhz = mhz,
        .light_sleep_enable = false,
    };
    return esp_pm_configure(&cfg);
#else
    (void)mhz;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
int s_kp_mode;

void sleep_check(uint32_t now)
{
    if (s_sleep_min == 0) {
        s_sleep_armed = false;
        return;
    }
    if (!s_sleep_armed) {
        s_sleep_armed = true;
        s_sleep_deadline = now + (uint32_t)s_sleep_min * 60000u;
        return;
    }
    {
        int32_t remain = (int32_t)(s_sleep_deadline - now);
        if (remain > 0) {
            return;
        }
    }
    ESP_LOGI(TAG, "sleep timer expired, entering deep sleep (reset to wake)");

    /* 오디오/표시/라디오 전원 차단 */
    board_amp(false);
    display_set_backlight(false);
    if (s_radio_ok) {
        si4732_power_down(&s_radio);
    }
    gpio_set_level((gpio_num_t)BOARD_RADIO_EN_GPIO, 0);

    /* 저장 대기 중인 설정을 마저 기록 */
    if (s_dirty || s_tune_save_pending) {
        do_save();
    }

    /* 슬립 중 전원 제어핀 상태 고정 (LDO/앰프/백라이트 OFF 유지) */
    gpio_hold_en((gpio_num_t)BOARD_RADIO_EN_GPIO);
    gpio_hold_en((gpio_num_t)BOARD_AMP_EN_GPIO);
    gpio_hold_en((gpio_num_t)BOARD_LCD_BL_GPIO);
    gpio_deep_sleep_hold_en();

    /* 웨이크 소스 없이 딥슬립: 리셋(전원 재투입) 전까지 유지 */
    ESP_LOGI(TAG, "deep sleep: all wake sources disabled (boot reason after "
                  "power cycle will show DEEPSLEEP, a reset shows another code)");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
}

static bool redraw_overlay_page(void)
{
    switch (s_hmi) {
    case HMI_MENU:
        rmenu_build();
        ui_draw_radio_panel(&s_ui);
        ui_draw_side_list(UI_TAB_MENU, &s_rmenu, &s_ui);
        return true;
    case HMI_SET:
        settings_build();
        ui_draw_radio_panel(&s_ui);
        ui_draw_side_list(UI_TAB_SET, &s_settings, &s_ui);
        return true;
    case HMI_FAV:
        ui_draw_radio_panel(&s_ui);
        fav_side_draw();
        return true;
    case HMI_ATS_LIST:
        ui_draw_radio_panel(&s_ui);
        ats_side_draw();
        return true;
    case HMI_OPT:
        ui_draw_options(&s_opt);
        return true;
    case HMI_QUICK:
        ui_draw_quick(&s_ui, s_quick_cursor);
        return true;
    case HMI_QUICKCFG:
        ui_draw_quick_cfg(s_qcfg_cur, s_qcfg_top, s_quick_mask);
        return true;
    case HMI_VOL:
        ui_draw_volume(&s_ui, true);
        return true;
    default:
        return false;
    }
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "OTHER";
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_power_init());
    {
        esp_reset_reason_t rr = esp_reset_reason();
        ESP_LOGI(TAG, "reset reason=%d (%s) wakeup=%d", (int)rr,
                 reset_reason_str(rr), (int)esp_sleep_get_wakeup_cause());
    }
    s_clock_ms0 = now_ms();
    s_periodic_ms = now_ms();
    s_last_input_ms = now_ms();

    if (storage_init() != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed");
    }
    if (storage_load_weather_location(s_weather_location,
                                      sizeof(s_weather_location)) != ESP_OK ||
        s_weather_location[0] == '\0') {
        snprintf(s_weather_location, sizeof(s_weather_location), "Seoul");
    }
    storage_settings_t settings;
    esp_err_t settings_err = storage_load_settings(&settings);
    restore_settings(&settings);
    /* 저장된 클럭을 부팅 시점에 실제로 적용한다.
       (restore_settings는 값만 복원하고 하드웨어에는 반영하지 않는다) */
    if (cpu_apply(s_cpu_mhz) != ESP_OK) {
        ESP_LOGW(TAG, "cpu_apply(%d) failed", s_cpu_mhz);
    }
    {
        bool setup_done = false;
        esp_err_t setup_err = storage_load_setup_done(&setup_done);
        if (setup_err == ESP_ERR_NOT_FOUND) {
            setup_done = (settings_err == ESP_OK);
            if (setup_done) {
                storage_save_setup_done(true);
            }
        }
        s_setup_done = setup_done;
    }
    {
        int64_t epoch = 0;
        if (storage_load_clock(&epoch) == ESP_OK &&
            epoch > 1700000000LL && epoch < 4102444800LL) {
            s_clock_base = epoch;
            s_clock_ms0 = now_ms();
            s_clock_set = true;
            s_clock_saved_utc = epoch;
        }
    }
    if (storage_load_presets(&s_presets) != ESP_OK) {
        memset(&s_presets, 0, sizeof(s_presets));
    }
    if (storage_load_names(&s_pnames) != ESP_OK) {
        memset(&s_pnames, 0, sizeof(s_pnames));
        s_pnames.magic = STORAGE_MAGIC_NAMES;
    }
    if (storage_load_ats(&s_ats_list) != ESP_OK) {
        memset(&s_ats_list, 0, sizeof(s_ats_list));
    }
    eibi_fs_mount();
#if EXLU_ENABLE
    if (s_theme == UI_THEME_CUSTOM) {
        exlu_boot_apply_theme();   /* littlefs 의 테마 EXLU 자동 적용 */
    }
#endif
    ui_runtime_init(s_theme == UI_THEME_CUSTOM);
    if (s_theme == UI_THEME_CUSTOM && !ui_runtime_enabled()) {
        s_theme = 0;
    }
    {
        /* Lua 런타임 자가 점검 (PSRAM 힙/샌드박스 확인) */
        lua_State *L = lua_port_new();
        if (L) {
            const char *e = lua_port_exec(L, "return 1+1", 10, "=selftest");
            ESP_LOGI(TAG, "lua selftest %s", e ? e : "ok");
            lua_port_close(L);
        }
    }
    rds_parser_reset(&s_rds);

    /* 디스플레이 설정 */
    display_config_t lcd_cfg = {
        .pin_wr  = BOARD_LCD_WR_GPIO,
        .pin_rd  = BOARD_LCD_RD_GPIO,
        .pin_cs  = BOARD_LCD_CS_GPIO,
        .pin_dc  = BOARD_LCD_DC_GPIO,
        .pin_rst = BOARD_LCD_RST_GPIO,
        .pin_bl  = BOARD_LCD_BL_GPIO,
        .pin_d  = {
            BOARD_LCD_D0_GPIO, BOARD_LCD_D1_GPIO, BOARD_LCD_D2_GPIO, BOARD_LCD_D3_GPIO,
            BOARD_LCD_D4_GPIO, BOARD_LCD_D5_GPIO, BOARD_LCD_D6_GPIO, BOARD_LCD_D7_GPIO,
        },
        .width   = BOARD_LCD_WIDTH,
        .height  = BOARD_LCD_HEIGHT,
        .rotate_landscape = BOARD_LCD_ROTATE_LANDSCAPE,
        .x_gap   = BOARD_LCD_GAP_X,
        .y_gap   = BOARD_LCD_GAP_Y,
        .use_psram = true,
    };

    /* 입력(엔코더/버튼)은 스플래시 복구 메뉴를 위해 디스플레이보다 먼저 초기화 */
    if (encoder_init(BOARD_ENC_A_GPIO, BOARD_ENC_B_GPIO) != ESP_OK) {
        ESP_LOGW(TAG, "encoder init failed");
    }
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BOARD_ENC_PUSH_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    if (display_init(&lcd_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "display init failed");
    } else {
        ui_init();
        if (s_theme == UI_THEME_CUSTOM) {
            ui_runtime_apply_theme();
        }
        brightness_apply();
        splash_show();
        splash_service_input();
#if EXLU_ENABLE
        /* littlefs 의 EXLU 선택 (없으면 즉시 통과) */
        exlu_boot_select();
#endif
        /* 스플래시를 짧게만 노출: 이후 라디오 준비 전까지 화면을 비움 */
        display_fill_screen(UI_BG);
        display_flush();
    }

    if (!s_setup_done) {
        setup_mode();
    }

    /* 엔코더 + 버튼 */


    /* 배터리 */
    battery_init(BOARD_VBAT_ADC_GPIO);

    /* SI4732 */
    gpio_set_level((gpio_num_t)BOARD_RADIO_EN_GPIO, 1);
    vTaskDelay(TICK_MS(50));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    si4732_config_t radio_cfg = SI4732_CONFIG_DEFAULT();
    radio_cfg.i2c_hz = BOARD_I2C_FREQ_HZ;
    radio_cfg.reset_gpio = BOARD_SI4732_RESET_GPIO;
    radio_cfg.timeout_ms = 1000;
    if (si4732_init(&s_radio, s_i2c_bus, &radio_cfg) == ESP_OK) {
        audio_init(&s_radio);
        audio_set_volume(settings.volume);
        s_radio_ok = true;
    } else {
        ESP_LOGE(TAG, "SI4732 init failed (check board.h)");
        s_radio_ok = false;
    }

    /* 밴드/모드 복원 */
    int band_idx = (settings.band_index < band_count) ? (int)settings.band_index : 0;
    radio_mode_t mode = (radio_mode_t)settings.mode;
    if (mode > RADIO_MODE_USB) {
        mode = band_table[band_idx].default_mode;
    }
    uint32_t freq = band_clamp_freq(band_idx, settings.freq_hz);
    int freq_band = band_index_for_freq(freq);
    if (freq_band >= 0) {
        band_idx = freq_band;
    }
    if (band_table[band_idx].default_mode == RADIO_MODE_FM) {
        mode = RADIO_MODE_FM;
    } else if (mode == RADIO_MODE_FM) {
        mode = band_table[band_idx].default_mode;
    }

    s_ui.band_idx  = band_idx;
    s_ui.mode      = mode;
    s_ui.freq_hz   = freq;
    s_ui.radio_ok  = s_radio_ok;
    s_ui.rds_on    = s_rds_on;
    s_ui.rds       = &s_rds;
    s_ui.preset_slot = preset_index_for_freq(freq, band_idx);
    sync_step();
    apply_step_snap();

    if (s_radio_ok) {
        esp_err_t err = radio_apply();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "initial tune failed (%s), retry", esp_err_to_name(err));
            vTaskDelay(TICK_MS(100));
            err = radio_apply();
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "initial tune failed (%s)", esp_err_to_name(err));
            s_radio_ok = false;
            s_ui.radio_ok = false;
        } else {
            si4732_rev_t rev;
            if (si4732_get_rev(&s_radio, &rev) == ESP_OK) {
                ESP_LOGI(TAG, "SI4732 PN=0x%02X FW=%u.%u CHIPREV=0x%02X",
                         rev.pn, rev.fw_major, rev.fw_minor, rev.chip_rev);
            }
            board_amp(true);
            ESP_LOGI(TAG, "start band=%s mode=%s freq=%lu Hz vol=%u patch=%u",
                     band_table[s_ui.band_idx].name, mode_str(s_ui.mode),
                     (unsigned long)s_ui.freq_hz, audio_get_volume(),
                     (unsigned)si4732_ssb_patch_size);
        }
    }

    battery_poll(now_ms());

    if (s_wifi_mode == 1) {
        if (wifi_start_ap() != ESP_OK) { s_wifi_mode = 0; }
    } else if (s_wifi_mode == 2 && s_wifi_ssid[0]) {
        if (wifi_sta_connect() != ESP_OK) { s_wifi_mode = 0; }
    }

    s_ui.wifi_state = s_wifi_mode;
    clock_update();
    page_radio();
    display_flush();
    mod_runtime_init();

#ifdef CONFIG_ESP_TASK_WDT
    esp_task_wdt_add(NULL);
#endif

    s_last_tune_ms = now_ms();

    while (1) {
        /* 메뉴 우선 렌더: 라디오 UI가 다시 그려지면 오버레이를 위에 재draw */
        uint32_t now = now_ms();
#ifdef CONFIG_ESP_TASK_WDT
        esp_task_wdt_reset();
#endif
        on_encoder(encoder_get_delta());
        button_poll(now);
        tune_commit(now);
        if (s_tune_save_pending &&
            (now - s_tune_save_ms) >= 5000u) {
            s_tune_save_pending = false;
            mark_change();
            s_change_ms = now - SAVE_DELAY;
        }
        web_poll();
        if (s_freq_dirty && s_pending_steps == 0) {
            s_freq_dirty = false;
            if (s_hmi == HMI_MENU) {
                rmenu_build();
                ui_draw_radio_panel(&s_ui);
                ui_draw_side_list(UI_TAB_MENU, &s_rmenu, &s_ui);
            } else if (s_hmi == HMI_SET) {
                settings_build();
                ui_draw_radio_panel(&s_ui);
                ui_draw_side_list(UI_TAB_SET, &s_settings, &s_ui);
            } else if (s_hmi == HMI_FAV) {
                ui_draw_radio_panel(&s_ui);
                fav_side_draw();
            } else if (s_hmi == HMI_ATS_LIST) {
                ui_draw_radio_panel(&s_ui);
                ats_side_draw();
            } else if (s_hmi == HMI_HOME) {
                ui_draw_home(&s_ui, s_home_sel, g_apps, APP_COUNT);
            } else if (s_hmi == HMI_CLOCK) {
                ui_draw_clock(&s_ui);
            } else if (s_hmi == HMI_WEATHER) {
                weather_redraw();
            } else if (s_hmi == HMI_SCAN) {
                ui_draw_spectrum(&s_ui, band_table[s_spec_band].name,
                                 s_spec_min, s_spec_max, s_spec_rssi,
                                 s_spec_snr, s_spec_count, s_spec_idx,
                                  s_spec_cursor, s_spec_done);
            } else if (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK) {
                ui_draw_radio_freq(&s_ui);
            }
        }
        if ((s_hmi == HMI_TUNE || s_hmi == HMI_SEEK) &&
            s_sleep_min > 0 && s_sleep_armed) {
            static uint32_t sleep_badge_ms;
            if ((now - sleep_badge_ms) >= 1000u) {
                sleep_badge_ms = now;
                ui_draw_sleep_badge();
            }
        }
        seek_poll(now);
        ats_poll(now);
        if (s_hmi == HMI_DECODE && decode_poll(now)) {
            ui_decode_refresh();
        }
        {
            static ats_state_t s_prev_ats = ATS_IDLE;
            bool just_done = (s_prev_ats == ATS_SEEK && s_ats == ATS_DONE);
            s_prev_ats = s_ats;
            if ((just_done || s_ats_autolist) && s_hmi != HMI_ATS_LIST) {
                s_ats_autolist = false;
                s_toast[0] = '\0';
                open_ats_list();
            } else if (s_ats_autolist) {
                s_ats_autolist = false;
            }
            if (s_ats_show_until) {
                s_ats_show_until = 0;
                if (s_hmi != HMI_ATS_LIST) {
                    open_ats_list();
                }
            }
        }
        spectrum_scan_poll(now);
        if (s_powered) {
            rsq_poll(now);
            rds_poll(now);
        }
        mod_runtime_tick(now);
        battery_poll(now);
        weather_poll();

        if (s_scan_running && s_scan_done) {
            s_scan_running = false;
            if (s_hmi == HMI_SCANNING) {
                open_scan_list();
            } else {
                build_scan_arrays();
            }
        }
        if (s_wifi_got_ip) {
            s_wifi_got_ip = false;
            if (s_hmi == HMI_WIFIAPP || s_hmi == HMI_SCANNING ||
                s_hmi == HMI_WIFISCAN) {
                page_radio();
                toast("WI-FI CONNECTED");
            }
        }
        if (s_eibi_state == 2) {
            s_eibi_state = 0;
            s_eibi_force = true;
            ESP_LOGI(TAG, "EIBI stored %d entries", eibi_record_count());
            if (s_hmi == HMI_EIBI_DL) {
                open_eibi_app();
            }
            toast("EIBI OK");
            eibi_refresh();
        } else if (s_eibi_state == 3) {
            s_eibi_state = 0;
            if (s_hmi == HMI_EIBI_DL) {
                open_eibi_app();
            }
            toast("EIBI FAIL");
        }
        if (s_hmi == HMI_EIBI_DL && s_eibi_state == 1 &&
            (now - s_eibi_dl_ms) > 250) {
            s_eibi_dl_ms = now;
            ui_draw_eibi_dl(s_eibi_entries, s_eibi_bytes, (int)(now / 250));
        }
        if ((now - s_eibi_ms) > 1000) {
            s_eibi_ms = now;
            eibi_refresh();
        }
        {
            static uint32_t st_ms;
            if (s_ui.mode != RADIO_MODE_FM &&
                s_ui.station && (now - st_ms) > 300) {
                st_ms = now;
                if (s_hmi == HMI_TUNE) {
                    ui_draw_all(&s_ui);
                } else if (s_hmi == HMI_MENU || s_hmi == HMI_SET ||
                           s_hmi == HMI_FAV || s_hmi == HMI_ATS_LIST) {
                    /* 커스텀 UI 는 패널 재그림 시 화면 전체를 다시 그리므로
                       EIBI 갱신마다 깜빡임 -> 커스텀 모드에서는 redraw 하지 않고
                       값만 저장(메뉴 종료 후 라디오로 돌아올 때 반영) */
                    if (!ui_custom_active()) {
                        ui_draw_radio_panel(&s_ui);
                    }
                }
            }
        }
        if (s_hmi == HMI_SCANNING && (now - s_scan_anim_ms) > 250) {
            s_scan_anim_ms = now;
            ui_draw_wifi_scanning((int)(now / 250));
        }

        if (s_sntp_started && esp_netif_sntp_sync_wait(0) == ESP_OK) {
            time_t t = time(NULL);
            if (t > 1700000000) {
                int64_t cur = s_clock_set ? clock_utc_now() : 0;
                if (!s_clock_set || t < cur - 5 || t > cur + 5) {
                    s_clock_base = (int64_t)t;
                    s_clock_ms0 = now;
                    s_clock_set = true;
                    clock_save_now();
                }
            }
        }
        clock_update();
        if (s_clock_set) {
            int64_t utc = clock_utc_now();
            if (utc / 60 != s_clock_saved_utc / 60) {
                storage_save_clock(utc);
                s_clock_saved_utc = utc;
            }
        }
        if (s_digit_mode && s_hmi != HMI_TUNE) {
            s_digit_mode = false;
            s_digit_swallow = false;
        }
        if (s_auto_bright) {
            static int last_auto_min = -1;
            int yy, mo, dd, hh, mm;
            local_datetime(&yy, &mo, &dd, &hh, &mm);
            int cur = hh * 60 + mm;
            if (s_clock_set && cur != last_auto_min) {
                last_auto_min = cur;
                brightness_apply();
            }
        }
        if ((s_hmi == HMI_ATS_LIST || s_hmi == HMI_FAV) &&
            (now - s_list_redraw_ms) > 200) {
            s_list_redraw_ms = now;
            if (s_hmi == HMI_ATS_LIST) {
                ats_side_draw();
            } else {
                fav_side_draw();
            }
        }

        if (s_hmi == HMI_VOL && (now - s_vol_ms) > VOL_TIMEOUT) {
            close_volume();
        }
        if (s_hmi == HMI_QUICK && (int32_t)(now - s_nav_ms) > OPT_TIMEOUT) {
            s_hmi = HMI_TUNE;
            ui_draw_all(&s_ui);
        }
        if (s_hmi == HMI_OPT) {
            if ((int32_t)(now - s_nav_ms) > OPT_TIMEOUT) {
                options_close(true);
            }
        } else if (s_hmi == HMI_MENU && (int32_t)(now - s_nav_ms) > MENU_IDLE_MS) {
            page_radio();
        } else if (s_hmi == HMI_SET && (int32_t)(now - s_nav_ms) > MENU_IDLE_MS) {
            page_radio();
        } else if ((s_hmi == HMI_FAV || s_hmi == HMI_ATS_LIST) &&
                   (int32_t)(now - s_nav_ms) > LIST_IDLE_MS) {
            page_radio();
        }
        if (s_screen_sec > 0 && !s_screen_off &&
            (now - s_last_input_ms) >= (uint32_t)s_screen_sec * 1000u) {
            display_set_backlight(false);
            s_screen_off = true;
            if (s_hmi == HMI_QUICK) {
                s_hmi = HMI_TUNE;
            }
        }
        if (s_screen_off) {
            /* 화면 꺼짐 해제: 엔코더/버튼 입력 시 복귀 (on_encoder/on_button에서 처리) */
            if (s_hmi == HMI_QUICK) {
                s_hmi = HMI_TUNE;
            }
        }
        if (s_toast[0] && (int32_t)(now - s_toast_ms) > TOAST_TIME) {
            s_toast[0] = '\0';
            if (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK) {
                ui_draw_hint(&s_ui, NULL);
            } else if (s_hmi == HMI_MENU || s_hmi == HMI_SET) {
                page_redraw();
            } else if (s_hmi == HMI_OPT) {
                ui_draw_options(&s_opt);
            } else if (s_hmi == HMI_WIFIAPP) {
                wifi_app_draw();
            } else if (s_hmi == HMI_SCANNING) {
                ui_draw_wifi_scanning(0);
            } else if (s_hmi == HMI_WIFISCAN) {
                scan_list_draw();
            } else if (s_hmi == HMI_KEYPAD) {
                ui_draw_keypad(&s_kp);
            } else if (s_hmi == HMI_FAV) {
                fav_side_draw();
            } else if (s_hmi == HMI_ATS_LIST) {
                ats_side_draw();
            } else if (s_hmi == HMI_HOME) {
                ui_draw_home(&s_ui, s_home_sel, g_apps, APP_COUNT);
            } else if (s_hmi == HMI_CLOCK) {
                ui_draw_clock(&s_ui);
            } else if (s_hmi == HMI_VOL) {
                ui_draw_volume(&s_ui, true);
            } else if (s_hmi == HMI_QUICK) {
                ui_draw_quick(&s_ui, s_quick_cursor);
            } else if (s_hmi == HMI_QUICKCFG) {
                ui_draw_quick_cfg(s_qcfg_cur, s_qcfg_top, s_quick_mask);
            } else if (s_hmi == HMI_EIBI) {
                ui_draw_eibi_app(s_eibi_cursor, eibi_available(),
                                 eibi_record_count());
            } else if (s_hmi == HMI_EIBI_VIEW) {
                ui_draw_eibi_view(s_eibi_view_cur, s_eibi_view_top,
                                  eibi_record_count());
            } else if (s_hmi == HMI_EIBI_DL) {
                ui_draw_eibi_dl(s_eibi_entries, s_eibi_bytes, (int)(now / 250));
            }
        }
        if (s_ui.save_active && (now - s_save_ms) > 1200) {
            s_ui.save_active = false;
            ui_refresh_top();
        }
        save_flush(now);
        sleep_check(now);
        if (s_radio_draw_seq != s_overlay_seq && redraw_overlay_page()) {
            s_overlay_seq = s_radio_draw_seq;
        }
        toast_redraw();
        display_flush();

        if (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK ||
            s_hmi == HMI_DECODE) {
            vTaskDelay(TICK_MS(10));
        } else {
            vTaskDelay(TICK_MS(20));
        }
    }
}
