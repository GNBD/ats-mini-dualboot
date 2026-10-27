# -*- coding: utf-8 -*-
"""에셋 캐시 강화(v=3) + no-store 헤더 보강."""
import io

# 1) index.html: v=2 -> v=3
p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('?v=2"', '?v=3"').replace('?v=2\'', '?v=3\'')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) 원본 UI 에디터도 동일하게 (공유 자산 캐시 무효화)
p = 'uiedt/index.html'
t = io.open(p, encoding='utf-8').read()
t = t.replace('?v=43"', '?v=44"').replace('?v=43\'', '?v=44\'')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) asset_handler: 캐시 금지 헤더 강화
p = 'main/ui/ui_web.c'
t = io.open(p, encoding='utf-8').read()
old = '''        httpd_resp_set_type(req, a->mime);
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, a->data, a->len);'''
new = '''        httpd_resp_set_type(req, a->mime);
        httpd_resp_set_hdr(req, "Cache-Control",
                           "no-store, no-cache, must-revalidate, max-age=0");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
        return httpd_resp_send(req, a->data, a->len);'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
