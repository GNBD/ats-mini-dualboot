# -*- coding: utf-8 -*-
import io

p = 'uiedt/extended/js/extended.js'
t = io.open(p, encoding='utf-8').read()
old = '''      { id: "tpl-decode", name: "Decode UI (예제)", sub: "디코드 패널 + 라디오 상단",
        ids: ["headerfull", "bandfull", "freq", "rsq", "decode"] },'''
new = '''      { id: "tpl-decode", name: "Decode UI (예제)", sub: "주파수 + 디코드 패널 (파라미터 없음)",
        ids: ["headerfull", "bandfull", "freq", "decode"] },'''
assert old in t, 'template not found'
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
