/*
 * SPDX-License-Identifier: MIT
 * properties.js - right panel (selection inspector)
 */
/* Bind 목록: 첫 항목은 바인딩 없음(직접 입력한 text 사용) */
function bindOptions() {
  return [["", T("(텍스트)")]].concat(BINDINGS.map(([v, l]) => [v, T(l)]));
}
const COLORS = ["bg", "panel", "fg", "muted", "line", "accent", "accenth",
  "ok", "warn", "red", "track", "hsel", "none"];

function colorOptions() {
  return (window.RecentColors ? RecentColors.options(COLORS) : COLORS);
}

const Props = {
  el(id) { return document.getElementById(id); },

  row(label, ...nodes) {
    const r = document.createElement("div");
    r.className = "row";
    const l = document.createElement("label");
    l.textContent = label;
    r.appendChild(l);
    for (const n of nodes) r.appendChild(n);
    return r;
  },

  input(value, onInput, type) {
    const i = document.createElement("input");
    i.type = type || "text";
    i.value = value == null ? "" : value;
    i.addEventListener("input", () => onInput(i.value));
    return i;
  },

  select(options, value, onInput) {
    const s = document.createElement("select");
    for (const o of options) {
      const [v, t] = Array.isArray(o) ? o : [o, o];
      const el = document.createElement("option");
      el.value = v; el.textContent = t;
      if (String(v) === String(value)) el.selected = true;
      s.appendChild(el);
    }
    s.addEventListener("change", () => onInput(s.value));
    return s;
  },

  render() {
    const body = this.el("propsBody");
    body.innerHTML = "";
    const i = ED.sel;
    if (i < 0) {
      const hint = document.createElement("div");
      hint.className = "empty";
      hint.innerHTML = [
        T("모듈을 선택하세요."), "",
        T("캔버스: 드래그 이동 · 가장자리/모서리 핸들로 크기 조절"),
        T("Shift+클릭 다중선택 · 방향키 미세조정"),
        T("Delete 삭제 · Ctrl+D 복제 · Ctrl+Z 실행취소"),
        T("레이어: 오른쪽 LAYERS에서 드래그 (아래 = 앞)"),
        T("FM / AM / SSB는 각각 별도 레이아웃입니다."),
      ].join("<br>");
      body.appendChild(hint);
      const sep = document.createElement("div");
      sep.className = "sep";
      body.appendChild(sep);
      const cnt = document.createElement("div");
      cnt.className = "muted";
      cnt.textContent = "items: " + ED.project.items.length;
      body.appendChild(cnt);
      return;
    }
    const w = ED.project.items[i];
    const upd = () => { ED.draw(); };

    const t = this.input(w.t, () => {}, "text");
    t.disabled = true;
    body.appendChild(this.row(T("타입 (읽기 전용)"), t));

    const nameInp = this.input(w.name || "", (v) => {
      w.name = v.trim() || undefined;
      upd();
      if (window.LayerView) LayerView.render();
    });
    nameInp.placeholder = w.origin || ED.baseOf(w);
    body.appendChild(this.row(T("이름"), nameInp));

    const g1 = document.createElement("div");
    g1.className = "grid2";
    g1.appendChild(this.input(w.x, (v) => { w.x = +v; upd(); }, "number"));
    g1.appendChild(this.input(w.y, (v) => { w.y = +v; upd(); }, "number"));
    body.appendChild(this.row(T("X / Y 위치"), g1));

    const g2 = document.createElement("div");
    g2.className = "grid2";
    g2.appendChild(this.input(w.w, (v) => { w.w = +v; upd(); }, "number"));
    g2.appendChild(this.input(w.h, (v) => { w.h = +v; upd(); }, "number"));
    body.appendChild(this.row(T("너비 / 높이"), g2));
    body.querySelectorAll(".grid2 input").forEach((inp) => { inp.style.width = "100%"; });

    body.appendChild(this.row(T("크기 %"), this.input(w.scale == null ? 100 : w.scale,
      (v) => {
        w.scale = Math.max(50, Math.min(200, parseInt(v, 10) || 100));
        upd();
      }, "number")));

    {
      const hide = document.createElement("input");
      hide.type = "checkbox";
      hide.checked = !!w.hidden;
      hide.addEventListener("change", () => {
        w.hidden = hide.checked;
        ED.draw();
      });
      body.appendChild(this.row(T("숨김"), hide));
    }

    {
      const lz = this.input(w.z == null ? 0 : w.z, (v) => { w.z = +v; upd(); }, "number");
      const up = document.createElement("button");
      up.className = "btn"; up.textContent = "▲"; up.title = T("레이어 +1");
      up.style.padding = "3px 7px";
      up.onclick = () => { w.z = (w.z || 0) + 1; upd(); Props.render(); };
      const dn = document.createElement("button");
      dn.className = "btn"; dn.textContent = "▼"; dn.title = T("레이어 -1");
      dn.style.padding = "3px 7px";
      dn.onclick = () => { w.z = (w.z || 0) - 1; upd(); Props.render(); };
      body.appendChild(this.row(T("레이어 (z)"), lz, up, dn));
    }
    {
      const front = document.createElement("button");
      front.className = "btn"; front.textContent = T("맨 앞으로");
      front.style.padding = "3px 7px";
      front.onclick = () => { w.z = ED.topZ(); upd(); Props.render(); if (window.LayerView) LayerView.render(); };
      const back = document.createElement("button");
      back.className = "btn"; back.textContent = T("맨 뒤로");
      back.style.padding = "3px 7px";
      back.onclick = () => { w.z = -1; upd(); Props.render(); if (window.LayerView) LayerView.render(); };
      body.appendChild(this.row(T("순서"), front, back));
    }
    {
      const others = ED.project.items.filter((x) => x !== w && x.uid);
      const opts = [["", "— none —"]].concat(others.map((x) =>
        [String(x.uid), (x.t || "?") + (x.bind ? "/" + x.bind : "") + " #" + x.uid]));
      body.appendChild(this.row(T("레이어 상속"), this.select(opts, w.parent || "",
        (v) => { w.parent = v || undefined; upd(); })));
      if (w.parent) {
        const n = document.createElement("div");
        n.className = "empty";
        n.textContent = T("부모의 레이어를 그대로 사용 (동일 레이어)");
        body.appendChild(n);
      }
    }

    if (w.t === "text") {
      body.appendChild(this.row(T("값 바인딩"), this.select(bindOptions(), w.bind || "", (v) => { w.bind = v || undefined; upd(); })));
      body.appendChild(this.row(T("표시 텍스트"), this.input(w.text || "", (v) => { w.text = v; upd(); })));
      body.appendChild(this.row(T("폰트 크기"), this.select(["tiny", "small", "large"], w.size || "tiny", (v) => { w.size = v; upd(); })));
      body.appendChild(this.row(T("정렬"), this.select([["l", "Left"], ["c", "Center"], ["r", "Right"]], w.align || "l", (v) => { w.align = v; upd(); })));
      body.appendChild(this.row(T("색상"), this.select(colorOptions(), w.color || "fg", (v) => { RecentColors.add(v); w.color = v; upd(); })));
      body.appendChild(this.row(T("칩 배경"), this.select([["", "off"], ["1", "on"]], w.chip ? "1" : "", (v) => { w.chip = v ? 1 : undefined; upd(); })));
    } else if (w.t === "rect") {
      body.appendChild(this.row(T("채우기 색"), this.select(colorOptions(), w.fill || "panel", (v) => { RecentColors.add(v); w.fill = v; upd(); })));
      body.appendChild(this.row(T("테두리 색"), this.select(colorOptions(), w.stroke || "none", (v) => { RecentColors.add(v); w.stroke = v; upd(); })));
      body.appendChild(this.row(T("모서리 반경"), this.input(w.r || 0, (v) => { w.r = +v; upd(); }, "number")));
    } else if (w.t === "bar") {
      body.appendChild(this.row(T("값 바인딩"), this.select(bindOptions(), w.bind || "", (v) => { w.bind = v || undefined; upd(); })));
      body.appendChild(this.row(T("최대값"), this.input(w.max || 63, (v) => { w.max = +v; upd(); }, "number")));
      body.appendChild(this.row(T("색상"), this.select(colorOptions(), w.color || "ok", (v) => { RecentColors.add(v); w.color = v; upd(); })));
    } else if (w.t === "line") {
      body.appendChild(this.row(T("색상"), this.select(colorOptions(), w.color || "line", (v) => { RecentColors.add(v); w.color = v; upd(); })));
    } else if (w.t === "wifi") {
      const winfo = document.createElement("div");
      winfo.className = "empty";
      winfo.textContent = T("Wi-Fi 세기 아이콘: 막대 수를 고정하거나 0으로 두면 실제 상태(RSSI)에 따라 자동 표시합니다.");
      body.appendChild(winfo);
      body.appendChild(this.row(T("막대 수 (0~4, 0=자동)"), this.input(w.max == null ? 4 : w.max,
        (v) => { w.max = Math.max(0, Math.min(4, +v)); upd(); }, "number")));
      body.appendChild(this.row(T("색상"), this.select(colorOptions(), w.color || "accenth", (v) => { RecentColors.add(v); w.color = v; upd(); })));
    } else if (w.t === "batt_icon") {
      body.appendChild(this.row("Bind", this.select(bindOptions(), w.bind || "batt",
        (v) => { w.bind = v || undefined; upd(); })));
      body.appendChild(this.row("Max", this.input(w.max || 100,
        (v) => { w.max = +v; upd(); }, "number")));
      body.appendChild(this.row(T("색상"), this.select(colorOptions(), w.color || "ok",
        (v) => { RecentColors.add(v); w.color = v; upd(); })));
      body.appendChild(this.row(T("옵션 (예: noterm)"), this.input(w.text || "",
        (v) => { w.text = v || undefined; upd(); })));
    } else if (w.t === "decode") {
      const info = document.createElement("div");
      info.className = "empty";
      info.textContent = T("디코드 패널: 옵션 문자열로 설정합니다. 예) gpio=4;mode=0;lines=4 (mode 0=CW, 1=RTTY)");
      body.appendChild(info);
      body.appendChild(this.row("Options", this.input(w.text || "gpio=4;mode=0;lines=4",
        (v) => { w.text = v; upd(); })));
      body.appendChild(this.row(T("색상"), this.select(colorOptions(), w.color || "accent",
        (v) => { w.color = v; upd(); })));
    } else if (w.t === "menu") {
      const info = document.createElement("div");
      info.className = "empty";
      info.innerHTML = T("설계 가이드입니다. 실제 MENU/SETTINGS 내용은 펌웨어가 그리고,") + "<br>" +
        T("이 모듈은 위치/크기와 가로·세로 배치만 보여줍니다 (내보내기 제외).");
      body.appendChild(info);
      body.appendChild(this.row(T("목록 종류"), this.select(
        [["menu", "MENU"], ["settings", "SETTINGS"]], w.kind || "menu",
        (v) => { w.kind = v; upd(); })));
      body.appendChild(this.row(T("배치 방향"), this.select(
        [["v", T("세로")], ["h", T("가로")]], w.orient || "v",
        (v) => {
          const old = w.orient || "v";
          w.orient = v;
          if (v !== old) {
            if (v === "h" && w.w <= 140 && w.h >= 80) {
              w.x = 8; w.y = 118; w.w = 304; w.h = 50;
            } else if (v === "v" && w.w >= 200 && w.h <= 80) {
              w.x = 212; w.y = 18; w.w = 98; w.h = 150;
            }
          }
          upd();
        })));
    } else {
      const n = document.createElement("div");
      n.className = "empty";
      n.textContent = T("이 모듈은 위치/크기만 조정합니다 (내용은 펌웨어가 그림).");
      body.appendChild(n);
    }

    const sep = document.createElement("div");
    sep.className = "sep";
    body.appendChild(sep);
    const btns = document.createElement("div");
    btns.className = "grid2";
    const d1 = document.createElement("button");
    d1.className = "btn"; d1.textContent = T("복제"); d1.onclick = () => ED.dup();
    const d2 = document.createElement("button");
    d2.className = "btn"; d2.textContent = T("삭제"); d2.onclick = () => ED.del();
    btns.appendChild(d1); btns.appendChild(d2);
    body.appendChild(btns);
  },

  sync() {
    const i = ED.sel;
    if (i < 0) return;
    const w = ED.project.items[i];
    const nums = this.el("propsBody").querySelectorAll(".grid2 input[type=number]");
    if (nums.length === 4) {
      if (document.activeElement !== nums[0]) nums[0].value = w.x;
      if (document.activeElement !== nums[1]) nums[1].value = w.y;
      if (document.activeElement !== nums[2]) nums[2].value = w.w;
      if (document.activeElement !== nums[3]) nums[3].value = w.h;
    }
  },
};
