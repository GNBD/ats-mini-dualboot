# -*- coding: utf-8 -*-
"""ui.c 내 심볼이 문자열 리터럴 안에 있는지 확인 (치환 안전성 검증)."""
import re

NAMES = ['mode_str', 'bw_label', 'fmt_freq', 'panel', 'pill', 'bar',
         'fit_text', 'wifi_icon', 'draw_status_right', 'draw_header_tabs',
         'draw_header_msg', 'ui_fmt_step', 'ui_mode_slot', 'chip_row',
         'ui_draw_all_dx3', 'battery_pill', 'weather_icon', 'lock_icon',
         'scan_freq_str', 'scan_graph', 'ui_keypad_char', 'band_meta',
         'draw_freq_text', 'draw_saved_badges', 'adv_hist_add', 'adv_kv',
         'marquee_line', 'eibi_current', 'dx_sig_geom', 'ui_draw_all_adv4',
         'ui_draw_panel_dx3']

txt = open('main/ui.c', encoding='utf-8', errors='replace').read()
found = False
for n in NAMES:
    hits = [l.strip()[:90] for l in txt.split('\n')
            if re.search(r'"[^"]*\b' + n + r'\b[^"]*"', l)]
    if hits:
        found = True
        print(n, 'STR:', hits[:3])
print('no string hits' if not found else 'FOUND')
