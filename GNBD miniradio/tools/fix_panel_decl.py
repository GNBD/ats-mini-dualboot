# -*- coding: utf-8 -*-
import io

p = 'main/ui/ui_exlu_app.c'
t = io.open(p, encoding='utf-8').read()
if 'void panel(int x' not in t:
    anchor = '#include "board.h"'
    assert anchor in t
    t = t.replace(anchor, anchor + '''

/* 공용 드로잉 헬퍼 (ui_core.c) */
void panel(int x, int y, int w, int h, int r, uint16_t fill, uint16_t stroke);''', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
