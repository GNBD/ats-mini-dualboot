/*
 * SPDX-License-Identifier: MIT
 * palette.js - module palette + hover preview.
 */
const Tip = {
  el: null, cv: null, nameEl: null, subEl: null, timer: null,

  init() {
    this.el = document.getElementById("tip");
    this.cv = document.getElementById("tipCanvas");
    this.nameEl = document.getElementById("tipName");
    this.subEl = document.getElementById("tipSub");
    this.colEl = document.getElementById("tipColors");
  },

  place(rect, h) {
    const w = 300;
    let x = rect.right + 10;
    let y = rect.top;
    if (x + w > window.innerWidth - 8) x = rect.left - w - 10;
    if (x < 8) x = 8;
    if (y + h > window.innerHeight - 8) y = window.innerHeight - h - 8;
    if (y < 8) y = 8;
    this.el.style.left = x + "px";
    this.el.style.top = y + "px";
  },

  preview(project, name, sub, rect, colors) {
    this.hide();
    this.timer = setTimeout(() => {
      this.el.classList.remove("textonly");
      this.nameEl.textContent = name;
      this.subEl.textContent = sub || "";
      if (this.colEl) {
        this.colEl.textContent = (colors && colors.length)
          ? TP("적용 색상: ", "colors: ") + colors.join(" · ")
          : "";
      }
      Render.draw(this.cv.getContext("2d"), project, MOCK, 2);
      this.el.hidden = false;
      this.place(rect, 230);
    }, 420);
  },

  module(mod, rect) {
    this.preview({ bg: "bg", items: mod.make() }, T(mod.name), T(mod.sub), rect, mod.colors);
  },

  template(tpl, rect) {
    const items = [];
    for (const id of tpl.ids) {
      const m = MODULES.find((x) => x.id === id);
      if (m) items.push(...m.make());
    }
    const pal = [];
    for (const id of tpl.ids) {
      const m = MODULES.find((x) => x.id === id);
      if (m && m.colors) for (const c of m.colors) if (pal.indexOf(c) < 0) pal.push(c);
    }
    this.preview({ bg: "bg", items }, T(tpl.name), TF("템플릿 · {0}", T(tpl.sub)), rect, pal);
  },

  text(title, sub, rect) {
    this.hide();
    this.timer = setTimeout(() => {
      this.el.classList.add("textonly");
      this.nameEl.textContent = title;
      this.subEl.textContent = sub || "";
      if (this.colEl) this.colEl.textContent = "";
      this.el.hidden = false;
      this.place(rect, 90);
    }, 420);
  },

  hide() {
    if (this.timer) { clearTimeout(this.timer); this.timer = null; }
    if (this.el) this.el.hidden = true;
  },
};

function attachTip(el, fn) {
  el.tabIndex = 0;
  el.addEventListener("pointerenter", () => fn(el.getBoundingClientRect()));
  el.addEventListener("pointerleave", () => Tip.hide());
  el.addEventListener("blur", () => Tip.hide());
  el.addEventListener("focus", () => fn(el.getBoundingClientRect()));
}

function buildPalette() {
  const body = document.getElementById("paletteBody");
  const q = (document.getElementById("search").value || "").toLowerCase();
  body.innerHTML = "";
  const match = (name, sub) => !q || (name + " " + (sub || "")).toLowerCase().includes(q);

  const gt = document.createElement("div");
  gt.className = "grp"; gt.textContent = T("TEMPLATES");
  body.appendChild(gt);
  for (const t of TEMPLATES) {
    if (!match(t.name, t.sub)) continue;
    const el = document.createElement("div");
    el.className = "mod tpl";
    el.innerHTML = `<span class="dot"></span><div><div class="nm">${T(t.name)}</div>
      <div class="sub">${T(t.sub)}</div></div>`;
    el.addEventListener("click", () => ED.applyTemplate(t));
    attachTip(el, (r) => Tip.template(t, r));
    body.appendChild(el);
  }

  for (const cat of ["UI", "BASIC"]) {
    const g = document.createElement("div");
    g.className = "grp";
    g.textContent = T(cat === "UI" ? "SCREEN MODULES" : "PRIMITIVES");
    body.appendChild(g);
    for (const m of MODULES.filter((x) => x.cat === cat)) {
      if (!match(m.name, m.sub)) continue;
      const el = document.createElement("div");
      el.className = "mod";
      el.draggable = false;
      el.innerHTML = `<span class="dot"></span><div><div class="nm">${T(m.name)}</div>
        <div class="sub">${T(m.sub)}</div></div>`;
      el.addEventListener("dragstart", (e) => {
        e.dataTransfer.setData("text/module", m.id);
        e.dataTransfer.effectAllowed = "copy";
        ED.dragPayload = { kind: "module", id: m.id };
      });
      el.addEventListener("dragend", () => { ED.dragPayload = null; ED.ghost = null; ED.draw(); });
      el.addEventListener("dblclick", () => ED.addModule(m, 160, 85));
      el.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;
        if (e.target.closest("input,select,button")) return;
        e.preventDefault();
        ED.beginModuleDrag(m, e);
      });
      attachTip(el, (r) => Tip.module(m, r));
      body.appendChild(el);
    }
  }
}
