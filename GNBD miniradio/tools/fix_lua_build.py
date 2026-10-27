# -*- coding: utf-8 -*-
import io

# 1) ui_runtime_find: 내장 레이아웃 검색 제거
p = 'main/ui/ui_runtime.c'
t = io.open(p, encoding='utf-8').read()
old = '''    /* 내장 레이아웃(ui_custom_layout.h)도 검색 */
    if (!s_enabled) {
        for (int i = 0; i < UC_WIDGET_COUNT; ++i) {
            if (uc_widgets[i].uid == uid) {
                return &uc_widgets[i];
            }
        }
    }
'''
if old in t:
    t = t.replace(old, '')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_lua.c: radio.h / ui_int.h 제거
p = 'main/ui/ui_lua.c'
t = io.open(p, encoding='utf-8').read()
t = t.replace('#include "ui_int.h"\n', '')
t = t.replace('#include "radio.h"\n', '')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) ui_custom.c: ui_widget_bind_value를 num_bind 정의 뒤로 이동
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()
blk = '''int ui_widget_bind_value(const uc_widget_t *w)
{
    if (!w) {
        return 0;
    }
    return num_bind(w->bind, &s_ui);
}
'''
if blk in t:
    t = t.replace(blk, '')

anchor = 'static const char *str_bind(uint8_t b, const ui_state_t *s, char *buf, size_t n)'
i = t.index(anchor)
depth = 0
started = False
j = i
while j < len(t):
    if t[j] == '{':
        depth += 1
        started = True
    elif t[j] == '}':
        depth -= 1
        if started and depth == 0:
            j += 1
            break
    j += 1
t = t[:j] + '\n\n' + blk + t[j:]
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
