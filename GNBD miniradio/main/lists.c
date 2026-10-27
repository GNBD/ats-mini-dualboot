/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Favorites and ATS preset lists.
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

/* 원본(main.c)에서는 mode_str 이 같은 번역 단위 static 함수라 길이를
 * 추론해 format-truncation 경고가 나지 않았다. 분할로 외부 함수가 되어
 * 추론이 불가능해져 경고만 추가된다(출력 동작은 동일). */
#pragma GCC diagnostic ignored "-Wformat-truncation"

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
        const char *nm = s_pnames.name[i];
        if (nm[0]) {
            snprintf(s_fav_lbl[i + 1], sizeof(s_fav_lbl[i + 1]), "%s", nm);
        } else {
            snprintf(s_fav_lbl[i + 1], sizeof(s_fav_lbl[i + 1]), "P%02d", i + 1);
        }
        if (p->freq_hz) {
            char f[16];
            fmt_freq_short(p->freq_hz, p->mode, f, sizeof(f));
            if (nm[0]) {
                snprintf(s_fav_val[i + 1], sizeof(s_fav_val[i + 1]),
                         "P%02d %s", i + 1, f);
            } else {
                snprintf(s_fav_val[i + 1], sizeof(s_fav_val[i + 1]), "%s %.8s",
                         f, mode_str((radio_mode_t)p->mode));
            }
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
        if (preset_index_for_freq(s_ui.freq_hz, s_ui.band_idx) >= 0) {
            toast("ALREADY SAVED");
            toast_redraw();
            return;
        }
        p->freq_hz = s_ui.freq_hz;
        p->band = (uint8_t)s_ui.band_idx;
        p->mode = (uint8_t)s_ui.mode;
        if (s_presets.count < s_fav_cursor) {
            s_presets.count = s_fav_cursor;
        }
        toast("SAVED");
    } else {
        memset(p, 0, sizeof(*p));
        s_pnames.name[s_fav_cursor - 1][0] = '\0';
        while (s_presets.count > 0 &&
               s_presets.slot[s_presets.count - 1].freq_hz == 0) {
            s_presets.count--;
        }
        toast("DELETED");
    }
    storage_save_presets(&s_presets);
    storage_save_names(&s_pnames);
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
