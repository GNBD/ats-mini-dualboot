# -*- coding: utf-8 -*-
"""이동/리사이즈 1:1 정확도: 기본 스냅 1px, 핸들 시각 확대."""
import io

# 1) 기본 스냅 4px -> 1px
p = 'uiedt/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('<option value="4" selected>4px</option>',
              '<option value="4">4px</option>')
t = t.replace('<option value="1">1px</option>',
              '<option value="1" selected>1px</option>')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 2) 핸들 시각 확대 (6 -> 8)
p = 'uiedt/js/editor.js'
t = io.open(p, encoding='utf-8').read()
old = '''      const hw = 6;'''
new = '''      const hw = 9;'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 3) extended 페이지도 동일
p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('<option value="4" selected>4px</option>',
              '<option value="4">4px</option>')
t = t.replace('<option value="1">1px</option>',
              '<option value="1" selected>1px</option>')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
