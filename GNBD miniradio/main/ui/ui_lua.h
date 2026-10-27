/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Lua 앱 런타임: 위젯 이벤트 디스패치 + 안전한 C API 노출.
 *
 * This source file is licensed under the MIT License.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lua.h"

/* Lua 힙을 PSRAM 에 두어 내부 SRAM 을 보호한다 */
lua_State *lua_port_new(void);
void lua_port_close(lua_State *L);

/* 제한 시간/명령 수를 걸고 청크를 실행한다. 실패 시 오류 문자열 반환. */
const char *lua_port_exec(lua_State *L, const char *src, size_t len,
                          const char *chunkname);

/* 마지막 오류 메시지 (실행 실패 시 원인 표기용) */
const char *lua_port_error(void);

/* ---------------- 앱 런타임 ---------------- */

/* 앱 스크립트 시작/종료. 실패 시 원인 문자열 반환. */
const char *ui_lua_app_start(const char *src, size_t len);
void ui_lua_app_stop(void);
bool ui_lua_app_running(void);

/* 위젯 이벤트 디스패치: events 문자열의 핸들러 함수를 호출한다.
   반환값 false = 핸들러 없음/오류 (원인은 lua_port_error). */
bool ui_lua_dispatch(uint16_t uid, const char *event, const char *arg);

/* 전역 스크립트의 선택적 훅 호출 (on_tick/on_key 등) */
bool ui_lua_call_hook(const char *name, int iarg, const char *sarg);
