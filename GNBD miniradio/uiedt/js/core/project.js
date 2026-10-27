/*
 * SPDX-License-Identifier: MIT
 * project.js - UI-only project normalization.
 * Accepts legacy .mrpack files and keeps only the ui section.
 */
(function (root, factory) {
  const S = (typeof require === "function") ? require("./schema.js") : root.MRSchema;
  const api = factory(S);
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  if (root) root.MRProject = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function (S) {
  function str(v, fb) { return typeof v === "string" ? v : (fb || ""); }
  function clone(v) { return v == null ? v : JSON.parse(JSON.stringify(v)); }

  function normalize(pack, fallbackManifest) {
    pack = pack || {};
    const legacy = pack.format !== S.FORMAT;
    const ui = pack.ui || {};
    const m = pack.manifest || {};

    let layouts = null;
    if (ui.layouts && typeof ui.layouts === "object") {
      layouts = {};
      for (const k of S.LAYOUTS) {
        layouts[k] = Array.isArray(ui.layouts[k]) ? clone(ui.layouts[k]) : [];
      }
    }
    const activeMode = S.LAYOUTS.indexOf(ui.activeMode) >= 0 ? ui.activeMode : "FM";

    let palette = null;
    if (ui.palette && typeof ui.palette === "object") {
      palette = {};
      for (const k of S.PALETTE_KEYS) {
        if (typeof ui.palette[k] === "string") palette[k] = ui.palette[k];
      }
    }

    return {
      format: S.FORMAT,
      schemaVersion: S.SCHEMA_VERSION,
      migratedFrom: legacy ? (pack.format || "unknown") : null,
      manifest: {
        name: str(m.name, (fallbackManifest && fallbackManifest.name) || "my-ui"),
        version: str(m.version, "0.1.0"),
        author: str(m.author),
        license: str(m.license, "MIT"),
        license_accepted: !!(m.license_accepted ||
          (fallbackManifest && fallbackManifest.license_accepted)),
        created: str(m.created),
        generator: str(m.generator, "MINI-radio UI Editor"),
      },
      target: {
        board: str((pack.target && pack.target.board), "ESP32-S3-N16R8"),
        firmware: str((pack.target && pack.target.firmware), "GNBD ATS Mini"),
      },
      ui: {
        bg: str(ui.bg, "bg"),
        items: Array.isArray(ui.items) ? clone(ui.items) : [],
        layouts,
        activeMode,
        palette,
      },
      meta: pack.meta && typeof pack.meta === "object" ? clone(pack.meta) : {},
    };
  }

  return { normalize };
});
