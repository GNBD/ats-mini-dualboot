/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * HMI 분할 공용 인터페이스.
 * main.c 를 기능별 파일로 나누면서 공유되는 전역변수/함수 선언만 모았다.
 * (app.h 는 모듈 간 인터페이스, 이 헤더는 HMI 내부 공유 선언)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 공유 전역변수 (정의: main.c) ---- */
extern const char *TAG;
extern bool        s_tune_save_pending;
extern uint32_t    s_tune_save_ms;
extern int         s_quick_cursor;
extern int64_t     s_clock_saved_utc;
extern bool        s_auto_bright;
extern int         s_cpu_saved;
extern bool        s_digit_swallow;
extern uint32_t    s_input_lock;
extern uint32_t    s_setup_reboot_ms;
extern int         s_kp_mode;

/* ---- 홈 화면 앱 목록 (정의: main.c) ---- */
extern const ui_app_t g_apps[3];
#define APP_COUNT ((int)(sizeof(g_apps) / sizeof(g_apps[0])))

/* ---- 공유 매크로 ---- */
#define ST_BRIGHT_MENU   0x7E
#define ST_BRIGHT_LEVEL  0x7F

enum { KP_MODE_WIFI = 0, KP_MODE_WEATHER, KP_MODE_SPLASH_CALL,
       KP_MODE_SPLASH_WEL };

/* ---- HMI 내부 함수 (분할 파일 간 호출) ---- */
void brightness_apply(void);
int64_t clock_utc_now(void);
void clock_save_now(void);
void app_open_radio(void);
void app_open_clock(void);
void app_open_weather(void);
void close_volume(void);
void enter_home(void);
int  menu_id_at(int cursor);
void open_volume(bool from_menu);
void open_weather_keypad(void);
int  settings_id_at(int cursor);
void power_save_set(bool on);
int  app_find(const char *id);
void launch_app(int idx);
int  quick_build(uint8_t *out);
void quick_exec(int qa);
int  quick_pop(uint16_t m);
void setup_mode(void);

#ifdef __cplusplus
}
#endif
