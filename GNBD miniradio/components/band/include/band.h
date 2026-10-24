/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 대역 정의 테이블 + 모드 전환 헬퍼.
 *
 * 주파수 단위: Hz (내부 공통). 드라이버 호출 시
 *   FM → 10 kHz 단위, AM/SSB → 1 kHz 단위로 변환.
 *
 * 대역 중복 정책:
 *   SW(2.3–26.1 MHz)는 햄 대역과 겹친다. 주파수→대역 탐색 시
 *   "가장 좁은 대역"을 우선하므로 햄 대역이 SW보다 먼저 선택된다.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UI/앱 레벨 모드 (SI4732 함수와 구분: SSB는 AM 함수 위에 패치로 얹힘) */
typedef enum {
    RADIO_MODE_FM = 0,
    RADIO_MODE_AM,      /* MW / LW / SW 방송 */
    RADIO_MODE_LSB,     /* SSB (패치 필요) */
    RADIO_MODE_USB,     /* SSB (패치 필요) */
} radio_mode_t;

typedef struct {
    const char  *name;       /* UI 표시 이름 (예: "80m") */
    radio_mode_t default_mode;
    uint32_t     min_hz;
    uint32_t     max_hz;
    uint32_t     step_hz;    /* 엔코더 1 detent당 기본 스텝 */
    bool         ham;        /* 햄/CB 대역 (SW보다 우선 선택) */
} band_desc_t;

/* FM / MW / LW / SW 방송 + 햄 대역 + CB */
extern const band_desc_t band_table[];
extern const size_t band_count;

/** @brief 현재 모드가 SSB인지 */
bool band_is_ssb(radio_mode_t mode);

/** @brief mode → SI4732 사이드밴드 값으로 변환 (fm/am = 0) */
uint8_t band_sideband_value(radio_mode_t mode);

/**
 * @brief freq_hz를 포함하는 대역 인덱스 탐색 (가장 좁은 대역 우선)
 * @return 0..band_count-1, 못 찾으면 -1
 */
int band_index_for_freq(uint32_t freq_hz);

/** @brief 다음/이전 대역 (순환) */
int band_next(int index);
int band_prev(int index);

/** @brief 대역 범위로 주파수 클램프 (범위 밖이면 clamp) */
uint32_t band_clamp_freq(int index, uint32_t freq_hz);

/**
 * @brief 대역 범위 안에서 스텝 이동 (양/음수 step 배수, 순환 없음)
 * @param step_hz 0이면 대역 기본 스텝 사용
 */
uint32_t band_step_freq(int index, uint32_t freq_hz, int32_t steps,
                        uint32_t step_hz);

/** @brief 대역의 스텝을 주파수 격자에 정렬 (min 기준) */
uint32_t band_snap_freq(int index, uint32_t freq_hz, uint32_t step_hz);

#ifdef __cplusplus
}
#endif
