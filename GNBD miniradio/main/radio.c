/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Radio tuning, DSP, seek, ATS and periodic signal polling.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/i2c_master.h"
#include "si4732.h"
#include "si4732_patch.h"
#include "band.h"
#include "display.h"
#include "audio.h"
#include "battery.h"
#include "rds.h"
#include "app.h"

static const char *TAG = "radio";

/* AMPLFLT: AM power-line noise rejection filter (AM_CHANNEL_FILTER bit8).
   ATS-mini enables it for AM; kept runtime-switchable for A/B tests. */
static bool s_am_line_filter = true;

bool radio_am_line_filter(void)
{
    return s_am_line_filter;
}

esp_err_t radio_set_am_line_filter(bool on)
{
    s_am_line_filter = on;
    if (!s_radio_ok || s_ui.mode != RADIO_MODE_AM) {
        return ESP_OK;
    }
    return si4732_set_am_bandwidth(&s_radio,
                                   (si4732_am_bandwidth_t)s_bw[1],
                                   s_am_line_filter);
}

/* NB (noise blanker) — Si474x 전용 추정, probe로 수용 여부 확인.
   UI 메뉴 없이 web 토글로만 조작 (연구용). */
static bool s_am_nb = false;
static int  s_nb_probe = -1;   /* -1 = 미실행, 0 = 미지원, 1 = 지원 */

bool radio_am_nb(void)
{
    return s_am_nb;
}

int radio_nb_probe_result(void)
{
    return s_nb_probe;
}

esp_err_t radio_set_am_nb(bool on)
{
    s_am_nb = on;
    if (!s_radio_ok || s_ui.mode == RADIO_MODE_FM) {
        return ESP_OK;
    }
    if (on) {
        /* PU2CLR 기본값: rate 64, interval 55 µs, iir 300 Hz */
        return si4732_set_am_noise_blank(&s_radio, 64, 55, 300);
    }
    /* OFF: 블랭킹 레이트 0 = 공백 없음 (기본 복원에 준함) */
    return si4732_set_am_noise_blank(&s_radio, 0, 55, 300);
}

/* 쓰기-읽기 probe. AM 컴포넌트 활성 상태에서만 의미가 있다. */
esp_err_t radio_nb_probe(void)
{
    if (!s_radio_ok || s_ui.mode == RADIO_MODE_FM) {
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = false;
    esp_err_t err = si4732_am_nb_probe(&s_radio, &ok);
    if (err != ESP_OK) {
        s_nb_probe = -1;
        return err;
    }
    s_nb_probe = ok ? 1 : 0;
    return ESP_OK;
}

static si4732_sideband_t radio_sideband(void)
{
    switch (s_ui.mode) {
    case RADIO_MODE_LSB:
        return SI4732_SSB_LSB;
    case RADIO_MODE_USB:
        return SI4732_SSB_USB;
    default:
        return SI4732_SSB_OFF;
    }
}

int16_t radio_bfo_value(void)
{
    int16_t bfo = (int16_t)s_bfo;
    if (!band_is_ssb(s_ui.mode)) {
        return bfo;
    }
    uint32_t base = ((s_ui.freq_hz + 500u) / 1000u) * 1000u;
    int16_t fine = (int16_t)((int32_t)s_ui.freq_hz - (int32_t)base);
    /* BFO = tuned - requested frequency. Use the same sign for LSB and USB
       (matches the ats-mini reference implementation). */
    bfo -= fine;
    return bfo;
}

static uint32_t s_seek_started;
static uint32_t s_ats_timeout_ms;
static uint32_t s_ats_draw_ms;

#define SEEK_RSSI_MIN 4
#define SEEK_SNR_MIN  1
#define SEEK_FM_RSSI_MIN 5
#define SEEK_FM_SNR_MIN  2
#define SEEK_AM_RSSI_MIN 10
#define SEEK_AM_SNR_MIN  3
#define SEEK_HW_TIMEOUT_MS     15000   /* abort a stuck hardware seek */
#define SEEK_STATUS_RETRY       3
static bool s_ssb_prepare_failed;

static void scan_draw_radio(void)
{
    if (s_hmi != HMI_TUNE && s_hmi != HMI_SEEK) {
        return;
    }
    ui_draw_all(&s_ui);
}

static void scan_rsq_show(const si4732_rsq_t *r, bool redraw)
{
    s_ui.rsq = *r;
    s_ui.have_rsq = true;
    s_ui.stereo = r->stereo;
    if (redraw) {
        scan_draw_radio();
    }
}

esp_err_t radio_ensure_mode(void)
{    if (!s_radio_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    radio_mode_t mode = s_ui.mode;
    bool want_ssb = band_is_ssb(mode);
    bool patch_ok = (si4732_ssb_patch_size > 0);
    s_ui.patch_missing = want_ssb && !patch_ok;

    if (want_ssb && patch_ok) {
        if (s_powered && s_radio.func != SI4732_FUNC_AM) {
            si4732_power_down(&s_radio);
            s_powered = false;
            s_ssb_prepared = false;
        }
        if (!s_powered) {
            ESP_RETURN_ON_ERROR(si4732_power_up(&s_radio, SI4732_FUNC_AM), TAG, "pw");
            s_powered = true;
            s_ssb_prepared = false;
            s_ssb_prepare_failed = false;
            radio_set_volume();
        }
        if (!s_ssb_prepared && !s_ssb_prepare_failed) {
            ui_draw_loading("SSB LOADING");
            esp_err_t err = si4732_ssb_prepare(&s_radio,
                                               (si4732_ssb_bandwidth_t)s_bw[2]);
            if (err == ESP_OK) {
                s_ssb_prepared = true;
                s_powered = true;
                radio_set_volume();
            } else {
                ESP_LOGW(TAG, "SSB prep failed (%s) - fallback AM", esp_err_to_name(err));
                s_ssb_prepare_failed = true;
                s_ui.patch_missing = true;
                si4732_power_down(&s_radio);
                ESP_RETURN_ON_ERROR(si4732_power_up(&s_radio, SI4732_FUNC_AM),
                                    TAG, "pw");
                s_powered = true;
                radio_set_volume();
            }
            ui_draw_radio_freq(&s_ui);
        }
        /* SSB bandwidth is controlled by SSB_MODE.AUDIOBW; AM_CHANNEL_FILTER
           is an AM-only control (AN332). */
        si4732_set_ssb_mode(&s_radio, (si4732_ssb_bandwidth_t)s_bw[2],
                            true, false, true);
        si4732_set_bfo(&s_radio, radio_bfo_value());
        si4732_set_gpo1(&s_radio, true);
        dsp_apply();
        return ESP_OK;
    }

    si4732_func_t func = (mode == RADIO_MODE_FM) ? SI4732_FUNC_FM : SI4732_FUNC_AM;
    if (s_ssb_prepared && !want_ssb) {
        /* SSB 준비 상태에서 일반 AM/FM으로 전환할 때 프로세스 정리 */
        si4732_power_down(&s_radio);
        s_powered = false;
        s_ssb_prepared = false;
    }
    if (s_powered && s_radio.func != func) {
        si4732_power_down(&s_radio);
        s_powered = false;
    }
    if (!s_powered) {
        ESP_RETURN_ON_ERROR(si4732_power_up(&s_radio, func), TAG, "pw");
        s_powered = true;
        s_ssb_prepared = false;
        radio_set_volume();
        if (func == SI4732_FUNC_FM) {
            si4732_disable_fm_debug(&s_radio);
            si4732_set_deemphasis(&s_radio, s_deemph_75);
            si4732_set_stereo(&s_radio, s_stereo_force);
            si4732_rds_enable(&s_radio, s_rds_on);
        }
    }
    if (func == SI4732_FUNC_FM) {
        si4732_set_fm_bandwidth(&s_radio, s_bw[0]);
    } else {
        si4732_set_am_bandwidth(&s_radio, (si4732_am_bandwidth_t)s_bw[1],
                                s_am_line_filter);
    }
    si4732_set_gpo1(&s_radio, func == SI4732_FUNC_AM);
    dsp_apply();
    return ESP_OK;
}

/* Match the displayed frequency to the chip's tune resolution
   (FM 10 kHz, AM 1 kHz). SSB keeps the fine frequency; BFO handles it. */
static uint32_t radio_round_tuned_freq(uint32_t freq_hz)
{
    if (s_ui.mode == RADIO_MODE_FM) {
        return ((freq_hz + 5000u) / 10000u) * 10000u;
    }
    if (!band_is_ssb(s_ui.mode)) {
        return ((freq_hz + 500u) / 1000u) * 1000u;
    }
    return freq_hz;
}

/* FM tune wrapper: a new station invalidates RDS data, and the fixed RF
   gain (FM_AGC_OVERRIDE) must be re-applied after every tune. */
static esp_err_t radio_fm_tune(uint32_t freq_hz)
{
    freq_hz = radio_round_tuned_freq(freq_hz);
    esp_err_t err = si4732_fm_tune(&s_radio, freq_hz);
    if (err == ESP_OK) {
        s_ui.freq_hz = freq_hz;
        rds_parser_reset(&s_rds);
        /* AUTO is the chip default; only a fixed AGC needs re-applying. */
        if (s_agc_idx >= 0) {
            agc_apply();
        }
    }
    return err;
}

/* Fast tune for ATS/spectrum scans: no settle delay, caller waits. */
static esp_err_t radio_tune_fast(uint32_t freq_hz)
{
    if (s_ui.mode == RADIO_MODE_FM) {
        return si4732_fm_tune_fast(&s_radio, freq_hz);
    }
    return si4732_am_tune_fast(&s_radio, freq_hz, SI4732_SSB_OFF);
}

esp_err_t radio_tune(void)
{
    if (!s_radio_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err;
    if (s_ui.mode == RADIO_MODE_FM) {
        err = radio_fm_tune(s_ui.freq_hz);
    } else {
        si4732_sideband_t sb = SI4732_SSB_OFF;
        if (band_is_ssb(s_ui.mode) && !s_ui.patch_missing) {
            sb = radio_sideband();
        }
        s_ui.freq_hz = radio_round_tuned_freq(s_ui.freq_hz);
        err = si4732_am_tune(&s_radio, s_ui.freq_hz, sb);
        if (err == ESP_OK && band_is_ssb(s_ui.mode) && !s_ui.patch_missing) {
            si4732_set_bfo(&s_radio, radio_bfo_value());
            /* The SSB patch may drop AGC/AVC state after a tune command,
               so re-apply the user settings here (not only on mode entry). */
            agc_apply();
            si4732_set_am_avc_max_gain(&s_radio, s_avc);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tune failed (%s), I2C recovery", esp_err_to_name(err));
        if (si4732_bus_reset(&s_radio) == ESP_OK) {
            s_powered = false;
            s_ssb_prepared = false;
            if (radio_ensure_mode() == ESP_OK) {
                err = (s_ui.mode == RADIO_MODE_FM)
                      ? radio_fm_tune(s_ui.freq_hz)
                      : si4732_am_tune(&s_radio, s_ui.freq_hz,
                                       (band_is_ssb(s_ui.mode) && !s_ui.patch_missing)
                                       ? radio_sideband()
                                       : SI4732_SSB_OFF);
            }
        }
    }
    return err;
}

esp_err_t radio_apply(void)
{
    /* VHF(FM)로 전환할 때 SSB 로딩과 같은 모달 표시 */
    if (s_ui.mode == RADIO_MODE_FM && s_powered &&
        (s_radio.func != SI4732_FUNC_FM || s_ssb_prepared)) {
        ui_draw_loading("VHF LOADING");
        vTaskDelay(pdMS_TO_TICKS(350));
    }
    /* 밴드/모드 전환 시 앰프와 음소거를 먼저 정리 */
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, true);
    }
    board_amp(false);

    esp_err_t err = radio_ensure_mode();
    if (err != ESP_OK) {
        board_amp(true);
        return err;
    }
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, true);
    }
    err = radio_tune();
    vTaskDelay(pdMS_TO_TICKS(90));
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, false);
    }
    board_amp(true);
    return err;
}

void apply_step_snap(void)
{
    uint32_t step = eff_step_hz();
    if (step == 0) {
        return;
    }
    uint32_t snapped = band_snap_freq(s_ui.band_idx, s_ui.freq_hz, step);
    if (snapped != s_ui.freq_hz) {
        s_ui.freq_hz = snapped;
    }
}


void set_band(int idx)
{
    if (idx < 0 || (size_t)idx >= band_count) {
        return;
    }

    if ((size_t)s_ui.band_idx < 16) {
        s_band_freq[s_ui.band_idx] = s_ui.freq_hz;
        s_band_mem_valid[s_ui.band_idx] = true;
    }

    s_ui.band_idx = idx;
    const band_desc_t *b = &band_table[idx];
    if (b->default_mode == RADIO_MODE_FM) {
        s_ui.mode = RADIO_MODE_FM;
    } else if (s_ui.mode == RADIO_MODE_FM) {
        /* Leaving FM: adopt the band's own default (AM / LSB / USB)
           instead of always falling back to AM. */
        s_ui.mode = b->default_mode;
    } else if (!b->ham) {
        s_ui.mode = RADIO_MODE_AM;
    }
    uint32_t f = ((size_t)idx < 16 && s_band_mem_valid[idx])
                 ? s_band_freq[idx] : band_table[idx].min_hz;
    s_ui.freq_hz = band_clamp_freq(idx, f);
    sync_step();
    apply_step_snap();
    s_band_freq[idx] = s_ui.freq_hz;
    s_band_mem_valid[idx] = true;
    radio_apply();
    mark_change();
}

/* ------------------------------------------------------------- ?? */

static bool s_seek_hw_active;
static uint32_t s_seek_log_ms;
static uint8_t s_seek_err_count;

static void seek_draw(void)
{
    ui_draw_all(&s_ui);
}

/* FM seek thresholds also define the VALID-channel criterion. Keep the
   AN332 defaults (20 dBuV / 3 dB) during normal listening and use the
   sensitive scan values only while a seek/ATS scan is running. */
static void fm_seek_thresholds(bool scanning)
{
    if (!s_radio_ok || s_radio.func != SI4732_FUNC_FM) {
        return;
    }
    si4732_set_property(&s_radio, SI4732_PROP_FM_SEEK_TUNE_RSSI_THRESHOLD,
                        scanning ? SEEK_FM_RSSI_MIN : 20);
    si4732_set_property(&s_radio, SI4732_PROP_FM_SEEK_TUNE_SNR_THRESHOLD,
                        scanning ? SEEK_FM_SNR_MIN : 3);
}

/* AM seek thresholds also define VALID for AM tuning; restore the
   AN332/ATS-mini defaults (10 dBuV / 3 dB) after a seek. */
static void am_seek_thresholds(bool scanning)
{
    if (!s_radio_ok || s_radio.func != SI4732_FUNC_AM) {
        return;
    }
    si4732_set_property(&s_radio, SI4732_PROP_AM_SEEK_SNR_THRESHOLD,
                        scanning ? SEEK_AM_SNR_MIN : 3);
    si4732_set_property(&s_radio, SI4732_PROP_AM_SEEK_RSSI_THRESHOLD,
                        scanning ? SEEK_AM_RSSI_MIN : 10);
}

static esp_err_t seek_hw_start(bool up)
{
    const band_desc_t *b = &band_table[s_ui.band_idx];
    bool fm = (s_ui.mode == RADIO_MODE_FM);
    uint8_t rssi = fm ? SEEK_FM_RSSI_MIN : SEEK_AM_RSSI_MIN;
    uint8_t snr = fm ? SEEK_FM_SNR_MIN : SEEK_AM_SNR_MIN;
    /* Cancel any previous hardware seek and clear STC before starting a
       new one. A stale seek would otherwise keep running while the app
       thinks it is idle. */
    esp_err_t cerr = si4732_tune_status(&s_radio, true, true,
                                        NULL, NULL, NULL, NULL);
    if (cerr != ESP_OK) {
        ESP_LOGW(TAG, "seek pre-cancel failed (%s)", esp_err_to_name(cerr));
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    if (fm) {
        fm_seek_thresholds(true);
    } else {
        am_seek_thresholds(true);
    }
    ESP_LOGI(TAG, "seek start fm=%d up=%d f=%lu step=%lu thr=%u/%u",
             (int)fm, (int)up, (unsigned long)s_ui.freq_hz,
             (unsigned long)s_sw_step, rssi, snr);
    /* Align the seek band bottom to the same raster as the tuning snap
       (MW 9 kHz starts at 531 kHz, not 520 kHz). */
    uint32_t bottom = band_snap_freq(s_ui.band_idx, b->min_hz, s_sw_step);
    esp_err_t err = si4732_seek_start(&s_radio, up, bottom, b->max_hz,
                                      s_sw_step, rssi, snr);
    s_seek_hw_active = (err == ESP_OK);
    ESP_LOGI(TAG, "seek cmd err=%s", esp_err_to_name(err));
    return err;
}

static esp_err_t seek_hw_cancel(uint32_t *freq_hz)
{
    s_seek_hw_active = false;
    if (!s_radio_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    return si4732_tune_status(&s_radio, true, true, freq_hz, NULL, NULL, NULL);
}

static void seek_progress(uint32_t freq)
{
    if (s_sw_step == 0 || s_sw_max <= 0) {
        return;
    }
    uint32_t diff = (freq >= s_sw_orig) ? freq - s_sw_orig
                                        : s_sw_orig - freq;
    int steps = (int)((diff + s_sw_step / 2) / s_sw_step);
    if (steps > s_sw_max) {
        steps = s_sw_max;
    }
    s_sw_steps = steps;
    s_ui.sw_steps = steps;
}

bool seek_start(bool up)
{
    if (!s_radio_ok || s_seeking) {
        return false;
    }
    if (band_is_ssb(s_ui.mode)) {
        return false;
    }

    const band_desc_t *b = &band_table[s_ui.band_idx];
    uint32_t step = eff_step_hz();
    if (s_ui.mode == RADIO_MODE_FM) {
        step = 100000;
    } else if (step < 5000u) {
        /* 1 kHz is the wheel step, not a usable seek raster: use the band
           raster instead (MW/LW 9 kHz, SW 5 kHz). */
        step = (s_ui.band_idx == 1 || s_ui.band_idx == 2) ? 9000u : 5000u;
    }

    s_sw_step = step;
    s_sw_dir = up ? 1 : -1;
    s_sw_orig = s_ui.freq_hz;
    s_sw_steps = 0;
    s_sw_max = (int)((b->max_hz - b->min_hz) / step) + 1;
    s_ui.sw_steps = 0;
    s_ui.sw_max = s_sw_max;

    s_seeking = true;
    s_ui.seeking = true;
    s_seek_started = now_ms();
    s_seek_log_ms = 0;
    s_seek_err_count = 0;
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, true);
    }
    if (seek_hw_start(up) != ESP_OK) {
        s_seeking = false;
        s_ui.seeking = false;
        if (s_radio_ok) {
            si4732_set_mute(&s_radio, false);
        }
        toast("SEEK ERR");
        seek_draw();
        return false;
    }
    seek_draw();
    return true;
}

esp_err_t seek_tune(uint32_t freq)
{
    if (s_ui.mode == RADIO_MODE_FM) {
        return radio_fm_tune(freq);
    }
    freq = radio_round_tuned_freq(freq);
    s_ui.freq_hz = freq;
    return si4732_am_tune(&s_radio, freq, SI4732_SSB_OFF);
}

void seek_finish(bool found)
{
    /* Always stop the hardware seek. A completed seek is harmless to cancel;
       an early/failed one must not keep scanning after the UI stops. */
    if (s_seek_hw_active) {
        seek_hw_cancel(NULL);
    }
    if (!found) {
        if (s_radio_ok) {
            s_ui.freq_hz = s_sw_orig;
            seek_tune(s_ui.freq_hz);
        }
    }
    s_seek_err_count = 0;
    s_seek_hw_active = false;
    s_seeking = false;
    s_ui.seeking = false;
    fm_seek_thresholds(false);
    am_seek_thresholds(false);
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, false);
    }
    if (found) {
        /* Hardware seek changed the station without a tune command, so the
           previous station's RDS data must be dropped here. */
        if (s_ui.mode == RADIO_MODE_FM) {
            rds_parser_reset(&s_rds);
        } else {
            /* Keep the displayed frequency on the band raster. */
            uint32_t snapped = band_snap_freq(s_ui.band_idx, s_ui.freq_hz,
                                              eff_step_hz());
            if (snapped != s_ui.freq_hz) {
                s_ui.freq_hz = snapped;
                (void)seek_tune(snapped);
            }
        }
        mark_change();
        s_freq_dirty = true;
    }
    ui_draw_all(&s_ui);
    if (s_ats != ATS_SEEK) {
        toast(found ? "FOUND" : "NO STN");
    } else {
        toast_redraw();
    }
}

void seek_cancel(void)
{
    uint32_t freq = 0;
    if (s_seek_hw_active) {
        seek_hw_cancel(&freq);
    }
    if (s_radio_ok) {
        s_ui.freq_hz = s_sw_orig;
        seek_tune(s_ui.freq_hz);
        si4732_set_mute(&s_radio, false);
    }
    s_seeking = false;
    s_ui.seeking = false;
    fm_seek_thresholds(false);
    am_seek_thresholds(false);
    toast("SEEK STOP");
}

void seek_stop(void)
{
    if (!s_seeking) {
        return;
    }
    uint32_t freq = 0;
    if (s_seek_hw_active) {
        esp_err_t err = seek_hw_cancel(&freq);
        if (err == ESP_OK && freq) {
            s_ui.freq_hz = band_clamp_freq(s_ui.band_idx, freq);
        }
    }
    s_seeking = false;
    s_ui.seeking = false;
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, false);
    }
    mark_change();
    s_freq_dirty = true;
    fm_seek_thresholds(false);
    am_seek_thresholds(false);
    ui_draw_all(&s_ui);
    toast("SEEK STOP");
}

void seek_reverse(bool up)
{
    (void)up;
    /* Opposite encoder turn means "stop", not "start a new seek in the
       other direction". seek_stop() sends TUNE_STATUS CANCEL to the chip. */
    seek_stop();
}

void seek_poll(uint32_t now)
{
    if (!s_seeking) {
        return;
    }
    if ((uint32_t)(now - s_seek_started) > SEEK_HW_TIMEOUT_MS) {
        ESP_LOGW(TAG, "seek timeout -> cancel");
        seek_finish(false);   /* seek_finish() cancels the hardware seek */
        return;
    }

    uint32_t freq = 0;
    bool stc = false;
    bool valid = false;
    bool bltf = false;
    /* Read TUNE_STATUS without INTACK while scanning so STCINT is not
       cleared before completion. */
    esp_err_t err = si4732_tune_status(&s_radio, false, false,
                                       &freq, &stc, &valid, &bltf);
    if (err != ESP_OK) {
        if (++s_seek_err_count >= SEEK_STATUS_RETRY) {
            ESP_LOGW(TAG, "seek status failed %u times -> cancel",
                     (unsigned)s_seek_err_count);
            seek_finish(false);
        } else if ((int32_t)(now - s_seek_log_ms) >= 0) {
            ESP_LOGW(TAG, "seek status err=%s", esp_err_to_name(err));
            s_seek_log_ms = now + 250;
        }
        return;
    }
    s_seek_err_count = 0;

    if (stc || (int32_t)(now - s_seek_log_ms) >= 0) {
        ESP_LOGI(TAG, "seek poll stc=%d valid=%d bltf=%d f=%lu",
                 (int)stc, (int)valid, (int)bltf, (unsigned long)freq);
        s_seek_log_ms = now + 250;
    }

    /* Update the display during the scan, independent of VALID. */
    if (freq) {
        freq = band_clamp_freq(s_ui.band_idx, freq);
        if (freq != s_ui.freq_hz) {
            s_ui.freq_hz = freq;
            seek_progress(freq);
            scan_draw_radio();
        }
    }

    /* Completion is STC only. VALID/BLTF can be stale from the previous
       tune; using them as a fallback ended the app-side seek while the
       chip was still scanning. */
    if (!stc) {
        return;
    }

    /* Clear STC and read the final frequency/validity. */
    uint32_t f2 = 0;
    bool valid2 = false;
    if (si4732_tune_status(&s_radio, true, false,
                           &f2, NULL, &valid2, NULL) == ESP_OK) {
        if (f2) {
            freq = f2;
        }
        valid = valid2;
    }
    if (freq) {
        freq = band_clamp_freq(s_ui.band_idx, freq);
        if (freq != s_ui.freq_hz) {
            s_ui.freq_hz = freq;
            seek_progress(freq);
        }
    }

    if (valid && freq) {
        s_ui.freq_hz = freq;
        seek_finish(true);
    } else {
        seek_finish(false);
    }
}

void ats_cancel(void)
{
    if (s_ats != ATS_SEEK) {
        return;
    }
    s_ats = ATS_DONE;
    fm_seek_thresholds(false);
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, false);
    }
    s_seeking = false;
    s_ui.seeking = false;
    s_ats_list.count = s_ats_found;
    storage_save_ats(&s_ats_list);
    page_radio();
    s_toast[0] = '\0';
    open_ats_list();
    s_ats_autolist = true;
    s_ats_show_until = now_ms() + 1500;
}

/* -------------------------------------------------------------- ATS */

void page_radio(void);
void fmt_freq_short(uint32_t hz, int mode, char *buf, size_t n);

void ats_finish(const char *msg)
{
    (void)msg;
    s_ats = ATS_DONE;
    s_ats_hold = false;
    fm_seek_thresholds(false);
    s_ui.seeking = false;
    s_ats_list.count = s_ats_found;
    storage_save_ats(&s_ats_list);
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, false);
    }

    if (s_ats_found > 0) {
        const preset_entry_t *p = &s_ats_list.slot[0];
        s_ui.band_idx = (p->band < band_count) ? p->band : 0;
        s_ui.mode = (p->mode <= RADIO_MODE_USB) ? (radio_mode_t)p->mode
                                                : band_table[s_ui.band_idx].default_mode;
        s_ui.freq_hz = band_clamp_freq(s_ui.band_idx, p->freq_hz);
        sync_step();
        apply_step_snap();
        if (s_radio_ok) {
            seek_tune(s_ui.freq_hz);
        }
    }
    mark_change();
    s_freq_dirty = false;
    s_toast[0] = '\0';
    open_ats_list();
    s_ats_autolist = true;
    s_ats_show_until = now_ms() + 1500;
}

void ats_start(void)
{
    if (!s_radio_ok || s_ui.mode != RADIO_MODE_FM) {
        toast("ATS: FM ONLY");
        return;
    }
    const band_desc_t *b = &band_table[s_ui.band_idx];
    s_ats_band = s_ui.band_idx;
    s_ats_found = 0;
    s_ats_last_freq = 0;
    s_ats_hold = false;
    s_ats_draw_ms = 0;
    memset(&s_ats_list, 0, sizeof(s_ats_list));
    s_ats = ATS_SEEK;
    s_ats_ms = now_ms();
    s_ats_freq = b->min_hz;
    s_ats_step = eff_step_hz();
    if (s_ui.mode == RADIO_MODE_FM) {
        s_ats_step = 100000;
    }
    fm_seek_thresholds(true);
    if (s_radio_ok) {
        si4732_set_mute(&s_radio, true);
    }
    s_ui.freq_hz = s_ats_freq;
    radio_tune_fast(s_ui.freq_hz);
    s_ui.seeking = true;
    s_ui.sw_steps = 0;
    s_ui.sw_max = (int)((b->max_hz - b->min_hz) / s_ats_step) + 1;
    s_ats_timeout_ms = (uint32_t)s_ui.sw_max * 60 + 20000;
    s_ats_wait = now_ms() + 50;
    ui_draw_all(&s_ui);
    toast("ATS START");
}

void ats_poll(uint32_t now)
{
    if (s_ats != ATS_SEEK) {
        return;
    }
    if ((int32_t)(now - s_ats_ms) > (int32_t)s_ats_timeout_ms) {
        ats_finish("ATS TIMEOUT");
        return;
    }

    const band_desc_t *b = &band_table[s_ats_band];

    if (s_ats_wait) {
        if ((int32_t)(now - s_ats_wait) < 0) {
            return;
        }
        s_ats_wait = 0;

        if (s_ats_hold) {
            s_ats_hold = false;
            ui_draw_all(&s_ui);
        } else {
            si4732_rsq_t r;
            bool good;
            esp_err_t e = (s_ui.mode == RADIO_MODE_FM)
                          ? si4732_fm_rsq(&s_radio, &r)
                          : si4732_am_rsq(&s_radio, &r);
            if (s_ui.mode == RADIO_MODE_FM) {
                good = (e == ESP_OK && r.valid &&
                        r.rssi >= SEEK_RSSI_MIN && r.snr >= SEEK_SNR_MIN);
            } else {
                good = (e == ESP_OK && r.valid &&
                        r.rssi >= SEEK_AM_RSSI_MIN);
            }
            if (e == ESP_OK) {
                scan_rsq_show(&r, !good);
            }
            uint32_t gap = (s_ui.mode == RADIO_MODE_FM) ? 100000
                         : ((s_ats_band == 1 || s_ats_band == 2) ? 9000 : 5000);
            bool far_enough = (s_ats_last_freq == 0 ||
                               s_ui.freq_hz - s_ats_last_freq >= gap);
            if (good && far_enough && s_ats_found < STORAGE_ATS_SLOTS) {
                preset_entry_t *p = &s_ats_list.slot[s_ats_found++];
                p->freq_hz = s_ui.freq_hz;
                p->band = (uint8_t)s_ats_band;
                p->mode = (uint8_t)s_ui.mode;
                s_ats_last_freq = s_ui.freq_hz;
                ui_draw_all(&s_ui);
                s_ats_hold = true;
                s_ats_wait = now + 300;
                return;
            }
        }
    }

    if (s_ats_found >= STORAGE_ATS_SLOTS) {
        ats_finish("ATS FULL");
        return;
    }

    if (s_ats_freq >= b->max_hz - s_ats_step / 2) {
        ats_finish("ATS DONE");
        return;
    }

    s_ats_freq += s_ats_step;
    if (s_ats_freq > b->max_hz) {
        s_ats_freq = b->max_hz;
    }
    s_ui.freq_hz = s_ats_freq;
    s_ui.sw_steps++;
    if (radio_tune_fast(s_ui.freq_hz) != ESP_OK) {
        ats_finish("ATS STOP");
        return;
    }
    s_ats_wait = now + 50;
    /* 스캔 중에도 주파수 숫자가 따라가도록 주기적으로 갱신 */
    if ((int32_t)(now - s_ats_draw_ms) >= 0) {
        s_ats_draw_ms = now + 100;
        ui_draw_radio_freq(&s_ui);
    }
}

/* ------------------------------------------------------------- 메뉴 */

const char *const RM_LABELS[RM_COUNT] = {
    "< BACK",
    "BAND", "MODE", "STEP", "BW", "BFO",
    "SEEK", "SCAN", "ATS",
    "FAVORITES", "RDS", "STEREO", "DEEMPH",
    "VOLUME", "AGC", "RF GAIN", "AVC", "SOFTMUTE", "SQUELCH",
    "MOD A", "MOD B", "MOD C", "MOD D",
    "MOD E", "MOD F", "MOD G", "MOD H",
    "SETTINGS"
};

/* ------------------------------------------------------------- 메뉴 */

const char *const ST_LABELS[ST_COUNT] = {
    "< BACK",
    "BRIGHT", "SCREEN", "SLEEP", "POWER SAVE",
    "UNIT", "THEME",
    "TIME", "UTC",
    "BATT", "BATT CAL", "CPU", "WI-FI",
    "EIBI", "QUICK SET",
    "SIG spd", "BOTTOM",
    "MOD A", "MOD B", "MOD C", "MOD D",
    "MOD E", "MOD F", "MOD G", "MOD H",
    "ABOUT"
};



const char *bw_label(void)
{
    static const char *fm[]  = {"AUTO", "110K", "84K", "60K", "40K"};
    static const char *am[]  = {"6K", "4K", "3K", "2K", "1K", "1.8K", "2.5K"};
    static const char *ssb[] = {"1.2K", "2.2K", "3K", "4K", "500", "1K"};
    int slot = mode_slot(s_ui.mode);
    switch (slot) {
    case 0:  return fm[s_bw[0] % 5];
    case 1:  return am[s_bw[1] % 7];
    default: return ssb[s_bw[2] % 6];
    }
}

int bw_max(void)
{
    int slot = mode_slot(s_ui.mode);
    return (slot == 0) ? 4 : (slot == 1 ? 6 : 5);
}

esp_err_t bw_apply(void)
{
    if (!s_radio_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    switch (s_ui.mode) {
    case RADIO_MODE_FM:
        return si4732_set_fm_bandwidth(&s_radio, s_bw[0]);
    case RADIO_MODE_AM:
        return si4732_set_am_bandwidth(&s_radio, (si4732_am_bandwidth_t)s_bw[1],
                                       s_am_line_filter);
    default:
        /* SSB: AUDIOBW only (AM_CHANNEL_FILTER does not apply). */
        return si4732_set_ssb_mode(&s_radio, (si4732_ssb_bandwidth_t)s_bw[2],
                                   true, false, true);
    }
}

int preset_index_for_freq(uint32_t freq, int band)
{
    for (int i = 0; i < s_presets.count; ++i) {
        if (s_presets.slot[i].freq_hz == freq &&
            s_presets.slot[i].band == (uint8_t)band) {
            return i;
        }
    }
    return -1;
}

/* 저장된 즐겨찾기 이름 (없으면 빈 문자열) */
const char *preset_name_at(int idx)
{
    if (idx < 0 || idx >= STORAGE_PRESET_SLOTS) {
        return "";
    }
    return s_pnames.name[idx];
}

int band_marks(int band, uint32_t *out, int max)
{
    if (!out || max <= 0) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < s_presets.count && n < max; ++i) {
        if (s_presets.slot[i].freq_hz &&
            s_presets.slot[i].band == (uint8_t)band) {
            out[n++] = s_presets.slot[i].freq_hz;
        }
    }
    for (int i = 0; i < s_ats_list.count && n < max; ++i) {
        if (s_ats_list.slot[i].freq_hz &&
            s_ats_list.slot[i].band == (uint8_t)band) {
            out[n++] = s_ats_list.slot[i].freq_hz;
        }
    }
    return n;
}

int ats_index_for_freq(uint32_t freq, int band)
{
    for (int i = 0; i < s_ats_list.count; ++i) {
        if (s_ats_list.slot[i].freq_hz == freq &&
            s_ats_list.slot[i].band == (uint8_t)band) {
            return i;
        }
    }
    return -1;
}

void fmt_freq_short(uint32_t hz, int mode, char *buf, size_t n)
{
    if (mode == RADIO_MODE_FM) {
        snprintf(buf, n, "%lu.%02lu", (unsigned long)(hz / 1000000),
                 (unsigned long)((hz / 10000) % 100));
    } else if (s_unit_mode == 1) {
        snprintf(buf, n, "%lu", (unsigned long)(hz / 1000));
    } else {
        snprintf(buf, n, "%lu.%03lu", (unsigned long)(hz / 1000000),
                 (unsigned long)((hz / 1000) % 1000));
    }
}


void rsq_poll(uint32_t now)
{
    if (s_hold_ring) {
        return;
    }
    static uint32_t last;
    bool fast = (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK);
    /* SIG spd: LOW=250ms(부드러움), HIGH=30ms(빠름) */
    uint32_t period = fast ? (s_dbg_sig == 0 ? 250u : 30u)
                           : (uint32_t)RSQ_PERIOD;
    if (!s_radio_ok || s_ats == ATS_SEEK || (now - last) < period) {
        return;
    }
    last = now;

    si4732_rsq_t r;
    esp_err_t err = (s_radio.func == SI4732_FUNC_FM)
                    ? si4732_fm_rsq(&s_radio, &r)
                    : si4732_am_rsq(&s_radio, &r);
    if (err != ESP_OK) {
        return;
    }

    /* EMA (시크 중에는 원시값을 그대로 표시) */
    static float ema_rssi = -1.0f;
    static float ema_snr = -1.0f;
    if (s_seeking) {
        ema_rssi = (float)r.rssi;
        ema_snr = (float)r.snr;
        s_ui.rsq = r;
        s_ui.have_rsq = true;
        s_ui.stereo = r.stereo;
        ui_draw_adv_signal4b(&s_ui);
        return;
    }
    if (ema_rssi < 0.0f) {
        ema_rssi = (float)r.rssi;
        ema_snr = (float)r.snr;
    }
    {
        float a = 0.25f;
        float dr = ((float)r.rssi - ema_rssi) * a;
        if (dr > 2.0f) { dr = 2.0f; }
        if (dr < -2.0f) { dr = -2.0f; }
        ema_rssi += dr;
        float dn = ((float)r.snr - ema_snr) * a;
        if (dn > 2.0f) { dn = 2.0f; }
        if (dn < -2.0f) { dn = -2.0f; }
        ema_snr += dn;
    }

    si4732_rsq_t d = r;
    d.rssi = (uint8_t)(ema_rssi + 0.5f);
    d.snr = (uint8_t)(ema_snr + 0.5f);
    s_ui.rsq = d;
    s_ui.have_rsq = true;
    bool stereo_changed = (s_ui.stereo != r.stereo);
    s_ui.stereo = r.stereo;

    int sq = mode_dsp_slot(s_ui.mode);
    bool mute = false;
    if (s_squelch_on) {
        int level = (s_squelch_type[sq] == 0) ? r.rssi : r.snr;
        mute = (level < s_squelch_thr[sq]);
    }
    bool mute_changed = (mute != s_squelch_muted);
    if (mute_changed) {
        s_squelch_muted = mute;
        si4732_set_mute(&s_radio, mute);
    }
    if (stereo_changed && !mute_changed &&
        (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK)) {
        ui_draw_band_meta(&s_ui);
    }

    if (s_hmi == HMI_TUNE || s_hmi == HMI_SEEK) {
        if (mute_changed) {
            ui_draw_all(&s_ui);   /* SQ 상태 갱신 */
        } else {
            ui_draw_adv_signal4b(&s_ui);
        }
    }
}

void rds_poll(uint32_t now)
{
    static uint32_t last;
    if (!s_radio_ok || !s_rds_on || s_ui.mode != RADIO_MODE_FM ||
        s_seeking || (now - last) < RDS_PERIOD) {
        return;
    }
    last = now;

    /* Si4732 RDS FIFO는 4그룹. 500ms에 1그룹만 읽으면 초당 11.4그룹
       유입을 따라가지 못해 FIFO가 넘치고 PS/RT 확보가 매우 느려진다.
       폴링 주기를 100ms로 줄이고 FIFO가 빌 때까지 읽어낸다. */
    bool fed = false;
    for (int i = 0; i < 4; ++i) {
        si4732_rds_t grp;
        if (si4732_rds_status(&s_radio, &grp) != ESP_OK) {
            break;
        }
        if (!grp.synced || grp.fifo_used == 0) {
            break;
        }
        rds_parser_feed(&s_rds, grp.block_a, grp.block_b, grp.block_c,
                        grp.block_d, grp.errors);
        fed = true;
        if (grp.fifo_used <= 1) {
            break;
        }
    }
    if (!fed) {
        return;
    }
    uint32_t h = 2166136261u;
    for (const char *p = s_rds.ps; *p; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    for (const char *p = s_rds.rt; *p; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    h = (h ^ s_rds.pty) * 16777619u;
    h = (h ^ (uint8_t)((s_rds.rt_valid ? 2 : 0) |
                      (s_rds.ps_valid ? 1 : 0))) * 16777619u;
    static uint32_t last_rds_hash;
    if (h != last_rds_hash) {
        last_rds_hash = h;
        if (s_hmi == HMI_TUNE) {
            ui_draw_all(&s_ui);
        }
    }
}

void battery_poll(uint32_t now)
{
    static uint32_t last;
    if ((now - last) < BATT_PERIOD) {
        return;
    }
    last = now;
    if (s_wifi_mode == 2 && s_sta_connected && s_wifi_started) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s_wifi_rssi = ap.rssi;
            s_ui.wifi_rssi = (int8_t)ap.rssi;
        }
    } else {
        s_wifi_rssi = 0;
        s_ui.wifi_rssi = 0;
    }
    if (battery_update(&s_batt) == ESP_OK) {
        static int last_pct = -1;
        static int last_mv = -1;
        static int last_low = -1;
        int mv = s_batt.millivolt / 10;
        bool changed = (s_batt.percent != last_pct || mv != last_mv ||
                        (int)s_batt.low != last_low);
        last_pct = s_batt.percent;
        last_mv = mv;
        last_low = s_batt.low;
        s_ui.battery = s_batt;
        s_ui.have_battery = true;
        if (changed) {
            wifi_update_tx_power();
        }
        if (changed && s_hmi == HMI_TUNE) {
            ui_draw_header(&s_ui);
            ui_draw_status(&s_ui);
        }
    }
}

void spectrum_scan_start(void)
{
    if (!s_radio_ok || band_is_ssb(s_ui.mode)) {
        toast("SCAN: FM/AM ONLY");
        return;
    }
    const band_desc_t *b = &band_table[s_ui.band_idx];
    s_spec_band = s_ui.band_idx;
    s_spec_min = b->min_hz;

    uint32_t base_step;
    if (s_ui.mode == RADIO_MODE_FM) {
        base_step = 100000;
    } else if (s_ui.band_idx == 1 || s_ui.band_idx == 2) {
        base_step = 1000;
    } else {
        base_step = 5000;
    }

    uint32_t span = b->max_hz - b->min_hz;
    uint32_t min_step = (span + SPEC_SCAN_MAX - 2) / (SPEC_SCAN_MAX - 1);
    if (min_step < base_step) {
        min_step = base_step;
    }
    min_step = ((min_step + base_step - 1) / base_step) * base_step;
    s_spec_step = min_step;

    int count = (int)((span + s_spec_step - 1) / s_spec_step) + 1;
    if (count > SPEC_SCAN_MAX) {
        count = SPEC_SCAN_MAX;
    }
    s_spec_max = s_spec_min + (uint32_t)(count - 1) * s_spec_step;
    if (s_spec_max > b->max_hz) {
        s_spec_max = b->max_hz;
    }
    s_spec_count = count;
    s_spec_idx = 0;
    s_spec_cursor = 0;
    s_spec_freq = s_spec_min;
    s_spec_orig = s_ui.freq_hz;
    s_spec_active = true;
    s_spec_done = false;
    memset(s_spec_rssi, 0, sizeof(s_spec_rssi));
    memset(s_spec_snr, 0, sizeof(s_spec_snr));

    s_hmi = HMI_SCAN;
    s_nav_ms = now_ms();
    s_ui.seeking = false;
    s_ui.seek_mode = false;
    si4732_set_mute(&s_radio, true);
    s_ui.freq_hz = s_spec_freq;
    radio_tune_fast(s_spec_freq);
    s_spec_wait = now_ms() + (s_ui.mode == RADIO_MODE_FM ? 40 : 30);
    ui_draw_spectrum(&s_ui, band_table[s_spec_band].name, s_spec_min,
                     s_spec_max, s_spec_rssi, s_spec_snr, s_spec_count, 0, 0,
                     false);
}

void spectrum_scan_poll(uint32_t now)
{
    if (!s_spec_active) {
        return;
    }
    if ((int32_t)(now - s_spec_wait) < 0) {
        return;
    }

    si4732_rsq_t r;
    esp_err_t e = (s_radio.func == SI4732_FUNC_FM)
                  ? si4732_fm_rsq(&s_radio, &r)
                  : si4732_am_rsq(&s_radio, &r);
    if (e == ESP_OK) {
        /* 무조건 신호세기 기록 (valid 여부와 무관) */
        s_spec_rssi[s_spec_idx] = r.rssi;
        s_spec_snr[s_spec_idx] = r.snr;
        s_spec_cursor = s_spec_idx;
    }
    ++s_spec_idx;

    if (s_spec_idx >= s_spec_count) {
        s_spec_active = false;
        s_spec_done = true;
        int best = 0;
        int bestv = -1;
        for (int i = 0; i < s_spec_count; ++i) {
            int v = s_spec_rssi[i] + s_spec_snr[i] * 2;
            if (v > bestv) {
                bestv = v;
                best = i;
            }
        }
        s_spec_cursor = best;
        si4732_set_mute(&s_radio, false);
        s_spec_freq = s_spec_min + (uint32_t)best * s_spec_step;
        if (s_spec_freq > s_spec_max) {
            s_spec_freq = s_spec_max;
        }
        s_ui.freq_hz = s_spec_freq;
        seek_tune(s_spec_freq);
        s_ui.preset_slot = preset_index_for_freq(s_ui.freq_hz, s_ui.band_idx);
        ui_draw_spectrum(&s_ui, band_table[s_spec_band].name, s_spec_min,
                         s_spec_max, s_spec_rssi, s_spec_snr, s_spec_count,
                         s_spec_count, s_spec_cursor, true);
        return;
    }

    s_spec_freq = s_spec_min + (uint32_t)s_spec_idx * s_spec_step;
    if (s_spec_freq > s_spec_max) {
        s_spec_freq = s_spec_max;
    }
    s_spec_cursor = s_spec_idx;
    s_ui.freq_hz = s_spec_freq;
    radio_tune_fast(s_spec_freq);
    s_spec_wait = now + (s_ui.mode == RADIO_MODE_FM ? 40 : 30);
    ui_draw_spectrum(&s_ui, band_table[s_spec_band].name, s_spec_min,
                     s_spec_max, s_spec_rssi, s_spec_snr, s_spec_count,
                     s_spec_idx, s_spec_cursor, false);
}

void spectrum_scan_cancel(void)
{
    if (!s_spec_active && !s_spec_done) {
        return;
    }
    s_spec_active = false;
    s_spec_done = false;
    si4732_set_mute(&s_radio, false);
    s_ui.freq_hz = s_spec_orig;
    seek_tune(s_spec_orig);
    page_radio();
}

void spectrum_scan_apply(void)
{
    if (!s_spec_done) {
        return;
    }
    uint32_t f = s_spec_min + (uint32_t)s_spec_cursor * s_spec_step;
    s_spec_done = false;
    si4732_set_mute(&s_radio, false);
    s_ui.freq_hz = band_clamp_freq(s_spec_band, f);
    seek_tune(s_ui.freq_hz);
    page_radio();
}

