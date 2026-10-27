/*
 * SPDX-License-Identifier: MIT
 * schema.js - UI-only project schema (no blocks/menus/code).
 */
(function (root, factory) {
  const api = factory();
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  if (root) root.MRSchema = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const FORMAT = "gnbd-ui-project";
  const LEGACY_FORMAT = "mrpack/1";
  const SCHEMA_VERSION = 1;

  const LIMITS = {
    widgets: 512,
    nameBytes: 64,
    textBytes: 64,
    menuItems: 0,
  };

  const WIDGET_TYPES = [
    "rect", "text", "bar", "line", "freq", "rsq", "rds", "sig", "bandmap",
    "menu", "wifi", "decode", "batt_icon",
  ];

  const COLORS = [
    "bg", "panel", "fg", "muted", "line", "accent", "accenth",
    "ok", "warn", "red", "track", "hsel", "none",
  ];  const PALETTE_KEYS = ["bg", "panel", "hl", "hsel", "cell", "line", "track",
    "fg", "muted", "accent", "accenth", "gold", "ok", "warn", "red", "blue"];

  const FONTS = ["tiny", "small", "large"];
  const ALIGNS = ["l", "c", "r"];
  const LAYOUTS = ["FM", "AM", "SSB"];
  const ORIENTS = ["v", "h"];

  const BINDS = [
    "freq", "mode", "band", "rssi", "snr", "clock", "date", "ps", "rt",
    "pty", "preset", "pname", "ats", "batt", "vol", "bw", "step", "bfo",
    "agc", "sm", "afc", "avc", "sq", "stereo", "line1", "line2", "line3",
    "eibi", "toast", "wifi", "status", "battv",
  ];

  function isAscii(s) {
    return /^[\x00-\x7F]*$/.test(s);
  }

  function byteLength(s) {
    if (typeof TextEncoder !== "undefined") return new TextEncoder().encode(s).length;
    return Buffer.byteLength(String(s), "utf8");
  }

  return {
    FORMAT, LEGACY_FORMAT, SCHEMA_VERSION, LIMITS,
    WIDGET_TYPES, COLORS, PALETTE_KEYS, FONTS, ALIGNS, LAYOUTS, BINDS, ORIENTS,
    isAscii, byteLength,
  };
});
