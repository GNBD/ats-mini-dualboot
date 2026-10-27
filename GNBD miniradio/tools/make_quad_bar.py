# -*- coding: utf-8 -*-
"""batt_icon: 사각형 공용 게이지 + 단자 옵션."""
import io

# 1) 펌웨어 렌더: 사각형 + bind/max 사용 + noterm 옵션
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()

start = t.find('static void draw_batt_icon(const uc_widget_t *w, const ui_state_t *s)')
assert start > 0
end = t.find('\n}\n', start)
assert end > start
new_fn = '''static void draw_batt_icon(const uc_widget_t *w, const ui_state_t *s)
{
    int max = w->max ? (int)w->max : 100;
    int val = num_bind(w->bind, s);
    if (val < 0) { val = 0; }
    if (val > max) { val = max; }

    /* 사각 게이지: 테두리 + 채움 (단자는 text 에 noterm 이 없을 때만) */
    display_fill_rect(w->x, w->y, w->w, w->h, col(UC_PANEL));
    display_draw_rect(w->x, w->y, w->w, w->h, col(UC_LINE));
    if (!(w->text && strstr(w->text, "noterm"))) {
        display_fill_rect((int16_t)(w->x + w->w), (int16_t)(w->y + w->h / 3),
                          2, (int16_t)(w->h / 3 > 0 ? w->h / 3 : 1),
                          col(UC_LINE));
    }
    int fill_w = (w->w - 2) * val / (max > 0 ? max : 1);
    if (fill_w > 0) {
        display_fill_rect((int16_t)(w->x + 1), (int16_t)(w->y + 1),
                          (uint16_t)fill_w, (uint16_t)(w->h - 2),
                          col(w->color));
    }
}'''
t = t[:start] + new_fn + t[end + 2:]
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) 에디터 미리보기: 사각 + bind/max
p = 'uiedt/js/render.js'
t = io.open(p, encoding='utf-8').read()
start = t.find('      case "batt_icon": {')
assert start > 0
end = t.find('      case "decode": {', start)
assert end > start
new_case = '''      case "batt_icon": {
        const max = wg.max || 100;
        const val = Math.max(0, Math.min(max, Number(s[wg.bind]) || 0));
        ctx.fillStyle = c(wg.fill || "panel");
        ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        ctx.strokeStyle = c(wg.stroke || "line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        if (!(wg.text && String(wg.text).indexOf("noterm") >= 0)) {
          ctx.fillStyle = c(wg.stroke || "line");
          ctx.fillRect(wg.x + wg.w, wg.y + Math.round(wg.h / 3), 2,
                       Math.max(1, Math.round(wg.h / 3)));
        }
        ctx.fillStyle = c(wg.color || "ok");
        ctx.fillRect(wg.x + 1, wg.y + 1, (wg.w - 2) * val / (max || 1), wg.h - 2);
        break;
      }
'''
t = t[:start] + new_case + t[end:]
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 3) properties: batt_icon 은 공용 게이지로 (Bind/Max/Color/옵션)
p = 'uiedt/js/properties.js'
t = io.open(p, encoding='utf-8').read()
old = '''    } else if (w.t === "decode") {'''
new = '''    } else if (w.t === "batt_icon") {
      body.appendChild(this.row("Bind", this.select(bindOptions(), w.bind || "batt",
        (v) => { w.bind = v || undefined; upd(); })));
      body.appendChild(this.row("Max", this.input(w.max || 100,
        (v) => { w.max = +v; upd(); }, "number")));
      body.appendChild(this.row("Color", this.select(colorOptions(), w.color || "ok",
        (v) => { RecentColors.add(v); w.color = v; upd(); })));
      body.appendChild(this.row("Options", this.input(w.text || "",
        (v) => { w.text = v || undefined; upd(); })));
    } else if (w.t === "decode") {'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 4) catalog: 사각 바 설명/옵션
p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('{ id: "batt_icon", cat: "UI", name: "Battery icon", sub: "배터리 아이콘 (% 채움)"',
              '{ id: "batt_icon", cat: "UI", name: "Quad bar (battery)", sub: "사각 게이지 · 공용 바"')
t = t.replace('W({ t: "batt_icon", x: 292, y: 5, w: 28, h: 11, bind: "batt",',
              'W({ t: "batt_icon", x: 292, y: 5, w: 28, h: 11, bind: "batt", max: 100,')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
