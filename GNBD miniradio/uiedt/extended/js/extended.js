/*
 * SPDX-License-Identifier: MIT
 * extended.js - EXTENDED Editor: EXLU 저장/불러오기/적용 + 탭.
 *
 * 확장 에디터 전용 카탈로그:
 *  - 기본 템플릿(Default/Band map/Minimal)을 제거하고 앱 예제로 대체
 *  - 디코드 UI 패널 모듈 추가
 */
(function () {
  const $ = (id) => document.getElementById(id);

  /* ---------------- 확장 에디터 전용 모듈/템플릿 ---------------- */
  const DECODE_MODULE = {
    id: "decode", cat: "APP", name: "DECODE panel", sub: "디코드 상태/텍스트 4줄",
    colors: ["panel", "line", "gold", "ok", "fg", "muted"],
    make: () => [
      W({ t: "rect", x: 6, y: 72, w: 308, h: 96, fill: "panel", stroke: "line", r: 8 }),
      W({ t: "text", x: 10, y: 74, w: 200, h: 10, text: "DECODE CW  GPIO 4", size: "tiny", color: "gold", align: "l" }),
      W({ t: "text", x: 270, y: 74, w: 40, h: 10, text: "RUN", size: "tiny", color: "ok", align: "r" }),
      W({ t: "text", x: 10, y: 86, w: 20, h: 10, text: "SIG", size: "tiny", color: "muted", align: "l" }),
      W({ t: "bar", x: 32, y: 88, w: 278, h: 4, bind: "rssi", max: 63, color: "ok" }),
      W({ t: "text", x: 10, y: 98, w: 300, h: 12, bind: "line1", size: "small", color: "fg", align: "l" }),
      W({ t: "text", x: 10, y: 111, w: 300, h: 12, bind: "line2", size: "small", color: "fg", align: "l" }),
      W({ t: "text", x: 10, y: 124, w: 300, h: 12, bind: "line3", size: "small", color: "fg", align: "l" }),
      W({ t: "text", x: 10, y: 137, w: 300, h: 12, text: "....", size: "small", color: "muted", align: "l" }),
      W({ t: "text", x: 10, y: 154, w: 300, h: 10, text: "TURN:GPIO  CLICK:MODE  HOLD:EXIT", size: "tiny", color: "muted", align: "l" }),
    ],
  };

  if (typeof MODULES !== "undefined" && !MODULES.some((m) => m.id === "decode")) {
    MODULES.push(DECODE_MODULE);
  }

  if (typeof TEMPLATES !== "undefined") {
    TEMPLATES.length = 0;
    /* 예제는 블록 기능과 함께 다시 제공한다 */
    TEMPLATES.push(
      { id: "tpl-blank", name: "빈 레이아웃", sub: "백지에서 시작", ids: [] }
    );
  }

  const SCRIPT_EXAMPLE = [
    "-- 확장 앱 스크립트 (고급 사용자용)",
    "-- 위젯 이벤트 핸들러 예: w12_click(uid, arg)",
    "-- 전역 훅: on_tick(ms), on_click()",
    "",
  ].join("\n");

  function say(el, text, cls) {
    const n = $(el);
    if (!n) return;
    n.textContent = text || "";
    n.className = "pmsg" + (cls ? " " + cls : "");
  }

  function slug(s) {
    return (s || "").toLowerCase().replace(/[^a-z0-9_-]+/g, "-")
      .replace(/^-+|-+$/g, "").slice(0, 31);
  }

  function currentId() {
    return slug(($("exId").value || "").trim() || ($("exName").value || "").trim());
  }

  function drawPreview() {
    const cv = $("previewCanvas");
    if (!cv || !window.ED || !window.Render) return;
    const ctx = cv.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    const mask = [1, 2, 4][ED.pmode || 0];
    Render.draw(ctx, { bg: ED.project.bg, items: ED.sortedItems() },
                ED.sim, 2, mask, true);
  }

  function tab(which) {
    const layout = which === "layout";
    $("tabLayout").classList.toggle("on", layout);
    $("tabApp").classList.toggle("on", !layout);
    $("viewLayout").style.display = layout ? "" : "none";
    $("viewApp").classList.toggle("on", !layout);
    if (layout && window.ED) {
      ED.draw();
    } else {
      drawPreview();
      if (window.Blocks) Blocks.refresh();
    }
  }

  function buildExlu() {
    const kind = $("exKind").value;
    const type = $("exType").value;
    const name = ($("exName").value || "").trim();
    const author = ($("exAuthor").value || "").trim();
    const id = currentId();
    const script = ($("exScript").value || "").replace(/\r\n/g, "\n").trim();

    if (!name) throw "앱 이름을 입력하세요.";
    if (!id) throw "ID를 입력하거나 이름을 영문으로 작성하세요.";
    if (kind === "theme" && script) throw "테마는 Lua 스크립트를 포함할 수 없습니다.";
    if (type === "script" && !script) throw "Lua 단독 액션은 스크립트가 필요합니다.";

    let layout = "";
    let project = "";
    if (type === "layout") {
      const pack = ED.pack();
      layout = MRRuntime.generate(pack);
      project = JSON.stringify(pack);
    }

    const man = { format: "exlu/1", kind, type, name, author, id, api: 1, modes: 7 };
    return "EXLU1\n#MANIFEST\n" + JSON.stringify(man) + "\n" +
      "#LAYOUT\n" + layout + "\n" +
      "#SCRIPT\n" + script + "\n" +
      (project ? "#PROJECT\n" + project + "\n" : "") +
      "#END\n";
  }

  function section(text, tag) {
    const i = text.indexOf(tag + "\n");
    if (i < 0) return "";
    const start = i + tag.length + 1;
    const next = text.indexOf("\n#", start);
    return text.slice(start, next < 0 ? text.length : next).replace(/\s+$/, "");
  }

  async function save() {
    let text;
    try { text = buildExlu(); } catch (e) { say("msgInfo", e, "err"); return; }
    const id = currentId();
    try {
      const r = await fetch("/api/exlu/save?id=" + encodeURIComponent(id) +
                            "&overwrite=1", { method: "POST", body: text });
      const j = await r.json().catch(() => ({}));
      if (!r.ok || !j.ok) { say("msgInfo", "저장 실패: " + (j.error || r.status), "err"); return; }
      say("msgInfo", "저장됨: " + id + ".exlu", "ok");
      refresh();
    } catch (e) { say("msgInfo", "저장 실패: " + e, "err"); }
  }

  function download() {
    let text;
    try { text = buildExlu(); } catch (e) { say("msgInfo", e, "err"); return; }
    const blob = new Blob([text], { type: "text/plain" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = currentId() + ".exlu";
    a.click();
    URL.revokeObjectURL(a.href);
  }

  async function refresh() {
    try {
      const r = await fetch("/api/exlu/list", { cache: "no-store" });
      const j = await r.json();
      const ul = $("exList");
      ul.innerHTML = "";
      (j.files || []).forEach((f) => {
        const li = document.createElement("li");
        const n = document.createElement("span");
        n.className = "n";
        n.textContent = f.id;
        n.title = (f.name || "") + " / " + (f.author || "");
        const t = document.createElement("span");
        t.className = "ptag";
        t.textContent = f.kind === "theme" ? "theme"
          : (f.type === "script" ? "script" : "app");
        const b1 = document.createElement("button");
        b1.className = "pbtn";
        b1.textContent = "열기";
        b1.onclick = () => load(f.id);
        const b2 = document.createElement("button");
        b2.className = "pbtn danger";
        b2.textContent = "삭제";
        b2.onclick = () => del(f.id);
        li.append(n, t, b1, b2);
        ul.append(li);
      });
      if (!ul.children.length) say("msgList", "저장된 EXLU가 없습니다.", "warn");
      else say("msgList", "");
    } catch (e) { say("msgList", "목록 실패: " + e, "err"); }
  }

  async function load(id) {
    try {
      const r = await fetch("/api/exlu/get?id=" + encodeURIComponent(id), { cache: "no-store" });
      if (!r.ok) {
        const j = await r.json().catch(() => ({}));
        say("msgList", "열기 실패: " + (j.error || r.status), "err");
        return;
      }
      const t = await r.text();
      const manRaw = section(t, "#MANIFEST");
      const man = manRaw ? JSON.parse(manRaw) : {};
      $("exKind").value = man.kind === "theme" ? "theme" : "app";
      $("exType").value = man.type === "script" ? "script" : "layout";
      $("exName").value = man.name || id;
      $("exAuthor").value = man.author || "";
      $("exId").value = id;
      $("exScript").value = section(t, "#SCRIPT");
      $("exType").disabled = (man.kind === "theme");
      const proj = section(t, "#PROJECT");
      if (!proj) {
        say("msgList", "불러옴(레이아웃 원본 없음): " + id, "warn");
        return;
      }
      try {
        if (ED.load(JSON.parse(proj))) {
          ED.dirty = false;
          ED.uiDirty();
          if (window.Colors) Colors.render();
          if (window.LayerView) LayerView.render();
          if (typeof Props !== "undefined") Props.render();
          ED.draw();
          say("msgList", "불러옴: " + id, "ok");
        } else {
          say("msgList", "레이아웃 복원 실패(검증 오류)", "warn");
        }
      } catch (e) {
        say("msgList", "레이아웃 복원 실패: " + e, "warn");
      }
    } catch (e) { say("msgList", "열기 실패: " + e, "err"); }
  }

  async function del(id) {
    if (!confirm(id + " 삭제할까요?")) return;
    try {
      const r = await fetch("/api/exlu/delete?id=" + encodeURIComponent(id), { method: "POST" });
      const j = await r.json().catch(() => ({}));
      if (!r.ok || !j.ok) { say("msgList", "삭제 실패: " + (j.error || r.status), "err"); return; }
      say("msgList", "삭제됨: " + id, "ok");
      refresh();
    } catch (e) { say("msgList", "삭제 실패: " + e, "err"); }
  }

  async function apply() {
    const id = currentId();
    if (!id) { say("msgList", "적용할 ID가 없습니다.", "err"); return; }
    try {
      const r = await fetch("/api/exlu/apply?id=" + encodeURIComponent(id), { method: "POST" });
      const j = await r.json().catch(() => ({}));
      if (!r.ok || !j.ok) { say("msgList", "적용 실패: " + (j.error || r.status), "err"); return; }
      say("msgList", "적용됨: " + id, "ok");
    } catch (e) { say("msgList", "적용 실패: " + e, "err"); }
  }

  window.addEventListener("DOMContentLoaded", () => {
    $("tabLayout").onclick = () => tab("layout");
    $("tabApp").onclick = () => tab("app");
    $("btnExluSave").onclick = save;
    $("btnExluDownload").onclick = download;
    $("btnExluRefresh").onclick = refresh;
    $("btnExluApply").onclick = apply;
    /* 레이아웃 변경 시 미리보기 갱신 */
    if (window.ED) {
      const origDraw = ED.draw.bind(ED);
      ED.draw = function () {
        origDraw();
        if ($("viewApp").classList.contains("on")) drawPreview();
      };
    }
    $("exName").addEventListener("input", () => {
      if (!$("exId").value) $("projName").value = $("exName").value || "my-app";
    });
    $("exKind").addEventListener("change", () => {
      const theme = $("exKind").value === "theme";
      $("exType").disabled = theme;
      if (theme) $("exType").value = "layout";
    });
    if ($("exScript") && !$("exScript").value) {
      $("exScript").value = SCRIPT_EXAMPLE;
    }
    refresh();
  });
})();
