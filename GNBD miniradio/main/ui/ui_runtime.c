/*
 * SPDX-License-Identifier: MIT
 * ui_runtime.c - parse/store/activate runtime UI layouts in LittleFS.
 *
 * Text format (tab separated, one widget per line):
 *   UIR1
 *   type x y w h modes fill stroke color font align bind chip max <TAB> text
 */
#include "ui_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"

#define UI_DIR    "/littlefs/ui"
#define UI_ACTIVE "/littlefs/ui/active.txt"

static const char *TAG = "ui_rt";

static uc_widget_t s_widgets[UI_RUNTIME_MAX_WIDGETS];
static int s_count;
static char *s_buf;
static char s_name[UI_RUNTIME_NAME_MAX + 1];
static bool s_enabled;

typedef struct {
    bool valid;
    int x, y, w, h, orient;
} ui_panel_t;
static ui_panel_t s_panels[2];
static uint16_t s_pal[UI_P_COUNT];
static bool s_pal_valid;

bool ui_runtime_valid_name(const char *name)
{
    if (!name || !name[0]) return false;
    size_t n = strlen(name);
    if (n > UI_RUNTIME_NAME_MAX) return false;
    if (name[0] == '.') return false;
    for (size_t i = 0; i < n; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool parse_num(const char *s, long *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

static bool parse_text(const char *text, size_t len, uc_widget_t *out,
                       int *out_count, char **out_buf,
                       uint16_t *pal, bool *pal_ok)
{
    if (!text || len == 0 || len > UI_RUNTIME_MAX_FILE) return false;
    if (memchr(text, '\0', len)) return false;
    if (pal_ok) *pal_ok = false;

    char *buf = (char *)malloc(len + 1);
    if (!buf) return false;
    memcpy(buf, text, len);
    buf[len] = '\0';

    int n = 0;
    char *p = buf;
    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = '\0'; p = nl + 1; } else { p += strlen(p); }
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r') line[--ll] = '\0';
        if (!ll || line[0] == '#') continue;
        if (strcmp(line, "UIR1") == 0) continue;
        if (strncmp(line, "PAL\t", 4) == 0) {
            char *save = NULL;
            char *tok = strtok_r(line + 4, "\t", &save);
            int k = 0;
            while (tok && k < UI_P_COUNT) {
                long v = 0;
                if (!parse_num(tok, &v) || v < 0 || v > 65535) {
                    free(buf); return false;
                }
                if (pal) pal[k] = (uint16_t)v;
                ++k;
                tok = strtok_r(NULL, "\t", &save);
            }
            if (k != UI_P_COUNT || tok != NULL) { free(buf); return false; }
            if (pal_ok) *pal_ok = true;
            continue;
        }
        if (n >= UI_RUNTIME_MAX_WIDGETS) { free(buf); return false; }

        char *f[20];   /* v2: 15필드 + scale + uid + events + text */
        char *cur = line;
        int tabs = 0;
        for (char *q = line; *q; ++q) if (*q == '\t') ++tabs;
        if (tabs != 14 && tabs != 15 && tabs != 17) { free(buf); return false; }
        for (int i = 0; i < tabs; ++i) {
            char *t = strchr(cur, '\t');
            if (!t) { free(buf); return false; }
            *t = '\0';
            f[i] = cur;
            cur = t + 1;
        }
        f[tabs] = cur;

        long v[18] = {0};
        int nnum = (tabs == 17) ? 15 : tabs;
        for (int i = 0; i < nnum; ++i) {
            if (!parse_num(f[i], &v[i])) { free(buf); return false; }
        }
        if (v[0] < 0 || v[0] > UC_BATT_ICON) { free(buf); return false; }
        if (v[1] < -32768 || v[1] > 32767 || v[2] < -32768 || v[2] > 32767) {
            free(buf); return false;
        }
        if (v[3] < 1 || v[3] > 512 || v[4] < 1 || v[4] > 512) {
            free(buf); return false;
        }
        if (v[5] < 0 || v[5] > 7) { free(buf); return false; }
        if (v[6] < 0 || v[6] > UC_NONE || v[7] < 0 || v[7] > UC_NONE ||
            v[8] < 0 || v[8] > UC_NONE) { free(buf); return false; }
        if (v[9] < 0 || v[9] > UC_LARGE) { free(buf); return false; }
        if (v[10] < 0 || v[10] > UC_AL_R) { free(buf); return false; }
        if (v[11] < 0 || v[11] > UC_B_DECODE_LEVEL) { free(buf); return false; }
        if (v[12] != 0 && v[12] != 1) { free(buf); return false; }
        if (v[13] < 0 || v[13] > 65535) { free(buf); return false; }
        int scale = 100;
        if (tabs == 15 || tabs == 17) {
            if (v[14] < 50 || v[14] > 200) { free(buf); return false; }
            scale = (int)v[14];
        }
        const char *ev = NULL;
        long uid = 0;
        const char *txt = f[tabs];
        if (tabs == 17) {
            if (!parse_num(f[15], &uid) || uid < 0 || uid > 65535) {
                free(buf); return false;
            }
            if (strlen(f[16]) >= 160) { free(buf); return false; }
            ev = f[16][0] ? f[16] : NULL;
            txt = f[17];
        }
        if (strlen(txt) >= 64) { free(buf); return false; }

        uc_widget_t *w = &out[n];
        memset(w, 0, sizeof(*w));
        w->type = (uint8_t)v[0];
        w->x = (int16_t)v[1];
        w->y = (int16_t)v[2];
        w->w = (int16_t)v[3];
        w->h = (int16_t)v[4];
        w->modes = (uint8_t)v[5];
        w->fill = (uint8_t)v[6];
        w->stroke = (uint8_t)v[7];
        w->color = (uint8_t)v[8];
        w->font = (uint8_t)v[9];
        w->align = (uint8_t)v[10];
        w->bind = (uint8_t)v[11];
        w->chip = (uint8_t)v[12];
        w->max = (uint16_t)v[13];
        w->scale = (uint8_t)scale;
        w->text = txt[0] ? txt : NULL;
        w->uid = (uint16_t)uid;
        w->events = ev;
        w->fn = NULL;
        ++n;
    }

    if (n == 0) { free(buf); return false; }
    *out_count = n;
    *out_buf = buf;
    return true;
}

static bool read_file(const char *path, char **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 131072) { fclose(f); return false; }
    char *buf = (char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return false;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)sz;
    return true;
}

static bool write_file_atomic(const char *path, const char *data, size_t len)
{
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    if (rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

static void path_of(const char *name, const char *ext, char *out, size_t n)
{
    snprintf(out, n, UI_DIR "/%s.%s", name, ext);
}

bool ui_runtime_save(const char *name, const char *text, size_t len, bool overwrite)
{
    if (!ui_runtime_valid_name(name)) return false;
    char path[96];
    path_of(name, "layout", path, sizeof(path));
    if (!overwrite && ui_runtime_has(name)) return false;

    uc_widget_t *tmp = (uc_widget_t *)calloc(UI_RUNTIME_MAX_WIDGETS, sizeof(*tmp));
    if (!tmp) return false;
    int n = 0;
    char *buf = NULL;
    uint16_t pal[UI_P_COUNT];
    bool pal_ok = false;
    bool valid = parse_text(text, len, tmp, &n, &buf, pal, &pal_ok);
    free(tmp);
    free(buf);
    if (!valid) return false;

    mkdir(UI_DIR, 0777);
    return write_file_atomic(path, text, len);
}

bool ui_runtime_has(const char *name)
{
    if (!ui_runtime_valid_name(name)) return false;
    char path[96];
    path_of(name, "layout", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool ui_runtime_load(const char *name)
{
    if (!ui_runtime_valid_name(name)) return false;
    char path[96];
    path_of(name, "layout", path, sizeof(path));
    char *text = NULL;
    size_t len = 0;
    if (!read_file(path, &text, &len)) return false;

    uc_widget_t *tmp = (uc_widget_t *)calloc(UI_RUNTIME_MAX_WIDGETS, sizeof(*tmp));
    if (!tmp) { free(text); return false; }
    int n = 0;
    char *buf = NULL;
    uint16_t pal[UI_P_COUNT];
    bool pal_ok = false;
    bool ok = parse_text(text, len, tmp, &n, &buf, pal, &pal_ok);
    free(text);
    if (!ok) { free(tmp); return false; }

    free(s_buf);
    s_buf = buf;
    memcpy(s_widgets, tmp, (size_t)n * sizeof(*tmp));
    s_count = n;
    s_pal_valid = pal_ok;
    if (pal_ok) {
        memcpy(s_pal, pal, sizeof(s_pal));
    }
    free(tmp);
    memset(s_panels, 0, sizeof(s_panels));
    for (int i = 0; i < n; ++i) {
        const uc_widget_t *w = &s_widgets[i];
        if (w->type != UC_MENU_GUIDE) continue;
        int slot = (w->bind == 1) ? 1 : 0;
        s_panels[slot].valid = true;
        s_panels[slot].x = w->x;
        s_panels[slot].y = w->y;
        s_panels[slot].w = w->w;
        s_panels[slot].h = w->h;
        s_panels[slot].orient = (w->align == 1) ? 1 : 0;
    }
    snprintf(s_name, sizeof(s_name), "%s", name);
    ESP_LOGI(TAG, "loaded %s: %d widgets", name, n);
    return true;
}

bool ui_runtime_activate(const char *name)
{
    if (!ui_runtime_load(name)) return false;
    char path[96];
    path_of(name, "layout", path, sizeof(path));
    (void)path;
    mkdir(UI_DIR, 0777);
    if (!write_file_atomic(UI_ACTIVE, name, strlen(name))) return false;
    s_enabled = true;
    return true;
}

bool ui_runtime_activate_active(void)
{
    char name[UI_RUNTIME_NAME_MAX + 1] = "";
    char *text = NULL;
    size_t len = 0;
    if (!read_file(UI_ACTIVE, &text, &len) || len > UI_RUNTIME_NAME_MAX) {
        free(text);
        return false;
    }
    memcpy(name, text, len);
    name[len] = '\0';
    free(text);
    while (len && (name[len - 1] == '\n' || name[len - 1] == '\r')) {
        name[--len] = '\0';
    }
    if (!ui_runtime_valid_name(name)) return false;
    return ui_runtime_activate(name);
}

bool ui_runtime_init(bool want_custom)
{
    mkdir(UI_DIR, 0777);
    s_enabled = false;
    bool loaded = ui_runtime_activate_active();
    if (!want_custom) {
        if (loaded) s_enabled = false;
        return true;
    }
    if (!loaded) {
        ESP_LOGW(TAG, "custom theme selected but no active UI");
        return false;
    }
    return true;
}

void ui_runtime_disable(void)
{
    s_enabled = false;
}

bool ui_runtime_enabled(void)
{
    return s_enabled && s_count > 0;
}

int ui_runtime_count(void)
{
    return s_count;
}

const uc_widget_t *ui_runtime_widgets(void)
{
    return s_widgets;
}

const char *ui_runtime_active_name(void)
{
    return s_name;
}

int ui_runtime_list(char names[][UI_RUNTIME_NAME_MAX + 1], int max)
{
    DIR *d = opendir(UI_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        size_t l = strlen(e->d_name);
        if (l <= 7 || strcmp(e->d_name + l - 7, ".layout") != 0) continue;
        char name[128];
        size_t nl = l - 7;
        if (nl > UI_RUNTIME_NAME_MAX) continue;
        memcpy(name, e->d_name, nl);
        name[nl] = '\0';
        if (!ui_runtime_valid_name(name)) continue;
        snprintf(names[n], UI_RUNTIME_NAME_MAX + 1, "%.*s",
                 UI_RUNTIME_NAME_MAX, name);
        ++n;
    }
    closedir(d);
    for (int i = 1; i < n; ++i) {
        char key[UI_RUNTIME_NAME_MAX + 1];
        snprintf(key, sizeof(key), "%s", names[i]);
        int j = i - 1;
        while (j >= 0 && strcmp(names[j], key) > 0) {
            snprintf(names[j + 1], sizeof(names[j + 1]), "%s", names[j]);
            --j;
        }
        snprintf(names[j + 1], sizeof(names[j + 1]), "%s", key);
    }
    return n;
}

bool ui_runtime_read(const char *name, const char *ext, char **out, size_t *len)
{
    if (!ui_runtime_valid_name(name) || !ext) return false;
    char path[96];
    path_of(name, ext, path, sizeof(path));
    return read_file(path, out, len);
}

bool ui_runtime_write(const char *name, const char *ext, const char *data, size_t len)
{
    if (!ui_runtime_valid_name(name) || !ext || !data ||
        len == 0 || len > 131072) return false;
    char path[96];
    path_of(name, ext, path, sizeof(path));
    mkdir(UI_DIR, 0777);
    return write_file_atomic(path, data, len);
}

bool ui_runtime_delete(const char *name)
{
    if (!ui_runtime_valid_name(name)) return false;
    char path[96];
    path_of(name, "layout", path, sizeof(path));
    if (remove(path) != 0) return false;
    path_of(name, "mrpack", path, sizeof(path));
    remove(path);
    if (strcmp(s_name, name) == 0) {
        free(s_buf);
        s_buf = NULL;
        s_count = 0;
        s_enabled = false;
        s_name[0] = '\0';
        memset(s_panels, 0, sizeof(s_panels));
        remove(UI_ACTIVE);
    }
    return true;
}

const uc_widget_t *ui_runtime_find(uint16_t uid)
{
    if (uid == 0) {
        return NULL;
    }
    for (int i = 0; i < s_count; ++i) {
        if (s_widgets[i].uid == uid) {
            return &s_widgets[i];
        }
    }
    return NULL;
}

bool ui_runtime_panel(bool settings, int *x, int *y, int *w, int *h, int *orient)
{
    if (!ui_runtime_enabled()) return false;
    const ui_panel_t *p = &s_panels[settings ? 1 : 0];
    if (!p->valid) return false;
    if (x) *x = p->x;
    if (y) *y = p->y;
    if (w) *w = p->w;
    if (h) *h = p->h;
    if (orient) *orient = p->orient;
    return true;
}

void ui_runtime_apply_theme(void)
{
    ui_theme_set(0);
    if (ui_runtime_enabled() && s_pal_valid) {
        memcpy(ui_pal, s_pal, sizeof(s_pal));
    }
}
