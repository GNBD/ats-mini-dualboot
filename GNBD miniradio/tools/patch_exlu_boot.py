# -*- coding: utf-8 -*-
import io

# 1) ui_exlu.h: 부팅 적용 함수 선언
p = 'main/ui/ui_exlu.h'
t = io.open(p, encoding='utf-8').read()
anchor = 'exlu_err_t exlu_apply(const exlu_t *e);'
assert anchor in t
if 'exlu_boot_apply_theme' not in t:
    t = t.replace(anchor, anchor + '\nvoid       exlu_boot_apply_theme(void);')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) main.c: include + 부팅 적용
p = 'main/main.c'
t = io.open(p, encoding='utf-8').read()
if '#include "ui_exlu.h"' not in t:
    old_inc = '#include "ui_runtime.h"'
    assert old_inc in t
    t = t.replace(old_inc, old_inc + '\n#include "ui_exlu.h"', 1)

old = '    ui_runtime_init(s_theme == UI_THEME_CUSTOM);'
new = ('    if (s_theme == UI_THEME_CUSTOM) {\n'
       '        exlu_boot_apply_theme();   /* littlefs 의 테마 EXLU 자동 적용 */\n'
       '    }\n'
       '    ui_runtime_init(s_theme == UI_THEME_CUSTOM);')
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
