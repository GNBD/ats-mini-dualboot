# -*- coding: utf-8 -*-
"""남은 한국어 제거 (바인딩 라벨, 언어 버튼)."""
import io

p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
reps = [
    ('["sleep", "Sleep 남은 초"]', '["sleep", "Sleep remaining (s)"]'),
    ('["dec_level", "디코드 레벨 0-100"]', '["dec_level", "Decode level 0-100"]'),
    ('["eibi", "EiBi 방송"]', '["eibi", "EiBi station"]'),
    ('["toast", "토스트"]', '["toast", "Toast"]'),
    ('["wifi", "Wi-Fi 상태"]', '["wifi", "Wi-Fi state"]'),
    ('["status", "VOL/BRT 상태"]', '["status", "VOL/BRT status"]'),
    ('["battv", "배터리 전압"]', '["battv", "Battery voltage"]'),
    ('["freq", "주파수 (문자열)"]', '["freq", "Frequency (string)"]'),
    ('["mode", "모드 FM/AM/LSB/USB"]', '["mode", "Mode FM/AM/LSB/USB"]'),
    ('["band", "밴드명"]', '["band", "Band name"]'),
    ('["clock", "시계 HH:MM"]', '["clock", "Clock HH:MM"]'),
    ('["date", "날짜"]', '["date", "Date"]'),
    ('["preset", "프리셋 번호"]', '["preset", "Preset number"]'),
    ('["pname", "프리셋 이름"]', '["pname", "Preset name"]'),
    ('["ats", "ATS 번호"]', '["ats", "ATS number"]'),
    ('["batt", "배터리 %"]', '["batt", "Battery %"]'),
    ('["vol", "볼륨"]', '["vol", "Volume"]'),
    ('["bw", "대역폭"]', '["bw", "Bandwidth"]'),
    ('["step", "스텝"]', '["step", "Step"]'),
    ('["sm", "소프트뮤트"]', '["sm", "Softmute"]'),
    ('["sq", "스퀠치"]', '["sq", "Squelch"]'),
    ('["stereo", "스테레오"]', '["stereo", "Stereo"]'),
]
for a, b in reps:
    t = t.replace(a, b)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 언어 버튼은 항상 영문 표기
p = 'uiedt/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('<button id="langBtn" class="btn" title="언어 변경">EN</button>',
              '<button id="langBtn" class="btn" title="Language">KO</button>')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
