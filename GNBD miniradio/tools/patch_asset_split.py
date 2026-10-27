# -*- coding: utf-8 -*-
"""adv4 화면을 에셋 단위 함수로 분해하고 마스크 조합으로 재구성."""
import io

p = 'main/ui/ui_dx.c'
t = io.open(p, encoding='utf-8').read()

old = '''static void ui_draw_all_adv4(const ui_state_t *s)
{
    s_radio_draw_seq++;
    display_fill_screen(UI_BG);
    draw_header_tabs(UI_TAB_RADIO, s);
    draw_header_msg(s);

    char num[20];
    const char *unit = "";
    ui_fmt_freq(s, num, sizeof(num), &unit);

    ui_draw_band_meta(s);

    char vb[24];
    snprintf(vb, sizeof(vb), "VOL %u  BRT %u", s->volume, s_brightness);
    display_draw_text(312, 24, vb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);

    ui_draw_sleep_badge();

    draw_saved_badges(s);

    draw_freq_text(12, 40, num, DISPLAY_FONT_LARGE, UI_BG);
    uint16_t fw = display_text_width(num, DISPLAY_FONT_LARGE);
    display_draw_text((int16_t)(12 + fw + 5), 50, unit, DISPLAY_FONT_SMALL,
                      UI_MUTED, UI_BG, DISPLAY_DATUM_TL);

    ui_draw_adv_signal4b(s);
}'''

new = '''/* ---------------- 에셋 (개별 요소, 자기 영역만 갱신) ---------------- */

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
}'''

assert old in t
t = t.replace(old, new)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
