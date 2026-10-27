/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * Safe entry point for generated mod code.
 *
 * mod_generated.h is either the disabled stub or the allowlisted output of
 * NEWPROJECT. Only this translation unit includes it, so generated helpers
 * can stay static. The rest of the firmware sees only these three functions.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize generated mod rules (no-op when MODS_ENABLE == 0). */
void mod_runtime_init(void);

/** @brief Evaluate mod rules from the main loop (non-blocking). */
void mod_runtime_tick(uint32_t now);

/**
 * @brief Dispatch an activated UI element (menu item) to generated rules.
 * @return true if a generated rule handled this element.
 */
bool mod_runtime_ui_event(int element_id);

/** @brief Dispatch an activated SETTINGS element to generated rules. */
bool mod_runtime_settings_event(int element_id);

/** @brief True when generated mods are compiled in. */
bool mod_runtime_enabled(void);

#ifdef __cplusplus
}
#endif
