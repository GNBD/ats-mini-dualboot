/*
 * SPDX-License-Identifier: MIT
 * colors.js - custom palette editor (background/accent/...).
 */
const Colors = {
  labels: {
    bg: "Background", panel: "Panel", hl: "Highlight", hsel: "Selected",
    cell: "Cell", line: "Line", track: "Track", fg: "Foreground",
    muted: "Muted", accent: "Accent", accenth: "Accent hi", gold: "Gold",
    ok: "OK", warn: "Warn", red: "Red", blue: "Blue",
  },

  labelsEn: {
    bg: "배경", panel: "패널", hl: "하이라이트", hsel: "선택됨",
    cell: "셀", line: "선", track: "트랙", fg: "전경",
    muted: "흐림", accent: "강조", accenth: "강조 하이", gold: "골드",
    ok: "정상", warn: "경고", red: "빨강", blue: "파랑",
  },
  palette() {
    if (!ED.project.palette) {
      ED.project.palette = JSON.parse(JSON.stringify(PALETTE));
    } else {
      for (const k of MRSchema.PALETTE_KEYS) {
        if (!ED.project.palette[k]) ED.project.palette[k] = PALETTE[k] || "#000000";
      }
    }
    return ED.project.palette;
  },

  render() {
    const body = document.getElementById("colorsBody");
    if (!body) return;
    body.innerHTML = "";
    const p = this.palette();

    const grid = document.createElement("div");
    grid.className = "colorgrid";
    for (const k of MRSchema.PALETTE_KEYS) {
      const cell = document.createElement("label");
      cell.className = "colorcell";
      cell.title = TP(this.labelsEn[k] || k, this.labels[k] || k);
      const inp = document.createElement("input");
      inp.type = "color";
      inp.value = p[k] || "#000000";
      inp.addEventListener("input", () => {
        p[k] = inp.value;
        ED.dirty = true;
        ED.uiDirty();
        ED.draw();
        if (window.LayerView) LayerView.render();
      });
      const nm = document.createElement("span");
      nm.textContent = k;
      cell.appendChild(inp);
      cell.appendChild(nm);
      grid.appendChild(cell);
    }
    body.appendChild(grid);

    const reset = document.createElement("button");
    reset.className = "btn";
    reset.textContent = TP("색상 초기화", "Reset colors");
    reset.style.width = "100%";
    reset.onclick = async () => {
      if (!await UI.confirm(T("색상을 기본값으로 되돌릴까요?"), TP("색상 초기화", "Reset colors"))) return;
      ED.project.palette = JSON.parse(JSON.stringify(PALETTE));
      ED.dirty = true;
      ED.uiDirty();
      this.render();
      ED.draw();
    };
    body.appendChild(reset);
  },
};

window.Colors = Colors;
