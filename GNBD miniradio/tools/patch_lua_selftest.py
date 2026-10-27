# -*- coding: utf-8 -*-
import io

p = 'main/main.c'
t = io.open(p, encoding='utf-8').read()

if '#include "ui_lua.h"' not in t:
    t = t.replace('#include "ui_exlu.h"',
                  '#include "ui_exlu.h"\n#include "ui_lua.h"', 1)

old = '    rds_parser_reset(&s_rds);'
new = '''    {
        /* Lua 런타임 자가 점검 (PSRAM 힙/샌드박스 확인) */
        lua_State *L = lua_port_new();
        if (L) {
            const char *e = lua_port_exec(L, "return 1+1", 10, "=selftest");
            ESP_LOGI(TAG, "lua selftest %s", e ? e : "ok");
            lua_port_close(L);
        }
    }
    rds_parser_reset(&s_rds);'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('main patched')
