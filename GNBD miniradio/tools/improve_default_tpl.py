# -*- coding: utf-8 -*-
"""디폴트 템플릿을 펌웨어 화면과 일치 + 누락 모듈 추가."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()

# 1) headerfull: 헤더 배경을 BG 로, 시계 tiny, 우측 정렬
old_header = '''  { id: "headerfull", cat: "UI", name: "Header + Wi-Fi", sub: "clock / wifi / battery",
    colors: ["panel", "accent", "fg", "muted", "accenth", "ok"],
    make: () => [
      W({ t: "rect", x: 0, y: 0, w: 320, h: 22, fill: "panel" }),
      W({ t: "rect", x: 6, y: 6, w: 3, h: 10, fill: "accent" }),
      W({ t: "text", x: 14, y: 6, w: 70, h: 12, bind: "clock", size: "small", color: "fg" }),
      W({ t: "text", x: 186, y: 6, w: 40, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "l" }),
      W({ t: "text", x: 226, y: 6, w: 56, h: 10, bind: "battv", size: "tiny", color: "muted", align: "l" }),
      W({ t: "wifi", x: 166, y: 5, w: 18, h: 11, color: "accenth" }),
      W({ t: "bar", x: 288, y: 7, w: 24, h: 8, bind: "batt", max: 100, color: "ok" }),
      W({ t: "line", x: 0, y: 22, w: 320, h: 1, color: "panel" }),
    ] },'''
new_header = '''  { id: "headerfull", cat: "UI", name: "Header (기본 상단바)", sub: "clock · wifi · 전압 우측 정렬",
    colors: ["bg", "accent", "fg", "muted", "accenth", "ok", "line"],
    make: () => [
      W({ t: "rect", x: 0, y: 0, w: 320, h: 22, fill: "bg" }),
      W({ t: "rect", x: 6, y: 7, w: 2, h: 8, fill: "accent" }),
      W({ t: "text", x: 12, y: 6, w: 44, h: 10, bind: "clock", size: "tiny", color: "fg", align: "l" }),
      W({ t: "text", x: 226, y: 6, w: 56, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" }),
      W({ t: "text", x: 186, y: 6, w: 36, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "r" }),
      W({ t: "wifi", x: 288, y: 5, w: 22, h: 11, color: "accenth" }),
      W({ t: "line", x: 0, y: 22, w: 320, h: 1, color: "line" }),
    ] },'''
assert old_header in t
t = t.replace(old_header, new_header, 1)

# 2) 누락 모듈 추가 (시계/전압/배터리바/슬립)
old_sig = '''  { id: "wifisig", cat: "BASIC", name: "Signal icon"'''
new_sig = '''  { id: "clock", cat: "UI", name: "Clock", sub: "시계 단독 (tiny)",
    colors: ["fg"],
    make: () => [ W({ t: "text", x: 12, y: 6, w: 44, h: 10, bind: "clock", size: "tiny", color: "fg", align: "l" }) ] },
  { id: "battv", cat: "UI", name: "Battery voltage", sub: "우측 정렬 전압",
    colors: ["muted"],
    make: () => [ W({ t: "text", x: 226, y: 6, w: 56, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" }) ] },
  { id: "battbar", cat: "UI", name: "Battery bar", sub: "배터리 % 게이지",
    colors: ["ok", "track"],
    make: () => [ W({ t: "bar", x: 288, y: 7, w: 22, h: 8, bind: "batt", max: 100, color: "ok" }) ] },
  { id: "sleep", cat: "UI", name: "Sleep 남은 시간", sub: "SLEEP 초",
    colors: ["accent"],
    make: () => [ W({ t: "text", x: 240, y: 24, w: 72, h: 10, bind: "sleep", size: "tiny", color: "accent", align: "r" }) ] },
  { id: "wifisig", cat: "BASIC", name: "Signal icon"'''
assert old_sig in t
t = t.replace(old_sig, new_sig, 1)

# 3) 디폴트 템플릿을 펌웨어 화면과 동일하게
old_tpl = '''  { id: "tpl-default", name: "Default (기본 UI)", sub: "firmware 기본 화면",
    ids: ["headerfull", "bandfull", "freq", "rsq", "params", "rds", "sig", "status"] },'''
new_tpl = '''  { id: "tpl-default", name: "Default (펌웨어 화면)", sub: "실제 라디오 기본 화면과 동일",
    ids: ["headerfull", "bandfull", "freq", "rsq", "params", "rds", "sig", "status"] },'''
assert old_tpl in t
t = t.replace(old_tpl, new_tpl, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
