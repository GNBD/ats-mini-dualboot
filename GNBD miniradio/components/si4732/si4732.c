/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * SI4732 ?�?��? ?�라?�버 구현: I²C ?�레?? 명령/?�답, ?�닝, ?�로?�티.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "si4732.h"
#include "si4732_cmd.h"
#include "si4732_prop.h"
#include "si4732_priv.h"

static const char *TAG = "si4732";

#define CTS_POLL_INTERVAL_MS   1
#define TUNE_SETTLE_MS         30   /* ?�닝 ???�디???��?�?*/
#define POWERUP_SETTLE_MS      500   /* ?�리?�탈 ?�정??*/

/* ---------------------------------------------------------------- init */

esp_err_t si4732_init(si4732_t *dev, i2c_master_bus_handle_t bus, const si4732_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(dev && bus && cfg, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    memset(dev, 0, sizeof(*dev));
    dev->bus = bus;
    dev->timeout_ms = cfg->timeout_ms ? cfg->timeout_ms : 200;
    dev->func = SI4732_FUNC_FM;
    dev->ssb = SI4732_SSB_OFF;

    /* ?��? RESET ?�스 (PU2CLR reset()�??�일 ?�?�밍) */
    if (cfg->reset_gpio >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->reset_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "reset gpio config failed");
        gpio_set_level(cfg->reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cfg->reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* SEN 배선???�라 주소가 ?�르므�?probe�??��? */
    uint8_t addr = cfg->i2c_addr;
    if (!addr) {
        if (i2c_master_probe(bus, SI4732_I2C_ADDR_DEFAULT, 50) == ESP_OK) {
            addr = SI4732_I2C_ADDR_DEFAULT;
        } else if (i2c_master_probe(bus, SI4732_I2C_ADDR_ALT, 50) == ESP_OK) {
            addr = SI4732_I2C_ADDR_ALT;
        } else {
            ESP_LOGE(TAG, "SI4732 not detected (0x11/0x63 no ACK ??RADIO_EN/RST/SDA/SCL ?�인)");
            return ESP_ERR_NOT_FOUND;
        }
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = cfg->i2c_hz ? cfg->i2c_hz : 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &dev->i2c),
                        TAG, "i2c add device failed");
    dev->addr = addr;

    ESP_LOGI(TAG, "init ok (addr=0x%02X, %lu Hz)",
             (unsigned)addr, (unsigned long)(cfg->i2c_hz ? cfg->i2c_hz : 100000));
    return ESP_OK;
}

esp_err_t si4732_deinit(si4732_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "null dev");
    if (dev->i2c) {
        i2c_master_bus_rm_device(dev->i2c);
        dev->i2c = NULL;
    }
    return ESP_OK;
}

esp_err_t si4732_bus_reset(si4732_t *dev)
{
    ESP_RETURN_ON_FALSE(dev && dev->bus, ESP_ERR_INVALID_ARG, TAG, "null dev/bus");
    ESP_LOGW(TAG, "I2C bus reset");
    if (dev->i2c) {
        i2c_master_bus_rm_device(dev->i2c);
        dev->i2c = NULL;
    }
    esp_err_t err = i2c_master_bus_reset(dev->bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus reset failed: %s", esp_err_to_name(err));
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = dev->addr ? dev->addr : SI4732_I2C_ADDR_DEFAULT,
        .scl_speed_hz    = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(dev->bus, &dev_cfg, &dev->i2c),
                        TAG, "re-add device failed");
    return ESP_OK;
}

/* ------------------------------------------------------- ?�?��? ?�틸 */

esp_err_t si4732_wait_cts(si4732_t *dev)
{
    uint8_t status = 0;
    for (uint32_t t = 0; t < dev->timeout_ms; t++) {
        ESP_RETURN_ON_ERROR(i2c_master_receive(dev->i2c, &status, 1, dev->timeout_ms),
                            TAG, "i2c status read failed");
        if (status & SI4732_STATUS_CTS) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(CTS_POLL_INTERVAL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t si4732_write(si4732_t *dev, const uint8_t *buf, size_t len)
{
    return i2c_master_transmit(dev->i2c, buf, len, dev->timeout_ms);
}

esp_err_t si4732_command(si4732_t *dev, uint8_t cmd, const uint8_t *args, size_t args_len)
{
    uint8_t buf[16];
    ESP_RETURN_ON_FALSE(args_len + 1 <= sizeof(buf), ESP_ERR_INVALID_SIZE,
                        TAG, "args too long (%u)", (unsigned)args_len);
    buf[0] = cmd;
    if (args_len) {
        memcpy(&buf[1], args, args_len);
    }

    esp_err_t err = si4732_wait_cts(dev);
    if (err != ESP_OK) {
        return err;
    }
    err = si4732_write(dev, buf, args_len + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cmd 0x%02X write failed: %s", cmd, esp_err_to_name(err));
        return err;
    }
    return si4732_wait_cts(dev);
}

esp_err_t si4732_read_response(si4732_t *dev, uint8_t *payload, size_t payload_len)
{
    uint8_t buf[20];
    ESP_RETURN_ON_FALSE(1 + payload_len <= sizeof(buf), ESP_ERR_INVALID_SIZE,
                        TAG, "payload too long (%u)", (unsigned)payload_len);

    ESP_RETURN_ON_ERROR(i2c_master_receive(dev->i2c, buf, 1 + payload_len, dev->timeout_ms),
                        TAG, "i2c response read failed");
    if (!(buf[0] & SI4732_STATUS_CTS)) {
        return ESP_ERR_TIMEOUT;
    }
    if (buf[0] & SI4732_STATUS_ERR) {
        ESP_LOGD(TAG, "response ERR (status=0x%02X)", buf[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (payload_len) {
        memcpy(payload, &buf[1], payload_len);
    }
    return ESP_OK;
}

esp_err_t si4732_command_read(si4732_t *dev, uint8_t cmd,
                              const uint8_t *args, size_t args_len,
                              uint8_t *payload, size_t payload_len)
{
    esp_err_t err = si4732_command(dev, cmd, args, args_len);
    if (err != ESP_OK) {
        return err;
    }
    return si4732_read_response(dev, payload, payload_len);
}

/* ------------------------------------------------------------- 부??*/

esp_err_t si4732_power_up(si4732_t *dev, si4732_func_t func)
{
    uint8_t args[2] = {
        (uint8_t)(SI4732_POWERUP_XOSCEN | ((uint8_t)func & 0x0F)),
        SI4732_OPMODE_ANALOG_AUDIO,
    };
    esp_err_t err = si4732_command(dev, SI4732_CMD_POWER_UP, args, sizeof(args));
    if (err != ESP_OK) {
        return err;
    }
    dev->func = func;
    dev->ssb = SI4732_SSB_OFF;
    vTaskDelay(pdMS_TO_TICKS(POWERUP_SETTLE_MS));
    return ESP_OK;
}

esp_err_t si4732_power_down(si4732_t *dev)
{
    return si4732_command(dev, SI4732_CMD_POWER_DOWN, NULL, 0);
}

esp_err_t si4732_get_rev(si4732_t *dev, si4732_rev_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    uint8_t raw[8];
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    for (int retry = 0; retry < 3; retry++) {
        err = si4732_command_read(dev, SI4732_CMD_GET_REV, NULL, 0, raw, sizeof(raw));
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (err != ESP_OK) {
        return err;
    }
    out->pn          = raw[0];
    out->fw_major    = raw[1];
    out->fw_minor    = raw[2];
    out->patch_major = raw[3];
    out->patch_minor = raw[4];
    out->chip_rev    = raw[7];
    return ESP_OK;
}

/* --------------------------------------------------------- ?�로?�티 */

esp_err_t si4732_set_property(si4732_t *dev, uint16_t prop, uint16_t value)
{
    uint8_t args[5] = {
        0x00,                    /* ARG1: ??�� 0 */
        (uint8_t)(prop >> 8),
        (uint8_t)(prop & 0xFF),
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
    };
    return si4732_command(dev, SI4732_CMD_SET_PROPERTY, args, sizeof(args));
}

esp_err_t si4732_get_property(si4732_t *dev, uint16_t prop, uint16_t *value)
{
    ESP_RETURN_ON_FALSE(value, ESP_ERR_INVALID_ARG, TAG, "null value");
    uint8_t args[3] = { 0x00, (uint8_t)(prop >> 8), (uint8_t)(prop & 0xFF) };
    uint8_t resp[3];
    esp_err_t err = si4732_command_read(dev, SI4732_CMD_GET_PROPERTY,
                                        args, sizeof(args), resp, sizeof(resp));
    if (err != ESP_OK) {
        return err;
    }
    /* resp[0] = reserved, resp[1] = DATAH, resp[2] = DATAL */
    *value = ((uint16_t)resp[1] << 8) | resp[2];
    return ESP_OK;
}

/* ------------------------------------------------------------- ?�닝 */

esp_err_t si4732_fm_tune(si4732_t *dev, uint32_t freq_hz)
{
    uint32_t units = (freq_hz + 5000) / 10000;  /* 10 kHz ?�위 반올�?*/
    ESP_RETURN_ON_FALSE(units >= 6400 && units <= 10800, ESP_ERR_INVALID_ARG,
                        TAG, "FM out of range: %lu Hz", (unsigned long)freq_hz);
    uint8_t args[4] = {
        0x00,
        (uint8_t)(units >> 8),
        (uint8_t)(units & 0xFF),
        0x00,                    /* ANTCAP auto */
    };
    esp_err_t err = si4732_command(dev, SI4732_CMD_FM_TUNE_FREQ, args, sizeof(args));
    if (err != ESP_OK) {
        return err;
    }
    dev->func = SI4732_FUNC_FM;
    dev->ssb = SI4732_SSB_OFF;
    vTaskDelay(pdMS_TO_TICKS(TUNE_SETTLE_MS));
    return ESP_OK;
}

esp_err_t si4732_am_tune(si4732_t *dev, uint32_t freq_hz, si4732_sideband_t sb)
{
    uint32_t units = (freq_hz + 500) / 1000;   /* 1 kHz ?�위 반올�?*/
    ESP_RETURN_ON_FALSE(units >= 149 && units <= 30000, ESP_ERR_INVALID_ARG,
                        TAG, "AM/SSB out of range: %lu Hz", (unsigned long)freq_hz);
    ESP_RETURN_ON_FALSE(sb <= SI4732_SSB_USB, ESP_ERR_INVALID_ARG,
                        TAG, "invalid sideband %d", (int)sb);

    /* ARG1: bit0 FAST, bit7:6 USBLSB (01=LSB, 10=USB) */
    uint8_t arg1 = (uint8_t)(((uint8_t)sb << 6));
    uint8_t args[5] = {
        arg1,
        (uint8_t)(units >> 8),
        (uint8_t)(units & 0xFF),
        0x00,                    /* ANTCAPH: auto */
        0x00,                    /* ANTCAPL: auto */
    };
    esp_err_t err = si4732_command(dev, SI4732_CMD_AM_TUNE_FREQ, args, sizeof(args));
    if (err != ESP_OK) {
        return err;
    }
    dev->func = SI4732_FUNC_AM;
    dev->ssb = sb;
    vTaskDelay(pdMS_TO_TICKS(TUNE_SETTLE_MS));
    return ESP_OK;
}

esp_err_t si4732_fm_tune_fast(si4732_t *dev, uint32_t freq_hz)
{
    uint32_t units = (freq_hz + 5000) / 10000;
    ESP_RETURN_ON_FALSE(units >= 6400 && units <= 10800, ESP_ERR_INVALID_ARG,
                        TAG, "FM out of range");
    uint8_t args[4] = {
        0x00,
        (uint8_t)(units >> 8),
        (uint8_t)(units & 0xFF),
        0x00,
    };
    esp_err_t err = si4732_command(dev, SI4732_CMD_FM_TUNE_FREQ, args,
                                   sizeof(args));
    if (err == ESP_OK) {
        dev->func = SI4732_FUNC_FM;
        dev->ssb = SI4732_SSB_OFF;
    }
    return err;
}

esp_err_t si4732_am_tune_fast(si4732_t *dev, uint32_t freq_hz,
                              si4732_sideband_t sb)
{
    uint32_t units = (freq_hz + 500) / 1000;
    ESP_RETURN_ON_FALSE(units >= 149 && units <= 30000, ESP_ERR_INVALID_ARG,
                        TAG, "AM out of range");
    uint8_t arg1 = (uint8_t)(((uint8_t)sb << 6));
    uint8_t args[5] = {
        arg1,
        (uint8_t)(units >> 8),
        (uint8_t)(units & 0xFF),
        0x00,
        0x00,
    };
    esp_err_t err = si4732_command(dev, SI4732_CMD_AM_TUNE_FREQ, args,
                                   sizeof(args));
    if (err == ESP_OK) {
        dev->func = SI4732_FUNC_AM;
        dev->ssb = sb;
    }
    return err;
}

esp_err_t si4732_seek_start(si4732_t *dev, bool up,
                            uint32_t bottom_hz, uint32_t top_hz,
                            uint32_t spacing_hz,
                            uint8_t rssi_threshold, uint8_t snr_threshold)
{
    bool fm = (dev->func == SI4732_FUNC_FM);
    uint32_t div = fm ? 10000u : 1000u;
    uint32_t bottom = (bottom_hz + div / 2u) / div;
    uint32_t top = (top_hz + div / 2u) / div;
    uint32_t spacing = (spacing_hz + div / 2u) / div;
    if (spacing < 1u) {
        spacing = 1u;
    }
    if (rssi_threshold > 127u) {
        rssi_threshold = 127u;
    }
    if (snr_threshold > 127u) {
        snr_threshold = 127u;
    }

    esp_err_t err;
    if (fm) {
        if (bottom < 6400u || top > 10800u || bottom >= top) {
            return ESP_ERR_INVALID_ARG;
        }
        err = si4732_set_property(dev, SI4732_PROP_FM_SEEK_BAND_BOTTOM,
                                  (uint16_t)bottom);
        if (err == ESP_OK) {
            err = si4732_set_property(dev, SI4732_PROP_FM_SEEK_BAND_TOP,
                                      (uint16_t)top);
        }
        if (err == ESP_OK) {
            err = si4732_set_property(dev, SI4732_PROP_FM_SEEK_FREQ_SPACING,
                                      (uint16_t)spacing);
        }
        if (err == ESP_OK) {
            err = si4732_set_property(dev,
                                      SI4732_PROP_FM_SEEK_TUNE_RSSI_THRESHOLD,
                                      rssi_threshold);
        }
        if (err == ESP_OK) {
            err = si4732_set_property(dev,
                                      SI4732_PROP_FM_SEEK_TUNE_SNR_THRESHOLD,
                                      snr_threshold);
        }
        if (err != ESP_OK) {
            return err;
        }
        uint8_t arg = (uint8_t)(up ? SI4732_SEEK_UP : 0);
        return si4732_command(dev, SI4732_CMD_FM_SEEK_START, &arg, 1);
    }

    if (bottom < 149u || top > 30000u || bottom >= top) {
        return ESP_ERR_INVALID_ARG;
    }
    err = si4732_set_property(dev, SI4732_PROP_AM_SEEK_BAND_BOTTOM,
                              (uint16_t)bottom);
    if (err == ESP_OK) {
        err = si4732_set_property(dev, SI4732_PROP_AM_SEEK_BAND_TOP,
                                  (uint16_t)top);
    }
    if (err == ESP_OK) {
        err = si4732_set_property(dev, SI4732_PROP_AM_SEEK_FREQ_SPACING,
                                  (uint16_t)spacing);
    }
    if (err == ESP_OK) {
        err = si4732_set_property(dev, SI4732_PROP_AM_SEEK_RSSI_THRESHOLD,
                                  rssi_threshold);
    }
    if (err == ESP_OK) {
        err = si4732_set_property(dev, SI4732_PROP_AM_SEEK_SNR_THRESHOLD,
                                  snr_threshold);
    }
    if (err != ESP_OK) {
        return err;
    }
    uint8_t args[5] = {
        (uint8_t)(up ? SI4732_SEEK_UP : 0),
        0x00, 0x00, 0x00,
        (uint8_t)(bottom > 1800u ? 1u : 0u),
    };
    return si4732_command(dev, SI4732_CMD_AM_SEEK_START, args, sizeof(args));
}

/* TUNE_STATUS: FM=0x22, AM=0x42. ?�답 8바이??STATUS ?�함) */
esp_err_t si4732_tune_status(si4732_t *dev, bool intack, bool cancel,
                             uint32_t *freq_hz, bool *stc, bool *valid,
                             bool *bltf)
{
    uint8_t cmd = (dev->func == SI4732_FUNC_FM) ? SI4732_CMD_FM_TUNE_STATUS
                                                : SI4732_CMD_AM_TUNE_STATUS;
    uint8_t arg = (uint8_t)((intack ? 0x01 : 0x00) | (cancel ? 0x02 : 0x00));

    esp_err_t err = si4732_command(dev, cmd, &arg, 1);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t buf[8];
    err = i2c_master_receive(dev->i2c, buf, sizeof(buf), dev->timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    if (!(buf[0] & SI4732_STATUS_CTS)) {
        return ESP_ERR_TIMEOUT;
    }
    if (buf[0] & SI4732_STATUS_ERR) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (stc) {
        *stc = (buf[0] & 0x01) != 0;   /* STCINT */
    }
    if (valid) {
        *valid = (buf[1] & 0x01) != 0; /* RESP1 VALID */
    }
    if (bltf) {
        *bltf = (buf[1] & 0x80) != 0;  /* RESP1 BLTF */
    }
    if (freq_hz) {
        uint32_t units = ((uint32_t)buf[2] << 8) | buf[3];   /* READFREQ */
        *freq_hz = (dev->func == SI4732_FUNC_FM) ? units * 10000u : units * 1000u;
    }
    return ESP_OK;
}

esp_err_t si4732_set_fm_bandwidth(si4732_t *dev, uint8_t code)
{
    if (code > SI4732_FM_BW_40KHZ) {
        code = SI4732_FM_BW_AUTO;
    }
    return si4732_set_property(dev, SI4732_PROP_FM_CHANNEL_FILTER, code);
}

esp_err_t si4732_disable_fm_debug(si4732_t *dev)
{
    esp_err_t err = si4732_set_property(dev, SI4732_PROP_FM_DEBUG, 0x0000);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    return err;
}

esp_err_t si4732_set_am_bandwidth(si4732_t *dev, si4732_am_bandwidth_t bw)
{
    if (bw > SI4732_AM_BW_2K5HZ) {
        bw = SI4732_AM_BW_2KHZ;
    }
    return si4732_set_property(dev, SI4732_PROP_AM_CHANNEL_FILTER, (uint16_t)bw);
}

esp_err_t si4732_set_deemphasis(si4732_t *dev, bool us75)
{
    /* AN332: 10 = 75 µs, 01 = 50 µs */
    return si4732_set_property(dev, SI4732_PROP_FM_DEEMPHASIS, us75 ? 2 : 1);
}

esp_err_t si4732_set_stereo(si4732_t *dev, bool force_mono)
{
    if (force_mono) {
        /* 모든 블렌드 임계값을 벗어나게 해 모노 고정 (G8PTN 방식) */
        si4732_set_property(dev, SI4732_PROP_FM_BLEND_RSSI_STEREO_THRESHOLD, 127);
        si4732_set_property(dev, SI4732_PROP_FM_BLEND_RSSI_MONO_THRESHOLD, 127);
        si4732_set_property(dev, SI4732_PROP_FM_BLEND_SNR_STEREO_THRESHOLD, 127);
        si4732_set_property(dev, SI4732_PROP_FM_BLEND_SNR_MONO_THRESHOLD, 127);
        si4732_set_property(dev, SI4732_PROP_FM_BLEND_MULTIPATH_STEREO_THRESHOLD, 0);
        return si4732_set_property(dev,
                                   SI4732_PROP_FM_BLEND_MULTIPATH_MONO_THRESHOLD, 0);
    }
    /* AUTO: AN332 기본 블렌드 임계값 복원 */
    si4732_set_property(dev, SI4732_PROP_FM_BLEND_RSSI_STEREO_THRESHOLD, 49);
    si4732_set_property(dev, SI4732_PROP_FM_BLEND_RSSI_MONO_THRESHOLD, 30);
    si4732_set_property(dev, SI4732_PROP_FM_BLEND_SNR_STEREO_THRESHOLD, 27);
    si4732_set_property(dev, SI4732_PROP_FM_BLEND_SNR_MONO_THRESHOLD, 14);
    si4732_set_property(dev, SI4732_PROP_FM_BLEND_MULTIPATH_STEREO_THRESHOLD, 20);
    return si4732_set_property(dev,
                               SI4732_PROP_FM_BLEND_MULTIPATH_MONO_THRESHOLD, 60);
}

static void parse_rsq(const uint8_t *raw, size_t len, si4732_rsq_t *out)
{
    /* raw[0]=RESP1 ?�터?�트, raw[1]=RESP2, raw[2]=RESP3,
       raw[3]=RSSI, raw[4]=SNR, raw[5]=MULT, raw[6]=FREQOFF */
    out->valid       = (raw[1] & 0x01) != 0;
    out->afc_rail    = (raw[1] & 0x02) != 0;
    out->soft_mute   = (raw[1] & 0x08) != 0;
    out->stereo      = (raw[2] & 0x80) != 0;      /* PILOT */
    out->stereo_blend = (uint8_t)(raw[2] & 0x7F); /* STBLEND % */
    out->rssi        = raw[3];
    out->snr         = (len > 4) ? raw[4] : 0;
    out->freq_offset = (len > 6) ? (int8_t)raw[6] : 0;
}

esp_err_t si4732_fm_rsq(si4732_t *dev, si4732_rsq_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    uint8_t arg = 0x00;               /* INTACK=0 */
    uint8_t raw[7];
    esp_err_t err = si4732_command_read(dev, SI4732_CMD_FM_RSQ_STATUS,
                                        &arg, 1, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    parse_rsq(raw, sizeof(raw), out);
    return ESP_OK;
}

esp_err_t si4732_am_rsq(si4732_t *dev, si4732_rsq_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    uint8_t arg = 0x00;
    uint8_t raw[5];   /* AM_RSQ_STATUS: RESP1..RESP5 */
    esp_err_t err = si4732_command_read(dev, SI4732_CMD_AM_RSQ_STATUS,
                                        &arg, 1, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    parse_rsq(raw, sizeof(raw), out);
    out->stereo = false;
    out->stereo_blend = 0;
    out->freq_offset = 0;
    return ESP_OK;
}

/* --------------------------------------------------------------- RDS */

esp_err_t si4732_rds_enable(si4732_t *dev, bool enable)
{
    return si4732_set_property(dev, SI4732_PROP_FM_RDS_CONFIG,
                               enable ? (SI4732_RDS_CONFIG_ENABLE |
                                         SI4732_RDS_CONFIG_THRESHOLD) : 0);
}

esp_err_t si4732_rds_status(si4732_t *dev, si4732_rds_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    uint8_t arg = 0x00;   /* INTACK=0, MTFIFO=0, STATUSONLY=0 */
    uint8_t raw[12];      /* RESP1..RESP12 */
    esp_err_t err = si4732_command_read(dev, SI4732_CMD_FM_RDS_STATUS,
                                        &arg, 1, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }
    out->recv        = (raw[0] & 0x01) != 0;
    out->sync_lost   = (raw[0] & 0x02) != 0;
    out->sync_found  = (raw[0] & 0x04) != 0;
    out->new_block_a = (raw[0] & 0x10) != 0;
    out->new_block_b = (raw[0] & 0x20) != 0;
    out->synced      = (raw[1] & 0x01) != 0;
    out->group_lost  = (raw[1] & 0x04) != 0;
    out->fifo_used   = raw[2];
    out->block_a     = ((uint16_t)raw[3] << 8) | raw[4];
    out->block_b     = ((uint16_t)raw[5] << 8) | raw[6];
    out->block_c     = ((uint16_t)raw[7] << 8) | raw[8];
    out->block_d     = ((uint16_t)raw[9] << 8) | raw[10];
    out->errors      = raw[11];
    return ESP_OK;
}

/* ------------------------------------------------------------- ?�디??*/

esp_err_t si4732_set_volume(si4732_t *dev, uint8_t volume)
{
    if (volume > 63) {
        volume = 63;
    }
    return si4732_set_property(dev, SI4732_PROP_RX_VOLUME, volume);
}

esp_err_t si4732_set_mute(si4732_t *dev, bool mute)
{
    return si4732_set_property(dev, SI4732_PROP_RX_HARD_MUTE,
                               mute ? SI4732_RX_HARD_MUTE_BOTH
                                    : SI4732_RX_HARD_MUTE_NONE);
}

esp_err_t si4732_set_gpo1(si4732_t *dev, bool level)
{
    /* GPO1 is the antenna switch: 0 = FM, 1 = AM/SSB.
       GPIO_CTL bit1 = GPO1OEN, GPIO_SET bit1 = GPO1LEVEL. */
    uint8_t ctl = 0x02;
    uint8_t lvl = level ? 0x02 : 0x00;
    esp_err_t err = si4732_command(dev, SI4732_CMD_GPIO_CTL, &ctl, 1);
    if (err != ESP_OK) {
        return err;
    }
    return si4732_command(dev, SI4732_CMD_GPIO_SET, &lvl, 1);
}

esp_err_t si4732_set_fm_soft_mute(si4732_t *dev, uint8_t atten_db)
{
    if (atten_db > 63) {
        atten_db = 63;
    }
    return si4732_set_property(dev, SI4732_PROP_FM_SOFT_MUTE_MAX_ATTENUATION,
                               atten_db);
}

esp_err_t si4732_fm_agc_override(si4732_t *dev, bool rf_agc_disable,
                                 uint8_t lna_gain_index)
{
    /* AN332 Command 0x28 FM_AGC_OVERRIDE:
       ARG1 bit0 RFAGCDIS (1 = RF AGC 정지),
       ARG2 bits4:0 LNA_GAIN_INDEX
       (0 = 최소 감쇠/최대 이득, 26 = 최대 감쇠/최소 이득) */
    if (lna_gain_index > 26u) {
        lna_gain_index = 26u;
    }
    uint8_t args[2] = {
        (uint8_t)(rf_agc_disable ? 0x01 : 0x00),
        (uint8_t)(rf_agc_disable ? (lna_gain_index & 0x1Fu) : 0x00),
    };
    return si4732_command(dev, SI4732_CMD_FM_AGC_OVERRIDE, args, sizeof(args));
}

esp_err_t si4732_set_am_soft_mute(si4732_t *dev, uint8_t atten_db)
{
    if (atten_db > 63) {
        atten_db = 63;
    }
    return si4732_set_property(dev, SI4732_PROP_AM_SOFT_MUTE_MAX_ATTENUATION,
                               atten_db);
}

esp_err_t si4732_set_am_agc(si4732_t *dev, bool enable)
{
    /* AN332 Command 0x48 AM_AGC_OVERRIDE: ARG1 bit0=1 이면 AGC 비활성,
       ARG2 = 강제 이득 인덱스(0=최소 감쇠, 37+ATTN_BACKUP=최대 감쇠) */
    uint8_t args[2] = {
        (uint8_t)(enable ? 0x00 : 0x01),
        (uint8_t)(enable ? 0x00 : 18)
    };
    return si4732_command(dev, SI4732_CMD_AM_AGC_OVERRIDE, args, sizeof(args));
}

esp_err_t si4732_set_am_avc_max_gain(si4732_t *dev, uint8_t gain_db)
{
    /* AN332 property 0x3103: Q8.8 고정소수점 (16 dB = 0x1000) */
    if (gain_db > 90) {
        gain_db = 90;
    }
    uint16_t v = (uint16_t)((uint16_t)gain_db * 256u);
    return si4732_set_property(dev,
                               SI4732_PROP_AM_AUTOMATIC_VOLUME_CONTROL_MAX_GAIN,
                               v);
}
