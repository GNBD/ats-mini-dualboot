/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Single source for the running firmware identity.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char *app_version(void);
/* 표시용 짧은 버전: "stable 1.1.21" -> "1.1.21" */
const char *app_version_short(void);
const char *app_project_name(void);
const char *app_idf_version(void);
const char *app_build_no(void);

#ifdef __cplusplus
}
#endif
