/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 데이터 기반 메뉴 정의. NEWPROJECT가 menu_data.c를 생성해 덮어쓴다.
 */
#pragma once

#include <stdint.h>

#include "app.h"

typedef struct {
    uint8_t     id;       /* RM_* (동작 식별자, 코어에서 고정) */
    const char *label;    /* 표시 이름 */
    uint8_t     visible;  /* 0 = 숨김 */
} menu_entry_t;

/** @brief 표시할 메뉴 항목 (순서 = 표시 순서) */
extern const menu_entry_t *g_menu_items;
extern const int g_menu_count;

/** @brief 표시할 설정 항목 (순서 = 표시 순서) */
extern const menu_entry_t *g_set_items;
extern const int g_set_count;
