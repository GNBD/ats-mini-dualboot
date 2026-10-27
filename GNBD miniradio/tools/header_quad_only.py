# -*- coding: utf-8 -*-
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
old = 'W({ t: "bar", x: 288, y: 7, w: 24, h: 8, bind: "batt", max: 100, color: "ok" }),'
new = ('W({ t: "batt_icon", x: 292, y: 5, w: 26, h: 11, bind: "batt", max: 100, '
       'fill: "panel", stroke: "line", color: "ok" }),')
assert old in t, 'bar line not found'
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
