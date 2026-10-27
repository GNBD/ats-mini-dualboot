# -*- coding: utf-8 -*-
"""캔버스 리사이즈 핸들 판정을 화면 픽셀 기준으로 보정."""
import io

p = 'uiedt/js/editor.js'
t = io.open(p, encoding='utf-8').read()

old = '''  /* 선택 위젯의 8방향 핸들 판정 (모서리/변) */
  handleAt(w, p) {
    const t = 6;                     /* 논리 px 판정 반경 */
    const xs = [[w.x, "w"], [w.x + w.w / 2, ""], [w.x + w.w, "e"]];
    const ys = [[w.y, "n"], [w.y + w.h / 2, ""], [w.y + w.h, "s"]];
    let hx = "", vy = "";
    for (const [v, tag] of xs) if (Math.abs(p.x - v) <= t) hx = tag;
    for (const [v, tag] of ys) if (Math.abs(p.y - v) <= t) vy = tag;
    if (!hx && !vy) return null;
    if (p.x < w.x - t || p.x > w.x + w.w + t) return null;
    if (p.y < w.y - t || p.y > w.y + w.h + t) return null;
    return vy + hx;
  },'''
new = '''  /* 선택 위젯의 8방향 핸들 판정 (모서리/변)
     화면 픽셀 기준으로 반경을 환산해 확대/축소 어느 쪽에서도 잡기 쉽게 한다. */
  handleAt(w, p) {
    const zoom = this.zoom || 1;
    const t = Math.max(3, 9 / zoom);   /* 화면 약 9px, 최소 3 논리 px */
    if (p.x < w.x - t || p.x > w.x + w.w + t) return null;
    if (p.y < w.y - t || p.y > w.y + w.h + t) return null;

    const dl = Math.abs(p.x - w.x);
    const dr = Math.abs(p.x - (w.x + w.w));
    const dt = Math.abs(p.y - w.y);
    const db = Math.abs(p.y - (w.y + w.h));

    let hx = "", hd = Infinity;
    if (dl <= t && dl < hd) { hx = "w"; hd = dl; }
    if (dr <= t && dr < hd) { hx = "e"; hd = dr; }
    let vy = "", vd = Infinity;
    if (dt <= t && dt < vd) { vy = "n"; vd = dt; }
    if (db <= t && db < vd) { vy = "s"; vd = db; }

    if (!hx && !vy) return null;
    return vy + hx;
  },'''

assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
