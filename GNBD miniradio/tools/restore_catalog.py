# -*- coding: utf-8 -*-
"""catalog를 69f6fd6(정상 배치)로 복원 + 시계 tiny + 모듈(Sleep/Quad/Decode)만 유지."""
import io
import re
import subprocess

rev = subprocess.run(['git', 'show', '69f6fd6:uiedt/js/catalog.js'],
                     capture_output=True).stdout
t = rev.decode('utf-8', errors='replace')
assert 'catalog.js' in t and 'headerfull' in t

# 1) 시계만 tiny 로 (사용자 요청 유지)
old = 'W({ t: "text", x: 14, y: 6, w: 70, h: 12, bind: "clock", size: "small", color: "fg" })'
new = 'W({ t: "text", x: 12, y: 6, w: 44, h: 10, bind: "clock", size: "tiny", color: "fg" })'
assert old in t
t = t.replace(old, new, 1)

# 2) 추가 모듈만 유지 (팔레트)
add = '''  { id: "sleep", cat: "UI", name: "Sleep 남은 시간", sub: "SLEEP 초 표시",
    colors: ["accent"],
    make: () => [ W({ t: "text", x: 240, y: 42, w: 72, h: 10, bind: "sleep", size: "tiny", color: "accent", align: "r" }) ] },
  { id: "batt_icon", cat: "UI", name: "Quad bar (battery)", sub: "사각 게이지 · 공용 바",
    colors: ["ok", "warn", "accent", "panel", "line"],
    make: () => [ W({ t: "batt_icon", x: 292, y: 5, w: 28, h: 11, bind: "batt", max: 100,
                      fill: "panel", stroke: "line", color: "ok" }) ] },
  { id: "decode", cat: "APP", name: "DECODE panel", sub: "GPIO/모드를 프로퍼티에서 설정",
    colors: ["panel", "line", "accent", "ok", "fg", "muted"],
    make: () => [ W({ t: "decode", x: 6, y: 72, w: 308, h: 96, fill: "panel", stroke: "line",
                      color: "accent", text: "gpio=4;mode=0;lines=4" }) ] },
'''
anchor = '  { id: "wifisig", cat: "BASIC", name: "Signal icon"'
assert anchor in t
t = t.replace(anchor, add + anchor, 1)

io.open('uiedt/js/catalog.js', 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
