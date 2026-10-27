# -*- coding: utf-8 -*-
import io

p = 'uiedt/js/core/schema.js'
t = io.open(p, encoding='utf-8').read()
old = '''    "menu", "wifi",
  ];'''
new = '''    "menu", "wifi", "decode", "batt_icon",
  ];'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
