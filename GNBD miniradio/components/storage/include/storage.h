/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * NVS 기반 설정/프리셋 영구 저장.
 *
 * v1(0x4D523031) → v2(0x4D523032) 자동 마이그레이션.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MAGIC_V1  0x4D523031  /* 'MR01' */
#define STORAGE_MAGIC_V2  0x4D523032  /* 'MR02' */
#define STORAGE_MAGIC     0x4D523033  /* 'MR03' */
#define STORAGE_NS        "miniradio"

#define STORAGE_PRESET_SLOTS 99
#define STORAGE_ATS_SLOTS    50
#define STORAGE_BAND_MEM     16

typedef struct {
    uint32_t magic;
    uint32_t freq_hz;      /* 마지막 주파수 */
    uint8_t  band_index;   /* band_table 인덱스 */
    uint8_t  mode;         /* radio_mode_t */
    uint8_t  volume;       /* 0–63 */
    uint8_t  brightness;   /* 백라이트 0–100 */
    uint8_t  flags;        /* bit0 RDS, bit1 스테레오 강제, bit2 스켈치 */
    uint8_t  deemphasis;   /* 0=50us, 1=75us */
    uint8_t  sleep_min;    /* 0=off, 그 외 분 */
    uint8_t  bw[3];        /* FM/AM/SSB 대역폭 코드 */
    int8_t   step_idx[3];  /* FM/AM/SSB 스텝 인덱스, -1=대역 기본 */
    int16_t  bfo_hz;       /* SSB BFO 오프셋 */
    uint32_t band_freq[STORAGE_BAND_MEM]; /* 밴드별 마지막 주파수 */
    uint16_t band_valid;   /* 밴드별 유효 비트마스크 */
    uint8_t  reserved[2];
} storage_settings_t;

/* v2 구조체 (마이그레이션 전용) */
typedef struct {
    uint32_t magic;
    uint32_t freq_hz;
    uint8_t  band_index;
    uint8_t  mode;
    uint8_t  volume;
    uint8_t  brightness;
    uint8_t  flags;
    uint8_t  deemphasis;
    uint8_t  sleep_min;
    uint8_t  bw[3];
    int8_t   step_idx[3];
    int16_t  bfo_hz;
    uint8_t  reserved[6];
} storage_settings_v2_t;

typedef struct {
    uint32_t freq_hz;
    uint8_t  band;
    uint8_t  mode;
    uint8_t  reserved[2];
} preset_entry_t;

typedef struct {
    uint8_t        count;
    preset_entry_t slot[STORAGE_PRESET_SLOTS];
} preset_bank_t;

typedef struct {
    uint8_t        count;
    preset_entry_t slot[STORAGE_ATS_SLOTS];
} ats_bank_t;

/* 확장 수신 설정 (별도 NVS 키) */
#define STORAGE_MAGIC_EXT 0x4D525832  /* 'MRX2' */

typedef struct {
    uint32_t magic;
    uint8_t  agc_on;         /* 0/1 */
    uint8_t  soft_mute;      /* AM/SSB 소프트뮤트 감쇠 dB */
    uint8_t  avc_gain;       /* AVC 최대 이득 0–63 */
    uint8_t  squelch_on;     /* 0/1 */
    uint8_t  squelch_type[4];/* FM/AM/LSB/USB: 0=RSSI, 1=SNR */
    uint8_t  squelch_thr[4]; /* FM/AM/LSB/USB 임계값 */
    uint8_t  wifi_mode;      /* 0=OFF, 1=AP, 2=STA */
    uint8_t  reserved[1];
    int16_t  utc_min;        /* UTC 오프셋(분) */
    uint8_t  batt_view;      /* 0=아이콘,1=전압,2=%,3=%+아이콘,4=전압+아이콘 */
    uint8_t  screen_off;     /* 자동 화면 꺼짐(초), 0=off */
    uint8_t  unit_mode;      /* 0=MHz, 1=kHz */
    uint8_t  reserved2[1];
    char     wifi_ssid[33];
    char     wifi_pass[65];
} storage_ext_t;

/* 스플래시 개인화 설정 (별도 NVS 키) */
#define STORAGE_MAGIC_SPLASH 0x53504C48  /* 'SPLH' */
typedef struct {
    uint32_t magic;
    uint8_t  on;      /* 0/1 */
    uint8_t  mode;    /* 0=callsign, 1=welcome */
    char     callsign[24];
    char     welcome[48];
} storage_splash_t;

/* 퀵 메뉴 구성 (별도 NVS 키) */
#define STORAGE_MAGIC_QUICK 0x51554943  /* 'QUIC' */
typedef struct {
    uint32_t magic;
    uint16_t mask;    /* QA_* 비트마스크 */
} storage_quick_t;

/* v1 구조체 (마이그레이션 전용) */
typedef struct {
    uint32_t magic;
    uint32_t freq_hz;
    uint8_t  band_index;
    uint8_t  mode;
    uint8_t  volume;
    uint8_t  bandwidth;
    uint8_t  brightness;
    uint8_t  agc_enable;
    int16_t  bfo_hz;
    uint8_t  reserved[6];
} storage_settings_v1_t;

/** @brief NVS 초기화 (필요 시 파티션 삭제 후 재초기화 포함) */
esp_err_t storage_init(void);

/** @brief 설정 로드 (없거나 손상 시 기본값, v1 자동 변환) */
esp_err_t storage_load_settings(storage_settings_t *out);

/** @brief 설정 저장 */
esp_err_t storage_save_settings(const storage_settings_t *settings);

/** @brief 기본값으로 초기화 */
void storage_settings_defaults(storage_settings_t *out);

/** @brief 프리셋 뱅크 로드 (없으면 count=0) */
esp_err_t storage_load_presets(preset_bank_t *out);

/** @brief 프리셋 뱅크 저장 */
esp_err_t storage_save_presets(const preset_bank_t *bank);

/** @brief ATS 결과 뱅크 로드 */
esp_err_t storage_load_ats(ats_bank_t *out);

/** @brief ATS 결과 뱅크 저장 */
esp_err_t storage_save_ats(const ats_bank_t *bank);

/** @brief 확장 설정 기본값 */
void storage_ext_defaults(storage_ext_t *out);

/** @brief 확장 설정 로드 (없으면 기본값) */
esp_err_t storage_load_ext(storage_ext_t *out);

/** @brief 확장 설정 저장 */
esp_err_t storage_save_ext(const storage_ext_t *ext);

/** @brief 스플래시 설정 로드 (없으면 기본값) */
esp_err_t storage_load_splash(storage_splash_t *out);
/** @brief 스플래시 설정 저장 */
esp_err_t storage_save_splash(const storage_splash_t *in);
esp_err_t storage_load_quick(storage_quick_t *out);
esp_err_t storage_save_quick(const storage_quick_t *in);
esp_err_t storage_load_web_auth(char *user, size_t un, char *pass, size_t pn);
esp_err_t storage_save_web_auth(const char *user, const char *pass);
esp_err_t storage_load_web_auth_on(bool *on);
esp_err_t storage_save_web_auth_on(bool on);
esp_err_t storage_load_clock(int64_t *epoch);
esp_err_t storage_save_clock(int64_t epoch);
esp_err_t storage_load_batt_cal(int16_t *offset_mv);
esp_err_t storage_save_batt_cal(int16_t offset_mv);
esp_err_t storage_load_setup_done(bool *done);
esp_err_t storage_save_setup_done(bool done);

/** @brief 날씨 지역 문자열 로드 */
esp_err_t storage_load_weather_location(char *out, size_t n);
/** @brief 날씨 지역 문자열 저장 */
esp_err_t storage_save_weather_location(const char *location);

/** @brief NVS 전체 삭제 (공장 초기화) */
esp_err_t storage_factory_reset(void);

#ifdef __cplusplus
}
#endif
