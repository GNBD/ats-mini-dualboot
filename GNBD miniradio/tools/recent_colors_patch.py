# -*- coding: utf-8 -*-
"""UC_CELL 수정 + 최근 사용 색상 연결."""
import io
import re

# 1) UC_CELL -> UC_PANEL
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()
if 'col(UC_CELL)' in t:
    t = t.replace('col(UC_CELL)', 'col(UC_PANEL)')
    io.open(p, 'w', encoding='utf-8', newline='').write(t)

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('fill: "cell"', 'fill: "panel"')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 2) properties.js: 색상 선택에 최근 사용 반영
p = 'uiedt/js/properties.js'
t = io.open(p, encoding='utf-8').read()

if 'colorOptions()' not in t:
    old = '''const Props = {'''
    new = '''function colorOptions() {
  return (window.RecentColors ? RecentColors.options(COLORS) : COLORS);
}

const Props = {'''
    assert old in t
    t = t.replace(old, new, 1)

# 색상 select 호출을 최근 사용 포함 + 사용 기록
t = re.sub(r'this\.select\(COLORS, (w\.\w+ \|\| "[a-z]+"), \(v\) => \{ (w\.\w+ = v; upd\(\);)( \})\)',
           r'this.select(colorOptions(), \1, (v) => { RecentColors.add(v); \2\3)', t)

# 남은 COLORS select 도 옵션만 확장
t = t.replace('this.select(COLORS,', 'this.select(colorOptions(),')

io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 3) 스크립트 로드 (index + extended)
for p, anchor in [('uiedt/index.html', '<script src="js/colors.js?v=44"></script>'),
                  ('uiedt/extended/index.html', '<script src="../js/colors.js?v=5"></script>')]:
    t = io.open(p, encoding='utf-8').read()
    if 'recent_colors.js' not in t:
        assert anchor in t, p
        add = anchor.replace('colors.js', 'recent_colors.js')
        t = t.replace(anchor, add + '\n' + anchor, 1)
        io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
