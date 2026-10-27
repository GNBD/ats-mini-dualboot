# -*- coding: utf-8 -*-
"""sleep 배지: 우측 기준 정렬(DATUM_TR)로 수정, 문구를 'S h:mm:ss'로."""
import io

p = 'main/ui_dx.c'
t = io.open(p, encoding='utf-8').read()

old = '''    char b[20];
    snprintf(b, sizeof(b), "SLEEP %d:%02d", sec / 60, sec % 60);
    int tw = display_text_width(b, DISPLAY_FONT_TINY);
    display_fill_rect(clear_x, y - 2, clear_w, 14, UI_BG);
    int tx = right - tw;
    if (tx < clear_x) {
        tx = clear_x;
    }
    display_draw_text((int16_t)tx, y, b, DISPLAY_FONT_TINY, UI_ACC,
                      UI_BG, DISPLAY_DATUM_TR);'''

new = '''    char b[20];
    snprintf(b, sizeof(b), "S %d:%02d:%02d", sec / 3600, (sec / 60) % 60,
             sec % 60);
    display_fill_rect(clear_x, y - 2, clear_w, 14, UI_BG);
    /* DISPLAY_DATUM_TR: x가 오른쪽 끝 기준이므로 right를 그대로 넘긴다 */
    display_draw_text((int16_t)right, y, b, DISPLAY_FONT_TINY, UI_ACC,
                      UI_BG, DISPLAY_DATUM_TR);'''

assert old in t
t = t.replace(old, new)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
