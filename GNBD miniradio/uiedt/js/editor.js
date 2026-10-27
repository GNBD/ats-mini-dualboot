/*
 * SPDX-License-Identifier: MIT
 * Canvas editor: select / drag / resize / properties / undo.
 */
const ED = {
  project: { bg: "bg", items: [] },
  sel: -1,
  selSet: [],
  uidSeq: 1,
  preview: false,
  dirty: false,
  snap: 4,
  useGrid: true,
  pmode: 0,
  drag: null,
  ghost: null,
  undoStack: [],
  redoStack: [],
  canvas: null,
  ctx: null,
  sim: null,

  init(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.ctx.imageSmoothingEnabled = false;

    canvas.addEventListener("pointerdown", (e) => this.onDown(e));
    canvas.addEventListener("pointermove", (e) => this.onMove(e));
    canvas.addEventListener("pointerup", () => this.onUp());
    canvas.addEventListener("pointercancel", () => this.onUp());
    canvas.addEventListener("pointerleave", () => { if (window.Tip) Tip.hide(); });
    canvas.addEventListener("dragover", (e) => this.onDragOver(e));
    canvas.addEventListener("dragleave", () => { this.ghost = null; this.draw(); });
    canvas.addEventListener("drop", (e) => this.onDrop(e));

    window.addEventListener("keydown", (e) => {
      if (e.target.matches("input,select,textarea")) return;
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") { e.preventDefault(); if (e.shiftKey) this.redo(); else this.undo(); }
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y") { e.preventDefault(); this.redo(); }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "d") { e.preventDefault(); this.dup(); }
      if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); this.del(); }
      const n = e.shiftKey ? 8 : 1;
      if (e.key === "ArrowLeft") { e.preventDefault(); this.nudge(-n, 0); }
      if (e.key === "ArrowRight") { e.preventDefault(); this.nudge(n, 0); }
      if (e.key === "ArrowUp") { e.preventDefault(); this.nudge(0, -n); }
      if (e.key === "ArrowDown") { e.preventDefault(); this.nudge(0, n); }
      if (e.key === "Escape") { this.setSel(-1); }
    });

    this.sim = JSON.parse(JSON.stringify(MOCK));
    this.sim.hist = [];
    for (let i = 0; i < 40; i++) {
      this.sim.hist.push({ rssi: 30 + Math.round(Math.sin(i / 4) * 6), snr: 8 + Math.round(Math.cos(i / 5) * 3) });
    }
    this.draw();
  },

  /* ---------- geometry ---------- */
  toLogical(e) {
    const r = this.canvas.getBoundingClientRect();
    return { x: (e.clientX - r.left) * 320 / r.width, y: (e.clientY - r.top) * 170 / r.height };
  },
  hit(p) {
    for (const { w, i } of this.stackOrder()) {
      if (w.hidden) continue;
      if (p.x >= w.x && p.x <= w.x + w.w && p.y >= w.y && p.y <= w.y + w.h) return i;
    }
    return -1;
  },
  cs(p) { return this.useGrid ? Math.round(p / this.snap) * this.snap : Math.round(p); },

  /* ---------- layers ---------- */
  /* 모듈이 여러 위젯을 만들 때: "모듈 · 역할" 로 구분 */
  partOf(w) {
    if (w.t === "text") {
      if (w.bind) return w.bind;
      if (w.text) return String(w.text).slice(0, 8);
    }
    if (w.t === "rect") return "box";
    if (w.t === "line") return "line";
    return w.t;
  },

  originFor(mod, w) {
    return mod.count > 1 ? mod.name + " · " + this.partOf(w) : mod.name;
  },

  ensureUid(w) {
    if (!w.uid) w.uid = this.uidSeq++;
    return w.uid;
  },

  /* ---------- per-mode layouts ---------- */
  layoutKeys: ["FM", "AM", "SSB"],

  cloneItems(items) {
    return JSON.parse(JSON.stringify(items || []));
  },

  activeLayout() {
    return this.project.activeMode || "FM";
  },

  ensureLayouts() {
    if (!this.project.layouts) {
      const cur = this.project.items || [];
      this.project.layouts = {
        FM: cur,
        AM: this.cloneItems(cur),
        SSB: this.cloneItems(cur),
      };
      this.project.activeMode = "FM";
    }
    for (const k of this.layoutKeys) {
      if (!Array.isArray(this.project.layouts[k])) this.project.layouts[k] = [];
    }
    return this.project.layouts;
  },

  setPmode(idx) {
    idx = Math.max(0, Math.min(2, idx | 0));
    const key = this.layoutKeys[idx];
    if (!this.project.layouts && key === this.activeLayout()) {
      this.pmode = idx;
      return;
    }
    const L = this.ensureLayouts();
    const old = this.activeLayout();
    L[old] = this.project.items || [];
    this.pmode = idx;
    this.project.activeMode = key;
    this.project.items = L[key];
    this.setSel(-1);
  },

  syncLayout() {
    if (!this.project.layouts) return;
    this.project.layouts[this.activeLayout()] = this.project.items || [];
  },

  /* 표시 이름: 사용자 지정 > 모듈 출처 > 타입 (+중복 번호) */
  baseOf(w) {
    if (w.origin) return w.origin;
    const T = { rect: "Panel", text: "Text", bar: "Bar", line: "Line",
      freq: "Frequency", rsq: "RSSI/SNR", rds: "Info card", sig: "SIG graph",
      bandmap: "Band map", menu: "Panel guide" };
    const base = T[w.t] || w.t;
    return (w.t === "text" && w.bind) ? base + " " + w.bind : base;
  },

  displayName(w) {
    if (w.name) return w.name;
    const base = this.baseOf(w);
    const same = this.project.items.filter((x) => !x.name && this.baseOf(x) === base);
    return same.length > 1 ? base + " " + (same.indexOf(w) + 1) : base;
  },

  layerOf(w, depth, pool) {
    depth = depth || 0;
    if (w.parent && depth < 5) {
      const arr = pool || this.project.items;
      const p = arr.find((x) => x.uid === w.parent);
      if (p && p !== w) return this.layerOf(p, depth + 1, arr);
    }
    return w.z == null ? 0 : w.z;
  },

  /* 그리기 순서: 낮은 레이어 → 높은 레이어, 같으면 삽입 순서 */
  sortedFor(items) {
    return (items || [])
      .map((w, i) => ({ w, i, z: this.layerOf(w, 0, items) }))
      .sort((a, b) => (a.z - b.z) || (a.i - b.i))
      .map((x) => x.w);
  },

  sortedItems() {
    return this.sortedFor(this.project.items);
  },

  /* 레이어 패널 순서: 위 = 뒤, 아래 = 앞 (마지막이 최상위) */
  listOrder() {
    return this.project.items
      .map((w, i) => ({ w, i, z: this.layerOf(w) }))
      .sort((a, b) => (a.z - b.z) || (a.i - b.i));
  },

  /* 클릭 판정: 앞(높은 레이어)부터 */
  stackOrder() {
    return this.project.items
      .map((w, i) => ({ w, i, z: this.layerOf(w) }))
      .sort((a, b) => (b.z - a.z) || (b.i - a.i));
  },

  /* ---------- pointer ---------- */
  /* ---------- alignment guides ---------- */
  /* 다른 위젯/화면 중심과의 정렬을 계산해 dx/dy 스냅 + 가이드 라인 반환 */
  snapAlign(box, ignore) {
    const tol = 4;
    const others = this.project.items.filter((o, i) => i !== ignore && !o.hidden);
    const vt = [0, 160, 320];
    const ht = [0, 85, 170];
    for (const o of others) {
      vt.push(o.x, o.x + o.w / 2, o.x + o.w);
      ht.push(o.y, o.y + o.h / 2, o.y + o.h);
    }
    const mineX = [box.x, box.x + box.w / 2, box.x + box.w];
    const mineY = [box.y, box.y + box.h / 2, box.y + box.h];
    let dx = 0, dy = 0, bx = tol + 1, by = tol + 1;
    let gx = null, gy = null;
    for (const m of mineX) {
      for (const t of vt) {
        const d = t - m;
        if (Math.abs(d) <= tol && Math.abs(d) < Math.abs(bx)) { bx = d; gx = t; }
      }
    }
    for (const m of mineY) {
      for (const t of ht) {
        const d = t - m;
        if (Math.abs(d) <= tol && Math.abs(d) < Math.abs(by)) { by = d; gy = t; }
      }
    }
    if (gx != null) dx = Math.round(bx);
    if (gy != null) dy = Math.round(by);
    const mx2 = [box.x + dx, box.x + dx + box.w / 2, box.x + dx + box.w];
    const my2 = [box.y + dy, box.y + dy + box.h / 2, box.y + dy + box.h];
    const v = [];
    const h = [];
    for (const t of vt) if (mx2.some((m) => Math.abs(m - t) < 0.6) && v.indexOf(t) < 0) v.push(t);
    for (const t of ht) if (my2.some((m) => Math.abs(m - t) < 0.6) && h.indexOf(t) < 0) h.push(t);
    return { dx, dy, v, h };
  },

  onDown(e) {
    if (this.preview) return;
    const p = this.toLogical(e);
    const i = this.hit(p);
    if (i < 0) { this.setSel(-1); return; }
    this.setSel(i, e.shiftKey);
    if (this.sel < 0) return;
    const w = this.project.items[i];
    const handle = this.handleAt(w, p);
    this.snapshot();
    this.drag = {
      i, mode: handle || "move", px: p.x, py: p.y,
      ox: w.x, oy: w.y, ow: w.w, oh: w.h,
    };
    this.canvas.setPointerCapture(e.pointerId);
  },

  /* 선택 위젯의 8방향 핸들 판정 (모서리/변)
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
  },

  cursorFor(mode) {
    if (!mode || mode === "move") return "move";
    if (mode === "n" || mode === "s") return "ns-resize";
    if (mode === "e" || mode === "w") return "ew-resize";
    if (mode === "nw" || mode === "se") return "nwse-resize";
    return "nesw-resize";
  },

  onMove(e) {
    if (!this.drag) {
      if (!this.preview) {
        this.hoverTip(e);
        const p = this.toLogical(e);
        const w = this.project.items[this.sel];
        const h = w ? this.handleAt(w, p) : null;
        this.canvas.style.cursor = h ? this.cursorFor(h) : "default";
      }
      return;
    }
    const p = this.toLogical(e);
    const w = this.project.items[this.drag.i];
    const dx = p.x - this.drag.px, dy = p.y - this.drag.py;
    if (this.drag.mode === "move") {
      let nx = this.clamp(this.cs(this.drag.ox + dx), 0, 320 - w.w);
      let ny = this.clamp(this.cs(this.drag.oy + dy), 0, 170 - w.h);
      const g = this.snapAlign({ x: nx, y: ny, w: w.w, h: w.h }, this.drag.i);
      nx = this.clamp(nx + g.dx, 0, 320 - w.w);
      ny = this.clamp(ny + g.dy, 0, 170 - w.h);
      w.x = nx; w.y = ny;
      this.guides = { v: g.v, h: g.h };
    } else {
      const m = this.drag.mode;
      let x = this.drag.ox, y = this.drag.oy;
      let ww = this.drag.ow, hh = this.drag.oh;
      if (m.indexOf("e") >= 0) ww = this.cs(this.drag.ox + this.drag.ow + dx) - x;
      if (m.indexOf("w") >= 0) {
        const right = this.drag.ox + this.drag.ow;
        x = this.clamp(this.cs(this.drag.ox + dx), 0, right - 8);
        ww = right - x;
      }
      if (m.indexOf("s") >= 0) hh = this.cs(this.drag.oy + this.drag.oh + dy) - y;
      if (m.indexOf("n") >= 0) {
        const bottom = this.drag.oy + this.drag.oh;
        y = this.clamp(this.cs(this.drag.oy + dy), 0, bottom - 4);
        hh = bottom - y;
      }
      ww = this.clamp(ww, 8, 320 - x);
      hh = this.clamp(hh, 4, 170 - y);
      w.x = x; w.y = y; w.w = ww; w.h = hh;
    }
    this.draw();
    Props.sync();
  },

  onUp() { this.drag = null; this.guides = null; this.draw(); },

  /* ---------- drop preview ---------- */
  onDragOver(e) {
    if (this.preview || !this.dragPayload) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = "copy";
    const p = this.toLogical(e);
    this.ghost = this.ghostFor(this.dragPayload, p.x, p.y);
    this.draw();
    const hud = document.getElementById("hud");
    if (hud && this.ghost) {
      hud.textContent = "drop  " + Math.round(this.ghost.x) + "," +
        Math.round(this.ghost.y) + "  " + this.ghost.w + "×" + this.ghost.h;
    }
  },

  ghostFor(payload, cx, cy) {
    if (payload.kind !== "module") return null;
    const mod = MODULES.find((m) => m.id === payload.id);
    if (!mod) return null;
    const items = mod.make();
    let bx = Infinity;
    let by = Infinity;
    let bw = -Infinity;
    let bh = -Infinity;
    for (const w of items) {
      bx = Math.min(bx, w.x);
      by = Math.min(by, w.y);
      bw = Math.max(bw, w.x + w.w);
      bh = Math.max(bh, w.y + w.h);
    }
    bw -= bx;
    bh -= by;
    let dx = Math.round((cx == null ? 160 : cx) - bw / 2 - bx);
    let dy = Math.round((cy == null ? 85 : cy) - bh / 2 - by);
    dx = this.clamp(dx, -bx, 320 - (bx + bw));
    dy = this.clamp(dy, -by, 170 - (by + bh));
    return {
      items: items.map((w) => Object.assign({}, w, { x: w.x + dx, y: w.y + dy })),
      x: bx + dx, y: by + dy, w: bw, h: bh,
    };
  },

  drawGhost(mask) {
    if (!this.ghost) return;
    const c = this.ctx;
    c.save();
    c.setTransform(2, 0, 0, 2, 0, 0);
    c.globalAlpha = 0.45;
    for (const w of this.ghost.items) {
      const mm = w.modes == null ? 7 : w.modes;
      if (mask && (mm & mask) === 0) continue;
      Render.widget(c, w, this.sim);
    }
    c.globalAlpha = 1;
    c.strokeStyle = "#0072de";
    c.setLineDash([4, 3]);
    c.strokeRect(this.ghost.x + .5, this.ghost.y + .5, this.ghost.w - 1, this.ghost.h - 1);
    c.setLineDash([]);
    c.restore();
  },

  hoverTip(e) {
    const i = this.hit(this.toLogical(e));
    if (i === this.hoverIdx) return;
    this.hoverIdx = i;
    if (this.hoverTimer) { clearTimeout(this.hoverTimer); this.hoverTimer = null; }
    if (i < 0) { Tip.hide(); return; }
    const w = this.project.items[i];
    const cx = e.clientX, cy = e.clientY;
    this.hoverTimer = setTimeout(() => {
      Tip.text(ED.displayName(w),
               `${w.x},${w.y}  ${w.w}×${w.h}`,
               { left: cx, right: cx, top: cy, bottom: cy });
    }, 520);
  },

  /* ---------- pointer drag (palette → canvas) ---------- */
  beginModuleDrag(mod, ev) {
    this.endPointerDrag();
    this.dragPayload = { kind: "module", id: mod.id };
    this.pointerDrag = {
      mod, active: false,
      sx: ev.clientX, sy: ev.clientY, x: ev.clientX, y: ev.clientY,
    };
    if (window.Tip) Tip.hide();
    this._pdMove = (e) => this.onPointerDragMove(e);
    this._pdUp = (e) => this.onPointerDragUp(e);
    window.addEventListener("pointermove", this._pdMove);
    window.addEventListener("pointerup", this._pdUp);
    window.addEventListener("pointercancel", this._pdUp);
  },

  onPointerDragMove(e) {
    const d = this.pointerDrag;
    if (!d) return;
    d.x = e.clientX; d.y = e.clientY;
    if (!d.active) {
      if (Math.abs(d.x - d.sx) + Math.abs(d.y - d.sy) < 5) return;
      d.active = true;
      document.body.classList.add("dragging");
    }
    if (this._pdPending) return;
    this._pdPending = requestAnimationFrame(() => {
      this._pdPending = 0;
      this.paintPointerDrag();
    });
  },

  dragLabel() {
    let el = document.getElementById("dragLabel");
    if (!el) {
      el = document.createElement("div");
      el.id = "dragLabel";
      el.className = "draglabel";
      el.hidden = true;
      document.body.appendChild(el);
    }
    return el;
  },

  paintPointerDrag() {
    const d = this.pointerDrag;
    if (!d || !d.active) return;
    const r = this.canvas.getBoundingClientRect();
    const inside = d.x >= r.left && d.x <= r.right &&
                   d.y >= r.top && d.y <= r.bottom;
    const lbl = this.dragLabel();
    if (inside) {
      const p = { x: (d.x - r.left) * 320 / r.width,
                  y: (d.y - r.top) * 170 / r.height };
      this.ghost = this.ghostFor(this.dragPayload, p.x, p.y);
      if (this.ghost) {
        const g = this.snapAlign(
          { x: this.ghost.x, y: this.ghost.y, w: this.ghost.w, h: this.ghost.h }, -1);
        if (g.dx || g.dy) {
          this.ghost.x += g.dx;
          this.ghost.y += g.dy;
          for (const it of this.ghost.items) { it.x += g.dx; it.y += g.dy; }
        }
        this.guides = { v: g.v, h: g.h };
      }
      lbl.hidden = true;
      this.draw();
      const hud = document.getElementById("hud");
      if (hud && this.ghost) {
        hud.textContent = "drop  " + Math.round(this.ghost.x) + "," +
          Math.round(this.ghost.y) + "  " + this.ghost.w + "\u00d7" + this.ghost.h;
      }
    } else {
      this.ghost = null;
      lbl.hidden = false;
      lbl.textContent = T(d.mod.name);
      lbl.style.left = (d.x + 14) + "px";
      lbl.style.top = (d.y + 16) + "px";
      this.draw();
    }
  },

  onPointerDragUp(e) {
    const d = this.pointerDrag;
    const active = !!(d && d.active);
    this.endPointerDrag();
    this.ghost = null;
    if (!active) { this.draw(); return; }
    const r = this.canvas.getBoundingClientRect();
    if (e.clientX >= r.left && e.clientX <= r.right &&
        e.clientY >= r.top && e.clientY <= r.bottom) {
      const p = { x: (e.clientX - r.left) * 320 / r.width,
                  y: (e.clientY - r.top) * 170 / r.height };
      this.addModule(d.mod, p.x, p.y);
    } else {
      this.draw();
    }
    if (typeof updateHud === "function") updateHud();
  },

  endPointerDrag() {
    if (this._pdMove) window.removeEventListener("pointermove", this._pdMove);
    if (this._pdUp) window.removeEventListener("pointerup", this._pdUp);
    if (this._pdUp) window.removeEventListener("pointercancel", this._pdUp);
    this._pdMove = this._pdUp = null;
    if (this._pdPending) { cancelAnimationFrame(this._pdPending); this._pdPending = 0; }
    this.pointerDrag = null;
    this.dragPayload = null;
    this.guides = null;
    const lbl = document.getElementById("dragLabel");
    if (lbl) lbl.hidden = true;
    document.body.classList.remove("dragging");
  },

  onDrop(e) {
    if (this.preview) return;
    e.preventDefault();
    this.ghost = null;
    this.dragPayload = null;
    const hud = document.getElementById("hud");
    if (hud && typeof updateHud === "function") updateHud();
    const id = e.dataTransfer.getData("text/module");
    const mod = MODULES.find((m) => m.id === id);
    if (!mod) return;
    const p = this.toLogical(e);
    this.addModule(mod, p.x, p.y);
  },

  clamp(v, a, b) { return Math.max(a, Math.min(b, v)); },

  /* ---------- model ---------- */
  topZ() {
    let z = 0;
    for (const w of this.project.items) {
      const v = (w.z == null ? 0 : w.z);
      if (v > z) z = v;
    }
    return z + 1;
  },

  addModule(mod, cx, cy) {
    const items = mod.make();
    let bx = Infinity, by = Infinity, bw = -Infinity, bh = -Infinity;
    for (const w of items) {
      bx = Math.min(bx, w.x); by = Math.min(by, w.y);
      bw = Math.max(bw, w.x + w.w); bh = Math.max(bh, w.y + w.h);
    }
    bw -= bx; bh -= by;
    let dx = Math.round((cx == null ? 160 : cx) - bw / 2 - bx);
    let dy = Math.round((cy == null ? 85 : cy) - bh / 2 - by);
    /* 모듈 전체를 한 번에 클램프: 낙하 위치와 고스트가 정확히 일치 */
    dx = this.clamp(dx, -bx, 320 - (bx + bw));
    dy = this.clamp(dy, -by, 170 - (by + bh));
    this.snapshot();
    const modMeta = { name: mod.name, count: items.length };
    for (const w of items) {
      w.x = Math.round(w.x + dx);
      w.y = Math.round(w.y + dy);
      w.origin = this.originFor(modMeta, w);
      this.ensureUid(w);
      /* 새 모듈은 항상 최상위 레이어 (겹칠 때 아래에 깔리지 않도록) */
      w.z = this.topZ();
      this.project.items.push(w);
    }
    this.setSel(this.project.items.length - 1);
    this.draw();
  },

  applyTemplate(tpl) {
    this.snapshot();
    if (tpl.id === "tpl-default") {
      this.project.palette = JSON.parse(JSON.stringify(PALETTE));
    }
    this.project.items = [];
    for (const id of tpl.ids) {
      const mod = MODULES.find((m) => m.id === id);
      if (mod) {
        const made = mod.make();
        const meta = { name: mod.name, count: made.length };
        for (const w of made) {
          w.origin = this.originFor(meta, w);
          this.ensureUid(w);
          w.z = this.topZ();
          this.project.items.push(w);
        }
      }
    }
    if (window.Colors) Colors.render();
    this.setSel(-1);
    this.draw();
  },

  setSel(i, add) {
    if (i < 0) {
      this.sel = -1;
      this.selSet = [];
    } else if (add) {
      const k = this.selSet.indexOf(i);
      if (k >= 0) this.selSet.splice(k, 1);
      else this.selSet.push(i);
      this.sel = this.selSet.length ? this.selSet[this.selSet.length - 1] : -1;
    } else {
      this.sel = i;
      this.selSet = [i];
    }
    const info = document.getElementById("selInfo");
    if (info) info.textContent = TP(this.selSet.length + "개 선택", this.selSet.length + " selected");
    this.draw();
    Props.render();
    if (window.LayerView) LayerView.render();
  },

  del() {
    if (!this.selSet.length) return;
    this.snapshot();
    const idx = [...this.selSet].sort((a, b) => b - a);
    for (const i of idx) this.project.items.splice(i, 1);
    this.setSel(-1);
  },

  dup() {
    if (!this.selSet.length) return;
    this.snapshot();
    const out = [];
    for (const i of [...this.selSet].sort((a, b) => a - b)) {
      const c = JSON.parse(JSON.stringify(this.project.items[i]));
      c.x = this.clamp(c.x + 8, 0, 320 - c.w);
      c.y = this.clamp(c.y + 8, 0, 170 - c.h);
      c.uid = 0;
      this.ensureUid(c);
      this.project.items.push(c);
      out.push(this.project.items.length - 1);
    }
    this.sel = out[out.length - 1];
    this.selSet = out;
    this.draw();
    Props.render();
    if (window.LayerView) LayerView.render();
  },

  /* ---------- alignment ---------- */
  align(kind) {
    const idx = this.selSet.length ? [...this.selSet] : (this.sel >= 0 ? [this.sel] : []);
    if (!idx.length) return;
    const items = idx.map((i) => this.project.items[i]).filter(Boolean);
    if (!items.length) return;
    this.snapshot();

    const one = items.length === 1;
    const minX = Math.min(...items.map((w) => w.x));
    const maxX = Math.max(...items.map((w) => w.x + w.w));
    const minY = Math.min(...items.map((w) => w.y));
    const maxY = Math.max(...items.map((w) => w.y + w.h));
    const ref = this.project.items[this.sel] || items[0];

    for (const w of items) {
      switch (kind) {
        case "left":    w.x = one ? 0 : minX; break;
        case "right":   w.x = one ? 320 - w.w : maxX - w.w; break;
        case "hcenter": w.x = Math.round(((one ? 0 : minX) + (one ? 320 : maxX) - w.w) / 2); break;
        case "top":     w.y = one ? 0 : minY; break;
        case "bottom":  w.y = one ? 170 - w.h : maxY - w.h; break;
        case "vcenter": w.y = Math.round(((one ? 0 : minY) + (one ? 170 : maxY) - w.h) / 2); break;
        case "width":   w.w = ref.w; break;
        case "height":  w.h = ref.h; break;
      }
    }

    if (kind === "disth" && items.length > 1) {
      const sorted = [...items].sort((a, b) => a.x - b.x);
      const total = sorted.reduce((s, w) => s + w.w, 0);
      const first = sorted[0], last = sorted[sorted.length - 1];
      const span = (last.x + last.w) - first.x;
      const gap = (span - total) / (sorted.length - 1);
      let x = first.x;
      for (const w of sorted) { w.x = Math.round(x); x += w.w + gap; }
    } else if (kind === "distv" && items.length > 1) {
      const sorted = [...items].sort((a, b) => a.y - b.y);
      const total = sorted.reduce((s, w) => s + w.h, 0);
      const first = sorted[0], last = sorted[sorted.length - 1];
      const span = (last.y + last.h) - first.y;
      const gap = (span - total) / (sorted.length - 1);
      let y = first.y;
      for (const w of sorted) { w.y = Math.round(y); y += w.h + gap; }
    }
    this.draw();
    Props.sync();
  },

  nudge(dx, dy) {
    if (!this.selSet.length) return;
    this.snapshot();
    for (const i of this.selSet) {
      const w = this.project.items[i];
      if (!w) continue;
      w.x = this.clamp(w.x + dx, 0, 320 - w.w);
      w.y = this.clamp(w.y + dy, 0, 170 - w.h);
    }
    this.draw();
    Props.sync();
  },

  /* ---------- undo ---------- */
  snapshot() {
    this.undoStack.push(JSON.stringify(this.project));
    if (this.undoStack.length > 60) this.undoStack.shift();
    this.redoStack.length = 0;
    this.dirty = true;
    this.uiDirty();
  },

  uiDirty() {
    const d = document.getElementById("dirtyDot");
    if (d) d.style.visibility = this.dirty ? "visible" : "hidden";
  },
  undo() {
    const s = this.undoStack.pop();
    if (!s) return;
    this.redoStack.push(JSON.stringify(this.project));
    this.applyHistory(s);
  },

  redo() {
    const s = this.redoStack.pop();
    if (!s) return;
    this.undoStack.push(JSON.stringify(this.project));
    this.applyHistory(s);
  },

  applyHistory(s) {
    this.project = JSON.parse(s);
    const i = this.layoutKeys.indexOf(this.activeLayout());
    this.pmode = i >= 0 ? i : 0;
    this.setSel(-1);
  },

  /* ---------- render ---------- */
  draw() {
    const mask = [1, 2, 4][this.pmode || 0];
    Render.draw(this.ctx, { bg: this.project.bg, items: this.sortedItems() },
      this.sim, 2, mask, this.preview);
    if (!this.preview) {
      this.overlay();
      this.drawGhost(mask);
      this.drawGuides();
    }
  },

  drawGuides() {
    const g = this.guides;
    if (!g || (!g.v.length && !g.h.length)) return;
    const c = this.ctx;
    c.save();
    c.setTransform(2, 0, 0, 2, 0, 0);
    c.lineWidth = 1;
    c.strokeStyle = "#ff2d55";
    c.setLineDash([4, 3]);
    for (const x of g.v) {
      c.beginPath(); c.moveTo(x + .5, 0); c.lineTo(x + .5, 170); c.stroke();
    }
    for (const y of g.h) {
      c.beginPath(); c.moveTo(0, y + .5); c.lineTo(320, y + .5); c.stroke();
    }
    c.setLineDash([]);
    c.restore();
  },

  overlay() {
    const c = this.ctx;
    c.save();
    c.setTransform(2, 0, 0, 2, 0, 0);
    c.lineWidth = 1;
    this.project.items.forEach((w, i) => {
      if (w.hidden) return;
      const on = this.selSet.indexOf(i) >= 0;
      c.strokeStyle = on ? "#0072de" : "rgba(0,114,222,.22)";
      c.setLineDash(on ? [] : [2, 3]);
      c.strokeRect(w.x + .5, w.y + .5, w.w - 1, w.h - 1);
    });
    const w = this.project.items[this.sel];
    if (w) {
      c.setLineDash([]);
      c.fillStyle = "#0072de";
      c.strokeStyle = "#ffffff";
      c.lineWidth = 1;
      const hw = 5;
      const pts = [
        [w.x, w.y], [w.x + w.w / 2, w.y], [w.x + w.w, w.y],
        [w.x, w.y + w.h / 2], [w.x + w.w, w.y + w.h / 2],
        [w.x, w.y + w.h], [w.x + w.w / 2, w.y + w.h], [w.x + w.w, w.y + w.h],
      ];
      for (const [hx, hy] of pts) {
        c.fillRect(hx - hw / 2, hy - hw / 2, hw, hw);
        c.strokeRect(hx - hw / 2 + .5, hy - hw / 2 + .5, hw - 1, hw - 1);
      }
    }
    c.restore();
  },

  /* ---------- preview simulation ---------- */
  startSim() {
    if (this.simTimer) return;
    this.simTimer = setInterval(() => {
      const s = this.sim;
      s.rssi = this.clamp(s.rssi + (Math.random() * 6 - 3) | 0, 0, 63);
      s.snr = this.clamp(s.snr + (Math.random() * 4 - 2) | 0, 0, 63);
      s.hist.push({ rssi: s.rssi, snr: s.snr });
      if (s.hist.length > 40) s.hist.shift();
      const d = new Date();
      s.clock = String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");
      this.draw();
    }, 500);
  },
  stopSim() { if (this.simTimer) { clearInterval(this.simTimer); this.simTimer = null; } },
};
