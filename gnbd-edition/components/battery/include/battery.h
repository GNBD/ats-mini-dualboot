/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 배터리 전압 측정 (ADC1, IO4).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t millivolt;   /* 배터리 전압 (mV) */
    uint8_t  percent;     /* 0–100 */
    bool     charging;    /* 충전 중 추정 (전압 상승) */
    bool     low;         /* 저전압 경고 */
} battery_state_t;

/** @brief ADC 초기화 (oneshot, 12bit) */
esp_err_t battery_init(int adc_gpio);

/** @brief 전압/잔량 갱신 (평균 8회) */
esp_err_t battery_update(battery_state_t *out);

/** @brief 최근 측정값 */
const battery_state_t *battery_get(void);

/** @brief 전압 보정값 설정 (mV, -1000..1000) */
esp_err_t battery_set_calibration(int16_t offset_mv);

/** @brief 현재 전압 보정값 */
int16_t battery_get_calibration(void);

#ifdef __cplusplus
}
#endif
