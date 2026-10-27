# -*- coding: utf-8 -*-
"""에디터 텍스트 영어 전용화: 기본 언어 en + 카탈로그/문구 영어."""
import io

# 1) 기본 언어 영어
p = 'uiedt/js/i18n.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('  lang: "ko",', '  lang: "en",') if '  lang: "ko",' in t else t
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 2) catalog: 한국어 이름/설명 -> 영어
p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
reps = [
    ('name: "Sleep 남은 시간", sub: "SLEEP 초 표시"',
     'name: "Sleep remaining", sub: "SLEEP seconds"'),
    ('name: "Quad bar (battery)", sub: "사각 게이지 · 공용 바"',
     'name: "Quad bar (battery)", sub: "square gauge / generic bar"'),
    ('name: "DECODE panel", sub: "GPIO/모드를 프로퍼티에서 설정"',
     'name: "DECODE panel", sub: "set GPIO/mode in properties"'),
    ('name: "Default (기본 UI)", sub: "firmware 기본 화면"',
     'name: "Default (basic UI)", sub: "firmware default screen"'),
    ('name: "Radio + Band map", sub: "하단 전체 밴드맵"',
     'name: "Radio + Band map", sub: "full-width band map"'),
    ('name: "Minimal", sub: "주파수 + RSSI만"',
     'name: "Minimal", sub: "frequency + RSSI only"'),
    ('name: "Signal icon", sub: "4-bar Wi-Fi level · free placement"',
     'name: "Signal icon", sub: "4-bar level / free placement"'),
    ('name: "Param grid", sub: "BW/ST/BFO/AGC/AFC..."',
     'name: "Param grid", sub: "BW/ST/BFO/AGC/AFC..."'),
    ('name: "DECODE panel", sub: "디코드 상태/레벨/텍스트"',
     'name: "DECODE panel", sub: "decode status / level / text"'),
]
for a, b in reps:
    t = t.replace(a, b)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
