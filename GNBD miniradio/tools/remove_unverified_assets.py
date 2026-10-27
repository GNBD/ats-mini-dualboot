# -*- coding: utf-8 -*-
"""검증되지 않은 새 에셋(batt_icon/decode)을 팔레트/기본 템플릿에서 제거."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()

# 1) 헤더 배터리: batt_icon -> bar(기존 안정 타입)
t = t.replace('W({ t: "batt_icon", x: 292, y: 5, w: 28, h: 11, bind: "batt", fill: "panel", stroke: "line", color: "ok" }),',
              'W({ t: "bar", x: 292, y: 7, w: 26, h: 8, bind: "batt", max: 100, color: "ok" }),')

# 2) 팔레트에서 decode / batt_icon 모듈 제거 (타입은 유지, 노출만 중단)
import re
t = re.sub(r'\n  \{ id: "decode",.*?\n    make: \(\) => \[ W\(\{ t: "decode".*?\] \},', '', t, flags=re.S)
t = re.sub(r'\n  \{ id: "battbar".*?\n    make: \(\) => \[ W\(\{ t: "bar".*?\] \},', '', t, flags=re.S)
t = re.sub(r'\n  \{ id: "battv".*?\n    make: \(\) => \[ W\(\{ t: "text".*?\] \},', '', t, flags=re.S)

io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
