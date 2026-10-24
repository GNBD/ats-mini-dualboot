/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 배터리 전압 측정: ADC oneshot + 분압 보정 + 리튬 잔량 테이블.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "battery.h"

static const char *TAG = "battery";

#define BATT_ADC_ATTEN   ADC_ATTEN_DB_12
#define BATT_DIVIDER     2.0f    /* 1:1 분압 (필요 시 보정) */
#define BATT_LOW_MV      3400
#define BATT_EMPTY_MV    3200
#define BATT_FULL_MV     4200

static adc_oneshot_unit_handle_t s_unit;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok;
static adc_channel_t             s_channel;
static battery_state_t           s_state;
static uint16_t                  s_charge_ref;
static uint8_t                   s_charge_score;
static int16_t                   s_cal_mv;
static uint16_t                  s_hist[4];
static uint8_t                   s_hist_n;
static uint8_t                   s_hist_pos;

esp_err_t battery_init(int adc_gpio)
{
    adc_unit_t unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(adc_gpio, &unit, &s_channel),
                        TAG, "adc channel");
    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id = unit,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&ucfg, &s_unit), TAG, "adc unit");

    adc_oneshot_chan_cfg_t ccfg = {
        .atten = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_unit, s_channel, &ccfg),
                        TAG, "adc channel cfg");

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
    ESP_LOGI(TAG, "init ok (gpio=%d ch=%d cali=%d)", adc_gpio, s_channel, s_cali_ok);
    return ESP_OK;
}

static uint8_t mv_to_percent(uint16_t mv)
{
    if (mv >= BATT_FULL_MV) return 100;
    if (mv <= BATT_EMPTY_MV) return 0;
    return (uint8_t)((uint32_t)(mv - BATT_EMPTY_MV) * 100 /
                     (BATT_FULL_MV - BATT_EMPTY_MV));
}

esp_err_t battery_update(battery_state_t *out)
{
    ESP_RETURN_ON_FALSE(s_unit, ESP_ERR_INVALID_STATE, TAG, "not init");

    uint32_t sum = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_unit, s_channel, &raw);
        if (err != ESP_OK) {
            return err;
        }
        int mv = 0;
        if (s_cali_ok) {
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
        } else {
            mv = raw * 3300 / 4095;
        }
        sum += (uint32_t)mv;
    }
    int32_t measured = (int32_t)((sum / 16) * BATT_DIVIDER) + s_cal_mv;
    if (measured < 0) {
        measured = 0;
    } else if (measured > 6000) {
        measured = 6000;
    }
    uint16_t mv = (uint16_t)measured;

    s_hist[s_hist_pos] = mv;
    s_hist_pos = (uint8_t)((s_hist_pos + 1) & 3);
    if (s_hist_n < 4) {
        s_hist_n++;
    }
    uint32_t avg = mv;
    if (s_hist_n >= 4) {
        uint32_t t = 0;
        for (int i = 0; i < 4; ++i) { t += s_hist[i]; }
        avg = t / 4;
    }

    s_state.millivolt = (uint16_t)avg;
    s_state.percent = mv_to_percent((uint16_t)avg);
    s_state.low = avg < BATT_LOW_MV;

    if (s_charge_ref == 0) {
        s_charge_ref = (uint16_t)avg;
    }
    int rise = (int)avg - (int)s_charge_ref;
    if (rise >= 10) {
        if (s_charge_score < 8) {
            s_charge_score++;
        }
        s_charge_ref = (uint16_t)avg;
    } else if (rise <= -15) {
        s_charge_score = 0;
        s_charge_ref = (uint16_t)avg;
    }
    s_state.charging = s_charge_score >= 3;

    if (out) {
        *out = s_state;
    }
    return ESP_OK;
}

const battery_state_t *battery_get(void)
{
    return &s_state;
}

esp_err_t battery_set_calibration(int16_t offset_mv)
{
    if (offset_mv < -1000) {
        offset_mv = -1000;
    } else if (offset_mv > 1000) {
        offset_mv = 1000;
    }
    s_cal_mv = offset_mv;
    s_hist_n = 0;
    s_hist_pos = 0;
    s_charge_ref = 0;
    s_charge_score = 0;
    return ESP_OK;
}

int16_t battery_get_calibration(void)
{
    return s_cal_mv;
}
