/*
 * SPDX-License-Identifier: MIT
 * app.js - UI editor bootstrap.
 */
function radioStatus(text, ok) {
  const el = document.getElementById("radioStatus");
  if (!el) return;
  el.textContent = text;
  el.style.color = ok ? "#067647" : "#b42318";
}

function updateHud() {
  const hud = document.getElementById("hud");
  if (!hud) return;
  hud.textContent = ED.preview
    ? T("PREVIEW — 실제 기기 렌더링과 동일한 좌표/색/폰트 크기")
    : T("FM/AM/SSB는 각각 별도 레이아웃 · 드래그로 배치 · Delete 삭제");
}

function refreshLocale() {
  I18N.apply();
  buildPalette();
  if (window.LayerView) LayerView.render();
  if (window.Colors) Colors.render();
  if (typeof Props !== "undefined") Props.render();
  updateHud();
  ED.draw();
}

function setZoom(z, silent) {
  const cv = document.getElementById("screen");
  const box = cv.parentElement;
  const nz = Math.max(0.5, Math.min(2, (parseInt(z, 10) || 100) / 100));
  const prev = ED.zoom || 1;
  const anchor = (document.getElementById("zoomAnchor") || {}).value || "center";
  const k = nz / prev;
  cv.style.width = Math.round(640 * nz) + "px";
  ED.zoom = nz;
  if (!silent) {
    document.getElementById("zoomRange").value = Math.round(nz * 100);
    document.getElementById("zoomVal").textContent = Math.round(nz * 100) + "%";
    try { localStorage.setItem("uiedt.zoom", String(Math.round(nz * 100))); } catch (e) { /* ignore */ }
    if (anchor === "center") {
      box.scrollLeft = (box.scrollLeft + box.clientWidth / 2) * k - box.clientWidth / 2;
      box.scrollTop = (box.scrollTop + box.clientHeight / 2) * k - box.clientHeight / 2;
    } else {
      box.scrollLeft = box.scrollLeft * k;
      box.scrollTop = box.scrollTop * k;
    }
  }
}

function initZoom() {
  const rng = document.getElementById("zoomRange");
  try {
    const a = localStorage.getItem("uiedt.zoomanchor");
    if (a) document.getElementById("zoomAnchor").value = a;
  } catch (e) { /* ignore */ }
  let z = 100;
  try { z = parseInt(localStorage.getItem("uiedt.zoom"), 10) || 100; } catch (e) { z = 100; }
  z = Math.max(50, Math.min(200, z));
  rng.value = z;
  setZoom(z, true);
  document.getElementById("zoomVal").textContent = z + "%";
  rng.addEventListener("input", () => setZoom(rng.value));
  document.getElementById("zoomAnchor").addEventListener("change", () => {
    try { localStorage.setItem("uiedt.zoomanchor", document.getElementById("zoomAnchor").value); } catch (e) { /* ignore */ }
  });
  const box = document.getElementById("screen").parentElement;
  box.addEventListener("wheel", (e) => {
    if (!e.ctrlKey && !e.metaKey) return;
    e.preventDefault();
    setZoom((ED.zoom || 1) * 100 + (e.deltaY < 0 ? 10 : -10));
  }, { passive: false });
}

function initNotice() {
  const el = document.getElementById("notice");
  if (!el) return;
  let hidden = false;
  try { hidden = localStorage.getItem("uiedt.notice") === "off"; } catch (e) { hidden = false; }
  el.hidden = hidden;
  const close = document.getElementById("noticeClose");
  if (close) {
    close.onclick = () => {
      el.hidden = true;
      try { localStorage.setItem("uiedt.notice", "off"); } catch (e) { /* ignore */ }
    };
  }
}

async function saveToRadio() {
  let name = (document.getElementById("projName").value || "my-ui").trim();
  name = await UI.prompt(T("저장 이름 (영문/숫자/_/- 1~24자)"), name, T("라디오에 저장"));
  if (name == null) return;
  if (!Radio.nameOk(name)) {
    radioStatus(T("이름이 올바르지 않음"), false);
    return;
  }
  let overwrite = false;
  try {
    const l = await Radio.list();
    const files = l.files || [];
    if (files.indexOf(name) >= 0) {
      if (!await UI.confirm(TF("'{0}' 파일이 이미 있습니다. 덮어쓸까요?", name), T("덮어쓰기"))) return;
      overwrite = true;
    }
  } catch (e) {
    radioStatus(T("라디오 연결 없음"), false);
    return;
  }
  try {
    radioStatus(T("저장 중..."), true);
    const pack = ED.pack();
    const chk = MRValidate.validateProject(pack, {});
    if (!chk.ok) {
      radioStatus(T("프로젝트가 올바르지 않음"), false);
      await UI.alert(chk.errors[0].message + "\n" + (chk.errors[0].path || ""), T("올바르지 않은 프로젝트"));
      return;
    }
    let total = (pack.ui.items || []).length;
    if (pack.ui.layouts) {
      for (const k of MRSchema.LAYOUTS) total += (pack.ui.layouts[k] || []).length;
    }
    if (total > 128) {
      radioStatus(T("위젯이 너무 많음"), false);
      await UI.alert(TF("라디오 런타임은 위젯 128개까지 지원합니다. 현재 {0}개", total), T("너무 많음"));
      return;
    }
    await Radio.save(pack, name, overwrite);
    ED.dirty = false;
    ED.uiDirty();
    radioStatus(TF("저장+적용됨: {0}", name), true);
  } catch (e) {
    radioStatus(TF("저장 실패: {0}", e.message), false);
  }
}

async function manageRadio() {
  let l;
  try { l = await Radio.list(); } catch (e) {
    radioStatus(T("라디오 연결 없음"), false);
    return;
  }
  const files = l.files || [];
  if (!files.length) {
    await UI.alert(T("저장된 UI가 없습니다."), T("UI 목록"));
    return;
  }
  const r = await UI.manage(T("저장된 UI"), files.map((f) => ({
    value: f, label: f + (f === l.active ? "  (active)" : ""),
  })), l.active || files[0]);
  if (!r) return;

  if (r.action === "load") {
    try {
      radioStatus(T("불러오는 중..."), true);
      await Radio.load(r.value);
      syncPmodeButtons();
      Colors.render();
      radioStatus(TF("불러옴: {0}", r.value), true);
    } catch (e) { radioStatus(TF("불러오기 실패: {0}", e.message), false); }
    return;
  }

  if (r.action === "delete") {
    if (!await UI.confirm(TF("'{0}' UI를 삭제할까요?", r.value), T("UI 삭제"))) return;
    try {
      await Radio.remove(r.value);
      radioStatus(TF("삭제됨: {0}", r.value), true);
      if (Radio.current === r.value) Radio.current = null;
    } catch (e) { radioStatus(TF("삭제 실패: {0}", e.message), false); }
  }
}

function syncPmodeButtons() {
  document.querySelectorAll("[data-pmode]").forEach((x) =>
    x.classList.toggle("on", (parseInt(x.dataset.pmode, 10) || 0) === ED.pmode));
}

window.addEventListener("DOMContentLoaded", () => {
  UI.init();
  ED.init(document.getElementById("screen"));
  Tip.init();
  buildPalette();

  document.getElementById("search").addEventListener("input", () => buildPalette());
  document.getElementById("btnUndo").onclick = () => ED.undo();
  document.getElementById("btnRedo").onclick = () => ED.redo();
  document.getElementById("btnExport").onclick = () => ED.exportPack();
  document.getElementById("btnImport").onclick = () => document.getElementById("fileIn").click();
  document.getElementById("btnRadioSave").onclick = () => saveToRadio();
  document.getElementById("btnRadioManage").onclick = () => manageRadio();
  document.getElementById("langBtn").onclick = () => I18N.toggle();
  initZoom();
  initNotice();

  document.getElementById("fileIn").addEventListener("change", (e) => {
    const f = e.target.files[0];
    if (!f) return;
    const rd = new FileReader();
    rd.onload = async () => {
      try {
        if (!ED.load(JSON.parse(rd.result))) {
          const err = ED.lastLoadErrors;
          await UI.alert(T("올바르지 않은 UI 프로젝트") +
            (err && err.length ? "\n" + err[0].message + " (" + err[0].path + ")" : ""),
            T("가져오기 실패"));
        } else {
          syncPmodeButtons();
          Colors.render();
        }
      } catch (err) { await UI.alert(TF("파싱 오류: {0}", err.message), T("가져오기 실패")); }
    };
    rd.readAsText(f);
    e.target.value = "";
  });

  document.getElementById("btnNew").onclick = async () => {
    if (!await UI.confirm(T("새 UI 프로젝트로 초기화할까요?"), T("새 프로젝트"))) return;
    ED.project = {
      bg: "bg", items: [], layouts: null, activeMode: "FM",
      palette: JSON.parse(JSON.stringify(PALETTE)), meta: {},
    };
    ED.pmode = 0;
    ED.setSel(-1);
    ED.dirty = false;
    ED.uiDirty();
    document.getElementById("projName").value = "my-ui";
    syncPmodeButtons();
    buildPalette();
    Colors.render();
    ED.draw();
  };

  window.addEventListener("beforeunload", (e) => {
    if (!ED.dirty) return undefined;
    const msg = T("저장하지 않은 변경사항이 있습니다.\nDownload로 저장하세요.");
    e.preventDefault();
    e.returnValue = msg;
    return msg;
  });

  document.getElementById("previewChk").addEventListener("change", (e) => {
    ED.preview = e.target.checked;
    document.getElementById("screen").classList.toggle("preview", ED.preview);
    document.getElementById("hud").textContent = ED.preview
      ? T("PREVIEW — 실제 기기 렌더링과 동일한 좌표/색/폰트 크기")
      : T("FM/AM/SSB는 각각 별도 레이아웃 · 드래그로 배치 · Delete 삭제");
    if (ED.preview) ED.startSim(); else ED.stopSim();
    ED.draw();
  });

  document.getElementById("gridChk").addEventListener("change", (e) => {
    ED.useGrid = e.target.checked;
  });
  document.getElementById("gridSize").addEventListener("change", (e) => {
    ED.snap = Math.max(1, parseInt(e.target.value, 10) || 8);
  });

  document.querySelectorAll("[data-align]").forEach((b) => {
    b.addEventListener("click", () => ED.align(b.dataset.align));
  });

  const MODE_MOCK = {
    0: { mode: "FM", band: "VHF", freq: 89100000, ps: "KBS 1FM",
         rt: "뉴스와 음악, 그리고 사람", pty: "Varied", stereo: "ST" },
    1: { mode: "AM", band: "MW", freq: 1200000, ps: "", rt: "", pty: "", stereo: "" },
    2: { mode: "LSB", band: "40m", freq: 7100000, ps: "", rt: "", pty: "", stereo: "" },
  };
  document.querySelectorAll("[data-pmode]").forEach((b) => {
    b.addEventListener("click", () => {
      const idx = parseInt(b.dataset.pmode, 10) || 0;
      ED.setPmode(idx);
      syncPmodeButtons();
      Object.assign(ED.sim, MODE_MOCK[idx]);
      ED.draw();
      if (window.LayerView) LayerView.render();
      Props.render();
    });
  });

  ED.applyTemplate(TEMPLATES[0]);
  if (!ED.project.palette) {
    ED.project.palette = JSON.parse(JSON.stringify(PALETTE));
  }
  Colors.render();
  ED.dirty = false;
  ED.uiDirty();
  I18N.apply();
  updateHud();

  const boot = document.getElementById("boot");
  if (boot) {
    /* 로딩 창에서 버전을 확인할 수 있도록 최소 표시 시간 확보 */
    setTimeout(() => {
      boot.classList.add("out");
      boot.addEventListener("transitionend", () => boot.remove(), { once: true });
      setTimeout(() => boot.remove(), 600);
    }, 500);
  }
});

window.ED = ED;
window.LayerView = LayerView;
