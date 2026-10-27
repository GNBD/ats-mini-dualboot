# -*- coding: utf-8 -*-
import io

p = 'main/CMakeLists.txt'
t = io.open(p, encoding='utf-8').read()
if 'ui/ui_lua.c' not in t:
    t = t.replace('"ui/ui_exlu.c"', '"ui/ui_exlu.c" "ui/ui_lua.c"')
if ' lua)' not in t:
    t = t.replace('esp_http_client esp-tls littlefs mbedtls)',
                  'esp_http_client esp-tls littlefs mbedtls lua)')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
