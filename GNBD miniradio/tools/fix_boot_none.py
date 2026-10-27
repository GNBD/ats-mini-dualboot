# -*- coding: utf-8 -*-
"""부팅 선택: 'None' 항목 추가 + 제목 'Select Extended'."""
import io

p = 'main/ui/ui_exlu_app.c'
t = io.open(p, encoding='utf-8').read()

# 1) 제목 변경
t = t.replace('"SELECT EXLU"', '"Select Extended"')

# 2) 그리기: None 포함 (index 0 = None)
old = '''static void draw_boot_list(const exlu_manifest_t *m, int n, int cursor,
                           const char *status)
{'''
new = '''static void draw_boot_list(const exlu_manifest_t *m, int n, int cursor,
                           const char *status)
{
    /* index 0 = None, 1..n = EXLU 항목 */
'''
assert old in t
t = t.replace(old, new, 1)

old2 = '''    if (n == 0) {
        display_draw_text(UI_W / 2, 70, "NO EXLU", DISPLAY_FONT_TINY,
                          UI_MUTED, UI_PANEL, DISPLAY_DATUM_TC);
    }
    for (int i = 0; i < n && i < 6; ++i) {
        int y = 48 + i * 14;
        bool cur = (i == cursor);
        if (cur) {
            display_fill_round_rect(50, (int16_t)(y - 2), 220, 14, 6, UI_HSEL);
        }
        display_draw_text(56, (int16_t)y, m[i].name, DISPLAY_FONT_TINY,
                          cur ? UI_ACC_HI : UI_FG, cur ? UI_HSEL : UI_PANEL,
                          DISPLAY_DATUM_TL);
        display_draw_text(264, (int16_t)y,
                          m[i].kind == EXLU_KIND_THEME ? "THEME" : "APP",
                          DISPLAY_FONT_TINY, UI_MUTED,
                          cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TR);
    }'''
new2 = '''    for (int i = 0; i <= n && i < 6; ++i) {
        int y = 48 + i * 14;
        bool cur = (i == cursor);
        if (cur) {
            display_fill_round_rect(50, (int16_t)(y - 2), 220, 14, 6, UI_HSEL);
        }
        if (i == 0) {
            display_draw_text(56, (int16_t)y, "None", DISPLAY_FONT_TINY,
                              cur ? UI_ACC_HI : UI_FG,
                              cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TL);
            display_draw_text(264, (int16_t)y, "DEFAULT", DISPLAY_FONT_TINY,
                              UI_MUTED, cur ? UI_HSEL : UI_PANEL,
                              DISPLAY_DATUM_TR);
        } else {
            display_draw_text(56, (int16_t)y, m[i - 1].name, DISPLAY_FONT_TINY,
                              cur ? UI_ACC_HI : UI_FG,
                              cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TL);
            display_draw_text(264, (int16_t)y,
                              m[i - 1].kind == EXLU_KIND_THEME ? "THEME" : "APP",
                              DISPLAY_FONT_TINY, UI_MUTED,
                              cur ? UI_HSEL : UI_PANEL, DISPLAY_DATUM_TR);
        }
    }'''
assert old2 in t
t = t.replace(old2, new2, 1)

# 3) 선택 로직: cursor 0 = None
old3 = '''        if (pressed) {
            const char *err = NULL;
            if (m[cursor].kind == EXLU_KIND_THEME) {'''
new3 = '''        if (pressed) {
            if (cursor == 0) {
                exlu_set_boot_id("");
                ESP_LOGI(TAG, "boot selection: none");
                return false;
            }
            const char *err = NULL;
            if (m[cursor - 1].kind == EXLU_KIND_THEME) {'''
assert old3 in t
t = t.replace(old3, new3, 1)

# 참조 인덱스 보정
t = t.replace('if (exlu_load(m[cursor].id, &e) == EXLU_OK) {',
              'if (exlu_load(m[cursor - 1].id, &e) == EXLU_OK) {')
t = t.replace('err = exlu_app_launch(m[cursor].id);',
              'err = exlu_app_launch(m[cursor - 1].id);')
t = t.replace('exlu_set_boot_id(m[cursor].id);\n            ESP_LOGI(TAG, "boot selected: %s", m[cursor].id);',
              'exlu_set_boot_id(m[cursor - 1].id);\n            ESP_LOGI(TAG, "boot selected: %s", m[cursor - 1].id);')

# 4) 커서 범위: None 포함 (n+1)
t = t.replace('cursor = (cursor + (d > 0 ? 1 : -1) + n) % n;',
              'cursor = (cursor + (d > 0 ? 1 : -1) + (n + 1)) % (n + 1);')

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
