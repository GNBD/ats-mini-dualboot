/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void decode_start(void);
void decode_stop(void);
bool decode_poll(uint32_t now);
void decode_cycle_mode(void);
void decode_cycle_gpio(int dir);

bool        decode_running(void);
const char *decode_mode_str(void);
int         decode_gpio(void);
int         decode_level(void);
const char *decode_text(void);

#ifdef __cplusplus
}
#endif
