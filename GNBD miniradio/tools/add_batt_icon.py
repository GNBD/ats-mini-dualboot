# -*- coding: utf-8 -*-
"""배터리 아이콘 위젯 추가 (런타임+에디터) + 헤더 배치."""
import io

# 1) ui_custom.h: 타입 추가
p = 'main/ui/ui_custom.h'
t = io.open(p, encoding='utf-8').read()
t = t.replace('UC_MENU_GUIDE, UC_WIFI, UC_DECODE\n} uc_type_t;',
              'UC_MENU_GUIDE, UC_WIFI, UC_DECODE, UC_BATT_ICON\n} uc_type_t;')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) ui_runtime.c: 타입 범위
p = 'main/ui/ui_runtime.c'
t = io.open(p, encoding='utf-8').read()
t = t.replace('if (v[0] < 0 || v[0] > UC_DECODE)',
              'if (v[0] < 0 || v[0] > UC_BATT_ICON)')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) ui_custom.c: 렌더
p = 'main/ui/ui_custom.c'
t = io.open(p, encoding='utf-8').read()
old = '''        case UC_DECODE:
            draw_decode_panel(w);
            break;'''
new = '''        case UC_DECODE:
            draw_decode_panel(w);
            break;
        case UC_BATT_ICON:
            draw_batt_icon(w, s);
            break;'''
assert old in t
t = t.replace(old, new, 1)

anchor = 'static void draw_decode_panel(const uc_widget_t *w)'
impl = '''/* 배터리 아이콘: 펌웨어 battery_pill 과 동일한 모양 */
static void draw_batt_icon(const uc_widget_t *w, const ui_state_t *s)
{
    int pct = s->have_battery ? (int)s->battery.percent : 0;
    uint16_t c = col(UC_OK);
    if (pct <= 15) {
        c = col(UC_WARN);
    } else if (pct <= 35) {
        c = col(UC_ACCENT);
    }
    display_fill_round_rect(w->x, w->y, 26, 11, 3, col(UC_CELL));
    display_draw_round_rect(w->x, w->y, 26, 11, 3, col(UC_LINE));
    display_fill_rect((int16_t)(w->x + 26), (int16_t)(w->y + 3), 2, 5,
                      col(UC_LINE));
    int fill = (pct * 20) / 100;
    if (fill > 0) {
        display_fill_round_rect((int16_t)(w->x + 2), (int16_t)(w->y + 2),
                                (uint16_t)fill, 7, 2, c);
    }
}

'''
assert anchor in t
t = t.replace(anchor, impl + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 4) runtime.js TYPE
p = 'uiedt/js/core/runtime.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('wifi: 11, decode: 12', 'wifi: 11, decode: 12, batt_icon: 13')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 5) render.js case
p = 'uiedt/js/render.js'
t = io.open(p, encoding='utf-8').read()
anchor2 = '      case "decode": {'
impl2 = '''      case "batt_icon": {
        const c = this.ctx;
        const pct = Math.max(0, Math.min(100, Number(s.batt) || 0));
        c.setTransform(2, 0, 0, 2, 0, 0);
        c.fillStyle = col(w.fill, "cell");
        c.fillRect(w.x, w.y, 26, 11);
        c.strokeStyle = col(w.stroke, "line");
        c.strokeRect(w.x + .5, w.y + .5, 25, 10);
        c.fillRect(w.x + 26, w.y + 3, 2, 5);
        c.fillStyle = pct <= 15 ? col(w.color, "warn")
                    : pct <= 35 ? col(w.color, "accent") : col(w.color, "ok");
        c.fillRect(w.x + 2, w.y + 2, 20 * pct / 100, 7);
        c.setTransform(1, 0, 0, 1, 0, 0);
        break;
      }
'''
assert anchor2 in t
t = t.replace(anchor2, impl2 + anchor2, 1)
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 6) catalog: headfull 배터리 bar -> batt_icon, 좌표 292,5
p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
old3 = 'W({ t: "bar", x: 292, y: 7, w: 26, h: 8, bind: "batt", max: 100, color: "ok" }),'
new3 = 'W({ t: "batt_icon", x: 292, y: 5, w: 28, h: 11, bind: "batt", fill: "cell", stroke: "line", color: "ok" }),'
assert old3 in t
t = t.replace(old3, new3, 1)
# 전압을 배터리 바로 왼쪽(끝 286)으로 유지
t = t.replace('W({ t: "text", x: 246, y: 6, w: 40, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" })',
              'W({ t: "text", x: 242, y: 6, w: 42, h: 10, bind: "battv", size: "tiny", color: "muted", align: "r" })')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
