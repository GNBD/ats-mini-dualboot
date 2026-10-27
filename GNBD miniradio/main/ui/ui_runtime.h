/*
 * SPDX-License-Identifier: MIT
 * ui_runtime.h - runtime UI layouts stored in LittleFS.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "ui_custom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_RUNTIME_MAX_WIDGETS 128
#define UI_RUNTIME_MAX_FILE    16384
#define UI_RUNTIME_NAME_MAX    24

bool ui_runtime_init(bool want_custom);
bool ui_runtime_enabled(void);
void ui_runtime_disable(void);
int  ui_runtime_count(void);
const uc_widget_t *ui_runtime_widgets(void);
const char *ui_runtime_active_name(void);

bool ui_runtime_valid_name(const char *name);
int  ui_runtime_list(char names[][UI_RUNTIME_NAME_MAX + 1], int max);

bool ui_runtime_save(const char *name, const char *text, size_t len, bool overwrite);
bool ui_runtime_load(const char *name);
bool ui_runtime_activate(const char *name);
bool ui_runtime_activate_active(void);
bool ui_runtime_delete(const char *name);
bool ui_runtime_has(const char *name);

/* menu/settings panel from the runtime layout (theme mode) */
bool ui_runtime_panel(bool settings, int *x, int *y, int *w, int *h, int *orient);

/* uid 로 위젯 찾기 (Lua 이벤트 바인딩용) */
const uc_widget_t *ui_runtime_find(uint16_t uid);
void ui_runtime_apply_theme(void);

/* raw file access for the editor (ext = "layout" or "mrpack") */
bool ui_runtime_read(const char *name, const char *ext, char **out, size_t *len);
bool ui_runtime_write(const char *name, const char *ext, const char *data, size_t len);

#ifdef __cplusplus
}
#endif
