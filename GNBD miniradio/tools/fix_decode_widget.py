# -*- coding: utf-8 -*-
import io

p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()

if '#include "decode.h"' not in t:
    t = t.replace('#include "ui_runtime.h"',
                  '#include "ui_runtime.h"\n#include "decode.h"', 1)

t = t.replace('col(UC_GOLD)', 'col(UC_ACCENT)')

# bar() 대신 직접 그리기 (ui_custom.c 에 bar 선언 없음)
old = '''    int bw = w->w - 26;
    if (bw < 8) { bw = 8; }
    bar(w->x + 26, w->y + 16, bw, 4, decode_level(), 100, col(UC_OK));'''
new = '''    int bw = w->w - 26;
    if (bw < 8) { bw = 8; }
    display_fill_rect((int16_t)(w->x + 26), (int16_t)(w->y + 16),
                      (uint16_t)bw, 4, col(UC_TRACK));
    int lv = decode_level();
    if (lv < 0) { lv = 0; }
    if (lv > 100) { lv = 100; }
    display_fill_rect((int16_t)(w->x + 26), (int16_t)(w->y + 16),
                      (uint16_t)(bw * lv / 100), 4, col(UC_OK));'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
