/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 기본 설정(SETTINGS) 테이블. NEWPROJECT가 settings_data.c를 생성해 대체한다.
 */
#include "menu_data.h"

static const menu_entry_t s_set[] = {
    { ST_BACK,       "< BACK",     1 },
    { ST_BRIGHT,     "BRIGHT",     1 },
    { ST_SCREEN,     "SCREEN",     1 },
    { ST_SLEEP,      "SLEEP",      1 },
    { ST_POWERSAVE,  "POWER SAVE", 1 },
    { ST_UNIT,       "UNIT",       1 },
    { ST_THEME,      "THEME",      1 },
    { ST_TIME,       "TIME",       1 },
    { ST_UTC,        "UTC",        1 },
    { ST_BATT,       "BATT",       1 },
    { ST_BATT_CAL,   "BATT CAL",   1 },
    { ST_CPU,        "CPU",        1 },
    { ST_WIFI,       "WI-FI",      1 },
    { ST_EIBI,       "EIBI",       1 },
    { ST_QUICKSET,   "QUICK SET",  1 },
    { ST_SIGDBG,     "SIG spd",    1 },
    { ST_SIGVIEW,    "BOTTOM",     1 },
    { ST_MOD_A,      "MOD A",      0 },
    { ST_MOD_B,      "MOD B",      0 },
    { ST_MOD_C,      "MOD C",      0 },
    { ST_MOD_D,      "MOD D",      0 },
    { ST_MOD_E,      "MOD E",      0 },
    { ST_MOD_F,      "MOD F",      0 },
    { ST_MOD_G,      "MOD G",      0 },
    { ST_MOD_H,      "MOD H",      0 },
    { ST_ABOUT,      "ABOUT",      1 },
};

const menu_entry_t *g_set_items = s_set;
const int g_set_count = (int)(sizeof(s_set) / sizeof(s_set[0]));
