# -*- coding: utf-8 -*-
"""헤더 우측 좌표를 펌웨어 draw_status_right 계산값에 정확히 맞춤."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()

old = '''      W({ t: "wifi", x: 208, y: 5, w: 22, h: 11, color: "accenth" }),
      W({ t: "text", x: 232, y: 6, w: 20, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "l" }),
      W({ t: "text", x: 250, y: 6, w: 38, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" }),
      W({ t: "bar", x: 292, y: 7, w: 22, h: 8, bind: "batt", max: 100, color: "ok" }),'''
new = '''      W({ t: "wifi", x: 217, y: 5, w: 22, h: 11, color: "accenth" }),
      W({ t: "text", x: 234, y: 6, w: 20, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "l" }),
      W({ t: "text", x: 246, y: 6, w: 40, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" }),
      W({ t: "bar", x: 292, y: 7, w: 26, h: 8, bind: "batt", max: 100, color: "ok" }),'''
assert old in t
t = t.replace(old, new, 1)

# 미리보기 값도 실제 표기(소문자 sta)에 맞춤
t = t.replace('wifi: "STA", wifiBars: 4', 'wifi: "sta", wifiBars: 4')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
