/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * NVS ?/?????????????????.
 */
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "storage.h"

static const char *TAG = "storage";

#define STORAGE_PART_NAME "settings"
static const char *s_part = NULL;   /* NULL = ?????? "nvs" ???????*/

static esp_err_t store_open(nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (s_part) {
        return nvs_open_from_partition(s_part, STORAGE_NS, mode, h);
    }
    return nvs_open(STORAGE_NS, mode, h);
}

#define KEY_SETTINGS "settings"
#define KEY_PRESETS  "presets"
#define KEY_ATS      "atslist"
#define KEY_EXT      "rxext"
#define KEY_WXLOC    "wxloc"
#define KEY_SPLASH   "splash"
#define KEY_WEB_USER "webuser"
#define KEY_WEB_PASS "webpass"
#define KEY_WEB_AUTH "webauth"
#define KEY_CLOCK    "clock"
#define KEY_BATTCAL  "battcal"
#define KEY_SETUP    "setup"
#define KEY_QUICK    "quickcfg"

void storage_settings_defaults(storage_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic       = STORAGE_MAGIC;
    out->reserved[1] = 1;          /* UI mode: standard */
    out->freq_hz     = 97500000;   /* FM ?????? */
    out->band_index  = 0;
    out->mode        = 0;          /* RADIO_MODE_FM */
    out->volume      = 40;
    out->brightness  = 100;
    out->flags       = 0x01;       /* RDS on */
    out->deemphasis  = 0;          /* 50us */
    out->sleep_min   = 0;
    out->bw[0]       = 0;          /* FM: auto */
    out->bw[1]       = 2;          /* AM: 3 kHz */
    out->bw[2]       = 1;          /* SSB: 2.2 kHz */
    out->step_idx[0] = 0;          /* FM: 100 kHz */
    out->step_idx[1] = -1;
    out->step_idx[2] = -1;
    out->bfo_hz      = 0;
}

esp_err_t storage_init(void)
{
    /* ?????? "nvs" ?(WiFi/BT ?????????? */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS ????? ????? (%s)", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }

    /* ?(DES)??? ???????????"settings" ?????????? ????????????????????*/
    const esp_partition_t *sp = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
        STORAGE_PART_NAME);
    if (sp) {
        esp_err_t e = nvs_flash_init_partition(STORAGE_PART_NAME);
        if (e == ESP_ERR_NVS_NO_FREE_PAGES ||
            e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "settings ???????????? (%s)", esp_err_to_name(e));
            nvs_flash_erase_partition(STORAGE_PART_NAME);
            e = nvs_flash_init_partition(STORAGE_PART_NAME);
        }
        if (e == ESP_OK) {
            s_part = STORAGE_PART_NAME;
            ESP_LOGI(TAG, "NVS ??????? settings (???????????? ??????");
        } else {
            ESP_LOGW(TAG, "settings init ????? (%s) ???????? nvs ?????",
                     esp_err_to_name(e));
        }
    } else {
        ESP_LOGW(TAG, "settings ???????????? ???????? nvs ?????");
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized");
    }
    return err;
}

esp_err_t storage_load_settings(storage_settings_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    storage_settings_defaults(out);

    nvs_handle_t handle;
    esp_err_t err = store_open(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "????? ?????, ??????????????");
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t raw[256];
    size_t len = sizeof(raw);
    err = nvs_get_blob(handle, KEY_SETTINGS, raw, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "????? ?/?????, ??????????????");
        return ESP_ERR_NOT_FOUND;
    }

    if (len == sizeof(storage_settings_t)) {
        storage_settings_t tmp;
        memcpy(&tmp, raw, sizeof(tmp));
        if (tmp.magic != STORAGE_MAGIC) {
            ESP_LOGW(TAG, "bad magic");
            return ESP_ERR_NOT_FOUND;
        }
        *out = tmp;
    } else if (len == sizeof(storage_settings_v2_t)) {
        storage_settings_v2_t old;
        memcpy(&old, raw, sizeof(old));
        if (old.magic != STORAGE_MAGIC_V2) {
            return ESP_ERR_NOT_FOUND;
        }
        out->freq_hz    = old.freq_hz;
        out->band_index = old.band_index;
        out->mode       = old.mode;
        out->volume     = old.volume;
        out->brightness = old.brightness;
        out->flags      = old.flags;
        out->deemphasis = old.deemphasis;
        out->sleep_min  = old.sleep_min;
        out->bfo_hz     = old.bfo_hz;
        memcpy(out->bw, old.bw, 3);
        memcpy(out->step_idx, old.step_idx, 3);
        ESP_LOGI(TAG, "v2 -> v3 migration");
    } else if (len == sizeof(storage_settings_v1_t)) {
        storage_settings_v1_t old;
        memcpy(&old, raw, sizeof(old));
        if (old.magic != STORAGE_MAGIC_V1) {
            return ESP_ERR_NOT_FOUND;
        }
        storage_settings_defaults(out);
        out->freq_hz     = old.freq_hz;
        out->band_index  = old.band_index;
        out->mode        = old.mode;
        out->volume      = old.volume;
        out->brightness  = old.brightness;
        out->bfo_hz      = old.bfo_hz;
        out->bw[0]       = old.reserved[0];
        out->bw[1]       = old.reserved[1];
        out->bw[2]       = old.reserved[2];
        for (int i = 0; i < 3; ++i) {
            out->step_idx[i] = (old.reserved[3 + i] < 16)
                               ? (int8_t)old.reserved[3 + i] : -1;
        }
        ESP_LOGI(TAG, "v1 -> v3 migration");
    } else {
        ESP_LOGW(TAG, "????? ????? ?(%u)", (unsigned)len);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "????? ??????: freq=%lu Hz band=%u mode=%u vol=%u",
             (unsigned long)out->freq_hz, out->band_index, out->mode, out->volume);
    return ESP_OK;
}

esp_err_t storage_save_settings(const storage_settings_t *settings)
{
    ESP_RETURN_ON_FALSE(settings, ESP_ERR_INVALID_ARG, TAG, "null settings");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");

    storage_settings_t tmp = *settings;
    tmp.magic = STORAGE_MAGIC;
    esp_err_t err = nvs_set_blob(handle, KEY_SETTINGS, &tmp, sizeof(tmp));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "????? ?????????: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_load_presets(preset_bank_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    memset(out, 0, sizeof(*out));

    nvs_handle_t handle;
    esp_err_t err = store_open(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, KEY_PRESETS, out, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*out)) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_NOT_FOUND;
    }
    if (out->count > STORAGE_PRESET_SLOTS) {
        out->count = STORAGE_PRESET_SLOTS;
    }
    return ESP_OK;
}

esp_err_t storage_save_presets(const preset_bank_t *bank)
{
    ESP_RETURN_ON_FALSE(bank, ESP_ERR_INVALID_ARG, TAG, "null bank");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(handle, KEY_PRESETS, bank, sizeof(*bank));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_ats(ats_bank_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    memset(out, 0, sizeof(*out));

    nvs_handle_t handle;
    esp_err_t err = store_open(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, KEY_ATS, out, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*out)) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_NOT_FOUND;
    }
    if (out->count > STORAGE_ATS_SLOTS) {
        out->count = STORAGE_ATS_SLOTS;
    }
    return ESP_OK;
}

esp_err_t storage_save_ats(const ats_bank_t *bank)
{
    ESP_RETURN_ON_FALSE(bank, ESP_ERR_INVALID_ARG, TAG, "null bank");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(handle, KEY_ATS, bank, sizeof(*bank));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void storage_ext_defaults(storage_ext_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic        = STORAGE_MAGIC_EXT;
    out->agc_on       = 1;
    out->soft_mute    = 0;
    out->avc_gain     = 63;
    out->squelch_on   = 0;
    out->wifi_mode    = 0;
    out->utc_min      = 0;
    out->batt_view    = 3;
    out->screen_off   = 0;
    out->unit_mode    = 1;   /* default kHz */
    memset(out->wifi_ssid, 0, sizeof(out->wifi_ssid));
    memset(out->wifi_pass, 0, sizeof(out->wifi_pass));
    for (int i = 0; i < 4; ++i) {
        out->squelch_type[i] = 0;
        out->squelch_thr[i] = 4;
    }
}

esp_err_t storage_load_ext(storage_ext_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    storage_ext_defaults(out);

    nvs_handle_t handle;
    esp_err_t err = store_open(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, KEY_EXT, out, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*out) || out->magic != STORAGE_MAGIC_EXT) {
        storage_ext_defaults(out);
        return ESP_ERR_NOT_FOUND;
    }
    if (out->soft_mute > 63)   { out->soft_mute = 0; }
    if (out->avc_gain > 63)    { out->avc_gain = 63; }
    for (int i = 0; i < 4; ++i) {
        if (out->squelch_type[i] > 1) { out->squelch_type[i] = 0; }
    }
    out->wifi_ssid[sizeof(out->wifi_ssid) - 1] = '\0';
    out->wifi_pass[sizeof(out->wifi_pass) - 1] = '\0';
    if (out->utc_min < -720 || out->utc_min > 840) {
        out->utc_min = 0;
    }
    if (out->batt_view > 4) {
        out->batt_view = 3;
    }
    if (out->screen_off > 120) {
        out->screen_off = 0;
    }
    if (out->unit_mode > 1) {
        out->unit_mode = 0;
    }
    return ESP_OK;
}

esp_err_t storage_save_ext(const storage_ext_t *ext)
{
    ESP_RETURN_ON_FALSE(ext, ESP_ERR_INVALID_ARG, TAG, "null ext");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    storage_ext_t tmp = *ext;
    tmp.magic = STORAGE_MAGIC_EXT;
    esp_err_t err = nvs_set_blob(handle, KEY_EXT, &tmp, sizeof(tmp));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_splash(storage_splash_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    memset(out, 0, sizeof(*out));
    out->magic = STORAGE_MAGIC_SPLASH;

    nvs_handle_t handle;
    esp_err_t err = store_open(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, KEY_SPLASH, out, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*out) ||
        out->magic != STORAGE_MAGIC_SPLASH) {
        memset(out, 0, sizeof(*out));
        out->magic = STORAGE_MAGIC_SPLASH;
        return ESP_ERR_NOT_FOUND;
    }
    out->callsign[sizeof(out->callsign) - 1] = '\0';
    out->welcome[sizeof(out->welcome) - 1] = '\0';
    if (out->on > 1) { out->on = 0; }
    if (out->mode > 1) { out->mode = 0; }
    return ESP_OK;
}

esp_err_t storage_save_splash(const storage_splash_t *in)
{
    ESP_RETURN_ON_FALSE(in, ESP_ERR_INVALID_ARG, TAG, "null in");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG,
                        "nvs open");
    storage_splash_t tmp = *in;
    tmp.magic = STORAGE_MAGIC_SPLASH;
    esp_err_t err = nvs_set_blob(handle, KEY_SPLASH, &tmp, sizeof(tmp));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_quick(storage_quick_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");
    memset(out, 0, sizeof(*out));
    out->magic = STORAGE_MAGIC_QUICK;
    out->mask = 0x000F;

    nvs_handle_t handle;
    esp_err_t err = store_open(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = sizeof(*out);
    err = nvs_get_blob(handle, KEY_QUICK, out, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*out) ||
        out->magic != STORAGE_MAGIC_QUICK) {
        memset(out, 0, sizeof(*out));
        out->magic = STORAGE_MAGIC_QUICK;
        out->mask = 0x000F;
        return ESP_ERR_NOT_FOUND;
    }
    out->mask &= 0x7FFF;
    if (out->mask == 0) {
        out->mask = 0x000F;
    }
    return ESP_OK;
}

esp_err_t storage_save_quick(const storage_quick_t *in)
{
    ESP_RETURN_ON_FALSE(in, ESP_ERR_INVALID_ARG, TAG, "null in");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    storage_quick_t tmp = *in;
    tmp.magic = STORAGE_MAGIC_QUICK;
    esp_err_t err = nvs_set_blob(handle, KEY_QUICK, &tmp, sizeof(tmp));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_weather_location(char *out, size_t n){    ESP_RETURN_ON_FALSE(out && n > 0, ESP_ERR_INVALID_ARG, TAG, "bad out");
    out[0] = '\0';
    nvs_handle_t handle;
    esp_err_t err = store_open(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = n;
    err = nvs_get_str(handle, KEY_WXLOC, out, &len);
    nvs_close(handle);
    return err;
}

esp_err_t storage_save_weather_location(const char *location)
{
    ESP_RETURN_ON_FALSE(location, ESP_ERR_INVALID_ARG, TAG, "null location");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_str(handle, KEY_WXLOC, location);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_web_auth(char *user, size_t un, char *pass, size_t pn)
{
    ESP_RETURN_ON_FALSE(user && pass, ESP_ERR_INVALID_ARG, TAG, "null auth");
    nvs_handle_t handle;
    if (store_open(NVS_READONLY, &handle) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t lu = un;
    esp_err_t e1 = nvs_get_str(handle, KEY_WEB_USER, user, &lu);
    size_t lp = pn;
    esp_err_t e2 = nvs_get_str(handle, KEY_WEB_PASS, pass, &lp);
    nvs_close(handle);
    if (e1 != ESP_OK || e2 != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t storage_save_web_auth(const char *user, const char *pass)
{
    ESP_RETURN_ON_FALSE(user && pass, ESP_ERR_INVALID_ARG, TAG, "null auth");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_str(handle, KEY_WEB_USER, user);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, KEY_WEB_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_web_auth_on(bool *on)
{
    ESP_RETURN_ON_FALSE(on, ESP_ERR_INVALID_ARG, TAG, "null auth flag");
    *on = false;
    nvs_handle_t handle;
    if (store_open(NVS_READONLY, &handle) != ESP_OK) {
        return ESP_OK;
    }
    uint8_t v = 0;
    if (nvs_get_u8(handle, KEY_WEB_AUTH, &v) != ESP_OK) {
        v = 0;
    }
    *on = v != 0;
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t storage_save_web_auth_on(bool on)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_u8(handle, KEY_WEB_AUTH, on ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_clock(int64_t *epoch)
{
    ESP_RETURN_ON_FALSE(epoch, ESP_ERR_INVALID_ARG, TAG, "null clock");
    nvs_handle_t handle;
    if (store_open(NVS_READONLY, &handle) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = nvs_get_i64(handle, KEY_CLOCK, epoch);
    nvs_close(handle);
    return err;
}

esp_err_t storage_save_clock(int64_t epoch)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_i64(handle, KEY_CLOCK, epoch);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_batt_cal(int16_t *offset_mv)
{
    ESP_RETURN_ON_FALSE(offset_mv, ESP_ERR_INVALID_ARG, TAG, "null cal");
    *offset_mv = 0;
    nvs_handle_t handle;
    if (store_open(NVS_READONLY, &handle) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = nvs_get_i16(handle, KEY_BATTCAL, offset_mv);
    nvs_close(handle);
    if (err != ESP_OK) {
        *offset_mv = 0;
        return ESP_ERR_NOT_FOUND;
    }
    if (*offset_mv < -1000) {
        *offset_mv = -1000;
    } else if (*offset_mv > 1000) {
        *offset_mv = 1000;
    }
    return ESP_OK;
}

esp_err_t storage_save_batt_cal(int16_t offset_mv)
{
    if (offset_mv < -1000) {
        offset_mv = -1000;
    } else if (offset_mv > 1000) {
        offset_mv = 1000;
    }
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_i16(handle, KEY_BATTCAL, offset_mv);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_setup_done(bool *done)
{
    ESP_RETURN_ON_FALSE(done, ESP_ERR_INVALID_ARG, TAG, "null setup");
    *done = false;
    nvs_handle_t handle;
    if (store_open(NVS_READONLY, &handle) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t v = 0;
    esp_err_t err = nvs_get_u8(handle, KEY_SETUP, &v);
    nvs_close(handle);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    *done = v != 0;
    return ESP_OK;
}

esp_err_t storage_save_setup_done(bool done)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_set_u8(handle, KEY_SETUP, done ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_factory_reset(void)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(store_open(NVS_READWRITE, &handle), TAG, "nvs open");
    esp_err_t err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
