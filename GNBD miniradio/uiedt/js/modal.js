/*
 * SPDX-License-Identifier: MIT
 * modal.js - HTML modal dialogs (no window.confirm/prompt/alert).
 */
const UI = {
  root: null,
  resolver: null,

  init() {
    if (this.root) return;
    const root = document.createElement("div");
    root.className = "uimodal";
    root.hidden = true;
    root.innerHTML = `
      <div class="uimodal-bg"></div>
      <section class="uimodal-card" role="dialog" aria-modal="true">
        <h3 id="uimodalTitle"></h3>
        <div id="uimodalBody" class="uimodal-body"></div>
        <input id="uimodalInput" class="uimodal-input" hidden>
        <div id="uimodalList" class="uimodal-list" hidden></div>
        <div class="uimodal-actions">
          <button id="uimodalCancel" class="btn">Cancel</button>
          <button id="uimodalOk" class="btn primary">OK</button>
        </div>
      </section>`;
    document.body.appendChild(root);
    this.root = root;

    root.querySelector(".uimodal-bg").onclick = () => this.finish(null);
    root.querySelector("#uimodalCancel").onclick = () => this.finish(null);
    root.querySelector("#uimodalOk").onclick = () => this.accept();
    document.addEventListener("keydown", (e) => {
      if (root.hidden) return;
      if (e.key === "Escape") { e.preventDefault(); this.finish(null); }
      if (e.key === "Enter" && e.target.id === "uimodalInput") {
        e.preventDefault(); this.accept();
      }
    });
  },

  accept() {
    const inp = this.root.querySelector("#uimodalInput");
    this.finish(inp.hidden ? true : inp.value);
  },

  finish(value) {
    if (!this.root || this.root.hidden) return;
    this.root.hidden = true;
    const r = this.resolver;
    this.resolver = null;
    if (r) r(value);
  },

  open(opts) {
    this.init();
    const root = this.root;
    root.querySelector("#uimodalTitle").textContent = opts.title || "";
    root.querySelector("#uimodalBody").textContent = opts.message || "";
    root.querySelector("#uimodalBody").style.whiteSpace = "pre-wrap";

    const input = root.querySelector("#uimodalInput");
    input.hidden = !opts.input;
    input.value = opts.value || "";

    const list = root.querySelector("#uimodalList");
    list.innerHTML = "";
    list.hidden = !opts.items;
    let picked = opts.value || null;
    if (opts.items) {
      for (const it of opts.items) {
        const b = document.createElement("button");
        b.className = "uimodal-item" + (it.value === opts.value ? " on" : "");
        b.textContent = it.label;
        b.onclick = () => {
          picked = it.value;
          list.querySelectorAll(".uimodal-item").forEach((x) =>
            x.classList.toggle("on", x === b));
        };
        b.ondblclick = () => { picked = it.value; this.finish(picked); };
        list.appendChild(b);
      }
    }

    root.querySelector("#uimodalCancel").hidden = opts.alert === true;
    root.querySelector("#uimodalCancel").textContent = TP("취소", "Cancel");
    root.querySelector("#uimodalOk").hidden = false;
    root.querySelector("#uimodalOk").textContent = opts.okText || TP("확인", "OK");
    root.hidden = false;

    return new Promise((resolve) => {
      this.resolver = (v) => {
        if (opts.items) resolve(v ? picked : null);
        else resolve(v);
      };
      setTimeout(() => {
        if (opts.input) { input.focus(); input.select(); }
        else if (opts.items) list.querySelector(".uimodal-item")?.focus();
        else root.querySelector("#uimodalOk").focus();
      }, 20);
    });
  },

  alert(message, title) {
    return this.open({ title: title || TP("알림", "Notice"), message, alert: true, okText: TP("확인", "OK") });
  },

  confirm(message, title) {
    return this.open({ title: title || TP("확인", "Confirm"), message, okText: TP("확인", "OK") });
  },

  prompt(message, value, title) {
    return this.open({ title: title || TP("입력", "Input"), message, input: true, value: value || "" });
  },

  choose(title, items, value) {
    return this.open({ title, message: "", items, value, okText: TP("불러오기", "Load") });
  },

  manage(title, items, value) {
    this.init();
    const root = this.root;
    root.querySelector("#uimodalTitle").textContent = title;
    root.querySelector("#uimodalBody").textContent = "";
    root.querySelector("#uimodalInput").hidden = true;
    const list = root.querySelector("#uimodalList");
    list.hidden = false;
    list.innerHTML = "";
    for (const it of items) {
      const row = document.createElement("div");
      row.className = "uimanage";
      const nm = document.createElement("button");
      nm.className = "uimodal-item" + (it.value === value ? " on" : "");
      nm.textContent = it.label;
      nm.onclick = () => this.finish({ action: "load", value: it.value });
      const del = document.createElement("button");
      del.className = "uimanage-del";
      del.textContent = TP("삭제", "Delete");
      del.onclick = () => this.finish({ action: "delete", value: it.value });
      row.appendChild(nm);
      row.appendChild(del);
      list.appendChild(row);
    }
    root.querySelector("#uimodalOk").hidden = true;
    root.querySelector("#uimodalCancel").hidden = false;
    root.hidden = false;
    return new Promise((resolve) => { this.resolver = resolve; });
  },
};

window.UI = UI;
