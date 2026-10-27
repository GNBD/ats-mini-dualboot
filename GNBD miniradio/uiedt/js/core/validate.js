/*
 * SPDX-License-Identifier: MIT
 * validate.js - UI-only validation.
 */
(function (root, factory) {
  const S = (typeof require === "function") ? require("./schema.js") : root.MRSchema;
  const P = (typeof require === "function") ? require("./project.js") : root.MRProject;
  const api = factory(S, P);
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  if (root) root.MRValidate = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function (S, P) {
  function V() { this.errors = []; this.warnings = []; }
  V.prototype.err = function (code, message, path) {
    message = (globalThis.I18N && globalThis.I18N) ? globalThis.I18N.tm(message) : message;
    this.errors.push({ code, message, path: path || "", severity: "error" });
  };
  V.prototype.warn = function (code, message, path) {
    message = (globalThis.I18N && globalThis.I18N) ? globalThis.I18N.tm(message) : message;
    this.warnings.push({ code, message, path: path || "", severity: "warning" });
  };

  function isInt(v) { return typeof v === "number" && isFinite(v) && Math.floor(v) === v; }

  function checkWidget(v, w, i, base) {
    const p = base + "[" + i + "]";
    if (!w || typeof w !== "object") return v.err("E_WIDGET", "위젯이 객체가 아닙니다.", p);
    if (S.WIDGET_TYPES.indexOf(w.t) < 0) {
      return v.err("E_WIDGET_TYPE", "지원하지 않는 위젯 타입: " + w.t, p + ".t");
    }
    if (!isInt(w.x) || !isInt(w.y)) {
      v.err("E_WIDGET_XY", "x/y는 정수여야 합니다.", p);
    } else if (w.x < -320 || w.x > 640 || w.y < -170 || w.y > 340) {
      v.warn("W_OFFSCREEN", "위젯이 화면 밖에 있습니다.", p);
    }
    if (!isInt(w.w) || !isInt(w.h) || w.w <= 0 || w.h <= 0) {
      v.err("E_WIDGET_SIZE", "w/h는 1 이상의 정수여야 합니다.", p);
    } else if (w.w > 320 || w.h > 170) {
      v.err("E_WIDGET_SIZE", "위젯 크기가 화면(320x170)을 넘습니다.", p);
    }
    if (w.z != null && (!isInt(w.z) || w.z < -1000 || w.z > 1000)) {
      v.err("E_WIDGET_Z", "Layer는 -1000..1000 정수여야 합니다.", p + ".z");
    }
    for (const k of ["fill", "stroke", "color"]) {
      if (w[k] != null && S.COLORS.indexOf(w[k]) < 0) {
        v.err("E_WIDGET_COLOR", "허용되지 않는 색상: " + w[k], p + "." + k);
      }
    }
    if (w.font != null && S.FONTS.indexOf(w.font) < 0) {
      v.err("E_WIDGET_FONT", "허용되지 않는 폰트: " + w.font, p + ".font");
    }
    if (w.size != null && S.FONTS.indexOf(w.size) < 0) {
      v.err("E_WIDGET_FONT", "허용되지 않는 폰트 크기: " + w.size, p + ".size");
    }
    if (w.align != null && S.ALIGNS.indexOf(w.align) < 0) {
      v.err("E_WIDGET_ALIGN", "허용되지 않는 정렬: " + w.align, p + ".align");
    }
    if (w.bind != null && S.BINDS.indexOf(w.bind) < 0) {
      v.err("E_WIDGET_BIND", "허용되지 않는 바인딩: " + w.bind, p + ".bind");
    }
    if (w.modes != null && (!isInt(w.modes) || w.modes < 0 || w.modes > 7)) {
      v.err("E_WIDGET_MODES", "모드 마스크는 0..7이어야 합니다.", p + ".modes");
    }
    if (w.max != null && (!isInt(w.max) || w.max < 0 || w.max > 65535)) {
      v.err("E_WIDGET_MAX", "max는 0..65535 정수여야 합니다.", p + ".max");
    }
    if (w.text != null) {
      if (typeof w.text !== "string") {
        v.err("E_WIDGET_TEXT", "text는 문자열이어야 합니다.", p + ".text");
      } else if (S.byteLength(w.text) > S.LIMITS.textBytes) {
        v.err("E_WIDGET_TEXT", "text가 너무 깁니다.", p + ".text");
      }
    }
    if (w.t === "menu") {
      if (w.kind != null && w.kind !== "menu" && w.kind !== "settings") {
        v.err("E_MENU_KIND", "MENU/SETTINGS만 허용됩니다.", p + ".kind");
      }
      if (w.orient != null && S.ORIENTS.indexOf(w.orient) < 0) {
        v.err("E_MENU_ORIENT", "가로/세로만 허용됩니다.", p + ".orient");
      }
    }
    if (w.t === "text") {
      if (w.text == null && w.bind == null) {
        v.warn("W_TEXT_EMPTY", "텍스트도 바인딩도 없습니다.", p);
      }
    }
    if (w.uid != null && !isInt(w.uid)) {
      v.err("E_WIDGET_UID", "uid는 정수여야 합니다.", p + ".uid");
    }
    if (w.scale != null && (!isInt(w.scale) || w.scale < 50 || w.scale > 200)) {
      v.err("E_WIDGET_SCALE", "Scale은 50..200 정수여야 합니다.", p + ".scale");
    }
  }

  function checkLayers(v, items, base) {
    const byUid = new Map();
    const seen = new Set();
    items.forEach((w, i) => {
      if (!w || w.uid == null) return;
      if (seen.has(w.uid)) v.err("E_UID_DUP", "중복 uid: " + w.uid, base + "[" + i + "].uid");
      seen.add(w.uid);
      byUid.set(w.uid, w);
    });
    items.forEach((w, i) => {
      if (!w || !w.parent) return;
      const p = base + "[" + i + "].parent";
      if (!byUid.has(w.parent)) {
        v.err("E_PARENT", "상속 대상 uid가 없습니다: " + w.parent, p);
        return;
      }
      let cur = w, depth = 0;
      while (cur && cur.parent != null) {
        if (++depth > 32) {
          v.err("E_PARENT_CYCLE", "레이어 상속이 순환합니다.", p);
          break;
        }
        const next = byUid.get(cur.parent);
        if (next === w) {
          v.err("E_PARENT_CYCLE", "레이어 상속이 순환합니다.", p);
          break;
        }
        cur = next;
      }
    });
  }

  function validateProject(pack, opts) {
    opts = opts || {};
    const v = new V();
    if (!pack || typeof pack !== "object") {
      v.err("E_PROJECT", "프로젝트가 객체가 아닙니다.", "");
      return { ok: false, errors: v.errors, warnings: v.warnings, project: null };
    }

    const proj = P.normalize(pack, opts.manifest);
    if (pack.schemaVersion != null && pack.format === S.FORMAT &&
        pack.schemaVersion > S.SCHEMA_VERSION) {
      v.err("E_SCHEMA_FUTURE", "더 새로운 스키마입니다.", "schemaVersion");
    }
    if (pack.format && pack.format !== S.FORMAT &&
        pack.format !== S.LEGACY_FORMAT && pack.format !== "gnbd-mod-project") {
      v.err("E_FORMAT", "알 수 없는 형식: " + pack.format, "format");
    }
    if (proj.migratedFrom) {
      v.warn("W_MIGRATE", "이전 형식(" + proj.migratedFrom + ")에서 UI만 가져왔습니다.", "format");
    }
    if (S.byteLength(proj.manifest.name) > S.LIMITS.nameBytes) {
      v.err("E_NAME_LEN", "프로젝트 이름이 너무 깁니다.", "manifest.name");
    }
    if (proj.ui.palette) {
      for (const k of S.PALETTE_KEYS) {
        const c = proj.ui.palette[k];
        if (c != null && !/^#[0-9a-fA-F]{6}$/.test(c)) {
          v.err("E_PALETTE", "색상은 #rrggbb 형식이어야 합니다: " + c, "ui.palette." + k);
        }
      }
    }

    const sets = [["ui.items", proj.ui.items]];
    if (proj.ui.layouts) {
      for (const k of S.LAYOUTS) sets.push(["ui.layouts." + k, proj.ui.layouts[k] || []]);
    }
    let total = 0;
    for (const [key, items] of sets) {
      total += items.length;
      if (items.length > S.LIMITS.widgets) {
        v.err("E_LIMIT", "위젯이 너무 많습니다 (최대 " + S.LIMITS.widgets + ").", key);
      }
      items.forEach((w, i) => checkWidget(v, w, i, key));
      checkLayers(v, items, key);
    }
    if (total > S.LIMITS.widgets * 3) {
      v.err("E_LIMIT", "전체 위젯 수가 너무 많습니다.", "ui");
    }

    return { ok: v.errors.length === 0, errors: v.errors, warnings: v.warnings, project: proj };
  }

  return { validateProject };
});
