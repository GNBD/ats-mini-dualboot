/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 대역 테이블 정의.
 */
#include "band.h"

const band_desc_t band_table[] = {
    /* name   mode          min_hz      max_hz       step_hz  ham   */
    { "VHF",  RADIO_MODE_FM, 64000000,  108000000,    100000, false },
    { "MW",   RADIO_MODE_AM,   520000,    1710000,      1000, false },
    { "LW",   RADIO_MODE_AM,   153000,     279000,      1000, false },
    { "SW",   RADIO_MODE_AM,  2300000,  26100000,       5000, false },
    { "160m", RADIO_MODE_LSB, 1800000,   2000000,       1000, true  },
    { "80m",  RADIO_MODE_LSB, 3500000,   4000000,       1000, true  },
    { "40m",  RADIO_MODE_LSB, 7000000,   7300000,       1000, true  },
    { "30m",  RADIO_MODE_USB, 10100000, 10150000,       1000, true  },
    { "20m",  RADIO_MODE_USB, 14000000, 14350000,       1000, true  },
    { "17m",  RADIO_MODE_USB, 18068000, 18168000,       1000, true  },
    { "15m",  RADIO_MODE_USB, 21000000, 21450000,       1000, true  },
    { "12m",  RADIO_MODE_USB, 24890000, 24990000,       1000, true  },
    { "10m",  RADIO_MODE_USB, 28000000, 29700000,       1000, true  },
    { "CB",   RADIO_MODE_AM, 26965000, 27405000,        1000, true  },
};

const size_t band_count = sizeof(band_table) / sizeof(band_table[0]);

bool band_is_ssb(radio_mode_t mode)
{
    return mode == RADIO_MODE_LSB || mode == RADIO_MODE_USB;
}

uint8_t band_sideband_value(radio_mode_t mode)
{
    /* SI4732_SSB_LSB=1, SI4732_SSB_USB=2, 그 외 0 */
    switch (mode) {
    case RADIO_MODE_LSB: return 1;
    case RADIO_MODE_USB: return 2;
    default:             return 0;
    }
}

int band_index_for_freq(uint32_t freq_hz)
{
    int best = -1;
    uint32_t best_span = UINT32_MAX;
    for (size_t i = 0; i < band_count; i++) {
        const band_desc_t *b = &band_table[i];
        if (freq_hz < b->min_hz || freq_hz > b->max_hz) {
            continue;
        }
        uint32_t span = b->max_hz - b->min_hz;
        /* 동일 폭이면 햄 대역 우선 */
        if (span < best_span || (span == best_span && b->ham &&
                                 !band_table[best].ham)) {
            best = (int)i;
            best_span = span;
        }
    }
    return best;
}

int band_next(int index)
{
    if (index < 0) {
        return 0;
    }
    return (int)(((size_t)index + 1) % band_count);
}

int band_prev(int index)
{
    if (index < 0) {
        return (int)band_count - 1;
    }
    return (int)(((size_t)index + band_count - 1) % band_count);
}

uint32_t band_clamp_freq(int index, uint32_t freq_hz)
{
    if (index < 0 || (size_t)index >= band_count) {
        return freq_hz;
    }
    const band_desc_t *b = &band_table[index];
    if (freq_hz < b->min_hz) {
        return b->min_hz;
    }
    if (freq_hz > b->max_hz) {
        return b->max_hz;
    }
    return freq_hz;
}

uint32_t band_step_freq(int index, uint32_t freq_hz, int32_t steps,
                        uint32_t step_hz)
{
    if (index < 0 || (size_t)index >= band_count) {
        return freq_hz;
    }
    const band_desc_t *b = &band_table[index];
    if (step_hz == 0) {
        step_hz = b->step_hz;
    }
    int64_t f = (int64_t)freq_hz + (int64_t)steps * (int64_t)step_hz;
    if (f > (int64_t)b->max_hz) {
        f = b->min_hz;
    } else if (f < (int64_t)b->min_hz) {
        f = b->max_hz;
    }
    return (uint32_t)f;
}

uint32_t band_snap_freq(int index, uint32_t freq_hz, uint32_t step_hz)
{
    if (index < 0 || (size_t)index >= band_count || step_hz == 0) {
        return freq_hz;
    }
    const band_desc_t *b = &band_table[index];

    /* 격자 시작점: 대역 최소값을 step 배수로 올림.
       MW 9 kHz만 규정상 531 kHz에서 시작하므로 예외 처리한다. */
    uint32_t base;
    if (index == 1 && step_hz == 9000) {
        base = 531000;
    } else {
        base = ((b->min_hz + step_hz - 1) / step_hz) * step_hz;
    }

    uint32_t off = (freq_hz > base) ? (freq_hz - base) : 0;
    uint32_t k = (off + step_hz / 2) / step_hz;
    uint64_t snapped = (uint64_t)base + (uint64_t)k * step_hz;

    while (snapped > b->max_hz && snapped >= step_hz) {
        snapped -= step_hz;
    }
    while (snapped < b->min_hz) {
        snapped += step_hz;
    }
    if (snapped > b->max_hz) {
        snapped = b->max_hz;
    }
    return (uint32_t)snapped;
}
