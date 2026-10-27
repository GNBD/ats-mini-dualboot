/*
 * SPDX-License-Identifier: MIT
 * catalog.js - UI modules, templates, bindings, mock data.
 */
const PALETTE = {
  bg: "#080c10", panel: "#191821", hl: "#102842", hsel: "#19416b",
  cell: "#101419", line: "#29313a", track: "#212831", fg: "#f7f3f7",
  muted: "#8c8e9c", accent: "#0886ff", accenth: "#19db94", gold: "#ff9e10",
  ok: "#31d25a", warn: "#ff453a", red: "#ff0000", blue: "#2128ff",
};

function W(o) { return Object.assign({ t: "rect", x: 0, y: 0, w: 10, h: 10 }, o); }

const MODULES = [
  { id: "headerfull", cat: "UI", name: "Header + Wi-Fi", sub: "clock / wifi / battery",
    colors: ["panel", "accent", "fg", "muted", "accenth", "ok"],
    make: () => [
      W({ t: "rect", x: 0, y: 0, w: 320, h: 22, fill: "panel" }),
      W({ t: "rect", x: 6, y: 6, w: 3, h: 10, fill: "accent" }),
      W({ t: "text", x: 12, y: 6, w: 44, h: 10, bind: "clock", size: "tiny", color: "fg" }),
      W({ t: "text", x: 186, y: 6, w: 40, h: 10, bind: "wifi", size: "tiny", color: "muted", align: "l" }),
      W({ t: "text", x: 226, y: 6, w: 56, h: 10, bind: "battv", size: "tiny", color: "muted", align: "l" }),
      W({ t: "wifi", x: 166, y: 5, w: 18, h: 11, color: "accenth" }),
      W({ t: "batt_icon", x: 292, y: 5, w: 26, h: 11, bind: "batt", max: 100, fill: "panel", stroke: "line", color: "ok" }),
      W({ t: "line", x: 0, y: 22, w: 320, h: 1, color: "panel" }),
    ] },
  { id: "bandfull", cat: "UI", name: "Band + Mode + ST", sub: "firmware meta row",
    colors: ["muted", "ok"],
    make: () => [
      W({ t: "text", x: 12, y: 24, w: 30, h: 12, bind: "band", size: "small", color: "muted" }),
      W({ t: "text", x: 44, y: 24, w: 30, h: 12, bind: "mode", size: "small", color: "muted" }),
      W({ t: "text", x: 76, y: 24, w: 40, h: 10, bind: "stereo", size: "tiny", color: "ok" }),
    ] },
  { id: "band", cat: "UI", name: "Band + Mode", sub: "VHF / FM",
    colors: ["muted"],
    make: () => [
      W({ t: "text", x: 12, y: 27, w: 28, h: 12, bind: "band", size: "tiny", color: "muted" }),
      W({ t: "text", x: 42, y: 27, w: 28, h: 12, bind: "mode", size: "tiny", color: "muted" }),
    ] },
  { id: "badges", cat: "UI", name: "P / A badges", sub: "preset + ATS",
    colors: ["accent", "warn"],
    make: () => [
      W({ t: "text", x: 120, y: 24, w: 34, h: 12, bind: "preset", size: "tiny", color: "accent", align: "c", chip: 1 }),
      W({ t: "text", x: 158, y: 24, w: 34, h: 12, bind: "ats", size: "tiny", color: "warn", align: "c", chip: 1 }),
    ] },
  { id: "freq", cat: "UI", name: "Frequency", sub: "large + unit",
    colors: ["fg", "muted"],
    make: () => [ W({ t: "freq", x: 12, y: 40, w: 200, h: 32 }) ] },
  { id: "rsq", cat: "UI", name: "RSSI / SNR", sub: "bars + numbers",
    colors: ["track", "ok", "accent", "warn", "muted", "fg"],
    make: () => [ W({ t: "rsq", x: 8, y: 72, w: 176, h: 44 }) ] },
  { id: "params", cat: "UI", name: "Param grid", sub: "BW/ST/BFO/AGC/AFC...",
    colors: ["muted"],
    make: () => [
      W({ t: "text", x: 188, y: 74, w: 122, h: 10, bind: "line1", size: "tiny", color: "muted" }),
      W({ t: "text", x: 188, y: 90, w: 122, h: 10, bind: "line2", size: "tiny", color: "muted" }),
      W({ t: "text", x: 188, y: 106, w: 122, h: 10, bind: "line3", size: "tiny", color: "muted" }),
    ] },
  { id: "rds", cat: "UI", name: "INFO card (RDS/EIBI)", sub: "mode-aware info",
    colors: ["panel", "line", "accenth", "fg", "muted", "warn"],
    make: () => [ W({ t: "rds", x: 8, y: 122, w: 200, h: 46 }) ] },
  { id: "sig", cat: "UI", name: "SIG graph", sub: "RSSI/SNR history",
    colors: ["panel", "line", "track", "ok", "accent"],
    make: () => [ W({ t: "sig", x: 212, y: 122, w: 98, h: 46 }) ] },
  { id: "bandmap", cat: "UI", name: "Band map", sub: "centered dial",
    colors: ["line", "bg", "fg", "muted", "accent"],
    make: () => [ W({ t: "bandmap", x: 8, y: 122, w: 304, h: 46 }) ] },
  { id: "panel", cat: "UI", name: "PANEL (MENU/SETTINGS)", sub: "가이드 · 가로/세로",
    colors: ["panel", "line", "hsel", "accenth", "fg"],
    make: () => [
      W({ t: "menu", kind: "menu", orient: "v", x: 212, y: 18, w: 98, h: 150 }),
    ] },
  { id: "status", cat: "UI", name: "VOL / BRT status", sub: "volume + brightness",
    colors: ["muted"],
    make: () => [
      W({ t: "text", x: 228, y: 24, w: 84, h: 10, bind: "status", size: "tiny", color: "muted", align: "l" }),
    ] },
  { id: "decode", cat: "APP", name: "DECODE panel", sub: "decode status / level / text",
    colors: ["panel", "line", "accent", "ok", "fg", "muted"],
    make: () => [ W({ t: "decode", x: 6, y: 72, w: 308, h: 96, fill: "panel", stroke: "line", color: "accent" }) ] },
  { id: "sleep", cat: "UI", name: "Sleep remaining", sub: "SLEEP seconds",
    colors: ["accent"],
    make: () => [ W({ t: "text", x: 240, y: 42, w: 72, h: 10, bind: "sleep", size: "tiny", color: "accent", align: "r" }) ] },
  { id: "batt_icon", cat: "UI", name: "Quad bar (battery)", sub: "square gauge / generic bar",
    colors: ["ok", "warn", "accent", "panel", "line"],
    make: () => [ W({ t: "batt_icon", x: 292, y: 5, w: 28, h: 11, bind: "batt", max: 100,
                      fill: "panel", stroke: "line", color: "ok" }) ] },
  { id: "decode", cat: "APP", name: "DECODE panel", sub: "set GPIO/mode in properties",
    colors: ["panel", "line", "accent", "ok", "fg", "muted"],
    make: () => [ W({ t: "decode", x: 6, y: 72, w: 308, h: 96, fill: "panel", stroke: "line",
                      color: "accent", text: "gpio=4;mode=0;lines=4" }) ] },
  { id: "wifisig", cat: "BASIC", name: "Signal icon", sub: "4-bar wifi level · free placement",
    colors: ["accenth"],
    make: () => [ W({ t: "wifi", x: 286, y: 4, w: 20, h: 11, color: "accenth" }) ] },
  { id: "text", cat: "BASIC", name: "Text", sub: "binding or literal",
    colors: ["color"], /* 기본 fg */
    make: () => [ W({ t: "text", x: 16, y: 70, w: 90, h: 12, text: "TEXT", size: "tiny", color: "fg" }) ] },
  { id: "bar", cat: "BASIC", name: "Bar", sub: "bound 0..max",
    colors: ["color", "track"],
    make: () => [ W({ t: "bar", x: 52, y: 78, w: 88, h: 8, bind: "rssi", max: 63, color: "ok" }) ] },
  { id: "rect", cat: "BASIC", name: "Rect / Panel", sub: "fill + border",
    colors: ["fill", "stroke"],
    make: () => [ W({ t: "rect", x: 20, y: 60, w: 120, h: 40, fill: "panel", stroke: "line" }) ] },
  { id: "line", cat: "BASIC", name: "Line", sub: "divider",
    colors: ["color"],
    make: () => [ W({ t: "line", x: 8, y: 118, w: 304, h: 1, color: "line" }) ] },
  { id: "toast", cat: "BASIC", name: "Toast", sub: "bottom message",
    colors: ["panel", "line", "fg"],
    make: () => [
      W({ t: "rect", x: 60, y: 140, w: 200, h: 18, fill: "panel", stroke: "line", r: 9 }),
      W({ t: "text", x: 70, y: 145, w: 180, h: 10, bind: "toast", size: "tiny", color: "fg", align: "c" }),
    ] },
];

const TEMPLATES = [
  { id: "tpl-default", name: "Default (basic UI)", sub: "firmware default screen",
    ids: ["headerfull", "bandfull", "freq", "rsq", "params", "rds", "sig", "status"] },
  { id: "tpl-bandmap", name: "Radio + Band map", sub: "full-width band map",
    ids: ["headerfull", "band", "badges", "freq", "rsq", "params", "bandmap"] },
  { id: "tpl-minimal", name: "Minimal", sub: "frequency + RSSI only",
    ids: ["headerfull", "freq", "rsq"] },
];

const MENU_LABELS = [
  "< BACK", "BAND", "MODE", "STEP", "BW", "BFO", "SEEK", "SCAN", "ATS",
  "FAVORITES", "RDS", "STEREO", "DEEMPH", "VOLUME", "AGC", "RF GAIN",
  "AVC", "SOFTMUTE", "SQUELCH", "SETTINGS",
];

const SET_LABELS = [
  "< BACK", "BRIGHT", "SCREEN", "SLEEP", "POWER SAVE", "UNIT", "THEME",
  "TIME", "UTC", "BATT", "BATT CAL", "CPU", "WI-FI", "EIBI", "QUICK SET",
  "SIG DBG", "BOTTOM", "ABOUT",
];

const BINDINGS = [
  ["freq", "Frequency (string)"], ["mode", "Mode FM/AM/LSB/USB"], ["band", "Band name"],
  ["rssi", "RSSI 0-63"], ["snr", "SNR 0-63"], ["clock", "Clock HH:MM"],
  ["date", "Date"], ["ps", "RDS PS"], ["rt", "RDS RT"], ["pty", "RDS PTY"],
  ["preset", "Preset number"], ["pname", "Preset name"], ["ats", "ATS number"],
  ["batt", "Battery %"], ["vol", "Volume"], ["bw", "Bandwidth"], ["step", "Step"],
  ["bfo", "BFO"], ["agc", "AGC"], ["sm", "Softmute"], ["afc", "AFC"],
  ["avc", "AVC"], ["sq", "Squelch"], ["stereo", "Stereo"],
  ["line1", "BW/ST"], ["line2", "BFO/AGC/SM"], ["line3", "AFC/AVC/SQ"],
  ["eibi", "EiBi station"], ["toast", "Toast"], ["wifi", "Wi-Fi state"],
  ["status", "VOL/BRT status"], ["battv", "Battery voltage"],
  ["sleep", "Sleep remaining (s)"], ["dec_level", "Decode level 0-100"],
];

const MOCK = {
  freq: 89100000, mode: "FM", band: "VHF", rssi: 35, snr: 9,
  clock: "14:32", date: "2026-10-26", ps: "KBS 1FM",
  rt: "뉴스와 음악, 그리고 사람", pty: "Varied", preset: "P01",
  pname: "KBS 1FM", ats: "A07", batt: 87, vol: 22, bw: "AUTO", step: "100K",
  bfo: "+0", agc: "ON", sm: "0", afc: "OK", avc: "63", sq: "OFF",
  stereo: "ST", eibi: "KBS World Radio 19:00", toast: "SAVED",
  wifi: "STA", wifiBars: 4, status: "VOL 22  BRT 50", battv: "4.36V",
  line1: "BW AUTO  ST 100K", line2: "BFO +0  AGC ON  SM 4",
  line3: "AFC OK  AVC 63  SQ OFF",
};
