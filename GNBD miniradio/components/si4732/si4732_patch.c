/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * SSB 패치 로딩 (AN332 Rev 0.8 Amendment 기준).
 *
 * 로딩 전략:
 *  1. power_down → POWER_UP FUNC=15 (Library ID 확인)
 *  2. POWER_UP (XOSCEN|PATCH|FUNC=AM)
 *  3. 패치를 8바이트 청크로 I²C 전송, 청크마다 CTS 확인
 *  4. SSB_MODE/BFO 설정 후 AM_TUNE_FREQ로 튜닝
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

#include "si4732.h"
#include "si4732_cmd.h"
#include "si4732_prop.h"
#include "si4732_patch.h"
#include "si4732_priv.h"

static const char *TAG = "si4732_patch";

#define PATCH_CHUNK_SIZE   8
#define PATCH_SETTLE_MS    50

esp_err_t si4732_query_library_id(si4732_t *dev, si4732_library_id_t *out)
{
    ESP_RETURN_ON_FALSE(dev && out, ESP_ERR_INVALID_ARG, TAG, "invalid args");

    uint8_t args[2] = {
        (uint8_t)(SI4732_POWERUP_XOSCEN | SI4732_POWERUP_FUNC_QUERY),  /* 0x1F */
        SI4732_OPMODE_ANALOG_AUDIO,
    };
    uint8_t raw[7];
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    for (int retry = 0; retry < 5; retry++) {
        err = si4732_command_read(dev, SI4732_CMD_POWER_UP, args, sizeof(args),
                                  raw, sizeof(raw));
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "query library id failed: %s", esp_err_to_name(err));
        return err;
    }
    out->pn          = raw[0];
    out->fw_major    = raw[1];
    out->fw_minor    = raw[2];
    out->chip_rev    = raw[5];
    out->library_id  = raw[6];
    ESP_LOGI(TAG, "library id: PN=0x%02X FW=%u.%u CHIPREV=0x%02X LIB=0x%02X",
             out->pn, out->fw_major, out->fw_minor, out->chip_rev, out->library_id);
    return ESP_OK;
}

esp_err_t si4732_patch_power_up(si4732_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "null dev");
    uint8_t args[2] = {
        (uint8_t)(SI4732_POWERUP_XOSCEN | SI4732_POWERUP_PATCH | SI4732_POWERUP_FUNC_AM),
        SI4732_OPMODE_ANALOG_AUDIO,
    };
    esp_err_t err = si4732_command(dev, SI4732_CMD_POWER_UP, args, sizeof(args));
    if (err != ESP_OK) {
        return err;
    }
    dev->func = SI4732_FUNC_AM;
    dev->ssb = SI4732_SSB_OFF;
    vTaskDelay(pdMS_TO_TICKS(PATCH_SETTLE_MS));
    return ESP_OK;
}

esp_err_t si4732_load_ssb_patch(si4732_t *dev, const uint8_t *data, size_t size)
{
    ESP_RETURN_ON_FALSE(dev && data && size > 0, ESP_ERR_INVALID_ARG, TAG, "invalid args");

    uint8_t buf[PATCH_CHUNK_SIZE];
    for (size_t off = 0; off < size; off += PATCH_CHUNK_SIZE) {
        size_t n = size - off;
        if (n > PATCH_CHUNK_SIZE) {
            n = PATCH_CHUNK_SIZE;
        }
        memcpy(buf, data + off, n);
        if (n < PATCH_CHUNK_SIZE) {
            memset(buf + n, 0, PATCH_CHUNK_SIZE - n);
        }

        /* 원본 청크 그대로 전송 (첫 바이트가 0x15/0x16) */
        esp_err_t err = si4732_write(dev, buf, PATCH_CHUNK_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "chunk write failed at %u: %s",
                     (unsigned)off, esp_err_to_name(err));
            return err;
        }
        err = si4732_wait_cts(dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "chunk CTS failed at %u: %s",
                     (unsigned)off, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "patch loaded (%u bytes)", (unsigned)size);
    return ESP_OK;
}

esp_err_t si4732_set_ssb_mode(si4732_t *dev, si4732_ssb_bandwidth_t bw,
                              bool avc_en, bool smute_snr, bool dsp_afc_dis)
{
    uint16_t v = ((uint16_t)bw & 0x000F)      /* AUDIOBW   [3:0]  */
               | (0x0u << 4)                    /* SBCUTFLT  [7:4]  기본 */
               | (0x0u << 8)                    /* AVC_DIVIDER [11:8] SSB=0 */
               | (avc_en     ? (1u << 12) : 0)  /* AVCEN     [12]   */
               | (smute_snr  ? (1u << 13) : 0)  /* SMUTESEL  [13]   */
               | (0u << 14)                     /* DUMMY     [14] = 0 */
               | (dsp_afc_dis ? (1u << 15) : 0);/* DSP_AFCDIS [15]  */
    return si4732_set_property(dev, SI4732_PROP_SSB_MODE, v);
}

esp_err_t si4732_set_bfo(si4732_t *dev, int16_t offset_hz)
{
    ESP_RETURN_ON_FALSE(offset_hz >= -16383 && offset_hz <= 16383,
                        ESP_ERR_INVALID_ARG, TAG, "BFO out of range: %d", (int)offset_hz);
    return si4732_set_property(dev, SI4732_PROP_SSB_BFO, (uint16_t)offset_hz);
}

esp_err_t si4732_ssb_prepare(si4732_t *dev, si4732_ssb_bandwidth_t bw)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "null dev");
    if (si4732_ssb_patch_size == 0) {
        ESP_LOGW(TAG, "SSB patch data 없음 (Phase 2 미구현) — SSB 모드 생략");
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = si4732_power_down(dev);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    err = si4732_patch_power_up(dev);
    if (err != ESP_OK) {
        return err;
    }
    err = si4732_load_ssb_patch(dev, si4732_ssb_patch, si4732_ssb_patch_size);
    if (err != ESP_OK) {
        si4732_power_down(dev);
        return err;
    }
    err = si4732_set_ssb_mode(dev, bw, true, false, true);
    if (err != ESP_OK) {
        si4732_power_down(dev);
        return err;
    }
    ESP_LOGI(TAG, "SSB 준비 완료");
    return ESP_OK;
}
