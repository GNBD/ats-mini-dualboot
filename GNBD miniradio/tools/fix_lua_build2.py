# -*- coding: utf-8 -*-
import io

# 1) ui_custom.h: override 선언 추가
p = 'main/ui/ui_custom.h'
t = io.open(p, encoding='utf-8').read()
anchor = 'int  ui_widget_override_hidden(uint16_t uid);'
assert anchor in t
if 'ui_widget_override_text(' not in t:
    t = t.replace(anchor,
                  'void ui_widget_override_text(uint16_t uid, const char *text);\n'
                  'void ui_widget_override_visible(uint16_t uid, bool visible);\n'
                  'void ui_widget_override_color(uint16_t uid, uint8_t color);\n'
                  + anchor)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_lua.c: ui_mode_str 대체 (ui_int.h 의존 제거)
p = 'main/ui/ui_lua.c'
t = io.open(p, encoding='utf-8').read()
old = '''static int l_radio_mode(lua_State *L)
{
    lua_pushstring(L, ui_mode_str(s_ui.mode));
    return 1;
}'''
new = '''static const char *mode_name(radio_mode_t m)
{
    switch (m) {
    case RADIO_MODE_FM:  return "FM";
    case RADIO_MODE_AM:  return "AM";
    default:             return "SSB";
    }
}

static int l_radio_mode(lua_State *L)
{
    lua_pushstring(L, mode_name(s_ui.mode));
    return 1;
}'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
