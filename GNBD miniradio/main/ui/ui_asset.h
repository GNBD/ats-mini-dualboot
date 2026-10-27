/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * UI asset layer: individually addressable screen elements.
 * Each asset owns drawing of one visual element so that partial
 * updates never redraw the whole screen (and never overlap).
 *
 * This source file is licensed under the MIT License.
 */
#pragma once

#include <stdint.h>
#include "ui.h"

/* 비트 마스크: 필요한 요소만 골라 그릴 수 있다 */
enum {
    UI_ASSET_HEADER = 1u << 0,   /* 상단바: 시계/상태/알림 */
    UI_ASSET_BAND   = 1u << 1,   /* 밴드/모드 메타 */
    UI_ASSET_STATUS = 1u << 2,   /* VOL / BRT */
    UI_ASSET_BADGES = 1u << 3,   /* P / A 배지 */
    UI_ASSET_FREQ   = 1u << 4,   /* 주파수 + 단위 */
    UI_ASSET_SIG    = 1u << 5,   /* 시그널 패널 */
    UI_ASSET_SLEEP  = 1u << 6,   /* sleep 남은 시간 */
    UI_ASSET_ALL    = 0x7Fu,
};

/* 요소 단위 그리기 (자기 영역만 지우고 다시 그린다) */
void ui_asset_header(const ui_state_t *s);
void ui_asset_band(const ui_state_t *s);
void ui_asset_status(const ui_state_t *s);
void ui_asset_badges(const ui_state_t *s);
void ui_asset_freq(const ui_state_t *s);
void ui_asset_sig(const ui_state_t *s);
void ui_asset_sleep(const ui_state_t *s);

/* 마스크에 해당하는 요소만 그리기 */
void ui_asset_draw(const ui_state_t *s, uint32_t mask);
