# -*- coding: utf-8 -*-
"""헤더 우측 순서를 펌웨어 draw_status_right 와 동일하게."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()

old = '''      W({ t: "text", x: 226, y: 6, w: 56, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" }),
      W({ t: "text", x: 186, y: 6, w: 36, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "r" }),
      W({ t: "wifi", x: 288, y: 5, w: 22, h: 11, color: "accenth" }),'''
new = '''      W({ t: "wifi", x: 208, y: 5, w: 22, h: 11, color: "accenth" }),
      W({ t: "text", x: 232, y: 6, w: 20, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "l" }),
      W({ t: "text", x: 250, y: 6, w: 38, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" }),
      W({ t: "bar", x: 292, y: 7, w: 22, h: 8, bind: "batt", max: 100, color: "ok" }),'''
assert old in t
t = t.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
