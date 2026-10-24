/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Weather app interfaces.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void weather_open(void);
void weather_poll(void);
void weather_redraw(void);
void weather_location_changed(void);
void weather_set_mode(int mode);

#ifdef __cplusplus
}
#endif
