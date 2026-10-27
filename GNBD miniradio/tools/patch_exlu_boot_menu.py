# -*- coding: utf-8 -*-
"""부팅 선택/메뉴 통합/CMake 연결."""
import io

# 1) ui_exlu_app.c 정리: placeholder 제거 + 타임아웃 시 boot id 자동 적용
p = 'main/ui/ui_exlu_app.c'
t = io.open(p, encoding='utf-8').read()
old = '''    exlu_manifest_t m[BOOT_LIST_MAX];
    int n = exlu_app_list(m, 0) ? 0 : 0;   /* placeholder */
    n = 0;
    {'''
new = '''    exlu_manifest_t m[BOOT_LIST_MAX];
    int n = 0;
    {'''
assert old in t
t = t.replace(old, new, 1)

old2 = '''    (void)last_delta;
    return false;
}'''
new2 = '''    (void)last_delta;
    /* 타임아웃: 마지막으로 선택했던 항목이 있으면 자동 실행 */
    const char *auto_id = exlu_get_boot_id();
    if (auto_id && auto_id[0]) {
        for (int i = 0; i < n; ++i) {
            if (strcmp(m[i].id, auto_id) == 0) {
                if (m[i].kind == EXLU_KIND_THEME) {
                    exlu_t e;
                    if (exlu_load(auto_id, &e) == EXLU_OK) {
                        exlu_apply(&e);
                        exlu_free(&e);
                    }
                } else {
                    exlu_app_launch(auto_id);
                }
                ESP_LOGI(TAG, "boot auto: %s", auto_id);
                return true;
            }
        }
    }
    return false;
}'''
assert old2 in t
t = t.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) menu.c: 앱 항목 최상단 + 실행
p = 'main/menu.c'
t = io.open(p, encoding='utf-8').read()
if '#include "ui_exlu_app.h"' not in t:
    t = t.replace('#include "ui_runtime.h"',
                  '#include "ui_runtime.h"\n#include "ui_exlu_app.h"', 1)

old3 = '''static int s_menu_ids[RM_COUNT];
static int s_set_ids[ST_COUNT];'''
new3 = '''#define RMENU_MAX (RM_COUNT + 8)
static int s_menu_ids[RMENU_MAX];
static int s_set_ids[ST_COUNT];'''
assert old3 in t
t = t.replace(old3, new3, 1)

old4 = '''    static const char *labels[RM_COUNT];
    int n = 0;
    for (int i = 0; i < g_menu_count && n < RM_COUNT; ++i) {'''
new4 = '''    static const char *labels[RMENU_MAX];
    int n = 0;
    /* EXLU 앱: 메뉴 최상단에 표시 */
    exlu_manifest_t apps[8];
    int na = exlu_app_list(apps, 8);
    for (int i = 0; i < na && n < RMENU_MAX; ++i) {
        labels[n] = apps[i].name;
        s_menu_ids[n] = EXLU_MENU_BASE + i;
        ++n;
    }
    for (int i = 0; i < g_menu_count && n < RMENU_MAX; ++i) {'''
assert old4 in t
t = t.replace(old4, new4, 1)

old5 = '    for (int i = 0; i < s_rmenu.count; ++i) {\n        s_rv[i] = s_rbuf[s_menu_ids[i]];'
if old5 not in t:
    # 실제 코드 찾기
    idx = t.find('s_rv[i] = s_rbuf[s_menu_ids[i]]')
    assert idx > 0
    line_start = t.rfind('\n', 0, idx)
    t = t[:line_start] + '\n        s_rv[i] = (s_menu_ids[i] >= EXLU_MENU_BASE)\n                   ? "RUN" : s_rbuf[s_menu_ids[i]];' + t[t.find(';', idx)+1:]
else:
    t = t.replace(old5, '    for (int i = 0; i < s_rmenu.count; ++i) {\n        s_rv[i] = (s_menu_ids[i] >= EXLU_MENU_BASE)\n                   ? "RUN" : s_rbuf[s_menu_ids[i]];', 1)

old6 = '''bool menu_item_is_action(int item)
{
    int id = menu_id_at(item);
    return (id == RM_BACK || id == RM_SEEK || id == RM_SCAN ||'''
new6 = '''bool menu_item_is_action(int item)
{
    int id = menu_id_at(item);
    if (id >= EXLU_MENU_BASE) {
        return true;
    }
    return (id == RM_BACK || id == RM_SEEK || id == RM_SCAN ||'''
assert old6 in t
t = t.replace(old6, new6, 1)

old7 = '''void menu_action(int item)
{
    int id = menu_id_at(item);
    bool mod_hit = false;'''
new7 = '''void menu_action(int item)
{
    int id = menu_id_at(item);
    bool mod_hit = false;
    if (id >= EXLU_MENU_BASE) {
        exlu_manifest_t apps[8];
        int na = exlu_app_list(apps, 8);
        int ai = id - EXLU_MENU_BASE;
        if (ai >= 0 && ai < na) {
            const char *err = exlu_app_launch(apps[ai].id);
            if (err) {
                toast(err);
            } else {
                page_radio();
            }
        }
        return;
    }'''
assert old7 in t
t = t.replace(old7, new7, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) main.c: 부팅 시 EXLU 선택
p = 'main/main.c'
t = io.open(p, encoding='utf-8').read()
if '#include "ui_exlu_app.h"' not in t:
    t = t.replace('#include "ui_exlu.h"',
                  '#include "ui_exlu.h"\n#include "ui_exlu_app.h"', 1)
old8 = '''        brightness_apply();
        splash_show();
        splash_service_input();'''
new8 = '''        brightness_apply();
        splash_show();
        splash_service_input();
        /* littlefs 의 EXLU 선택 (없으면 즉시 통과) */
        exlu_boot_select();'''
assert old8 in t
t = t.replace(old8, new8, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 4) CMake
p = 'main/CMakeLists.txt'
t = io.open(p, encoding='utf-8').read()
if 'ui/ui_exlu_app.c' not in t:
    t = t.replace('"ui/ui_exlu.c"', '"ui/ui_exlu.c" "ui/ui_exlu_app.c"')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
