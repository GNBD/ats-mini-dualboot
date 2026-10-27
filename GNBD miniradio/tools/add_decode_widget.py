# -*- coding: utf-8 -*-
"""UI 런타임/에디터: 디코드 패널 위젯 + sleep 바인딩 연결."""
import io

# 1) ui_custom.h: 타입/바인딩 append (기존 값 유지)
p = 'main/ui/ui_custom.h'
t = io.open(p, encoding='utf-8').read()
old = '''    UC_MENU_GUIDE, UC_WIFI
} uc_type_t;'''
new = '''    UC_MENU_GUIDE, UC_WIFI, UC_DECODE
} uc_type_t;'''
assert old in t
t = t.replace(old, new, 1)

old2 = '''    UC_B_WIFI, UC_B_STATUS, UC_B_BATTV
} uc_bind_t;'''
new2 = '''    UC_B_WIFI, UC_B_STATUS, UC_B_BATTV, UC_B_SLEEP, UC_B_DECODE_LEVEL
} uc_bind_t;'''
assert old2 in t
t = t.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_runtime.c: 타입/바인딩 범위 검사 확장
p = 'main/ui/ui_runtime.c'
t = io.open(p, encoding='utf-8').read()
t = t.replace('if (v[0] < 0 || v[0] > UC_WIFI) { free(buf); return false; }',
              'if (v[0] < 0 || v[0] > UC_DECODE) { free(buf); return false; }')
t = t.replace('if (v[11] < 0 || v[11] > UC_B_BATTV) { free(buf); return false; }',
              'if (v[11] < 0 || v[11] > UC_B_DECODE_LEVEL) { free(buf); return false; }')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) ui_custom.c: 디코드 패널 렌더 + 바인딩 값
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()

# num_bind에 sleep/디코드 레벨
old3 = '''static int num_bind(uint8_t b, const ui_state_t *s)
{'''
new3 = '''static int num_bind(uint8_t b, const ui_state_t *s);

static int num_bind(uint8_t b, const ui_state_t *s)
{
    if (b == UC_B_SLEEP) {
        int32_t remain = (int32_t)(s_sleep_deadline - now_ms());
        if (!s_sleep_armed || s_sleep_min == 0) {
            return 0;
        }
        return (remain > 0) ? (int)((remain + 999) / 1000) : 0;
    }
    if (b == UC_B_DECODE_LEVEL) {
        return decode_level();
    }'''
assert old3 in t
t = t.replace(old3, new3, 1)

# 렌더 스위치에 UC_DECODE
old4 = '''        case UC_WIFI:
            draw_wifi(w, s);
            break;'''
new4 = '''        case UC_WIFI:
            draw_wifi(w, s);
            break;
        case UC_DECODE:
            draw_decode_panel(w);
            break;'''
assert old4 in t
t = t.replace(old4, new4, 1)

# draw_decode_panel 구현 (draw_wifi 앞)
anchor = 'static void draw_wifi(const uc_widget_t *w, const ui_state_t *s)'
assert anchor in t
impl = '''/* 디코드 패널: 상태/레벨/텍스트 4줄 */
static void draw_decode_panel(const uc_widget_t *w)
{
    display_fill_round_rect(w->x, w->y, w->w, w->h, 8, col(UC_PANEL));
    display_draw_round_rect(w->x, w->y, w->w, w->h, 8, col(UC_LINE));

    char title[40];
    snprintf(title, sizeof(title), "DECODE %s  GPIO %d",
             decode_mode_str(), decode_gpio());
    display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 2), title,
                      DISPLAY_FONT_TINY, col(UC_GOLD), col(UC_PANEL),
                      DISPLAY_DATUM_TL);

    const char *st = strncmp(decode_text(), "ADC ERR", 7) == 0 ? "ERR" : "RUN";
    display_draw_text((int16_t)(w->x + w->w - 4), (int16_t)(w->y + 2), st,
                      DISPLAY_FONT_TINY, col(st[0] == 'E' ? UC_RED : UC_OK),
                      col(UC_PANEL), DISPLAY_DATUM_TR);

    display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 14), "SIG",
                      DISPLAY_FONT_TINY, col(UC_MUTED), col(UC_PANEL),
                      DISPLAY_DATUM_TL);
    int bw = w->w - 26;
    if (bw < 8) { bw = 8; }
    bar(w->x + 26, w->y + 16, bw, 4, decode_level(), 100, col(UC_OK));

    const char *txt = decode_text();
    int len = (int)strlen(txt);
    int lines = (w->h - 30) / 13;
    if (lines < 1) { lines = 1; }
    if (lines > 4) { lines = 4; }
    if (len == 0) {
        display_draw_text((int16_t)(w->x + 4), (int16_t)(w->y + 26), "WAIT...",
                          DISPLAY_FONT_SMALL, col(UC_MUTED), col(UC_PANEL),
                          DISPLAY_DATUM_TL);
    } else {
        int per = 44;
        int start = (len > per * lines) ? len - per * lines : 0;
        for (int i = 0; i < lines && start < len; ++i) {
            int n = len - start;
            if (n > per) { n = per; }
            char buf[48];
            memcpy(buf, txt + start, (size_t)n);
            buf[n] = '\\0';
            display_draw_text((int16_t)(w->x + 4),
                              (int16_t)(w->y + 26 + i * 13), buf,
                              DISPLAY_FONT_SMALL, col(UC_FG), col(UC_PANEL),
                              DISPLAY_DATUM_TL);
            start += n;
        }
    }
}

'''
t = t.replace(anchor, impl + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
