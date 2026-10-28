/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * DX / panel layouts and radio composite screens.
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "display.h"
#include "ui.h"
#include "ui_custom.h"
#include "ui_runtime.h"
#include "eibi.h"
#include "decode.h"
#include "ui_int.h"
#include "ui_asset.h"
#define ADV_HIST 64
static uint8_t s_hist_rssi[ADV_HIST];
static uint8_t s_hist_snr[ADV_HIST];
static int s_hist_n;
static int s_hist_pos;
static uint32_t s_eibi_freq;
static int64_t  s_eibi_utc_min = -1;
static char     s_eibi_name[160];



/* DX 시그너처 지오는 ui_draw_dx_signal/2(미사용)와 함께 제거됨 */







static void band_meta(const ui_state_t *s, char *buf, size_t n)
{
    const char *bn = band_table[s->band_idx].name;
    if (strcmp(bn, "SW") == 0) {
        uint32_t khz = s->freq_hz / 1000;
        int m = khz ? (int)((300000u + khz / 2) / khz) : 0;
        snprintf(buf, n, "%s %s %02dM", bn, ui_mode_str(s->mode), m);
    } else {
        snprintf(buf, n, "%s %s", bn, ui_mode_str(s->mode));
    }
}

static void draw_saved_badges(const ui_state_t *s)
{
    char vb[24];
    snprintf(vb, sizeof(vb), "VOL %u  BRT %u", s->volume, s_brightness);
    int bx = 312 - display_text_width(vb, DISPLAY_FONT_TINY) - 8;
    if (bx < 52) {
        bx = 52;
    }
    /* 배지가 사라진 경우를 포함해 배지 영역을 먼저 지운다 */
    display_fill_rect(52, 22, (int16_t)(bx - 52), 20, UI_BG);
    if (s->ats_slot >= 0) {
        char a[16];
        snprintf(a, sizeof(a), "A%d", s->ats_slot + 1);
        pill(bx - 46, 24, a, 46, UI_BLACK, UI_GOLD);
        bx -= 52;
    }
    if (s->preset_slot >= 0) {
        char p[16];
        snprintf(p, sizeof(p), "P%d", s->preset_slot + 1);
        pill(bx - 46, 24, p, 46, UI_BLACK, UI_ACC);
    }
}

void ui_draw_band_meta(const ui_state_t *s)
{
    if (ui_custom_active()) {
        return;
    }
    display_fill_rect(0, 22, META_CLEAR_W, 18, UI_BG);

    char meta[48];
    band_meta(s, meta, sizeof(meta));
    uint16_t mw = display_draw_text(12, 24, meta, DISPLAY_FONT_SMALL,
                                    UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
    if (s->mode == RADIO_MODE_FM && s->stereo) {
        display_draw_text((int16_t)(12 + mw + 6), 29, "STEREO",
                          DISPLAY_FONT_TINY, UI_OK, UI_BG, DISPLAY_DATUM_TL);
    }
}

static void draw_freq_text(int x, int y, const char *num, display_font_t font,
                           uint16_t bg)
{
    display_draw_text((int16_t)x, (int16_t)y, num, font, UI_FG, bg,
                      DISPLAY_DATUM_TL);
    if (!s_digit_mode) {
        return;
    }
    int dc = 0;
    for (int i = 0; num[i]; ++i) {
        if (isdigit((unsigned char)num[i])) {
            dc++;
        }
    }
    int target = dc - 1 - s_digit_place;
    if (target < 0 || target >= dc) {
        return;
    }
    int k = 0;
    int ci = -1;
    for (int i = 0; num[i]; ++i) {
        if (isdigit((unsigned char)num[i])) {
            if (k == target) {
                ci = i;
                break;
            }
            k++;
        }
    }
    if (ci < 0) {
        return;
    }
    char pre[24];
    int pl = ci;
    if (pl > (int)sizeof(pre) - 1) {
        pl = (int)sizeof(pre) - 1;
    }
    memcpy(pre, num, (size_t)pl);
    pre[pl] = '\0';
    int dx = x + display_text_width(pre, font);
    char one[2] = { num[ci], '\0' };
    display_draw_text((int16_t)dx, (int16_t)y, one, font, UI_BLUE, bg,
                      DISPLAY_DATUM_TL);
}

static void adv_hist_add(const ui_state_t *s)
{
    if (!s->have_rsq) {
        return;
    }
    int r = s->rsq.rssi;
    int n = s->rsq.snr;
    if (r > 63) { r = 63; }
    if (n > 63) { n = 63; }
    s_hist_rssi[s_hist_pos] = (uint8_t)r;
    s_hist_snr[s_hist_pos] = (uint8_t)n;
    s_hist_pos = (s_hist_pos + 1) % ADV_HIST;
    if (s_hist_n < ADV_HIST) {
        s_hist_n++;
    }
}

void ui_draw_adv_signal4(const ui_state_t *s)
{
    adv_hist_add(s);

    char fnum[20];
    const char *funit = "";
    ui_fmt_freq(s, fnum, sizeof(fnum), &funit);
    int ix = 12 + display_text_width(fnum, DISPLAY_FONT_LARGE) + 5 +
             display_text_width(funit, DISPLAY_FONT_SMALL) + 10;
    if (ix < 176) { ix = 176; }
    if (ix < 260) {
        int iw = 312 - ix;
        display_fill_rect((int16_t)ix, 36, (uint16_t)iw, 38, UI_BG);
        char ib[160];
        if (s->mode == RADIO_MODE_FM) {
            if (s->rds && s->rds->ps_valid) {
                fit_text(ib, sizeof(ib), s->rds->ps, DISPLAY_FONT_SMALL, iw);
                display_draw_text((int16_t)ix, 38, ib, DISPLAY_FONT_SMALL,
                                  UI_FG, UI_BG, DISPLAY_DATUM_TL);
            }
            if (s->rds && s->rds->rt_valid && s->rds->rt[0]) {
                fit_text(ib, sizeof(ib), s->rds->rt, DISPLAY_FONT_TINY, iw);
                display_draw_text((int16_t)ix, 56, ib, DISPLAY_FONT_TINY,
                                  UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
            }
            if (s->rds) {
                char cb[32];
                if (s->rds->ct_valid) {
                    snprintf(cb, sizeof(cb), "CT %02d:%02d",
                             (int)s->rds->hour, (int)s->rds->minute);
                } else {
                    snprintf(cb, sizeof(cb), "PTY %s",
                             rds_pty_name(s->rds->pty));
                }
                fit_text(ib, sizeof(ib), cb, DISPLAY_FONT_TINY, iw);
                display_draw_text((int16_t)ix, 64, ib, DISPLAY_FONT_TINY,
                                  UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
            }
        } else {
            int64_t utc = eibi_utc_now();
            int64_t utc_min = utc / 60;
            if (s->freq_hz != s_eibi_freq || utc_min != s_eibi_utc_min) {
                s_eibi_freq = s->freq_hz;
                s_eibi_utc_min = utc_min;
                s_eibi_name[0] = '\0';
                eibi_lookup((uint16_t)(s->freq_hz / 1000), utc,
                            s_eibi_name, sizeof(s_eibi_name));
            }
            if (s_eibi_name[0]) {
                fit_text(ib, sizeof(ib), s_eibi_name, DISPLAY_FONT_SMALL, iw);
                display_draw_text((int16_t)ix, 38, ib, DISPLAY_FONT_SMALL,
                                  UI_FG, UI_BG, DISPLAY_DATUM_TL);
            }
        }
    }

    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    if (rssi > 63) { rssi = 63; }
    if (snr > 63) { snr = 63; }
    char v[16];

    display_draw_text(12, 74, "RSSI", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(56, 78, 196, 8, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_fill_rect(286, 74, 28, 16, UI_BG);
    display_draw_text(312, 74, v, DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);

    display_draw_text(12, 94, "SNR", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(56, 98, 196, 8, snr, 63, snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_fill_rect(286, 94, 28, 16, UI_BG);
    display_draw_text(312, 94, v, DISPLAY_FONT_SMALL,
                      snr < 10 ? UI_WARN : UI_FG, UI_BG, DISPLAY_DATUM_TR);

    display_fill_rect(8, 118, 304, 1, UI_LINE);

    char pre[48];
    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));
    const char *afc = !s->have_rsq ? "--" : (s->rsq.afc_rail ? "RA" : "OK");
    bool afc_active = s->have_rsq &&
                      (s->rsq.afc_rail || s->rsq.freq_offset != 0);
    snprintf(pre, sizeof(pre), "BW %s  ST %s  AFC ", ui_bw_label(s), stp);
    display_fill_rect(8, 122, 224, 14, UI_BG);
    display_draw_text(8, 122, pre, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    int vx = 8 + display_text_width(pre, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)vx, 122, afc, DISPLAY_FONT_TINY,
                      afc_active ? UI_RED : UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    char smv[8];
    if (s_softmute == 0) {
        snprintf(smv, sizeof(smv), "OFF");
    } else {
        snprintf(smv, sizeof(smv), "%u", s_softmute);
    }
    snprintf(pre, sizeof(pre), "BFO %+ld  AGC %s  SM ", (long)s_bfo,
             s_agc_on ? "ON" : "OFF");
    display_fill_rect(8, 136, 224, 14, UI_BG);
    display_draw_text(8, 136, pre, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    vx = 8 + display_text_width(pre, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)vx, 136, smv, DISPLAY_FONT_TINY,
                      s_softmute ? UI_RED : UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);

    int sq = ui_mode_slot(s->mode);
    char sqv[16];
    if (s_squelch_on) {
        snprintf(sqv, sizeof(sqv), "%s %u",
                 s_squelch_type[sq] ? "SNR" : "RSSI", s_squelch_thr[sq]);
    } else {
        snprintf(sqv, sizeof(sqv), "OFF");
    }
    snprintf(pre, sizeof(pre), "AVC %u  SQ ", s_avc);
    display_fill_rect(8, 150, 224, 14, UI_BG);
    display_draw_text(8, 150, pre, DISPLAY_FONT_TINY, UI_FG, UI_BG,
                      DISPLAY_DATUM_TL);
    vx = 8 + display_text_width(pre, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)vx, 150, sqv, DISPLAY_FONT_TINY,
                      (s_squelch_on && s_squelch_muted) ? UI_RED : UI_MUTED,
                      UI_BG, DISPLAY_DATUM_TL);
    if (s->stereo) {
        vx += display_text_width(sqv, DISPLAY_FONT_TINY) + 12;
        display_draw_text((int16_t)vx, 150, "STEREO", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
    }

    int gx = 236;
    int gy = 124;
    int gw = 76;
    int gh = 40;
    display_fill_rect(gx, gy, gw, gh, UI_BG);
    display_fill_rect(gx + gw / 2, gy, 1, (uint16_t)gh, UI_TRACK);
    display_fill_rect(gx, gy + gh - 1, gw, 1, UI_LINE);
    if (s_hist_n >= 2) {
        int start = (s_hist_pos - s_hist_n + ADV_HIST) % ADV_HIST;

        int px = -1;
        int py = 0;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_rssi[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_OK);
            }
            px = x;
            py = y;
        }

        px = -1;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_snr[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_ACC);
            }
            px = x;
            py = y;
        }
    }
}

static const char *eibi_current(const ui_state_t *s)
{
    int64_t utc = eibi_utc_now();
    int64_t utc_min = utc / 60;
    if (s->freq_hz != s_eibi_freq || utc_min != s_eibi_utc_min) {
        s_eibi_freq = s->freq_hz;
        s_eibi_utc_min = utc_min;
        s_eibi_name[0] = '\0';
        eibi_lookup((uint16_t)(s->freq_hz / 1000), utc,
                    s_eibi_name, sizeof(s_eibi_name));
    }
    return s_eibi_name;
}







static void marquee_line(int x, int y, int w, const char *text,
                         display_font_t font, uint16_t fg, uint16_t bg,
                         int *off)
{
    if (!text || !*text || w <= 0) {
        return;
    }
    int tw = display_text_width(text, font);
    if (tw <= w) {
        display_draw_text((int16_t)x, (int16_t)y, text, font, fg, bg,
                          DISPLAY_DATUM_TL);
        return;
    }
    int span = tw + 24;
    *off = (*off + 1) % span;
    display_set_clip((int16_t)x, (int16_t)y, (uint16_t)w, 16);
    display_draw_text((int16_t)(x - *off), (int16_t)y, text, font, fg, bg,
                      DISPLAY_DATUM_TL);
    display_draw_text((int16_t)(x - *off + span), (int16_t)y, text, font, fg,
                      bg, DISPLAY_DATUM_TL);
    display_clear_clip();
}

static int adv_kv(int x, int y, const char *label, const char *value,
                  uint16_t value_color)
{
    display_draw_text((int16_t)x, (int16_t)y, label, DISPLAY_FONT_TINY, UI_FG,
                      UI_BG, DISPLAY_DATUM_TL);
    x += display_text_width(label, DISPLAY_FONT_TINY);
    display_draw_text((int16_t)x, (int16_t)y, value, DISPLAY_FONT_TINY,
                      value_color, UI_BG, DISPLAY_DATUM_TL);
    return x + display_text_width(value, DISPLAY_FONT_TINY);
}

void ui_draw_adv_signal4b(const ui_state_t *s)
{
    if (ui_custom_active()) {
        ui_custom_render(s);
        return;
    }
    s_radio_draw_seq++;
    adv_hist_add(s);

    int rssi = s->have_rsq ? s->rsq.rssi : 0;
    int snr = s->have_rsq ? s->rsq.snr : 0;
    if (rssi > 63) { rssi = 63; }
    if (snr > 63) { snr = 63; }
    char v[16];

    display_draw_text(12, 74, "RSSI", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(52, 78, 88, 8, rssi, 63, UI_OK);
    snprintf(v, sizeof(v), "%d", rssi);
    display_fill_rect(144, 74, 34, 16, UI_BG);
    display_draw_text(174, 74, v, DISPLAY_FONT_SMALL, UI_FG, UI_BG,
                      DISPLAY_DATUM_TR);

    display_draw_text(12, 94, "SNR", DISPLAY_FONT_SMALL, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TL);
    bar(52, 98, 88, 8, snr, 63, snr < 10 ? UI_WARN : UI_ACC);
    snprintf(v, sizeof(v), "%d", snr);
    display_fill_rect(144, 94, 34, 16, UI_BG);
    display_draw_text(174, 94, v, DISPLAY_FONT_SMALL,
                      snr < 10 ? UI_WARN : UI_FG, UI_BG, DISPLAY_DATUM_TR);

    char stp[12];
    ui_fmt_step(s, stp, sizeof(stp));
    char pb[16];
    char smv[8];
    char sqv[16];
    display_fill_rect(176, 72, 138, 46, UI_BG);

    const char *afc = !s->have_rsq ? "--" : (s->rsq.afc_rail ? "RA" : "OK");
    bool afc_active = s->have_rsq &&
                      (s->rsq.afc_rail || s->rsq.freq_offset != 0);
    uint16_t afc_col = afc_active ? UI_RED : UI_MUTED;
    if (s_softmute == 0) {
        snprintf(smv, sizeof(smv), "OFF");
    } else {
        snprintf(smv, sizeof(smv), "%u", s_softmute);
    }
    int sq = ui_mode_slot(s->mode);
    if (s_squelch_on) {
        snprintf(sqv, sizeof(sqv), "%s%u",
                 s_squelch_type[sq] ? "S" : "R", s_squelch_thr[sq]);
    } else {
        snprintf(sqv, sizeof(sqv), "OFF");
    }

    int x = 180;
    x = adv_kv(x, 74, "BW ", ui_bw_label(s), UI_MUTED);
    adv_kv(x + 6, 74, "ST ", stp, UI_MUTED);

    x = 180;
    snprintf(pb, sizeof(pb), "%+ld", (long)s_bfo);
    x = adv_kv(x, 90, "BFO ", pb, UI_MUTED);
    x = adv_kv(x + 6, 90, "AGC ", s_agc_on ? "ON" : "OFF", UI_MUTED);
    adv_kv(x + 6, 90, "SM ", smv, s_softmute ? UI_RED : UI_MUTED);

    x = 180;
    x = adv_kv(x, 106, "AFC ", afc, afc_col);
    snprintf(pb, sizeof(pb), "%u", s_avc);
    x = adv_kv(x + 6, 106, "AVC ", pb, UI_MUTED);
    adv_kv(x + 6, 106, "SQ ", sqv,
           (s_squelch_on && s_squelch_muted) ? UI_RED : UI_MUTED);

    display_fill_rect(8, 118, 304, 1, UI_LINE);

    int cy = 122;
    int ch = 46;
    int cw = 200;

    if (s_sig_view == 1) {
        /* BAND MAP: 以묒븰 怨좎젙 諛붾뒛 + ?ㅽ겕濡ㅻ릺???덇툑 */
        int bx = 8;
        int bw = 304;
        display_draw_rect(bx, cy, bw, ch, UI_LINE);

        const char *txt;
        if (s->mode == RADIO_MODE_FM) {
            if (s->rds && s->rds->ps_valid && s->rds->ps[0]) {
                txt = s->rds->ps;
            } else if (s->rds && s->rds->rt_valid && s->rds->rt[0]) {
                txt = s->rds->rt;
            } else {
                txt = "RDS --";
            }
        } else {
            const char *en = eibi_current(s);
            txt = (en && en[0]) ? en : "EIBI --";
        }
        char chip[40];
        fit_text(chip, sizeof(chip), txt, DISPLAY_FONT_TINY, bw - 40);
        uint16_t tw = display_text_width(chip, DISPLAY_FONT_TINY);
        int tx = bx + (bw - (int)tw) / 2;
        display_fill_round_rect((int16_t)(tx - 6), (int16_t)(cy + 2),
                                (uint16_t)(tw + 12), 13, 6, UI_BG);
        display_draw_text((int16_t)tx, (int16_t)(cy + 4), chip,
                          DISPLAY_FONT_TINY, UI_FG, UI_BG,
                          DISPLAY_DATUM_TL);

        const band_desc_t *bd = &band_table[s->band_idx];
        uint32_t step = bd->step_hz ? bd->step_hz : 100000u;
        int cx = bx + bw / 2;
        int mw = bw - 20;
        int base = cy + 29;
        int64_t half = (int64_t)step * 10;
        int64_t center = (int64_t)s->freq_hz;

        display_fill_rect(bx + 8, base, bw - 16, 1, UI_LINE);

        int64_t f0 = ((center - half) / (int64_t)step) * (int64_t)step;
        for (int64_t f = f0; f <= center + half; f += (int64_t)step) {
            if (f < (int64_t)bd->min_hz || f > (int64_t)bd->max_hz) {
                continue;
            }
            int x = cx + (int)((f - center) * (mw / 2) / half);
            bool major = (((f / (int64_t)step) % 5) == 0);
            int th = major ? 8 : 4;
            display_fill_rect((int16_t)x, (int16_t)(base - th), 1,
                              (uint16_t)th, major ? UI_MUTED : UI_LINE);
            if (major && x > bx + 26 && x < bx + bw - 26) {
                char lb[16];
                fmt_freq_short((uint32_t)f, s->mode, lb, sizeof(lb));
                display_draw_text((int16_t)x, (int16_t)(base + 3), lb,
                                  DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                                  DISPLAY_DATUM_TC);
            }
        }

        uint32_t marks[64];
        int n = band_marks(s->band_idx, marks, 64);
        for (int i = 0; i < n; ++i) {
            int64_t f = (int64_t)marks[i];
            if (f < center - half || f > center + half) {
                continue;
            }
            int x = cx + (int)((f - center) * (mw / 2) / half);
            display_fill_rect((int16_t)x, (int16_t)(base - 13), 2, 4, UI_ACC);
        }

        /* 以묒븰 怨좎젙 諛붾뒛 */
        display_fill_rect((int16_t)(cx - 1), (int16_t)(cy + 17), 2,
                          (uint16_t)(base - (cy + 17) + 3), UI_FG);
        display_fill_rect((int16_t)(cx - 3), (int16_t)(cy + 16), 7, 3, UI_FG);
        return;
    }
    display_fill_rect(8, cy, cw, ch, UI_PANEL);
    display_draw_rect(8, cy, cw, ch, UI_LINE);
    if (s->mode == RADIO_MODE_FM) {
        static int rds_off;
        static int rt_off;
        display_draw_text(12, cy + 3, "RDS", DISPLAY_FONT_TINY, UI_ACC_HI,
                          UI_PANEL, DISPLAY_DATUM_TL);
        {
            /* 즐겨찾기 이름이 있으면 RDS 헤더 옆에 표시 */
            const char *nm = preset_name_at(s->preset_slot);
            if (nm && nm[0]) {
                char nb[20];
                fit_text(nb, sizeof(nb), nm, DISPLAY_FONT_TINY, 64);
                display_draw_text(34, cy + 3, nb, DISPLAY_FONT_TINY, UI_ACC,
                                  UI_PANEL, DISPLAY_DATUM_TL);
            }
        }
        if (s->rds) {
            const rds_info_t *r = s->rds;
            int sx = 8 + cw - 4;
            if (r->eon_valid) {
                char eb[24];
                char tmp[24];
                if (r->eon_ps_valid && r->eon_ps[0]) {
                    snprintf(tmp, sizeof(tmp), "EON %s", r->eon_ps);
                } else {
                    snprintf(tmp, sizeof(tmp), "EON");
                }
                fit_text(eb, sizeof(eb), tmp, DISPLAY_FONT_TINY, 88);
                uint16_t ew = display_text_width(eb, DISPLAY_FONT_TINY);
                display_draw_text((int16_t)sx, cy + 3, eb,
                                  DISPLAY_FONT_TINY,
                                  r->eon_ta ? UI_WARN : UI_ACC_HI,
                                  UI_PANEL, DISPLAY_DATUM_TR);
                sx -= (int)ew + 6;
            }
            if (r->ta) {
                display_draw_text((int16_t)sx, cy + 3, "TA",
                                  DISPLAY_FONT_TINY, UI_WARN, UI_PANEL,
                                  DISPLAY_DATUM_TR);
                sx -= 14;
            }
            if (r->tp) {
                display_draw_text((int16_t)sx, cy + 3, "TP",
                                  DISPLAY_FONT_TINY, UI_OK, UI_PANEL,
                                  DISPLAY_DATUM_TR);
            }

            const char *ps = (r->ps_valid && r->ps[0]) ? r->ps : "--";
            marquee_line(12, cy + 16, cw - 8, ps, DISPLAY_FONT_TINY, UI_FG,
                         UI_PANEL, &rds_off);
            if (r->rt_valid && r->rt[0]) {
                marquee_line(12, cy + 30, cw - 8, s->rds->rt,
                             DISPLAY_FONT_TINY, UI_MUTED, UI_PANEL, &rt_off);
            }
        } else {
            display_draw_text(12, cy + 16, "RDS --", DISPLAY_FONT_TINY,
                              UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);
        }
    } else {
        static int ei_off;
        display_draw_text(12, cy + 3, "EIBI", DISPLAY_FONT_TINY, UI_GOLD,
                          UI_PANEL, DISPLAY_DATUM_TL);
        const char *en = eibi_current(s);
        if (en && en[0]) {
            marquee_line(12, cy + 16, cw - 8, en, DISPLAY_FONT_TINY, UI_FG,
                         UI_PANEL, &ei_off);
        } else {
            display_draw_text(12, cy + 16, "NO DATA", DISPLAY_FONT_TINY,
                              UI_MUTED, UI_PANEL, DISPLAY_DATUM_TL);
        }
    }

    int sqx = 212;
    int sw = 98;
    display_fill_rect(sqx, cy, sw, ch, UI_PANEL);
    display_draw_rect(sqx, cy, sw, ch, UI_LINE);
    display_draw_text(sqx + 4, cy + 3, "SIG", DISPLAY_FONT_TINY, UI_MUTED,
                      UI_PANEL, DISPLAY_DATUM_TL);

    int gx = sqx + 4;
    int gy = cy + 14;
    int gw = sw - 8;
    int gh = ch - 18;
    display_fill_rect(gx, gy, gw, gh, UI_PANEL);
    display_fill_rect(gx + gw / 2, gy, 1, (uint16_t)gh, UI_TRACK);
    display_fill_rect(gx, gy + gh - 1, gw, 1, UI_LINE);
    if (s_hist_n >= 2) {
        int start = (s_hist_pos - s_hist_n + ADV_HIST) % ADV_HIST;
        int px = -1;
        int py = 0;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_rssi[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_OK);
            }
            px = x;
            py = y;
        }
        px = -1;
        for (int i = 0; i < s_hist_n; ++i) {
            int idx = (start + i) % ADV_HIST;
            int x = gx + (ADV_HIST - s_hist_n + i) * (gw - 1) /
                        (ADV_HIST - 1);
            int y = gy + gh - 2 - (int)s_hist_snr[idx] * (gh - 3) / 63;
            if (px >= 0) {
                display_draw_line((int16_t)px, (int16_t)py, (int16_t)x,
                                  (int16_t)y, UI_ACC);
            }
            px = x;
            py = y;
        }
    }
}

/* Sleep 남은 시간: 'VOL/BRT' 줄 바로 아래(우측 정렬)에 표시.
   매초 부분 갱신할 수 있도록 별도 함수로 둔다. */
void ui_draw_sleep_badge(void)
{
    const int right = 312;
    /* BRT 줄(24)의 배지(pill) 아래, 주파수 큰 글씨 오른쪽 여백 */
    const int y = 42;
    const int clear_x = 186;
    const int clear_w = 134;

    if (!(s_sleep_min > 0 && s_sleep_armed)) {
        return;
    }
    int32_t remain = (int32_t)(s_sleep_deadline - now_ms());
    if (remain < 0) {
        remain = 0;
    }
    int sec = (int)((remain + 999) / 1000);
    if (sec > 99 * 60 + 59) {
        sec = 99 * 60 + 59;
    }
    char b[20];
    snprintf(b, sizeof(b), "S %d:%02d:%02d", sec / 3600, (sec / 60) % 60,
             sec % 60);
    display_fill_rect(clear_x, y - 2, clear_w, 14, UI_BG);
    /* DISPLAY_DATUM_TR: x가 오른쪽 끝 기준이므로 right를 그대로 넘긴다 */
    display_draw_text((int16_t)right, y, b, DISPLAY_FONT_TINY, UI_ACC,
                      UI_BG, DISPLAY_DATUM_TR);
}

/* ---------------- 에셋 (개별 요소, 자기 영역만 갱신) ---------------- */

void ui_asset_header(const ui_state_t *s)
{
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);
}

void ui_asset_band(const ui_state_t *s)
{
    ui_draw_band_meta(s);
}

void ui_asset_status(const ui_state_t *s)
{
    char vb[24];
    snprintf(vb, sizeof(vb), "VOL %u  BRT %u", s->volume, s_brightness);
    int tw = display_text_width(vb, DISPLAY_FONT_TINY);
    int tx = 312 - tw;
    if (tx < 52) {
        tx = 52;
    }
    display_fill_rect((int16_t)tx, 22, (uint16_t)(312 - tx + 2), 12, UI_BG);
    display_draw_text(312, 24, vb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);
}

void ui_asset_badges(const ui_state_t *s)
{
    draw_saved_badges(s);
}

void ui_asset_freq(const ui_state_t *s)
{
    char num[20];
    const char *unit = "";
    ui_fmt_freq(s, num, sizeof(num), &unit);

    /* 주파수 큰 글씨 영역(시그널 패널 y=72 위까지)만 지운다 */
    display_fill_rect(0, 38, UI_W, 33, UI_BG);
    draw_freq_text(12, 40, num, DISPLAY_FONT_LARGE, UI_BG);
    uint16_t fw = display_text_width(num, DISPLAY_FONT_LARGE);
    display_draw_text((int16_t)(12 + fw + 5), 50, unit, DISPLAY_FONT_SMALL,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TL);
}

void ui_asset_sig(const ui_state_t *s)
{
    ui_draw_adv_signal4b(s);
}

void ui_asset_sleep(const ui_state_t *s)
{
    (void)s;
    ui_draw_sleep_badge();
}

/* ---------------- 화면 조합 ---------------- */

static void ui_draw_all_adv4(const ui_state_t *s)
{
    s_radio_draw_seq++;
    display_fill_screen(UI_BG);
    ui_asset_draw(s, UI_ASSET_ALL);
}

void ui_draw_all(const ui_state_t *s)
{
    if (ui_custom_active()) {
        ui_custom_render(s);
        return;
    }
    s_radio_draw_seq++;
    ui_draw_all_adv4(s);
    return;
}

void ui_draw_radio_freq(const ui_state_t *s)
{
    if (ui_custom_active()) {
        return;
    }
    s_radio_draw_seq++;
    ui_draw_all(s);
    return;
}


void ui_draw_radio_panel(const ui_state_t *s)
{
    if (ui_custom_active()) {
        ui_custom_render(s);
        return;
    }
    ui_draw_all_adv4(s);
    return;
}
