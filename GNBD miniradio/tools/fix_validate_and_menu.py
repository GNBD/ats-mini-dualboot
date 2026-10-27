# -*- coding: utf-8 -*-
"""validate.js root 참조 버그 수정 + 메뉴는 부팅 선택 앱만 표시."""
import io

# 1) validate.js: root -> globalThis (브라우저에서 미정의 변수)
p = 'uiedt/js/core/validate.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('(root && root.I18N)', '(globalThis.I18N && globalThis.I18N)')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) menu.c: 부팅 선택(boot_app.txt)에 등록된 앱만 메뉴에 노출
p = 'main/menu.c'
t = io.open(p, encoding='utf-8').read()

old = '''    /* 2) EXLU 앱: BACK 바로 아래 */
    exlu_manifest_t apps[8];
    int na = exlu_app_list(apps, 8);
    for (int i = 0; i < na && n < RMENU_MAX; ++i) {'''
new = '''    /* 2) EXLU 앱: 부팅에서 선택한 앱만 BACK 바로 아래에 노출.
          (None/미선택이면 메뉴에 아무것도 추가하지 않는다) */
    exlu_manifest_t apps[1];
    int na = 0;
    {
        const char *boot = exlu_get_boot_id();
        if (boot && boot[0]) {
            exlu_t be;
            if (exlu_load(boot, &be) == EXLU_OK) {
                if (be.manifest.kind == EXLU_KIND_APP) {
                    apps[0] = be.manifest;
                    na = 1;
                }
                exlu_free(&be);
            }
        }
    }
    for (int i = 0; i < na && n < RMENU_MAX; ++i) {'''
assert old in t
t = t.replace(old, new, 1)

# menu_action: 동일하게 boot 앱 기준
old2 = '''    if (id >= EXLU_MENU_BASE) {
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
new2 = '''    if (id >= EXLU_MENU_BASE) {
        const char *boot = exlu_get_boot_id();
        if (boot && boot[0]) {
            const char *err = exlu_app_launch(boot);
            if (err) {
                toast(err);
            } else {
                page_radio();
            }
        }
        return;
    }'''
assert old2 in t
t = t.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
