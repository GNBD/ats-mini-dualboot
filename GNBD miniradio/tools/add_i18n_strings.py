# -*- coding: utf-8 -*-
"""i18n: 새로 추가된 UI 문자열의 영어 번역 등록."""
import io

p = 'uiedt/js/i18n.js'
t = io.open(p, encoding='utf-8').read()

entries = [
    ("Sleep 남은 시간", "Sleep remaining"),
    ("SLEEP 초 표시", "Show remaining sleep seconds"),
    ("디코드 패널: GPIO/모드/줄 수를 옵션 문자열로 지정합니다.",
     "Decode panel: set GPIO / mode / lines with the option string."),
    ("GPIO/모드를 프로퍼티에서 설정", "Set GPIO / mode in properties"),
    ("맨 앞으로", "Bring to front"),
    ("맨 뒤로", "Send to back"),
    ("기기에 저장", "Save to device"),
    ("PC로 내려받기", "Download to PC"),
    ("레이아웃 미리보기", "Layout preview"),
    ("레이아웃 탭에서 배치한 결과입니다. 이 화면을 기준으로 스크립트/블록을 작성하세요.",
     "This is the layout result. Write scripts/blocks based on this screen."),
    ("Default (기본 UI)", "Default (basic UI)"),
    ("firmware 기본 화면", "firmware default screen"),
    ("하단 전체 밴드맵", "full-width band map"),
    ("주파수 + RSSI만", "frequency + RSSI only"),
    ("디코드 상태/레벨/텍스트", "decode status / level / text"),
    ("디코드 상태/레벨", "decode status / level"),
    ("SLEEP 초", "SLEEP seconds"),
]

# dict 시작 지점 찾기
anchor = '  dict: {\n'
assert anchor in t
add = ''
for ko, en in entries:
    if '"' + ko + '":' in t:
        continue
    add += '    "%s": "%s",\n' % (ko, en)
assert add, 'nothing to add'
t = t.replace(anchor, anchor + add, 1)

io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('added', add.count('\n'), 'entries')
