/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * EC11 엔코더 (PCNT 쿼드라처 디코드).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PCNT 단위 생성 + A/B 채널 설치 (예제 quadrature 설정 채용)
 * @note  디바운스는 글리치 필터(1 µs)만 적용. 소프트 디바운스는 TODO.
 */
esp_err_t encoder_init(gpio_num_t pin_a, gpio_num_t pin_b);

/**
 * @brief 이전 호출 이후 증감분 반환 (读后 카운트 클리어)
 * @return 증가=시계방향(+), 감소=반시계(-)
 */
int encoder_get_delta(void);

/** @brief 카운트 클리어만 수행 */
void encoder_reset(void);

/** @brief PCNT 단위 해제 */
void encoder_deinit(void);

#ifdef __cplusplus
}
#endif
