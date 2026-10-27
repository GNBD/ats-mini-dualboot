# -*- coding: utf-8 -*-
"""EXTENDED(EXLU) 기능 전체 비활성화 플래그 적용."""
import io

# 1) 플래그 정의
p = 'main/ui/ui_exlu_app.h'
t = io.open(p, encoding='utf-8').read()
if '#define EXLU_ENABLE' not in t:
    t = t.replace('#include "ui_exlu.h"',
                  '#include "ui_exlu.h"\n\n/* EXTENDED(EXLU) 기능 스위치: 0 = 숨김/비활성 */\n'
                  '#ifndef EXLU_ENABLE\n#define EXLU_ENABLE 0\n#endif')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) 메뉴: EXLU 앱 항목 숨김
p = 'main/menu.c'
t = io.open(p, encoding='utf-8').read()
old = '''    /* 2) EXLU 앱: 부팅에서 선택한 앱만 BACK 바로 아래에 노출.
          (None/미선택이면 메뉴에 아무것도 추가하지 않는다) */
    exlu_manifest_t apps[1];
    int na = 0;
    {'''
new = '''    /* 2) EXLU 앱: 부팅에서 선택한 앱만 BACK 바로 아래에 노출.
          (None/미선택이면 메뉴에 아무것도 추가하지 않는다) */
    exlu_manifest_t apps[1];
    int na = 0;
#if EXLU_ENABLE
    {'''
assert old in t
t = t.replace(old, new, 1)

old2 = '''                exlu_free(&be);
            }
        }
    }
    for (int i = 0; i < na && n < RMENU_MAX; ++i) {'''
new2 = '''                exlu_free(&be);
            }
        }
    }
#endif
    for (int i = 0; i < na && n < RMENU_MAX; ++i) {'''
assert old2 in t
t = t.replace(old2, new2, 1)

old3 = '''    if (id >= EXLU_MENU_BASE) {
        const char *boot = exlu_get_boot_id();'''
new3 = '''    if (id >= EXLU_MENU_BASE) {
#if EXLU_ENABLE
        const char *boot = exlu_get_boot_id();'''
assert old3 in t
t = t.replace(old3, new3, 1)

old4 = '''            } else {
                page_radio();
            }
        }
        return;
    }'''
new4 = '''            } else {
                page_radio();
            }
        }
#endif
        return;
    }'''
assert old4 in t
t = t.replace(old4, new4, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) main.c: 부팅 선택/테마 자동적용 숨김
p = 'main/main.c'
t = io.open(p, encoding='utf-8').read()
old5 = '''        /* littlefs 의 EXLU 선택 (없으면 즉시 통과) */
        exlu_boot_select();'''
new5 = '''#if EXLU_ENABLE
        /* littlefs 의 EXLU 선택 (없으면 즉시 통과) */
        exlu_boot_select();
#endif'''
assert old5 in t
t = t.replace(old5, new5, 1)

old6 = '''    if (s_theme == UI_THEME_CUSTOM) {
        exlu_boot_apply_theme();   /* littlefs 의 테마 EXLU 자동 적용 */
    }'''
new6 = '''#if EXLU_ENABLE
    if (s_theme == UI_THEME_CUSTOM) {
        exlu_boot_apply_theme();   /* littlefs 의 테마 EXLU 자동 적용 */
    }
#endif'''
assert old6 in t
t = t.replace(old6, new6, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 4) ui_web.c: EXLU API/페이지 라우트 숨김
p = 'main/ui/ui_web.c'
t = io.open(p, encoding='utf-8').read()
if '#include "ui_exlu_app.h"' not in t:
    t = t.replace('#include "ui_exlu.h"',
                  '#include "ui_exlu.h"\n#include "ui_exlu_app.h"', 1)

old7 = '''    httpd_register_uri_handler(server, &exlu_list_u);
    httpd_register_uri_handler(server, &exlu_get_u);
    httpd_register_uri_handler(server, &exlu_save_u);
    httpd_register_uri_handler(server, &exlu_del_u);
    httpd_register_uri_handler(server, &exlu_apply_u);'''
new7 = '''#if EXLU_ENABLE
    httpd_register_uri_handler(server, &exlu_list_u);
    httpd_register_uri_handler(server, &exlu_get_u);
    httpd_register_uri_handler(server, &exlu_save_u);
    httpd_register_uri_handler(server, &exlu_del_u);
    httpd_register_uri_handler(server, &exlu_apply_u);
#else
    (void)exlu_list_u; (void)exlu_get_u; (void)exlu_save_u;
    (void)exlu_del_u; (void)exlu_apply_u;
#endif'''
assert old7 in t
t = t.replace(old7, new7, 1)

old8 = '''    static httpd_uri_t ext_root = {
        .uri = "/ui/extended", .method = HTTP_GET, .handler = asset_handler,
    };
    static httpd_uri_t ext_index = {
        .uri = "/ui/extended/", .method = HTTP_GET, .handler = asset_handler,
    };
    httpd_register_uri_handler(server, &ui_root);
    httpd_register_uri_handler(server, &ui_index);
    httpd_register_uri_handler(server, &ext_root);
    httpd_register_uri_handler(server, &ext_index);'''
new8 = '''    httpd_register_uri_handler(server, &ui_root);
    httpd_register_uri_handler(server, &ui_index);
#if EXLU_ENABLE
    static httpd_uri_t ext_root = {
        .uri = "/ui/extended", .method = HTTP_GET, .handler = asset_handler,
    };
    static httpd_uri_t ext_index = {
        .uri = "/ui/extended/", .method = HTTP_GET, .handler = asset_handler,
    };
    httpd_register_uri_handler(server, &ext_root);
    httpd_register_uri_handler(server, &ext_index);
#endif'''
assert old8 in t
t = t.replace(old8, new8, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
