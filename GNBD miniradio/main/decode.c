/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * CW / RTTY audio decoder using an ADC input (ADC2, GPIO 11..14).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "app.h"
#include "decode.h"

#define DECODE_FS        8000
#define DECODE_BLOCK     176
#define DECODE_BLOCK_MS  22
#define DECODE_PI        3.14159265f

typedef enum {
    DECODE_MODE_CW = 0,
    DECODE_MODE_RTTY,
} decode_mode_t;

static decode_mode_t s_mode = DECODE_MODE_CW;
static int      s_gpio = 11;
static bool     s_running;
static bool     s_adc_ok;
static uint8_t  s_wifi_prev;

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t             s_chan;
static int16_t                   s_block[DECODE_BLOCK];

static char s_text[256];
static int  s_text_len;
static int  s_level;
static bool s_changed;

static bool s_cw_key;
static int  s_cw_mark_ms;
static int  s_cw_gap_ms;
static bool s_cw_letter_done;
static bool s_cw_word_done;
static char s_cw_char[10];
static int  s_cw_char_len;
static float s_cw_noise;

static int  s_rtty_state;
static int  s_rtty_bitcount;
static uint8_t s_rtty_data;
static bool s_rtty_figs;

static const struct {
    const char *code;
    char ch;
} s_morse[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},
    {".", 'E'}, {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'},
    {"..", 'I'}, {".---", 'J'}, {"-.-", 'K'}, {".-..", 'L'},
    {"--", 'M'}, {"-.", 'N'}, {"---", 'O'}, {".--.", 'P'},
    {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'},
    {"-.--", 'Y'}, {"--..", 'Z'}, {"-----", '0'}, {".----", '1'},
    {"..---", '2'}, {"...--", '3'}, {"....-", '4'}, {".....", '5'},
    {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
    {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {"-.-.--", '!'},
    {"-..-.", '/'}, {"-....-", '-'}, {"-.--.", '('}, {"-.--.-", ')'},
    {"---...", ':'}, {"-.-.-.", ';'}, {"-...-", '='}, {".-.-.", '+'},
};

static const char s_baudot_letters[32] = {
    '\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J',
    'N', 'F', 'C', 'K', 'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q',
    'O', 'B', 'G', 0x01, 'M', 'X', 'V', 0x02,
};

static const char s_baudot_figs[32] = {
    '\0', '3', '\n', '-', ' ', '\'', '8', '7', '\r', '$', '4', '\a',
    ',', '!', ':', '(', '5', '"', ')', '2', '#', '6', '0', '1',
    '9', '?', '&', 0x01, '.', '/', ';', 0x02,
};

static void text_clear(void)
{
    s_text[0] = '\0';
    s_text_len = 0;
    s_changed = true;
}

static void text_put(char c)
{
    if (c == '\0' || c == '\r') {
        return;
    }
    if (c == '\n') {
        c = ' ';
    }
    if (s_text_len >= (int)sizeof(s_text) - 1) {
        memmove(s_text, s_text + 128, (size_t)(s_text_len - 128));
        s_text_len -= 128;
    }
    s_text[s_text_len++] = c;
    s_text[s_text_len] = '\0';
    s_changed = true;
}

static void adc_close(void)
{
    if (s_adc) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
    s_adc_ok = false;
}

static bool adc_open(int gpio)
{
    adc_close();
    adc_unit_t unit;
    adc_channel_t chan;
    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, &chan);
    if (err != ESP_OK) {
        return false;
    }
    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id = unit,
    };
    err = adc_oneshot_new_unit(&ucfg, &s_adc);
    if (err != ESP_OK) {
        s_adc = NULL;
        return false;
    }
    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, chan, &ccfg);
    if (err != ESP_OK) {
        adc_close();
        return false;
    }
    s_chan = chan;
    s_adc_ok = true;
    return true;
}

static bool adc_read_block(void)
{
    if (!s_adc || !s_adc_ok) {
        return false;
    }
    int64_t next = esp_timer_get_time();
    for (int i = 0; i < DECODE_BLOCK; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, s_chan, &raw) != ESP_OK) {
            return false;
        }
        s_block[i] = (int16_t)(raw - 2048);
        next += 1000000 / DECODE_FS;
        int64_t now = esp_timer_get_time();
        if (next > now) {
            esp_rom_delay_us((uint32_t)(next - now));
        } else {
            next = now;
        }
    }
    return true;
}

static float tone_mag(float freq)
{
    float w = 2.0f * DECODE_PI * freq / DECODE_FS;
    float c = 2.0f * cosf(w);
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    for (int i = 0; i < DECODE_BLOCK; ++i) {
        s0 = (float)s_block[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    float p = s1 * s1 + s2 * s2 - c * s1 * s2;
    if (p < 0.0f) {
        p = 0.0f;
    }
    return sqrtf(p) / DECODE_BLOCK;
}

static char morse_char(const char *code)
{
    for (size_t i = 0; i < sizeof(s_morse) / sizeof(s_morse[0]); ++i) {
        if (strcmp(code, s_morse[i].code) == 0) {
            return s_morse[i].ch;
        }
    }
    return '?';
}

static void cw_finish_char(void)
{
    if (s_cw_char_len == 0) {
        return;
    }
    s_cw_char[s_cw_char_len] = '\0';
    text_put(morse_char(s_cw_char));
    s_cw_char_len = 0;
}

static void cw_reset(void)
{
    s_cw_key = false;
    s_cw_mark_ms = 0;
    s_cw_gap_ms = 0;
    s_cw_letter_done = false;
    s_cw_word_done = false;
    s_cw_char_len = 0;
    s_cw_noise = 10.0f;
}

static void cw_process(void)
{
    float mag = 0.0f;
    for (int f = 500; f <= 900; f += 100) {
        float m = tone_mag((float)f);
        if (m > mag) {
            mag = m;
        }
    }
    int lvl = (int)(mag * 0.4f);
    if (lvl < 0) lvl = 0;
    if (lvl > 100) lvl = 100;
    s_level = lvl;

    float thr = s_cw_noise * 2.0f + 15.0f;
    bool key = s_cw_key;
    if (!s_cw_key && mag > thr) {
        key = true;
    } else if (s_cw_key && mag < thr * 0.6f) {
        key = false;
    }
    if (!key && mag < thr) {
        s_cw_noise = s_cw_noise * 0.95f + mag * 0.05f;
    }

    if (key) {
        if (!s_cw_key) {
            s_cw_mark_ms = 0;
            s_cw_gap_ms = 0;
            s_cw_letter_done = false;
            s_cw_word_done = false;
        }
        s_cw_mark_ms += DECODE_BLOCK_MS;
    } else {
        if (s_cw_key) {
            if (s_cw_mark_ms >= 22 && s_cw_char_len < 9) {
                s_cw_char[s_cw_char_len++] = s_cw_mark_ms >= 110 ? '-' : '.';
            }
            s_cw_mark_ms = 0;
        }
        s_cw_gap_ms += DECODE_BLOCK_MS;
        if (s_cw_gap_ms >= 180 && s_cw_char_len > 0 && !s_cw_letter_done) {
            cw_finish_char();
            s_cw_letter_done = true;
        }
        if (s_cw_gap_ms >= 420 && !s_cw_word_done) {
            text_put(' ');
            s_cw_word_done = true;
        }
    }
    s_cw_key = key;
}

static void rtty_reset(void)
{
    s_rtty_state = 0;
    s_rtty_bitcount = 0;
    s_rtty_data = 0;
    s_rtty_figs = false;
}

static void baudot_put(uint8_t code)
{
    code &= 0x1F;
    if (code == 0x1B) {
        s_rtty_figs = true;
        return;
    }
    if (code == 0x1F) {
        s_rtty_figs = false;
        return;
    }
    char c = s_rtty_figs ? s_baudot_figs[code] : s_baudot_letters[code];
    if (c) {
        text_put(c);
    }
}

static void rtty_process(void)
{
    float mark = tone_mag(2125.0f);
    float space = tone_mag(2295.0f);
    float peak = mark > space ? mark : space;
    int lvl = (int)(peak * 0.4f);
    if (lvl < 0) lvl = 0;
    if (lvl > 100) lvl = 100;
    s_level = lvl;

    int bit = (mark >= space) ? 1 : 0;
    if (peak < 20.0f) {
        bit = 1;
    }

    switch (s_rtty_state) {
    case 0:
        if (bit == 0) {
            s_rtty_state = 1;
            s_rtty_bitcount = 0;
            s_rtty_data = 0;
        }
        break;
    case 1:
        s_rtty_data |= (uint8_t)(bit << s_rtty_bitcount);
        if (++s_rtty_bitcount >= 5) {
            s_rtty_state = 2;
        }
        break;
    default:
        if (bit == 1) {
            baudot_put(s_rtty_data);
        }
        s_rtty_state = 0;
        break;
    }
}

void decode_start(void)
{
    if (s_running) {
        return;
    }
    s_wifi_prev = s_wifi_mode;
    if (s_wifi_prev != 0) {
        wifi_off();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    s_running = true;
    s_level = 0;
    text_clear();
    cw_reset();
    rtty_reset();
    if (!adc_open(s_gpio)) {
        text_clear();
        text_put('A');
        text_put('D');
        text_put('C');
        text_put(' ');
        text_put('E');
        text_put('R');
        text_put('R');
    }
}

void decode_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    adc_close();
    if (s_wifi_prev == 1) {
        wifi_start_ap();
    } else if (s_wifi_prev == 2) {
        wifi_sta_connect();
    }
    s_wifi_prev = 0;
}

bool decode_poll(uint32_t now)
{
    (void)now;
    if (!s_running) {
        return false;
    }
    s_changed = false;
    if (!adc_read_block()) {
        s_level = 0;
        return false;
    }
    if (s_mode == DECODE_MODE_CW) {
        cw_process();
    } else {
        rtty_process();
    }
    return s_changed;
}

void decode_set_gpio(int pin)
{
    if (pin < 0 || pin > 48) {
        return;
    }
    s_gpio = pin;
}

void decode_set_mode(int idx)
{
    s_mode = (idx == 1) ? DECODE_MODE_RTTY : DECODE_MODE_CW;
}

void decode_cycle_mode(void)
{
    s_mode = (s_mode == DECODE_MODE_CW) ? DECODE_MODE_RTTY
                                        : DECODE_MODE_CW;
    text_clear();
    cw_reset();
    rtty_reset();
    if (s_mode == DECODE_MODE_RTTY) {
        s_rtty_figs = false;
    }
}

void decode_cycle_gpio(int dir)
{
    int step = dir >= 0 ? 1 : 3;
    s_gpio = 11 + ((s_gpio - 11 + step) & 3);
    text_clear();
    cw_reset();
    rtty_reset();
    if (!adc_open(s_gpio)) {
        text_clear();
        text_put('A');
        text_put('D');
        text_put('C');
        text_put(' ');
        text_put('E');
        text_put('R');
        text_put('R');
    }
}

bool decode_running(void)
{
    return s_running;
}

const char *decode_mode_str(void)
{
    return s_mode == DECODE_MODE_CW ? "CW" : "RTTY";
}

int decode_gpio(void)
{
    return s_gpio;
}

int decode_level(void)
{
    return s_level;
}

const char *decode_text(void)
{
    return s_text;
}
