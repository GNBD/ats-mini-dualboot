# -*- coding: utf-8 -*-
"""남은 영문 라벨 정리 + 새 문구 영어 번역 등록."""
import io

p = 'uiedt/js/properties.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('this.row("Bind", this.select(bindOptions(), w.bind || "", (v) => { w.bind = v || undefined; upd(); }))',
              'this.row(T("값 바인딩"), this.select(bindOptions(), w.bind || "", (v) => { w.bind = v || undefined; upd(); }))')
t = t.replace('this.row("Color", this.select(colorOptions()',
              'this.row(T("색상"), this.select(colorOptions()')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

p = 'uiedt/js/i18n.js'
t = io.open(p, encoding='utf-8').read()
entries = [
    ("타입 (읽기 전용)", "Type (read only)"),
    ("이름", "Name"),
    ("X / Y 위치", "X / Y position"),
    ("너비 / 높이", "Width / Height"),
    ("크기 %", "Scale %"),
    ("숨김", "Hidden"),
    ("레이어 (z)", "Layer (z)"),
    (" 레이어 상속", "Layer inherit"),
    ("레이어 상속", "Layer inherit"),
    ("순서", "Order"),
    ("값 바인딩", "Binding"),
    ("표시 텍스트", "Text"),
    ("폰트 크기", "Font size"),
    ("정렬", "Align"),
    ("색상", "Color"),
    ("칩 배경", "Chip background"),
    ("채우기 색", "Fill color"),
    ("테두리 색", "Stroke color"),
    ("모서리 반경", "Corner radius"),
    ("최대값", "Max value"),
    ("막대 수 (0~4, 0=자동)", "Bars (0-4, 0=auto)"),
    ("옵션 (예: noterm)", "Options (e.g. noterm)"),
    ("목록 종류", "List kind"),
    ("배치 방향", "Orientation"),
    ("캔버스: 드래그 이동 · 가장자리/모서리 핸들로 크기 조절",
     "Canvas: drag to move, edge/corner handles to resize"),
    ("이 모듈은 위치·크기·색상만 바꿀 수 있고, 표시 내용은 펌웨어가 그립니다.",
     "This module only changes position/size/color; the firmware draws the content."),
    ("디코드 패널: 옵션 문자열로 설정합니다. 예) gpio=4;mode=0;lines=4 (mode 0=CW, 1=RTTY)",
     "Decode panel options, e.g. gpio=4;mode=0;lines=4 (mode 0=CW, 1=RTTY)"),
    ("사각 게이지: 바인딩 값(0~최대값)을 테두리+채움으로 표시합니다. 옵션에 noterm 입력 시 단자를 숨깁니다.",
     "Quad gauge: shows the bound value (0..max) as border + fill. Add noterm to hide the terminal."),
    ("Wi-Fi 세기 아이콘: 막대 수를 고정하거나 0으로 두면 실제 상태(RSSI)에 따라 자동 표시합니다.",
     "Wi-Fi icon: fix the bar count, or 0 to follow the actual RSSI."),
]
anchor = '  dict: {\n'
assert anchor in t
add = ''
for ko, en in entries:
    if '"' + ko + '":' in t:
        continue
    add += '    "%s": "%s",\n' % (ko, en)
assert add
t = t.replace(anchor, anchor + add, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
