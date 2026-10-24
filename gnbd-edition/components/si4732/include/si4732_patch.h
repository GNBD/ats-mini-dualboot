/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * SI4732 SSB 패치 로딩 및 BFO 제어.
 *
 * ⚠️ 라이선스: si4732_ssb_patch 데이터는 Silicon Labs 소유로 별도 라이선스
 *    적용 가능성 있음 (계획서 3.2 / 5장 참조). Phase 2에서 데이터 출처 검증 후 삽입.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "si4732.h"
#include "si4732_prop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* POWER_UP FUNC=15 (Library ID 조회) 응답 */
typedef struct {
    uint8_t pn;
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t chip_rev;
    uint8_t library_id;
} si4732_library_id_t;

/* SSB 패치 데이터 (si4732_patch_data.c, Phase 2에서 교체) */
extern const uint8_t si4732_ssb_patch[];
extern const size_t si4732_ssb_patch_size;

/**
 * @brief POWER_UP FUNC=15로 Library ID 조회 (패치 호환성 확인용)
 * @note  패치 로딩 전 power_down 상태에서 호출
 */
esp_err_t si4732_query_library_id(si4732_t *dev, si4732_library_id_t *out);

/**
 * @brief 패치 로딩용 POWER_UP (XOSCEN | PATCH | FUNC=AM)
 */
esp_err_t si4732_patch_power_up(si4732_t *dev);

/**
 * @brief SSB 패치 다운로드 (8바이트 청크 + 청크별 CTS 확인)
 * @param data 0x15/0x16 시작 바이트를 포함한 원본 패치 배열
 * @param size 배열 크기 (8의 배수 권장)
 */
esp_err_t si4732_load_ssb_patch(si4732_t *dev, const uint8_t *data, size_t size);

/**
 * @brief SSB_MODE 프로퍼티 (0x0101) 설정
 * @param bw          오디오 대역폭
 * @param avc_en      자동 음량 제어
 * @param smute_snr   소프트 뮤트 기준 (false=RSSI, true=SNR)
 * @param dsp_afc_dis AFC disable (SSB 모드=1, SYNC 모드=0)
 */
esp_err_t si4732_set_ssb_mode(si4732_t *dev, si4732_ssb_bandwidth_t bw,
                              bool avc_en, bool smute_snr, bool dsp_afc_dis);

/**
 * @brief SSB_BFO 프로퍼티 (0x0100) 설정
 * @param offset_hz -16383 ~ +16383 Hz
 */
esp_err_t si4732_set_bfo(si4732_t *dev, int16_t offset_hz);

/**
 * @brief 내장 패치 데이터로 SSB 수신 준비 (Phase 2 시퀀스)
 *        query_library_id → patch_power_up → load_ssb_patch → set_ssb_mode
 */
esp_err_t si4732_ssb_prepare(si4732_t *dev, si4732_ssb_bandwidth_t bw);

#ifdef __cplusplus
}
#endif
