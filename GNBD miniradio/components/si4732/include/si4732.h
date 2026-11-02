/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * SI4732-A10 저수준 I²C 드라이버 (자체 구현).
 *
 * 전제:
 *  - ESP-IDF v5.x 신규 i2c_master 드라이버 사용
 *  - 명령/응답 프레임은 AN332 (Si47XX Programming Guide) 준수
 *  - 모든 16비트 값은 big-endian (MSB 먼저) 전송
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "si4732_prop.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SI4732_I2C_ADDR_DEFAULT   0x11  /* SEN=LOW 기준 7비트 주소 */
#define SI4732_I2C_ADDR_ALT       0x63  /* SEN=HIGH 기준 7비트 주소 */

/* SI4732 수신 함수 (POWER_UP FUNC 필드) */
typedef enum {
    SI4732_FUNC_FM = 0,  /* FM 수신 */
    SI4732_FUNC_AM = 1,  /* AM (LW/MW/SW) + SSB (패치 적용 시) */
} si4732_func_t;

/* SSB 사이드밴드 (AM_TUNE_FREQ ARG1 비트 7:6) */
typedef enum {
    SI4732_SSB_OFF = 0,  /* 일반 AM */
    SI4732_SSB_LSB = 1,
    SI4732_SSB_USB = 2,
} si4732_sideband_t;

/* si4732_init 설정 (반드시 SI4732_CONFIG_DEFAULT()로 초기화 후 수정) */
typedef struct {
    uint8_t   i2c_addr;    /* 0 = 자동 탐지 (0x11 → 0x63), 지정 시 그 주소 사용 */
    uint32_t  i2c_hz;      /* 기본 100000 */
    uint32_t  timeout_ms;  /* CTS 폴링/전송 타임아웃, 기본 200 */
    int       reset_gpio;  /* 외부 RESET 핀, 사용 안 함 = -1 */
} si4732_config_t;

#define SI4732_CONFIG_DEFAULT() {      \
    .i2c_addr   = 0,                   \
    .i2c_hz     = 100000,              \
    .timeout_ms = 200,                 \
    .reset_gpio = -1,                  \
}

/* 드라이버 인스턴스 */
typedef struct {
    i2c_master_dev_handle_t i2c;
    i2c_master_bus_handle_t bus;      /* 버스 리셋 복구용 */
    uint8_t          addr;            /* 실제 사용 주소 */
    si4732_func_t    func;            /* 현재 부팅 함수 (FM/AM) */
    si4732_sideband_t ssb;            /* 현재 사이드밴드 (OFF=AM) */
    uint32_t         timeout_ms;
} si4732_t;

/* GET_REV 응답 */
typedef struct {
    uint8_t pn;          /* 최종 2자리 파트 번호 (예: 0x32) */
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t patch_major;
    uint8_t patch_minor;
    uint8_t chip_rev;
} si4732_rev_t;

/* RSQ (신호 품질) 공통 응답 */
typedef struct {
    bool    valid;        /* 유효 채널 */
    bool    afc_rail;     /* AFC 레일 인디케이터 */
    bool    soft_mute;    /* 소프트 뮤트 동작 중 */
    bool    stereo;       /* FM 파일럿 검출 (스테레오) */
    uint8_t stereo_blend; /* 0–100 % */
    uint8_t rssi;         /* dBμV */
    uint8_t snr;          /* dB */
    int8_t  freq_offset;  /* kHz */
} si4732_rsq_t;

/* FM RDS_STATUS 응답 */
typedef struct {
    bool    recv;         /* FIFO 최소 그룹 도달 */
    bool    sync_lost;    /* 동기 상실 */
    bool    sync_found;   /* 동기 획득 */
    bool    synced;       /* 현재 동기 상태 */
    bool    group_lost;   /* FIFO 오버런으로 그룹 유실 */
    bool    new_block_a;
    bool    new_block_b;
    uint8_t fifo_used;
    uint16_t block_a;
    uint16_t block_b;
    uint16_t block_c;
    uint16_t block_d;
    uint8_t errors;       /* 블록별 오류도 (2bit × 4: A,B,C,D) */
} si4732_rds_t;

/* GET_INT_STATUS 응답 (AN332 0x14: STATUS 바이트 자체가 인터럽트 상태) */
typedef struct {
    bool    stc;   /* STCINT: 시크/튠 완료 */
    bool    rds;   /* RDSINT */
    bool    rsq;   /* RSQINT */
    bool    err;   /* ERR */
    bool    cts;   /* CTS */
    uint8_t raw;   /* STATUS 원본 */
} si4732_int_status_t;

/* FM/AM_AGC_STATUS 응답 (AN332 0x27 / 0x47) */
typedef struct {
    bool    agc_enabled; /* false = AGCDIS (AGC 정지) */
    uint8_t index;       /* 현재 AGC 이득 인덱스 (FM 0–26, AM 0–37+) */
    uint8_t raw1;        /* RESP1 원본 */
    uint8_t raw2;        /* RESP2 원본 */
} si4732_agc_status_t;

/**
 * @brief 드라이버 초기화: I²C 장치 등록 + (선택) 외부 RESET 펄스
 */
esp_err_t si4732_init(si4732_t *dev, i2c_master_bus_handle_t bus, const si4732_config_t *cfg);

/** @brief I²C 장치 핸들 해제 (버스는 호출자 소유) */
esp_err_t si4732_deinit(si4732_t *dev);

/** @brief I²C 버스 리셋 후 재탐지 (통신 오류 복구) */
esp_err_t si4732_bus_reset(si4732_t *dev);

/** @brief POWER_UP (XOSCEN 크리스탈, 아나로그 출력) */
esp_err_t si4732_power_up(si4732_t *dev, si4732_func_t func);

/** @brief POWER_DOWN */
esp_err_t si4732_power_down(si4732_t *dev);

/** @brief GET_REV */
esp_err_t si4732_get_rev(si4732_t *dev, si4732_rev_t *out);

/** @brief SET_PROPERTY */
esp_err_t si4732_set_property(si4732_t *dev, uint16_t prop, uint16_t value);

/** @brief GET_PROPERTY */
esp_err_t si4732_get_property(si4732_t *dev, uint16_t prop, uint16_t *value);

/** @brief FM_TUNE_FREQ */
esp_err_t si4732_fm_tune(si4732_t *dev, uint32_t freq_hz);

/** @brief AM_TUNE_FREQ (AM/SSB 공용) */
esp_err_t si4732_am_tune(si4732_t *dev, uint32_t freq_hz, si4732_sideband_t sb);

/** @brief FM_TUNE_FREQ 즉시 반환 (스캔용, 안정화 대기 없음) */
esp_err_t si4732_fm_tune_fast(si4732_t *dev, uint32_t freq_hz);

/** @brief AM_TUNE_FREQ 즉시 반환 (스캔용, 안정화 대기 없음) */
esp_err_t si4732_am_tune_fast(si4732_t *dev, uint32_t freq_hz,
                              si4732_sideband_t sb);

/** @brief FM/AM_SEEK_START, ??/??/??? ?? ?? */
esp_err_t si4732_seek_start(si4732_t *dev, bool up,
                            uint32_t bottom_hz, uint32_t top_hz,
                            uint32_t spacing_hz,
                            uint8_t rssi_threshold, uint8_t snr_threshold);

/** @brief TUNE_STATUS (FM/AM) */
esp_err_t si4732_tune_status(si4732_t *dev, bool intack, bool cancel,
                             uint32_t *freq_hz, bool *stc, bool *valid,
                             bool *bltf);

/** @brief FM 채널 필터 (0=auto, 1=110k, 2=84k, 3=60k, 4=40 kHz) */
esp_err_t si4732_set_fm_bandwidth(si4732_t *dev, uint8_t code);

/** @brief Disable the SI4732 FM debug mode (property 0xFF00). */
esp_err_t si4732_disable_fm_debug(si4732_t *dev);

/** @brief AM 채널 필터 (si4732_am_bandwidth_t)
 *  @param line_filter true = AMPLFLT(bit8, 전원선 험 제거) 활성 */
esp_err_t si4732_set_am_bandwidth(si4732_t *dev, si4732_am_bandwidth_t bw,
                                  bool line_filter);

/** @brief FM 디엠퍼시스 (true=75µs, false=50µs) */
esp_err_t si4732_set_deemphasis(si4732_t *dev, bool us75);

/** @brief FM 스테레오 강제 모노/자동 (true = 모노 강제, false = AUTO) */
esp_err_t si4732_set_stereo(si4732_t *dev, bool force_mono);

/** @brief FM_RSQ_STATUS */
esp_err_t si4732_fm_rsq(si4732_t *dev, si4732_rsq_t *out);

/** @brief AM_RSQ_STATUS */
esp_err_t si4732_am_rsq(si4732_t *dev, si4732_rsq_t *out);

/** @brief RDS 처리 on/off (FM 전용) */
esp_err_t si4732_rds_enable(si4732_t *dev, bool enable);

/** @brief FM_RDS_STATUS (FIFO에서 1그룹 읽기) */
esp_err_t si4732_rds_status(si4732_t *dev, si4732_rds_t *out);

/** @brief RX_VOLUME (0–63) */
esp_err_t si4732_set_volume(si4732_t *dev, uint8_t volume);

/** @brief RX_HARD_MUTE (좌우 동시 음소거) */
esp_err_t si4732_set_mute(si4732_t *dev, bool mute);

/** @brief GPO1 출력 설정 (0=FM, 1=AM/SSB 밴드 스위치) */
esp_err_t si4732_set_gpo1(si4732_t *dev, bool level);

/** @brief GET_INT_STATUS (0x14) — 인터럽트 상태 갱신/조회 */
esp_err_t si4732_get_int_status(si4732_t *dev, si4732_int_status_t *out);

/** @brief FM_AGC_STATUS (0x27) — 현재 AGC 설정/이득 인덱스 조회 */
esp_err_t si4732_fm_agc_status(si4732_t *dev, si4732_agc_status_t *out);

/** @brief AM_AGC_STATUS (0x47) — 현재 AGC 설정/이득 인덱스 조회 (AM/SSB) */
esp_err_t si4732_am_agc_status(si4732_t *dev, si4732_agc_status_t *out);

/** @brief GPIO_CTL (0x80) — GPO1/2/3 출력 enable 설정 */
esp_err_t si4732_gpio_ctl(si4732_t *dev, bool gpo1_oen, bool gpo2_oen,
                          bool gpo3_oen);

/** @brief GPIO_SET (0x81) — GPO1/2/3 레벨 설정 */
esp_err_t si4732_gpio_set(si4732_t *dev, bool gpo1, bool gpo2, bool gpo3);

/**
 * @brief GPO_IEN(0x0001) — 인터럽트 소스/리피트 설정
 * @param stcien STC 인터럽트, rsqien RSQ, errien ERR, ctsien CTS
 * @param stcrep STC 반복, rsqrep RSQ 반복
 */
esp_err_t si4732_gpo_ien(si4732_t *dev, bool stcien, bool rsqien,
                         bool errien, bool ctsien, bool stcrep, bool rsqrep);

/**
 * @brief 임의 명령 전송 (탈출구)
 *
 * @param cmd     명령 opcode (예: 0x14)
 * @param args    인자 버퍼 (없으면 NULL)
 * @param args_len 인자 길이
 * @param resp    응답 버퍼. STATUS 바이트는 제외한 payload가 담긴다.
 *                NULL이면 응답을 읽지 않는다.
 * @param resp_len 응답 payload 길이
 * @return ESP_OK, 또는 ERR 응답 시 ESP_ERR_INVALID_RESPONSE
 */
esp_err_t si4732_raw(si4732_t *dev, uint8_t cmd,
                     const uint8_t *args, size_t args_len,
                     uint8_t *resp, size_t resp_len);

/** @brief FM 소프트 뮤트 최대 감쇠 (0–63 dB, 0=비활성) */
esp_err_t si4732_set_fm_soft_mute(si4732_t *dev, uint8_t atten_db);

/**
 * @brief FM RF AGC 정지 + LNA 이득 고정 (AN332 Command 0x28 FM_AGC_OVERRIDE)
 *
 * @param dev            드라이버
 * @param rf_agc_disable true = RF AGC 정지하고 이득 고정,
 *                       false = AGC 자동 복귀
 * @param lna_gain_index AGC 정지 시 이득 인덱스 (0 = 최소 감쇠/최대 이득,
 *                       1–25 = 중간, 26 = 최대 감쇠/최소 이득)
 *
 * rf_agc_disable = false일 때는 lna_gain_index가 무시된다.
 * FM 전용이며 AM/SSB에서는 사용할 수 없다 (ERR 응답).
 */
esp_err_t si4732_fm_agc_override(si4732_t *dev, bool rf_agc_disable,
                                 uint8_t lna_gain_index);

/** @brief AM/SSB 소프트 뮤트 최대 감쇠 (0–63 dB, 0=비활성) */
esp_err_t si4732_set_am_soft_mute(si4732_t *dev, uint8_t atten_db);

/** @brief AM/SSB 프런트엔드 AGC on/off (off = 감쇠 없이 최대 이득) */
esp_err_t si4732_set_am_agc(si4732_t *dev, bool enable);

/** @brief AM/SSB AGC 정지 + 고정 감쇠 인덱스 (0=max gain, 37+=max attn) */
esp_err_t si4732_set_am_agc_attn(si4732_t *dev, uint8_t index);

/** @brief AM/SSB 자동 볼륨 제어 최대 이득 (12–90 dB, AN332 g*340.2) */
esp_err_t si4732_set_am_avc_max_gain(si4732_t *dev, uint8_t gain_db);

/**
 * @brief AM 노이즈 블랭커 설정 (AN332 0x3900–0x3904)
 * @param rate     블랭킹 레이트 (100 Hz 단위, 기본 64)
 * @param interval 대체 구간 (µs)
 * @param iir      노이즈 플로어 추정 대역 (Hz, 기본 300)
 * @note Si474x 전용 기능일 수 있음. 지원하지 않으면 ERR 응답.
 */
esp_err_t si4732_set_am_noise_blank(si4732_t *dev, uint16_t rate,
                                    uint16_t interval, uint16_t iir);

/** @brief AM NB 검출 임계값 (0x3900, dB) */
esp_err_t si4732_set_am_nb_threshold(si4732_t *dev, uint16_t threshold_db);

/** @brief AM NB 블랭킹 지연 (0x3904, µs) */
esp_err_t si4732_set_am_nb_delay(si4732_t *dev, uint16_t delay_us);

/**
 * @brief AM NB 지원 여부 probe
 *
 * 0x3902(AM_NB_RATE)에 값을 쓰고 읽어 칩이 수용하는지 확인한 뒤
 * 원래 값으로 복원한다.
 *
 * @param supported true = 칩이 값을 수용함 (NB 동작 가능성)
 *                  false = 값 무시됨 (Si474x 전용 추정)
 * @return 통신 실패 시 ESP_ERR_INVALID_RESPONSE
 */
esp_err_t si4732_am_nb_probe(si4732_t *dev, bool *supported);

#ifdef __cplusplus
}
#endif
