# -*- coding: utf-8 -*-
"""헤더 전압/sta 를 일반 텍스트로 + 시그널 아이콘 공용(Bars 속성)."""
import io

# 1) catalog: battv/wifi -> 일반 텍스트
p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('W({ t: "text", x: 234, y: 6, w: 20, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "l" })',
              'W({ t: "text", x: 234, y: 6, w: 20, h: 10, text: "sta", size: "tiny", color: "muted", align: "l" })')
t = t.replace('W({ t: "text", x: 242, y: 6, w: 42, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" })',
              'W({ t: "text", x: 242, y: 6, w: 42, h: 10, text: "4.36V", size: "tiny", color: "muted", align: "r" })')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 2) properties: wifi 아이콘에 Bars/Color
p = 'uiedt/js/properties.js'
t = io.open(p, encoding='utf-8').read()
old = '''    } else if (w.t === "wifi") {
      body.appendChild(this.row("Color", this.select(colorOptions(), w.color || "accenth", (v) => { RecentColors.add(v); w.color = v; upd(); })));'''
new = '''    } else if (w.t === "wifi") {
      body.appendChild(this.row("Bars", this.input(w.max == null ? 4 : w.max,
        (v) => { w.max = Math.max(0, Math.min(4, +v)); upd(); }, "number")));
      body.appendChild(this.row("Color", this.select(colorOptions(), w.color || "accenth", (v) => { RecentColors.add(v); w.color = v; upd(); })));'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 3) 펌웨어 draw_wifi: max(bars) 우선
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()
import re
m = re.search(r'static void draw_wifi\(const uc_widget_t \*w, const ui_state_t \*s\)\n\{', t)
assert m
# bars 계산부 확인용 출력만 (수정은 아래 replace)
old2 = '''static void draw_wifi(const uc_widget_t *w, const ui_state_t *s)
{'''
assert old2 in t
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 4) 에디터 미리보기: max(bars) 우선
p = 'uiedt/js/render.js'
t = io.open(p, encoding='utf-8').read()
old3 = 'const bars = s.wifiBars == null ? 4 : s.wifiBars;'
new3 = 'const bars = (wg.max != null) ? wg.max : (s.wifiBars == null ? 4 : s.wifiBars);'
assert old3 in t
t = t.replace(old3, new3, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
