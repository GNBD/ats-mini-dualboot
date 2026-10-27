/*
 * SPDX-License-Identifier: MIT
 * layers.js - layer viewer/panel (Photoshop style)
 *
 * - top of the list = drawn last (highest layer)
 * - drag a row to reorder (own-layer items only)
 * - eye toggles visibility
 * - inherited items follow their parent and are not draggable
 */
const LayerView = {
  dragUid: null,

  render() {
    const body = document.getElementById("layersBody");
    if (!body) return;
    body.innerHTML = "";

    /* inherited → 부모 uid 로 매핑 (드롭 대상) */
    const parentOf = (w) => w.parent || null;

    for (const { w, i } of ED.listOrder()) {
      const inherited = !!w.parent;
      const row = document.createElement("div");
      row.className = "lrow" + (ED.selSet.indexOf(i) >= 0 ? " on" : "") +
                      (w.hidden ? " dim" : "");
      row.draggable = !inherited;
      row.dataset.uid = w.uid;
      row.dataset.puid = inherited ? parentOf(w) : w.uid;

      const grip = document.createElement("span");
      grip.className = "grip";
      grip.textContent = inherited ? "↳" : "≡";
      row.appendChild(grip);

      const nm = document.createElement("span");
      nm.className = "nm";
      const auto = ED.baseOf(w);
      nm.textContent = ED.displayName(w);
      nm.title = "#" + w.uid + (w.name ? " · " + auto : "") +
        (inherited ? T(" (상속)") : "");
      row.appendChild(nm);

      const eye = document.createElement("span");
      eye.className = "eye";
      eye.textContent = w.hidden ? "◌" : "●";
      eye.title = w.hidden ? T("표시") : T("숨김");
      eye.addEventListener("click", (e) => {
        e.stopPropagation();
        w.hidden = !w.hidden;
        ED.dirty = true; ED.uiDirty();
        ED.draw();
        this.render();
      });
      row.appendChild(eye);

      row.addEventListener("click", () => ED.setSel(i));

      if (!inherited) {
        row.addEventListener("dragstart", (e) => {
          this.dragUid = w.uid;
          e.dataTransfer.setData("text/layer", String(w.uid));
          e.dataTransfer.effectAllowed = "move";
        });
      }
      row.addEventListener("dragover", (e) => { e.preventDefault(); row.classList.add("over"); });
      row.addEventListener("dragleave", () => row.classList.remove("over"));
      row.addEventListener("drop", (e) => {
        e.preventDefault();
        row.classList.remove("over");
        const from = this.dragUid || parseInt(e.dataTransfer.getData("text/layer"), 10);
        if (!from) return;
        this.reorder(from, parseInt(row.dataset.puid, 10));
      });

      body.appendChild(row);
    }
  },

  /* uid 기준으로 own-layer 목록을 재정렬하고 z를 정규화 (아래 = 앞) */
  reorder(dragUid, targetUid) {
    if (dragUid === targetUid) return;
    const own = ED.listOrder().filter((x) => !x.w.parent);
    const from = own.findIndex((x) => x.w.uid === dragUid);
    const to = own.findIndex((x) => x.w.uid === targetUid);
    if (from < 0 || to < 0) return;

    const [m] = own.splice(from, 1);
    own.splice(to, 0, m);

    /* 목록의 마지막이 가장 앞(높은 레이어) */
    own.forEach((x, i) => { x.w.z = i; });

    ED.dirty = true;
    ED.uiDirty();
    ED.draw();
    this.render();
  },
};

/* const 는 window 에 붙지 않으므로 명시적으로 노출 */
window.LayerView = LayerView;

