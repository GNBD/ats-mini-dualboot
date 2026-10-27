/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * EXLU 앱 런타임: 부팅 시 선택 화면, 앱 실행/종료.
 *
 * 오류는 숨기지 않는다: 실패 시 E_xxx 원인 문자열을 화면/로그에 표시.
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "app.h"
#include "ui.h"
#include "ui_runtime.h"
#include "ui_lua.h"
#include "ui_exlu.h"
#include "ui_exlu_app.h"
#include "encoder.h"
#include "display.h"
#include "board.h"

/* 공용 드로잉 헬퍼 (ui_core.c) */
void panel(int x, int y, int w, int h, int r, uint16_t fill, uint16_t stroke);

static const char *TAG = "exlu_app";

#define BOOT_LIST_MAX 8
#define BOOT_IDLE_MS  3000

static char s_current[EXLU_NAME_MAX];
static char s_boot_id[EXLU_NAME_MAX];

static void boot_path(char *out, size_t n)
{
    snprintf(out, n, "/littlefs/ui/boot_app.txt");
}

void exlu_set_boot_id(const char *id)
{
    char path[64];
    boot_path(path, sizeof(path));
    if (!id || !id[0]) {
        remove(path);
        s_boot_id[0] = '\0';
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGW(TAG, "E_IO: cannot write boot id");
        return;
    }
    fwrite(id, 1, strlen(id), f);
    fclose(f);
    snprintf(s_boot_id, sizeof(s_boot_id), "%s", id);
}

const char *exlu_get_boot_id(void)
{
    if (s_boot_id[0]) {
        return s_boot_id;
    }
    char path[64];
    boot_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    size_t n = fread(s_boot_id, 1, sizeof(s_boot_id) - 1, f);
    fclose(f);
    s_boot_id[n] = '\0';
    return s_boot_id[0] ? s_boot_id : NULL;
}

int exlu_app_list(exlu_manifest_t *out, int max)
{
    exlu_manifest_t all[16];
    int n = exlu_list(all, 16);
    int used = 0;
    for (int i = 0; i < n && used < max; ++i) {
        if (all[i].kind == EXLU_KIND_APP) {
            out[used++] = all[i];
        }
    }
    return used;
}

/* ---------------- 앱 실행 ---------------- */

const char *exlu_app_launch(const char *id)
{
    if (!id || !id[0]) {
        return "E_ARG: empty id";
    }
    exlu_t e;
    exlu_err_t err = exlu_load(id, &e);
    if (err != EXLU_OK) {
        ESP_LOGW(TAG, "launch %s: %s", id, exlu_strerror(err));
        return exlu_strerror(err);
    }
    const char *res = NULL;
    if (e.manifest.kind != EXLU_KIND_APP) {
        res = "E_KIND: not an app";
    } else if (e.manifest.type == EXLU_TYPE_LAYOUT) {
        if (!e.layout) {
            res = "E_NO_LAYOUT: layout missing";
        } else if (!ui_runtime_save(id, e.layout, e.layout_len, true)) {
            res = "E_LAYOUT: rejected by runtime";
        } else if (!ui_runtime_activate(id)) {
            res = "E_LAYOUT: activate failed";
        }
    }

    if (!res) {
        ui_lua_app_stop();
        if (e.script_len) {
            res = ui_lua_app_start(e.script, e.script_len);
        }
    }
    if (!res) {
        snprintf(s_current, sizeof(s_current), "%s", id);
        s_theme = UI_THEME_CUSTOM;
        ui_runtime_apply_theme();
        mark_change();
        ESP_LOGI(TAG, "app started: %s", id);
    } else {
        ESP_LOGW(TAG, "launch %s failed: %s", id, res);
    }
    exlu_free(&e);
    return res;
}

void exlu_app_stop(void)
{
    if (s_current[0]) {
        ESP_LOGI(TAG, "app stopped: %s", s_current);
    }
    s_current[0] = '\0';
    ui_lua_app_stop();
    ui_runtime_disable();
    s_theme = 0;
    ui_theme_set(0);
}

bool exlu_app_running(void)
{
    return s_current[0] != '\0';
}

const char *exlu_app_current(void)
{
    return s_current;
}

/* ---------------- 부팅 선택 화면 ---------------- */

static void draw_boot_list(const exlu_manifest_t *m, int n, int cursor,
                           const char *status)
{
    /* index 0 = None, 1..n = EXLU 항목 */

    display_fill_screen(UI_BG);
    panel(40, 22, 240, 126, 12, UI_PANEL, UI_LINE);
    display_draw_text(UI_W / 2, 30, "Select Extended", DISPLAY_FONT_SMALL,
                      UI_ACC_HI, UI_PANEL, DISPLAY_DATUM_TC);

    for (int i = 0; i <= n && i < 6; ++i) {
        int y = 48 + i * 14;
        bool cur = (i == cursor);
        if (cur) {
            display_fill_round_rect(50, (int16_t)(y - 2), 220, 14, 6, UI_HSEL);
        }
        if (i == 0) {
            display_draw_text(56, (int16_t)y, "None", DISPLAY_FONT_TINY,
                              cur ? UI_ACC_HI : UI_FG,
                              cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TL);
            display_draw_text(264, (int16_t)y, "DEFAULT", DISPLAY_FONT_TINY,
                              UI_MUTED, cur ? UI_HSEL : UI_PANEL,
                              DISPLAY_DATUM_TR);
        } else {
            display_draw_text(56, (int16_t)y, m[i - 1].name, DISPLAY_FONT_TINY,
                              cur ? UI_ACC_HI : UI_FG,
                              cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TL);
            display_draw_text(264, (int16_t)y,
                              m[i - 1].kind == EXLU_KIND_THEME ? "THEME" : "APP",
                              DISPLAY_FONT_TINY, UI_MUTED,
                              cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TR);
        }
    }
    display_draw_text(UI_W / 2, 136, "TURN: MOVE   CLICK: START",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TC);
    if (status && status[0]) {
        display_draw_text(UI_W / 2, 152, status, DISPLAY_FONT_TINY, UI_WARN,
                          UI_BG, DISPLAY_DATUM_TC);
    }
    display_flush();
}

bool exlu_boot_select(void)
{
    exlu_manifest_t m[BOOT_LIST_MAX];
    int n = 0;
    {
        exlu_manifest_t all[16];
        int an = exlu_list(all, 16);
        for (int i = 0; i < an && n < BOOT_LIST_MAX; ++i) {
            if (all[i].kind == EXLU_KIND_APP ||
                all[i].kind == EXLU_KIND_THEME) {
                m[n++] = all[i];
            }
        }
    }
    if (n == 0) {
        return false;
    }

    /* 마지막 부팅 선택 위치에서 시작 */
    int cursor = 0;
    const char *boot = exlu_get_boot_id();
    if (boot) {
        for (int i = 0; i < n; ++i) {
            if (strcmp(m[i].id, boot) == 0) {
                cursor = i;
                break;
            }
        }
    }

    char status[48] = "";
    draw_boot_list(m, n, cursor, status);

    int64_t t0 = esp_timer_get_time();
    int last_delta = 0;
    while (1) {
        int64_t now = esp_timer_get_time();
        if ((now - t0) / 1000 > BOOT_IDLE_MS) {
            break;
        }
        int d = encoder_get_delta();
        if (d != 0) {
            cursor = (cursor + (d > 0 ? 1 : -1) + (n + 1)) % (n + 1);
            last_delta = d;
            draw_boot_list(m, n, cursor, status);
            t0 = now;
        }
        bool pressed = gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0;
        if (pressed) {
            if (cursor == 0) {
                exlu_set_boot_id("");
                ESP_LOGI(TAG, "boot selection: none");
                return false;
            }
            const char *err = NULL;
            if (m[cursor - 1].kind == EXLU_KIND_THEME) {
                exlu_t e;
                if (exlu_load(m[cursor - 1].id, &e) == EXLU_OK) {
                    if (exlu_apply(&e) != EXLU_OK) {
                        err = "E_LAYOUT: theme apply failed";
                    }
                    exlu_free(&e);
                } else {
                    err = "E_OPEN: cannot load theme";
                }
            } else {
                err = exlu_app_launch(m[cursor - 1].id);
            }
            if (err) {
                snprintf(status, sizeof(status), "%s", err);
                draw_boot_list(m, n, cursor, status);
                t0 = esp_timer_get_time();
                vTaskDelay(pdMS_TO_TICKS(1200));
                continue;
            }
            exlu_set_boot_id(m[cursor - 1].id);
            ESP_LOGI(TAG, "boot selected: %s", m[cursor - 1].id);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    (void)last_delta;
    /* 타임아웃: None 과 동일하게 아무것도 적용하지 않는다 */
    return false;
}
