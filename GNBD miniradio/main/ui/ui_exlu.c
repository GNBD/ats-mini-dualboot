/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * EXLU container parser / validator / littlefs storage.
 *
 * This source file is licensed under the MIT License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "esp_log.h"
#include "ui_exlu.h"
#include "ui_runtime.h"

#define EXLU_DIR "/littlefs/ui"
#define EXLU_SUFFIX ".exlu"

static const char *TAG = "exlu";

const char *exlu_strerror(exlu_err_t err)
{
    switch (err) {
    case EXLU_OK:            return "OK";
    case EXLU_ERR_ARG:       return "E_ARG: invalid argument";
    case EXLU_ERR_OPEN:      return "E_OPEN: file read failed";
    case EXLU_ERR_SIZE:      return "E_SIZE: file too large";
    case EXLU_ERR_MAGIC:     return "E_MAGIC: not an EXLU1 file";
    case EXLU_ERR_NO_MANIFEST: return "E_NO_MANIFEST: #MANIFEST missing";
    case EXLU_ERR_MANIFEST:  return "E_MANIFEST: bad or missing field";
    case EXLU_ERR_FORMAT:    return "E_FORMAT: unsupported format";
    case EXLU_ERR_API:       return "E_API: unsupported api version";
    case EXLU_ERR_KIND:      return "E_KIND: kind must be theme|app";
    case EXLU_ERR_TYPE:      return "E_TYPE: type must be layout|script";
    case EXLU_ERR_COMBO:     return "E_COMBO: theme cannot carry a script";
    case EXLU_ERR_NO_LAYOUT: return "E_NO_LAYOUT: layout section missing";
    case EXLU_ERR_SCRIPT:    return "E_SCRIPT: script too large";
    case EXLU_ERR_LAYOUT:    return "E_LAYOUT: layout rejected by ui_runtime";
    case EXLU_ERR_EXISTS:    return "E_EXISTS: file already exists";
    case EXLU_ERR_NOT_FOUND: return "E_NOT_FOUND: no such exlu";
    case EXLU_ERR_MEM:       return "E_MEM: out of memory";
    default:                 return "E_UNKNOWN";
    }
}

/* ---------------- minimal JSON field access ---------------- */

static const char *json_find(const char *json, const char *key)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return NULL;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return NULL;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return p;
}

static bool json_str(const char *json, const char *key, char *out, size_t n)
{
    const char *p = json_find(json, key);
    if (!p || *p != '"') {
        return false;
    }
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) {
            ++p;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return *p == '"';
}

static bool json_int(const char *json, const char *key, int *out)
{
    const char *p = json_find(json, key);
    if (!p) {
        return false;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = (int)v;
    return true;
}

/* ---------------- section helpers ---------------- */

static const char *find_line(const char *text, const char *tag)
{
    const char *p = text;
    size_t n = strlen(tag);
    while (p && *p) {
        if (strncmp(p, tag, n) == 0 && (p[n] == '\n' || p[n] == '\r' ||
                                        p[n] == '\0')) {
            return p;
        }
        p = strchr(p, '\n');
        if (p) {
            ++p;
        }
    }
    return NULL;
}

static char *dup_n(const char *p, size_t len)
{
    char *s = (char *)malloc(len + 1);
    if (!s) {
        return NULL;
    }
    memcpy(s, p, len);
    s[len] = '\0';
    return s;
}

void exlu_free(exlu_t *e)
{
    if (!e) {
        return;
    }
    free(e->layout);
    free(e->script);
    e->layout = NULL;
    e->script = NULL;
    e->layout_len = 0;
    e->script_len = 0;
}

/* ---------------- parse ---------------- */

exlu_err_t exlu_parse(const char *text, size_t len, exlu_t *out)
{
    if (!text || !out || len == 0) {
        return EXLU_ERR_ARG;
    }
    if (len > EXLU_MAX_FILE_SIZE) {
        return EXLU_ERR_SIZE;
    }
    memset(out, 0, sizeof(*out));

    if (strncmp(text, "EXLU1", 5) != 0) {
        return EXLU_ERR_MAGIC;
    }

    const char *pm = find_line(text, "#MANIFEST");
    if (!pm) {
        return EXLU_ERR_NO_MANIFEST;
    }
    const char *json = strchr(pm, '\n');
    if (!json) {
        return EXLU_ERR_MANIFEST;
    }
    ++json;

    char fmt[16] = {0};
    char kind[16] = {0};
    char type[16] = {0};
    if (!json_str(json, "format", fmt, sizeof(fmt))) {
        return EXLU_ERR_MANIFEST;
    }
    if (strcmp(fmt, "exlu/1") != 0) {
        return EXLU_ERR_FORMAT;
    }
    if (!json_str(json, "kind", kind, sizeof(kind))) {
        return EXLU_ERR_KIND;
    }
    if (strcmp(kind, "theme") == 0) {
        out->manifest.kind = EXLU_KIND_THEME;
    } else if (strcmp(kind, "app") == 0) {
        out->manifest.kind = EXLU_KIND_APP;
    } else {
        return EXLU_ERR_KIND;
    }

    if (json_str(json, "type", type, sizeof(type))) {
        if (strcmp(type, "layout") == 0) {
            out->manifest.type = EXLU_TYPE_LAYOUT;
        } else if (strcmp(type, "script") == 0) {
            out->manifest.type = EXLU_TYPE_SCRIPT;
        } else {
            return EXLU_ERR_TYPE;
        }
    } else {
        out->manifest.type = EXLU_TYPE_LAYOUT;
    }

    if (!json_str(json, "name", out->manifest.name,
                  sizeof(out->manifest.name)) ||
        out->manifest.name[0] == '\0') {
        return EXLU_ERR_MANIFEST;
    }
    if (!json_str(json, "author", out->manifest.author,
                  sizeof(out->manifest.author))) {
        out->manifest.author[0] = '\0';
    }
    if (!json_str(json, "id", out->manifest.id, sizeof(out->manifest.id)) ||
        out->manifest.id[0] == '\0') {
        snprintf(out->manifest.id, sizeof(out->manifest.id), "%s",
                 out->manifest.name);
    }
    if (!json_int(json, "api", &out->manifest.api)) {
        out->manifest.api = 1;
    }
    if (out->manifest.api != 1) {
        return EXLU_ERR_API;
    }
    int modes = 0;
    out->manifest.modes = json_int(json, "modes", &modes)
                          ? (uint8_t)(modes & 0x07) : 0x07;

    /* theme 은 script 금지 */
    const char *ps = find_line(text, "#SCRIPT");
    const char *pl = find_line(text, "#LAYOUT");
    const char *pe = find_line(text, "#END");

    if (pl) {
        const char *start = strchr(pl, '\n');
        start = start ? start + 1 : pl + strlen(pl);
        const char *stop = ps ? ps : (pe ? pe : text + len);
        size_t n = (size_t)(stop - start);
        while (n > 0 && (start[n - 1] == '\n' || start[n - 1] == '\r')) {
            --n;
        }
        if (n > 0) {
            out->layout = dup_n(start, n);
            if (!out->layout) {
                exlu_free(out);
                return EXLU_ERR_MEM;
            }
            out->layout_len = n;
        }
    }

    if (ps) {
        const char *start = strchr(ps, '\n');
        start = start ? start + 1 : ps + strlen(ps);
        const char *stop = pe ? pe : text + len;
        size_t n = (size_t)(stop - start);
        while (n > 0 && (start[n - 1] == '\n' || start[n - 1] == '\r')) {
            --n;
        }
        if (n > EXLU_MAX_SCRIPT_SIZE) {
            exlu_free(out);
            return EXLU_ERR_SCRIPT;
        }
        if (n > 0) {
            out->script = dup_n(start, n);
            if (!out->script) {
                exlu_free(out);
                return EXLU_ERR_MEM;
            }
            out->script_len = n;
            out->manifest.has_script = true;
        }
    }

    if (out->manifest.kind == EXLU_KIND_THEME && out->manifest.has_script) {
        exlu_free(out);
        return EXLU_ERR_COMBO;
    }
    if (out->manifest.type == EXLU_TYPE_LAYOUT && !out->layout) {
        exlu_free(out);
        return EXLU_ERR_NO_LAYOUT;
    }
    if (out->manifest.type == EXLU_TYPE_SCRIPT && !out->script) {
        exlu_free(out);
        return EXLU_ERR_SCRIPT;
    }
    return EXLU_OK;
}

/* ---------------- littlefs ---------------- */

static void path_of(const char *id, char *out, size_t n)
{
    snprintf(out, n, EXLU_DIR "/%s" EXLU_SUFFIX, id);
}

static char *read_all(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > EXLU_MAX_FILE_SIZE) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    *out_len = (size_t)sz;
    return buf;
}

exlu_err_t exlu_load(const char *id, exlu_t *out)
{
    if (!id || !out) {
        return EXLU_ERR_ARG;
    }
    char path[96];
    path_of(id, path, sizeof(path));
    size_t len = 0;
    char *buf = read_all(path, &len);
    if (!buf) {
        return EXLU_ERR_OPEN;
    }
    exlu_err_t err = exlu_parse(buf, len, out);
    free(buf);
    if (err != EXLU_OK) {
        ESP_LOGW(TAG, "load %s failed: %s", id, exlu_strerror(err));
    }
    return err;
}

exlu_err_t exlu_save(const char *id, const exlu_t *e, bool overwrite)
{
    if (!id || !e) {
        return EXLU_ERR_ARG;
    }
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

    size_t need = 256 + (e->layout_len ? e->layout_len : 0) +
                  (e->script_len ? e->script_len : 0);
    char *txt = (char *)malloc(need);
    if (!txt) {
        return EXLU_ERR_MEM;
    }
    int n = snprintf(txt, need,
                     "EXLU1\n#MANIFEST\n"
                     "{\"format\":\"exlu/1\",\"kind\":\"%s\",\"type\":\"%s\","
                     "\"name\":\"%s\",\"author\":\"%s\",\"id\":\"%s\","
                     "\"api\":%d,\"modes\":%d}\n"
                     "#LAYOUT\n",
                     (e->manifest.kind == EXLU_KIND_THEME) ? "theme" : "app",
                     (e->manifest.type == EXLU_TYPE_SCRIPT) ? "script"
                                                            : "layout",
                     e->manifest.name, e->manifest.author, id,
                     e->manifest.api, e->manifest.modes);
    if (n < 0 || (size_t)n >= need) {
        free(txt);
        return EXLU_ERR_MEM;
    }
    size_t pos = (size_t)n;
    if (e->layout_len) {
        memcpy(txt + pos, e->layout, e->layout_len);
        pos += e->layout_len;
    }
    txt[pos++] = '\n';
    n = snprintf(txt + pos, need - pos, "#SCRIPT\n");
    pos += (size_t)n;
    if (e->script_len) {
        memcpy(txt + pos, e->script, e->script_len);
        pos += e->script_len;
    }
    n = snprintf(txt + pos, need - pos, "\n#END\n");
    pos += (size_t)n;

    const char *tmp = "/littlefs/ui/.exlu.tmp";
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        free(txt);
        return EXLU_ERR_LAYOUT;
    }
    size_t wr = fwrite(txt, 1, pos, f);
    fclose(f);
    free(txt);
    if (wr != pos) {
        remove(tmp);
        return EXLU_ERR_LAYOUT;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        return EXLU_ERR_LAYOUT;
    }
    return EXLU_OK;
}

exlu_err_t exlu_store_raw(const char *id, const char *text, size_t len,
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

exlu_err_t exlu_remove(const char *id)
{
    if (!id) {
        return EXLU_ERR_ARG;
    }
    char path[96];
    path_of(id, path, sizeof(path));
    if (remove(path) != 0) {
        return EXLU_ERR_NOT_FOUND;
    }
    return EXLU_OK;
}

int exlu_list_ids(char ids[][EXLU_NAME_MAX], int max)
{
    if (!ids || max <= 0) {
        return 0;
    }
    DIR *d = opendir(EXLU_DIR);
    if (!d) {
        return 0;
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < max) {
        size_t l = strlen(ent->d_name);
        size_t sfx = strlen(EXLU_SUFFIX);
        if (l <= sfx || strcmp(ent->d_name + l - sfx, EXLU_SUFFIX) != 0) {
            continue;
        }
        size_t idl = l - sfx;
        if (idl >= EXLU_NAME_MAX) {
            continue;
        }
        memcpy(ids[n], ent->d_name, idl);
        ids[n][idl] = '\0';
        ++n;
    }
    closedir(d);
    return n;
}

int exlu_list(exlu_manifest_t *out, int max)
{
    if (!out || max <= 0) {
        return 0;
    }
    char ids[16][EXLU_NAME_MAX];
    int n = exlu_list_ids(ids, 16);
    int used = 0;
    for (int i = 0; i < n && used < max; ++i) {
        exlu_t e;
        if (exlu_load(ids[i], &e) == EXLU_OK) {
            out[used++] = e.manifest;
            exlu_free(&e);
        }
    }
    return used;
}

exlu_err_t exlu_apply(const exlu_t *e)
{
    if (!e) {
        return EXLU_ERR_ARG;
    }
    if (e->manifest.type == EXLU_TYPE_LAYOUT) {
        if (!e->layout || e->layout_len == 0) {
            return EXLU_ERR_NO_LAYOUT;
        }
        if (!ui_runtime_save(e->manifest.id, e->layout, e->layout_len, true)) {
            return EXLU_ERR_LAYOUT;
        }
        if (!ui_runtime_activate(e->manifest.id)) {
            return EXLU_ERR_LAYOUT;
        }
    }
    return EXLU_OK;
}

/* 부팅 시 테마 EXLU 자동 적용 (테마 모드에서만 호출).
   실패는 로그로 남기고 내장 UI로 계속 진행한다. */
void exlu_boot_apply_theme(void)
{
    char ids[8][EXLU_NAME_MAX];
    int n = exlu_list_ids(ids, 8);
    for (int i = 0; i < n; ++i) {
        exlu_t e;
        if (exlu_load(ids[i], &e) != EXLU_OK) {
            continue;
        }
        if (e.manifest.kind == EXLU_KIND_THEME) {
            exlu_err_t err = exlu_apply(&e);
            if (err == EXLU_OK) {
                ESP_LOGI(TAG, "theme applied: %s", ids[i]);
            } else {
                ESP_LOGW(TAG, "theme %s rejected: %s", ids[i],
                         exlu_strerror(err));
            }
            exlu_free(&e);
            return;
        }
        exlu_free(&e);
    }
}
