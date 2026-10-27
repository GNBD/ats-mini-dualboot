# -*- coding: utf-8 -*-
"""위젯 오버라이드(text/visible/color) + uid 조회 + 바인딩 값 API."""
import io

# 1) ui_runtime.h 선언
p = 'main/ui/ui_runtime.h'
t = io.open(p, encoding='utf-8').read()
anchor = 'bool ui_runtime_panel(bool settings, int *x, int *y, int *w, int *h, int *orient);'
assert anchor in t
if 'ui_runtime_find' not in t:
    t = t.replace(anchor,
                  anchor + '\n\n/* uid 로 위젯 찾기 (Lua 이벤트 바인딩용) */\n'
                  'const uc_widget_t *ui_runtime_find(uint16_t uid);')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_runtime.c 구현
p = 'main/ui/ui_runtime.c'
t = io.open(p, encoding='utf-8').read()
anchor2 = 'bool ui_runtime_panel(bool settings, int *x, int *y, int *w, int *h, int *orient)'
assert anchor2 in t
impl = '''const uc_widget_t *ui_runtime_find(uint16_t uid)
{
    if (uid == 0) {
        return NULL;
    }
    for (int i = 0; i < s_count; ++i) {
        if (s_widgets[i].uid == uid) {
            return &s_widgets[i];
        }
    }
    /* 내장 레이아웃(ui_custom_layout.h)도 검색 */
    if (!s_enabled) {
        for (int i = 0; i < UC_WIDGET_COUNT; ++i) {
            if (uc_widgets[i].uid == uid) {
                return &uc_widgets[i];
            }
        }
    }
    return NULL;
}

'''
t = t.replace(anchor2, impl + anchor2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) ui_custom.c: 오버라이드 저장/조회 + 바인딩 값
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()

anchor3 = 'int ui_custom_active(void)' if 'int ui_custom_active(void)' in t else 'bool ui_custom_active(void)'
assert anchor3 in t

ov = '''/* ---------------- Lua 위젯 오버라이드 ---------------- */

#define UI_OV_MAX 32

typedef struct {
    uint16_t uid;
    char     text[32];
    uint8_t  color;
    int8_t   visible;   /* -1 = 미설정 */
} ui_ov_t;

static ui_ov_t s_ov[UI_OV_MAX];

static ui_ov_t *ov_get(uint16_t uid, bool create)
{
    for (int i = 0; i < UI_OV_MAX; ++i) {
        if (s_ov[i].uid == uid) {
            return &s_ov[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (int i = 0; i < UI_OV_MAX; ++i) {
        if (s_ov[i].uid == 0) {
            s_ov[i].uid = uid;
            s_ov[i].visible = -1;
            s_ov[i].color = 0xFF;
            s_ov[i].text[0] = '\\0';
            return &s_ov[i];
        }
    }
    return NULL;
}

void ui_widget_override_text(uint16_t uid, const char *text)
{
    ui_ov_t *o = ov_get(uid, true);
    if (!o) {
        return;
    }
    snprintf(o->text, sizeof(o->text), "%s", text ? text : "");
}

void ui_widget_override_visible(uint16_t uid, bool visible)
{
    ui_ov_t *o = ov_get(uid, true);
    if (o) {
        o->visible = visible ? 1 : 0;
    }
}

void ui_widget_override_color(uint16_t uid, uint8_t color)
{
    ui_ov_t *o = ov_get(uid, true);
    if (o) {
        o->color = color;
    }
}

int ui_widget_override_hidden(uint16_t uid)
{
    ui_ov_t *o = ov_get(uid, false);
    return (o && o->visible == 0) ? 1 : 0;
}

const char *ui_widget_override_text_of(uint16_t uid)
{
    ui_ov_t *o = ov_get(uid, false);
    return (o && o->text[0]) ? o->text : NULL;
}

int ui_widget_override_color_of(uint16_t uid, int def)
{
    ui_ov_t *o = ov_get(uid, false);
    return (o && o->color != 0xFF) ? (int)o->color : def;
}

int ui_widget_bind_value(const uc_widget_t *w)
{
    if (!w) {
        return 0;
    }
    return num_bind(w->bind, &s_ui);
}

'''
t = t.replace(anchor3, ov + anchor3, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
