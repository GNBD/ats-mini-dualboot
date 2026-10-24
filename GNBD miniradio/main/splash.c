/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * 부팅 스플래시 (미니멀 워드마크, 320x170).
 * 좌상단 빌드번호(YYMMDDHH), 우하단 버전, 중앙 워드마크.
 * 라디오 초기화가 끝나면 page_radio()가 화면을 덮어쓴다.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "board.h"
#include "encoder.h"
#include "storage.h"

#include "display.h"
#include "ui.h"
#include "app.h"

#include "splash.h"

void splash_show(void)
{
    display_fill_screen(UI_BG);

    char gb[48];
    snprintf(gb, sizeof(gb), "GNBD v%s", app_version());
    display_draw_text(UI_W / 2, 136, gb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 150, app_build_no(), DISPLAY_FONT_TINY,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TC);

    display_flush();
}

static bool splash_btn_down(void)
{
    return gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0;
}

static void splash_recovery_draw(int sel)
{
    display_fill_screen(UI_BG);
    display_fill_rect(0, 0, UI_W, 2, UI_WARN);

    display_draw_text(UI_W / 2, 24, "RECOVERY", DISPLAY_FONT_LARGE, UI_WARN,
                      UI_BG, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 58, "Factory reset erases all settings",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);

    static const char *const items[2] = {"CONTINUE BOOT", "FACTORY RESET"};
    for (int i = 0; i < 2; ++i) {
        int y = 84 + i * 34;
        bool cur = (i == sel);
        uint16_t col = (i == 1) ? UI_WARN : UI_ACC_HI;
        if (cur) {
            display_fill_round_rect(40, (int16_t)y, 240, 28, 10, UI_HSEL);
        }
        display_draw_round_rect(40, (int16_t)y, 240, 28, 10,
                                cur ? col : UI_LINE);
        display_draw_text(UI_W / 2, (int16_t)(y + 7), items[i],
                          DISPLAY_FONT_SMALL, cur ? UI_FG : UI_MUTED,
                          cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TC);
    }

    display_draw_text(UI_W / 2, UI_H - 14, "turn: select   press: ok",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    display_flush();
}

static void splash_confirm_draw(int sel)
{
    display_fill_screen(UI_BG);
    display_fill_rect(0, 0, UI_W, 2, UI_WARN);

    display_draw_text(UI_W / 2, 26, "FACTORY RESET", DISPLAY_FONT_LARGE,
                      UI_WARN, UI_BG, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 60, "Erase all settings (NVS only)?",
                      DISPLAY_FONT_SMALL, UI_FG, UI_BG, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 82, "Presets, ATS, Wi-Fi, configs",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);

    static const char *const items[2] = {"CANCEL", "RESET"};
    for (int i = 0; i < 2; ++i) {
        int x = 30 + i * 140;
        int y = 112;
        bool cur = (i == sel);
        uint16_t col = (i == 1) ? UI_WARN : UI_ACC_HI;
        if (cur) {
            display_fill_round_rect((int16_t)x, (int16_t)y, 120, 30, 10,
                                    UI_HSEL);
        }
        display_draw_round_rect((int16_t)x, (int16_t)y, 120, 30, 10,
                                cur ? col : UI_LINE);
        display_draw_text((int16_t)(x + 60), (int16_t)(y + 8), items[i],
                          DISPLAY_FONT_SMALL, cur ? UI_FG : UI_MUTED,
                          cur ? UI_HSEL : UI_BG, DISPLAY_DATUM_TC);
    }

    display_draw_text(UI_W / 2, UI_H - 12, "turn: select   press: ok",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TC);
    display_flush();
}

static void splash_reset_progress(int done, int total)
{
    display_fill_round_rect(10, 142, UI_W - 20, 4, 2, UI_TRACK);
    if (done <= 0) {
        return;
    }
    int w = (UI_W - 20) * done / total;
    if (w > 0) {
        display_fill_rect(10, 142, (uint16_t)w, 4, UI_WARN);
    }
}

static void splash_reset_line(int idx, const char *text)
{
    int y = 82 + idx * 11;
    display_draw_text(16, (int16_t)y, ">", DISPLAY_FONT_TINY, UI_WARN,
                      UI_PANEL, DISPLAY_DATUM_TL);
    display_draw_text(26, (int16_t)y, text, DISPLAY_FONT_TINY, UI_FG,
                      UI_PANEL, DISPLAY_DATUM_TL);
    display_draw_text(UI_W - 18, (int16_t)y, "OK", DISPLAY_FONT_TINY,
                      UI_OK, UI_PANEL, DISPLAY_DATUM_TR);
}

static void splash_factory_reset(void)
{
    char bn[16];
    char vb[48];
    snprintf(bn, sizeof(bn), "%s", app_build_no());
    snprintf(vb, sizeof(vb), "v%s", app_version());

    display_fill_screen(UI_BG);
    display_fill_rect(0, 0, UI_W, 2, UI_WARN);

    display_draw_text(10, 6, "BUILD", DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    display_draw_text(46, 6, bn, DISPLAY_FONT_TINY, UI_WARN, UI_BG,
                      DISPLAY_DATUM_TL);
    display_draw_text(UI_W - 10, 6, vb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);
    display_fill_rect(10, 18, UI_W - 20, 1, UI_LINE);

    display_draw_text(UI_W / 2, 26, "RESETTING", DISPLAY_FONT_LARGE,
                      UI_WARN, UI_BG, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 64, "FACTORY RESET IN PROGRESS",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TC);

    display_fill_round_rect(10, 78, UI_W - 20, 58, 8, UI_PANEL);
    display_draw_round_rect(10, 78, UI_W - 20, 58, 8, UI_LINE);
    splash_reset_progress(0, 5);

    display_draw_text(UI_W / 2, 154, "DO NOT POWER OFF",
                      DISPLAY_FONT_TINY, UI_WARN, UI_BG, DISPLAY_DATUM_TC);
    display_flush();

    static const char *const steps[5] = {
        "Stopping radio",
        "Clearing settings",
        "Erasing presets",
        "Resetting Wi-Fi",
        "Rebooting",
    };
    for (int i = 0; i < 5; ++i) {
        vTaskDelay(pdMS_TO_TICKS(110));
        splash_reset_line(i, steps[i]);
        splash_reset_progress(i + 1, 5);
        display_flush();
    }

    vTaskDelay(pdMS_TO_TICKS(180));
    storage_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(260));

    display_fill_rect(0, 152, UI_W, 12, UI_BG);
    display_draw_text(UI_W / 2, 154, "REBOOTING",
                      DISPLAY_FONT_TINY, UI_OK, UI_BG, DISPLAY_DATUM_TC);
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(220));
    esp_restart();
}

void splash_service_input(void)
{
    /* 재부팅 직후 버튼이 눌린 채여도 복구 메뉴로 재진입하지 않도록 먼저 뗌을 기다린다 */
    int64_t r0 = esp_timer_get_time();
    while (splash_btn_down() && (esp_timer_get_time() - r0) < 2000000) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* 스플래시 표시 중 약 1.2초간 클릭을 기다린다 */
    int64_t t0 = esp_timer_get_time();
    bool hit = false;
    while ((esp_timer_get_time() - t0) < 1200000) {
        if (splash_btn_down()) {
            hit = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!hit) {
        return;
    }

    /* 진입을 유발한 누름이 풀릴 때까지 대기 */
    r0 = esp_timer_get_time();
    while (splash_btn_down() && (esp_timer_get_time() - r0) < 1000000) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    encoder_reset();
    int sel = 0;
    bool confirm = false;
    bool prev = false;
    splash_recovery_draw(sel);

    while (1) {
        int d = encoder_get_delta();
        if (d != 0) {
            sel = (sel + (d > 0 ? 1 : -1) + 2) % 2;
            if (confirm) {
                splash_confirm_draw(sel);
            } else {
                splash_recovery_draw(sel);
            }
        }

        bool down = splash_btn_down();
        if (down && !prev) {
            if (!confirm) {
                if (sel == 0) {
                    break;                    /* CONTINUE BOOT */
                }
                confirm = true;               /* FACTORY RESET -> 확인 */
                sel = 0;
                splash_confirm_draw(sel);
            } else if (sel == 0) {
                confirm = false;              /* CANCEL */
                sel = 1;
                splash_recovery_draw(sel);
            } else {
                splash_factory_reset();
            }
        }
        prev = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    splash_show();
    display_flush();
}

