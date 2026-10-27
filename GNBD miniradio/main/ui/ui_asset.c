/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * UI asset layer: mask-driven composition of individual elements.
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include "display.h"
#include "ui.h"
#include "ui_int.h"
#include "ui_asset.h"

void ui_asset_draw(const ui_state_t *s, uint32_t mask)
{
    if (mask & UI_ASSET_HEADER) {
        ui_asset_header(s);
    }
    if (mask & UI_ASSET_BAND) {
        ui_asset_band(s);
    }
    if (mask & UI_ASSET_BADGES) {
        ui_asset_badges(s);
    }
    if (mask & UI_ASSET_STATUS) {
        ui_asset_status(s);
    }
    if (mask & UI_ASSET_FREQ) {
        ui_asset_freq(s);
    }
    if (mask & UI_ASSET_SIG) {
        ui_asset_sig(s);
    }
    if (mask & UI_ASSET_SLEEP) {
        ui_asset_sleep(s);
    }
}
