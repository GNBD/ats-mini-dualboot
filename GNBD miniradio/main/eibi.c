/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * EiBi shortwave station database: download, lookup and UI entry points.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_littlefs.h"
#include "app.h"

static const char *TAG = "eibi";

#define EIBI_DIR  "/littlefs"
#define EIBI_BIN  "/littlefs/eibi.bin"
#define EIBI_TMP  "/littlefs/eibi.tmp"

#define EIBI_FILE_VERSION 2u
static const char EIBI_MAGIC[4] = {'E', 'I', 'B', 'I'};

typedef struct __attribute__((packed)) {
    char     magic[4];
    uint16_t version;
    uint16_t rec_size;
} eibi_file_hdr_t;

static bool eibi_header_ok(FILE *f)
{
    eibi_file_hdr_t h;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h)) {
        return false;
    }
    return memcmp(h.magic, EIBI_MAGIC, sizeof(EIBI_MAGIC)) == 0 &&
           h.version == EIBI_FILE_VERSION &&
           h.rec_size == sizeof(eibi_rec_t);
}

esp_err_t eibi_fs_mount(void)
{
    if (s_fs_mounted) {
        return ESP_OK;
    }
    esp_vfs_littlefs_conf_t cfg = {
        .base_path = EIBI_DIR,
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&cfg);
    if (err == ESP_OK) {
        s_fs_mounted = true;
        ESP_LOGI(TAG, "LittleFS mounted");
        size_t tot = 0, used = 0;
        if (esp_littlefs_info("littlefs", &tot, &used) == ESP_OK) {
            ESP_LOGI(TAG, "LittleFS total=%u used=%u", (unsigned)tot,
                     (unsigned)used);
        }
    } else {
        ESP_LOGW(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

bool eibi_available(void)
{
    FILE *f = fopen(EIBI_BIN, "rb");
    if (!f) {
        return false;
    }
    bool ok = eibi_header_ok(f);
    fclose(f);
    return ok;
}

static int eibi_wday(const char *s)
{
    static const char *const names[7] = {
        "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"
    };
    for (int i = 0; i < 7; ++i) {
        if (s[0] == names[i][0] && s[1] == names[i][1]) {
            return i;
        }
    }
    return -1;
}

static int eibi_month(const char *s)
{
    static const char *const names[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    for (int i = 0; i < 12; ++i) {
        if (strncmp(s, names[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static void eibi_parse_days(const char *raw, eibi_rec_t *out)
{
    char d[8];
    size_t n = strlen(raw);
    if (n > 7) {
        n = 7;
    }
    memcpy(d, raw, n);
    d[n] = '\0';

    char *p = d;
    while (*p == ' ') {
        ++p;
    }
    char *q = p + strlen(p);
    while (q > p && q[-1] == ' ') {
        *--q = '\0';
    }

    out->days = 0x7F;
    out->week = 0;
    out->month = 0;
    out->day = 0;
    if (!*p) {
        return;
    }

    size_t len = strlen(p);

    /* Last7 = last Sunday, 1.Fr = first Friday, 24Oct = exact date. */
    if (len == 5 && strncmp(p, "Last", 4) == 0 &&
        p[4] >= '1' && p[4] <= '7') {
        out->days = (uint8_t)(1u << (p[4] - '1'));
        out->week = 0xFF;
        return;
    }
    if (len == 4 && p[1] == '.' && p[0] >= '1' && p[0] <= '5') {
        int wd = eibi_wday(p + 2);
        if (wd >= 0) {
            out->days = (uint8_t)(1u << wd);
            out->week = (uint8_t)(p[0] - '0');
            return;
        }
    }
    if (isdigit((unsigned char)p[0])) {
        char *end = NULL;
        long dd = strtol(p, &end, 10);
        int mm = (end && *end) ? eibi_month(end) : 0;
        if (mm && dd >= 1 && dd <= 31) {
            out->month = (uint8_t)mm;
            out->day = (uint8_t)dd;
            return;
        }
    }

    bool numeric = true;
    uint8_t mask = 0;
    for (size_t i = 0; i < len; ++i) {
        if (p[i] < '1' || p[i] > '7') {
            numeric = false;
            break;
        }
        mask |= (uint8_t)(1u << (p[i] - '1'));
    }
    if (numeric && mask) {
        out->days = mask;
        return;
    }

    bool valid = true;
    mask = 0;
    for (size_t i = 0; i < len;) {
        if (p[i] == ',' || p[i] == ' ') {
            ++i;
            continue;
        }
        int a = eibi_wday(p + i);
        if (a < 0) {
            valid = false;
            break;
        }
        i += 2;
        if (i < len && p[i] == '-') {
            int b = (i + 2 < len) ? eibi_wday(p + i + 1) : -1;
            if (b < 0) {
                valid = false;
                break;
            }
            i += 3;
            for (int wd = a;; wd = (wd + 1) % 7) {
                mask |= (uint8_t)(1u << wd);
                if (wd == b) {
                    break;
                }
            }
        } else {
            mask |= (uint8_t)(1u << a);
        }
    }
    if (valid && mask) {
        out->days = mask;
    }
}

bool eibi_parse_line(char *line, eibi_rec_t *out)
{
    if (!line || !out || strlen(line) < 34) {
        return false;
    }

    char fs[15];
    char ts[10];
    char ds[8];
    char nm[33];
    memcpy(fs, line, 14);
    memcpy(ts, line + 14, 9);
    memcpy(ds, line + 23, 7);
    fs[14] = '\0';
    ts[9] = '\0';
    ds[7] = '\0';
    snprintf(nm, sizeof(nm), "%s", line + 34);

    int f = atoi(fs);
    if (f <= 0) {
        return false;
    }
    int sh, sm, eh, em;
    if (sscanf(ts, "%2d%2d-%2d%2d", &sh, &sm, &eh, &em) != 4) {
        return false;
    }
    if (strstr(nm, "Jammer") != NULL) {
        return false;
    }

    char *p = nm;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    char *q = p + strlen(p);
    while (q > p && (q[-1] == ' ' || q[-1] == '\t')) {
        *--q = '\0';
    }
    for (char *t = p; *t; ++t) {
        unsigned char c = (unsigned char)*t;
        if (c < 32 || c > 126) {
            *t = ' ';
        }
    }

    out->freq = (uint16_t)(f > 65535 ? 65535 : f);
    out->sh = (uint8_t)(sh > 24 ? 24 : sh);
    out->sm = (uint8_t)(sm > 59 ? 59 : sm);
    out->eh = (uint8_t)(eh > 24 ? 24 : eh);
    out->em = (uint8_t)(em > 59 ? 59 : em);
    eibi_parse_days(ds, out);
    snprintf(out->name, sizeof(out->name), "%s", p);
    return true;
}

void eibi_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "EiBi download start");
    s_eibi_state = 1;
    if (eibi_fs_mount() != ESP_OK) {
        s_eibi_state = 3;
        vTaskDelete(NULL);
        return;
    }

    FILE *f = fopen(EIBI_TMP, "wb");
    if (!f) {
        s_eibi_state = 3;
        vTaskDelete(NULL);
        return;
    }

    eibi_file_hdr_t hdr = {
        .magic = {'E', 'I', 'B', 'I'},
        .version = EIBI_FILE_VERSION,
        .rec_size = (uint16_t)sizeof(eibi_rec_t),
    };
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        remove(EIBI_TMP);
        s_eibi_state = 3;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t cfg = {
        .url = "http://www.eibispace.de/dx/eibi.txt",
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    bool ok = (c && esp_http_client_open(c, 0) == ESP_OK);
    int recs = 0;
    int expected = -1;
    if (ok) {
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        expected = (int)esp_http_client_get_content_length(c);
        ESP_LOGI(TAG, "EiBi HTTP %d, len %d", status,
                 (int)esp_http_client_get_content_length(c));
        if (status != 200) {
            ok = false;
        } else {
        static char buf[2048];
        char line[220];
        int ll = 0;
        int n;
        while ((n = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
            s_eibi_bytes += n;
            for (int i = 0; i < n; ++i) {
                char ch = buf[i];
                if (ch == '\r') {
                    ch = ' ';
                }
                if (ch == '\n') {
                    line[ll] = '\0';
                    if (ll > 0 && isdigit((unsigned char)line[0])) {
                        eibi_rec_t rec;
                        if (eibi_parse_line(line, &rec)) {
                            fwrite(&rec, sizeof(rec), 1, f);
                            ++recs;
                            s_eibi_entries = recs;
                        }
                    }
                    ll = 0;
                } else if (ll < (int)sizeof(line) - 1) {
                    line[ll++] = ch;
                }
            }
        }
        if (n < 0 || (expected > 0 && s_eibi_bytes < expected)) {
            ok = false;
        }
        }
    }
    if (c) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    {
        long wpos = ftell(f);
        int ferr = ferror(f);
        ESP_LOGI(TAG, "EiBi write pos=%ld err=%d bytes=%d", wpos, ferr,
                 s_eibi_bytes);
    }
    fclose(f);

    if (ok && recs == 0) {
        ok = false;
    }
    if (ok) {
        ESP_LOGI(TAG, "EiBi download done (%d entries)", recs);
        remove(EIBI_BIN);
        if (rename(EIBI_TMP, EIBI_BIN) == 0) {
            FILE *chk = fopen(EIBI_BIN, "rb");
            long sz = -1;
            if (chk) {
                fseek(chk, 0, SEEK_END);
                sz = ftell(chk);
                fclose(chk);
            }
            ESP_LOGI(TAG, "EiBi renamed, size=%ld fopen=%d", sz, chk != NULL);
            s_eibi_state = 2;
        } else {
            ESP_LOGW(TAG, "EiBi rename failed");
            remove(EIBI_TMP);
            s_eibi_state = 3;
        }
    } else {
        remove(EIBI_TMP);
        s_eibi_state = 3;
    }
    vTaskDelete(NULL);
}

void eibi_download_start(void)
{
    if (!s_sta_connected || s_wifi_mode != 2) {
        toast("WI-FI FIRST");
        return;
    }
    if (s_eibi_state == 1) {
        toast("EIBI BUSY");
        return;
    }
    s_eibi_state = 1;
    if (xTaskCreate(eibi_task, "eibi", 10240, NULL, 5, NULL) != pdPASS) {
        s_eibi_state = 3;
    }
    toast("EIBI DL...");
}

static bool eibi_day_match(const eibi_rec_t *rec, int64_t utc)
{
    int64_t days = utc / 86400;
    int64_t sod = utc % 86400;
    if (sod < 0) {
        sod += 86400;
        days -= 1;
    }
    int minute = (int)(sod / 60);
    int start = rec->sh * 60 + rec->sm;
    int end = rec->eh * 60 + rec->em;

    int64_t day_for_mask = days;
    bool in_time;
    if (start <= end) {
        in_time = (minute >= start && minute < end);
    } else {
        in_time = (minute >= start || minute < end);
        if (minute < end) {
            day_for_mask = days - 1;
        }
    }
    if (!in_time) {
        return false;
    }

    int y, m, d;
    civil_from_days(day_for_mask, &y, &m, &d);
    if (rec->month) {
        return rec->month == m && rec->day == d;
    }

    int wday = (int)(((day_for_mask % 7) + 7 + 3) % 7);
    if (!(rec->days & (1u << wday))) {
        return false;
    }
    if (rec->week == 0xFF) {
        return (d + 7) > days_in_month(y, m);
    }
    if (rec->week) {
        return ((d - 1) / 7 + 1) == rec->week;
    }
    return true;
}

int64_t eibi_utc_now(void)
{
    if (!s_clock_set) {
        return 0;
    }
    return s_clock_base + (int64_t)((now_ms() - s_clock_ms0) / 1000);
}

typedef struct {
    uint16_t freq_khz;
    uint8_t  tol_khz;
    const char *name;
} activity_entry_t;

static const activity_entry_t s_activity[] = {
    {  1840, 1, "FT8" },
    {  3573, 1, "FT8" },
    {  5357, 1, "FT8" },
    {  7074, 1, "FT8" },
    { 10136, 1, "FT8" },
    { 14074, 1, "FT8" },
    { 18100, 1, "FT8" },
    { 21074, 1, "FT8" },
    { 24915, 1, "FT8" },
    { 28074, 1, "FT8" },
    {  3575, 1, "FT4" },
    {  7047, 1, "FT4" },
    { 10140, 1, "FT4" },
    { 14080, 1, "FT4" },
    { 18104, 1, "FT4" },
    { 21140, 1, "FT4" },
    { 24919, 1, "FT4" },
    { 28180, 1, "FT4" },
    {  1842, 1, "JS8" },
    {  3578, 1, "JS8" },
    {  7078, 1, "JS8" },
    { 10130, 1, "JS8" },
    { 14078, 1, "JS8" },
    { 18078, 1, "JS8" },
    { 21078, 1, "JS8" },
    { 24922, 1, "JS8" },
    { 28078, 1, "JS8" },
    {  3730, 2, "SSTV" },
    {  7171, 2, "SSTV" },
    { 14230, 2, "SSTV" },
    { 21340, 2, "SSTV" },
    { 28680, 2, "SSTV" },
    { 29600, 2, "10m FM" },
};

static const uint16_t s_cb_khz[40] = {
    26965, 26975, 26985, 27005, 27015, 27025, 27035, 27055, 27065, 27075,
    27085, 27105, 27115, 27125, 27135, 27155, 27165, 27175, 27185, 27205,
    27215, 27225, 27255, 27235, 27245, 27265, 27275, 27285, 27295, 27305,
    27315, 27325, 27335, 27345, 27355, 27365, 27375, 27385, 27395, 27405,
};

static void activity_append(char *out, size_t n, const char *name, int *count)
{
    size_t used = strlen(out);
    if (used) {
        if (used + 3 >= n) {
            return;
        }
        strncat(out, " / ", n - used - 1);
        used += 3;
    }
    if (used >= n) {
        return;
    }
    strncat(out, name, n - used - 1);
    (*count)++;
}

static int activity_lookup(uint16_t freq, char *out, size_t n)
{
    int count = 0;
    for (size_t i = 0; i < sizeof(s_activity) / sizeof(s_activity[0]); ++i) {
        int diff = (int)freq - (int)s_activity[i].freq_khz;
        if (diff < 0) {
            diff = -diff;
        }
        if (diff <= (int)s_activity[i].tol_khz) {
            activity_append(out, n, s_activity[i].name, &count);
        }
    }
    if (freq >= 26965u && freq <= 27405u) {
        for (int i = 0; i < 40; ++i) {
            int diff = (int)freq - (int)s_cb_khz[i];
            if (diff < 0) {
                diff = -diff;
            }
            if (diff <= 4) {
                char label[20];
                if (i == 8) {
                    snprintf(label, sizeof(label), "CB CH9 EMERG");
                } else if (i == 18) {
                    snprintf(label, sizeof(label), "CB CH19 CALL");
                } else {
                    snprintf(label, sizeof(label), "CB CH%d", i + 1);
                }
                activity_append(out, n, label, &count);
                break;
            }
        }
    }
    return count;
}

int eibi_lookup(uint16_t freq, int64_t utc, char *out, size_t n)
{
    out[0] = '\0';
    int count = activity_lookup(freq, out, n);
    if (utc <= 0) {
        return count;
    }
    FILE *f = fopen(EIBI_BIN, "rb");
    if (!f) {
        return count;
    }
    if (!eibi_header_ok(f)) {
        fclose(f);
        return count;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return count;
    }
    long size = ftell(f);
    long total = (size - (long)sizeof(eibi_file_hdr_t)) /
                 (long)sizeof(eibi_rec_t);
    long lo = 0;
    long hi = total > 0 ? total : 0;
    eibi_rec_t rec;

    while (lo < hi) {
        long mid = (lo + hi) / 2;
        long off = (long)sizeof(eibi_file_hdr_t) +
                   mid * (long)sizeof(rec);
        if (fseek(f, off, SEEK_SET) != 0) {
            break;
        }
        if (fread(&rec, sizeof(rec), 1, f) != 1) {
            break;
        }
        if (rec.freq < freq) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    long off = (long)sizeof(eibi_file_hdr_t) + lo * (long)sizeof(rec);
    if (fseek(f, off, SEEK_SET) == 0) {
        while (fread(&rec, sizeof(rec), 1, f) == 1 && rec.freq == freq) {
            if (!eibi_day_match(&rec, utc)) {
                continue;
            }
            if (strstr(out, rec.name) != NULL && rec.name[0]) {
                continue;
            }
            size_t used = strlen(out);
            size_t add = strlen(rec.name) + (count ? 3 : 0);
            if (used + add + 1 >= n) {
                break;
            }
            if (count) {
                strncat(out, " / ", n - used - 1);
            }
            strncat(out, rec.name, n - strlen(out) - 1);
            ++count;
        }
    }
    fclose(f);
    return count;
}

static void eibi_trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

int eibi_lookup_entries(uint16_t freq, int64_t utc, eibi_entry_t *out, int max)
{
    if (!out || max <= 0 || utc <= 0) {
        return 0;
    }
    FILE *f = fopen(EIBI_BIN, "rb");
    if (!f) {
        return 0;
    }
    if (!eibi_header_ok(f)) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long size = ftell(f);
    long total = (size - (long)sizeof(eibi_file_hdr_t)) /
                 (long)sizeof(eibi_rec_t);
    long lo = 0;
    long hi = total > 0 ? total : 0;
    eibi_rec_t rec;

    while (lo < hi) {
        long mid = (lo + hi) / 2;
        long off = (long)sizeof(eibi_file_hdr_t) +
                   mid * (long)sizeof(rec);
        if (fseek(f, off, SEEK_SET) != 0) {
            break;
        }
        if (fread(&rec, sizeof(rec), 1, f) != 1) {
            break;
        }
        if (rec.freq < freq) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    long off = (long)sizeof(eibi_file_hdr_t) + lo * (long)sizeof(rec);
    int count = 0;
    if (fseek(f, off, SEEK_SET) == 0) {
        while (fread(&rec, sizeof(rec), 1, f) == 1 && rec.freq == freq) {
            if (!eibi_day_match(&rec, utc)) {
                continue;
            }
            char raw[33];
            memcpy(raw, rec.name, 32);
            raw[32] = '\0';
            eibi_entry_t e;
            memset(&e, 0, sizeof(e));
            memcpy(e.station, raw, 25);
            memcpy(e.lang, raw + 25, 4);
            memcpy(e.target, raw + 29, 3);
            eibi_trim(e.station);
            eibi_trim(e.lang);
            eibi_trim(e.target);
            if (e.station[0] == '\0') {
                continue;
            }
            if (count < max) {
                out[count++] = e;
            }
        }
    }
    fclose(f);
    return count;
}

void eibi_refresh(void)
{
    static uint32_t last_freq;
    static int64_t last_utc_min = -1;
    static char last_name[120];

    int64_t utc = eibi_utc_now();
    int64_t utc_min = utc / 60;

    if (!s_eibi_force && s_ui.freq_hz == last_freq &&
        utc_min == last_utc_min) {
        return;
    }
    s_eibi_force = false;
    last_freq = s_ui.freq_hz;
    last_utc_min = utc_min;

    char name[120] = {0};
    if (s_ui.mode != RADIO_MODE_FM && eibi_available()) {
        eibi_lookup((uint16_t)(s_ui.freq_hz / 1000), utc, name, sizeof(name));
    }
    int minute = (int)(((utc % 86400) + 86400) % 86400 / 60);
    ESP_LOGI(TAG, "EIBI %u kHz @ UTC %d min -> %s",
             (unsigned)(s_ui.freq_hz / 1000), minute,
             name[0] ? name : "(none)");
    if (strcmp(name, last_name) == 0) {
        return;
    }
    snprintf(last_name, sizeof(last_name), "%s", name);
    snprintf(s_station, sizeof(s_station), "%s", name);
    s_ui.station = s_station[0] ? s_station : NULL;

    if (s_hmi == HMI_TUNE) {
        if (s_ui.ui_mode != UI_MODE_EASY) {
            ui_draw_all(&s_ui);
        } else {
            ui_draw_rds(&s_ui);
        }
    }
}

int eibi_record_count(void)
{
    FILE *f = fopen(EIBI_BIN, "rb");
    if (!f) {
        return 0;
    }
    if (!eibi_header_ok(f) || fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long sz = ftell(f);
    fclose(f);
    long data = sz - (long)sizeof(eibi_file_hdr_t);
    return (data > 0) ? (int)(data / (long)sizeof(eibi_rec_t)) : 0;
}

void open_eibi_app(void)
{
    s_eibi_cursor = 0;
    s_hmi = HMI_EIBI;
    s_nav_ms = now_ms();
    ui_draw_eibi_app(s_eibi_cursor, eibi_available(), eibi_record_count());
}

bool eibi_read_record(int idx, eibi_rec_t *out)
{
    if (!out || idx < 0) {
        return false;
    }
    FILE *f = fopen(EIBI_BIN, "rb");
    if (!f) {
        return false;
    }
    if (!eibi_header_ok(f)) {
        fclose(f);
        return false;
    }
    long off = (long)sizeof(eibi_file_hdr_t) +
               (long)idx * (long)sizeof(*out);
    bool ok = (fseek(f, off, SEEK_SET) == 0) &&
              (fread(out, sizeof(*out), 1, f) == 1);
    fclose(f);
    return ok;
}

void open_eibi_view(void)
{
    s_eibi_view_cur = 0;
    s_eibi_view_top = 0;
    s_hmi = HMI_EIBI_VIEW;
    s_nav_ms = now_ms();
    ui_draw_eibi_view(s_eibi_view_cur, s_eibi_view_top,
                      eibi_record_count());
}

static int s_about_page;

static void about_draw(void)
{
    ui_draw_about(app_version(), app_idf_version(), app_build_no(),
                  s_about_page);
}

void open_about(void)
{
    s_about_page = 0;
    s_hmi = HMI_ABOUT;
    s_nav_ms = now_ms();
    about_draw();
}

void about_page_move(int dir)
{
    s_about_page = (s_about_page + (dir > 0 ? 1 : -1) + 3) % 3;
    about_draw();
}

void open_eibi_confirm(void)
{
    s_eibi_yes = false;
    s_hmi = HMI_EIBI_CONFIRM;
    s_nav_ms = now_ms();
    ui_draw_confirm("EIBI DOWNLOAD", "Download schedule now?", false);
}

void eibi_app_action(void)
{
    s_hmi = HMI_EIBI_DL;
    s_eibi_dl_ms = now_ms();
    s_eibi_entries = 0;
    s_eibi_bytes = 0;
    ui_draw_eibi_dl(0, 0, 0);
    eibi_download_start();
    if (s_eibi_state != 1) {
        open_eibi_app();
    }
}

