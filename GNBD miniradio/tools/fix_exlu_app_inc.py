# -*- coding: utf-8 -*-
import io

p = 'main/ui/ui_exlu_app.c'
t = io.open(p, encoding='utf-8').read()

# ui_int.h 는 app.h 와 extern 선언이 충돌 -> 제거, board.h 추가
t = t.replace('#include "ui_int.h"\n', '')
if '#include "board.h"' not in t:
    t = t.replace('#include "display.h"', '#include "display.h"\n#include "board.h"', 1)

# 부팅 대기 최소화 (기존 부팅 흐름 보호)
t = t.replace('#define BOOT_IDLE_MS  6000', '#define BOOT_IDLE_MS  3000')

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
