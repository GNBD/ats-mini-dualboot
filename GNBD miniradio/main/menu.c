/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Main menu / settings menu build & option apply.
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
#include "ui_exlu_app.h"
#include "decode.h"
#include "weather.h"
#include "hmi.h"
static bool s_opt_quick;
static bool s_opt_force_radio;
#define QUICK_ITEMS 4

#define RMENU_MAX (RM_COUNT + 8)
static int s_menu_ids[RMENU_MAX];
static int s_set_ids[ST_COUNT];

void rmenu_build(void)
{
    /* 표시 순서/이름은 menu_data.c 의 테이블을 따른다 */
    static const char *labels[RMENU_MAX];
    static char app_labels[8][24];
    int n = 0;
    /* 1) BACK 은 항상 최상단 고정 */
    labels[n] = "< BACK";
    s_menu_ids[n] = RM_BACK;
    ++n;
    /* 2) EXLU 앱: 부팅에서 선택한 앱만 BACK 바로 아래에 노출.
          (None/미선택이면 메뉴에 아무것도 추가하지 않는다) */
    exlu_manifest_t apps[1];
    int na = 0;
#if EXLU_ENABLE
    {
        const char *boot = exlu_get_boot_id();
        if (boot && boot[0]) {
            exlu_t be;
            if (exlu_load(boot, &be) == EXLU_OK) {
                if (be.manifest.kind == EXLU_KIND_APP) {
                    apps[0] = be.manifest;
                    na = 1;
                }
                exlu_free(&be);
            }
        }
    }
#endif
    for (int i = 0; i < na && n < RMENU_MAX; ++i) {
        /* 기기 폰트는 ASCII 만 표시 가능: 비ASCII/제어문자는 '_' 로 */
        size_t j = 0;
        for (const unsigned char *q = (const unsigned char *)apps[i].name;
             *q && j + 1 < sizeof(app_labels[0]); ++q) {
            if (*q >= 0x20 && *q < 0x7F) {
                app_labels[i][j++] = (char)*q;
            } else if (*q >= 0x80) {
                while (q[1] && (q[1] & 0xC0) == 0x80) {
                    ++q;
                }
                app_labels[i][j++] = '_';
            }
        }
        app_labels[i][j] = '\0';
        if (j == 0) {
            snprintf(app_labels[i], sizeof(app_labels[i]), "%s", apps[i].id);
        }
        labels[n] = app_labels[i];
        s_menu_ids[n] = EXLU_MENU_BASE + i;
        ++n;
    }
    /* 3) 일반 메뉴 (BACK 중복 제외) */
    for (int i = 0; i < g_menu_count && n < RMENU_MAX; ++i) {
        if (g_menu_items[i].id == RM_BACK) {
            continue;
        }
        if (!g_menu_items[i].visible) {
            continue;
        }
        /* ATS는 FM 전용 */
        if (g_menu_items[i].id == RM_ATS && s_ui.mode != RADIO_MODE_FM) {
            continue;
        }
        labels[n] = g_menu_items[i].label;
        s_menu_ids[n] = g_menu_items[i].id;
        ++n;
    }
    if (n < 1) {
        labels[0] = "< BACK";
        s_menu_ids[0] = RM_BACK;
        n = 1;
    }
    s_rmenu.labels = labels;
    s_rmenu.values = s_rv;
    s_rmenu.count = n;
    if (s_rmenu.cursor >= n) {
        s_rmenu.cursor = 0;
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
    if (s_rfgain == 0) {
        snprintf(s_rbuf[RM_RFGAIN], sizeof(s_rbuf[RM_RFGAIN]), "AUTO");
    } else {
        snprintf(s_rbuf[RM_RFGAIN], sizeof(s_rbuf[RM_RFGAIN]), "G%u",
                 (unsigned)(s_rfgain - 1));
    }
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

    /* 값 버퍼를 표시 순서로 매핑 */
    for (int i = 0; i < s_rmenu.count; ++i) {
        s_rv[i] = (s_menu_ids[i] >= EXLU_MENU_BASE)
                   ? "RUN" : s_rbuf[s_menu_ids[i]];
    }
}

void local_datetime(int *y, int *mo, int *d, int *h, int *mi);

void settings_build(void)
{
    static const char *labels[ST_COUNT];
    int n = 0;
    for (int i = 0; i < g_set_count && n < ST_COUNT; ++i) {
        if (!g_set_items[i].visible) {
            continue;
        }
        /* 배터리 캘리브레이션은 일반 사용자에게 숨김 */
        if (g_set_items[i].id == ST_BATT_CAL) {
            continue;
        }
        labels[n] = g_set_items[i].label;
        s_set_ids[n] = g_set_items[i].id;
        ++n;
    }
    if (n < 1) {
        labels[0] = "< BACK";
        s_set_ids[0] = ST_BACK;
        n = 1;
    }
    s_settings.labels = labels;
    s_settings.values = s_sv;
    s_settings.count = n;
    if (s_settings.cursor >= n) {
        s_settings.cursor = 0;
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
    snprintf(s_sbuf[ST_SIGDBG], sizeof(s_sbuf[ST_SIGDBG]), "%s",
             (const char *[]){"LOW", "HIGH"}[s_dbg_sig > 1 ? 1
                                                           : s_dbg_sig]);
    snprintf(s_sbuf[ST_SIGVIEW], sizeof(s_sbuf[ST_SIGVIEW]), "%s",
             (const char *[]){"RDS+SIG", "BAND MAP"}[s_sig_view > 1 ? 0
                                                                : s_sig_view]);
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
             (s_theme == UI_THEME_CUSTOM)
                 ? (ui_runtime_active_name()[0] ? ui_runtime_active_name()
                                                : "CUSTOM UI")
                 : (const char *[]){"OFF", "BLACK", "WHITE", "LIGHT", "BLUE",
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

    /* 값 버퍼를 표시 순서로 매핑 */
    for (int i = 0; i < s_settings.count; ++i) {
        s_sv[i] = s_sbuf[s_set_ids[i]];
    }
}

/* 설정 커서 → 동작 id (ST_*) */
int settings_id_at(int cursor)
{
    if (cursor < 0 || cursor >= s_settings.count) {
        return -1;
    }
    return s_set_ids[cursor];
}

void opt_label(int i, const char *text)
{
    if (i < 0 || i >= UI_OPT_MAX) {
        return;
    }
    snprintf(s_opt_buf[i], sizeof(s_opt_buf[i]), "%.*s",
             (int)sizeof(s_opt_buf[i]) - 1, text ? text : "");
    s_opt_ptr[i] = s_opt_buf[i];
}

static void fmt_mhz(uint32_t hz, char *out, size_t n)
{
    uint32_t whole = hz / 1000000u;
    uint32_t frac = (hz / 1000u) % 1000u;
    if (frac == 0) {
        snprintf(out, n, "%lu", (unsigned long)whole);
    } else if ((frac % 100u) == 0) {
        snprintf(out, n, "%lu.%lu", (unsigned long)whole,
                 (unsigned long)(frac / 100u));
    } else if ((frac % 10u) == 0) {
        snprintf(out, n, "%lu.%02lu", (unsigned long)whole,
                 (unsigned long)(frac / 10u));
    } else {
        snprintf(out, n, "%lu.%03lu", (unsigned long)whole,
                 (unsigned long)frac);
    }
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
        case RM_RFGAIN:  return s_rfgain;
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
        case ST_SIGDBG: return s_dbg_sig;
        case ST_SIGVIEW: return s_sig_view;
        case ST_UTC:    return s_utc_min / 60 + 12;
        case ST_WIFI:   return (s_wifi_mode <= 2) ? s_wifi_mode : 0;
        case ST_BATT:   return (s_batt_view <= 4) ? s_batt_view : 3;
        case ST_BATT_CAL: return s_batt_cal_mv;
        case ST_SCREEN: return s_screen_sec;
        case ST_POWERSAVE: return s_power_save ? 0 : 1;
        case ST_UNIT:   return s_unit_mode;
        case ST_THEME: {
            if (s_theme != UI_THEME_CUSTOM) {
                return (s_theme <= 5) ? s_theme : 0;
            }
            char names[32][UI_RUNTIME_NAME_MAX + 1];
            int n = ui_runtime_list(names, 32);
            for (int i = 0; i < n; ++i) {
                if (strcmp(names[i], ui_runtime_active_name()) == 0) {
                    return 6 + i;
                }
            }
            return -1;
        }
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

void options_open(int page, int item)
{
    int n = 0;
    int sel = opt_sel(page, item);
    char tmp[24];

    bool slider = false;
    bool live = true;
    int vmin = 0, vmax = 0, vstep = 1, vval = 0;
    int voff = 0;
    const char *unit = NULL;
    const char *zero = NULL;

    if (page == HMI_MENU) {
        switch (item) {
        case RM_BAND:
            for (size_t i = 0; i < band_count && n < UI_OPT_MAX; ++i) {
                const band_desc_t *b = &band_table[i];
                char lo[12];
                char hi[12];
                char lbl[48];
                fmt_mhz(b->min_hz, lo, sizeof(lo));
                fmt_mhz(b->max_hz, hi, sizeof(hi));
                snprintf(lbl, sizeof(lbl), "%.8s\t%s~%s", b->name, lo, hi);
                opt_label(n++, lbl);
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
        case RM_RFGAIN:
            /* 슬라이더: 왼쪽 끝 = AUTO(RF AGC), 그 오른쪽 0..26 = LNA 이득 인덱스 */
            slider = true;
            vmin = 0; vmax = 27; vstep = 1;
            vval = s_rfgain;
            voff = -1;          /* 내부값 1..27 → 표시 0..26 */
            zero = "AUTO";
            s_opt.title = "RF GAIN (FM)";
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
            {
                char names[32][UI_RUNTIME_NAME_MAX + 1];
                int un = ui_runtime_list(names, 32);
                for (int i = 0; i < un && n < UI_OPT_MAX; ++i) {
                    opt_label(n++, names[i]);
                }
            }
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
        case ST_SIGDBG:
            opt_label(n++, "LOW (250ms)");
            opt_label(n++, "HIGH (30ms)");
            s_opt.title = "SIGNAL SPEED";
            live = false;
            break;
        case ST_SIGVIEW:
            opt_label(n++, "RDS/EIBI + SIG");
            opt_label(n++, "BAND MAP");
            s_opt.title = "BOTTOM VIEW";
            live = false;
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
    s_opt.value_offset = voff;

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
            s_freq_dirty = true;
            mark_change();
            break;
        case RM_AGC:
            s_agc_on = (idx == 0);
            dsp_apply();
            s_freq_dirty = true;
            mark_change();
            break;
        case RM_RFGAIN:
            s_rfgain = (uint8_t)((idx < 0) ? 0 : (idx > 27 ? 27 : idx));
            rfgain_apply();
            mark_change();
            break;
        case RM_AVC:
            s_avc = (uint8_t)((idx < 0) ? 0 : (idx > 90 ? 90 : idx));
            dsp_apply();
            s_freq_dirty = true;
            mark_change();
            break;
        case RM_SOFTMUTE:
            s_softmute = (uint8_t)((idx < 0) ? 0 : (idx > 63 ? 63 : idx));
            dsp_apply();
            s_freq_dirty = true;
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
            s_freq_dirty = true;
            mark_change();
            break;
        }
        case RM_BFO:
            if (band_is_ssb(s_ui.mode) && si4732_ssb_patch_size > 0) {
                s_bfo = idx;
                if (s_radio_ok) {
                    si4732_set_bfo(&s_radio, radio_bfo_value());
                }
                s_freq_dirty = true;
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
            if (idx >= 6) {
                char names[32][UI_RUNTIME_NAME_MAX + 1];
                int un = ui_runtime_list(names, 32);
                int fi = idx - 6;
                if (fi >= 0 && fi < un && ui_runtime_activate(names[fi])) {
                    s_theme = UI_THEME_CUSTOM;
                    ui_runtime_apply_theme();
                    s_opt_force_radio = true;
                } else {
                    s_theme = 0;
                    ui_runtime_disable();
                    ui_theme_set(0);
                    s_opt_force_radio = true;
                }
            } else {
                s_theme = (uint8_t)(idx < 0 ? 0 : idx);
                ui_runtime_disable();
                ui_theme_set(s_theme);
                s_opt_force_radio = true;
            }
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
        case ST_SIGDBG:
            s_dbg_sig = (uint8_t)((idx <= 0) ? 0 : 1);
            toast(s_dbg_sig ? "SIG spd HIGH" : "SIG spd LOW");
            break;
        case ST_SIGVIEW:
            s_sig_view = (uint8_t)((idx == 1) ? 1 : 0);
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

/* 메뉴 커서 → 동작 id (RM_*) */
int menu_id_at(int cursor)
{
    if (cursor < 0 || cursor >= s_rmenu.count) {
        return -1;
    }
    return s_menu_ids[cursor];
}

bool menu_item_is_action(int item)
{
    int id = menu_id_at(item);
    if (id >= EXLU_MENU_BASE) {
        return true;
    }
    return (id == RM_BACK || id == RM_SEEK || id == RM_SCAN ||
            id == RM_FAV || id == RM_SETTINGS || id == RM_VOLUME ||
            (id >= RM_MOD_A && id <= RM_MOD_H));
}

void menu_action(int item)
{
    int id = menu_id_at(item);
    bool mod_hit = false;
    if (id >= EXLU_MENU_BASE) {
#if EXLU_ENABLE
        const char *boot = exlu_get_boot_id();
        if (boot && boot[0]) {
            const char *err = exlu_app_launch(boot);
            if (err) {
                toast(err);
            } else {
                page_radio();
            }
        }
#endif
        return;
    }
    if (id >= RM_MOD_A && id <= RM_MOD_H) {
        mod_hit = mod_runtime_ui_event(id);
    }
    switch (id) {
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
        toast("SEEK: turn");
        break;
    case RM_ATS:
        page_radio();
        ats_start();
        break;
    case RM_MOD_A:
    case RM_MOD_B:
    case RM_MOD_C:
    case RM_MOD_D:
    case RM_MOD_E:
    case RM_MOD_F:
    case RM_MOD_G:
    case RM_MOD_H:
        if (!mod_hit) {
            toast("NO MOD");
        }
        break;
    default:
        break;
    }
}

int quick_pop(uint16_t m)
{
    int c = 0;
    while (m) {
        m &= (uint16_t)(m - 1);
        ++c;
    }
    return c;
}

int quick_build(uint8_t *out)
{
    int n = 0;
    for (int i = 0; i < QA_COUNT && n < QUICK_MAX; ++i) {
        if (!(s_quick_mask & (1u << i))) {
            continue;
        }
        /* ATS는 FM 전용 */
        if ((i == QA_ATS_LIST || i == QA_ATS_RUN) &&
            s_ui.mode != RADIO_MODE_FM) {
            continue;
        }
        out[n++] = (uint8_t)i;
    }
    return n;
}

void quick_exec(int qa)
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
    if (s_opt_force_radio) {
        s_opt_force_radio = false;
        s_hmi = HMI_TUNE;
        s_nav_ms = now_ms();
        s_toast[0] = '\0';
        page_radio();
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
        /* 밝기/슬립/볼륨 등 값 조절에도 엔코더 가속 적용 */
        int32_t a = accel_counts(d);
        if (a == 0 && d != 0) {
            a = (d > 0) ? 1 : -1;
        }
        int v = s_opt.value + (int)(a * s_opt.step);
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
