# -*- coding: utf-8 -*-
"""메뉴: BACK 최상단 고정 + EXLU 앱은 그 아래, 이름은 ASCII 안전 표시."""
import io

p = 'main/menu.c'
t = io.open(p, encoding='utf-8').read()

old = '''    static const char *labels[RMENU_MAX];
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
new = '''    static const char *labels[RMENU_MAX];
    static char app_labels[8][24];
    int n = 0;
    /* 1) BACK 은 항상 최상단 고정 */
    labels[n] = "< BACK";
    s_menu_ids[n] = RM_BACK;
    ++n;
    /* 2) EXLU 앱: BACK 바로 아래 */
    exlu_manifest_t apps[8];
    int na = exlu_app_list(apps, 8);
    for (int i = 0; i < na && n < RMENU_MAX; ++i) {
        /* 기기 폰트는 ASCII 만 표시 가능: 비ASCII/제어문자는 '_' 로 */
        size_t j = 0;
        for (const unsigned char *q = (const unsigned char *)apps[i].name;
             *q && j + 1 < sizeof(app_labels[0]); ++q) {
            if (*q >= 0x20 && *q < 0x7F) {
                app_labels[i][j++] = (char)*q;
            } else if (*q >= 0x80) {
                while (q[1] && (q[1] & 0xC0) == 0x80) {
                    ++q;
                }
                app_labels[i][j++] = '_';
            }
        }
        app_labels[i][j] = '\\0';
        if (j == 0) {
            snprintf(app_labels[i], sizeof(app_labels[i]), "%s", apps[i].id);
        }
        labels[n] = app_labels[i];
        s_menu_ids[n] = EXLU_MENU_BASE + i;
        ++n;
    }
    /* 3) 일반 메뉴 (BACK 중복 제외) */
    for (int i = 0; i < g_menu_count && n < RMENU_MAX; ++i) {
        if (g_menu_items[i].id == RM_BACK) {
            continue;
        }'''
assert old in t
t = t.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 에디터 힌트: 메뉴에는 영문/숫자만 표시됨
p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()
old2 = '''    <div class="phint">ID를 비우면 이름에서 자동 생성됩니다.</div>'''
new2 = '''    <div class="phint">ID를 비우면 이름에서 자동 생성됩니다. 기기 메뉴에는 영문/숫자만 표시됩니다(한글은 _ 로 표시).</div>'''
if old2 in t:
    t = t.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
