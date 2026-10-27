# -*- coding: utf-8 -*-
"""fix: v2 라인(17탭) 파싱 시 필드 배열 오버플로."""
import io

p = 'main/ui/ui_runtime.c'
t = io.open(p, encoding='utf-8').read()
old = '        char *f[16];'
new = '        char *f[20];   /* v2: 15필드 + scale + uid + events + text */'
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
