/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * Custom layout renderer (see ui_custom.h).
 */
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "ui.h"
#include "app.h"
#include "band.h"

#include "ui_custom.h"
#include "ui_custom_layout.h"
#include "ui_runtime.h"
#include "decode.h"

extern bool s_agc_on;
extern uint8_t s_softmute;
extern uint8_t s_avc;
extern bool s_squelch_on;
extern uint8_t s_squelch_type[4];
extern uint8_t s_squelch_thr[4];
extern int32_t s_bfo;
extern uint8_t s_brightness;
const char *bw_label(void);

/* ---------------- Lua 위젯 오버라이드 ---------------- */

#define UI_OV_MAX 32

typedef struct {
    uint16_t uid;
    char     text[32];
    uint8_t  color;
    int8_t   visible;   /* -1 = 미설정 */
} ui_ov_t;

static ui_ov_t s_ov[UI_OV_MAX];

static ui_ov_t *ov_get(uint16_t uid, bool create)
{
    for (int i = 0; i < UI_OV_MAX; ++i) {
        if (s_ov[i].uid == uid) {
            return &s_ov[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (int i = 0; i < UI_OV_MAX; ++i) {
        if (s_ov[i].uid == 0) {
            s_ov[i].uid = uid;
            s_ov[i].visible = -1;
            s_ov[i].color = 0xFF;
            s_ov[i].text[0] = '\0';
            return &s_ov[i];
        }
    }
    return NULL;
}

void ui_widget_override_text(uint16_t uid, const char *text)
{
    ui_ov_t *o = ov_get(uid, true);
    if (!o) {
        return;
    }
    snprintf(o->text, sizeof(o->text), "%s", text ? text : "");
}

void ui_widget_override_visible(uint16_t uid, bool visible)
{
    ui_ov_t *o = ov_get(uid, true);
    if (o) {
        o->visible = visible ? 1 : 0;
    }
}

void ui_widget_override_color(uint16_t uid, uint8_t color)
{
    ui_ov_t *o = ov_get(uid, true);
    if (o) {
        o->color = color;
    }
}

int ui_widget_override_hidden(uint16_t uid)
{
    ui_ov_t *o = ov_get(uid, false);
    return (o && o->visible == 0) ? 1 : 0;
}

const char *ui_widget_override_text_of(uint16_t uid)
{
    ui_ov_t *o = ov_get(uid, false);
    return (o && o->text[0]) ? o->text : NULL;
}

int ui_widget_override_color_of(uint16_t uid, int def)
{
    ui_ov_t *o = ov_get(uid, false);
    return (o && o->color != 0xFF) ? (int)o->color : def;
}


bool ui_custom_active(void)
{
    return UI_CUSTOM_ENABLE != 0 || ui_runtime_enabled();
}

static uint16_t col(uint8_t c)
{
    switch (c) {
    case UC_PANEL:   return UI_PANEL;
    case UC_FG:      return UI_FG;
    case UC_MUTED:   return UI_MUTED;
    case UC_LINE:    return UI_LINE;
    case UC_ACCENT:  return UI_ACC;
    case UC_ACCENTH: return UI_ACC_HI;
    case UC_OK:      return UI_OK;
    case UC_WARN:    return UI_WARN;
    case UC_RED:     return UI_RED;
    case UC_TRACK:   return UI_TRACK;
    case UC_HSEL:    return UI_HSEL;
    case UC_BG:      return UI_BG;
    default:         return UI_BG;
    }
}

static display_font_t fnt(uint8_t f)
{
    return f == UC_LARGE ? DISPLAY_FONT_LARGE
         : f == UC_SMALL ? DISPLAY_FONT_SMALL : DISPLAY_FONT_TINY;
}

static display_datum_t dat(uint8_t a, bool chipped)
{
    (void)chipped;
    return a == UC_AL_C ? DISPLAY_DATUM_TC
         : a == UC_AL_R ? DISPLAY_DATUM_TR : DISPLAY_DATUM_TL;
}

static void fit(char *out, size_t n, const char *src, display_font_t f, int maxw)
{
    snprintf(out, n, "%s", src ? src : "");
    size_t len = strlen(out);
    while (len > 0 && display_text_width(out, f) > (uint16_t)maxw) {
        out[--len] = '\0';
    }
}

static void fmt_freq(uint32_t hz, int mode, int unit_mode, char *buf, size_t n,
                     const char **unit)
{
    if (mode == RADIO_MODE_FM) {
        snprintf(buf, n, "%lu.%02lu", (unsigned long)(hz / 1000000),
                 (unsigned long)((hz / 10000) % 100));
        *unit = "MHz";
    } else if (unit_mode == 1) {
        snprintf(buf, n, "%lu", (unsigned long)(hz / 1000));
        *unit = "kHz";
    } else {
        snprintf(buf, n, "%lu.%03lu", (unsigned long)(hz / 1000000),
                 (unsigned long)((hz / 1000) % 1000));
        *unit = "MHz";
    }
}

static int num_bind(uint8_t b, const ui_state_t *s);

static int num_bind(uint8_t b, const ui_state_t *s)
{
    if (b == UC_B_SLEEP) {
        int32_t remain = (int32_t)(s_sleep_deadline - now_ms());
        if (!s_sleep_armed || s_sleep_min == 0) {
            return 0;
        }
        return (remain > 0) ? (int)((remain + 999) / 1000) : 0;
    }
    if (b == UC_B_DECODE_LEVEL) {
        return decode_level();
    }
    switch (b) {
    case UC_B_RSSI: return s->have_rsq ? s->rsq.rssi : 0;
    case UC_B_SNR:  return s->have_rsq ? s->rsq.snr : 0;
    case UC_B_BATT: return s->have_battery ? s->battery.percent : 0;
    case UC_B_VOL:  return s->volume;
    case UC_B_SQ: {
        if (!s_squelch_on) {
            return 0;
        }
        int sq = (s->mode == RADIO_MODE_FM) ? 0
               : (s->mode == RADIO_MODE_AM) ? 1
               : (s->mode == RADIO_MODE_LSB) ? 2 : 3;
        return s_squelch_thr[sq];
    }
    default:        return 0;
    }
}

static const char *str_bind(uint8_t b, const ui_state_t *s, char *buf, size_t n)
{
    buf[0] = '\0';
    switch (b) {
    case UC_B_FREQ:
        fmt_freq_short(s->freq_hz, s->mode, buf, n);
        return buf;
    case UC_B_MODE:
        return s->mode == RADIO_MODE_FM ? "FM"
             : s->mode == RADIO_MODE_LSB ? "LSB"
             : s->mode == RADIO_MODE_USB ? "USB" : "AM";
    case UC_B_BAND:  return band_table[s->band_idx].name;
    case UC_B_CLOCK: return s->clock;
    case UC_B_DATE:  return s->date;
    case UC_B_PS:    return (s->rds && s->rds->ps_valid) ? s->rds->ps : "";
    case UC_B_RT:    return (s->rds && s->rds->rt_valid) ? s->rds->rt : "";
    case UC_B_PTY:   return (s->rds && s->rds->pty) ? rds_pty_name(s->rds->pty) : "";
    case UC_B_PRESET:
        if (s->preset_slot < 0) { return "--"; }
        snprintf(buf, n, "P%02d", s->preset_slot + 1);
        return buf;
    case UC_B_PNAME: {
        const char *nm = preset_name_at(s->preset_slot);
        return (nm && nm[0]) ? nm : "";
    }
    case UC_B_ATS:
        if (s->ats_slot < 0) { return "--"; }
        snprintf(buf, n, "A%02d", s->ats_slot + 1);
        return buf;
    case UC_B_BATT:
        snprintf(buf, n, "%u%%", (unsigned)num_bind(UC_B_BATT, s));
        return buf;
    case UC_B_VOL:
        snprintf(buf, n, "%u", (unsigned)s->volume);
        return buf;
    case UC_B_BW:    return bw_label();
    case UC_B_STEP:
        if (s->step_hz % 1000 == 0) {
            snprintf(buf, n, "%dK", s->step_hz / 1000);
        } else {
            snprintf(buf, n, "%dH", s->step_hz);
        }
        return buf;
    case UC_B_BFO:
        snprintf(buf, n, "%+ld", (long)s_bfo);
        return buf;
    case UC_B_AGC:   return s_agc_on ? "ON" : "OFF";
    case UC_B_SM:
        snprintf(buf, n, "%u", (unsigned)s_softmute);
        return buf;
    case UC_B_AFC:
        return !s->have_rsq ? "--" : (s->rsq.afc_rail ? "RA" : "OK");
    case UC_B_AVC:
        snprintf(buf, n, "%u", (unsigned)s_avc);
        return buf;
    case UC_B_SQ: {
        int sq = mode_dsp_slot(s->mode);
        if (!s_squelch_on) { return "OFF"; }
        snprintf(buf, n, "%s%u", s_squelch_type[sq] ? "S" : "R",
                 (unsigned)s_squelch_thr[sq]);
        return buf;
    }
    case UC_B_STEREO: return s->stereo ? "ST" : "";
    case UC_B_LINE1:
        snprintf(buf, n, "BW %s ST %luK", bw_label(),
                 (unsigned long)(eff_step_hz() / 1000));
        return buf;
    case UC_B_LINE2:
        snprintf(buf, n, "BFO %+ld AGC %s SM %u", (long)s_bfo,
                 s_agc_on ? "ON" : "OFF", (unsigned)s_softmute);
        return buf;
    case UC_B_LINE3: {
        const char *afc = !s->have_rsq ? "--" : (s->rsq.afc_rail ? "RA" : "OK");
        int sq = mode_dsp_slot(s->mode);
        char sqv[8];
        if (s_squelch_on) {
            snprintf(sqv, sizeof(sqv), "%s%u",
                     s_squelch_type[sq] ? "S" : "R", s_squelch_thr[sq]);
        } else {
            snprintf(sqv, sizeof(sqv), "OFF");
        }
        /* 위젯 폭(약 122px)에 맞춰 공백을 한 칸으로 압축 */
        snprintf(buf, n, "AFC %s AVC %u SQ %s", afc, (unsigned)s_avc, sqv);
        return buf;
    }
    case UC_B_WIFI:
        return s->wifi_state == 1 ? "AP"
             : s->wifi_state == 2 ? "STA"
             : s->wifi_state == 3 ? "SCAN" : "";
    case UC_B_STATUS:
        snprintf(buf, n, "VOL %u  BRT %u", (unsigned)s->volume,
                 (unsigned)s_brightness);
        return buf;
    case UC_B_BATTV:
        if (!s->have_battery) {
            return "";
        }
        snprintf(buf, n, "%u.%02uV",
                 (unsigned)(s->battery.millivolt / 1000),
                 (unsigned)((s->battery.millivolt % 1000) / 10));
        return buf;
    case UC_B_EIBI:
        return (s->station && s->station[0]) ? s->station : "";
    case UC_B_TOAST:
        return s_toast[0] ? s_toast : "";
    case UC_B_SLEEP:
        if (!s_sleep_armed || s_sleep_min == 0) {
            return "";
        }
        {
            int32_t remain = (int32_t)(s_sleep_deadline - now_ms());
            if (remain < 0) {
                remain = 0;
            }
            int sec = (int)((remain + 999) / 1000);
            snprintf(buf, n, "SLEEP %d:%02d", sec / 60, sec % 60);
            return buf;
        }
    default: return buf;
    }
}

int ui_widget_bind_value(const uc_widget_t *w)
{
    if (!w) {
        return 0;
    }
    return num_bind(w->bind, &s_ui);
}


static void fit_pct(char *out, size_t n, const char *src, display_font_t f,
                    int maxw, uint16_t pct)
{
    if (!out || n == 0) return;
    snprintf(out, n, "%.*s", (int)(n - 1), src ? src : "");
    size_t len = strlen(out);
    while (len > 0 && display_text_width_pct(out, f, pct) > (uint16_t)maxw) {
        out[--len] = '\0';
    }
}

static void draw_text(const uc_widget_t *w, const char *txt)
{
    char fitbuf[64];
    display_font_t f = fnt(w->font);
    uint16_t fg = col(w->color);
    uint16_t bg = UI_BG;
    const char *src = txt;
    uint16_t pct = w->scale ? w->scale : 100;

    if (txt && txt[0] && !w->bind) { src = txt; }
    if (w->bind) { /* already resolved by caller */ }
    fit_pct(fitbuf, sizeof(fitbuf), src, f, w->w > 0 ? w->w : 320, pct);
    src = fitbuf;

    int x = w->x;
    int y = w->y;
    if (w->chip) {
        uint16_t tw = display_text_width_pct(src, f, pct);
        display_fill_round_rect((int16_t)(x + (w->w - tw) / 2 - 5),
                                (int16_t)(y - 1), (uint16_t)(tw + 10),
                                (uint16_t)((f == DISPLAY_FONT_TINY ? 12 : 20) * pct / 100), 6,
                                UI_HSEL);
        display_draw_text_pct((int16_t)(x + (w->w - tw) / 2), (int16_t)y, src, f,
                              UI_ACC_HI, UI_HSEL, DISPLAY_DATUM_TL, pct);
        return;
    }
    display_draw_text_pct((int16_t)x, (int16_t)y, src, f, fg, bg,
                          dat(w->align, false), pct);
}

static void draw_bar(const uc_widget_t *w, int value)
{
    uint16_t maxv = w->max ? w->max : 63;
    if (value < 0) { value = 0; }
    if (value > (int)maxv) { value = (int)maxv; }
    display_fill_round_rect((int16_t)w->x, (int16_t)w->y, (uint16_t)w->w,
                            (uint16_t)w->h, (uint16_t)(w->h / 2), UI_TRACK);
    int fill = (w->w - 2) * value / (int)maxv;
    if (fill > 0) {
        display_fill_round_rect((int16_t)(w->x + 1), (int16_t)(w->y + 1),
                                (uint16_t)fill, (uint16_t)(w->h - 2),
                                (uint16_t)((w->h - 2) / 2), col(w->color));
    }
}

static void draw_rsq(const uc_widget_t *w, const ui_state_t *s)
{
    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    if (rssi > 63) { rssi = 63; }
    if (snr > 63) { snr = 63; }
    int bw = w->w - 90;
    char nb[8];
    uint16_t pct = w->scale ? w->scale : 100;

    display_draw_text_pct((int16_t)(w->x + 4), (int16_t)(w->y + 4), "RSSI",
                          DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL,
                          pct);
    display_fill_round_rect((int16_t)(w->x + 44), (int16_t)(w->y + 6),
                            (uint16_t)bw, 8, 4, UI_TRACK);
    display_fill_round_rect((int16_t)(w->x + 44), (int16_t)(w->y + 6),
                            (uint16_t)(bw * rssi / 63), 8, 4, UI_OK);
    snprintf(nb, sizeof(nb), "%d", rssi);
    display_draw_text_pct((int16_t)(w->x + w->w - 4), (int16_t)(w->y + 3), nb,
                          DISPLAY_FONT_SMALL, UI_FG, UI_BG, DISPLAY_DATUM_TR,
                          pct);

    display_draw_text_pct((int16_t)(w->x + 4), (int16_t)(w->y + 24), "SNR",
                          DISPLAY_FONT_TINY, UI_MUTED, UI_BG, DISPLAY_DATUM_TL,
                          pct);
    display_fill_round_rect((int16_t)(w->x + 44), (int16_t)(w->y + 26),
                            (uint16_t)bw, 8, 4, UI_TRACK);
    display_fill_round_rect((int16_t)(w->x + 44), (int16_t)(w->y + 26),
                            (uint16_t)(bw * snr / 63), 8, 4,
                            snr < 10 ? UI_WARN : UI_ACC);
    snprintf(nb, sizeof(nb), "%d", snr);
    display_draw_text_pct((int16_t)(w->x + w->w - 4), (int16_t)(w->y + 23), nb,
                          DISPLAY_FONT_SMALL, snr < 10 ? UI_WARN : UI_FG, UI_BG,
                          DISPLAY_DATUM_TR, pct);
}

/* EiBi 이름의 과도한 공백을 하나로 축약 */
static void compact_spaces(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    bool sp = false;
    for (const char *p = src; p && *p && o + 1 < n; ++p) {
        if (*p == ' ' || *p == '\t') {
            sp = true;
            continue;
        }
        if (sp && o > 0) {
            dst[o++] = ' ';
        }
        sp = false;
        dst[o++] = *p;
    }
    dst[o] = '\0';
}

static void draw_rds(const uc_widget_t *w, const ui_state_t *s)
{
    display_fill_rect((int16_t)w->x, (int16_t)w->y, (uint16_t)w->w,
                      (uint16_t)w->h, UI_PANEL);
    display_draw_rect((int16_t)w->x, (int16_t)w->y, (uint16_t)w->w,
                      (uint16_t)w->h, UI_LINE);

    int idx = w->uid ? (int)(w->uid % 8) : 0;

    if (s->mode != RADIO_MODE_FM) {
        /* AM/SSB: EiBi 정보 카드 (uid 별 마퀴) */
        display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 3), "EIBI",
                          DISPLAY_FONT_TINY, UI_GOLD, UI_PANEL,
                          DISPLAY_DATUM_TL);
        display_draw_text((int16_t)(w->x + w->w - 4), (int16_t)(w->y + 3),
                          s->clock, DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                          DISPLAY_DATUM_TR);
        const char *st = (s->station && s->station[0]) ? s->station : "NO DATA";
        /* 매칭된 방송국을 한 줄에 이어 붙이면 폭에 걸려 2개만 보인다.
         * 방송국별로 최대 3줄로 나눠 모두 표시한다. */
        char lines = (char)((w->h - 16) / 10);
        if (lines < 1) { lines = 1; }
        if (lines > 3) { lines = 3; }
        const char *p = st;
        for (int i = 0; i < lines && *p; ++i) {
            while (*p == ' ') { ++p; }
            if (!*p) { break; }
            const char *sep = strstr(p, " / ");
            size_t len = sep ? (size_t)(sep - p) : strlen(p);
            char one[64];
            if (len >= sizeof(one)) { len = sizeof(one) - 1; }
            memcpy(one, p, len);
            one[len] = '\0';
            char cb[64];
            compact_spaces(cb, sizeof(cb), one);
            char eb[40];
            fit(eb, sizeof(eb), cb, DISPLAY_FONT_TINY, w->w - 8);
            display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 16 + i * 10),
                              eb, DISPLAY_FONT_TINY, UI_FG, UI_PANEL,
                              DISPLAY_DATUM_TL);
            if (!sep) { break; }
            p = sep + 3;
        }
        return;
    }

    display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 3), "RDS",
                      DISPLAY_FONT_TINY, UI_ACC_HI, UI_PANEL, DISPLAY_DATUM_TL);
    if (s->rds) {
        display_draw_text((int16_t)(w->x + w->w - 4), (int16_t)(w->y + 3),
                          rds_pty_name(s->rds->pty), DISPLAY_FONT_TINY,
                          UI_MUTED, UI_PANEL, DISPLAY_DATUM_TR);
        const char *ps = (s->rds->ps_valid && s->rds->ps[0]) ? s->rds->ps : "--";
        static int rds_off[8];
        char pr[40];
        size_t plen = strlen(ps);
        if (plen > 3 && display_text_width(ps, DISPLAY_FONT_TINY) >
            (uint16_t)(w->w - 8)) {
            int off = rds_off[idx] % (int)plen;
            char rot[128];
            size_t n = plen;
            if (n > sizeof(rot) - 1) {
                n = sizeof(rot) - 1;
            }
            for (size_t i = 0; i < n; ++i) {
                rot[i] = ps[(off + i) % plen];
            }
            rot[n] = '\0';
            fit(pr, sizeof(pr), rot, DISPLAY_FONT_TINY, w->w - 8);
            static int rds_skip[8];
            if (++rds_skip[idx] >= 4) {
                rds_skip[idx] = 0;
                ++rds_off[idx];
            }
        } else {
            fit(pr, sizeof(pr), ps, DISPLAY_FONT_TINY, w->w - 8);
        }
        display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 16), pr,
                          DISPLAY_FONT_TINY, UI_FG, UI_PANEL,
                          DISPLAY_DATUM_TL);
        if (s->rds->rt_valid && s->rds->rt[0]) {
            char rt[40];
            fit(rt, sizeof(rt), s->rds->rt, DISPLAY_FONT_TINY, w->w - 8);
            display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 30), rt,
                              DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL,
                              DISPLAY_DATUM_TL);
        }
    }
}


static void draw_sig(const uc_widget_t *w, const ui_state_t *s)
{
    display_fill_rect((int16_t)w->x, (int16_t)w->y, (uint16_t)w->w,
                      (uint16_t)w->h, UI_PANEL);
    display_draw_rect((int16_t)w->x, (int16_t)w->y, (uint16_t)w->w,
                      (uint16_t)w->h, UI_LINE);
    display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 3), "SIG",
                      DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);

    enum { UC_HIST = 40 };
    static uint8_t hist_r[UC_HIST];
    static uint8_t hist_s[UC_HIST];
    static int hist_n;
    static int hist_pos;
    static uint32_t hist_ms;

    uint32_t now = now_ms();
    uint8_t r = s->have_rsq ? s->rsq.rssi : 0;
    uint8_t sn = s->have_rsq ? s->rsq.snr : 0;
    if (hist_n == 0) {
        for (int i = 0; i < UC_HIST; ++i) {
            hist_r[i] = r;
            hist_s[i] = sn;
        }
        hist_n = UC_HIST;
        hist_pos = 0;
        hist_ms = now;
    } else if ((uint32_t)(now - hist_ms) >= 250u) {
        hist_ms = now;
        hist_r[hist_pos] = r;
        hist_s[hist_pos] = sn;
        hist_pos = (hist_pos + 1) % UC_HIST;
    }

    const int gx = w->x + 4;
    const int gy = w->y + 14;
    const int gw = w->w - 8;
    const int gh = w->h - 18;
    if (gw < 4 || gh < 4) {
        return;
    }
    display_fill_rect((int16_t)(gx + gw / 2), (int16_t)gy, 1, (uint16_t)gh,
                      UI_TRACK);
    display_fill_rect((int16_t)gx, (int16_t)(gy + gh - 1), (uint16_t)gw, 1,
                      UI_LINE);

    for (int i = 0; i < UC_HIST - 1; ++i) {
        int idx0 = (hist_pos + i) % UC_HIST;
        int idx1 = (hist_pos + i + 1) % UC_HIST;
        int x0 = gx + (i * (gw - 1)) / (UC_HIST - 1);
        int x1 = gx + ((i + 1) * (gw - 1)) / (UC_HIST - 1);
        int y0r = gy + gh - 2 - (hist_r[idx0] * (gh - 3)) / 63;
        int y1r = gy + gh - 2 - (hist_r[idx1] * (gh - 3)) / 63;
        int y0s = gy + gh - 2 - (hist_s[idx0] * (gh - 3)) / 63;
        int y1s = gy + gh - 2 - (hist_s[idx1] * (gh - 3)) / 63;
        display_draw_line((int16_t)x0, (int16_t)y0r, (int16_t)x1,
                          (int16_t)y1r, UI_OK);
        display_draw_line((int16_t)x0, (int16_t)y0s, (int16_t)x1,
                          (int16_t)y1s, UI_ACC);
    }
}

static void draw_bandmap(const uc_widget_t *w, const ui_state_t *s)
{
    display_draw_rect((int16_t)w->x, (int16_t)w->y, (uint16_t)w->w,
                      (uint16_t)w->h, UI_LINE);

    const char *lab = "";
    if (s->mode == RADIO_MODE_FM) {
        lab = (s->rds && s->rds->ps_valid && s->rds->ps[0]) ? s->rds->ps : "RDS --";
    } else {
        lab = (s->station && s->station[0]) ? s->station : "EIBI --";
    }
    char chip[40];
    fit(chip, sizeof(chip), lab, DISPLAY_FONT_TINY, w->w - 40);
    uint16_t tw = display_text_width(chip, DISPLAY_FONT_TINY);
    int tx = w->x + (w->w - (int)tw) / 2;
    display_fill_round_rect((int16_t)(tx - 6), (int16_t)(w->y + 2),
                            (uint16_t)(tw + 12), 13, 6, UI_BG);
    display_draw_text((int16_t)tx, (int16_t)(w->y + 4), chip, DISPLAY_FONT_TINY,
                      UI_FG, UI_BG, DISPLAY_DATUM_TL);

    const band_desc_t *bd = &band_table[s->band_idx];
    uint32_t step = bd->step_hz ? bd->step_hz : 100000u;
    int cx = w->x + w->w / 2;
    int mw = w->w - 20;
    int base = w->y + w->h - 17;
    int64_t half = (int64_t)step * 10;
    int64_t center = (int64_t)s->freq_hz;

    display_fill_rect((int16_t)(w->x + 8), (int16_t)base,
                      (uint16_t)(w->w - 16), 1, UI_LINE);

    int64_t f0 = ((center - half) / (int64_t)step) * (int64_t)step;
    for (int64_t f = f0; f <= center + half; f += (int64_t)step) {
        if (f < (int64_t)bd->min_hz || f > (int64_t)bd->max_hz) {
            continue;
        }
        int px = cx + (int)((f - center) * (mw / 2) / half);
        bool major = (((f / (int64_t)step) % 5) == 0);
        int th = major ? 8 : 4;
        display_fill_rect((int16_t)px, (int16_t)(base - th), 1, (uint16_t)th,
                          major ? UI_MUTED : UI_LINE);
        if (major && px > w->x + 26 && px < w->x + w->w - 26) {
            char lb[16];
            if (s->mode == RADIO_MODE_FM) {
                snprintf(lb, sizeof(lb), "%lu.%02lu",
                         (unsigned long)(f / 1000000),
                         (unsigned long)((f / 10000) % 100));
            } else {
                snprintf(lb, sizeof(lb), "%lu", (unsigned long)(f / 1000));
            }
            display_draw_text((int16_t)px, (int16_t)(base + 3), lb,
                              DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                              DISPLAY_DATUM_TC);
        }
    }

    uint32_t marks[64];
    int n = band_marks(s->band_idx, marks, 64);
    for (int i = 0; i < n; ++i) {
        int64_t f = (int64_t)marks[i];
        if (f < center - half || f > center + half) { continue; }
        int px = cx + (int)((f - center) * (mw / 2) / half);
        display_fill_rect((int16_t)px, (int16_t)(base - 13), 2, 4, UI_ACC);
    }

    display_fill_rect((int16_t)(cx - 1), (int16_t)(w->y + 17), 2,
                      (uint16_t)(base - (w->y + 17) + 3), UI_FG);
    display_fill_rect((int16_t)(cx - 3), (int16_t)(w->y + 16), 7, 3, UI_FG);
}

/* Wi-Fi 신호 막대 (4단계): 펌웨어 기본 표시와 동일 규칙 */
/* 디코드 패널: 상태/레벨/텍스트 4줄 */
static int opt_int(const char *s, const char *key, int def)
{
    if (!s) {
        return def;
    }
    size_t kl = strlen(key);
    const char *p = s;
    while ((p = strstr(p, key)) != NULL) {
        p += kl;
        if (*p == '=') {
            return atoi(p + 1);
        }
    }
    return def;
}

/* 배터리 아이콘: 펌웨어 battery_pill 과 동일한 모양 */
static void draw_batt_icon(const uc_widget_t *w, const ui_state_t *s)
{
    int max = w->max ? (int)w->max : 100;
    int val = num_bind(w->bind, s);
    if (val < 0) { val = 0; }
    if (val > max) { val = max; }

    /* 사각 게이지: 테두리 + 채움 (단자는 text 에 noterm 이 없을 때만) */
    display_fill_rect(w->x, w->y, w->w, w->h, col(UC_PANEL));
    display_draw_rect(w->x, w->y, w->w, w->h, col(UC_LINE));
    if (!(w->text && strstr(w->text, "noterm"))) {
        display_fill_rect((int16_t)(w->x + w->w), (int16_t)(w->y + w->h / 3),
                          2, (int16_t)(w->h / 3 > 0 ? w->h / 3 : 1),
                          col(UC_LINE));
    }
    int fill_w = (w->w - 2) * val / (max > 0 ? max : 1);
    if (fill_w > 0) {
        display_fill_rect((int16_t)(w->x + 1), (int16_t)(w->y + 1),
                          (uint16_t)fill_w, (uint16_t)(w->h - 2),
                          col(w->color));
    }
}

static void draw_decode_panel(const uc_widget_t *w)
{
    /* 옵션 문자열: "gpio=4;mode=0;lines=4" (위젯 text 필드) */
    int gpio = opt_int(w->text, "gpio", -1);
    int mode = opt_int(w->text, "mode", -1);
    int lines = opt_int(w->text, "lines", 0);
    if (gpio >= 0) {
        decode_set_gpio(gpio);
    }
    if (mode >= 0) {
        decode_set_mode(mode);
    }

    display_fill_round_rect(w->x, w->y, w->w, w->h, 8, col(UC_PANEL));
    display_draw_round_rect(w->x, w->y, w->w, w->h, 8, col(UC_LINE));

    char title[40];
    snprintf(title, sizeof(title), "DECODE %s  GPIO %d",
             decode_mode_str(), decode_gpio());
    display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 2), title,
                      DISPLAY_FONT_TINY, col(UC_ACCENT), col(UC_PANEL),
                      DISPLAY_DATUM_TL);

    const char *st = strncmp(decode_text(), "ADC ERR", 7) == 0 ? "ERR" : "RUN";
    display_draw_text((int16_t)(w->x + w->w - 4), (int16_t)(w->y + 2), st,
                      DISPLAY_FONT_TINY, col(st[0] == 'E' ? UC_RED : UC_OK),
                      col(UC_PANEL), DISPLAY_DATUM_TR);

    display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 14), "SIG",
                      DISPLAY_FONT_TINY, col(UC_MUTED), col(UC_PANEL),
                      DISPLAY_DATUM_TL);
    int bw = w->w - 26;
    if (bw < 8) { bw = 8; }
    display_fill_rect((int16_t)(w->x + 26), (int16_t)(w->y + 16),
                      (uint16_t)bw, 4, col(UC_TRACK));
    int lv = decode_level();
    if (lv < 0) { lv = 0; }
    if (lv > 100) { lv = 100; }
    display_fill_rect((int16_t)(w->x + 26), (int16_t)(w->y + 16),
                      (uint16_t)(bw * lv / 100), 4, col(UC_OK));

    const char *txt = decode_text();
    int len = (int)strlen(txt);
    if (lines <= 0) {
        lines = (w->h - 30) / 13;
    }
    if (lines < 1) { lines = 1; }
    if (lines > 4) { lines = 4; }
    if (len == 0) {
        display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 26), "WAIT...",
                          DISPLAY_FONT_SMALL, col(UC_MUTED), col(UC_PANEL),
                          DISPLAY_DATUM_TL);
    } else {
        int per = 44;
        int start = (len > per * lines) ? len - per * lines : 0;
        for (int i = 0; i < lines && start < len; ++i) {
            int n = len - start;
            if (n > per) { n = per; }
            char buf[48];
            memcpy(buf, txt + start, (size_t)n);
            buf[n] = '\0';
            display_draw_text((int16_t)(w->x + 4),
                              (int16_t)(w->y + 26 + i * 13), buf,
                              DISPLAY_FONT_SMALL, col(UC_FG), col(UC_PANEL),
                              DISPLAY_DATUM_TL);
            start += n;
        }
    }
}

static void draw_wifi(const uc_widget_t *w, const ui_state_t *s)
{
    int bars = 0;
    if (w->max) {
        /* 공용 사용: 속성에서 지정한 막대 수 */
        bars = (int)w->max;
        if (bars > 4) { bars = 4; }
    } else if (s->wifi_state == 1) {
        bars = 4;
    } else if (s->wifi_state == 2) {
        int r = s->wifi_rssi;
        bars = (r >= -55) ? 4 : (r >= -65) ? 3 : (r >= -75) ? 2 : 1;
    } else if (s->wifi_state == 3) {
        bars = 4;
    }
    if (bars <= 0) {
        return;
    }
    uint16_t color = col(w->color ? w->color : (uint8_t)UC_ACCENTH);
    int bw = w->w / 8;
    if (bw < 2) { bw = 2; }
    int gap = bw / 2;
    if (gap < 1) { gap = 1; }
    int total = 4 * bw + 3 * gap;
    int x0 = w->x + (w->w - total) / 2;
    if (x0 < w->x) { x0 = w->x; }
    static const uint8_t rel[4] = {3, 5, 7, 9};
    for (int i = 0; i < bars; ++i) {
        int h = w->h * rel[i] / 9;
        if (h < 2) { h = 2; }
        display_fill_rect((int16_t)(x0 + i * (bw + gap)),
                          (int16_t)(w->y + w->h - h),
                          (uint16_t)bw, (uint16_t)h, color);
    }
}

void ui_custom_render(const ui_state_t *s)
{
    char buf[64];
    const char *unit = "";
    uint8_t mask = (s->mode == RADIO_MODE_FM) ? UC_M_FM
                 : (s->mode == RADIO_MODE_AM) ? UC_M_AM : UC_M_SSB;

    display_fill_screen(UI_BG);

    const uc_widget_t *list = ui_runtime_enabled() ? ui_runtime_widgets() : uc_widgets;
    int count = ui_runtime_enabled() ? ui_runtime_count() : UC_WIDGET_COUNT;
    for (int i = 0; i < count; ++i) {
        const uc_widget_t *w = &list[i];
        if (w->w <= 0 || w->h <= 0) {
            continue;
        }
        if (w->modes && !(w->modes & mask)) {
            continue;
        }
        switch (w->type) {
        case UC_RECT:
            if (w->fill != UC_NONE) {
                if (w->x == 0 && w->y == 0 && w->w >= 320) {
                    display_fill_rect((int16_t)w->x, (int16_t)w->y,
                                      (uint16_t)w->w, (uint16_t)w->h,
                                      col(w->fill));
                } else {
                    display_fill_rect((int16_t)w->x, (int16_t)w->y,
                                      (uint16_t)w->w, (uint16_t)w->h,
                                      col(w->fill));
                }
            }
            if (w->stroke != UC_NONE && w->stroke != 0) {
                display_draw_rect((int16_t)w->x, (int16_t)w->y,
                                  (uint16_t)w->w, (uint16_t)w->h,
                                  col(w->stroke));
            }
            break;
        case UC_LINE_W:
            display_fill_rect((int16_t)w->x, (int16_t)w->y, (uint16_t)w->w,
                              (uint16_t)(w->h > 0 ? w->h : 1),
                              col(w->color));
            break;
        case UC_TEXT: {
            const char *t = w->bind ? str_bind(w->bind, s, buf, sizeof(buf))
                                    : w->text;
            draw_text(w, t);
            break;
        }
        case UC_BAR:
            draw_bar(w, num_bind(w->bind, s));
            break;
        case UC_FREQ: {
            char num[24];
            uint16_t pct = w->scale ? w->scale : 100;
            fmt_freq(s->freq_hz, s->mode, s->unit_mode, num, sizeof(num), &unit);
            display_draw_text_pct((int16_t)(w->x + 2), (int16_t)w->y, num,
                                  DISPLAY_FONT_LARGE, UI_FG, UI_BG,
                                  DISPLAY_DATUM_TL, pct);
            uint16_t nw = display_text_width_pct(num, DISPLAY_FONT_LARGE, pct);
            display_draw_text_pct((int16_t)(w->x + 6 + nw),
                                  (int16_t)(w->y + 10 * pct / 100),
                                  unit, DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                                  DISPLAY_DATUM_TL, pct);
            break;
        }
        case UC_RSQ:
            draw_rsq(w, s);
            break;
        case UC_RDS:
            draw_rds(w, s);
            break;
        case UC_SIG:
            draw_sig(w, s);
            break;
        case UC_BANDMAP:
            draw_bandmap(w, s);
            break;
        case UC_CUSTOM:
            if (w->fn) {
                w->fn(w->x, w->y, w->w, w->h);
            }
            break;
        case UC_MENU_GUIDE:
            break;
        case UC_WIFI:
            draw_wifi(w, s);
            break;
        case UC_DECODE:
            draw_decode_panel(w);
            break;
        case UC_BATT_ICON:
            draw_batt_icon(w, s);
            break;
        }
    }
}
