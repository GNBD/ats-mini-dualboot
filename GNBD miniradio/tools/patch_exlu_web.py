# -*- coding: utf-8 -*-
"""ui_web.c 에 EXLU API(list/get/save/delete/apply) 추가."""
import io

p = 'main/ui/ui_web.c'
t = io.open(p, encoding='utf-8').read()

# include
if '#include "ui_exlu.h"' not in t:
    t = t.replace('#include "ui_runtime.h"',
                  '#include "ui_runtime.h"\n#include "ui_exlu.h"', 1)

# API 구현 삽입 (asset_handler 앞)
anchor = 'static esp_err_t asset_handler(httpd_req_t *req)'
assert anchor in t

api = '''/* ---------------- EXLU (테마/확장 앱) ---------------- */

static esp_err_t api_exlu_list(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    exlu_manifest_t m[16];
    int n = exlu_list(m, 16);
    char *out = (char *)malloc(2048);
    if (!out) return ESP_FAIL;
    int p = snprintf(out, 2048, "{\\"files\\":[");
    for (int i = 0; i < n && p < 1900; ++i) {
        p += snprintf(out + p, 2048 - p,
                      "%s{\\"id\\":\\"%s\\",\\"name\\":\\"%s\\","
                      "\\"author\\":\\"%s\\",\\"kind\\":\\"%s\\","
                      "\\"type\\":\\"%s\\"}",
                      i ? "," : "", m[i].id, m[i].name, m[i].author,
                      (m[i].kind == EXLU_KIND_THEME) ? "theme" : "app",
                      (m[i].type == EXLU_TYPE_SCRIPT) ? "script" : "layout");
    }
    snprintf(out + p, 2048 - p, "]}");
    esp_err_t e = send_json(req, out);
    free(out);
    return e;
}

static esp_err_t api_exlu_get(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char id[EXLU_NAME_MAX];
    if (!query(req, "id", id, sizeof(id))) {
        return send_err(req, "400 Bad Request", "E_ARG: id required");
    }
    exlu_t e;
    exlu_err_t err = exlu_load(id, &e);
    if (err != EXLU_OK) {
        return send_err(req, "404 Not Found", exlu_strerror(err));
    }
    char *out = (char *)malloc(EXLU_MAX_FILE_SIZE + 256);
    if (!out) {
        exlu_free(&e);
        return ESP_FAIL;
    }
    int p = snprintf(out, EXLU_MAX_FILE_SIZE + 256,
                     "EXLU1\\n#MANIFEST\\n"
                     "{\\"format\\":\\"exlu/1\\",\\"kind\\":\\"%s\\","
                     "\\"type\\":\\"%s\\",\\"name\\":\\"%s\\","
                     "\\"author\\":\\"%s\\",\\"id\\":\\"%s\\","
                     "\\"api\\":%d,\\"modes\\":%d}\\n#LAYOUT\\n",
                     (e.manifest.kind == EXLU_KIND_THEME) ? "theme" : "app",
                     (e.manifest.type == EXLU_TYPE_SCRIPT) ? "script"
                                                           : "layout",
                     e.manifest.name, e.manifest.author, e.manifest.id,
                     e.manifest.api, e.manifest.modes);
    if (e.layout_len) {
        memcpy(out + p, e.layout, e.layout_len);
        p += (int)e.layout_len;
    }
    p += snprintf(out + p, EXLU_MAX_FILE_SIZE + 256 - p, "\\n#SCRIPT\\n");
    if (e.script_len) {
        memcpy(out + p, e.script, e.script_len);
        p += (int)e.script_len;
    }
    p += snprintf(out + p, EXLU_MAX_FILE_SIZE + 256 - p, "\\n#END\\n");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, out, (ssize_t)p);
    free(out);
    exlu_free(&e);
    return r;
}

static esp_err_t api_exlu_save(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char id[EXLU_NAME_MAX];
    if (!query(req, "id", id, sizeof(id))) {
        return send_err(req, "400 Bad Request", "E_ARG: id required");
    }
    for (const char *p = id; *p; ++p) {
        bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                  *p == '-' || *p == '_';
        if (!ok) {
            return send_err(req, "400 Bad Request", "E_ARG: bad id");
        }
    }
    char ow[8] = "";
    bool overwrite = query(req, "overwrite", ow, sizeof(ow)) &&
                     strcmp(ow, "1") == 0;

    size_t len = 0;
    char *body = read_body(req, EXLU_MAX_FILE_SIZE, &len);
    if (!body) return send_err(req, "400 Bad Request", "E_OPEN: body");
    exlu_t e;
    exlu_err_t err = exlu_parse(body, len, &e);
    free(body);
    if (err != EXLU_OK) {
        ESP_LOGW(TAG, "exlu save rejected (%s): %s", id, exlu_strerror(err));
        return send_err(req, "400 Bad Request", exlu_strerror(err));
    }
    err = exlu_save(id, &e, overwrite);
    exlu_free(&e);
    if (err != EXLU_OK) {
        ESP_LOGW(TAG, "exlu write failed (%s): %s", id, exlu_strerror(err));
        return send_err(req, "409 Conflict", exlu_strerror(err));
    }
    ESP_LOGI(TAG, "exlu saved: %s (%u bytes)", id, (unsigned)len);
    return send_json(req, "{\\"ok\\":true}");
}

static esp_err_t api_exlu_delete(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char id[EXLU_NAME_MAX];
    if (!query(req, "id", id, sizeof(id))) {
        return send_err(req, "400 Bad Request", "E_ARG: id required");
    }
    exlu_err_t err = exlu_remove(id);
    if (err != EXLU_OK) {
        return send_err(req, "404 Not Found", exlu_strerror(err));
    }
    return send_json(req, "{\\"ok\\":true}");
}

static esp_err_t api_exlu_apply(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char id[EXLU_NAME_MAX];
    if (!query(req, "id", id, sizeof(id))) {
        return send_err(req, "400 Bad Request", "E_ARG: id required");
    }
    exlu_t e;
    exlu_err_t err = exlu_load(id, &e);
    if (err != EXLU_OK) {
        return send_err(req, "404 Not Found", exlu_strerror(err));
    }
    if (e.manifest.kind != EXLU_KIND_THEME) {
        exlu_free(&e);
        return send_err(req, "501 Not Implemented",
                        "E_NOT_READY: app runtime not implemented yet");
    }
    err = exlu_apply(&e);
    exlu_free(&e);
    if (err != EXLU_OK) {
        return send_err(req, "400 Bad Request", exlu_strerror(err));
    }
    s_theme = UI_THEME_CUSTOM;
    ui_runtime_apply_theme();
    mark_change();
    web_cmd_t c = { .type = WEB_CMD_UI_RELOAD };
    web_cmd_push(&c);
    ESP_LOGI(TAG, "exlu theme applied: %s", id);
    return send_json(req, "{\\"ok\\":true}");
}

'''

t = t.replace(anchor, api + anchor, 1)

# /ui/extended/ -> index.html
old_asset = '''    if (strcmp(path, "/ui/") == 0) want = "/ui/index.html";'''
new_asset = '''    if (strcmp(path, "/ui/") == 0) want = "/ui/index.html";
    if (strcmp(path, "/ui/extended/") == 0) want = "/ui/extended/index.html";'''
assert old_asset in t
t = t.replace(old_asset, new_asset, 1)

# 라우트 등록
old_reg = '''    httpd_register_uri_handler(server, &del);
    ESP_LOGI(TAG, "%d assets, api ready", ui_editor_asset_count);'''
new_reg = '''    httpd_register_uri_handler(server, &del);

    static httpd_uri_t exlu_list_u = {
        .uri = "/api/exlu/list", .method = HTTP_GET, .handler = api_exlu_list,
    };
    static httpd_uri_t exlu_get_u = {
        .uri = "/api/exlu/get", .method = HTTP_GET, .handler = api_exlu_get,
    };
    static httpd_uri_t exlu_save_u = {
        .uri = "/api/exlu/save", .method = HTTP_POST, .handler = api_exlu_save,
    };
    static httpd_uri_t exlu_del_u = {
        .uri = "/api/exlu/delete", .method = HTTP_POST,
        .handler = api_exlu_delete,
    };
    static httpd_uri_t exlu_apply_u = {
        .uri = "/api/exlu/apply", .method = HTTP_POST,
        .handler = api_exlu_apply,
    };
    httpd_register_uri_handler(server, &exlu_list_u);
    httpd_register_uri_handler(server, &exlu_get_u);
    httpd_register_uri_handler(server, &exlu_save_u);
    httpd_register_uri_handler(server, &exlu_del_u);
    httpd_register_uri_handler(server, &exlu_apply_u);
    ESP_LOGI(TAG, "%d assets, api ready", ui_editor_asset_count);'''
assert old_reg in t
t = t.replace(old_reg, new_reg, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ui_web patched')
