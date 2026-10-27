# -*- coding: utf-8 -*-
import io

p = r'C:\Users\Administrator\Desktop\Development\extfirm\main1\tools\copy_to_dual_repo.py'
t = io.open(p, encoding='utf-8').read()
t = t.replace('SKIP_EXT = {".bin", ".elf", ".map", ".o", ".a"}',
              'SKIP_EXT = {".bin", ".elf", ".o", ".a"}')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok', 'SKIP_EXT' in t)
