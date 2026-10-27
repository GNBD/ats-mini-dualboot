# -*- coding: utf-8 -*-
import io

p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('?v=3"', '?v=4"')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok', t.count('?v=4"'))
