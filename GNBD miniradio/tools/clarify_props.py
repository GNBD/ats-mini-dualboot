# -*- coding: utf-8 -*-
"""프로퍼티 라벨/안내 문구를 명확하게 정리."""
import io

p = 'uiedt/js/properties.js'
t = io.open(p, encoding='utf-8').read()

reps = [
    ('body.appendChild(this.row("Type", t));',
     'body.appendChild(this.row(T("타입 (읽기 전용)"), t));'),
    ('body.appendChild(this.row("Name", nameInp));',
     'body.appendChild(this.row(T("이름"), nameInp));'),
    ('body.appendChild(this.row("X / Y", g1));',
     'body.appendChild(this.row(T("X / Y 위치"), g1));'),
    ('body.appendChild(this.row("W / H", g2));',
     'body.appendChild(this.row(T("너비 / 높이"), g2));'),
    ('body.appendChild(this.row("Scale %", this.input(',
     'body.appendChild(this.row(T("크기 %"), this.input('),
    ('body.appendChild(this.row("Hide", hide));',
     'body.appendChild(this.row(T("숨김"), hide));'),
    ('body.appendChild(this.row("Layer", lz, up, dn));',
     'body.appendChild(this.row(T("레이어 (z)"), lz, up, dn));'),
    ('body.appendChild(this.row("Order", front, back));',
     'body.appendChild(this.row(T("순서"), front, back));'),
    ('body.appendChild(this.row("Inherit", this.select(opts, w.parent || "",',
     'body.appendChild(this.row(T("레이어 상속"), this.select(opts, w.parent || "",'),
    ('body.appendChild(this.row("Bind", this.select(bindOptions(), w.bind || "", (v) => { w.bind = v || undefined; upd(); })));',
     'body.appendChild(this.row(T("값 바인딩"), this.select(bindOptions(), w.bind || "", (v) => { w.bind = v || undefined; upd(); })));'),
    ('body.appendChild(this.row("Text", this.input(w.text || "", (v) => { w.text = v; upd(); })));',
     'body.appendChild(this.row(T("표시 텍스트"), this.input(w.text || "", (v) => { w.text = v; upd(); })));'),
    ('body.appendChild(this.row("Size", this.select(["tiny", "small", "large"]',
     'body.appendChild(this.row(T("폰트 크기"), this.select(["tiny", "small", "large"]'),
    ('body.appendChild(this.row("Align", this.select([["l", "Left"], ["c", "Center"], ["r", "Right"]]',
     'body.appendChild(this.row(T("정렬"), this.select([["l", "Left"], ["c", "Center"], ["r", "Right"]]'),
    ('body.appendChild(this.row("Color", this.select(colorOptions(), w.color || "fg"',
     'body.appendChild(this.row(T("색상"), this.select(colorOptions(), w.color || "fg"'),
    ('body.appendChild(this.row("Chip", this.select(',
     'body.appendChild(this.row(T("칩 배경"), this.select('),
    ('body.appendChild(this.row("Fill", this.select(colorOptions(), w.fill || "panel"',
     'body.appendChild(this.row(T("채우기 색"), this.select(colorOptions(), w.fill || "panel"'),
    ('body.appendChild(this.row("Stroke", this.select(colorOptions(), w.stroke || "none"',
     'body.appendChild(this.row(T("테두리 색"), this.select(colorOptions(), w.stroke || "none"'),
    ('body.appendChild(this.row("Radius", this.input(',
     'body.appendChild(this.row(T("모서리 반경"), this.input('),
    ('body.appendChild(this.row("Max", this.input(',
     'body.appendChild(this.row(T("최대값"), this.input('),
    ('body.appendChild(this.row("Bars", this.input(',
     'body.appendChild(this.row(T("막대 수 (0~4, 0=자동)"), this.input('),
    ('body.appendChild(this.row("Options", this.input(w.text || ""',
     'body.appendChild(this.row(T("옵션 (예: noterm)"), this.input(w.text || ""'),
    ('body.appendChild(this.row("List", this.select(',
     'body.appendChild(this.row(T("목록 종류"), this.select('),
    ('body.appendChild(this.row("Layout", this.select(',
     'body.appendChild(this.row(T("배치 방향"), this.select('),
    ('T("캔버스: 드래그 이동 · 우하단 핸들 크기조절")',
     'T("캔버스: 드래그 이동 · 가장자리/모서리 핸들로 크기 조절")'),
    ('info.textContent = T("이 모듈은 위치/크기만 조정합니다 (내용은 펌웨어가 그림).");',
     'info.textContent = T("이 모듈은 위치·크기·색상만 바꿀 수 있고, 표시 내용은 펌웨어가 그립니다.");'),
    ('info.textContent = T("디코드 패널: GPIO/모드/줄 수를 옵션 문자열로 지정합니다.");',
     'info.textContent = T("디코드 패널: 옵션 문자열로 설정합니다. 예) gpio=4;mode=0;lines=4 (mode 0=CW, 1=RTTY)");'),
]

for a, b in reps:
    if a in t:
        t = t.replace(a, b, 1)

# batt_icon 안내 추가
old = '''    } else if (w.t === "batt_icon") {
      body.appendChild(this.row(T("값 바인딩"), this.select(bindOptions(), w.bind || "batt",'''
if old in t and '사각 게이지' not in t:
    t = t.replace(old, '''    } else if (w.t === "batt_icon") {
      const binfo = document.createElement("div");
      binfo.className = "empty";
      binfo.textContent = T("사각 게이지: 바인딩 값(0~최대값)을 테두리+채움으로 표시합니다. 옵션에 noterm 입력 시 단자를 숨깁니다.");
      body.appendChild(binfo);
      body.appendChild(this.row(T("값 바인딩"), this.select(bindOptions(), w.bind || "batt",''', 1)

# wifi 안내
old2 = '''    } else if (w.t === "wifi") {
      body.appendChild(this.row(T("막대 수 (0~4, 0=자동)"), this.input('''
if old2 in t and 'Wi-Fi 세기' not in t:
    t = t.replace(old2, '''    } else if (w.t === "wifi") {
      const winfo = document.createElement("div");
      winfo.className = "empty";
      winfo.textContent = T("Wi-Fi 세기 아이콘: 막대 수를 고정하거나 0으로 두면 실제 상태(RSSI)에 따라 자동 표시합니다.");
      body.appendChild(winfo);
      body.appendChild(this.row(T("막대 수 (0~4, 0=자동)"), this.input(''', 1)

io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
