/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * Default (disabled) custom layout. NEWPROJECT exports ui_custom_layout.h
 * and overwrites this file.
 */
#pragma once

#include "ui_custom.h"

#define UI_CUSTOM_ENABLE 0

static const uc_widget_t uc_widgets[] = {
    { .type = UC_RECT },
};

#define UC_WIDGET_COUNT ((int)(sizeof(uc_widgets) / sizeof(uc_widgets[0])))
