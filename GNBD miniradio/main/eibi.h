/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * EiBi schedule record and module interfaces.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct __attribute__((packed)) {
    uint16_t freq;
    uint8_t  sh, sm, eh, em;
    uint8_t  days;    /* bit0=Mon ... bit6=Sun, 0x7F=daily */
    uint8_t  week;    /* 0=every, 1..5=nth weekday, 0xFF=last */
    uint8_t  month;   /* 0=none, 1..12 */
    uint8_t  day;     /* 0=none, 1..31 */
    char     name[32];
} eibi_rec_t;

typedef struct {
    char station[26];
    char lang[5];
    char target[5];
} eibi_entry_t;

esp_err_t eibi_fs_mount(void);
bool eibi_available(void);
bool eibi_parse_line(char *line, eibi_rec_t *out);
void eibi_task(void *arg);
void eibi_download_start(void);
int eibi_lookup(uint16_t freq, int64_t utc, char *out, size_t n);
int eibi_lookup_entries(uint16_t freq, int64_t utc, eibi_entry_t *out, int max);
int64_t eibi_utc_now(void);
void eibi_refresh(void);
int eibi_record_count(void);
bool eibi_read_record(int idx, eibi_rec_t *out);
void open_eibi_app(void);
void open_eibi_view(void);
void open_eibi_confirm(void);
void eibi_app_action(void);
