/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * RDS 그룹 파서 구현.
 * 지원 그룹: 0A/0B (PS, DI), 2A/2B (RT), 4A (CT), 14A (EON 기본 무시)
 * 오류도 3(정정 불가) 블록은 폐기한다.
 */
#include <string.h>
#include "rds.h"

#define RDS_ERR_UNCORRECTABLE 3

static const char *const PTY_NAMES[32] = {
    "None", "News", "Current", "Info", "Sport", "Education", "Drama", "Culture",
    "Science", "Varied", "Pop", "Rock", "Easy", "Light", "Classics", "Other",
    "Weather", "Finance", "Children", "Social", "Religion", "Phone", "Travel",
    "Leisure", "Jazz", "Country", "Nation", "Oldies", "Folk", "Document", "Alarm",
    "Test",
};

void rds_parser_reset(rds_info_t *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    memset(info->ps, ' ', 8);
    info->ps[8] = '\0';
    memset(info->eon_ps, ' ', 8);
    info->eon_ps[8] = '\0';
    info->rt[0] = '\0';
}

const char *rds_pty_name(uint8_t pty)
{
    return PTY_NAMES[pty & 0x1F];
}

static void handle_ps(rds_info_t *info, uint16_t b, uint16_t d)
{
    uint8_t addr = (uint8_t)(b & 0x03);
    char c0 = (char)(d >> 8);
    char c1 = (char)(d & 0xFF);
    if (c0 < 32) c0 = ' ';
    if (c1 < 32) c1 = ' ';
    info->ps[addr * 2] = c0;
    info->ps[addr * 2 + 1] = c1;
    info->ps_valid = true;
}

static void handle_0a(rds_info_t *info, uint16_t b, uint16_t c, uint16_t d)
{
    uint8_t addr = (uint8_t)(b & 0x03);

    /* DI 값은 block B bit2, C1C0(addr)가 DI 세그먼트 번호.
       세그먼트 0 = stereo flag */
    bool di = (b & 0x04) != 0;
    info->ta = (b & 0x10) != 0;
    info->ms = (b & 0x08) != 0;

    if (addr == 0) {
        info->stereo = di;
    }
    if (addr == 0 || addr == 1) {
        uint8_t af_code = (uint8_t)(c >> 8);
        if (af_code >= 1 && af_code <= 204) {
            uint16_t f = (uint16_t)(876 + af_code);
            if (info->af_count < 32) {
                bool dup = false;
                for (uint8_t i = 0; i < info->af_count; ++i) {
                    if (info->af[i] == f) { dup = true; break; }
                }
                if (!dup) {
                    info->af[info->af_count++] = f;
                }
            }
        }
    }
    handle_ps(info, b, d);
}

static void handle_2a(rds_info_t *info, uint16_t b, uint16_t c, uint16_t d)
{
    uint8_t addr = (uint8_t)(b & 0x0F);
    if (addr > 15) {
        return;
    }
    bool ab = (b & 0x10) != 0;
    if (addr == 0 && ab != info->rt_ab) {
        info->rt_ab = ab;
        memset(info->rt, ' ', 64);
        info->rt[64] = '\0';
    }
    char c0 = (char)(d >> 8);
    char c1 = (char)(d & 0xFF);
    if (c0 < 32) c0 = ' ';
    if (c1 < 32) c1 = ' ';
    info->rt[addr * 4]     = (char)(c >> 8);
    info->rt[addr * 4 + 1] = (char)(c & 0xFF);
    info->rt[addr * 4 + 2] = c0;
    info->rt[addr * 4 + 3] = c1;
    for (int i = 0; i < 4; ++i) {
        if (info->rt[addr * 4 + i] < 32) {
            info->rt[addr * 4 + i] = ' ';
        }
    }
    info->rt[64] = '\0';
    info->rt_valid = true;
}

static void handle_2b(rds_info_t *info, uint16_t b, uint16_t d)
{
    uint8_t addr = (uint8_t)(b & 0x0F);
    if (addr > 15) {
        return;
    }
    char c0 = (char)(d >> 8);
    char c1 = (char)(d & 0xFF);
    if (c0 < 32) c0 = ' ';
    if (c1 < 32) c1 = ' ';
    info->rt[addr * 2]     = c0;
    info->rt[addr * 2 + 1] = c1;
    info->rt[64] = '\0';
    info->rt_valid = true;
}

static void handle_4a(rds_info_t *info, uint16_t b, uint16_t c, uint16_t d)
{
    /* MJD 상위 7비트 + hour + minute + offset */
    uint32_t mjd = ((uint32_t)(b & 0x03) << 15) | (c >> 1);
    uint8_t hour = (uint8_t)(((c & 0x01) << 4) | (d >> 12));
    uint8_t minute = (uint8_t)((d >> 6) & 0x3F);
    int8_t offset = (int8_t)(d & 0x1F);
    bool negative = (d & 0x20) != 0;

    if (hour > 23 || minute > 59) {
        return;
    }
    info->mjd = mjd;
    info->hour = hour;
    info->minute = minute;
    info->utc_offset = negative ? (int8_t)-offset : offset;
    info->ct_valid = true;
}

static void handle_14(rds_info_t *info, uint16_t b, uint16_t c, uint16_t d,
                      bool version_b, uint8_t err_c)
{
    if (d == 0) {
        return;
    }
    if (info->eon_pi != d) {
        info->eon_pi = d;
        info->eon_ps_valid = false;
        info->eon_ps_mask = 0;
        info->eon_ta = false;
        info->eon_tp = false;
        info->eon_pty = 0;
        info->eon_freq_10khz = 0;
        memset(info->eon_ps, ' ', 8);
        info->eon_ps[8] = '\0';
    }
    info->eon_valid = true;
    info->eon_tp = (b & 0x10) != 0;

    if (version_b) {
        info->eon_ta = (b & 0x08) != 0;
        return;
    }
    if (err_c == RDS_ERR_UNCORRECTABLE) {
        return;
    }

    uint8_t variant = (uint8_t)(b & 0x0F);
    if (variant <= 3) {
        char c0 = (char)(c >> 8);
        char c1 = (char)(c & 0xFF);
        if (c0 < 32) c0 = ' ';
        if (c1 < 32) c1 = ' ';
        info->eon_ps[variant * 2] = c0;
        info->eon_ps[variant * 2 + 1] = c1;
        info->eon_ps_mask |= (uint8_t)(1u << variant);
        if (info->eon_ps_mask == 0x0F) {
            int end = 7;
            while (end >= 0 && info->eon_ps[end] == ' ') {
                info->eon_ps[end--] = '\0';
            }
            info->eon_ps_valid = true;
        }
    } else if (variant >= 5 && variant <= 9) {
        uint8_t code = (uint8_t)(c & 0xFF);
        if (code >= 1 && code <= 204) {
            info->eon_freq_10khz = (uint16_t)(876 + code);
        }
    } else if (variant == 13) {
        info->eon_pty = (uint8_t)((c >> 11) & 0x1F);
        info->eon_ta = (c & 0x01) != 0;
    }
}

void rds_parser_feed(rds_info_t *info, uint16_t block_a, uint16_t block_b,
                     uint16_t block_c, uint16_t block_d, uint8_t errors)
{
    if (!info) {
        return;
    }
    /* AN332 RESP12: BLEA[7:6] BLEB[5:4] BLEC[3:2] BLED[1:0] */
    uint8_t err_a = (errors >> 6) & 0x03;
    uint8_t err_b = (errors >> 4) & 0x03;
    uint8_t err_c = (errors >> 2) & 0x03;
    uint8_t err_d = errors & 0x03;
    if (err_a == RDS_ERR_UNCORRECTABLE || err_b == RDS_ERR_UNCORRECTABLE) {
        return;
    }
    if (block_a == 0) {
        return;
    }

    info->pi = block_a;
    uint8_t group = (uint8_t)((block_b >> 12) & 0x0F);
    bool version_b = (block_b & 0x0800) != 0;
    info->tp = (block_b & 0x0400) != 0;
    info->pty = (uint8_t)((block_b >> 5) & 0x1F);

    switch (group) {
    case 0:
        if (err_c == RDS_ERR_UNCORRECTABLE || err_d == RDS_ERR_UNCORRECTABLE) {
            return;
        }
        if (!version_b) {
            handle_0a(info, block_b, block_c, block_d);
        } else {
            info->ta = (block_b & 0x10) != 0;
            info->ms = (block_b & 0x08) != 0;
            handle_ps(info, block_b, block_d);
        }
        break;
    case 2:
        if (err_c == RDS_ERR_UNCORRECTABLE || err_d == RDS_ERR_UNCORRECTABLE) {
            return;
        }
        if (!version_b) {
            handle_2a(info, block_b, block_c, block_d);
        } else {
            handle_2b(info, block_b, block_d);
        }
        break;
    case 4:
        if (!version_b && err_c != RDS_ERR_UNCORRECTABLE &&
            err_d != RDS_ERR_UNCORRECTABLE) {
            handle_4a(info, block_b, block_c, block_d);
        }
        break;
    case 14:
        if (err_d != RDS_ERR_UNCORRECTABLE) {
            handle_14(info, block_b, block_c, block_d, version_b, err_c);
        }
        break;
    default:
        break;
    }
}
