# -*- coding: utf-8 -*-
"""EXLU 왕복 무손실: 원문 저장/읽기 API 추가 + 웹 API 연결."""
import io

# 1) ui_exlu.h 선언
p = 'main/ui/ui_exlu.h'
t = io.open(p, encoding='utf-8').read()
anchor = 'exlu_err_t exlu_save(const char *id, const exlu_t *e, bool overwrite);'
assert anchor in t
if 'exlu_store_raw' not in t:
    t = t.replace(anchor,
                  anchor + '\n/* 검증만 수행하고 원문을 그대로 저장/읽기 (알 수 없는 섹션 보존) */\n'
                  'exlu_err_t exlu_store_raw(const char *id, const char *text,\n'
                  '                          size_t len, bool overwrite);\n'
                  'exlu_err_t exlu_read_raw(const char *id, char **out, size_t *len);')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_exlu.c 구현 추가
p = 'main/ui/ui_exlu.c'
t = io.open(p, encoding='utf-8').read()
anchor2 = 'exlu_err_t exlu_remove(const char *id)'
assert anchor2 in t
impl = '''exlu_err_t exlu_store_raw(const char *id, const char *text, size_t len,
                           bool overwrite)
{
    if (!id || !text || len == 0) {
        return EXLU_ERR_ARG;
    }
    exlu_t e;
    exlu_err_t err = exlu_parse(text, len, &e);
    if (err != EXLU_OK) {
        return err;
    }
    exlu_free(&e);

    char path[96];
    path_of(id, path, sizeof(path));
    if (!overwrite) {
        FILE *chk = fopen(path, "rb");
        if (chk) {
            fclose(chk);
            return EXLU_ERR_EXISTS;
        }
    }
    mkdir(EXLU_DIR, 0777);
    const char *tmp = "/littlefs/ui/.exlu.tmp";
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return EXLU_ERR_LAYOUT;
    }
    size_t wr = fwrite(text, 1, len, f);
    fclose(f);
    if (wr != len) {
        remove(tmp);
        return EXLU_ERR_LAYOUT;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        return EXLU_ERR_LAYOUT;
    }
    return EXLU_OK;
}

exlu_err_t exlu_read_raw(const char *id, char **out, size_t *len)
{
    if (!id || !out) {
        return EXLU_ERR_ARG;
    }
    char path[96];
    path_of(id, path, sizeof(path));
    size_t n = 0;
    char *buf = read_all(path, &n);
    if (!buf) {
        return EXLU_ERR_NOT_FOUND;
    }
    *out = buf;
    if (len) {
        *len = n;
    }
    return EXLU_OK;
}

'''
t = t.replace(anchor2, impl + anchor2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) ui_web.c: save는 원문 저장, get은 원문 반환
p = 'main/ui/ui_web.c'
t = io.open(p, encoding='utf-8').read()

old_get_start = '    exlu_t e;\n    exlu_err_t err = exlu_load(id, &e);\n    if (err != EXLU_OK) {\n        return send_err(req, "404 Not Found", exlu_strerror(err));\n    }\n    char *out = (char *)malloc(EXLU_MAX_FILE_SIZE + 256);'
new_get_start = '''    char *out = NULL;
    size_t olen = 0;
    exlu_err_t err = exlu_read_raw(id, &out, &olen);
    if (err != EXLU_OK) {
        return send_err(req, "404 Not Found", exlu_strerror(err));
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, out, (ssize_t)olen);
    free(out);
    return r;
#if 0
    char *out2 = (char *)malloc(EXLU_MAX_FILE_SIZE + 256);'''
assert old_get_start in t
t = t.replace(old_get_start, new_get_start, 1)

# 기존 get 재조립 코드는 #if 0 로 무력화하고 #endif 추가
old_tail = '''    esp_err_t r = httpd_resp_send(req, out, (ssize_t)p);
    free(out);
    exlu_free(&e);
    return r;
}'''
new_tail = '''    esp_err_t r2 = httpd_resp_send(req, out2, (ssize_t)p);
    free(out2);
    exlu_free(&e);
    return r2;
#endif
}'''
assert old_tail in t
t = t.replace(old_tail, new_tail, 1)

# 저장: 검증 + 원문 저장
old_save = '''    exlu_t e;
    exlu_err_t err = exlu_parse(body, len, &e);
    free(body);
    if (err != EXLU_OK) {
        ESP_LOGW(TAG, "exlu save rejected (%s): %s", id, exlu_strerror(err));
        return send_err(req, "400 Bad Request", exlu_strerror(err));
    }
    err = exlu_save(id, &e, overwrite);
    exlu_free(&e);'''
new_save = '''    exlu_err_t err = exlu_store_raw(id, body, len, overwrite);
    free(body);'''
assert old_save in t
t = t.replace(old_save, new_save, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
