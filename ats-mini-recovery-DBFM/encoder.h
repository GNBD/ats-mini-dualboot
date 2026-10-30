/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * EC11 encoder (PCNT quadrature decode).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the PCNT unit and install the A/B channels.
 * @note  Debounce is the hardware glitch filter (1 us).
 */
esp_err_t encoder_init(gpio_num_t pin_a, gpio_num_t pin_b);

/**
 * @brief Detents since the previous call (clears the count).
 * @return clockwise = +, counter-clockwise = -
 */
int encoder_get_delta(void);

/** @brief Clear the count only. */
void encoder_reset(void);

/** @brief Release the PCNT unit. */
void encoder_deinit(void);

#ifdef __cplusplus
}
#endif
