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
#include "weather.h"

static const char *TAG = "main";
static bool     s_tune_save_pending;
static uint32_t s_tune_save_ms;

#define TICK_MS(ms)        pdMS_TO_TICKS(ms)
#define VOL_TIMEOUT        3000
#define MENU_IDLE_MS       5000
#define LIST_IDLE_MS       300000
#define OPT_TIMEOUT        20000
#define TOAST_TIME         2000
#define SAVE_DELAY         2000
#define RDS_PERIOD         500
#define BATT_PERIOD        2000
#define TUNE_COALESCE_MS   15
#define BFO_MAX_HZ         5000

static void app_open_radio(void);
static void app_open_clock(void);
static void app_open_weather(void);

static int s_quick_cursor;
static int64_t s_clock_saved_utc;
static bool s_opt_quick;
static bool s_auto_bright;
static int  s_cpu_saved;
static bool s_digit_swallow;
static uint32_t s_input_lock;
static uint32_t s_overlay_seq;
static bool s_wifi_sel;
static int s_wifi_focus;
static uint32_t s_wifi_scan_ms;
static uint32_t s_setup_reboot_ms;
#define ST_BRIGHT_MENU  0x7E
#define ST_BRIGHT_LEVEL 0x7F
static void brightness_apply(void);
static void power_save_set(bool on)
{
    if (on) {
        if (!s_power_save) {
            s_cpu_saved = s_cpu_mhz;
        }
        s_power_save = true;
        s_cpu_mhz = 80;
    } else {
        s_power_save = false;
        s_cpu_mhz = (s_cpu_saved >= 80) ? s_cpu_saved : 240;
    }
    cpu_apply(s_cpu_mhz);
    brightness_apply();
}
#define QUICK_ITEMS 4

static const ui_app_t g_apps[] = {
    { "radio",   "RADIO",   "FM", DISPLAY_FONT_SMALL, true,  app_open_radio },
    { "clock",   "CLOCK",   "12", DISPLAY_FONT_SMALL, false, app_open_clock },
    { "weather", "WEATHER", "WX", DISPLAY_FONT_TINY,  false, app_open_weather },
};

#define APP_COUNT ((int)(sizeof(g_apps) / sizeof(g_apps[0])))




int preset_index_for_freq(uint32_t freq, int band);

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

void dsp_apply(void)
{
    if (!s_radio_ok) {
        return;
    }
    if (s_radio.func == SI4732_FUNC_FM) {
        si4732_set_fm_soft_mute(&s_radio, 16);
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

void rmenu_build(void)
{
    s_rmenu.labels = RM_LABELS;
    s_rmenu.values = s_rv;
    s_rmenu.count = RM_COUNT;

    for (int i = 0; i < RM_COUNT; ++i) {
        s_rv[i] = s_rbuf[i];
    }

    s_rbuf[RM_BACK][0] = 0;
    snprintf(s_rbuf[RM_BAND], sizeof(s_rbuf[RM_BAND]), "%s",
             band_table[s_ui.band_idx].name);
    snprintf(s_rbuf[RM_VOLUME], sizeof(s_rbuf[RM_VOLUME]), "%u",
             audio_get_volume());
    snprintf(s_rbuf[RM_MODE], sizeof(s_rbuf[RM_MODE]), "%s",
             mode_str(s_ui.mode));
    snprintf(s_rbuf[RM_BW], sizeof(s_rbuf[RM_BW]), "%s", bw_label());

    int slot = mode_slot(s_ui.mode);
    if (s_step_idx[slot] < 0) {
        snprintf(s_rbuf[RM_STEP], sizeof(s_rbuf[RM_STEP]), "BAND");
    } else {
        int st = (int)eff_step_hz();
        if (st % 1000 == 0) {
            snprintf(s_rbuf[RM_STEP], sizeof(s_rbuf[RM_STEP]), "%dK", st / 1000);
        } else {
            snprintf(s_rbuf[RM_STEP], sizeof(s_rbuf[RM_STEP]), "%dH", st);
        }
    }

    snprintf(s_rbuf[RM_SEEK], sizeof(s_rbuf[RM_SEEK]), "MODE");
    snprintf(s_rbuf[RM_SCAN], sizeof(s_rbuf[RM_SCAN]), "START");

    {
        int filled = 0;
        for (int i = 0; i < STORAGE_PRESET_SLOTS; ++i) {
            if (s_presets.slot[i].freq_hz != 0) {
                ++filled;
            }
        }
        snprintf(s_rbuf[RM_FAV], sizeof(s_rbuf[RM_FAV]), "%d", filled);
    }

    snprintf(s_rbuf[RM_ATS], sizeof(s_rbuf[RM_ATS]), "MENU");
    snprintf(s_rbuf[RM_RDS], sizeof(s_rbuf[RM_RDS]), "%s",
             s_rds_on ? "ON" : "OFF");
    snprintf(s_rbuf[RM_STEREO], sizeof(s_rbuf[RM_STEREO]), "%s",
             s_stereo_force ? "MONO" : "AUTO");
    snprintf(s_rbuf[RM_DEEMPH], sizeof(s_rbuf[RM_DEEMPH]), "%s",
             s_deemph_75 ? "75us" : "50us");
    snprintf(s_rbuf[RM_AGC], sizeof(s_rbuf[RM_AGC]), "%s",
             s_agc_on ? "ON" : "OFF");
    snprintf(s_rbuf[RM_AVC], sizeof(s_rbuf[RM_AVC]), "%u", s_avc);
    snprintf(s_rbuf[RM_SOFTMUTE], sizeof(s_rbuf[RM_SOFTMUTE]), "%udB",
             s_softmute);
    if (s_squelch_on) {
        int sq = mode_dsp_slot(s_ui.mode);
        snprintf(s_rbuf[RM_SQUELCH], sizeof(s_rbuf[RM_SQUELCH]), "%s%u",
                 s_squelch_type[sq] ? "SNR" : "RSSI", s_squelch_thr[sq]);
    } else {
        snprintf(s_rbuf[RM_SQUELCH], sizeof(s_rbuf[RM_SQUELCH]), "OFF");
    }
    snprintf(s_rbuf[RM_SETTINGS], sizeof(s_rbuf[RM_SETTINGS]), ">");

    if (band_is_ssb(s_ui.mode) && si4732_ssb_patch_size > 0) {
        snprintf(s_rbuf[RM_BFO], sizeof(s_rbuf[RM_BFO]), "%+ld", (long)s_bfo);
    } else {
        snprintf(s_rbuf[RM_BFO], sizeof(s_rbuf[RM_BFO]), "N/A");
    }
}

void local_datetime(int *y, int *mo, int *d, int *h, int *mi);

void settings_build(void)
{
    s_settings.labels = ST_LABELS;
    s_settings.values = s_sv;
    s_settings.count = ST_COUNT;

    for (int i = 0; i < ST_COUNT; ++i) {
        s_sv[i] = s_sbuf[i];
    }

    s_sbuf[ST_BACK][0] = 0;
    unsigned bright_shown = s_brightness;
    if (s_power_save && bright_shown > 65) {
        bright_shown = 65;
    }
    if (s_auto_bright) {
        snprintf(s_sbuf[ST_BRIGHT], sizeof(s_sbuf[ST_BRIGHT]), "AUTO %u%%",
                 bright_shown);
    } else {
        snprintf(s_sbuf[ST_BRIGHT], sizeof(s_sbuf[ST_BRIGHT]), "%u%%",
                 bright_shown);
    }
    if (s_sleep_min == 0) {
        snprintf(s_sbuf[ST_SLEEP], sizeof(s_sbuf[ST_SLEEP]), "OFF");
    } else {
    snprintf(s_sbuf[ST_SLEEP], sizeof(s_sbuf[ST_SLEEP]), "%um",
             s_sleep_min);
    }
    snprintf(s_sbuf[ST_CPU], sizeof(s_sbuf[ST_CPU]), "%dMHz", s_cpu_mhz);
    snprintf(s_sbuf[ST_POWERSAVE], sizeof(s_sbuf[ST_POWERSAVE]), "%s",
             s_power_save ? "ON" : "OFF");
    snprintf(s_sbuf[ST_UTC], sizeof(s_sbuf[ST_UTC]), "%+d:%02d",
             s_utc_min / 60, abs(s_utc_min % 60));
    snprintf(s_sbuf[ST_WIFI], sizeof(s_sbuf[ST_WIFI]), "%s",
             (s_wifi_mode == 1) ? "AP" :
             (s_wifi_mode == 2) ? "STA" : "OFF");
    {
        int yy, mo, dd, hh, mm;
        local_datetime(&yy, &mo, &dd, &hh, &mm);
        snprintf(s_sbuf[ST_TIME], sizeof(s_sbuf[ST_TIME]), "%02d-%02d %02d:%02d",
                 mo, dd, hh, mm);
    }
    if (s_screen_sec == 0) {
        snprintf(s_sbuf[ST_SCREEN], sizeof(s_sbuf[ST_SCREEN]), "OFF");
    } else {
        snprintf(s_sbuf[ST_SCREEN], sizeof(s_sbuf[ST_SCREEN]), "%us",
                 (unsigned)s_screen_sec);
    }
    snprintf(s_sbuf[ST_BATT], sizeof(s_sbuf[ST_BATT]), "%s",
             (const char *[]){"ICON", "VOLT", "PERCENT", "% + ICON",
                              "V + ICON"}[s_batt_view > 4 ? 3 : s_batt_view]);
    if (s_batt_cal_mv == 0) {
        snprintf(s_sbuf[ST_BATT_CAL], sizeof(s_sbuf[ST_BATT_CAL]), "0mV");
    } else {
        snprintf(s_sbuf[ST_BATT_CAL], sizeof(s_sbuf[ST_BATT_CAL]), "%+dmV",
                 (int)s_batt_cal_mv);
    }
    snprintf(s_sbuf[ST_UNIT], sizeof(s_sbuf[ST_UNIT]), "%s",
             s_unit_mode ? "kHz" : "MHz");
    snprintf(s_sbuf[ST_THEME], sizeof(s_sbuf[ST_THEME]), "%s",
             (const char *[]){"OFF", "BLACK", "WHITE", "LIGHT", "BLUE",
                              "GREEN"}
                 [s_theme > 5 ? 0 : s_theme]);
    snprintf(s_sbuf[ST_EIBI], sizeof(s_sbuf[ST_EIBI]), "%s",
             (s_eibi_state == 1) ? "BUSY"
             : eibi_available() ? "READY" : "NONE");
    snprintf(s_sbuf[ST_ABOUT], sizeof(s_sbuf[ST_ABOUT]), "INFO");
    {
        int qc = 0;
        for (int i = 0; i < QA_COUNT; ++i) {
            if (s_quick_mask & (1u << i)) {
                ++qc;
            }
        }
        snprintf(s_sbuf[ST_QUICKSET], sizeof(s_sbuf[ST_QUICKSET]), "%d/%d",
                 qc, QUICK_MAX);
    }
}

void opt_label(int i, const char *text)
{
    if (i < 0 || i >= UI_OPT_MAX) {
        return;
    }
    snprintf(s_opt_buf[i], sizeof(s_opt_buf[i]), "%s", text ? text : "");
    s_opt_ptr[i] = s_opt_buf[i];
}

static const uint8_t BW_ORDER[3][7] = {
    {0, 4, 3, 2, 1, 0, 0},
    {4, 5, 3, 6, 2, 1, 0},
    {4, 5, 0, 1, 2, 3, 0},
};

static int bw_code_from_idx(int slot, int idx)
{
    int n = bw_max() + 1;
    if (idx < 0 || idx >= n) {
        return 0;
    }
    return BW_ORDER[slot][idx];
}

int bw_index(int slot, int code)
{
    int n = bw_max() + 1;
    for (int i = 0; i < n; ++i) {
        if (BW_ORDER[slot][i] == code) {
            return i;
        }
    }
    return 0;
}

int opt_sel(int page, int item)
{
    int slot = mode_slot(s_ui.mode);
    if (page == HMI_MENU) {
        switch (item) {
        case RM_BAND:    return s_ui.band_idx;
        case RM_MODE:    return (band_table[s_ui.band_idx].default_mode ==
                                 RADIO_MODE_FM) ? 0
                                : ((int)s_ui.mode - (int)RADIO_MODE_AM);
        case RM_BW:      return bw_index(slot, s_bw[slot]);
        case RM_STEP:    return (s_step_idx[slot] < 0) ? 0 : (s_step_idx[slot] + 1);
        case RM_RDS:     return s_rds_on ? 0 : 1;
        case RM_STEREO:  return s_stereo_force ? 1 : 0;
        case RM_DEEMPH:  return s_deemph_75 ? 1 : 0;
        case RM_AGC:     return s_agc_on ? 0 : 1;
        case RM_AVC:     return s_avc;
        case RM_SOFTMUTE: return s_softmute;
        case RM_SQUELCH: return s_squelch_on ? s_squelch_thr[mode_dsp_slot(s_ui.mode)] : 0;
        case RM_BFO: {
            return (int)s_bfo;
        }
        }
    } else {
        switch (item) {
        case ST_BRIGHT_MENU:  return 0;
        case ST_BRIGHT_LEVEL: return s_brightness;
        case ST_CPU:    return (s_cpu_mhz >= 240) ? 2 : (s_cpu_mhz >= 160 ? 1 : 0);
        case ST_UTC:    return s_utc_min / 60 + 12;
        case ST_WIFI:   return (s_wifi_mode <= 2) ? s_wifi_mode : 0;
        case ST_BATT:   return (s_batt_view <= 4) ? s_batt_view : 3;
        case ST_BATT_CAL: return s_batt_cal_mv;
        case ST_SCREEN: return s_screen_sec;
        case ST_POWERSAVE: return s_power_save ? 0 : 1;
        case ST_UNIT:   return s_unit_mode;
        case ST_THEME:  return (s_theme <= 5) ? s_theme : 0;
        case ST_SLEEP:  return s_sleep_min;
        }
    }
    return 0;
}

void ui_refresh_top(void);
void open_favorites(void);
void open_ats_list(void);
void page_radio(void);
void fmt_freq_short(uint32_t hz, int mode, char *buf, size_t n);
void clock_update(void);
void local_datetime(int *y, int *mo, int *d, int *h, int *mi);
static int64_t clock_utc_now(void);
static void clock_save_now(void);

void options_open(int page, int item)
{
    int n = 0;
    int sel = opt_sel(page, item);
    char tmp[24];

    bool slider = false;
    bool live = true;
    int vmin = 0, vmax = 0, vstep = 1, vval = 0;
    const char *unit = NULL;
    const char *zero = NULL;

    if (page == HMI_MENU) {
        switch (item) {
        case RM_BAND:
            for (size_t i = 0; i < band_count && n < UI_OPT_MAX; ++i) {
                opt_label(n++, band_table[i].name);
            }
            s_opt.title = "BAND";
            break;
        case RM_MODE:
            if (band_table[s_ui.band_idx].default_mode == RADIO_MODE_FM) {
                opt_label(n++, "FM");
            } else {
                opt_label(n++, "AM");
                opt_label(n++, "LSB");
                opt_label(n++, "USB");
            }
            s_opt.title = "MODE";
            break;
        case RM_BW: {
            static const char *fm[]  = {"AUTO", "40K", "60K", "84K", "110K"};
            static const char *am[]  = {"1K", "1.8K", "2K", "2.5K", "3K", "4K", "6K"};
            static const char *ssb[] = {"500", "1K", "1.2K", "2.2K", "3K", "4K"};
            int slot = mode_slot(s_ui.mode);
            int cnt = bw_max() + 1;
            for (int i = 0; i < cnt; ++i) {
                opt_label(n++, slot == 0 ? fm[i] : (slot == 1 ? am[i] : ssb[i]));
            }
            s_opt.title = "BANDWIDTH";
            break;
        }
        case RM_STEP: {
            int slot = mode_slot(s_ui.mode);
            opt_label(n++, "BAND");
            for (int i = 0; i < step_count(slot); ++i) {
                int hz = (int)step_at(slot, i);
                if (hz == 10) {
                    snprintf(tmp, sizeof(tmp), "0.01K");
                } else if (hz == 100) {
                    snprintf(tmp, sizeof(tmp), "0.1K");
                } else if (hz % 1000 == 0) {
                    snprintf(tmp, sizeof(tmp), "%dK", hz / 1000);
                } else {
                    snprintf(tmp, sizeof(tmp), "%dH", hz);
                }
                opt_label(n++, tmp);
            }
            s_opt.title = "STEP";
            break;
        }
        case RM_ATS:
            opt_label(n++, "ATS RUN");
            opt_label(n++, "SAVED LIST");
            s_opt.title = "ATS";
            live = false;
            break;
        case RM_RDS:
            opt_label(n++, "ON");
            opt_label(n++, "OFF");
            s_opt.title = "RDS";
            break;
        case RM_STEREO:
            opt_label(n++, "AUTO");
            opt_label(n++, "MONO");
            s_opt.title = "STEREO";
            break;
        case RM_DEEMPH:
            opt_label(n++, "50us");
            opt_label(n++, "75us");
            s_opt.title = "DEEMPHASIS";
            break;
        case RM_AGC:
            opt_label(n++, "ON");
            opt_label(n++, "OFF");
            s_opt.title = "AGC";
            break;
        case RM_AVC:
            slider = true;
            vmin = 0; vmax = 90; vstep = 1; vval = s_avc;
            unit = " dB";
            s_opt.title = "AVC GAIN";
            break;
        case RM_SOFTMUTE:
            slider = true;
            vmin = 0; vmax = 63; vstep = 1; vval = s_softmute;
            unit = " dB";
            s_opt.title = "SOFT MUTE";
            break;
        case RM_SQUELCH: {
            slider = true;
            vmin = 0; vmax = 100; vstep = 1;
            int sq = mode_dsp_slot(s_ui.mode);
            vval = s_squelch_on ? s_squelch_thr[sq] : 0;
            zero = "OFF";
            s_opt.title = s_squelch_type[sq] ? "SQUELCH SNR" : "SQUELCH RSSI";
            break;
        }
        case RM_BFO:
            if (!band_is_ssb(s_ui.mode) || si4732_ssb_patch_size == 0) {
                opt_label(n++, "N/A");
            } else {
                slider = true;
                vmin = -BFO_MAX_HZ; vmax = BFO_MAX_HZ; vstep = 1;
                vval = (int)s_bfo;
                unit = " Hz";
            }
            s_opt.title = "BFO";
            break;
        default:
            return;
        }
    } else {
        switch (item) {
        case ST_BRIGHT_MENU:
            snprintf(tmp, sizeof(tmp), "BRIGHT %u%%", s_brightness);
            opt_label(n++, tmp);
            opt_label(n++, s_auto_bright ? "AUTO BRIGHT  ON"
                                         : "AUTO BRIGHT  OFF");
            s_opt.title = "BRIGHTNESS";
            live = false;
            break;
        case ST_BRIGHT_LEVEL:
            slider = true;
            vmin = 1; vmax = 100; vstep = 1; vval = s_brightness;
            unit = "%";
            s_opt.title = "BRIGHTNESS";
            break;
        case ST_SLEEP:
            slider = true;
            vmin = 0; vmax = 100; vstep = 1; vval = s_sleep_min;
            unit = "m";
            zero = "OFF";
            s_opt.title = "SLEEP";
            break;
        case ST_POWERSAVE:
            opt_label(n++, "ON");
            opt_label(n++, "OFF");
            s_opt.title = "POWER SAVE";
            break;
        case ST_UTC:
            slider = true;
            vmin = -12; vmax = 14; vstep = 1; vval = s_utc_min / 60;
            unit = "h";
            s_opt.title = "UTC OFFSET";
            break;
        case ST_SCREEN:
            slider = true;
            vmin = 0; vmax = 120; vstep = 1; vval = s_screen_sec;
            unit = "s";
            zero = "OFF";
            s_opt.title = "SCREEN OFF";
            break;
        case ST_UNIT:
            opt_label(n++, "MHz");
            opt_label(n++, "kHz");
            s_opt.title = "FREQ UNIT";
            break;
        case ST_THEME:
            opt_label(n++, "OFF");
            opt_label(n++, "BLACK");
            opt_label(n++, "WHITE");
            opt_label(n++, "LIGHT");
            opt_label(n++, "BLUE");
            opt_label(n++, "GREEN");
            s_opt.title = "THEME";
            live = false;
            break;
        case ST_CPU:
            opt_label(n++, "80 MHz");
            opt_label(n++, "160 MHz");
            opt_label(n++, "240 MHz");
            s_opt.title = "CPU CLOCK";
            break;
        case ST_WIFI:
            opt_label(n++, "OFF");
            opt_label(n++, "AP");
            opt_label(n++, "CONNECT");
            opt_label(n++, "SCAN");
            s_opt.title = "WI-FI";
            live = false;
            break;
        case ST_BATT:
            opt_label(n++, "ICON");            opt_label(n++, "VOLTAGE");
            opt_label(n++, "PERCENT");
            opt_label(n++, "PERCENT + ICON");
            opt_label(n++, "VOLTAGE + ICON");
            s_opt.title = "BATTERY VIEW";
            break;
        case ST_BATT_CAL:
            slider = true;
            vmin = -1000; vmax = 1000; vstep = 10;
            vval = s_batt_cal_mv;
            unit = " mV";
            s_opt.title = "BATTERY CAL";
            break;
        default:
            return;
        }
    }

    s_opt.slider = slider;
    s_opt.live = live;
    s_opt.min = vmin;
    s_opt.max = vmax;
    s_opt.step = vstep;
    s_opt.value = vval;
    s_opt.unit = unit;
    s_opt.zero_label = zero;

    if (slider) {
        s_opt.labels = s_opt_ptr;
        s_opt.count = 1;
        s_opt.cursor = 0;
        s_opt.sel = -1;
        s_opt.top = 0;
    } else {
        if (n <= 0) {
            return;
        }
        if (sel >= n) {
            sel = n - 1;
        }

        s_opt.labels = s_opt_ptr;
        s_opt.count = n;
        s_opt.cursor = sel;
        s_opt.sel = sel;
        s_opt.top = 0;
        if (s_opt.cursor >= UI_OPT_ROWS) {
            s_opt.top = s_opt.cursor - UI_OPT_ROWS + 1;
        }
    }

    s_opt_page = page;
    s_opt_item = item;
    s_hmi = HMI_OPT;
    s_nav_ms = now_ms();
    s_input_lock = now_ms() + 300;
    ui_draw_options(&s_opt);
}

void opt_apply(int page, int item, int idx)
{
    int slot = mode_slot(s_ui.mode);

    if (page == HMI_MENU) {
        switch (item) {
        case RM_BAND:
            set_band(idx);
            break;
        case RM_MODE: {
            radio_mode_t m = (band_table[s_ui.band_idx].default_mode ==
                              RADIO_MODE_FM)
                             ? RADIO_MODE_FM
                             : (radio_mode_t)((int)RADIO_MODE_AM + idx);
            s_ui.mode = m;
            sync_step();
            apply_step_snap();
            radio_apply();
            mark_change();
            break;
        }
        case RM_BW:
            s_bw[slot] = (uint8_t)bw_code_from_idx(slot, idx);
            bw_apply();
            sync_step();
            mark_change();
            break;
        case RM_STEP:
            s_step_idx[slot] = idx - 1;
            sync_step();
            apply_step_snap();
            radio_tune();
            mark_change();
            break;
        case RM_ATS:
            if (idx == 0) {
                page_radio();
                ats_start();
            } else {
                open_ats_list();
            }
            break;
        case RM_RDS:
            s_rds_on = (idx == 0);
            s_ui.rds_on = s_rds_on;
            if (s_radio_ok && s_ui.mode == RADIO_MODE_FM) {
                si4732_rds_enable(&s_radio, s_rds_on);
            }
            rds_parser_reset(&s_rds);
            mark_change();
            break;
        case RM_STEREO:
            s_stereo_force = (idx == 1);
            if (s_radio_ok && s_ui.mode == RADIO_MODE_FM) {
                si4732_set_stereo(&s_radio, s_stereo_force);
            }
            mark_change();
            break;
        case RM_DEEMPH:
            s_deemph_75 = (idx == 1);
            if (s_radio_ok && s_ui.mode == RADIO_MODE_FM) {
                si4732_set_deemphasis(&s_radio, s_deemph_75);
            }
            mark_change();
            break;
        case RM_AGC:
            s_agc_on = (idx == 0);
            dsp_apply();
            mark_change();
            break;
        case RM_AVC:
            s_avc = (uint8_t)((idx < 0) ? 0 : (idx > 90 ? 90 : idx));
            dsp_apply();
            mark_change();
            break;
        case RM_SOFTMUTE:
            s_softmute = (uint8_t)((idx < 0) ? 0 : (idx > 63 ? 63 : idx));
            dsp_apply();
            mark_change();
            break;
        case RM_SQUELCH: {
            int sq = mode_dsp_slot(s_ui.mode);
            if (idx <= 0) {
                s_squelch_on = false;
            } else {
                s_squelch_on = true;
                if (idx > 100) { idx = 100; }
                s_squelch_thr[sq] = (uint8_t)idx;
            }
            s_squelch_muted = false;
            if (s_radio_ok) {
                si4732_set_mute(&s_radio, false);
            }
            mark_change();
            break;
        }
        case RM_BFO:
            if (band_is_ssb(s_ui.mode) && si4732_ssb_patch_size > 0) {
                s_bfo = idx;
                if (s_radio_ok) {
                    si4732_set_bfo(&s_radio, radio_bfo_value());
                }
                mark_change();
            }
            break;
        }
    } else {
        switch (item) {
        case ST_BRIGHT_MENU:
            if (idx == 1) {
                s_auto_bright = !s_auto_bright;
                brightness_apply();
                mark_change();
            }
            break;
        case ST_BRIGHT_LEVEL:
            s_brightness = (uint8_t)((idx < 1) ? 1 : (idx > 100 ? 100 : idx));
            brightness_apply();
            mark_change();
            break;
        case ST_SLEEP:
            s_sleep_min = (uint16_t)((idx < 0) ? 0 : (idx > 100 ? 100 : idx));
            mark_change();
            break;
        case ST_POWERSAVE:
            power_save_set(idx == 0);
            mark_change();
            break;
        case ST_UTC:
            if (idx < -12) { idx = -12; }
            if (idx > 14) { idx = 14; }
            s_utc_min = (int16_t)(idx * 60);
            mark_change();
            break;
        case ST_SCREEN:
            if (idx < 0) { idx = 0; }
            if (idx > 120) { idx = 120; }
            s_screen_sec = (uint16_t)idx;
            s_screen_off = false;
            brightness_apply();
            mark_change();
            break;
        case ST_UNIT:
            s_unit_mode = (idx == 1) ? 1 : 0;
            s_ui.unit_mode = s_unit_mode;
            mark_change();
            ui_refresh_top();
            break;
        case ST_THEME:
            s_theme = (uint8_t)(idx > 5 ? 0 : idx);
            ui_theme_set(s_theme);
            mark_change();
            break;
        case ST_CPU:
            if (idx < 0) { idx = 0; }
            if (idx > 2) { idx = 2; }
            if (s_power_save) {
                s_cpu_saved = 80 + idx * 80;
                s_cpu_mhz = 80;
            } else {
                s_cpu_mhz = 80 + idx * 80;
            }
            if (cpu_apply(s_cpu_mhz) != ESP_OK) {
                toast("CPU ERR");
            }
            mark_change();
            break;
        case ST_WIFI:
            if (idx == 0) {
                wifi_off();
                toast("WIFI OFF");
            } else if (idx == 1) {
                toast(wifi_start_ap() == ESP_OK ? "AP MINI-radio"
                                                : "WIFI ERR");
            } else if (idx == 2) {
                if (s_wifi_ssid[0]) {
                    toast(wifi_sta_connect() == ESP_OK ? "CONNECTING"
                                                       : "WIFI ERR");
                } else {
                    wifi_scan_start();
                }
            } else {
                wifi_scan_start();
            }
            mark_change();
            break;
        case ST_BATT:
            if (idx < 0) { idx = 0; }
            if (idx > 4) { idx = 4; }
            s_batt_view = (uint8_t)idx;
            s_ui.batt_view = s_batt_view;
            mark_change();
            ui_refresh_top();
            break;
        case ST_BATT_CAL:
            if (idx < -1000) { idx = -1000; }
            if (idx > 1000) { idx = 1000; }
            s_batt_cal_mv = (int16_t)idx;
            battery_set_calibration(s_batt_cal_mv);
            storage_save_batt_cal(s_batt_cal_mv);
            {
                battery_state_t b;
                if (battery_update(&b) == ESP_OK) {
                    s_batt = b;
                    s_ui.battery = b;
                    s_ui.have_battery = true;
                }
            }
            ui_refresh_top();
            break;
        }
    }
}

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

static int app_find(const char *id)
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

static void launch_app(int idx)
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

static void app_open_radio(void)
{
    page_radio();
}

static void app_open_clock(void)
{
    s_hmi = HMI_CLOCK;
    s_nav_ms = now_ms();
    ui_draw_clock(&s_ui);
}

static void app_open_weather(void)
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

static void brightness_apply(void)
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

/* ------------------------------------------------------------- FAVORITES */

void fav_side_draw(void);
void ats_side_draw(void);

void tune_entry(const preset_entry_t *p)
{
    if (p->freq_hz == 0) {
        toast("EMPTY");
        return;
    }
    s_ui.band_idx = (p->band < band_count) ? p->band : 0;
    s_ui.mode = (p->mode <= RADIO_MODE_USB) ? (radio_mode_t)p->mode
                                            : band_table[s_ui.band_idx].default_mode;
    s_ui.freq_hz = band_clamp_freq(s_ui.band_idx, p->freq_hz);
    sync_step();
    apply_step_snap();
    radio_apply();
    mark_change();
    s_freq_dirty = true;
    s_nav_ms = now_ms();
    ui_draw_radio_panel(&s_ui);
    if (s_hmi == HMI_ATS_LIST) {
        ats_side_draw();
    } else if (s_hmi == HMI_FAV) {
        fav_side_draw();
    }
}

void fav_build(void)
{
    /* 인덱스 0 = Back 항목 */
    snprintf(s_fav_lbl[0], sizeof(s_fav_lbl[0]), "< BACK");
    s_fav_val[0][0] = 0;
    s_fav_lblp[0] = s_fav_lbl[0];
    s_fav_valp[0] = s_fav_val[0];
    for (int i = 0; i < STORAGE_PRESET_SLOTS; ++i) {
        const preset_entry_t *p = &s_presets.slot[i];
        snprintf(s_fav_lbl[i + 1], sizeof(s_fav_lbl[i + 1]), "P%02d", i + 1);
        if (p->freq_hz) {
            char f[16];
            fmt_freq_short(p->freq_hz, p->mode, f, sizeof(f));
            snprintf(s_fav_val[i + 1], sizeof(s_fav_val[i + 1]), "%s %.8s", f,
                     mode_str((radio_mode_t)p->mode));
        } else {
            snprintf(s_fav_val[i + 1], sizeof(s_fav_val[i + 1]), "EMPTY");
        }
        s_fav_lblp[i + 1] = s_fav_lbl[i + 1];
        s_fav_valp[i + 1] = s_fav_val[i + 1];
    }
    s_fav_ui.labels = s_fav_lblp;
    s_fav_ui.values = s_fav_valp;
    s_fav_ui.count = STORAGE_PRESET_SLOTS + 1;
    s_fav_ui.cursor = s_fav_cursor;
    s_fav_ui.top = s_fav_top;
}

void fav_side_draw(void)
{
    fav_build();
    ui_draw_side_list_title("FAVORITES", &s_fav_ui, &s_ui);
}

void fav_draw(void)
{
    fav_build();
    ui_draw_radio_panel(&s_ui);
    ui_draw_side_list_title("FAVORITES", &s_fav_ui, &s_ui);
}

void open_favorites(void)
{
    s_hmi = HMI_FAV;
    s_nav_ms = now_ms();
    s_ui.list_mode = true;
    fav_draw();
}

void preview_entry(const preset_entry_t *p)
{
    if (p->freq_hz == 0) {
        return;
    }
    s_ui.band_idx = (p->band < band_count) ? p->band : 0;
    s_ui.mode = (p->mode <= RADIO_MODE_USB) ? (radio_mode_t)p->mode
                                            : band_table[s_ui.band_idx].default_mode;
    s_ui.freq_hz = band_clamp_freq(s_ui.band_idx, p->freq_hz);
    sync_step();
    apply_step_snap();
    if (radio_ensure_mode() == ESP_OK) {
        radio_tune();
    }
    s_freq_dirty = false;
    mark_change();
    ui_draw_radio_panel(&s_ui);
    ui_draw_header(&s_ui);
}

void fav_move(int d)
{
    s_fav_cursor = wrap(s_fav_cursor + d, STORAGE_PRESET_SLOTS + 1);
    if (s_fav_cursor < s_fav_top) {
        s_fav_top = s_fav_cursor;
    } else if (s_fav_cursor >= s_fav_top + UI_SIDE_ROWS) {
        s_fav_top = s_fav_cursor - UI_SIDE_ROWS + 1;
    }
    if (s_fav_cursor > 0) {
        preview_entry(&s_presets.slot[s_fav_cursor - 1]);
    }
    fav_side_draw();
}

void fav_apply(void)
{
    if (s_fav_cursor == 0) {
        page_radio();
        return;
    }
    preset_entry_t *p = &s_presets.slot[s_fav_cursor - 1];
    if (p->freq_hz == 0) {
        tune_entry(p);
        return;
    }
    tune_entry(p);
    page_radio();
}

void fav_long(void)
{
    if (s_fav_cursor == 0) {
        page_radio();
        return;
    }
    preset_entry_t *p = &s_presets.slot[s_fav_cursor - 1];
    if (p->freq_hz == 0) {
        p->freq_hz = s_ui.freq_hz;
        p->band = (uint8_t)s_ui.band_idx;
        p->mode = (uint8_t)s_ui.mode;
        if (s_presets.count < s_fav_cursor) {
            s_presets.count = s_fav_cursor;
        }
        toast("SAVED");
    } else {
        memset(p, 0, sizeof(*p));
        while (s_presets.count > 0 &&
               s_presets.slot[s_presets.count - 1].freq_hz == 0) {
            s_presets.count--;
        }
        toast("DELETED");
    }
    storage_save_presets(&s_presets);
    fav_side_draw();
    toast_redraw();
}

/* ------------------------------------------------------------- ATS 목록 */

void ats_build(void)
{
    /* 인덱스 0 = Back 항목 */
    snprintf(s_ats_lbl[0], sizeof(s_ats_lbl[0]), "< BACK");
    s_ats_val[0][0] = 0;
    s_ats_lblp[0] = s_ats_lbl[0];
    s_ats_valp[0] = s_ats_val[0];
    for (int i = 0; i < STORAGE_ATS_SLOTS; ++i) {
        const preset_entry_t *p = &s_ats_list.slot[i];
        snprintf(s_ats_lbl[i + 1], sizeof(s_ats_lbl[i + 1]), "A%02d", i + 1);
        if (p->freq_hz) {
            char f[16];
            fmt_freq_short(p->freq_hz, p->mode, f, sizeof(f));
            snprintf(s_ats_val[i + 1], sizeof(s_ats_val[i + 1]), "%s %.8s", f,
                     mode_str((radio_mode_t)p->mode));
        } else {
            snprintf(s_ats_val[i + 1], sizeof(s_ats_val[i + 1]), "EMPTY");
        }
        s_ats_lblp[i + 1] = s_ats_lbl[i + 1];
        s_ats_valp[i + 1] = s_ats_val[i + 1];
    }
    s_ats_ui.labels = s_ats_lblp;
    s_ats_ui.values = s_ats_valp;
    s_ats_ui.count = STORAGE_ATS_SLOTS + 1;
    s_ats_ui.cursor = s_ats_cur;
    s_ats_ui.top = s_ats_top;
}

void ats_side_draw(void)
{
    ats_build();
    ui_draw_side_list_title("ATS LIST", &s_ats_ui, &s_ui);
}

void open_ats_list(void)
{
    s_hmi = HMI_ATS_LIST;
    s_nav_ms = now_ms();
    s_ui.list_mode = true;
    ats_build();
    ui_draw_radio_panel(&s_ui);
    ui_draw_side_list_title("ATS LIST", &s_ats_ui, &s_ui);
}

void ats_list_move(int d)
{
    s_ats_cur = wrap(s_ats_cur + d, STORAGE_ATS_SLOTS + 1);
    if (s_ats_cur < s_ats_top) {
        s_ats_top = s_ats_cur;
    } else if (s_ats_cur >= s_ats_top + UI_SIDE_ROWS) {
        s_ats_top = s_ats_cur - UI_SIDE_ROWS + 1;
    }
    if (s_ats_cur > 0) {
        preview_entry(&s_ats_list.slot[s_ats_cur - 1]);
    }
    ats_side_draw();
}

void ats_list_apply(void)
{
    if (s_ats_cur == 0) {
        page_radio();
        return;
    }
    preset_entry_t *p = &s_ats_list.slot[s_ats_cur - 1];
    if (p->freq_hz == 0) {
        tune_entry(p);
        return;
    }
    tune_entry(p);
    page_radio();
}

void ats_list_long(void)
{
    if (s_ats_cur == 0) {
        page_radio();
        return;
    }
    preset_entry_t *p = &s_ats_list.slot[s_ats_cur - 1];
    if (p->freq_hz == 0) {
        toast("EMPTY");
        return;
    }
    memset(p, 0, sizeof(*p));
    while (s_ats_list.count > 0 &&
           s_ats_list.slot[s_ats_list.count - 1].freq_hz == 0) {
        s_ats_list.count--;
    }
    storage_save_ats(&s_ats_list);
    toast("DELETED");
    ats_side_draw();
    toast_redraw();
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

enum { KP_MODE_WIFI = 0, KP_MODE_WEATHER, KP_MODE_SPLASH_CALL,
       KP_MODE_SPLASH_WEL };
static int s_kp_mode;

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

bool menu_item_is_action(int item)
{
    return (item == RM_BACK || item == RM_SEEK || item == RM_SCAN ||
            item == RM_FAV || item == RM_SETTINGS || item == RM_VOLUME);
}

void menu_action(int item)
{
    switch (item) {
    case RM_BACK:
        page_radio();
        break;
    case RM_VOLUME:
        open_volume(true);
        break;
    case RM_SETTINGS:
        page_set();
        break;
    case RM_FAV:
        open_favorites();
        break;
    case RM_SCAN:
        spectrum_scan_start();
        break;
    case RM_SEEK:
        page_radio();
        s_hmi = HMI_SEEK;
        s_ui.seek_mode = true;
        s_nav_ms = now_ms();
        if (s_ui.ui_mode == UI_MODE_EASY) {
            ui_draw_header(&s_ui);
        }
        toast("SEEK: turn");
        break;
    case RM_ATS:
        page_radio();
        ats_start();
        break;
    default:
        break;
    }
}

static int quick_pop(uint16_t m)
{
    int c = 0;
    while (m) {
        m &= (uint16_t)(m - 1);
        ++c;
    }
    return c;
}

static int quick_build(uint8_t *out)
{
    int n = 0;
    for (int i = 0; i < QA_COUNT && n < QUICK_MAX; ++i) {
        if (s_quick_mask & (1u << i)) {
            out[n++] = (uint8_t)i;
        }
    }
    return n;
}

static void quick_exec(int qa)
{
    switch (qa) {
    case QA_VOLUME:
        open_volume(false);
        break;
    case QA_ATS_LIST:
        open_ats_list();
        break;
    case QA_FAVORITES:
        open_favorites();
        break;
    case QA_SCREEN_OFF:
        display_set_backlight(false);
        s_screen_off = true;
        s_hmi = HMI_TUNE;
        ui_draw_all(&s_ui);
        break;
    case QA_SEEK:
        menu_action(RM_SEEK);
        break;
    case QA_SCAN:
        spectrum_scan_start();
        break;
    case QA_ATS_RUN:
        page_radio();
        ats_start();
        break;
    case QA_RDS:
        s_rds_on = !s_rds_on;
        s_ui.rds_on = s_rds_on;
        if (s_radio_ok && s_ui.mode == RADIO_MODE_FM) {
            si4732_rds_enable(&s_radio, s_rds_on);
        }
        rds_parser_reset(&s_rds);
        mark_change();
        page_radio();
        break;
    case QA_STEREO:
        s_stereo_force = !s_stereo_force;
        if (s_radio_ok) {
            si4732_set_stereo(&s_radio, s_stereo_force);
        }
        mark_change();
        page_radio();
        break;
    case QA_BAND:
        set_band((s_ui.band_idx + 1) % (int)band_count);
        page_radio();
        break;
    case QA_STEP:
        s_opt_quick = true;
        page_radio();
        options_open(HMI_MENU, RM_STEP);
        break;
    case QA_BW:
        s_opt_quick = true;
        page_radio();
        options_open(HMI_MENU, RM_BW);
        break;
    case QA_SETTINGS:
        page_set();
        break;
    default:
        break;
    }
}

void options_close(bool apply)
{
    if (apply) {
        opt_apply(s_opt_page, s_opt_item,
                  s_opt.slider ? s_opt.value : s_opt.cursor);
    }
    if (apply && s_opt_page == HMI_SET && s_opt_item == ST_BRIGHT_MENU &&
        s_opt.cursor == 0) {
        options_open(HMI_SET, ST_BRIGHT_LEVEL);
        return;
    }
    if (s_opt_page == HMI_MENU && s_opt_item == RM_ATS && apply) {
        /* ATS 선택 시 실제 시작/목록으로 화면 전환 */
        return;
    }
    if (s_opt_quick) {
        s_opt_quick = false;
        page_radio();
        return;
    }
    s_hmi = (s_opt_page == HMI_MENU) ? HMI_MENU : HMI_SET;
    s_nav_ms = now_ms();
    page_redraw();
}

/* 옵션 값 이동 */
void options_move(int d)
{
    if (s_opt.slider) {
        int v = s_opt.value + d * s_opt.step;
        if (v < s_opt.min) { v = s_opt.min; }
        if (v > s_opt.max) { v = s_opt.max; }
        s_opt.value = v;
        s_opt.sel = v;
        opt_apply(s_opt_page, s_opt_item, v);
        ui_draw_options(&s_opt);
        return;
    }

    int old_top = s_opt.top;
    s_opt.cursor = wrap(s_opt.cursor + d, s_opt.count);
    if (s_opt.cursor < s_opt.top) {
        s_opt.top = s_opt.cursor;
    } else if (s_opt.cursor >= s_opt.top + UI_OPT_ROWS) {
        s_opt.top = s_opt.cursor - UI_OPT_ROWS + 1;
    }
    (void)old_top;
    if (s_opt.live) {
        s_opt.sel = s_opt.cursor;
        opt_apply(s_opt_page, s_opt_item, s_opt.cursor);
    }
    ui_draw_options(&s_opt);
}

/* --------------------------------------------------------- 입력 처리 */

void tune_commit(uint32_t now)
{
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
    case HMI_SEEK:
        if (!s_radio_ok) {
            break;
        }
        if (band_is_ssb(s_ui.mode)) {
            toast("SEEK: FM/AM ONLY");
            break;
        }
        if (s_seeking) {
            if ((d > 0) != (s_sw_dir > 0)) {
                seek_reverse(d > 0);
            }
            break;
        }
        seek_start(d > 0);
        break;
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
            options_open(HMI_MENU, s_rmenu.cursor);
        }
        break;
    case HMI_SET:
        s_nav_ms = now;
        if (long_press) {
            page_menu();
        } else if (s_settings.cursor == ST_BACK) {
            page_radio();
        } else if (s_settings.cursor == ST_TIME) {
            open_time_set();
        } else if (s_settings.cursor == ST_WIFI) {
            open_wifi_app();
        } else if (s_settings.cursor == ST_EIBI) {
            open_eibi_app();
        } else if (s_settings.cursor == ST_QUICKSET) {
            open_quick_cfg();
        } else if (s_settings.cursor == ST_ABOUT) {
            open_about();
        } else if (s_settings.cursor == ST_BRIGHT) {
            options_open(HMI_SET, ST_BRIGHT_MENU);
        } else {
            options_open(HMI_SET, s_settings.cursor);
        }
        break;
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
    s_ext.reserved2[0] = s_follow_browser ? 1 : 0;
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
    if (e_ext != ESP_OK || e_cfg != ESP_OK || e_pre != ESP_OK || e_ats != ESP_OK) {
        ESP_LOGW(TAG, "save failed ext=%s cfg=%s pre=%s ats=%s",
                 esp_err_to_name(e_ext), esp_err_to_name(e_cfg),
                 esp_err_to_name(e_pre), esp_err_to_name(e_ats));
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

void wifi_app_draw(void);
void scan_list_draw(void);

void ui_refresh_top(void)
{
    switch (s_hmi) {
    case HMI_TUNE:
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
        scan_list_draw();
        break;
    case HMI_KEYPAD:
        ui_draw_keypad(&s_kp);
        break;
    default:
        break;
    }
}

static int64_t clock_utc_now(void)
{
    return s_clock_base + (int64_t)((now_ms() - s_clock_ms0) / 1000);
}

static void clock_save_now(void)
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
    if ((int32_t)(now - s_sleep_deadline) < 0) {
        return;
    }
    ESP_LOGI(TAG, "sleep timer expired, entering sleep");
    board_amp(false);
    display_set_backlight(false);
    if (s_radio_ok) {
        si4732_power_down(&s_radio);
    }
    gpio_set_level((gpio_num_t)BOARD_RADIO_EN_GPIO, 0);
    uint64_t mask = (1ULL << BOARD_ENC_PUSH_GPIO) |
                    (1ULL << BOARD_ENC_A_GPIO) |
                    (1ULL << BOARD_ENC_B_GPIO);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
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
    if (s_theme > 5) {
        s_theme = 0;
    }
    s_rds_on = (cfg->flags & 0x01) != 0;
    s_stereo_force = (cfg->flags & 0x02) != 0;
    s_power_save = (cfg->flags & 0x04) != 0;
    s_deemph_75 = cfg->deemphasis != 0;
    s_sleep_min = cfg->sleep_min;
    s_ui.volume = cfg->volume;
    s_weather_mode = (cfg->reserved[0] <= 1) ? cfg->reserved[0] : 0;
    s_ui_mode = 1;
    s_ui.ui_mode = 1;

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
    s_follow_browser = s_ext.reserved2[0] == 1;
    s_batt_view = (s_ext.batt_view <= 4) ? s_ext.batt_view : 3;
    s_screen_sec = (s_ext.screen_off <= 120) ? s_ext.screen_off : 0;
    s_unit_mode = (s_ext.unit_mode <= 1) ? s_ext.unit_mode : 0;
    s_ui.unit_mode = s_unit_mode;
    s_ui.batt_view = s_batt_view;
}

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
        if (s_presets.count < STORAGE_PRESET_SLOTS) {
            int i = s_presets.count++;
            s_presets.slot[i].freq_hz = s_ui.freq_hz;
            s_presets.slot[i].band = (uint8_t)s_ui.band_idx;
            s_presets.slot[i].mode = (uint8_t)s_ui.mode;
            storage_save_presets(&s_presets);
            mark_change();
            toast("PRESET SAVED");
        } else {
            toast("PRESET FULL");
        }
        break;

    case WEB_CMD_PRESET_DEL:
        if (c->a >= 0 && c->a < s_presets.count) {
            for (int i = c->a; i < s_presets.count - 1; ++i) {
                s_presets.slot[i] = s_presets.slot[i + 1];
            }
            s_presets.count--;
            memset(&s_presets.slot[s_presets.count], 0,
                   sizeof(s_presets.slot[s_presets.count]));
            storage_save_presets(&s_presets);
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

static bool s_setup_btn_raw;
static bool s_setup_btn_stable;
static bool s_setup_btn_long;
static uint32_t s_setup_btn_raw_ms;
static uint32_t s_setup_btn_press_ms;

static bool setup_button_event(bool *long_press)
{
    uint32_t now = now_ms();
    bool pressed = (gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0);
    if (pressed != s_setup_btn_raw) {
        s_setup_btn_raw = pressed;
        s_setup_btn_raw_ms = now;
    }
    if (s_setup_btn_raw != s_setup_btn_stable &&
        (now - s_setup_btn_raw_ms) >= 30) {
        s_setup_btn_stable = s_setup_btn_raw;
        if (s_setup_btn_stable) {
            s_setup_btn_press_ms = now;
            s_setup_btn_long = false;
        } else {
            bool lp = s_setup_btn_long || (now - s_setup_btn_press_ms) >= 800;
            if (long_press) {
                *long_press = lp;
            }
            return true;
        }
    }
    if (s_setup_btn_stable && pressed && !s_setup_btn_long &&
        (now - s_setup_btn_press_ms) >= 800) {
        s_setup_btn_long = true;
    }
    return false;
}

static void setup_service(void)
{
    web_poll();
    if (s_scan_running && s_scan_done) {
        s_scan_running = false;
        build_scan_arrays();
    }
    if (s_setup_reboot_ms &&
        (int32_t)(now_ms() - s_setup_reboot_ms) >= 0) {
        esp_restart();
    }
}

static void setup_adjust_top(int *top, int cursor, int count, int rows)
{
    if (cursor < *top) {
        *top = cursor;
    }
    if (cursor >= *top + rows) {
        *top = cursor - rows + 1;
    }
    if (*top > count - rows) {
        *top = count - rows;
    }
    if (*top < 0) {
        *top = 0;
    }
}

static void setup_progress(int step, int total)
{
    char b[24];
    snprintf(b, sizeof(b), "%d LEFT", total - step);
    display_draw_text(6, 4, b, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    snprintf(b, sizeof(b), "%d/%d", step, total);
    display_draw_text(UI_W - 6, 4, b, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TR);
}

static void setup_progress_bottom(int step, int total)
{
    char b[24];
    snprintf(b, sizeof(b), "%d/%d", step, total);
    display_draw_text(6, UI_H - 10, b, DISPLAY_FONT_TINY, UI_ACC_HI, UI_BG,
                      DISPLAY_DATUM_TL);
    snprintf(b, sizeof(b), "%d LEFT", total - step);
    display_draw_text(UI_W - 6, UI_H - 10, b, DISPLAY_FONT_TINY, UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TR);
}

static void setup_draw_options(const char *title, const char *const *labels,
                               int count, int cursor, int top,
                               const char *hint, int step, int total)
{
    display_fill_screen(UI_BG);
    ui_options_t o;
    memset(&o, 0, sizeof(o));
    o.title = title;
    o.labels = labels;
    o.count = count;
    o.cursor = cursor;
    o.top = top;
    o.sel = cursor;
    ui_draw_options(&o);
    setup_progress(step, total);
    if (hint) {
        display_draw_text(UI_W / 2, UI_H - 10, hint, DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    }
}

static bool setup_wait_options(const char *title, const char *const *labels,
                               int count, int *value, const char *hint,
                               bool *cancelled, int step, int total)
{
    int cursor = *value;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor >= count) {
        cursor = count - 1;
    }
    int top = 0;
    setup_adjust_top(&top, cursor, count, UI_OPT_ROWS);
    int drawn = -1;
    int drawn_top = -1;
    while (1) {
        setup_service();
        int d = encoder_get_delta();
        if (d) {
            cursor = wrap(cursor + d, count);
            setup_adjust_top(&top, cursor, count, UI_OPT_ROWS);
        }
        if (cursor != drawn || top != drawn_top) {
            setup_draw_options(title, labels, count, cursor, top, hint,
                               step, total);
            display_flush();
            drawn = cursor;
            drawn_top = top;
        }
        bool lp = false;
        if (setup_button_event(&lp)) {
            if (lp) {
                if (cancelled) {
                    *cancelled = true;
                    return false;
                }
                continue;
            }
            *value = cursor;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool setup_keypad(const char *ssid, char *out, size_t n)
{
    ui_keypad_t kp;
    memset(&kp, 0, sizeof(kp));
    kp.title = "WI-FI PASSWORD";
    kp.subtitle = ssid;
    ui_draw_keypad(&kp);
    display_flush();
    while (1) {
        setup_service();
        int d = encoder_get_delta();
        if (d) {
            kp.cursor = wrap(kp.cursor + d, UI_KEY_COUNT);
            ui_draw_keypad(&kp);
            display_flush();
        }
        bool lp = false;
        if (setup_button_event(&lp)) {
            if (lp) {
                return false;
            }
            int idx = kp.cursor;
            if (idx < 40) {
                char c = ui_keypad_char(idx, kp.shift, kp.sym);
                if (c && kp.len < UI_KEY_TEXT_MAX - 1) {
                    kp.text[kp.len++] = c;
                    kp.text[kp.len] = '\0';
                }
            } else if (idx == UI_KEY_DEL) {
                if (kp.len > 0) {
                    kp.text[--kp.len] = '\0';
                }
            } else if (idx == UI_KEY_SHIFT) {
                kp.shift = !kp.shift;
            } else if (idx == UI_KEY_SYM) {
                kp.sym = !kp.sym;
            } else if (idx == UI_KEY_SPACE) {
                if (kp.len < UI_KEY_TEXT_MAX - 1) {
                    kp.text[kp.len++] = ' ';
                    kp.text[kp.len] = '\0';
                }
            } else if (idx == UI_KEY_OK) {
                snprintf(out, n, "%s", kp.text);
                return true;
            }
            ui_draw_keypad(&kp);
            display_flush();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void setup_scan_wait(void)
{
    ui_draw_wifi_scanning(0);
    setup_progress_bottom(3, 5);
    display_flush();
    wifi_scan_start();
    uint32_t t0 = now_ms();
    int dots = -1;
    while (!s_scan_done && (now_ms() - t0) < 12000u) {
        setup_service();
        int nd = (int)((now_ms() - t0) / 250u);
        if (nd != dots) {
            dots = nd;
            ui_draw_wifi_scanning(nd);
            setup_progress_bottom(3, 5);
            display_flush();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!s_scan_done) {
        s_scan_running = false;
    }
    build_scan_arrays();
}

static void setup_mode(void)
{
    if (wifi_start_apsta() != ESP_OK) {
        s_wifi_mode = 0;
    }

    int welcome_sel = 0;
    (void)encoder_get_delta();
    ui_draw_setup_welcome("GNBD miniradio", app_version(), 1, 5,
                          welcome_sel);
    display_flush();
    while (1) {
        setup_service();
        int wd = encoder_get_delta();
        if (wd) {
            welcome_sel = (welcome_sel + (wd > 0 ? 1 : -1) + 2) % 2;
            ui_draw_setup_welcome("GNBD miniradio", app_version(), 1, 5,
                                  welcome_sel);
            display_flush();
        }
        bool lp = false;
        if (setup_button_event(&lp)) {
            if (welcome_sel == 1) {
                s_wifi_mode = 0;
                s_setup_done = true;
                storage_save_setup_done(true);
                do_save();
                esp_restart();
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    static char tzbuf[27][12];
    static const char *tzlab[27];
    for (int i = 0; i < 27; ++i) {
        snprintf(tzbuf[i], sizeof(tzbuf[i]), "UTC%+d", i - 12);
        tzlab[i] = tzbuf[i];
    }
    int tz = s_utc_min / 60 + 12;
    if (tz < 0) tz = 0;
    if (tz > 26) tz = 26;
    setup_wait_options("TIME ZONE", tzlab, 27, &tz,
                       "turn: change  press: next  web: 192.168.4.1",
                       NULL, 2, 5);
    s_utc_min = (int16_t)((tz - 12) * 60);

    bool wifi_used = false;
    static const char *const wifi_choices[] = {"CONNECT", "SKIP"};
    int choice = 0;
    setup_wait_options("WI-FI", wifi_choices, 2, &choice,
                       "turn: change  press: next", NULL, 3, 5);
    if (choice == 0) {
        setup_scan_wait();
        const char *names[16];
        bool secure[16];
        int rssi[16];
        int count = 0;
        bool no_nets = false;
        for (int i = 0; i < 16 && i < (int)s_scan_n; ++i) {
            names[count] = s_scan_buf[i];
            secure[count] = s_scan_secure[i];
            rssi[count] = s_scan_rssi[i];
            ++count;
        }
        if (count == 0) {
            names[0] = "NO NETWORKS";
            secure[0] = false;
            rssi[0] = 0;
            count = 1;
            no_nets = true;
        }
        int cursor = 0;
        int top = 0;
        int drawn = -1;
        int drawn_top = -1;
        while (1) {
            setup_service();
            int d = encoder_get_delta();
            if (d) {
                cursor = wrap(cursor + d, count);
                setup_adjust_top(&top, cursor, count, 5);
            }
            if (cursor != drawn || top != drawn_top) {
                ui_draw_wifi_list("WI-FI",
                                  "turn: move  press: connect  hold: skip",
                                  names, secure, rssi, count, cursor, top);
                setup_progress_bottom(3, 5);
                display_flush();
                drawn = cursor;
                drawn_top = top;
            }
            bool lp = false;
            if (!setup_button_event(&lp)) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (lp || no_nets) {
                wifi_used = false;
                break;
            }
            char ssid[33];
            snprintf(ssid, sizeof(ssid), "%s", names[cursor]);
            char pass[65] = "";
            if (secure[cursor]) {
                if (!setup_keypad(ssid, pass, sizeof(pass))) {
                    drawn = -1;
                    continue;
                }
            }
            snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", ssid);
            snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", pass);
            if (wifi_setup_connect(s_wifi_ssid, s_wifi_pass) == ESP_OK) {
                wifi_used = true;
                display_fill_screen(UI_BG);
                display_draw_text(UI_W / 2, 60, "CONNECTING...",
                                  DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                                  DISPLAY_DATUM_TC);
                display_draw_text(UI_W / 2, 84, ssid, DISPLAY_FONT_TINY,
                                  UI_ACC_HI, UI_BG, DISPLAY_DATUM_TC);
                setup_progress_bottom(3, 5);
                display_flush();
                uint32_t t0 = now_ms();
                while (!s_sta_connected && (now_ms() - t0) < 7000u) {
                    setup_service();
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            break;
        }
    }

    static const char *const unit_labels[] = {"MHz", "kHz"};
    int unit = s_unit_mode ? 1 : 0;
    setup_wait_options("FREQUENCY UNIT", unit_labels, 2, &unit,
                       "turn: change  press: next", NULL, 4, 5);
    s_unit_mode = unit ? 1 : 0;
    s_ui.unit_mode = s_unit_mode;

    static const char *const batt_labels[] = {
        "ICON", "VOLT", "PERCENT", "% + ICON", "V + ICON"
    };
    int batt = s_batt_view;
    setup_wait_options("BATTERY DISPLAY", batt_labels, 5, &batt,
                       "turn: change  press: next", NULL, 5, 5);
    if (batt < 0) batt = 0;
    if (batt > 4) batt = 4;
    s_batt_view = (uint8_t)batt;
    s_ui.batt_view = s_batt_view;

    s_wifi_mode = wifi_used ? 2 : 0;
    s_setup_done = true;
    storage_save_setup_done(true);
    do_save();
    esp_restart();
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_power_init());
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
    if (s_power_save) {
        cpu_apply(80);
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
    if (storage_load_ats(&s_ats_list) != ESP_OK) {
        memset(&s_ats_list, 0, sizeof(s_ats_list));
    }
    eibi_fs_mount();
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
        brightness_apply();
        splash_show();
        splash_service_input();
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
        seek_poll(now);
        ats_poll(now);
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
                    if (s_ui.ui_mode != UI_MODE_EASY) {
                        ui_draw_all(&s_ui);
                    } else {
                        ui_draw_rds(&s_ui);
                    }
                } else if (s_hmi == HMI_MENU || s_hmi == HMI_SET ||
                           s_hmi == HMI_FAV || s_hmi == HMI_ATS_LIST) {
                    ui_draw_radio_panel(&s_ui);
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
        if (s_toast[0] && (now - s_toast_ms) > TOAST_TIME) {
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

        if (s_ui.ui_mode != UI_MODE_EASY &&
            (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK)) {
            vTaskDelay(TICK_MS(10));
        } else {
            vTaskDelay(TICK_MS(20));
        }
    }
}
