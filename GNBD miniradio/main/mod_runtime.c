/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * Generated-mod runtime boundary.
 *
 * Design constraints:
 *  - mod code never runs before the radio/UI are initialized
 *  - evaluation is non-blocking and lives in the existing main loop
 *  - MODS_ENABLE == 0 keeps the baseline firmware byte-for-byte independent
 *    of the mod generator
 */
#include "mod_runtime.h"
#include "mod_generated.h"
#include "esp_log.h"

static const char *TAG = "mod";

void mod_runtime_init(void)
{
#if MODS_ENABLE
    mod_user_init();
    ESP_LOGI(TAG, "mods enabled");
#else
    ESP_LOGI(TAG, "mods disabled");
#endif
}

void mod_runtime_tick(uint32_t now)
{
#if MODS_ENABLE
    mod_user_tick(now);
#else
    (void)now;
#endif
}

bool mod_runtime_enabled(void)
{
    return MODS_ENABLE != 0;
}

bool mod_runtime_ui_event(int element_id)
{
#if MODS_ENABLE
    return mod_user_menu_event(element_id);
#else
    (void)element_id;
    return false;
#endif
}

bool mod_runtime_settings_event(int element_id)
{
#if MODS_ENABLE
    return mod_user_settings_event(element_id);
#else
    (void)element_id;
    return false;
#endif
}
