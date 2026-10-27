# -*- coding: utf-8 -*-
"""Battery icon 모듈을 팔레트에 등록 (스키마 등록 완료 상태)."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()

if 'id: "batt_icon"' not in t:
    anchor = '  { id: "sleep", cat: "UI", name: "Sleep'
    assert anchor in t
    add = '''  { id: "batt_icon", cat: "UI", name: "Battery icon", sub: "배터리 아이콘 (% 채움)",
    colors: ["ok", "warn", "accent", "panel", "line"],
    make: () => [ W({ t: "batt_icon", x: 292, y: 5, w: 28, h: 11, bind: "batt",
                      fill: "panel", stroke: "line", color: "ok" }) ] },
'''
    t = t.replace(anchor, add + anchor, 1)
    io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
    print('batt_icon module added')
else:
    print('already present')
