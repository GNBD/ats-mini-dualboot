/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 오디오 볼륨/음소거 관리. SI4732 RX_VOLUME 프로퍼티로 구현.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "audio.h"

static const char *TAG = "audio";

static si4732_t *s_radio;
static uint8_t s_volume = 40;
static bool s_muted = false;

esp_err_t audio_init(si4732_t *radio)
{
    ESP_RETURN_ON_FALSE(radio, ESP_ERR_INVALID_ARG, TAG, "null radio");
    /* 칩은 아직 power-down 상태 — 여기서는 핸들 바인딩만 수행하고,
       실제 볼륨/뮤트 프로퍼티는 radio_apply()가 power_up 직후 보낸다. */
    s_radio = radio;
    ESP_LOGI(TAG, "init ok (volume=%u)", s_volume);
    return ESP_OK;
}

esp_err_t audio_set_volume(uint8_t volume)
{
    if (volume > AUDIO_VOLUME_MAX) {
        volume = AUDIO_VOLUME_MAX;
    }
    if (!s_radio) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = si4732_set_volume(s_radio, volume);
    if (err == ESP_OK) {
        s_volume = volume;
    }
    return err;
}

esp_err_t audio_change_volume(int8_t delta)
{
    int16_t v = (int16_t)s_volume + delta;
    if (v < 0) {
        v = 0;
    }
    if (v > AUDIO_VOLUME_MAX) {
        v = AUDIO_VOLUME_MAX;
    }
    return audio_set_volume((uint8_t)v);
}

uint8_t audio_get_volume(void)
{
    return s_volume;
}

esp_err_t audio_set_mute(bool mute)
{
    if (!s_radio) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = si4732_set_mute(s_radio, mute);
    if (err == ESP_OK) {
        s_muted = mute;
    }
    return err;
}

bool audio_is_muted(void)
{
    return s_muted;
}
