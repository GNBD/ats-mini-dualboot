/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Board power, timing and encoder acceleration helpers.
 */
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "board.h"
#include "app.h"

static const char *TAG = "board";

/* ---------------------------------------------------------- 전원 제어 */

esp_err_t board_power_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_AUDIO_MUTE_GPIO) |
                        (1ULL << BOARD_AMP_EN_GPIO) |
                        (1ULL << BOARD_RADIO_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "power gpio");
    gpio_set_level((gpio_num_t)BOARD_AUDIO_MUTE_GPIO, 1);
    gpio_set_level((gpio_num_t)BOARD_AMP_EN_GPIO, 0);
    gpio_set_level((gpio_num_t)BOARD_RADIO_EN_GPIO, 0);
    return ESP_OK;
}

void board_amp(bool on)
{
    if (on) {
        gpio_set_level((gpio_num_t)BOARD_AUDIO_MUTE_GPIO, 1);
        vTaskDelay(TICK_MS(10));
        gpio_set_level((gpio_num_t)BOARD_AMP_EN_GPIO, 1);
        vTaskDelay(TICK_MS(20));
        gpio_set_level((gpio_num_t)BOARD_AUDIO_MUTE_GPIO, 0);
    } else {
        gpio_set_level((gpio_num_t)BOARD_AUDIO_MUTE_GPIO, 1);
        gpio_set_level((gpio_num_t)BOARD_AMP_EN_GPIO, 0);
    }
}

/* ------------------------------------------------------------- 타이밍 */

uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

int wrap(int v, int n)
{
    return ((v % n) + n) % n;
}

int16_t accelerate_encoder(int8_t dir)
{
    static const uint32_t speed_thresholds[5] = {260, 55, 38, 26, 16};
    static const uint16_t accel_factors[5]    = {1, 2, 4, 8, 16};
    static uint32_t last_time;
    static uint32_t last_speed = 260;
    static uint16_t last_factor = 1;
    static int8_t   last_dir;

    uint32_t now = now_ms();
    last_speed = ((now - last_time) * 7 + last_speed * 3) / 10;

    if (last_speed > speed_thresholds[0] || last_dir != dir) {
        last_speed = speed_thresholds[0];
        last_factor = accel_factors[0];
    } else {
        for (int i = 4; i >= 0; --i) {
            if (last_speed <= speed_thresholds[i] &&
                last_factor < accel_factors[i]) {
                last_factor = accel_factors[i];
                break;
            }
        }
    }
    last_time = now;
    last_dir = dir;
    return (int16_t)(dir * (int16_t)last_factor);
}

int accel_counts(int detents)
{
    if (detents == 0) {
        return 0;
    }

    /* ??? ? ???? ??? ??. ?? ??? 1?, ?? ??? ?? 16?. */
    static uint32_t last_ms;
    static int last_dir;
    uint32_t now = now_ms();
    int ad = (detents < 0) ? -detents : detents;
    uint32_t dt = now - last_ms;
    last_ms = now;

    uint32_t iv = (dt > 0 && ad > 0) ? (dt / (uint32_t)ad) : 999;
    int dir = (detents > 0) ? 1 : -1;
    int factor = 1;
    if (iv <= 20) {
        factor = 16;
    } else if (iv <= 35) {
        factor = 8;
    } else if (iv <= 55) {
        factor = 4;
    } else if (iv <= 90) {
        factor = 2;
    }
    if (dir != last_dir) { factor = 1; last_dir = dir; }
    return detents * factor;
}
