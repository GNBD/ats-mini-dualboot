// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// UiKit primitives (shared look with the "mini game" firmware)
// ---------------------------------------------------------------------------

static uint16_t shade16(uint16_t c, int8_t d)
{
  int r = (c >> 11) + d;
  int g = ((c >> 5) & 63) + d;
  int b = (c & 31) + d;
  if(r < 0) r = 0; else if(r > 31) r = 31;
  if(g < 0) g = 0; else if(g > 63) g = 63;
  if(b < 0) b = 0; else if(b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Small spring used by the DBFM menu animations. Semi-implicit Euler with a
// clamped delta: stiff enough to react at once, damped enough to settle.
struct UiSpring
{
  float x = 0;
  float v = 0;
};

static float uiSpringStep(UiSpring &s, float target, float stiffness,
                          float damping, float dt)
{
  if(dt < 0.001f) dt = 0.001f;
  if(dt > 0.032f) dt = 0.032f;
  float accel = (target - s.x) * stiffness - s.v * damping;
  s.v += accel * dt;
  s.x += s.v * dt;
  return s.x;
}

// DBFM main screen: 0/1 are the app slots, 2/3/4 are the OTA, Partition and
// Settings tiles.
static void mainItemRect(int index, int &x, int &y, int &w, int &h)
{
  if(index < 2)
  {
    w = 148;
    h = 72;
    x = 8 + index * 156;
    y = 22;
  }
  else
  {
    w = 96;
    h = 40;
    x = 8 + (index - 2) * 104;
    y = 102;
  }
}

enum { SMALL_OTA = 0, SMALL_PARTITION, SMALL_SETTINGS };

static void uiMenuIconSmall(int id, int cx, int cy, uint16_t c)
{
  if(id == SMALL_OTA)
  {
    gSprite.drawFastVLine(cx, cy - 8, 9, c);
    gSprite.drawLine(cx - 4, cy - 1, cx, cy + 3, c);
    gSprite.drawLine(cx + 4, cy - 1, cx, cy + 3, c);
    gSprite.drawLine(cx - 7, cy + 6, cx + 7, cy + 6, c);
    gSprite.drawLine(cx - 7, cy + 6, cx - 7, cy + 3, c);
    gSprite.drawLine(cx + 7, cy + 6, cx + 7, cy + 3, c);
  }
  else if(id == SMALL_PARTITION)
  {
    gSprite.drawRect(cx - 7, cy - 7, 14, 14, c);
    gSprite.drawFastVLine(cx, cy - 7, 14, c);
    gSprite.drawFastHLine(cx - 7, cy, 14, c);
  }
  else
  {
    gSprite.drawCircle(cx, cy, 5, c);
    gSprite.drawCircle(cx, cy, 2, c);
    for(int i = 0; i < 8; i++)
    {
      int dx = (i == 0 || i == 4) ? 0 : (i < 4 ? 1 : -1);
      int dy = (i == 2 || i == 6) ? 0 : (i < 2 || i > 6 ? -1 : 1);
      gSprite.drawLine(cx + dx * 6, cy + dy * 6, cx + dx * 9, cy + dy * 9, c);
    }
  }
}

static void uiBadge(int xRight, int y, const char *text,
                    uint16_t bg, uint16_t fg)
{
  int w = gSprite.textWidth(text, FONT_TINY) + 12;
  int x = xRight - w;
  gSprite.fillRoundRect(x, y, w, 13, 6, bg);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(fg, bg);
  gSprite.drawString(text, x + w / 2, y + 2, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
}

// Card with a coloured header strip.
static void uiPanel(int x, int y, int w, int h, uint16_t accent)
{
  gSprite.fillRoundRect(x, y, w, h, 8, COL_PANEL);
  gSprite.drawRoundRect(x, y, w, h, 8, COL_LINE);
  gSprite.fillRoundRect(x + 1, y + 1, w - 2, 9, 7, accent);
  gSprite.fillRect(x + 1, y + 6, w - 2, 4, accent);
  gSprite.drawFastHLine(x + 1, y + 10, w - 2, COL_LINE);
}

static void uiHintBar(const char *left, const char *right)
{
  gSprite.fillRect(0, 148, SCR_W, 22, COL_BG);
  gSprite.fillRect(0, 148, SCR_W, 1, COL_LINE);

  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  if(left) gSprite.drawString(left, 8, 156, FONT_TINY);

  if(right)
  {
    gSprite.setTextDatum(TR_DATUM);
    gSprite.drawString(right, 312, 156, FONT_TINY);
  }
  gSprite.setTextDatum(TL_DATUM);
}

// Short non-blocking feedback pill; the caller repaints its screen after it.
static void uiToast(const char *msg)
{
  int w = gSprite.textWidth(msg, FONT_TINY) + 24;
  if(w > 300) w = 300;
  int x = (SCR_W - w) / 2;
  int y = 120;
  gSprite.fillRoundRect(x, y, w, 20, 10, COL_HL);
  gSprite.drawRoundRect(x, y, w, 20, 10, COL_ACC);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_HL);
  gSprite.drawString(msg, x + w / 2, y + 3, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
  delay(550);
}

static void uiChip(int x, int y, const char *text, uint16_t bg, uint16_t fg)
{
  int w = gSprite.textWidth(text, FONT_TINY) + 14;
  gSprite.fillRoundRect(x, y, w, 15, 7, bg);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(fg, bg);
  gSprite.drawString(text, x + w / 2, y + 4, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
}

// One selectable list row.
static void uiRow(int x, int y, int w, int h, const char *text,
                  bool on, uint16_t acc)
{
  if(on)
  {
    gSprite.fillRoundRect(x, y, w, h, 6, COL_HL);
    gSprite.drawRoundRect(x, y, w, h, 6, acc);
    gSprite.fillRoundRect(x + 2, y + 4, 4, h - 8, 2, acc);
    gSprite.setTextColor(acc, COL_HL);
  }
  else
  {
    gSprite.fillRoundRect(x, y, w, h, 6, COL_PANEL);
    gSprite.drawRoundRect(x, y, w, h, 6, COL_LINE);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
  }
  gSprite.setTextDatum(TL_DATUM);
  gSprite.drawString(text, x + 16, y + (h - 16) / 2, FONT_SMALL);
}

static void uiDotGrid()
{
  // Dot grid disabled: the plain black background is preferred.
}

// Tile icon ids for uiMenuIcon().
enum {
  ICON_SLOT0 = 0, ICON_SLOT1, ICON_UPDATE, ICON_SETTINGS, ICON_ERASE,
  ICON_WIFI, ICON_ABOUT, ICON_BRIGHT, ICON_WIFICFG, ICON_WEB,
  ICON_BOOT, ICON_HOLD, ICON_FOLDER, ICON_NET, ICON_TRASH, ICON_DB,
  ICON_CALLSIGN
};

static void uiMenuIcon(int id, int cx, int cy, uint16_t acc)
{
  int i;

  // 8 way rays/teeth offsets: x1,y1,x2,y2 relative to the icon centre.
  static const int8_t ray[8][4] = {
    {  0, -10,  0, -15}, {  7,  -7, 11, -11}, { 10,   0, 15,   0},
    {  7,   7, 11,  11}, {  0,  10,  0,  15}, {- 7,   7,-11,  11},
    {-10,   0,-15,   0}, {- 7,  -7,-11, -11}
  };

  switch(id)
  {
    case ICON_SLOT0:
    case ICON_SLOT1:
    {
      gSprite.fillRoundRect(cx - 15, cy - 13, 30, 26, 4, COL_CELL);
      gSprite.drawRoundRect(cx - 15, cy - 13, 30, 26, 4, acc);
      for(i = 0; i < 3; i++)
      {
        gSprite.fillRect(cx - 19, cy - 10 + i * 9, 4, 3, COL_LINE);
        gSprite.fillRect(cx + 15, cy - 10 + i * 9, 4, 3, COL_LINE);
      }
      gSprite.fillRect(cx - 3, cy - 17, 6, 4, COL_LINE);
      gSprite.setTextDatum(MC_DATUM);
      gSprite.setTextColor(acc, COL_CELL);
      gSprite.drawString(id == ICON_SLOT0 ? "0" : "1", cx, cy - 7, FONT_SMALL);
      gSprite.setTextDatum(TL_DATUM);
      break;
    }
    case ICON_UPDATE: // download into tray
    {
      gSprite.fillRect(cx - 3, cy - 15, 6, 17, acc);
      for(i = 0; i < 6; i++)
        gSprite.fillRect(cx - 4 - i, cy + 2 + i, 8 + 2 * i, 2, acc);
      gSprite.fillRoundRect(cx - 14, cy + 13, 28, 8, 2, COL_CELL);
      gSprite.drawRoundRect(cx - 14, cy + 13, 28, 8, acc);
      break;
    }
    case ICON_SETTINGS: // sliders
    {
      for(i = 0; i < 3; i++)
      {
        int ly = cy - 12 + i * 12;
        gSprite.fillRect(cx - 16, ly, 32, 2, COL_LINE);
        int kx = cx - 8 + i * 8;
        gSprite.fillRoundRect(kx - 3, ly - 4, 7, 10, 3, acc);
      }
      break;
    }
    case ICON_ERASE: // eraser
    {
      gSprite.fillRect(cx - 4, cy - 16, 8, 4, acc);
      gSprite.fillRect(cx - 12, cy - 12, 24, 4, acc);
      gSprite.fillRect(cx - 9, cy - 6, 18, 20, COL_CELL);
      gSprite.drawRect(cx - 9, cy - 6, 18, 20, acc);
      for(i = 0; i < 3; i++)
        gSprite.drawFastVLine(cx - 5 + i * 5, cy - 2, 12, COL_LINE);
      break;
    }
    case ICON_WIFI: // signal arcs over a dot
    {
      gSprite.fillCircle(cx, cy + 8, 3, acc);
      gSprite.drawArc(cx, cy + 8, 9, 11, 225, 315, acc);
      gSprite.drawArc(cx, cy + 8, 16, 18, 225, 315, acc);
      break;
    }
    case ICON_ABOUT: // info
    {
      gSprite.drawCircle(cx, cy, 14, acc);
      gSprite.fillCircle(cx, cy - 7, 2, acc);
      gSprite.fillRect(cx - 1, cy - 3, 3, 9, acc);
      break;
    }
    case ICON_BRIGHT: // sun
    {
      for(i = 0; i < 8; i++)
        gSprite.drawLine(cx + ray[i][0], cy + ray[i][1],
                     cx + ray[i][2], cy + ray[i][3], acc);
      gSprite.fillCircle(cx, cy, 7, acc);
      break;
    }
    case ICON_WIFICFG: // gear
    {
      gSprite.drawCircle(cx, cy, 12, acc);
      gSprite.drawCircle(cx, cy, 6, acc);
      for(i = 0; i < 8; i++)
        gSprite.drawLine(cx + ray[i][0] * 12 / 15, cy + ray[i][1] * 12 / 15,
                     cx + ray[i][2], cy + ray[i][3], acc);
      break;
    }
    case ICON_WEB: // globe
    {
      gSprite.drawCircle(cx, cy, 13, acc);
      gSprite.drawEllipse(cx, cy, 6, 13, acc);
      gSprite.drawLine(cx - 13, cy, cx + 13, cy, acc);
      gSprite.drawLine(cx - 11, cy - 6, cx + 11, cy - 6, acc);
      gSprite.drawLine(cx - 11, cy + 6, cx + 11, cy + 6, acc);
      break;
    }
    case ICON_BOOT: // play in a circle
    {
      gSprite.drawCircle(cx, cy, 13, acc);
      gSprite.fillTriangle(cx - 5, cy - 8, cx - 5, cy + 8, cx + 9, cy, acc);
      break;
    }
    case ICON_HOLD: // clock
    {
      gSprite.drawCircle(cx, cy, 13, acc);
      gSprite.fillCircle(cx, cy - 11, 1, acc);
      gSprite.fillCircle(cx + 11, cy, 1, acc);
      gSprite.fillCircle(cx, cy + 11, 1, acc);
      gSprite.fillCircle(cx - 11, cy, 1, acc);
      gSprite.drawLine(cx, cy, cx, cy - 8, acc);
      gSprite.drawLine(cx, cy, cx + 6, cy + 4, acc);
      break;
    }
    case ICON_FOLDER: // file folder
    {
      gSprite.fillRect(cx - 14, cy - 10, 13, 5, acc);
      gSprite.fillRect(cx - 14, cy - 5, 28, 15, COL_CELL);
      gSprite.drawRect(cx - 14, cy - 10, 13, 5, acc);
      gSprite.drawRect(cx - 14, cy - 5, 28, 15, acc);
      break;
    }
    case ICON_NET: // three linked nodes
    {
      gSprite.drawLine(cx, cy + 10, cx - 11, cy - 7, acc);
      gSprite.drawLine(cx, cy + 10, cx + 11, cy - 7, acc);
      gSprite.drawLine(cx - 11, cy - 7, cx + 11, cy - 7, acc);
      gSprite.fillCircle(cx, cy + 10, 4, acc);
      gSprite.fillCircle(cx - 11, cy - 7, 4, acc);
      gSprite.fillCircle(cx + 11, cy - 7, 4, acc);
      break;
    }
    case ICON_TRASH: // waste bin
    {
      gSprite.fillRect(cx - 5, cy - 16, 10, 3, acc);
      gSprite.fillRect(cx - 10, cy - 13, 20, 3, acc);
      gSprite.fillRect(cx - 8, cy - 10, 16, 20, COL_CELL);
      gSprite.drawRect(cx - 8, cy - 10, 16, 20, acc);
      for(i = 0; i < 3; i++)
        gSprite.drawFastVLine(cx - 4 + i * 4, cy - 6, 12, COL_LINE);
      break;
    }
    case ICON_CALLSIGN: // ID badge
    {
      gSprite.drawRoundRect(cx - 13, cy - 14, 26, 28, 4, acc);
      gSprite.fillCircle(cx, cy - 6, 4, acc);
      gSprite.drawLine(cx - 8, cy + 8, cx + 8, cy + 8, acc);
      break;
    }
    case ICON_DB: // storage cylinder
    {
      gSprite.drawEllipse(cx, cy - 8, 9, 4, acc);
      gSprite.drawEllipse(cx, cy + 8, 9, 4, acc);
      gSprite.drawEllipse(cx, cy, 9, 4, acc);
      gSprite.drawLine(cx - 9, cy - 8, cx - 9, cy + 8, acc);
      gSprite.drawLine(cx + 9, cy - 8, cx + 9, cy + 8, acc);
      break;
    }
    default: // about
    {
      gSprite.drawCircle(cx, cy, 14, acc);
      gSprite.fillCircle(cx, cy - 7, 2, acc);
      gSprite.fillRect(cx - 1, cy - 3, 3, 9, acc);
      break;
    }
  }
}

// drawHeader() has a default argument, so arduino-preprocessor does not emit
// a prototype for it; declare it before the screens that call it.
static void drawHeader(const char *title, const char *right);

// Tile geometry for a selection screen: 2/3 items -> one row of big tiles,
// 4 items -> 2x2, 5/6 items -> the 3x2 main menu grid, 7/8 items -> a 4x2 grid
// of smaller tiles (the last cell may stay empty).
static void tileGeom(int n, int &x0, int &y0, int &w, int &h,
                     int &dx, int &dy, int &cols)
{
  if(n <= 3)
  {
    cols = n;
    w = (304 - (n - 1) * 8) / n;
    dx = w + 8;
    x0 = 8;
    h = 96;
    dy = h;
    y0 = 40;
  }
  else if(n == 4)
  {
    cols = 2;
    w = 148;
    dx = 156;
    x0 = 8;
    h = 56;
    dy = 62;
    y0 = 28;
  }
  else if(n <= 6)
  {
    cols = 3;
    w = 93;
    dx = 101;
    x0 = 12;
    h = 56;
    dy = 60;
    y0 = 28;
  }
  else
  {
    cols = 4;
    w = 70;
    dx = 78;
    x0 = 8;
    h = 56;
    dy = 60;
    y0 = 28;
  }
}

// One selectable tile: accent strip, icon, label.
static void uiTile(int x, int y, int w, int h, const char *label, int iconId,
                   uint16_t acc, bool on)
{
  gSprite.fillRoundRect(x, y, w, h, 7, on ? COL_HL : COL_PANEL);
  gSprite.drawRoundRect(x, y, w, h, 7, on ? acc : COL_LINE);
  if(on)
    gSprite.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 6, shade16(acc, -8));

  gSprite.fillRoundRect(x + 10, y + 5, w - 20, 3, 1, acc);

  int iconCy = (h > 64) ? y + (h - 24) / 2 + 6 : y + h - 30;
  int labelY = (h > 64) ? y + h - 16 : y + h - 12;
  uiMenuIcon(iconId, x + w / 2, iconCy, acc);

  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(on ? acc : COL_TEXT, on ? COL_HL : COL_PANEL);
  gSprite.drawString(label, x + w / 2, labelY, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
}

// Compact tile used by the Settings grid (4x3).
static void uiTileCompact(int x, int y, int w, int h, const char *label,
                          uint16_t acc, bool on)
{
  gSprite.fillRoundRect(x, y, w, h, 6, on ? COL_PANEL2 : COL_PANEL);
  gSprite.drawRoundRect(x, y, w, h, 6, on ? acc : COL_LINE);
  if(on) gSprite.fillRoundRect(x + 5, y + 6, 3, h - 12, 1, acc);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(on ? COL_TEXT : COL_MUTED,
                       on ? COL_PANEL2 : COL_PANEL);
  gSprite.drawString(label, x + (on ? 12 : 9), y + (h - 8) / 2, FONT_TINY);
}

// A whole tile selection screen: title bar, dot grid, tiles, hint bar.
static void drawTilePage(const char *title, const char *right,
                         const char *hintLeft, const char *hintRight,
                         const char *const *labels, const int *icons,
                         const uint16_t *accs, int count, int selected)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader(title, right);

  int x0, y0, w, h, dx, dy, cols;
  tileGeom(count, x0, y0, w, h, dx, dy, cols);

  for(int i = 0; i < count; i++)
    uiTile(x0 + (i % cols) * dx, y0 + (i / cols) * dy, w, h,
           labels[i], icons[i], accs[i], i == selected);

  uiHintBar(hintLeft, hintRight);
  gSprite.pushSprite(0, 0);
}

// Tile based equivalent of runChoice() for short fixed labels. Hold to pick,
// click to back out (returns -1).
static int runTileChoice(const char *title, const char *hintLeft,
                         const char *hintRight,
                         const char *const *labels, const int *icons,
                         const uint16_t *accs, int count)
{
  int selected = 0;
  drawTilePage(title, NULL, hintLeft, hintRight, labels, icons, accs,
               count, selected);

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      selected = (selected + d) % count;
      if(selected < 0) selected += count;
      drawTilePage(title, NULL, hintLeft, hintRight, labels, icons, accs,
                   count, selected);
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_CLICK) return selected;
    if(ev == BTN_HOLD) return -1;
    delay(10);
  }
}

