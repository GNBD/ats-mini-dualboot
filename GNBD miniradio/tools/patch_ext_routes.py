# -*- coding: utf-8 -*-
"""EXTENDED Editor 페이지 라우트 등록 + 리다이렉트 보강."""
import io

p = 'main/ui/ui_web.c'
t = io.open(p, encoding='utf-8').read()

# 1) /ui/extended -> /ui/extended/ 리다이렉트
old = '''    if (strcmp(path, "/ui/extended/") == 0) want = "/ui/extended/index.html";'''
new = '''    if (strcmp(path, "/ui/extended") == 0) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/ui/extended/");
        return httpd_resp_send(req, "", 0);
    }
    if (strcmp(path, "/ui/extended/") == 0) want = "/ui/extended/index.html";'''
assert old in t
t = t.replace(old, new, 1)

# 2) 라우트 등록
old2 = '''    static httpd_uri_t ui_index = {
        .uri = "/ui/", .method = HTTP_GET, .handler = asset_handler,
    };
    httpd_register_uri_handler(server, &ui_root);
    httpd_register_uri_handler(server, &ui_index);'''
new2 = '''    static httpd_uri_t ui_index = {
        .uri = "/ui/", .method = HTTP_GET, .handler = asset_handler,
    };
    static httpd_uri_t ext_root = {
        .uri = "/ui/extended", .method = HTTP_GET, .handler = asset_handler,
    };
    static httpd_uri_t ext_index = {
        .uri = "/ui/extended/", .method = HTTP_GET, .handler = asset_handler,
    };
    httpd_register_uri_handler(server, &ui_root);
    httpd_register_uri_handler(server, &ui_index);
    httpd_register_uri_handler(server, &ext_root);
    httpd_register_uri_handler(server, &ext_index);'''
assert old2 in t
t = t.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
