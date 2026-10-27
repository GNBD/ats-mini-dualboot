# -*- coding: utf-8 -*-
import io

p = 'main/CMakeLists.txt'
t = io.open(p, encoding='utf-8').read()

ui = ['ui_core.c', 'ui_freq.c', 'ui_home.c', 'ui_spectrum.c', 'ui_quick.c',
      'ui_dx.c', 'ui_list.c', 'ui_wifi.c', 'ui_misc.c', 'ui_custom.c',
      'ui_runtime.c', 'ui_web.c', 'ui_editor_assets.c']
for f in ui:
    t = t.replace('"' + f + '"', '"ui/' + f + '"')

t = t.replace('INCLUDE_DIRS "."', 'INCLUDE_DIRS "." "ui"')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print(t)
