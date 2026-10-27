/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Lua runtime port for ESP32-S3:
 *  - allocator uses PSRAM so scripts never eat internal SRAM
 *  - sandboxed execution with an instruction-count budget (no infinite loops)
 *  - component event dispatch (App Inventor style: when Component.Event)
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lauxlib.h"
#include "lualib.h"

#include "app.h"
#include "ui.h"
#include "ui_runtime.h"
#include "ui_custom.h"
#include "driver/gpio.h"

#include "ui_lua.h"

static const char *TAG = "lua";

#define LUA_MEM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LUA_MAX_INSTRUCTIONS 2000000   /* 약 수십 ms 상한 */

static char s_err[192];
static lua_State *s_app_L;

static void *psram_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    if (nsize == 0) {
        heap_caps_free(ptr);
        return NULL;
    }
    void *p = heap_caps_realloc(ptr, nsize, LUA_MEM_CAPS);
    if (!p) {
        /* PSRAM 부족 시 내부 SRAM 으로 폴백 (마지막 수단) */
        p = heap_caps_realloc(ptr, nsize, MALLOC_CAP_8BIT);
    }
    return p;
}

static void hook_count(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    int *budget = (int *)lua_getextraspace(L);
    if (--(*budget) <= 0) {
        luaL_error(L, "E_BUDGET: instruction budget exceeded");
    }
}

const char *lua_port_error(void)
{
    return s_err[0] ? s_err : "E_UNKNOWN";
}

/* ---------------- C API ---------------- */

static const uc_widget_t *widget_of(lua_State *L, int idx)
{
    lua_Integer uid = luaL_checkinteger(L, idx);
    return (uid > 0) ? ui_runtime_find((uint16_t)uid) : NULL;
}

/* w.text(uid, "TEXT") */
static int l_w_text(lua_State *L)
{
    const uc_widget_t *w = widget_of(L, 1);
    if (!w) return luaL_error(L, "E_ARG: unknown widget uid");
    const char *s = luaL_checkstring(L, 2);
    ui_widget_override_text(w->uid, s);
    return 0;
}

/* w.visible(uid, bool) */
static int l_w_visible(lua_State *L)
{
    const uc_widget_t *w = widget_of(L, 1);
    if (!w) return luaL_error(L, "E_ARG: unknown widget uid");
    ui_widget_override_visible(w->uid, lua_toboolean(L, 2) != 0);
    return 0;
}

/* w.color(uid, colorIndex) */
static int l_w_color(lua_State *L)
{
    const uc_widget_t *w = widget_of(L, 1);
    if (!w) return luaL_error(L, "E_ARG: unknown widget uid");
    ui_widget_override_color(w->uid, (uint8_t)luaL_checkinteger(L, 2));
    return 0;
}

/* w.value(uid) -> 현재 바인딩 값(숫자) */
static int l_w_value(lua_State *L)
{
    const uc_widget_t *w = widget_of(L, 1);
    if (!w) return luaL_error(L, "E_ARG: unknown widget uid");
    lua_pushinteger(L, ui_widget_bind_value(w));
    return 1;
}

/* radio.freq() / radio.mode() */
static int l_radio_freq(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)s_ui.freq_hz);
    return 1;
}

static const char *mode_name(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return "FM";
    case RADIO_MODE_AM:  return "AM";
    default:             return "SSB";
    }
}

static int l_radio_mode(lua_State *L)
{
    lua_pushstring(L, mode_name(s_ui.mode));
    return 1;
}

/* radio.tune(hz) */
static int l_radio_tune(lua_State *L)
{
    lua_Integer hz = luaL_checkinteger(L, 1);
    if (hz <= 0 || hz > 30000000) {
        return luaL_error(L, "E_ARG: bad frequency");
    }
    s_ui.freq_hz = (uint32_t)hz;
    if (radio_tune() != ESP_OK) {
        return luaL_error(L, "E_RADIO: tune failed");
    }
    s_freq_dirty = true;
    return 0;
}

/* radio.seek(up) */
static int l_radio_seek(lua_State *L)
{
    seek_start(lua_toboolean(L, 1) != 0);
    return 0;
}

static int l_radio_stop(lua_State *L)
{
    (void)L;
    seek_stop();
    return 0;
}

/* gpio.write(pin, level) — 화이트리스트 */
static const int s_gpio_allow[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                   13, 14, 15, 16, 17, 18, 21, 38, 39, 40,
                                   41, 42, 45, 46, 47, 48};

static bool gpio_allowed(int pin)
{
    for (size_t i = 0; i < sizeof(s_gpio_allow) / sizeof(s_gpio_allow[0]); ++i) {
        if (s_gpio_allow[i] == pin) {
            return true;
        }
    }
    return false;
}

static int l_gpio_write(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int level = (int)luaL_checkinteger(L, 2);
    if (!gpio_allowed(pin)) {
        return luaL_error(L, "E_PERM: gpio %d is not allowed", pin);
    }
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
    return 0;
}

static int l_gpio_read(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    if (!gpio_allowed(pin)) {
        return luaL_error(L, "E_PERM: gpio %d is not allowed", pin);
    }
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    lua_pushinteger(L, gpio_get_level((gpio_num_t)pin));
    return 1;
}

static int l_log(lua_State *L)
{
    ESP_LOGI(TAG, "app: %s", luaL_checkstring(L, 1));
    return 0;
}

static int l_time_ms(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)now_ms());
    return 1;
}

static void register_api(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_w_text);     lua_setfield(L, -2, "text");
    lua_pushcfunction(L, l_w_visible);  lua_setfield(L, -2, "visible");
    lua_pushcfunction(L, l_w_color);    lua_setfield(L, -2, "color");
    lua_pushcfunction(L, l_w_value);    lua_setfield(L, -2, "value");
    lua_setglobal(L, "w");

    lua_newtable(L);
    lua_pushcfunction(L, l_radio_freq); lua_setfield(L, -2, "freq");
    lua_pushcfunction(L, l_radio_mode); lua_setfield(L, -2, "mode");
    lua_pushcfunction(L, l_radio_tune); lua_setfield(L, -2, "tune");
    lua_pushcfunction(L, l_radio_seek); lua_setfield(L, -2, "seek");
    lua_pushcfunction(L, l_radio_stop); lua_setfield(L, -2, "stop");
    lua_setglobal(L, "radio");

    lua_newtable(L);
    lua_pushcfunction(L, l_gpio_write); lua_setfield(L, -2, "write");
    lua_pushcfunction(L, l_gpio_read);  lua_setfield(L, -2, "read");
    lua_setglobal(L, "gpio");

    lua_pushcfunction(L, l_log);     lua_setglobal(L, "log");
    lua_pushcfunction(L, l_time_ms); lua_setglobal(L, "time_ms");
}

lua_State *lua_port_new(void)
{
    lua_State *L = lua_newstate(psram_alloc, NULL);
    if (!L) {
        ESP_LOGE(TAG, "E_MEM: cannot create Lua state");
        return NULL;
    }
    luaL_openlibs(L);
    register_api(L);
    int *budget = (int *)lua_getextraspace(L);
    *budget = LUA_MAX_INSTRUCTIONS;
    lua_sethook(L, hook_count, LUA_MASKCOUNT, 20000);
    return L;
}

void lua_port_close(lua_State *L)
{
    if (L) {
        lua_close(L);
    }
}

const char *lua_port_exec(lua_State *L, const char *src, size_t len,
                          const char *chunkname)
{
    s_err[0] = '\0';
    if (!L || !src) {
        snprintf(s_err, sizeof(s_err), "E_ARG: null state or source");
        return s_err;
    }
    int *budget = (int *)lua_getextraspace(L);
    *budget = LUA_MAX_INSTRUCTIONS;

    int rc = luaL_loadbufferx(L, src, len, chunkname ? chunkname : "=script",
                              NULL);
    if (rc != LUA_OK) {
        snprintf(s_err, sizeof(s_err), "E_SYNTAX: %s",
                 lua_tostring(L, -1) ? lua_tostring(L, -1) : "load failed");
        lua_pop(L, 1);
        ESP_LOGW(TAG, "%s", s_err);
        return s_err;
    }
    rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        snprintf(s_err, sizeof(s_err), "E_RUNTIME: %s",
                 lua_tostring(L, -1) ? lua_tostring(L, -1) : "call failed");
        lua_pop(L, 1);
        ESP_LOGW(TAG, "%s", s_err);
        return s_err;
    }
    return NULL;
}

/* ---------------- 앱 런타임 ---------------- */

const char *ui_lua_app_start(const char *src, size_t len)
{
    ui_lua_app_stop();
    lua_State *L = lua_port_new();
    if (!L) {
        snprintf(s_err, sizeof(s_err), "E_MEM: cannot create state");
        return s_err;
    }
    const char *err = lua_port_exec(L, src, len, "=app");
    if (err) {
        lua_port_close(L);
        return err;
    }
    s_app_L = L;
    ESP_LOGI(TAG, "app script started");
    return NULL;
}

void ui_lua_app_stop(void)
{
    if (s_app_L) {
        lua_port_close(s_app_L);
        s_app_L = NULL;
    }
}

bool ui_lua_app_running(void)
{
    return s_app_L != NULL;
}

/* events 문자열에서 "event=handler" 를 찾는다 */
static const char *find_handler(const char *events, const char *event,
                                char *out, size_t n)
{
    if (!events || !event) {
        return NULL;
    }
    size_t el = strlen(event);
    const char *p = events;
    while (*p) {
        if (strncmp(p, event, el) == 0 && p[el] == '=') {
            const char *v = p + el + 1;
            const char *end = strchr(v, ',');
            size_t len = end ? (size_t)(end - v) : strlen(v);
            if (len == 0 || len >= n) {
                return NULL;
            }
            memcpy(out, v, len);
            out[len] = '\0';
            return out;
        }
        p = strchr(p, ',');
        if (!p) {
            break;
        }
        ++p;
    }
    return NULL;
}

bool ui_lua_dispatch(uint16_t uid, const char *event, const char *arg)
{
    if (!s_app_L || !event) {
        return false;
    }
    const uc_widget_t *w = ui_runtime_find(uid);
    if (!w) {
        snprintf(s_err, sizeof(s_err), "E_ARG: widget %u not found", uid);
        return false;
    }
    char fn[64];
    if (!find_handler(w->events, event, fn, sizeof(fn))) {
        return false;
    }

    lua_State *L = s_app_L;
    int *budget = (int *)lua_getextraspace(L);
    *budget = LUA_MAX_INSTRUCTIONS;

    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        snprintf(s_err, sizeof(s_err), "E_HANDLER: %s.%s -> %s not found",
                 w->text ? w->text : "widget", event, fn);
        ESP_LOGW(TAG, "%s", s_err);
        return false;
    }
    lua_pushinteger(L, uid);
    lua_pushstring(L, arg ? arg : "");
    int rc = lua_pcall(L, 2, 0, 0);
    if (rc != LUA_OK) {
        snprintf(s_err, sizeof(s_err), "E_RUNTIME: %s.%s -> %s",
                 w->text ? w->text : "widget", event,
                 lua_tostring(L, -1) ? lua_tostring(L, -1) : "error");
        lua_pop(L, 1);
        ESP_LOGW(TAG, "%s", s_err);
        return false;
    }
    return true;
}

bool ui_lua_call_hook(const char *name, int iarg, const char *sarg)
{
    if (!s_app_L || !name) {
        return false;
    }
    lua_State *L = s_app_L;
    int *budget = (int *)lua_getextraspace(L);
    *budget = LUA_MAX_INSTRUCTIONS;

    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pushinteger(L, iarg);
    lua_pushstring(L, sarg ? sarg : "");
    int rc = lua_pcall(L, 2, 0, 0);
    if (rc != LUA_OK) {
        snprintf(s_err, sizeof(s_err), "E_RUNTIME: %s -> %s", name,
                 lua_tostring(L, -1) ? lua_tostring(L, -1) : "error");
        lua_pop(L, 1);
        ESP_LOGW(TAG, "%s", s_err);
        return false;
    }
    return true;
}
