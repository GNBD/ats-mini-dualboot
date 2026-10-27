# -*- coding: utf-8 -*-
"""디코드 위젯 프로퍼티(gpio/mode/lines) + 런타임 반영."""
import io

# 1) decode.c: 설정 setter 추가
p = 'main/decode.h'
t = io.open(p, encoding='utf-8').read()
anchor = 'void decode_cycle_gpio(int dir);'
assert anchor in t
if 'decode_set_gpio' not in t:
    t = t.replace(anchor, anchor + '\nvoid decode_set_gpio(int pin);\nvoid decode_set_mode(int idx);')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) properties.js: decode 분기
p = 'uiedt/js/properties.js'
t = io.open(p, encoding='utf-8').read()
old = '''    } else if (w.t === "menu") {'''
new = '''    } else if (w.t === "decode") {
      const info = document.createElement("div");
      info.className = "empty";
      info.textContent = T("디코드 패널: GPIO/모드/줄 수를 옵션 문자열로 지정합니다.");
      body.appendChild(info);
      body.appendChild(this.row("Options", this.input(w.text || "gpio=4;mode=0;lines=4",
        (v) => { w.text = v; upd(); })));
      body.appendChild(this.row("Color", this.select(COLORS, w.color || "accent",
        (v) => { w.color = v; upd(); })));
    } else if (w.t === "menu") {'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
