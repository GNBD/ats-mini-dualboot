# -*- coding: utf-8 -*-
"""모듈 추가 시 최상위 레이어 + RDS/SIG 70:30 기본값 + 맨앞으로 버튼."""
import io

# 1) catalog: RDS 70% / SIG 30%
p = 'uiedt/js/catalog.js'
t = io.open(p, encoding='utf-8').read()
t = t.replace('W({ t: "rds", x: 6, y: 122, w: 308, h: 46 })',
              'W({ t: "rds", x: 6, y: 122, w: 216, h: 46 })')
t = t.replace('W({ t: "sig", x: 6, y: 122, w: 308, h: 46 })',
              'W({ t: "sig", x: 226, y: 122, w: 88, h: 46 })')
io.open(p, 'w', encoding='utf-8', newline='\n').write(t)

# 2) editor.js: 새 블록/템플릿은 항상 최상위 z
p = 'uiedt/js/editor.js'
t = io.open(p, encoding='utf-8').read()

old = '''  addModule(mod, cx, cy) {'''
new = '''  topZ() {
    let z = 0;
    for (const w of this.project.items) {
      const v = (w.z == null ? 0 : w.z);
      if (v > z) z = v;
    }
    return z + 1;
  },

  addModule(mod, cx, cy) {'''
assert old in t
t = t.replace(old, new, 1)

old2 = '''    for (const w of items) {
      w.x = Math.round(w.x + dx);
      w.y = Math.round(w.y + dy);
      w.origin = this.originFor(modMeta, w);
      this.ensureUid(w);
      this.project.items.push(w);
    }'''
new2 = '''    for (const w of items) {
      w.x = Math.round(w.x + dx);
      w.y = Math.round(w.y + dy);
      w.origin = this.originFor(modMeta, w);
      this.ensureUid(w);
      /* 새 모듈은 항상 최상위 레이어 (겹칠 때 아래에 깔리지 않도록) */
      w.z = this.topZ();
      this.project.items.push(w);
    }'''
assert old2 in t
t = t.replace(old2, new2, 1)

old3 = '''        const made = mod.make();
        const meta = { name: mod.name, count: made.length };
        for (const w of made) {
          w.origin = this.originFor(meta, w);
          this.ensureUid(w);
          this.project.items.push(w);
        }'''
new3 = '''        const made = mod.make();
        const meta = { name: mod.name, count: made.length };
        for (const w of made) {
          w.origin = this.originFor(meta, w);
          this.ensureUid(w);
          w.z = this.topZ();
          this.project.items.push(w);
        }'''
assert old3 in t
t = t.replace(old3, new3, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 3) properties: 맨 앞으로/맨 뒤로 버튼
p = 'uiedt/js/properties.js'
t = io.open(p, encoding='utf-8').read()
old4 = '''    {
      const others = ED.project.items.filter((x) => x !== w && x.uid);'''
new4 = '''    {
      const front = document.createElement("button");
      front.className = "btn"; front.textContent = T("맨 앞으로");
      front.style.padding = "3px 7px";
      front.onclick = () => { w.z = ED.topZ(); upd(); Props.render(); if (window.LayerView) LayerView.render(); };
      const back = document.createElement("button");
      back.className = "btn"; back.textContent = T("맨 뒤로");
      back.style.padding = "3px 7px";
      back.onclick = () => { w.z = -1; upd(); Props.render(); if (window.LayerView) LayerView.render(); };
      body.appendChild(this.row("Order", front, back));
    }
    {
      const others = ED.project.items.filter((x) => x !== w && x.uid);'''
assert old4 in t
t = t.replace(old4, new4, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
