/*
 * SPDX-License-Identifier: MIT
 * radio.js - talk to the firmware UI storage API.
 *
 * Endpoints (main/ui_web.c):
 *   GET  /api/ui/list
 *   POST /api/ui/save?name=&overwrite=
 *   GET  /api/ui/project?name=
 *   POST /api/ui/project?name=
 *   POST /api/ui/activate?name=
 *   POST /api/ui/delete?name=
 */
const Radio = {
  base: "/api/ui",
  current: null,

  nameOk(name) {
    return /^[A-Za-z0-9_-]{1,24}$/.test(name || "");
  },

  async json(res) {
    let data = null;
    try { data = await res.json(); } catch (e) { data = null; }
    if (!res.ok) {
      const msg = data && (data.error || data.message) ? (data.error || data.message) : ("HTTP " + res.status);
      throw new Error(msg);
    }
    return data;
  },

  async list() {
    const res = await fetch(this.base + "/list", { cache: "no-store" });
    return this.json(res);
  },

  async save(project, name, overwrite) {
    if (!this.nameOk(name)) throw new Error("invalid name");
    const layout = MRRuntime.generate(project);
    const q = "?name=" + encodeURIComponent(name) + (overwrite ? "&overwrite=1" : "");
    let res = await fetch(this.base + "/save" + q, {
      method: "POST",
      headers: { "Content-Type": "text/plain" },
      body: layout,
    });
    await this.json(res);
    res = await fetch(this.base + "/project?name=" + encodeURIComponent(name), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(project),
    });
    await this.json(res);
    res = await fetch(this.base + "/activate?name=" + encodeURIComponent(name), {
      method: "POST",
      body: "",
    });
    await this.json(res);
    this.current = name;
    return true;
  },

  async load(name) {
    if (!this.nameOk(name)) throw new Error("invalid name");
    const res = await fetch(this.base + "/project?name=" + encodeURIComponent(name),
      { cache: "no-store" });
    const pack = await this.json(res);
    if (!ED.load(pack)) throw new Error("project rejected");
    this.current = name;
    if (window.Colors) Colors.render();
    await this.activate(name);
    return true;
  },

  async activate(name) {
    if (!this.nameOk(name)) throw new Error("invalid name");
    const res = await fetch(this.base + "/activate?name=" + encodeURIComponent(name),
      { method: "POST", body: "" });
    await this.json(res);
    this.current = name;
    return true;
  },

  async remove(name) {
    if (!this.nameOk(name)) throw new Error("invalid name");
    const res = await fetch(this.base + "/delete?name=" + encodeURIComponent(name),
      { method: "POST", body: "" });
    await this.json(res);
    if (this.current === name) this.current = null;
    return true;
  },
};

window.Radio = Radio;
