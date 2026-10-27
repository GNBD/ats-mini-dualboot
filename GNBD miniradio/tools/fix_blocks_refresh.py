# -*- coding: utf-8 -*-
"""블록 탭 진입 시 위젯 목록 갱신."""
import io

# 1) blocks.js: refresh 노출
p = 'uiedt/extended/js/blocks.js'
t = io.open(p, encoding='utf-8').read()
old = '  window.Blocks = { init, generateLua, eventsFor, model: () => s_model };'
new = ('  window.Blocks = { init, generateLua, eventsFor, model: () => s_model,\n'
       '    refresh: () => { renderWidgets(); renderStack(); } };')
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) extended.js: 앱 탭 진입 시 refresh
p = 'uiedt/extended/js/extended.js'
t = io.open(p, encoding='utf-8').read()
old2 = '''    if (layout && window.ED) {
      ED.draw();
    } else {
      drawPreview();
    }'''
new2 = '''    if (layout && window.ED) {
      ED.draw();
    } else {
      drawPreview();
      if (window.Blocks) Blocks.refresh();
    }'''
assert old2 in t
t = t.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
