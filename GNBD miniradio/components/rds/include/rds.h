/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * RDS 그룹 파서 (PS/RT/PTY/CT/AF).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t pi;           /* Program Identification */
    char     ps[9];        /* Program Service (8자) */
    char     rt[65];       /* Radio Text (64자) */
    uint8_t  pty;          /* Program Type */
    bool     tp;           /* Traffic Program */
    bool     ta;           /* Traffic Announcement */
    bool     ms;           /* Music/Speech */
    bool     stereo;       /* 0A DI stereo 비트 */
    bool     ps_valid;
    bool     rt_valid;
    bool     ct_valid;
    uint8_t  hour;         /* 로컬 시 */
    uint8_t  minute;
    int8_t   utc_offset;   /* 30분 단位 */
    uint32_t mjd;          /* Modified Julian Day (CT) */
    bool     rt_ab;        /* RadioText A/B 버퍼 토글 */
    uint8_t  af_count;     /* AF 리스트 개수 */
    uint16_t af[32];       /* AF 주파수 (0.1MHz 단위, e.g. 950=95.0MHz) */
    bool     eon_valid;    /* EON 그룹 수신됨 */
    bool     eon_ps_valid;
    bool     eon_tp;
    bool     eon_ta;
    uint16_t eon_pi;
    uint8_t  eon_ps_mask;
    uint8_t  eon_pty;
    uint16_t eon_freq_10khz;
    char     eon_ps[9];
} rds_info_t;

/** @brief 파서 초기화 */
void rds_parser_reset(rds_info_t *info);

/**
 * @brief RDS 그룹 1개 처리
 * @param block_a..d  FM_RDS_STATUS 블록 (16비트)
 * @param errors      블록별 오류도 (2bit × 4)
 */
void rds_parser_feed(rds_info_t *info, uint16_t block_a, uint16_t block_b,
                     uint16_t block_c, uint16_t block_d, uint8_t errors);

/** @brief PTY 코드 → 영문 이름 */
const char *rds_pty_name(uint8_t pty);

#ifdef __cplusplus
}
#endif
