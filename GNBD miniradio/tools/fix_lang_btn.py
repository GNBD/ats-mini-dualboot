# -*- coding: utf-8 -*-
import io

p = 'uiedt/js/i18n.js'
t = io.open(p, encoding='utf-8').read()
old = 'btn.textContent = this.lang === "en" ? "\ud55c\uad6d\uc5b4" : "EN";'
new = 'btn.textContent = this.lang === "en" ? "KO" : "EN";'
assert old in t, 'langBtn line not found'
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
