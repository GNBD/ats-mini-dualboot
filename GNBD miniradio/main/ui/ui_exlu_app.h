/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * EXLU 앱 런타임: 부팅 시 선택, 앱 실행/종료, 메뉴 노출.
 *
 * This source file is licensed under the MIT License.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui_exlu.h"

/* EXTENDED(EXLU) 기능 스위치: 0 = 숨김/비활성 */
#ifndef EXLU_ENABLE
#define EXLU_ENABLE 0
#endif

/* 메뉴 id 대역 (RM_* 와 충돌하지 않게) */
#define EXLU_MENU_BASE 0x5000

/* 앱(kind=app) 목록만 반환 */
int  exlu_app_list(exlu_manifest_t *out, int max);

/* 앱 실행: 레이아웃 활성화 + 스크립트 시작. 실패 시 원인 문자열 반환 */
const char *exlu_app_launch(const char *id);
void exlu_app_stop(void);
bool exlu_app_running(void);
const char *exlu_app_current(void);

/* 부팅 시 EXLU 선택 화면. 선택/자동 적용 후 true */
bool exlu_boot_select(void);

/* 설정에서 부팅 시 자동 실행할 항목 저장/조회 */
void exlu_set_boot_id(const char *id);
const char *exlu_get_boot_id(void);
