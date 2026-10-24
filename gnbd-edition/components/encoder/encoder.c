/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * EC11 엔코더 PCNT 드라이버.
 * ESP-IDF rotary_encoder 예제의 채널 액션 설정을 그대로 채용 (4X 디코딩:
 * 디텐트당 ±4). 원본 ats-mini(Ben Buxton full-step)와 동일한 감도를 위해
 * get_delta에서 디텐트당 ±1로 정규화한다.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"

#include "encoder.h"

static const char *TAG = "encoder";

#define ENC_HIGH_LIMIT  32767
#define ENC_LOW_LIMIT  (-32768)
#define ENC_GLITCH_NS   1000  /* EC11 바운스 대응 (1µs) */
#define ENC_EDGES_PER_DETENT 4   /* PCNT 4X → 디텐트당 4엣지 */

/* 방향 반전 (원본 Rotary(B,A) 기준 CW=+1과 다를 때 1로 전환) */
#ifndef ENC_INVERT
#define ENC_INVERT 0
#endif

static pcnt_unit_handle_t s_unit;
static int s_remain;   /* 디텐트 미만 잔여 엣지 누적 */

esp_err_t encoder_init(gpio_num_t pin_a, gpio_num_t pin_b)
{
    ESP_RETURN_ON_FALSE(s_unit == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    pcnt_unit_config_t unit_cfg = {
        .high_limit = ENC_HIGH_LIMIT,
        .low_limit = ENC_LOW_LIMIT,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_unit), TAG, "pcnt unit");

    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = ENC_GLITCH_NS,
    };
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_unit, &filter_cfg), TAG, "glitch filter");

    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num = pin_a,
        .level_gpio_num = pin_b,
    };
    pcnt_channel_handle_t chan_a;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &chan_a_cfg, &chan_a), TAG, "chan a");

    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num = pin_b,
        .level_gpio_num = pin_a,
    };
    pcnt_channel_handle_t chan_b;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &chan_b_cfg, &chan_b), TAG, "chan b");

    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_a,
                            PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                            PCNT_CHANNEL_EDGE_ACTION_INCREASE), TAG, "chan a edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_a,
                            PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                            PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "chan a level");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(chan_b,
                            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                            PCNT_CHANNEL_EDGE_ACTION_DECREASE), TAG, "chan b edge");
    ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(chan_b,
                            PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                            PCNT_CHANNEL_LEVEL_ACTION_INVERSE), TAG, "chan b level");

    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_unit), TAG, "enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_unit), TAG, "clear");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(s_unit), TAG, "start");

    ESP_LOGI(TAG, "EC11 init (A=%d, B=%d)", pin_a, pin_b);
    return ESP_OK;
}

int encoder_get_delta(void)
{
    if (!s_unit) {
        return 0;
    }
    int count = 0;
    if (pcnt_unit_get_count(s_unit, &count) != ESP_OK) {
        return 0;
    }
    pcnt_unit_clear_count(s_unit);

    /* 4X ?�� �� -> ?�텐??��1) ?�규??(?�머지???�음 read?� ?�월) */
    /* direction change: drop residual to avoid a lost detent */
    if (count != 0 &&
        ((s_remain > 0 && count < 0) || (s_remain < 0 && count > 0))) {
        s_remain = 0;
    }
    s_remain += count;
    int detent = s_remain / ENC_EDGES_PER_DETENT;
    s_remain -= detent * ENC_EDGES_PER_DETENT;
#if ENC_INVERT
    detent = -detent;
#endif
    return detent;
}

void encoder_reset(void)
{
    if (s_unit) {
        pcnt_unit_clear_count(s_unit);
    }
    s_remain = 0;
}

void encoder_deinit(void)
{
    if (s_unit) {
        pcnt_unit_stop(s_unit);
        pcnt_del_unit(s_unit);
        s_unit = NULL;
    }
}
