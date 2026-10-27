/*
 * SPDX-License-Identifier: MIT
 * ui_web.c - UI editor assets + /api/ui storage endpoints.
 */
#include "ui_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "app.h"
#include "ui_runtime.h"
#include "ui_exlu.h"
#include "ui_exlu_app.h"
#include "ui_editor_assets.h"

static const char *TAG = "ui_web";

#define UI_JSON_MAX 1400
#define UI_LAYOUT_BODY_MAX 16384
#define UI_PROJECT_BODY_MAX 131072

static bool auth_ok(httpd_req_t *req)
{
    return web_authorized(req);
}

static bool query(httpd_req_t *req, const char *key, char *out, size_t n)
{
    char q[256];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    return httpd_query_key_value(q, key, out, n) == ESP_OK;
}

static char *read_body(httpd_req_t *req, size_t max, size_t *out_len)
{
    if (req->content_len <= 0 || req->content_len > max) return NULL;
    size_t len = (size_t)req->content_len;
    char *buf = (char *)malloc(len + 1);
    if (!buf) return NULL;
    size_t got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r <= 0) { free(buf); return NULL; }
        got += (size_t)r;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_err(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    return send_json(req, buf);
}

static esp_err_t api_list(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char names[32][UI_RUNTIME_NAME_MAX + 1];
    int n = ui_runtime_list(names, 32);
    char *out = (char *)malloc(UI_JSON_MAX);
    if (!out) return ESP_FAIL;
    int p = snprintf(out, UI_JSON_MAX, "{\"active\":\"%s\",\"files\":[",
                     ui_runtime_enabled() ? ui_runtime_active_name() : "");
    for (int i = 0; i < n && p < UI_JSON_MAX - 40; ++i) {
        p += snprintf(out + p, UI_JSON_MAX - p, "%s\"%s\"",
                      i ? "," : "", names[i]);
    }
    snprintf(out + p, UI_JSON_MAX - p, "]}");
    esp_err_t e = send_json(req, out);
    free(out);
    return e;
}

static esp_err_t api_layout_get(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char name[UI_RUNTIME_NAME_MAX + 1];
    if (!query(req, "name", name, sizeof(name))) {
        return send_err(req, "400 Bad Request", "name");
    }
    char *data = NULL;
    size_t len = 0;
    if (!ui_runtime_read(name, "layout", &data, &len)) {
        return send_err(req, "404 Not Found", "not found");
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, data, len);
    free(data);
    return e;
}

static esp_err_t api_save(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char name[UI_RUNTIME_NAME_MAX + 1];
    if (!query(req, "name", name, sizeof(name))) {
        return send_err(req, "400 Bad Request", "name");
    }
    if (!ui_runtime_valid_name(name)) {
        return send_err(req, "400 Bad Request", "bad name");
    }
    char ow[8] = "";
    bool overwrite = query(req, "overwrite", ow, sizeof(ow)) &&
                     strcmp(ow, "1") == 0;
    if (!overwrite && ui_runtime_has(name)) {
        return send_err(req, "409 Conflict", "exists");
    }
    if (!overwrite) {
        char names[32][UI_RUNTIME_NAME_MAX + 1];
        int n = ui_runtime_list(names, 32);
        for (int i = 0; i < n; ++i) {
            if (strcasecmp(names[i], name) == 0) {
                return send_err(req, "409 Conflict", "exists");
            }
        }
    }
    size_t len = 0;
    char *body = read_body(req, UI_LAYOUT_BODY_MAX, &len);
    if (!body) return send_err(req, "400 Bad Request", "body");
    bool ok = ui_runtime_save(name, body, len, true);
    free(body);
    if (!ok) return send_err(req, "400 Bad Request", "invalid layout");
    ESP_LOGI(TAG, "layout saved: %s (%u bytes)", name, (unsigned)len);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t api_project_get(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char name[UI_RUNTIME_NAME_MAX + 1];
    if (!query(req, "name", name, sizeof(name))) {
        return send_err(req, "400 Bad Request", "name");
    }
    char *data = NULL;
    size_t len = 0;
    if (!ui_runtime_read(name, "mrpack", &data, &len)) {
        return send_err(req, "404 Not Found", "not found");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, data, len);
    free(data);
    return e;
}

static esp_err_t api_project_post(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char name[UI_RUNTIME_NAME_MAX + 1];
    if (!query(req, "name", name, sizeof(name))) {
        return send_err(req, "400 Bad Request", "name");
    }
    if (!ui_runtime_valid_name(name)) {
        return send_err(req, "400 Bad Request", "bad name");
    }
    size_t len = 0;
    char *body = read_body(req, UI_PROJECT_BODY_MAX, &len);
    if (!body) return send_err(req, "400 Bad Request", "body");
    bool ok = ui_runtime_write(name, "mrpack", body, len);
    free(body);
    if (!ok) return send_err(req, "500 Internal Server Error", "write");
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t api_activate(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char name[UI_RUNTIME_NAME_MAX + 1];
    if (!query(req, "name", name, sizeof(name))) {
        return send_err(req, "400 Bad Request", "name");
    }
    if (!ui_runtime_activate(name)) {
        return send_err(req, "404 Not Found", "not found");
    }
    s_theme = UI_THEME_CUSTOM;
    ui_runtime_apply_theme();
    mark_change();
    web_cmd_t c = { .type = WEB_CMD_UI_RELOAD };
    web_cmd_push(&c);
    ESP_LOGI(TAG, "activated: %s", name);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t api_delete(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char name[UI_RUNTIME_NAME_MAX + 1];
    if (!query(req, "name", name, sizeof(name))) {
        return send_err(req, "400 Bad Request", "name");
    }
    bool was_active = strcmp(ui_runtime_active_name(), name) == 0;
    if (!ui_runtime_delete(name)) {
        return send_err(req, "404 Not Found", "not found");
    }
    if (was_active) {
        s_theme = 0;
        ui_runtime_disable();
        ui_theme_set(0);
        mark_change();
        web_cmd_t c = { .type = WEB_CMD_UI_RELOAD };
        web_cmd_push(&c);
    }
    return send_json(req, "{\"ok\":true}");
}

/* ---------------- EXLU (테마/확장 앱) ---------------- */

static esp_err_t api_exlu_list(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    exlu_manifest_t m[16];
    int n = exlu_list(m, 16);
    char *out = (char *)malloc(2048);
    if (!out) return ESP_FAIL;
    int p = snprintf(out, 2048, "{\"files\":[");
    for (int i = 0; i < n && p < 1900; ++i) {
        p += snprintf(out + p, 2048 - p,
                      "%s{\"id\":\"%s\",\"name\":\"%s\","
                      "\"author\":\"%s\",\"kind\":\"%s\","
                      "\"type\":\"%s\"}",
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
    char *out = NULL;
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
    char *out2 = (char *)malloc(EXLU_MAX_FILE_SIZE + 256);
    if (!out) {
        exlu_free(&e);
        return ESP_FAIL;
    }
    int p = snprintf(out, EXLU_MAX_FILE_SIZE + 256,
                     "EXLU1\n#MANIFEST\n"
                     "{\"format\":\"exlu/1\",\"kind\":\"%s\","
                     "\"type\":\"%s\",\"name\":\"%s\","
                     "\"author\":\"%s\",\"id\":\"%s\","
                     "\"api\":%d,\"modes\":%d}\n#LAYOUT\n",
                     (e.manifest.kind == EXLU_KIND_THEME) ? "theme" : "app",
                     (e.manifest.type == EXLU_TYPE_SCRIPT) ? "script"
                                                           : "layout",
                     e.manifest.name, e.manifest.author, e.manifest.id,
                     e.manifest.api, e.manifest.modes);
    if (e.layout_len) {
        memcpy(out + p, e.layout, e.layout_len);
        p += (int)e.layout_len;
    }
    p += snprintf(out + p, EXLU_MAX_FILE_SIZE + 256 - p, "\n#SCRIPT\n");
    if (e.script_len) {
        memcpy(out + p, e.script, e.script_len);
        p += (int)e.script_len;
    }
    p += snprintf(out + p, EXLU_MAX_FILE_SIZE + 256 - p, "\n#END\n");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r2 = httpd_resp_send(req, out2, (ssize_t)p);
    free(out2);
    exlu_free(&e);
    return r2;
#endif
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
    exlu_err_t err = exlu_store_raw(id, body, len, overwrite);
    free(body);
    if (err != EXLU_OK) {
        ESP_LOGW(TAG, "exlu write failed (%s): %s", id, exlu_strerror(err));
        return send_err(req, "409 Conflict", exlu_strerror(err));
    }
    ESP_LOGI(TAG, "exlu saved: %s (%u bytes)", id, (unsigned)len);
    return send_json(req, "{\"ok\":true}");
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
    return send_json(req, "{\"ok\":true}");
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
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    if (!auth_ok(req)) return web_require_auth(req);
    char path[128];
    const char *q = strchr(req->uri, '?');
    size_t n = q ? (size_t)(q - req->uri) : strlen(req->uri);
    if (n >= sizeof(path)) n = sizeof(path) - 1;
    memcpy(path, req->uri, n);
    path[n] = '\0';

    if (strcmp(path, "/ui") == 0) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/ui/");
        return httpd_resp_send(req, "", 0);
    }
    const char *want = path;
    if (strcmp(path, "/ui/") == 0) want = "/ui/index.html";
    if (strcmp(path, "/ui/extended") == 0) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/ui/extended/");
        return httpd_resp_send(req, "", 0);
    }
    if (strcmp(path, "/ui/extended/") == 0) want = "/ui/extended/index.html";
    for (int i = 0; i < ui_editor_asset_count; ++i) {
        const ui_asset_t *a = &ui_editor_assets[i];
        if (strcmp(a->path, want) != 0) continue;
        httpd_resp_set_type(req, a->mime);
        httpd_resp_set_hdr(req, "Cache-Control",
                           "no-store, no-cache, must-revalidate, max-age=0");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");
        return httpd_resp_send(req, a->data, a->len);
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
}

esp_err_t ui_web_register(httpd_handle_t server)
{
    static httpd_uri_t ui_root = {
        .uri = "/ui", .method = HTTP_GET, .handler = asset_handler,
    };
    static httpd_uri_t ui_index = {
        .uri = "/ui/", .method = HTTP_GET, .handler = asset_handler,
    };
    httpd_register_uri_handler(server, &ui_root);
    httpd_register_uri_handler(server, &ui_index);
#if EXLU_ENABLE
    static httpd_uri_t ext_root = {
        .uri = "/ui/extended", .method = HTTP_GET, .handler = asset_handler,
    };
    static httpd_uri_t ext_index = {
        .uri = "/ui/extended/", .method = HTTP_GET, .handler = asset_handler,
    };
    httpd_register_uri_handler(server, &ext_root);
    httpd_register_uri_handler(server, &ext_index);
#endif

    static httpd_uri_t assets[64];
    for (int i = 0; i < ui_editor_asset_count && i < 64; ++i) {
        assets[i].uri = ui_editor_assets[i].path;
        assets[i].method = HTTP_GET;
        assets[i].handler = asset_handler;
        httpd_register_uri_handler(server, &assets[i]);
    }

    static httpd_uri_t list = {
        .uri = "/api/ui/list", .method = HTTP_GET, .handler = api_list,
    };
    static httpd_uri_t layout = {
        .uri = "/api/ui/layout", .method = HTTP_GET, .handler = api_layout_get,
    };
    static httpd_uri_t save = {
        .uri = "/api/ui/save", .method = HTTP_POST, .handler = api_save,
    };
    static httpd_uri_t project_get = {
        .uri = "/api/ui/project", .method = HTTP_GET, .handler = api_project_get,
    };
    static httpd_uri_t project_post = {
        .uri = "/api/ui/project", .method = HTTP_POST, .handler = api_project_post,
    };
    static httpd_uri_t activate = {
        .uri = "/api/ui/activate", .method = HTTP_POST, .handler = api_activate,
    };
    static httpd_uri_t del = {
        .uri = "/api/ui/delete", .method = HTTP_POST, .handler = api_delete,
    };
    httpd_register_uri_handler(server, &list);
    httpd_register_uri_handler(server, &layout);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &project_get);
    httpd_register_uri_handler(server, &project_post);
    httpd_register_uri_handler(server, &activate);
    httpd_register_uri_handler(server, &del);

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
#if EXLU_ENABLE
    httpd_register_uri_handler(server, &exlu_list_u);
    httpd_register_uri_handler(server, &exlu_get_u);
    httpd_register_uri_handler(server, &exlu_save_u);
    httpd_register_uri_handler(server, &exlu_del_u);
    httpd_register_uri_handler(server, &exlu_apply_u);
#else
    (void)exlu_list_u; (void)exlu_get_u; (void)exlu_save_u;
    (void)exlu_del_u; (void)exlu_apply_u;
#endif
    ESP_LOGI(TAG, "%d assets, api ready", ui_editor_asset_count);
    return ESP_OK;
}
