/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * 부팅 스플래시 화면 + 복구 메뉴.
 * 좌상단 빌드번호, 우하단 버전, 중앙 워드마크.
 * 라디오 초기화가 끝나면 page_radio()가 화면을 덮어쓴다.
 *
 * 복구 메뉴:
 *   스플래시가 떠 있는 동안 엔코더를 누르면 RECOVERY 메뉴가 열린다.
 *   CONTINUE BOOT / FACTORY RESET(설정 전체 삭제 후 재부팅) 선택 가능.
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
    snprintf(gb, sizeof(gb), "GNBD %s", app_version_short());
    display_draw_text(UI_W / 2, 136, gb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 150, app_build_no(), DISPLAY_FONT_TINY,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TC);

    display_flush();
}

/* ------------------------------------------------------------ 복구 메뉴 */

static bool splash_btn_down(void)
{
    return gpio_get_level((gpio_num_t)BOARD_ENC_PUSH_GPIO) == 0;
}

static void splash_wait_btn_release(void)
{
    int64_t t0 = esp_timer_get_time();
    while (splash_btn_down() && (esp_timer_get_time() - t0) < 3000000) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static int splash_menu_2(const char *title, const char *l1, const char *l2,
                         const char *hint, int cur)
{
    display_fill_screen(UI_BG);
    display_fill_rect(0, 0, UI_W, 22, UI_PANEL);
    display_draw_text(10, 7, title, DISPLAY_FONT_SMALL, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TL);
    display_draw_text(UI_W - 10, 8, hint, DISPLAY_FONT_TINY, UI_MUTED,
                      UI_PANEL, DISPLAY_DATUM_TR);

    const char *items[2] = { l1, l2 };
    for (int i = 0; i < 2; ++i) {
        int y = 54 + i * 34;
        bool on = (i == cur);
        display_fill_round_rect(10, (int16_t)y, UI_W - 20, 28, 10,
                                on ? UI_HSEL : UI_PANEL);
        display_draw_text(22, (int16_t)(y + 6), items[i], DISPLAY_FONT_SMALL,
                          on ? UI_ACC_HI : UI_FG,
                          on ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TL);
    }
    display_flush();
    return cur;
}

static void splash_confirm_draw(int cur)
{
    display_fill_screen(UI_BG);
    display_fill_rect(0, 0, UI_W, 22, UI_PANEL);
    display_draw_text(10, 7, "FACTORY RESET", DISPLAY_FONT_SMALL, UI_WARN,
                      UI_PANEL, DISPLAY_DATUM_TL);

    display_draw_text(10, 34, "Erase all settings (NVS only)?",
                      DISPLAY_FONT_SMALL, UI_FG, UI_BG, DISPLAY_DATUM_TL);
    display_draw_text(10, 52, "Presets, ATS, Wi-Fi, configs",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL);

    const char *items[2] = { "CANCEL", "RESET" };
    for (int i = 0; i < 2; ++i) {
        int x = 40 + i * 140;
        bool on = (i == cur);
        display_fill_round_rect((int16_t)x, 104, 100, 30, 10,
                                on ? (i ? UI_RED : UI_HSEL) : UI_PANEL);
        display_draw_text((int16_t)(x + 50), 111, items[i],
                          DISPLAY_FONT_SMALL, on ? UI_FG : UI_MUTED,
                          on ? (i ? UI_RED : UI_HSEL) : UI_PANEL,
                          DISPLAY_DATUM_TC);
    }
    display_flush();
}

static void splash_reset_progress(void)
{
    static const char *const steps[] = {
        "Stopping radio", "Clearing settings", "Erasing presets",
        "Resetting Wi-Fi", "Rebooting"
    };
    const int total = (int)(sizeof(steps) / sizeof(steps[0]));

    display_fill_screen(UI_BG);
    display_fill_rect(0, 0, UI_W, 22, UI_PANEL);
    display_draw_text(10, 7, "FACTORY RESET IN PROGRESS", DISPLAY_FONT_SMALL,
                      UI_WARN, UI_PANEL, DISPLAY_DATUM_TL);
    display_draw_text(10, 26, "DO NOT POWER OFF", DISPLAY_FONT_SMALL, UI_RED,
                      UI_BG, DISPLAY_DATUM_TL);

    for (int i = 0; i < total; ++i) {
        int y = 52 + i * 20;
        display_draw_text(18, (int16_t)(y + 4), ">", DISPLAY_FONT_TINY,
                          UI_ACC_HI, UI_BG, DISPLAY_DATUM_TL);
        display_draw_text(32, y, steps[i], DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                          DISPLAY_DATUM_TL);
        display_flush();
        if (i == 1) {
            storage_factory_reset();
        }
        vTaskDelay(pdMS_TO_TICKS(220));
    }
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

static void splash_recovery_menu(void)
{
    int cur = 0;
    splash_menu_2("RECOVERY", "CONTINUE BOOT", "FACTORY RESET",
                  "turn: select   press: ok", cur);

    for (;;) {
        if (encoder_get_delta() != 0) {
            cur ^= 1;
            splash_menu_2("RECOVERY", "CONTINUE BOOT", "FACTORY RESET",
                          "turn: select   press: ok", cur);
            vTaskDelay(pdMS_TO_TICKS(120));
        }
        if (splash_btn_down()) {
            splash_wait_btn_release();
            if (cur == 0) {
                return;
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    int sel = 0;
    splash_confirm_draw(sel);
    for (;;) {
        if (encoder_get_delta() != 0) {
            sel ^= 1;
            splash_confirm_draw(sel);
            vTaskDelay(pdMS_TO_TICKS(120));
        }
        if (splash_btn_down()) {
            splash_wait_btn_release();
            if (sel == 1) {
                splash_reset_progress();
            }
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void splash_service_input(void)
{
    /* 스플래시가 떠 있는 동안 클릭하면 복구 메뉴 */
    int64_t t0 = esp_timer_get_time();
    while ((esp_timer_get_time() - t0) < 1500000) {
        if (splash_btn_down()) {
            splash_wait_btn_release();
            splash_recovery_menu();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
