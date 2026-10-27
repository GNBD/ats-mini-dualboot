# -*- coding: utf-8 -*-
"""main.c를 기능별 파일로 분할한다(행동 불변).

- 함수 블록: tools/main_funcs.json + tools/map_main.json 기준으로 이동
- 그 외 라인(전역변수/매크로/프로토타입/주석): 다음 함수와 같은 파일로 자동 배정
- 오버라이드: 교차 static 심볼 처리(제거/이동/static 해제)는 여기서 지정
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, 'main', 'main.c')

# 원본 라이선스/설명 헤더(파일별 설명만 교체, 라이선스 문구 유지)
LICENSE_HEAD = """/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * %s
 *
 * This source file is licensed under the MIT License.
 */
"""

DESC = {
    'main.c': "miniradio main firmware.\n * HMI state machine, input handling, RDS/battery/power management.",
    'menu.c': "Main menu / settings menu build & option apply.",
    'input.c': "Encoder / button input handling.",
    'pages.c': "Home screen pages and app launching.",
    'time.c': "Clock, calendar and auto brightness.",
    'wifi_ui.c': "Wi-Fi app screen and scan list UI.",
    'lists.c': "Favorites and ATS preset lists.",
    'keypad.c': "On-screen keypad.",
    'save.c': "Settings snapshot / save / restore.",
    'webcmd.c': "Web command execution.",
    'setup.c': "Factory setup (service) mode.",
}

# 라인 단위 오버라이드 -------------------------------------------------
# 삭제: (start, end) inclusive
DELETE = [
    (67, 75),    # app.h와 동일한 중복 매크로
    (77, 79),    # app_open_* 프로토타입 -> hmi.h로 이동
    (94, 95),    # ST_BRIGHT_* -> hmi.h
    (96, 96),    # brightness_apply 프로토타입 -> hmi.h
    (121, 121),  # APP_COUNT 정의 -> hmi.h (+ main.c static assert 삽입)
    (126, 126),  # preset_index_for_freq 프로토타입은 app.h에 이미 존재
    (635, 636),  # static clock_* 프로토타입 -> hmi.h (non-static)
    (1905, 1906),  # KP_MODE enum -> hmi.h
]
# 특정 라인을 지정 파일로 강제 이동(수정 전 라인 번호)
MOVE = {
    83: 'menu.c',      # s_opt_quick (menu.c 전용)
    85: 'menu.c',      # s_opt_force_radio (menu.c 전용)
    90: 'wifi_ui.c',   # s_wifi_sel
    91: 'wifi_ui.c',   # s_wifi_focus
    92: 'wifi_ui.c',   # s_wifi_scan_ms
    113: 'menu.c',     # QUICK_ITEMS
    1907: 'main.c',    # s_kp_mode: 교차(input/keypad) -> main.c 전역
}
# main.c에 남기고 static 키워드만 제거(교차 전역변수)
STRIP_STATIC_MAIN = [(63, 65), (81, 82), (84, 84), (86, 88), (93, 93),
                     (115, 115), (1907, 1907)]
# 함수 시그니처에서 static 제거(교차 함수)
STRIP_STATIC_FUNC = {'brightness_apply', 'clock_utc_now', 'clock_save_now',
                     'app_open_radio', 'app_open_clock', 'app_open_weather',
                     'power_save_set', 'app_find', 'launch_app',
                     'quick_build', 'quick_exec', 'quick_pop', 'setup_mode'}

PREAMBLE_INCLUDES_FROM = 17   # 원본 include 블록 시작(1-based)
PREAMBLE_INCLUDES_TO = 61


def main():
    lines = open(MAIN, encoding='utf-8', errors='surrogateescape').readlines()
    funcs = json.load(open(os.path.join(ROOT, 'tools', 'main_funcs.json'),
                           encoding='utf-8'))['funcs']
    mapping = json.load(open(os.path.join(ROOT, 'tools', 'map_main.json'),
                             encoding='utf-8'))

    n = len(lines)
    owner = [None] * (n + 1)          # 1-based
    deleted = [False] * (n + 1)
    strip = [False] * (n + 1)         # static 키워드 제거 대상 라인

    # 1) 삭제
    for a, b in DELETE:
        for i in range(a, b + 1):
            deleted[i] = True

    # 2) 함수 블록 배정
    func_ranges = []
    for f in funcs:
        tgt = mapping.get(f['name'])
        if tgt in (None, 'SKIP'):
            sys.exit('UNMAPPED function: %s' % f['name'])
        a, b = f['start'], min(f['end'], n)
        func_ranges.append((a, b, tgt))
        for i in range(a, b + 1):
            if not deleted[i]:
                owner[i] = tgt
        if f['name'] in STRIP_STATIC_FUNC:
            strip[a] = True

    # 3) 강제 이동 / static 제거
    for ln, tgt in MOVE.items():
        owner[ln] = tgt
        deleted[ln] = False
    for a, b in STRIP_STATIC_MAIN:
        for i in range(a, b + 1):
            strip[i] = True
            if owner[i] is None:
                owner[i] = 'main.c'
    # MOVE로 간 라인(1907)은 static 제거 대상
    strip[1907] = True
    strip[83] = strip[85] = False      # 이동하는 정의는 static 유지
    strip[90] = strip[91] = strip[92] = False

    # 4) 남은 라인(gap): 다음 함수의 파일로 배정(마지막 gap은 이전 함수 파일)
    func_ranges.sort()
    prev_end = 0
    prev_tgt = 'main.c'
    for a, b, tgt in func_ranges:
        for i in range(prev_end + 1, a):
            if not deleted[i] and owner[i] is None:
                owner[i] = tgt
        prev_end = max(prev_end, b)
        prev_tgt = tgt
    for i in range(prev_end + 1, n + 1):
        if not deleted[i] and owner[i] is None:
            owner[i] = prev_tgt

    # 5) 검증: 모든 라인이 배정되었는가
    un = [i for i in range(1, n + 1) if not deleted[i] and owner[i] is None]
    if un:
        sys.exit('unassigned lines: %s' % un[:20])

    # 6) 라인 변형(static 제거, assert 삽입)
    out = list(lines)
    for i in range(1, n + 1):
        if strip[i] and not deleted[i]:
            out[i - 1] = re.sub(r'^(\s*)static\s+', r'\1', out[i - 1], count=1)

    # 7) 파일별 내용 수집
    order = ['main.c', 'menu.c', 'input.c', 'pages.c', 'time.c', 'wifi_ui.c',
             'lists.c', 'keypad.c', 'save.c', 'webcmd.c', 'setup.c']
    body = {f: [] for f in order}
    for i in range(PREAMBLE_INCLUDES_TO + 1, n + 1):
        if deleted[i]:
            continue
        body[owner[i]].append(out[i - 1])

    inc = ''.join(lines[PREAMBLE_INCLUDES_FROM - 1:PREAMBLE_INCLUDES_TO])
    inc_hmi = '#include "hmi.h"\n'

    # main.c: 원본 라이선스 헤더(1-16) 유지
    head_main = ''.join(lines[0:16])
    text_main = head_main + inc + inc_hmi + ''.join(body['main.c'])

    texts = {'main.c': text_main}
    for f in order[1:]:
        texts[f] = (LICENSE_HEAD % DESC[f]) + inc + inc_hmi + ''.join(body[f])

    for f, t in texts.items():
        p = os.path.join(ROOT, 'main', f)
        with open(p, 'w', encoding='utf-8', errors='surrogateescape',
                  newline='') as fh:
            fh.write(t)
        print('wrote %-12s %5d lines' % (f, t.count('\n')))


if __name__ == '__main__':
    main()
