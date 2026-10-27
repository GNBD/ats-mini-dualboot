# -*- coding: utf-8 -*-
"""EXTENDED Editor 가 기존 /ui 자산(js/css)을 공유하도록 경로 변경 + 중복 사본 제거."""
import io
import os
import shutil

p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('href="css/app.css?v=1"', 'href="../css/app.css?v=43"')
t = t.replace('src="js/', 'src="../js/')
t = t.replace('src="../js/extended.js?v=1"', 'src="js/extended.js?v=1"')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 중복 사본 제거 (extended/js/extended.js 만 유지)
ext = 'uiedt/extended'
keep = os.path.join(ext, 'js', 'extended.js')
tmp = keep + '.tmp'
if os.path.exists(keep):
    shutil.copyfile(keep, tmp)
if os.path.isdir(os.path.join(ext, 'js')):
    shutil.rmtree(os.path.join(ext, 'js'))
os.makedirs(os.path.join(ext, 'js'), exist_ok=True)
if os.path.exists(tmp):
    shutil.move(tmp, keep)
if os.path.isdir(os.path.join(ext, 'css')):
    shutil.rmtree(os.path.join(ext, 'css'))
print('ok')
