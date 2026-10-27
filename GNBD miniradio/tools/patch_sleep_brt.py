# -*- coding: utf-8 -*-
"""sleep 카운트다운을 'VOL/BRT' 줄 바로 아래에 우측 정렬로 표시."""
import io

# 1) ui_dx.c: 부분 갱신 함수 + adv4에서 호출
p = 'main/ui_dx.c'
t = io.open(p, encoding='utf-8').read()

anchor = 'static void ui_draw_all_adv4(const ui_state_t *s)\n{'
assert anchor in t

func = '''/* Sleep 남은 시간: 'VOL/BRT' 줄 바로 아래(우측 정렬)에 표시.
   매초 부분 갱신할 수 있도록 별도 함수로 둔다. */
void ui_draw_sleep_badge(void)
{
    const int right = 312;
    const int y = 33;
    const int clear_x = 226;
    const int clear_w = 88;

    if (!(s_sleep_min > 0 && s_sleep_armed)) {
        return;
    }
    int32_t remain = (int32_t)(s_sleep_deadline - now_ms());
    if (remain < 0) {
        remain = 0;
    }
    int sec = (int)((remain + 999) / 1000);
    if (sec > 99 * 60 + 59) {
        sec = 99 * 60 + 59;
    }
    char b[20];
    snprintf(b, sizeof(b), "SLEEP %d:%02d", sec / 60, sec % 60);
    int tw = display_text_width(b, DISPLAY_FONT_TINY);
    display_fill_rect(clear_x, y - 1, clear_w, 11, UI_BG);
    display_draw_text((int16_t)(right - tw), y, b, DISPLAY_FONT_TINY, UI_ACC,
                      UI_BG, DISPLAY_DATUM_TR);
}

'''
t = t.replace(anchor, func + anchor, 1)

old = '''    display_draw_text(312, 24, vb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);
'''
new = '''    display_draw_text(312, 24, vb, DISPLAY_FONT_TINY, UI_MUTED, UI_BG,
                      DISPLAY_DATUM_TR);

    ui_draw_sleep_badge();
'''
assert old in t
t = t.replace(old, new)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui.h 선언
p = 'main/ui.h'
t = io.open(p, encoding='utf-8').read()
anchor2 = 'void ui_draw_all(const ui_state_t *s);'
assert anchor2 in t
if 'ui_draw_sleep_badge' not in t:
    t = t.replace(anchor2, anchor2 + '\nvoid ui_draw_sleep_badge(void);')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) main.c: 라디오 화면에서 매초 배지 갱신
p = 'main/main.c'
t = io.open(p, encoding='utf-8').read()
old3 = '        seek_poll(now);\n        ats_poll(now);'
new3 = ('        if ((s_hmi == HMI_TUNE || s_hmi == HMI_SEEK) &&\n'
        '            s_sleep_min > 0 && s_sleep_armed) {\n'
        '            static uint32_t sleep_badge_ms;\n'
        '            if ((now - sleep_badge_ms) >= 1000u) {\n'
        '                sleep_badge_ms = now;\n'
        '                ui_draw_sleep_badge();\n'
        '            }\n'
        '        }\n'
        '        seek_poll(now);\n'
        '        ats_poll(now);')
assert old3 in t
t = t.replace(old3, new3)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
