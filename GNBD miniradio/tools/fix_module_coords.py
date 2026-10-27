# -*- coding: utf-8 -*-
"""모듈 좌표를 펌웨어 실제 렌더 좌표에 맞춤."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()

def rep(old, new):
    global t
    assert old in t, old[:60]
    t = t.replace(old, new, 1)

# 1) headerfull: 펌웨어 draw_header_tabs / draw_status_right 좌표
rep('''      W({ t: "rect", x: 0, y: 0, w: 320, h: 22, fill: "panel" }),
      W({ t: "rect", x: 6, y: 6, w: 3, h: 10, fill: "accent" }),
      W({ t: "text", x: 12, y: 6, w: 44, h: 10, bind: "clock", size: "tiny", color: "fg" }),
      W({ t: "text", x: 186, y: 6, w: 40, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "l" }),
      W({ t: "text", x: 226, y: 6, w: 56, h: 10, bind: "battv", size: "tiny", color: "muted", align: "l" }),
      W({ t: "wifi", x: 166, y: 5, w: 18, h: 11, color: "accenth" }),
      W({ t: "bar", x: 288, y: 7, w: 24, h: 8, bind: "batt", max: 100, color: "ok" }),
      W({ t: "line", x: 0, y: 22, w: 320, h: 1, color: "panel" }),''',
'''      W({ t: "rect", x: 0, y: 0, w: 320, h: 22, fill: "panel" }),
      W({ t: "rect", x: 5, y: 7, w: 2, h: 8, fill: "accent" }),
      W({ t: "text", x: 12, y: 6, w: 44, h: 10, bind: "clock", size: "tiny", color: "fg" }),
      W({ t: "text", x: 226, y: 6, w: 56, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" }),
      W({ t: "text", x: 186, y: 6, w: 36, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "r" }),
      W({ t: "wifi", x: 288, y: 5, w: 22, h: 11, color: "accenth" }),
      W({ t: "line", x: 0, y: 22, w: 320, h: 1, color: "line" }),''')

# 2) bandfull: 펌웨어 ui_draw_band_meta (12,24 SMALL / stereo 29 TINY)
rep('''      W({ t: "text", x: 12, y: 24, w: 30, h: 12, bind: "band", size: "small", color: "muted" }),
      W({ t: "text", x: 44, y: 24, w: 30, h: 12, bind: "mode", size: "small", color: "muted" }),
      W({ t: "text", x: 76, y: 24, w: 40, h: 10, bind: "stereo", size: "tiny", color: "ok" }),''',
'''      W({ t: "text", x: 12, y: 24, w: 30, h: 12, bind: "band", size: "small", color: "muted" }),
      W({ t: "text", x: 44, y: 24, w: 30, h: 12, bind: "mode", size: "small", color: "muted" }),
      W({ t: "text", x: 78, y: 29, w: 40, h: 10, bind: "stereo", size: "tiny", color: "ok" }),''')

# 3) status: 우측 정렬 (312)
rep('W({ t: "text", x: 228, y: 24, w: 84, h: 10, bind: "status", size: "tiny", color: "muted", align: "l" })',
    'W({ t: "text", x: 228, y: 24, w: 84, h: 10, bind: "status", size: "tiny", color: "muted", align: "r" })')

# 4) params: 펌웨어 adv_kv (x=180, y=74/90/106)
rep('''      W({ t: "text", x: 188, y: 74, w: 122, h: 10, bind: "line1", size: "tiny", color: "muted" }),
      W({ t: "text", x: 188, y: 90, w: 122, h: 10, bind: "line2", size: "tiny", color: "muted" }),
      W({ t: "text", x: 188, y: 106, w: 122, h: 10, bind: "line3", size: "tiny", color: "muted" }),''',
'''      W({ t: "text", x: 180, y: 74, w: 132, h: 10, bind: "line1", size: "tiny", color: "muted" }),
      W({ t: "text", x: 180, y: 90, w: 132, h: 10, bind: "line2", size: "tiny", color: "muted" }),
      W({ t: "text", x: 180, y: 106, w: 132, h: 10, bind: "line3", size: "tiny", color: "muted" }),''')

# 5) rds / sig / bandmap: 펌웨어 렌더 영역(6,122,308,46)
rep('W({ t: "rds", x: 8, y: 122, w: 200, h: 46 })', 'W({ t: "rds", x: 6, y: 122, w: 308, h: 46 })')
rep('W({ t: "sig", x: 212, y: 122, w: 98, h: 46 })', 'W({ t: "sig", x: 6, y: 122, w: 308, h: 46 })')
rep('W({ t: "bandmap", x: 8, y: 122, w: 304, h: 46 })', 'W({ t: "bandmap", x: 6, y: 122, w: 308, h: 46 })')

# 6) rsq: 펌웨어 RSSI/SNR 영역
rep('W({ t: "rsq", x: 8, y: 72, w: 176, h: 44 })', 'W({ t: "rsq", x: 8, y: 74, w: 168, h: 40 })')

# 7) menu 가이드: 펌웨어 기본 우측 패널 (212,22,98,148)
rep('W({ t: "menu", kind: "menu", orient: "v", x: 212, y: 18, w: 98, h: 150 })',
    'W({ t: "menu", kind: "menu", orient: "v", x: 212, y: 22, w: 98, h: 148 })')

# 8) sleep: 펌웨어 우측(y=42, 312 기준)
rep('W({ t: "text", x: 240, y: 24, w: 72, h: 10, bind: "sleep", size: "tiny", color: "accent", align: "r" })',
    'W({ t: "text", x: 240, y: 42, w: 72, h: 10, bind: "sleep", size: "tiny", color: "accent", align: "r" })')

# 9) badges: 펌웨어 우측 정렬 위치
rep('''      W({ t: "text", x: 120, y: 24, w: 34, h: 12, bind: "preset", size: "tiny", color: "accent", align: "c", chip: 1 }),
      W({ t: "text", x: 158, y: 24, w: 34, h: 12, bind: "ats", size: "tiny", color: "warn", align: "c", chip: 1 }),''',
'''      W({ t: "text", x: 190, y: 24, w: 34, h: 12, bind: "preset", size: "tiny", color: "accent", align: "c", chip: 1 }),
      W({ t: "text", x: 228, y: 24, w: 34, h: 12, bind: "ats", size: "tiny", color: "warn", align: "c", chip: 1 }),''')

io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
