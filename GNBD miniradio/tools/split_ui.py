# -*- coding: utf-8 -*-
"""ui.c를 기능별 모듈로 분할한다(행동 불변).

- tools/ui_funcs.json + tools/map_ui.json 기준으로 함수 블록 이동
- DEAD 목록(사용되지 않는 레거시 DX1/DX2/panel_dx/EiBi카드 마퀴/scrollbar)은 삭제
- 교차 static 은 un-static + ui_int.h 프로토타입
- 이름 충돌 회피: ui.c 내 static mode_str/bw_label/fmt_freq -> ui_mode_str/ui_bw_label/ui_fmt_freq
  (app.h 전역 mode_str/bw_label, ui_custom.c static fmt_freq와 별개)
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'main', 'ui.c')

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
    'ui_core.c': "UI core: theme, init, header and shared draw helpers.",
    'ui_freq.c': "Radio frequency screen (hero card, S-meter, RDS, status).",
    'ui_home.c': "Home / setup welcome / clock / weather screens.",
    'ui_spectrum.c': "Spectrum, decode refresh and hint overlay.",
    'ui_quick.c': "Quick action bar and quick config sheet.",
    'ui_dx.c': "DX / panel layouts and radio composite screens.",
    'ui_list.c': "Side list, settings page, options sheet and keypad.",
    'ui_wifi.c': "Wi-Fi app screens and scan list.",
    'ui_misc.c': "Time set, EiBi views, splash, confirm and about.",
}

DEAD = {
    'dx_kv', 'ui_draw_all_dx', 'ui_draw_all_dx2', 'ui_draw_panel_dx',
    'ui_draw_panel_dx2', 'ui_draw_dx_signal', 'ui_draw_dx_signal2',
    'eibi_entry_text', 'eibi_card_draw', 'eibi_card_marquee',
    'eibi_card_pause_marquee', 'eibi_card_carousel', 'scrollbar',
    'dx_sig_geom',  # ui_draw_dx_signal/2 삭제로 호출자 소멸
}

# 교차 static -> un-static (ui_int.h 프로토타입)
UNSTATIC = {
    'mode_str', 'bw_label', 'fit_text', 'panel', 'pill', 'bar', 'wifi_icon',
    'draw_status_right', 'draw_header_tabs', 'draw_header_msg', 'fmt_freq',
    'ui_fmt_step', 'ui_mode_slot', 'chip_row', 'ui_draw_all_dx3',
}

# 이름 충돌 회피 재명명 (정의 + 호출부 전역 치환)
RENAME = {
    'mode_str': 'ui_mode_str',
    'bw_label': 'ui_bw_label',
    'fmt_freq': 'ui_fmt_freq',
}

# 파일 수준 공유 변수/매크로 -> ui_int.h 선언 대상
EXT_VARS = [
    's_hist_rssi', 's_hist_snr', 's_hist_n', 's_hist_pos',
    's_eibi_freq', 's_eibi_utc_min', 's_eibi_name',
    'ui_pal',
]

INCLUDES_FROM = 13   # 원본 include 블록 (1-based, 13~22)
INCLUDES_TO = 22
EXTERN_BLOCK_FROM = 24  # extern 선언 블록 24~53, 65~69
EXTERN_BLOCK_TO = 53
EXTERN_BLOCK2_FROM = 65
EXTERN_BLOCK2_TO = 69

DELETE_LINES = [
    (1, 12),      # 원본 헤더 주석 (6행 'iOS / macOS 스타일' 포함) -> 새 헤더로 교체
    (24, 53),     # extern 선언 블록 -> ui_int.h
    (54, 55),     # forward decl -> ui_int.h
    (65, 69),     # extern 선언 블록2 -> ui_int.h
    (70, 73),     # UI_QUICK_MAX/UI_QA_COUNT/UI_LAYOUT_DX -> ui_int.h
    (85, 110),    # HDR_/META_/SIG_/RDS_ 매크로 -> ui_int.h
    (1455, 1455), # ui_fmt_step forward decl
    (1706, 1708), # CP_W ifndef 블록 -> ui_int.h 단일 정의
    (2664, 2664), # EIBI_SLIDE_MS (dead eibi_card_carousel 전용)
    (3169, 3169), # CP_W 중복 정의 -> ui_int.h 단일 정의
]
# 라인 단위 강제 이동(변형 전 라인 번호): ADV_HIST + s_hist_*/s_eibi_* -> ui_dx.c
MOVE = {i: 'ui_dx.c' for i in range(57, 65)}


def main():
    lines = open(SRC, encoding='utf-8', errors='surrogateescape').readlines()
    data = json.load(open(os.path.join(ROOT, 'tools', 'ui_funcs.json'),
                          encoding='utf-8'))
    funcs = data['funcs']
    mapping = json.load(open(os.path.join(ROOT, 'tools', 'map_ui.json'),
                             encoding='utf-8'))
    n = len(lines)

    deleted = [False] * (n + 1)
    for a, b in DELETE_LINES:
        for i in range(a, b + 1):
            deleted[i] = True

    owner = [None] * (n + 1)
    ranges = []
    for f in funcs:
        name = f['name']
        a, b = f['start'], min(f['end'], n)
        if name in DEAD:
            for i in range(a, b + 1):
                deleted[i] = True
            continue
        tgt = mapping.get(name)
        if not tgt:
            sys.exit('UNMAPPED: ' + name)
        ranges.append((a, b, tgt))
        for i in range(a, b + 1):
            owner[i] = tgt

    # gap(함수 사이) 라인: 다음 함수의 파일로
    ranges.sort()
    prev_end = INCLUDES_TO
    for a, b, tgt in ranges:
        for i in range(prev_end + 1, a):
            if not deleted[i] and owner[i] is None:
                owner[i] = tgt
        prev_end = max(prev_end, b)
    for i in range(prev_end + 1, n + 1):
        if not deleted[i] and owner[i] is None:
            owner[i] = 'ui_core.c' if i <= EXTERN_BLOCK_TO else 'ui_misc.c'

    # 강제 이동 (ADV_HIST/s_hist_*/s_eibi_* -> ui_dx.c)
    for ln, tgt in MOVE.items():
        if not deleted[ln]:
            owner[ln] = tgt

    un = [i for i in range(INCLUDES_TO + 1, n + 1)
          if not deleted[i] and owner[i] is None]
    if un:
        sys.exit('unassigned: %s' % un[:20])

    # 재명명 + un-static 처리 (파일 전체 텍스트 대상, 문자열/주호출 주의 없이
    # 식별자 경계 치환 - 원본에 해당 이름이 문자열 리터럴 안에 없음을 확인함)
    out = list(lines)
    strip_static = set()
    for f in funcs:
        if f['name'] in DEAD or f['name'] not in UNSTATIC:
            continue
        strip_static.add(f['start'])

    for i in range(1, n + 1):
        if deleted[i]:
            continue
        t = out[i - 1]
        if i in strip_static:
            t = re.sub(r'^(\s*)static\s+', r'\1', t, count=1)
        for old, new in RENAME.items():
            t = re.sub(r'\b' + old + r'\b', new, t)
        out[i - 1] = t

    # 파일별 본문 수집
    order = ['ui_core.c', 'ui_freq.c', 'ui_home.c', 'ui_spectrum.c',
             'ui_quick.c', 'ui_dx.c', 'ui_list.c', 'ui_wifi.c', 'ui_misc.c']
    body = {f: [] for f in order}
    for i in range(INCLUDES_TO + 1, n + 1):
        if deleted[i]:
            continue
        body[owner[i]].append(out[i - 1])

    inc = ''.join(lines[INCLUDES_FROM - 1:INCLUDES_TO])
    inc += '#include "ui_int.h"\n'

    texts = {}
    for f in order:
        texts[f] = (LICENSE_HEAD % DESC[f]) + inc + ''.join(body[f])

    for f, t in texts.items():
        p = os.path.join(ROOT, 'main', f)
        with open(p, 'w', encoding='utf-8', errors='surrogateescape',
                  newline='') as fh:
            fh.write(t)
        print('wrote %-14s %5d lines' % (f, t.count('\n')))

    # extern 블록 + 심볼 추출 (ui_int.h 작성용)
    ext_block = ''.join(lines[EXTERN_BLOCK_FROM - 1:EXTERN_BLOCK_TO])
    ext_block2 = ''.join(lines[EXTERN_BLOCK2_FROM - 1:EXTERN_BLOCK2_TO])
    with open(os.path.join(ROOT, 'tools', 'ui_int_raw.txt'), 'w',
              encoding='utf-8', newline='') as fh:
        fh.write(ext_block)
        fh.write(ext_block2)


if __name__ == '__main__':
    main()
