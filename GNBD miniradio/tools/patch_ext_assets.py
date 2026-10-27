# -*- coding: utf-8 -*-
import io

# 1) extended/index.html: app.js가 참조하는 숨김 버튼 추가
p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()
old = '  <input type="file" id="fileIn" accept=".json,.mrpack,.exlu" hidden>'
new = ('  <input type="file" id="fileIn" accept=".json,.mrpack,.exlu" hidden>\n'
       '  <button id="btnRadioSave" hidden></button>\n'
       '  <button id="btnRadioManage" hidden></button>\n'
       '  <button id="langBtn" hidden></button>')
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_web.c: 에셋 라우트 슬롯 32 -> 64
p = 'main/ui/ui_web.c'
t = io.open(p, encoding='utf-8').read()
t = t.replace('static httpd_uri_t assets[32];', 'static httpd_uri_t assets[64];')
t = t.replace('for (int i = 0; i < ui_editor_asset_count && i < 32; ++i) {',
              'for (int i = 0; i < ui_editor_asset_count && i < 64; ++i) {')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
