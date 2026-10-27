# -*- coding: utf-8 -*-
"""catalog.js 복구(UTF-8) + 디코드 모듈 제거 + Sleep 추가 + 시계 tiny."""
import io
import re
import subprocess

rev = subprocess.run(['git', 'show', '69f6fd6:uiedt/js/catalog.js'],
                     capture_output=True).stdout
t = rev.decode('utf-8', errors='replace')
assert 'catalog.js' in t

# 1) 디코드 모듈 제거
t2 = re.sub(r'\n  \{ id: "decode",.*?\n    make: \(\) => \[ W\(\{ t: "decode".*?\] \},',
            '', t, flags=re.S)
assert t2 != t, 'decode module not found'
t = t2

# 2) 시계 tiny
old = 'W({ t: "text", x: 14, y: 6, w: 70, h: 12, bind: "clock", size: "small", color: "fg" })'
new = 'W({ t: "text", x: 12, y: 6, w: 44, h: 10, bind: "clock", size: "tiny", color: "fg" })'
assert old in t
t = t.replace(old, new, 1)

# 3) Sleep 모듈 추가
anchor = '  { id: "wifisig", cat: "BASIC", name: "Signal icon"'
add = '''  { id: "sleep", cat: "UI", name: "Sleep 남은 시간", sub: "SLEEP 초 표시",
    colors: ["accent"],
    make: () => [ W({ t: "text", x: 240, y: 24, w: 72, h: 10, bind: "sleep", size: "tiny", color: "accent", align: "r" }) ] },
'''
assert anchor in t
t = t.replace(anchor, add + anchor, 1)

io.open('uiedt/js/catalog.js', 'w', encoding='utf-8', newline='\n').write(t)
print('catalog fixed utf-8, len', len(t))
