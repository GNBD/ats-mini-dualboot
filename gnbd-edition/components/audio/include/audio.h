/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 볼륨 / 음소거 상태 관리 (AVC·대역폭 제어는 Phase 3).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "si4732.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_VOLUME_MAX 63

/** @brief 드라이버 핸들 등록 + 초기 볼륨 적용 */
esp_err_t audio_init(si4732_t *radio);

/** @brief 절대 볼륨 설정 (0–63, 칩에 즉시 적용) */
esp_err_t audio_set_volume(uint8_t volume);

/** @brief 상대 볼륨 조정 (양수 증가/음수 감소, 클램프) */
esp_err_t audio_change_volume(int8_t delta);

uint8_t audio_get_volume(void);

/** @brief 소프트 뮤트 (RX_HARD_MUTE) */
esp_err_t audio_set_mute(bool mute);

bool audio_is_muted(void);

#ifdef __cplusplus
}
#endif
