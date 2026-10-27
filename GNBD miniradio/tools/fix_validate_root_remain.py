# -*- coding: utf-8 -*-
import io

p = 'uiedt/js/core/validate.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('root.I18N.tm(message)', 'globalThis.I18N.tm(message)')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('root left:', t.count('root'))

p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('?v=4"', '?v=5"')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('v5 ok')
