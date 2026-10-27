# -*- coding: utf-8 -*-
"""에디터: 디코드 위젯/바인딩 연결."""
import io

# 1) runtime.js: TYPE + BIND
p = 'uiedt/js/core/runtime.js'
t = io.open(p, encoding='utf-8').read()
old = 'const TYPE = { rect: 0, text: 1, bar: 2, line: 3, freq: 4, rsq: 5, rds: 6, sig: 7, bandmap: 8, menu: 10, wifi: 11 };'
new = 'const TYPE = { rect: 0, text: 1, bar: 2, line: 3, freq: 4, rsq: 5, rds: 6, sig: 7, bandmap: 8, menu: 10, wifi: 11, decode: 12 };'
if old in t:
    t = t.replace(old, new, 1)
else:
    # 멀티라인 정의 대응
    t = t.replace('menu: 10, wifi: 11', 'menu: 10, wifi: 11, decode: 12', 1)
t = t.replace('"eibi", "toast", "wifi", "status", "battv",',
              '"eibi", "toast", "wifi", "status", "battv", "sleep", "dec_level",', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) render.js: decode case
p = 'uiedt/js/render.js'
t = io.open(p, encoding='utf-8').read()
anchor = '      case "wifi":'
assert anchor in t
impl = '''      case "decode": {
        const c = this.ctx;
        c.setTransform(2, 0, 0, 2, 0, 0);
        c.fillStyle = col(w.fill, "panel");
        c.fillRect(w.x, w.y, w.w, w.h);
        c.strokeStyle = col(w.stroke, "line");
        c.strokeRect(w.x + .5, w.y + .5, w.w - 1, w.h - 1);
        c.fillStyle = col(w.color, "accent");
        c.font = font(w.font, w.size);
        c.textBaseline = "top";
        c.textAlign = "left";
        c.fillText("DECODE CW  GPIO 4", w.x + 4, w.y + 2);
        c.fillStyle = col(w.color, "ok");
        c.textAlign = "right";
        c.fillText("RUN", w.x + w.w - 4, w.y + 2);
        c.textAlign = "left";
        c.fillStyle = col(w.color, "muted");
        c.fillText("SIG", w.x + 4, w.y + 14);
        c.fillStyle = col(w.color, "track");
        c.fillRect(w.x + 26, w.y + 16, w.w - 30, 4);
        c.fillStyle = col(w.color, "ok");
        c.fillRect(w.x + 26, w.y + 16, (w.w - 30) * 0.6, 4);
        c.fillStyle = col(w.color, "fg");
        c.fillText("....", w.x + 4, w.y + 26);
        c.restore && c.restore();
        break;
      }
'''
t = t.replace(anchor, impl + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) catalog.js: 모듈 + 바인딩
p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
old3 = '''  { id: "wifisig", cat: "BASIC", name: "Signal icon"'''
new3 = '''  { id: "decode", cat: "APP", name: "DECODE panel", sub: "디코드 상태/레벨/텍스트",
    colors: ["panel", "line", "accent", "ok", "fg", "muted"],
    make: () => [ W({ t: "decode", x: 6, y: 72, w: 308, h: 96, fill: "panel", stroke: "line", color: "accent" }) ] },
  { id: "wifisig", cat: "BASIC", name: "Signal icon"'''
assert old3 in t
t = t.replace(old3, new3, 1)

t = t.replace('["status", "VOL/BRT 상태"], ["battv", "배터리 전압"],',
              '["status", "VOL/BRT 상태"], ["battv", "배터리 전압"],\n'
              '  ["sleep", "Sleep 남은 초"], ["dec_level", "디코드 레벨 0-100"],', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
