/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Radio frequency screen (hero card, S-meter, RDS, status).
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "display.h"
#include "ui.h"
#include "ui_custom.h"
#include "ui_runtime.h"
#include "eibi.h"
#include "decode.h"
#include "ui_int.h"


void ui_draw_status(const ui_state_t *s)
{
    if (ui_custom_active()) {
        return;
    }
    draw_header_msg(s);
}

void ui_draw_loading(const char *msg)
{
    panel(60, 55, 200, 60, 12, UI_PANEL, UI_ACC);
    display_draw_text(UI_W / 2, 68, msg ? msg : "LOADING",
                      DISPLAY_FONT_SMALL, UI_FG, UI_PANEL, DISPLAY_DATUM_TC);
    display_draw_text(UI_W / 2, 88, "PLEASE WAIT", DISPLAY_FONT_TINY,
                      UI_MUTED, UI_PANEL, DISPLAY_DATUM_TC);
    display_flush();
}


void ui_draw_volume(const ui_state_t *s, bool show)
{
    if (!show) {
        ui_draw_all(s);
        return;
    }
    display_fill_round_rect(44, 50, 232, 70, 18, UI_PANEL);
    display_draw_round_rect(44, 50, 232, 70, 18, UI_LINE);

    display_draw_text(60, 58, "Volume", DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                      DISPLAY_DATUM_TL);
    char v[8];
    snprintf(v, sizeof(v), "%u", s->volume);
    display_draw_text(260, 55, v, DISPLAY_FONT_LARGE, UI_FG, UI_PANEL,
                      DISPLAY_DATUM_TR);
    bar(60, 96, 200, 8, s->volume, 63, UI_ACC);
}
