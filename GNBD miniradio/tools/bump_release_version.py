# -*- coding: utf-8 -*-
"""릴리즈 버전 올리기 (PROJECT_VER/project)."""
import io
import re

p = 'CMakeLists.txt'
t = io.open(p, encoding='utf-8').read()
t = re.sub(r'set\(PROJECT_VER\s+"stable [^"]+"\)', 'set(PROJECT_VER "stable 1.1.22")', t)
t = re.sub(r'project\(miniradio VERSION [0-9.]+\)', 'project(miniradio VERSION 1.1.22)', t)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print([l for l in t.splitlines() if 'PROJECT_VER' in l or 'project(miniradio' in l])
