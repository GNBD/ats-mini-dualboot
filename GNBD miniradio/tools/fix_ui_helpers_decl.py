# -*- coding: utf-8 -*-
import io

# panel/bar/pill 선언을 ui.h 로 (ui_int.h 의존 제거)
p = 'main/ui/ui.h'
t = io.open(p, encoding='utf-8').read()
if 'void panel(' not in t:
    anchor = 'void draw_header_msg(const ui_state_t *s);'
    assert anchor in t
    t = t.replace(anchor,
                  anchor + '\n\n/* 공용 드로잉 헬퍼 (ui_core.c) */\n'
                  'void panel(int x, int y, int w, int h, int r, uint16_t fill,\n'
                  '           uint16_t stroke);\n'
                  'void pill(int x, int y, const char *text, int maxw,\n'
                  '          uint16_t fill, uint16_t fg);\n'
                  'void bar(int x, int y, int w, int h, int value, int max_value,\n'
                  '         uint16_t color);')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# ui_int.h 중복 선언 제거 (ui.h 로 이동)
p = 'main/ui/ui_int.h'
t = io.open(p, encoding='utf-8').read()
import re
t = re.sub(r'void panel\([^;]*;\n', '', t)
t = re.sub(r'void pill\([^;]*;\n', '', t)
t = re.sub(r'void bar\([^;]*;\n', '', t)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
