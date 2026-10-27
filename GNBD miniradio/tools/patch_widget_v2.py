# -*- coding: utf-8 -*-
"""런타임 위젯 v2: uid/events 필드 추가 (파서 + JS 생성기)."""
import io

# 1) uc_widget_t 확장
p = 'main/ui/ui_custom.h'
t = io.open(p, encoding='utf-8').read()
old = '    const char *text;\n    void (*fn)(int x, int y, int w, int h);   /* UC_CUSTOM */'
new = ('    const char *text;\n'
       '    uint16_t uid;       /* 위젯 고유 번호 (Lua 이벤트 바인딩용, 0=없음) */\n'
       '    const char *events; /* 이벤트 핸들러 목록: "click=w1_click,rotate=w1_rot" */\n'
       '    void (*fn)(int x, int y, int w, int h);   /* UC_CUSTOM */')
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) 파서: tabs 17 허용
p = 'main/ui/ui_runtime.c'
t = io.open(p, encoding='utf-8').read()

old = '        if (tabs != 14 && tabs != 15) { free(buf); return false; }'
new = '        if (tabs != 14 && tabs != 15 && tabs != 17) { free(buf); return false; }'
assert old in t
t = t.replace(old, new, 1)

old = '''        long v[15];
        for (int i = 0; i < tabs; ++i) {
            if (!parse_num(f[i], &v[i])) { free(buf); return false; }
        }'''
new = '''        long v[18] = {0};
        int nnum = (tabs == 17) ? 15 : tabs;
        for (int i = 0; i < nnum; ++i) {
            if (!parse_num(f[i], &v[i])) { free(buf); return false; }
        }'''
assert old in t
t = t.replace(old, new, 1)

old = '''        int scale = 100;
        if (tabs == 15) {
            if (v[14] < 50 || v[14] > 200) { free(buf); return false; }
            scale = (int)v[14];
        }
        if (strlen(f[tabs]) >= 64) { free(buf); return false; }'''
new = '''        int scale = 100;
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
        if (strlen(txt) >= 64) { free(buf); return false; }'''
assert old in t
t = t.replace(old, new, 1)

old = '''        w->text = f[tabs][0] ? f[tabs] : NULL;
        w->fn = NULL;'''
new = '''        w->text = txt[0] ? txt : NULL;
        w->uid = (uint16_t)uid;
        w->events = ev;
        w->fn = NULL;'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) JS 생성기: v2 라인 (scale + uid + events + text)
p = 'uiedt/js/core/runtime.js'
t = io.open(p, encoding='utf-8').read()
old = '''    return [
      t, Math.round(w.x), Math.round(w.y), Math.round(w.w), Math.round(w.h),
      mode,
      COLOR[w.fill] != null ? COLOR[w.fill] : 0,
      COLOR[w.stroke] != null ? COLOR[w.stroke] : 0,
      COLOR[w.color] != null ? COLOR[w.color] : 0,
      FONT[w.font || w.size] != null ? FONT[w.font || w.size] : 0,
      ALIGN[w.align] != null ? ALIGN[w.align] : 0,
      bind >= 0 ? bind : 0,
      w.chip ? 1 : 0,
      w.max || 0,
      Math.max(50, Math.min(200, Math.round(w.scale || 100))),
    ].join("\\t") + "\\t" + clean(w.text);'''
new = '''    const events = w.events && typeof w.events === "object"
      ? Object.keys(w.events)
          .filter((k) => w.events[k])
          .map((k) => k + "=" + w.events[k])
          .join(",")
      : "";
    return [
      t, Math.round(w.x), Math.round(w.y), Math.round(w.w), Math.round(w.h),
      mode,
      COLOR[w.fill] != null ? COLOR[w.fill] : 0,
      COLOR[w.stroke] != null ? COLOR[w.stroke] : 0,
      COLOR[w.color] != null ? COLOR[w.color] : 0,
      FONT[w.font || w.size] != null ? FONT[w.font || w.size] : 0,
      ALIGN[w.align] != null ? ALIGN[w.align] : 0,
      bind >= 0 ? bind : 0,
      w.chip ? 1 : 0,
      w.max || 0,
      Math.max(50, Math.min(200, Math.round(w.scale || 100))),
      w.uid || 0,
      events,
    ].join("\\t") + "\\t" + clean(w.text);'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('patched')
