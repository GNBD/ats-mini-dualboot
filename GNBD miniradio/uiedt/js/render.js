/*
 * SPDX-License-Identifier: MIT
 * Canvas renderer. Mirrors the firmware primitive renderer:
 *   rect / text / bar / line / freq / rsq / rds / sig / bandmap
 * Geometry is in 320x170 logical pixels (canvas is scaled 2x).
 */
const BANDS = {
  VHF:  { min: 64000000,  max: 108000000, step: 100000, fm: true },
  MW:   { min: 520000,    max: 1710000,   step: 1000 },
  LW:   { min: 153000,    max: 279000,    step: 1000 },
  SW:   { min: 2300000,   max: 26100000,  step: 5000 },
  "160m": { min: 1800000, max: 2000000,   step: 1000 },
  "80m":  { min: 3500000, max: 4000000,   step: 1000 },
  "40m":  { min: 7000000, max: 7300000,   step: 1000 },
  "30m":  { min: 10100000, max: 10150000, step: 1000 },
  "20m":  { min: 14000000, max: 14350000, step: 1000 },
  "17m":  { min: 18068000, max: 18168000, step: 1000 },
  "15m":  { min: 21000000, max: 21450000, step: 1000 },
  "12m":  { min: 24890000, max: 24990000, step: 1000 },
  "10m":  { min: 28000000, max: 29700000, step: 1000 },
  CB:     { min: 26965000, max: 27405000, step: 1000 },
};

const FONTS = { tiny: 8, small: 16, large: 32 };
const FONT_FAMILY = '"DejaVu Sans Mono","Consolas",monospace';

function fmtFreq(hz, mode) {
  if (mode === "FM") {
    return (hz / 1e6).toFixed(2);
  }
  return String(Math.round(hz / 1000));
}
function fmtUnit(mode) { return mode === "FM" ? "MHz" : "kHz"; }
function fmtShort(hz, band) {
  const b = BANDS[band] || BANDS.VHF;
  return b.fm ? (hz / 1e6).toFixed(2) : String(Math.round(hz / 1000));
}

const Render = {
  color(name) {
    const p = (window.ED && ED.project && ED.project.palette) || PALETTE;
    return p[name] || PALETTE[name] || PALETTE.fg;
  },

  text(ctx, x, y, w, h, str, opt) {
    const pct = opt.scale == null ? 100 : Math.max(50, Math.min(200, opt.scale));
    const size = (FONTS[opt.size || "tiny"] || 8) * pct / 100;
    ctx.font = `${opt.bold ? "600 " : ""}${size}px ${FONT_FAMILY}`;
    /* 기기 폰트 진폭에 맞춤: tiny 6px/char, small 8px/char */
    if ("letterSpacing" in ctx) {
      ctx.letterSpacing = opt.size === "tiny" ? "1.2px"
                        : opt.size === "small" ? "-1.2px" : "0px";
    }
    ctx.fillStyle = this.color(opt.color || "fg");
    ctx.textBaseline = "top";
    ctx.textAlign = opt.align === "r" ? "right" : opt.align === "c" ? "center" : "left";
    const tx = opt.align === "r" ? x + w : opt.align === "c" ? x + w / 2 : x;
    let s = str == null ? "" : String(str);
    if (!opt.noFit) {
      while (s.length && ctx.measureText(s).width > (w || 0)) {
        s = s.slice(0, -1);
      }
    }
    if (opt.chip) {
      const tw = ctx.measureText(s).width + 10;
      ctx.fillStyle = this.color(opt.chipColor || "hsel");
      this.roundRect(ctx, tx - tw / 2, y - 1, tw, size + 4, 5);
      ctx.fill();
      ctx.fillStyle = this.color(opt.color || "fg");
    }
    ctx.fillText(s, tx, y);
    ctx.textAlign = "left";
    if ("letterSpacing" in ctx) ctx.letterSpacing = "0px";
  },

  roundRect(ctx, x, y, w, h, r) {
    r = Math.min(r, h / 2, w / 2);
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  },

  bar(ctx, x, y, w, h, val, max, color) {
    ctx.fillStyle = this.color("track");
    this.roundRect(ctx, x, y, w, h, h / 2); ctx.fill();
    const p = Math.max(0, Math.min(1, val / (max || 1)));
    if (p > 0) {
      ctx.fillStyle = this.color(color || "ok");
      this.roundRect(ctx, x, y, Math.max(2, w * p), h, h / 2); ctx.fill();
    }
  },

  widget(ctx, wg, s) {
    const c = this.color.bind(this);
    switch (wg.t) {
      case "rect": {
        if (wg.fill && wg.fill !== "none") {
          ctx.fillStyle = c(wg.fill);
          if (wg.r) { this.roundRect(ctx, wg.x, wg.y, wg.w, wg.h, wg.r); ctx.fill(); }
          else ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        }
        if (wg.stroke && wg.stroke !== "none") {
          ctx.strokeStyle = c(wg.stroke);
          ctx.lineWidth = 1;
          if (wg.r) { this.roundRect(ctx, wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1, wg.r); ctx.stroke(); }
          else ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        }
        break;
      }
      case "line":
        ctx.fillStyle = c(wg.color || "line");
        ctx.fillRect(wg.x, wg.y, wg.w, Math.max(1, wg.h));
        break;
      case "text": {
        const v = wg.bind ? (s[wg.bind] != null ? s[wg.bind] : "") : wg.text;
        this.text(ctx, wg.x, wg.y, wg.w, wg.h, v, wg);
        break;
      }
      case "bar":
        this.bar(ctx, wg.x, wg.y, wg.w, wg.h, s[wg.bind] || 0, wg.max || 63, wg.color);
        break;
      case "freq": {
        const num = fmtFreq(s.freq, s.mode);
        const pct = wg.scale == null ? 100 : wg.scale;
        this.text(ctx, wg.x + 2, wg.y, wg.w, wg.h, num,
          { size: "large", color: "fg", noFit: true, scale: pct });
        ctx.font = `${32 * pct / 100}px ${FONT_FAMILY}`;
        if ("letterSpacing" in ctx) ctx.letterSpacing = "0px";
        const tw = ctx.measureText(num).width;
        this.text(ctx, wg.x + 6 + tw, wg.y + 10 * pct / 100, 40, 14,
          fmtUnit(s.mode), { size: "small", color: "muted", scale: pct });
        break;
      }
      case "rsq": {
        /* 기기 좌표: 라벨 12,74/94 · 바 52,78/98 (88px) · 값 우측 174 */
        const bw = wg.w - 88;
        this.text(ctx, wg.x + 4, wg.y + 2, 36, 10, "RSSI", { size: "tiny", color: "muted", scale: wg.scale });
        this.bar(ctx, wg.x + 44, wg.y + 6, bw, 8, s.rssi, 63, "ok");
        this.text(ctx, wg.x + wg.w - 36, wg.y + 1, 26, 12, s.rssi,
          { size: "small", color: "fg", align: "r", scale: wg.scale });
        this.text(ctx, wg.x + 4, wg.y + 22, 36, 10, "SNR", { size: "tiny", color: "muted", scale: wg.scale });
        this.bar(ctx, wg.x + 44, wg.y + 26, bw, 8, s.snr, 63, s.snr < 10 ? "warn" : "accent");
        this.text(ctx, wg.x + wg.w - 36, wg.y + 21, 26, 12, s.snr,
          { size: "small", color: s.snr < 10 ? "warn" : "fg", align: "r", scale: wg.scale });
        break;
      }
      case "rds": {
        ctx.fillStyle = c("panel");
        ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        ctx.strokeStyle = c("line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        if (s.mode === "FM") {
          this.text(ctx, wg.x + 4, wg.y + 3, 40, 10, "RDS", { size: "tiny", color: "accenth" });
          this.text(ctx, wg.x + wg.w - 66, wg.y + 3, 60, 10, s.pty,
            { size: "tiny", color: "muted", align: "r" });
          this.text(ctx, wg.x + 4, wg.y + 16, wg.w - 10, 12, s.ps || "--",
            { size: "tiny", color: "fg" });
          this.text(ctx, wg.x + 4, wg.y + 30, wg.w - 10, 10, s.rt,
            { size: "tiny", color: "muted" });
        } else {
          this.text(ctx, wg.x + 4, wg.y + 3, 40, 10, "EIBI", { size: "tiny", color: "warn" });
          this.text(ctx, wg.x + wg.w - 6, wg.y + 3, 60, 10, s.clock,
            { size: "tiny", color: "muted", align: "r" });
          this.text(ctx, wg.x + 4, wg.y + 16, wg.w - 10, 12, s.eibi || "NO DATA",
            { size: "tiny", color: "fg" });
        }
        break;
      }
      case "sig": {
        ctx.fillStyle = c("panel");
        ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        ctx.strokeStyle = c("line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        this.text(ctx, wg.x + 4, wg.y + 3, 30, 10, "SIG", { size: "tiny", color: "muted" });
        const gx = wg.x + 4, gy = wg.y + 14, gw = wg.w - 8, gh = wg.h - 18;
        ctx.fillStyle = c("track");
        ctx.fillRect(gx + gw / 2, gy, 1, gh);
        ctx.fillStyle = c("line");
        ctx.fillRect(gx, gy + gh - 1, gw, 1);
        const h = s.hist || [];
        if (h.length > 1) {
          this.poly(ctx, h, gx, gy, gw, gh, "rssi", "ok");
          this.poly(ctx, h, gx, gy, gw, gh, "snr", "accent");
        }
        break;
      }
      case "batt_icon": {
        const max = wg.max || 100;
        const val = Math.max(0, Math.min(max, Number(s[wg.bind]) || 0));
        ctx.fillStyle = c(wg.fill || "panel");
        ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        ctx.strokeStyle = c(wg.stroke || "line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        if (!(wg.text && String(wg.text).indexOf("noterm") >= 0)) {
          ctx.fillStyle = c(wg.stroke || "line");
          ctx.fillRect(wg.x + wg.w, wg.y + Math.round(wg.h / 3), 2,
                       Math.max(1, Math.round(wg.h / 3)));
        }
        ctx.fillStyle = c(wg.color || "ok");
        ctx.fillRect(wg.x + 1, wg.y + 1, (wg.w - 2) * val / (max || 1), wg.h - 2);
        break;
      }
      case "decode": {
        ctx.fillStyle = c(wg.fill || "panel");
        ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        ctx.strokeStyle = c(wg.stroke || "line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        ctx.fillStyle = c(wg.color || "accent");
        ctx.font = font(wg.font, wg.size);
        ctx.textBaseline = "top";
        ctx.textAlign = "left";
        ctx.fillText("DECODE CW  GPIO 4", wg.x + 4, wg.y + 2);
        ctx.fillStyle = c("ok");
        ctx.textAlign = "right";
        ctx.fillText("RUN", wg.x + wg.w - 4, wg.y + 2);
        ctx.textAlign = "left";
        ctx.fillStyle = c("muted");
        ctx.fillText("SIG", wg.x + 4, wg.y + 14);
        ctx.fillStyle = c("track");
        ctx.fillRect(wg.x + 26, wg.y + 16, wg.w - 30, 4);
        ctx.fillStyle = c("ok");
        ctx.fillRect(wg.x + 26, wg.y + 16, (wg.w - 30) * 0.6, 4);
        ctx.fillStyle = c("fg");
        ctx.fillText("....", wg.x + 4, wg.y + 26);
        break;
      }
      case "wifi": {
        const bars = (wg.max != null) ? wg.max : (s.wifiBars == null ? 4 : s.wifiBars);
        if (bars <= 0) break;
        ctx.fillStyle = c(wg.color || "accenth");
        const bw = Math.max(2, Math.round(wg.w / 8));
        const gp = Math.max(1, Math.round(bw / 2));
        const total = 4 * bw + 3 * gp;
        const x0 = wg.x + Math.max(0, Math.round((wg.w - total) / 2));
        const rel = [3, 5, 7, 9];
        for (let i = 0; i < bars && i < 4; i++) {
          const h = Math.max(2, Math.round(wg.h * rel[i] / 9));
          ctx.fillRect(x0 + i * (bw + gp), wg.y + wg.h - h, bw, h);
        }
        break;
      }
      case "bandmap": {
        ctx.strokeStyle = c("line");
        ctx.strokeRect(wg.x + .5, wg.y + .5, wg.w - 1, wg.h - 1);
        /* chip */
        const tx = wg.x + wg.w / 2;
        const label = s.ps || s.eibi || "--";
        ctx.font = `8px ${FONT_FAMILY}`;
        const tw = ctx.measureText(label).width + 12;
        ctx.fillStyle = c("bg");
        this.roundRect(ctx, tx - tw / 2, wg.y + 2, tw, 13, 6); ctx.fill();
        this.text(ctx, tx - tw / 2 + 6, wg.y + 4, tw - 12, 10, label,
          { size: "tiny", color: "fg" });
        /* dial */
        const b = BANDS[s.band] || BANDS.VHF;
        const step = b.step;
        const cx = wg.x + wg.w / 2;
        const mw = wg.w - 20;
        const base = wg.y + wg.h - 17;
        const half = step * 10;
        const center = s.freq;
        ctx.fillStyle = c("line");
        ctx.fillRect(wg.x + 8, base, wg.w - 16, 1);
        let f0 = Math.floor((center - half) / step) * step;
        for (let f = f0; f <= center + half; f += step) {
          if (f < b.min || f > b.max) continue;
          const px = Math.round(cx + (f - center) * (mw / 2) / half);
          const major = Math.round(f / step) % 5 === 0;
          ctx.fillStyle = c(major ? "muted" : "line");
          ctx.fillRect(px, base - (major ? 8 : 4), 1, major ? 8 : 4);
          if (major && px > wg.x + 26 && px < wg.x + wg.w - 26) {
            this.text(ctx, px - 20, base + 3, 40, 10, fmtShort(f, s.band),
              { size: "tiny", color: "muted", align: "c" });
          }
        }
        /* needle */
        ctx.fillStyle = c("fg");
        ctx.fillRect(cx - 1, wg.y + 17, 2, base - (wg.y + 17) + 3);
        ctx.beginPath();
        ctx.moveTo(cx - 4, wg.y + 16); ctx.lineTo(cx + 4, wg.y + 16);
        ctx.lineTo(cx, wg.y + 22); ctx.closePath(); ctx.fill();
        break;
      }
      case "menu": {
        const kind = wg.kind === "settings" ? "SETTINGS" : "MENU";
        const labels = wg.kind === "settings" ? SET_LABELS : MENU_LABELS;
        ctx.fillStyle = this.color("panel");
        ctx.fillRect(wg.x, wg.y, wg.w, wg.h);
        ctx.fillStyle = this.color("line");
        ctx.fillRect(wg.x, wg.y, 1, wg.h);

        if (wg.orient === "h") {
          this.text(ctx, wg.x + 8, wg.y + 5, wg.w - 16, 10, kind,
            { size: "tiny", color: "muted" });
          ctx.save();
          ctx.beginPath();
          ctx.rect(wg.x + 1, wg.y + 16, wg.w - 2, wg.h - 17);
          ctx.clip();
          let x = wg.x + 3;
          ctx.font = `8px ${FONT_FAMILY}`;
          if ("letterSpacing" in ctx) ctx.letterSpacing = "1.2px";
          for (let i = 0; i < labels.length; i++) {
            const tw = Math.max(28, ctx.measureText(labels[i]).width + 14);
            if (x + tw > wg.x + wg.w - 2) break;
            if (i === 0) {
              ctx.fillStyle = this.color("hsel");
              this.roundRect(ctx, x, wg.y + 18, tw, 18, 6);
              ctx.fill();
            }
            this.text(ctx, x + 7, wg.y + 23, tw - 12, 10, labels[i],
              { size: "tiny", color: i === 0 ? "accent" : "fg", noFit: true });
            x += tw + 4;
          }
          if ("letterSpacing" in ctx) ctx.letterSpacing = "0px";
          ctx.restore();
          break;
        }

        this.text(ctx, wg.x + 12, wg.y + 7, 60, 10, kind,
          { size: "tiny", color: "muted" });
        const rows = Math.min(5, Math.max(0, Math.floor((wg.h - 18) / 24)));
        for (let i = 0; i < rows && i < labels.length; i++) {
          const y = wg.y + 18 + i * 24;
          if (i === 0) {
            ctx.fillStyle = this.color("hsel");
            this.roundRect(ctx, wg.x + 4, y, wg.w - 8, 22, 8);
            ctx.fill();
            ctx.fillStyle = this.color("accent");
            this.roundRect(ctx, wg.x + 7, y + 6, 2, 10, 1);
            ctx.fill();
          }
          this.text(ctx, wg.x + 13, y + 1, wg.w - 22, 14, labels[i],
            { size: "small", color: i === 0 ? "accent" : "fg" });
        }
        break;
      }
    }
  },

  poly(ctx, hist, gx, gy, gw, gh, key, col) {
    ctx.strokeStyle = this.color(col);
    ctx.lineWidth = 1;
    ctx.beginPath();
    const n = hist.length;
    for (let i = 0; i < n; i++) {
      const x = gx + (i * (gw - 1)) / Math.max(1, n - 1);
      const y = gy + gh - 2 - (hist[i][key] / 63) * (gh - 3);
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
    ctx.stroke();
  },

  draw(ctx, project, s, scale, mask, preview) {
    scale = scale || 2;
    ctx.save();
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    ctx.fillStyle = this.color(project.bg || "bg");
    ctx.fillRect(0, 0, 320, 170);
    for (const wg of project.items) {
      if (wg.hidden) continue;
      if (wg.t === "menu" && window.ED && ED.hideGuides) continue;
      const mm = wg.modes == null ? 7 : wg.modes;
      if (mask && (mm & mask) === 0) {
        if (preview) continue;
        ctx.globalAlpha = 0.22;
        this.widget(ctx, wg, s);
        ctx.globalAlpha = 1;
        continue;
      }
      this.widget(ctx, wg, s);
    }
    ctx.restore();
  },
};
