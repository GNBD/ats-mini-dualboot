# -*- coding: utf-8 -*-
"""ui_refresh_top 누락 화면 추가 → 시계가 seek/scan 등에서도 갱신되도록."""
import io

p = 'main/pages.c'
t = io.open(p, encoding='utf-8').read()

old = '''    switch (s_hmi) {
    case HMI_TUNE:
        ui_draw_header(&s_ui);
        break;
'''
new = '''    switch (s_hmi) {
    case HMI_TUNE:
    case HMI_SEEK:
    case HMI_DECODE:
    case HMI_FAV:
    case HMI_ATS_LIST:
        ui_draw_header(&s_ui);
        break;
'''
assert old in t
t = t.replace(old, new)

old2 = '''    case HMI_WIFISCAN:
        scan_list_draw();
        break;
'''
new2 = '''    case HMI_WIFISCAN:
    case HMI_SCANNING:
        scan_list_draw();
        break;
'''
assert old2 in t
t = t.replace(old2, new2)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
