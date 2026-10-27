/*
 * SPDX-License-Identifier: MIT
 * recent_colors.js - 최근 사용 색상 저장/표시.
 */
const RecentColors = {
  key: "uiedt.recent_colors",
  max: 12,

  list() {
    try {
      const v = JSON.parse(localStorage.getItem(this.key) || "[]");
      return Array.isArray(v) ? v.filter((x) => typeof x === "string") : [];
    } catch (e) {
      return [];
    }
  },

  add(c) {
    if (!c) return;
    const l = this.list().filter((x) => x !== c);
    l.unshift(c);
    try {
      localStorage.setItem(this.key, JSON.stringify(l.slice(0, this.max)));
    } catch (e) { /* ignore */ }
  },

  /* 기본 팔레트 키 + 최근 사용(★) */
  options(base) {
    const recent = this.list().filter((c) => base.indexOf(c) < 0);
    return base.concat(recent.map((c) => [c, "★ " + c]));
  },
};
