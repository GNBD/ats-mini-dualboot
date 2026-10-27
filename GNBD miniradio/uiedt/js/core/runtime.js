/*
 * SPDX-License-Identifier: MIT
 * runtime.js - compact runtime layout format for the firmware.
 *
 * Tab separated: type x y w h modes fill stroke color font align bind chip max <TAB> text
 * The firmware parser lives in main/ui_runtime.c.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  if (root) root.MRRuntime = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const TYPE = { rect: 0, text: 1, bar: 2, line: 3, freq: 4, rsq: 5, rds: 6, sig: 7, bandmap: 8, menu: 10, wifi: 11, decode: 12, batt_icon: 13 };
  const COLOR = { bg: 0, panel: 1, fg: 2, muted: 3, line: 4, accent: 5, accenth: 6,
    ok: 7, warn: 8, red: 9, track: 10, hsel: 11, none: 12 };
  const FONT = { tiny: 0, small: 1, large: 2 };
  const ALIGN = { l: 0, c: 1, r: 2 };
  const BIND = [
    "none", "freq", "mode", "band", "rssi", "snr", "clock", "date", "ps", "rt",
    "pty", "preset", "pname", "ats", "batt", "vol", "bw", "step", "bfo",
    "agc", "sm", "afc", "avc", "sq", "stereo", "line1", "line2", "line3",
    "eibi", "toast", "wifi", "status", "battv", "sleep", "dec_level",
  ];
  const PALETTE_KEYS = ["bg", "panel", "hl", "hsel", "cell", "line", "track",
    "fg", "muted", "accent", "accenth", "gold", "ok", "warn", "red", "blue"];

  function rgb565(hex) {
    const m = /^#?([0-9a-fA-F]{6})$/.exec(hex || "");
    if (!m) return 0;
    const n = parseInt(m[1], 16);
    const r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  }

  function layerOf(w, depth, pool) {
    depth = depth || 0;
    if (w.parent && depth < 5) {
      const p = pool.find((x) => x.uid === w.parent);
      if (p && p !== w) return layerOf(p, depth + 1, pool);
    }
    return w.z == null ? 0 : w.z;
  }
  function sortedFor(items) {
    return (items || [])
      .map((w, i) => ({ w, i, z: layerOf(w, 0, items) }))
      .sort((a, b) => (a.z - b.z) || (a.i - b.i))
      .map((x) => x.w);
  }
  function clean(s) {
    let v = String(s == null ? "" : s).replace(/[\t\r\n]+/g, " ");
    if (typeof TextEncoder !== "undefined") {
      const enc = new TextEncoder();
      while (enc.encode(v).length > 63) v = v.slice(0, -1);
    } else {
      v = v.slice(0, 63);
    }
    return v;
  }

  function line(w, forceMode) {
    const t = TYPE[w.t];
    if (t == null || w.hidden) return null;
    if (w.t === "menu") {
      return [10, Math.round(w.x), Math.round(w.y), Math.round(w.w), Math.round(w.h),
        7, 0, 0, 0, 0, w.orient === "h" ? 1 : 0, w.kind === "settings" ? 1 : 0,
        0, 0, 100].join("\t") + "\t";
    }
    const mode = forceMode || (w.modes == null ? 7 : w.modes);
    const bind = BIND.indexOf(w.bind);
    const events = w.events && typeof w.events === "object"
      ? Object.keys(w.events)
          .filter((k) => w.events[k])
          .map((k) => k + "=" + w.events[k])
          .join(",")
      : "";
    return [
      t, Math.round(w.x), Math.round(w.y), Math.round(w.w), Math.round(w.h),
      mode,
      COLOR[w.fill] != null ? COLOR[w.fill] : 0,
      COLOR[w.stroke] != null ? COLOR[w.stroke] : 0,
      COLOR[w.color] != null ? COLOR[w.color] : 0,
      FONT[w.font || w.size] != null ? FONT[w.font || w.size] : 0,
      ALIGN[w.align] != null ? ALIGN[w.align] : 0,
      bind >= 0 ? bind : 0,
      w.chip ? 1 : 0,
      w.max || 0,
      Math.max(50, Math.min(200, Math.round(w.scale || 100))),
      w.uid || 0,
      events,
    ].join("\t") + "\t" + clean(w.text);
  }

  function generate(project) {
    const out = ["UIR1"];
    const pal = project.ui && project.ui.palette;
    if (pal) {
      out.push("PAL\t" + PALETTE_KEYS.map((k) => rgb565(pal[k])).join("\t"));
    }
    const layouts = project.ui && project.ui.layouts;
    if (layouts) {
      const masks = { FM: 1, AM: 2, SSB: 4 };
      for (const k of ["FM", "AM", "SSB"]) {
        for (const w of sortedFor(layouts[k] || [])) {
          const ln = line(w, masks[k]);
          if (ln) out.push(ln);
        }
      }
    } else {
      for (const w of sortedFor((project.ui && project.ui.items) || [])) {
        const ln = line(w, 0);
        if (ln) out.push(ln);
      }
    }
    return out.join("\n") + "\n";
  }

  return { generate, TYPE, COLOR, FONT, ALIGN, BIND };
});
