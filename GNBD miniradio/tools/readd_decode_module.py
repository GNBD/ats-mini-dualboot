# -*- coding: utf-8 -*-
"""디코드 모듈 재등록 (프로퍼티로 gpio/mode/lines 설정)."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()

if '{ id: "decode"' not in t:
    anchor = '  { id: "sleep", cat: "UI", name: "Sleep'
    assert anchor in t
    add = '''  { id: "decode", cat: "APP", name: "DECODE panel", sub: "GPIO/모드를 프로퍼티에서 설정",
    colors: ["panel", "line", "accent", "ok", "fg", "muted"],
    make: () => [ W({ t: "decode", x: 6, y: 72, w: 308, h: 96, fill: "panel", stroke: "line",
                      color: "accent", text: "gpio=4;mode=0;lines=4" }) ] },
'''
    t = t.replace(anchor, add + anchor, 1)
    io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
    print('decode module re-added')
else:
    print('already present')
