/*
 * SPDX-License-Identifier: MIT
 * export.js - UI project save/load + layout C export.
 */
Object.assign(ED, {
  pack() {
    if (this.project.layouts) this.syncLayout();
    if (window.Colors) Colors.palette();
    const base = {
      format: MRSchema.FORMAT,
      schemaVersion: MRSchema.SCHEMA_VERSION,
      manifest: {
        name: (document.getElementById("projName").value || "my-ui").trim(),
        version: "0.1.0",
        author: "",
        license: "MIT",
        license_accepted: true,
        created: new Date().toISOString(),
        generator: "MINI-radio UI Editor v1.0",
      },
      target: { board: "ESP32-S3-N16R8", firmware: "GNBD ATS Mini" },
      ui: {
        bg: this.project.bg,
        items: this.project.items,
        layouts: this.project.layouts || null,
        activeMode: this.activeLayout(),
        palette: this.project.palette || null,
      },
      meta: {},
    };
    return MRProject.normalize(base);
  },

  load(pack) {
    if (!pack || (!pack.ui && !pack.manifest)) return false;
    this.lastLoadErrors = null;
    const chk = MRValidate.validateProject(pack, {});
    if (!chk.ok) {
      this.lastLoadErrors = chk.errors;
      return false;
    }
    this.snapshot();
    const n = chk.project;
    const ui = n.ui;
    const layouts = ui.layouts || null;
    const active = (layouts && MRSchema.LAYOUTS.indexOf(ui.activeMode) >= 0)
      ? ui.activeMode : "FM";
    this.project = {
      bg: ui.bg || "bg",
      items: layouts ? (layouts[active] || []) : (ui.items || []),
      layouts,
      activeMode: active,
      palette: ui.palette || null,
      meta: {},
    };
    this.project.items.forEach((w) => this.ensureUid(w));
    if (layouts) {
      for (const k of this.layoutKeys) {
        (layouts[k] || []).forEach((w) => this.ensureUid(w));
      }
    }
    this.pmode = Math.max(0, this.layoutKeys.indexOf(active));
    if (window.Colors) Colors.palette();
    document.getElementById("projName").value = n.manifest.name || "my-ui";
    this.setSel(-1);
    this.dirty = false;
    this.uiDirty();
    return true;
  },

  exportPack() {
    const pack = this.pack();
    const blob = new Blob([JSON.stringify(pack, null, 2)], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = (pack.manifest.name || "ui") + ".mrpack";
    a.click();
    URL.revokeObjectURL(a.href);
    this.dirty = false;
    this.uiDirty();
  },

  exportC() {
    return MRLayoutC.generate(this.pack()).content;
  },

  downloadC() {
    const blob = new Blob([this.exportC()], { type: "text/plain" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "ui_custom_layout.h";
    a.click();
    URL.revokeObjectURL(a.href);
    this.dirty = false;
    this.uiDirty();
  },
});
