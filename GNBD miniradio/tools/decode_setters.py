# -*- coding: utf-8 -*-
"""decode setter 구현 + 디코드 위젯 옵션 문자열 반영."""
import io

# 1) decode.c 구현
p = 'main/decode.c'
t = io.open(p, encoding='utf-8').read()
anchor = 'void decode_cycle_mode(void)'
assert anchor in t
impl = '''void decode_set_gpio(int pin)
{
    if (pin < 0 || pin > 48) {
        return;
    }
    s_gpio = pin;
}

void decode_set_mode(int idx)
{
    s_mode = (idx == 1) ? DECODE_MODE_RTTY : DECODE_MODE_CW;
}

'''
if 'decode_set_gpio' not in t:
    t = t.replace(anchor, impl + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_custom.c: 옵션 문자열 파싱 (gpio=/mode=/lines=)
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()

old = '''static void draw_decode_panel(const uc_widget_t *w)
{
    display_fill_round_rect(w->x, w->y, w->w, w->h, 8, col(UC_PANEL));'''
new = '''static int opt_int(const char *s, const char *key, int def)
{
    if (!s) {
        return def;
    }
    size_t kl = strlen(key);
    const char *p = s;
    while ((p = strstr(p, key)) != NULL) {
        p += kl;
        if (*p == '=') {
            return atoi(p + 1);
        }
    }
    return def;
}

static void draw_decode_panel(const uc_widget_t *w)
{
    /* 옵션 문자열: "gpio=4;mode=0;lines=4" (위젯 text 필드) */
    int gpio = opt_int(w->text, "gpio", -1);
    int mode = opt_int(w->text, "mode", -1);
    int lines = opt_int(w->text, "lines", 0);
    if (gpio >= 0) {
        decode_set_gpio(gpio);
    }
    if (mode >= 0) {
        decode_set_mode(mode);
    }

    display_fill_round_rect(w->x, w->y, w->w, w->h, 8, col(UC_PANEL));'''
assert old in t
t = t.replace(old, new, 1)

# lines 옵션 반영
old2 = '''    int lines = (w->h - 30) / 13;
    if (lines < 1) { lines = 1; }
    if (lines > 4) { lines = 4; }'''
new2 = '''    if (lines <= 0) {
        lines = (w->h - 30) / 13;
    }
    if (lines < 1) { lines = 1; }
    if (lines > 4) { lines = 4; }'''
assert old2 in t
t = t.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) stdlib (atoi) 포함 확인
if '#include <stdlib.h>' not in t:
    pass
print('ok')
