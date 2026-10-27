# -*- coding: utf-8 -*-
"""render.js batt_icon/decode 케이스의 ctx 참조 오류 수정."""
import io
import re

p = 'uiedt/js/render.js'
t = io.open(p, encoding='utf-8').read()

# batt_icon 블록 교체
start = t.find('      case "batt_icon": {')
assert start > 0
end = t.find('      case "decode": {', start)
assert end > start
batt = '''      case "batt_icon": {
        const pct = Math.max(0, Math.min(100, Number(s.batt) || 0));
        ctx.fillStyle = c(wg.fill || "panel");
        ctx.fillRect(wg.x, wg.y, 26, 11);
        ctx.strokeStyle = c(wg.stroke || "line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, 25, 10);
        ctx.fillStyle = c(wg.stroke || "line");
        ctx.fillRect(wg.x + 26, wg.y + 3, 2, 5);
        ctx.fillStyle = pct <= 15 ? c("warn")
                      : pct <= 35 ? c("accent") : c(wg.color || "ok");
        ctx.fillRect(wg.x + 2, wg.y + 2, 20 * pct / 100, 7);
        break;
      }
'''
t = t[:start] + batt + t[end:]

# decode 블록 교체 (this.ctx / col / setTransform 제거)
start = t.find('      case "decode": {')
assert start > 0
end = t.find('      case "wifi":', start)
assert end > start
decode = '''      case "decode": {
        ctx.fillStyle = c(wg.fill || "panel");
        ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        ctx.strokeStyle = c(wg.stroke || "line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        ctx.fillStyle = c(wg.color || "accent");
        ctx.font = font(wg.font, wg.size);
        ctx.textBaseline = "top";
        ctx.textAlign = "left";
        ctx.fillText("DECODE CW  GPIO 4", wg.x + 4, wg.y + 2);
        ctx.fillStyle = c("ok");
        ctx.textAlign = "right";
        ctx.fillText("RUN", wg.x + wg.w - 4, wg.y + 2);
        ctx.textAlign = "left";
        ctx.fillStyle = c("muted");
        ctx.fillText("SIG", wg.x + 4, wg.y + 14);
        ctx.fillStyle = c("track");
        ctx.fillRect(wg.x + 26, wg.y + 16, wg.w - 30, 4);
        ctx.fillStyle = c("ok");
        ctx.fillRect(wg.x + 26, wg.y + 16, (wg.w - 30) * 0.6, 4);
        ctx.fillStyle = c("fg");
        ctx.fillText("....", wg.x + 4, wg.y + 26);
        break;
      }
'''
t = t[:start] + decode + t[end:]

io.open(p, 'w', encoding='utf-8', newline='\n').write(t)
print('ok')
