// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// ATS Mini standalone recovery firmware (beta) - Boot Manager
// Network update + WiFi setting beta. Runs from its own partition.
//
// Boot sequence:
//   1. Power ON -> recovery always boots first
//   2. Wait 1 second for encoder press
//   3. No encoder -> auto boot to app0
//   4. Encoder held -> recovery menu (STA WiFi, WiFi section, update, etc.)
//   5. Web server runs in a background task (STA, or AP when not connected)
//   6. Recovery always sets itself as next boot target before jumping to app
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <esp_littlefs.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include <MD5Builder.h>
#include <qrcode.h>
#include "Display.h"
#include "Rotary.h"
#include <freertos/task.h>

#define PIN_LCD_BL          38
#define ENCODER_PIN_A        2
#define ENCODER_PIN_B        1
#define ENCODER_PUSH_BUTTON 21
// v4.x DES: the recovery keeps its own NVS so wificfg/uicfg survive an
// environment switch. v4.x.md rule 4 allows exactly this one line.
#define STORAGE_PARTITION    "rec_settings"

#define WIFI_PAGE_SIZE       4
#define FILE_PAGE_SIZE       4
#define MAX_SCAN_NETWORKS    20
#define MAX_URLS             8
#define WIFI_CONNECT_TIMEOUT 15000
#define BOOT_CONNECT_TIMEOUT 5000
#define KB_MAX_KEYS          48
#define HOLD_NOTICE_MS       500

static constexpr const lgfx::IFont* FONT_LARGE = &lgfx::fonts::Font4;
static constexpr const lgfx::IFont* FONT_SMALL = &lgfx::fonts::Font2;
static constexpr const lgfx::IFont* FONT_TINY  = &lgfx::fonts::Font0;

#define SCR_W 320
#define SCR_H 170

// UiKit palette (shared with the "mini game" firmware). Black shell, the rest
// is coloured.
#define RGB(r, g, b) ((uint16_t)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31)))

#define COL_BG      0x0000            // pure black
// Surfaces are neutral charcoal with a faint green cast so nothing in the UI
// reads blue any more; the sage green stays for accents only.
#define COL_PANEL   RGB(9, 19, 9)     // cards / bars
#define COL_HL      RGB(11, 23, 11)   // selected row / tile
#define COL_CELL    RGB(5, 10, 5)     // sunken interior
#define COL_LINE    RGB(10, 21, 10)   // borders / grid
#define COL_TRACK   RGB(5, 10, 5)     // progress track
#define COL_TEXT    RGB(29, 53, 58)   // off white
#define COL_MUTED   RGB(13, 31, 40)   // gray
#define COL_ACC     RGB(20, 44, 17)   // primary accent (header, selection)
#define COL_ACC2    RGB(31, 46, 10)   // gold accent (slots, hints)
#define COL_GOLD    RGB(31, 46, 10)   // amber (tile accent)
#define COL_OK      RGB(6, 55, 16)    // green
#define COL_WARN    RGB(31, 14, 10)   // red (destructive)
#define COL_AP      RGB(31, 46, 10)   // AP badge (amber)
#define COL_KEY     RGB(7, 14, 7)     // keyboard key face
#define COL_KEYSEL  RGB(20, 44, 17)   // keyboard selected key (same as COL_ACC)

#define RECOVERY_VERSION "4.0.0"

// Default: fetch this .txt (one URL per line). Local /update_url.txt and
// DEFAULT_UPDATE_URLS are fallbacks when the remote list is unavailable.
static const char *REMOTE_UPDATE_URL_TXT =
  "https://gnbdatsmini.netlify.app/update_url.txt";

static const char *DEFAULT_UPDATE_URLS[MAX_URLS] = {};

LGFX tft;

// All drawing goes to this off-screen buffer; screens are pushed to the LCD
// once they are complete, so a redraw no longer flickers. It lives in PSRAM.
static LGFX_Sprite gSprite(&tft);
WebServer server(80);
Rotary encoder(ENCODER_PIN_B, ENCODER_PIN_A, false);
Preferences prefs;

static volatile int16_t encoderCount = 0;
static volatile int16_t encoderCountAccel = 0;

static String apIP;
static volatile bool apModeActive = false;
static bool serverRunning = false;
static volatile bool gWebEnabled = true;
static volatile bool gUiQuiet = false;
static volatile bool gUiRefresh = false;
static TaskHandle_t webTaskHandle = nullptr;

static const char *gBootSlot = "App0";
static bool gBootIsApp1 = false;

// Set when the littlefs volume could not be repaired. Nothing may mount it
// then and the app must not start either: the stale superblock trips a
// lfs_fs_grow_ assert and the board reboots forever.
static bool gLittleFsBlocked = false;

// Set by the one-shot flash task while it erases/writes a slot. Up here
// because batteryTick() guards its redraw on it, and the sketch preprocessor
// emits forward declarations above everything past this point.
static volatile bool gFlashBusy = false;

// Sketch preprocessor forward declares drawEraseList()/runEraseList() above
// their definition, so the type they take has to exist that early too.
struct EraseList;

static const char *menu[] = {
  "Boot App0",
  "Boot App1",
  "Firmware Update",
  "Erase",
  "Settings"
};
#define MENU_COUNT (sizeof(menu) / sizeof(menu[0]))

// Settings > ...  (Network / Brightness / About live here now)
static const char *settingsMenu[] = {
  "Network",
  "Brightness",
  "About"
};
#define SETTINGS_COUNT (sizeof(settingsMenu) / sizeof(settingsMenu[0]))

static const char *wifiMenu[] = {
  "WiFi",
  "Network About"
};
#define WIFI_MENU_COUNT (sizeof(wifiMenu) / sizeof(wifiMenu[0]))

static const char *bootModeMenu[] = {
  "Default boot",
  "Hold mode"
};
#define BOOT_MODE_COUNT (sizeof(bootModeMenu) / sizeof(bootModeMenu[0]))

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------

static int16_t accelerateEncoder(int8_t dir)
{
  const uint32_t speedThresholds[] = {350, 60, 45, 35, 25};
  const uint16_t accelFactors[] =      {1,  2,  4,  8, 16};
  static uint32_t lastEncoderTime = 0;
  static uint32_t lastSpeed = speedThresholds[0];
  static uint16_t lastAccelFactor = accelFactors[0];
  static int8_t lastEncoderDir = 0;

  uint32_t currentTime = millis();
  lastSpeed = ((currentTime - lastEncoderTime) * 7 + lastSpeed * 3) / 10;

  if(lastSpeed > speedThresholds[0] || lastEncoderDir != dir)
  {
    lastSpeed = speedThresholds[0];
    lastAccelFactor = accelFactors[0];
  }
  else
  {
    for(int8_t i = 4; i >= 0; i--)
    {
      if(lastSpeed <= speedThresholds[i] && lastAccelFactor < accelFactors[i])
      {
        lastAccelFactor = accelFactors[i];
        break;
      }
    }
  }
  lastEncoderTime = currentTime;
  lastEncoderDir = dir;
  return dir * lastAccelFactor;
}

static void IRAM_ATTR rotaryEncoderISR()
{
  uint8_t encoderStatus = encoder.process();
  if(encoderStatus)
  {
    int8_t delta = encoderStatus == DIR_CW ? 1 : -1;
    int16_t accelDelta = accelerateEncoder(delta);
    if(abs(encoderCount) < 5)
    {
      encoderCount += delta;
      encoderCountAccel += accelDelta;
    }
  }
}

// accel=true: menus (main feel). accel=false: keyboard (raw steps).
static int8_t readEncoder(bool accel = true)
{
  int16_t c;
  noInterrupts();
  c = accel ? encoderCountAccel : encoderCount;
  encoderCount = 0;
  encoderCountAccel = 0;
  interrupts();
  if(c > 127) c = 127;
  if(c < -127) c = -127;
  return (int8_t)c;
}

// A hold is reported as soon as it reaches 300 ms: the button does not have
// to be released first, so a menu opens while it is still held. That press is
// then consumed until it is really released, so one hold never fires twice.
// Releasing before 300 ms returns the shorter time (a click).
static bool gBtnConsumed = false;

// Redrawn once a second from readButton() so the header voltage tracks the
// supply without any menu having to own a timer.
static void batteryTick();

static uint32_t readButton()
{
  batteryTick();

  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW)
  {
    gBtnConsumed = false;
    return 0;
  }
  if(gBtnConsumed) return 0;
  delay(30);
  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW)
  {
    gBtnConsumed = false;
    return 0;
  }

  uint32_t start = millis();
  while(digitalRead(ENCODER_PUSH_BUTTON) == LOW)
  {
    uint32_t held = millis() - start;
    if(held >= 300)
    {
      gBtnConsumed = true;
      return held;                          // auto-enter while still held
    }
    delay(10);
  }
  return millis() - start;                  // released early = click
}

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
  ICON_BOOT, ICON_HOLD, ICON_FOLDER, ICON_NET, ICON_TRASH, ICON_DB
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
// 4 items -> 2x2, 5/6 items -> the 3x2 main menu grid.
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
  else
  {
    cols = 3;
    w = 93;
    dx = 101;
    x0 = 12;
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

    uint32_t h = readButton();
    if(h >= 300) return selected;
    if(h > 0) return -1;
    delay(10);
  }
}

// ---------------------------------------------------------------------------
// WiFi helpers
// ---------------------------------------------------------------------------

static void applyTxPower()
{
  WiFi.setTxPower(WIFI_POWER_13dBm);
}

static void drawNetIcon()
{
  gSprite.fillRect(290, 2, 30, 21, COL_BG);
  if(apModeActive)
  {
    gSprite.setTextDatum(TL_DATUM);
    gSprite.setTextColor(COL_AP, COL_BG);
    gSprite.drawString("AP", 297, 5, FONT_SMALL);
    return;
  }
  if(WiFi.status() != WL_CONNECTED)
  {
    gSprite.setTextColor(COL_WARN, COL_BG);
    gSprite.drawLine(300, 7, 310, 17, COL_WARN);
    gSprite.drawLine(310, 7, 300, 17, COL_WARN);
    return;
  }
  gSprite.drawCircle(305, 17, 2, COL_OK);
  gSprite.drawArc(305, 17, 5, 6, 225, 315, COL_OK);
  gSprite.drawArc(305, 17, 9, 10, 225, 315, COL_OK);
}

// Battery voltage on GPIO4 (VBAT_MON). Same maths as the main firmware
// (ats-mini/Battery.cpp): 10 averaged ADC samples * 1.702 / 1000 = volts.
static float readBatteryVolts()
{
  long sum = 0;
  for(int i = 0; i < 10; i++) sum += analogRead(4);
  return (float)sum / 10.0f * 1.702f / 1000.0f;
}

// Battery voltage, right aligned so it ends right before rightX. Remembers
// where it last drew so batteryTick() can refresh just that text.
static uint32_t gBattDrawnAt = 0;
static bool gBattValid = false;
static int gBattRightX = 286;

static void drawBattery(int rightX)
{
  float v = readBatteryVolts();
  uint16_t col = (v >= 3.80f) ? COL_OK : ((v >= 3.50f) ? COL_GOLD : COL_WARN);
  char buf[8];
  snprintf(buf, sizeof(buf), "%.2fV", v);
  gSprite.setTextDatum(TR_DATUM);
  gSprite.setTextColor(col, COL_BG);
  gSprite.drawString(buf, rightX, 5, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);

  gBattRightX = rightX;
  gBattDrawnAt = millis();
  gBattValid = true;
}

// The header voltage refreshes every second. Skipped while a flash job owns
// the sprite, and only after a header has drawn it once.
static void batteryTick()
{
  if(!gBattValid || gFlashBusy || gUiQuiet) return;
  if(millis() - gBattDrawnAt < 1000) return;
  drawBattery(gBattRightX);
  gSprite.pushSprite(0, 0);
}

static void drawHeader(const char *title, const char *right = nullptr)
{
  gSprite.fillRect(0, 0, SCR_W, 24, COL_BG);
  gSprite.fillRect(0, 23, SCR_W, 1, COL_ACC);

  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_ACC, COL_BG);
  gSprite.drawString(title, 8, 4, FONT_SMALL);

  drawNetIcon();
  if(right)
  {
    uiChip(240, 4, right, COL_GOLD, COL_BG);
    drawBattery(234);
  }
  else drawBattery(286);
}

static bool loadWifiCredentials(String &ssid, String &pass)
{
  if(!prefs.begin("wificfg", true, STORAGE_PARTITION)) return false;
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  return ssid.length() > 0;
}

static void saveWifiCredentials(const String &ssid, const String &pass)
{
  if(!prefs.begin("wificfg", false, STORAGE_PARTITION)) return;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

static bool loadWebEnabled()
{
  if(!prefs.begin("wificfg", true, STORAGE_PARTITION)) return true;
  bool v = prefs.getBool("web", true);
  prefs.end();
  return v;
}

static void saveWebEnabled(bool on)
{
  if(!prefs.begin("wificfg", false, STORAGE_PARTITION)) return;
  prefs.putBool("web", on);
  prefs.end();
}

// The slot the user last chose, kept in the recovery's own settings. otadata
// cannot be trusted for this: the bootloader patch forces the recovery on every
// power-up and setup() rewrites otadata to the recovery slot each time, so the
// application selection does not survive a power cycle on its own. Stores 0 or
// 1; 0xFF means "no record yet" (fresh install or wiped partition).
static uint8_t loadBootSlot()
{
  if(!prefs.begin("bootcfg", true, STORAGE_PARTITION)) return 0xFF;
  uint8_t v = prefs.getUChar("slot", 0xFF);
  prefs.end();
  return v;
}

static void saveBootSlot(uint8_t slot)
{
  if(!prefs.begin("bootcfg", false, STORAGE_PARTITION)) return;
  // Rewriting the same value on every auto boot would burn NVS cycles for
  // nothing, so only commit when the selection actually changed.
  if(prefs.getUChar("slot", 0xFF) != slot) prefs.putUChar("slot", slot);
  prefs.end();
}

static bool wifiConnectBlocking(const String &ssid, const String &pass, uint32_t timeoutMs)
{
  WiFi.mode(WIFI_STA);
  applyTxPower();
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while(WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs)
    delay(50);
  bool ok = (WiFi.status() == WL_CONNECTED);
  if(ok) apModeActive = false;
  return ok;
}

static void wifiStartAp()
{
  WiFi.disconnect(true);
  delay(50);
  WiFi.mode(WIFI_AP);
  applyTxPower();
  WiFi.softAP("ats-recovery", "12345678");
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setSleep(false);
  apIP = WiFi.softAPIP().toString();
  apModeActive = true;
}

static String currentIp()
{
  if(WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if(apModeActive) return WiFi.softAPIP().toString();
  return "-";
}

// ---------------------------------------------------------------------------
// Brightness (Settings > Brightness, stored in the settings partition)
// ---------------------------------------------------------------------------

#define BRIGHT_MIN  10
#define BRIGHT_MAX  100
#define BRIGHT_STEP 5

static int loadBrightness()
{
  if(!prefs.begin("uicfg", true, STORAGE_PARTITION)) return 80;
  int v = prefs.getInt("bright", 80);
  prefs.end();
  if(v < BRIGHT_MIN) v = BRIGHT_MIN;
  if(v > BRIGHT_MAX) v = BRIGHT_MAX;
  return v;
}

static void saveBrightness(int pct)
{
  if(!prefs.begin("uicfg", false, STORAGE_PARTITION)) return;
  prefs.putInt("bright", pct);
  prefs.end();
}

static void applyBrightness(int pct)
{
  if(pct < BRIGHT_MIN) pct = BRIGHT_MIN;
  if(pct > BRIGHT_MAX) pct = BRIGHT_MAX;
  ledcWrite(PIN_LCD_BL, (pct * 255) / 100);
}

// ---------------------------------------------------------------------------
// Yes / No popup for destructive or leaving-recovery actions
// ---------------------------------------------------------------------------

// The two buttons of confirmDialog(). Only these are refreshed while the
// decision is open, so the popup no longer repaints its whole panel every
// 10 ms (that was the flicker).
static void drawConfirmButtons(bool yes)
{
  const int by = 112, bw = 96, bh = 18;
  const int bx[2] = {56, 168};

  gSprite.setTextDatum(TC_DATUM);
  for(int i = 0; i < 2; i++)
  {
    bool sel = (i == 0) ? yes : !yes;
    uint16_t bg = sel ? COL_OK : COL_HL;
    gSprite.fillRoundRect(bx[i], by, bw, bh, 4, bg);
    gSprite.drawRoundRect(bx[i], by, bw, bh, 4, sel ? COL_OK : COL_LINE);
    gSprite.setTextColor(sel ? COL_BG : COL_TEXT, bg);
    gSprite.drawString(i == 0 ? "Yes" : "No", bx[i] + bw / 2, by + 1, FONT_SMALL);
  }
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
}

// Yes / No popup for destructive or leaving-recovery actions.
// l2 may be null. Rotate to move, hold to confirm, click to cancel.
static bool confirmDialog(const char *title, const char *l1, const char *l2)
{
  bool yes = false;                      // start on No

  uiPanel(24, 40, 272, 94, COL_WARN);

  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_WARN, COL_PANEL);
  gSprite.drawString(title, 160, 54, FONT_SMALL);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString(l1, 160, 76, FONT_SMALL);
  if(l2) gSprite.drawString(l2, 160, 94, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);
  drawConfirmButtons(yes);

  for(;;)
  {
    int8_t d = readEncoder();
    if(d)
    {
      yes = !yes;
      drawConfirmButtons(yes);
    }

    uint32_t h = readButton();
    if(h > 0) return h >= 300 ? yes : false;   // hold = confirm, click = cancel
    delay(10);
  }
}

// Single button sibling of confirmDialog(): the one action offered is the only
// one available, so any press continues. Used by the littlefs upgrade prompt,
// where saying no is not a choice - the app cannot boot onto that volume.
static void confirmOne(const char *title, const char *l1, const char *l2,
                       const char *btn)
{
  const int by = 112, bw = 96, bh = 18;
  const int bx = (320 - bw) / 2;

  uiPanel(24, 40, 272, 94, COL_WARN);

  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_WARN, COL_PANEL);
  gSprite.drawString(title, 160, 54, FONT_SMALL);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString(l1, 160, 76, FONT_SMALL);
  if(l2) gSprite.drawString(l2, 160, 94, FONT_SMALL);

  gSprite.fillRoundRect(bx, by, bw, bh, 4, COL_OK);
  gSprite.drawRoundRect(bx, by, bw, bh, 4, COL_OK);
  gSprite.setTextColor(COL_BG, COL_OK);
  gSprite.drawString(btn, bx + bw / 2, by + 1, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);

  while(readButton() == 0) delay(10);
}

// ---------------------------------------------------------------------------
// Boot helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Flash self check
//
// The ESP32-S3 second stage bootloader lives at flash offset 0x0
// (build.bootloader_addr for esp32s3), not at 0x1000, so it is read back raw.
// BOOTLOADER_FLASH_CRC32 is the CRC32 of the 20000 byte bootloader.bin shipped
// next to this sketch; recompute it whenever that file is rebuilt.
// ---------------------------------------------------------------------------

#define BOOTLOADER_FLASH_ADDR   0x0
#define BOOTLOADER_FLASH_SIZE   20000u
#define BOOTLOADER_FLASH_CRC32  0x4B2022DEu
#define PARTITION_TABLE_ADDR    0x8000

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len)
{
  for(size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for(int k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

// Bootloader image present and byte identical to our bootloader.bin?
static bool bootloaderOk()
{
  uint32_t buf[64];
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t addr = BOOTLOADER_FLASH_ADDR;

  // 20000 is not a multiple of the buffer, so the tail chunk must be clamped
  // or the CRC would cover bytes past the bootloader.
  while(addr < BOOTLOADER_FLASH_SIZE)
  {
    size_t chunk = BOOTLOADER_FLASH_SIZE - addr;
    if(chunk > sizeof(buf)) chunk = sizeof(buf);
    if(!ESP.flashRead(addr, buf, chunk)) return false;
    crc = crc32Update(crc, (const uint8_t *)buf, chunk);
    addr += chunk;
  }

  crc ^= 0xFFFFFFFFu;
  bool ok = (crc == BOOTLOADER_FLASH_CRC32);
  Serial.printf("self check: bootloader crc 0x%08lX (%s)\n",
                (unsigned long)crc, ok ? "ok" : "MISMATCH");
  return ok;
}

// ---------------------------------------------------------------------------
// DES: partition table patch
// ---------------------------------------------------------------------------
// ENV0 and ENV1 share every entry except `settings`, which points at the
// environment's own NVS region (0xF9D000 for ENV0, 0xFBD000 for ENV1).
// Switching therefore means: retarget that one offset in a RAM copy of the
// canonical table, recompute the MD5 entry over the first num_parts * 32
// bytes, then erase and rewrite 0x8000. The table is always staged in RAM so
// no mapped flash pointer survives into the write.
#define DES_TABLE_SIZE      0xC00u         // slot the bootloader scans
#define DES_ENTRY_SIZE      32u
#define DES_MAX_PARTITIONS  64u
#define DES_MAGIC_ENTRY     0x50AAu
#define DES_MAGIC_MD5       0xEBEBu
#define DES_SETTINGS_ENV0   0xF9D000u
#define DES_SETTINGS_ENV1   0xFBD000u
#define DES_SETTINGS_SIZE   0x20000u
#define DES_PT_SECTOR       (PARTITION_TABLE_ADDR / 4096u)

static const uint8_t DES_LABEL_SETTINGS[16] = "settings";

// Walks the entry list. Fails when the magic chain or the MD5 terminator is
// broken; otherwise reports the partition count, the index of the `settings`
// entry (0xFFFF when absent) and the offset of the MD5 entry.
static bool desParseTable(const uint8_t *buf, size_t len, uint16_t *numParts,
                          uint16_t *settingsIdx, size_t *md5Pos)
{
  *numParts = 0;
  *settingsIdx = 0xFFFF;
  *md5Pos = 0;

  for(size_t pos = 0; pos + DES_ENTRY_SIZE <= len; pos += DES_ENTRY_SIZE)
  {
    uint16_t magic = (uint16_t)buf[pos] | ((uint16_t)buf[pos + 1] << 8);

    if(magic == DES_MAGIC_MD5)
    {
      *md5Pos = pos;
      return *numParts > 0 && *numParts <= DES_MAX_PARTITIONS;
    }
    if(magic != DES_MAGIC_ENTRY) return false;
    if(memcmp(buf + pos + 12, DES_LABEL_SETTINGS, sizeof(DES_LABEL_SETTINGS)) == 0)
      *settingsIdx = (uint16_t)(pos / DES_ENTRY_SIZE);
    (*numParts)++;
  }
  return false;                             // no MD5 terminator in the slot
}

// ESP-IDF hashes exactly num_parts * 32 bytes (everything before the MD5
// entry) and stores the digest in bytes 16..31 of that entry - the same rule
// gen_esp32part.py and the host side verify_md5.py apply.
static void desStoreMd5(uint8_t *buf, uint16_t numParts, size_t md5Pos)
{
  MD5Builder md5;
  md5.begin();
  md5.add(buf, (size_t)numParts * DES_ENTRY_SIZE);
  md5.calculate();
  md5.getBytes(buf + md5Pos + 16);
}

// Retargets the `settings` entry to `env` inside a RAM copy of the table and
// refreshes the MD5. Fails if the copy does not parse.
static bool desBuildTable(uint8_t *buf, size_t len, uint8_t env)
{
  uint16_t numParts, settingsIdx;
  size_t md5Pos;
  if(!desParseTable(buf, len, &numParts, &settingsIdx, &md5Pos)) return false;
  if(settingsIdx == 0xFFFF) return false;

  size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
  uint32_t off = env ? DES_SETTINGS_ENV1 : DES_SETTINGS_ENV0;

  // Only the offset moves - both environments use the same size, which
  // desVerifyTable() checks against the source table.
  for(int i = 0; i < 4; i++) buf[e + 4 + i] = (uint8_t)(off >> (8 * i));

  desStoreMd5(buf, numParts, md5Pos);
  return true;
}

// The criteria the offline verifier applies: a parseable magic chain, exactly
// one `settings` entry at the expected 64KB aligned address inside flash, and
// a stored MD5 that matches a recomputation.
static bool desVerifyTable(const uint8_t *buf, size_t len, uint8_t env)
{
  uint16_t numParts, settingsIdx;
  size_t md5Pos;
  if(!desParseTable(buf, len, &numParts, &settingsIdx, &md5Pos)) return false;
  if(settingsIdx == 0xFFFF) return false;

  size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
  uint32_t off = 0, size = 0;
  for(int i = 0; i < 4; i++)
  {
    off  |= (uint32_t)buf[e + 4 + i] << (8 * i);
    size |= (uint32_t)buf[e + 8 + i] << (8 * i);
  }

  uint32_t want = env ? DES_SETTINGS_ENV1 : DES_SETTINGS_ENV0;
  if(off != want || size != DES_SETTINGS_SIZE) return false;
  if(off & 0xFFFu) return false;            // 4KB (only app slots need 64KB)
  if((uint64_t)off + size > 0x1000000ull) return false;

  uint8_t stored[16];
  MD5Builder md5;
  md5.begin();
  md5.add(buf, (size_t)numParts * DES_ENTRY_SIZE);
  md5.calculate();
  md5.getBytes(stored);
  return memcmp(stored, buf + md5Pos + 16, sizeof(stored)) == 0;
}

// Reads the canonical table straight from flash and reports which
// environment its `settings` entry points at. 0/1 on success, 0xFF when the
// table does not parse or the offset is not one of the two DES regions.
static uint8_t desGetCurrentEnv()
{
  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf) return 0xFF;

  uint8_t env = 0xFF;
  if(ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE))
  {
    uint16_t numParts, settingsIdx;
    size_t md5Pos;
    if(desParseTable(buf, DES_TABLE_SIZE, &numParts, &settingsIdx, &md5Pos)
       && settingsIdx != 0xFFFF)
    {
      size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
      uint32_t off = 0;
      for(int i = 0; i < 4; i++) off |= (uint32_t)buf[e + 4 + i] << (8 * i);
      if(off == DES_SETTINGS_ENV0) env = 0;
      else if(off == DES_SETTINGS_ENV1) env = 1;
    }
  }

  free(buf);
  return env;
}

// IDF treats the bootloader and partition table as write-protected and abort()
// on a write that reaches them (CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS), so
// the table cannot be erased with ESP.flashEraseSector as-is. This is the
// documented toggle; it only affects the window between the two calls below.
extern "C" esp_err_t esp_flash_set_dangerous_write_protection(esp_flash_t *chip,
                                                              const bool protect);

// Rewrites the canonical table at 0x8000 so `settings` belongs to `env`. The
// staged buffer is verified before the sector is erased and again after the
// write, so any failure before the erase leaves the table byte-for-byte
// intact. The caller must ESP.restart() afterwards because the running
// firmware has already cached the old partition list.
static bool patchPartitions(uint8_t env)
{
  // Already pointing where it should be - no reason to rewrite the sector.
  if(desGetCurrentEnv() == env) return true;

  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf)
  {
    Serial.println("patch: no memory");
    return false;
  }

  bool ok = ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
         && desBuildTable(buf, DES_TABLE_SIZE, env)
         && desVerifyTable(buf, DES_TABLE_SIZE, env);
  if(!ok)
  {
    Serial.println("patch: staged table failed verification");
    free(buf);
    return false;
  }

  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  ok = ESP.flashEraseSector(DES_PT_SECTOR)
    && ESP.flashWrite(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE);
  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  if(!ok)
  {
    Serial.println("patch: flash write failed");
    free(buf);
    return false;
  }

  memset(buf, 0, DES_TABLE_SIZE);
  ok = ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
    && desVerifyTable(buf, DES_TABLE_SIZE, env);
  Serial.printf("patch: env %u -> %s\n", env, ok ? "ok" : "FAILED");
  free(buf);
  return ok;
}

// Canonical Table A (DES ENV0) embedded in the image: 10 entries plus the MD5
// terminator, exactly the bytes of des/table_a.bin. Everything past 0x160 in
// the slot is erased flash. Keeping a copy here is what lets the splash check
// rebuild a table that esptool or an older release overwrote.
static const uint8_t DES_CANONICAL_TABLE[0x160] = {
    0xAA, 0x50, 0x01, 0x02, 0x00, 0x90, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x6E, 0x76, 0x73, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x6F, 0x74, 0x61, 0x64,
    0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x38, 0x00, 0x61, 0x70, 0x70, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x00, 0x11, 0x00, 0x00, 0x39, 0x00, 0x00, 0x00, 0x38, 0x00, 0x61, 0x70, 0x70, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x81, 0x00, 0x00, 0x71, 0x00, 0x00, 0x00, 0x15, 0x00, 0x66, 0x66, 0x61, 0x74,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x00, 0x12, 0x00, 0x00, 0x86, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x72, 0x65, 0x63, 0x6F,
    0x76, 0x65, 0x72, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x83, 0x00, 0x00, 0xA0, 0x00, 0x00, 0x50, 0x59, 0x00, 0x6C, 0x69, 0x74, 0x74,
    0x6C, 0x65, 0x66, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x02, 0x00, 0x50, 0xF9, 0x00, 0x00, 0x80, 0x00, 0x00, 0x72, 0x65, 0x63, 0x5F,
    0x73, 0x65, 0x74, 0x74, 0x69, 0x6E, 0x67, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x02, 0x00, 0xD0, 0xF9, 0x00, 0x00, 0x00, 0x02, 0x00, 0x73, 0x65, 0x74, 0x74,
    0x69, 0x6E, 0x67, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x03, 0x00, 0xF0, 0xFD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x63, 0x6F, 0x72, 0x65,
    0x64, 0x75, 0x6D, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x7B, 0x20, 0x02, 0x65, 0xF3, 0xDF, 0x2D, 0x81, 0x94, 0x15, 0xFB, 0x81, 0x3F, 0x1B, 0xC7, 0x76,
};

// Entry by entry comparison of the slot in flash against the embedded copy.
// Only one difference is legal: the `settings` offset, which may point at
// either DES environment. Everything else - count, order, magic, type,
// subtype, size, label and the MD5 digest - has to match byte for byte.
static bool desMatchesCanonical(const uint8_t *buf, size_t len)
{
  uint16_t numParts, settingsIdx;
  size_t md5Pos;
  if(!desParseTable(buf, len, &numParts, &settingsIdx, &md5Pos)) return false;

  uint16_t cNum, cSettings;
  size_t cMd5;
  if(!desParseTable(DES_CANONICAL_TABLE, sizeof(DES_CANONICAL_TABLE),
                    &cNum, &cSettings, &cMd5)) return false;
  if(numParts != cNum || settingsIdx != cSettings) return false;

  for(size_t i = 0; i < cNum; i++)
  {
    size_t pos = i * DES_ENTRY_SIZE;
    if(memcmp(buf + pos, DES_CANONICAL_TABLE + pos, DES_ENTRY_SIZE) == 0) continue;

    // The `settings` entry is the only one allowed to differ, and only in its
    // four offset bytes.
    if(i != (size_t)cSettings) return false;
    if(memcmp(buf + pos, DES_CANONICAL_TABLE + pos, 4) != 0) return false;
    if(memcmp(buf + pos + 8, DES_CANONICAL_TABLE + pos + 8, DES_ENTRY_SIZE - 8) != 0)
      return false;

    uint32_t off = 0;
    for(int b = 0; b < 4; b++) off |= (uint32_t)buf[pos + 4 + b] << (8 * b);
    if(off != DES_SETTINGS_ENV0 && off != DES_SETTINGS_ENV1) return false;
  }

  uint8_t env = 0;
  if(settingsIdx != 0xFFFF)
  {
    size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
    uint32_t off = 0;
    for(int b = 0; b < 4; b++) off |= (uint32_t)buf[e + 4 + b] << (8 * b);
    env = (uint8_t)(off == DES_SETTINGS_ENV1 ? 1 : 0);
  }
  return desVerifyTable(buf, len, env);
}

// Reads the slot and checks it against the embedded canonical table. Silent
// on pass; names the first entry that differs otherwise.
static bool partitionTableOk()
{
  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf) { Serial.println("self check: no memory"); return false; }

  bool ok = ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
         && desMatchesCanonical(buf, DES_TABLE_SIZE);

  if(!ok)
  {
    uint16_t numParts = 0, settingsIdx = 0xFFFF;
    size_t md5Pos = 0;
    if(ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
       && desParseTable(buf, DES_TABLE_SIZE, &numParts, &settingsIdx, &md5Pos))
      Serial.printf("self check: partition table differs (%u entries)\n", numParts);
    else
      Serial.println("self check: partition table unreadable");
  }
  free(buf);
  return ok;
}

// Rebuilds the slot from DES_CANONICAL_TABLE. `env` selects which DES
// environment the `settings` entry points at; ENV0 is the default so a slot
// that cannot be read at all still comes back to a known state.
static bool desRebuildTable(uint8_t env)
{
  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf) { Serial.println("rebuild: no memory"); return false; }

  // Everything past the embedded entries is erased flash, not zeroes.
  memset(buf, 0xFF, DES_TABLE_SIZE);
  memcpy(buf, DES_CANONICAL_TABLE, sizeof(DES_CANONICAL_TABLE));

  bool ok = desBuildTable(buf, DES_TABLE_SIZE, env)
         && desVerifyTable(buf, DES_TABLE_SIZE, env);
  if(!ok)
  {
    Serial.println("rebuild: staged table failed verification");
    free(buf);
    return false;
  }

  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  ok = ESP.flashEraseSector(DES_PT_SECTOR)
    && ESP.flashWrite(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE);
  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  if(!ok) Serial.println("rebuild: flash write failed");

  free(buf);
  if(!ok) return false;

  ok = partitionTableOk();
  Serial.printf("rebuild: env %u -> %s\n", env, ok ? "ok" : "FAILED");
  return ok;
}

// Blocking warning screen: header title, panel with two lines, wait for a
// click (or a hold) to continue.
static void uiBootWarning(const char *title, const char *l1, uint16_t c1,
                          const char *l2, uint16_t c2, const char *hintLeft)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader(title);

  uiPanel(24, 44, 272, 76, COL_WARN);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_WARN, COL_PANEL);
  gSprite.drawString("WARNING", 160, 58, FONT_SMALL);
  gSprite.setTextColor(c1, COL_PANEL);
  gSprite.drawString(l1, 160, 78, FONT_SMALL);
  gSprite.setTextColor(c2, COL_PANEL);
  gSprite.drawString(l2, 160, 98, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);

  uiHintBar(hintLeft, "CLICK=CONTINUE");
   gSprite.pushSprite(0, 0);
  while(readButton() == 0) delay(10);
  gSprite.pushSprite(0, 0);
}

// Splash side status screen: no input needed, it clears itself once the
// repair below has run. Click skips the wait.
static void uiSelfCheckStatus(const char *l1, uint16_t c1,
                              const char *l2, uint16_t c2)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("FLASH CHECK");

  uiPanel(24, 44, 272, 76, COL_WARN);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_WARN, COL_PANEL);
  gSprite.drawString("WARNING", 160, 58, FONT_SMALL);
  gSprite.setTextColor(c1, COL_PANEL);
  gSprite.drawString(l1, 160, 78, FONT_SMALL);
  gSprite.setTextColor(c2, COL_PANEL);
  gSprite.drawString(l2, 160, 98, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);

  uiHintBar("Repairing", "CLICK=SKIP");
  gSprite.pushSprite(0, 0);
}

// Runs on every boot while the splash is up. Reads the bootloader image and
// the partition slot, compares both against what this build shipped with, and
// stays silent when they agree - the whole check is a few hundred kilobytes
// of flash reads plus two memcmp passes, well inside the splash window.
//
// A partition table that differs is not just reported: the canonical table is
// embedded in this image, so the slot is rebuilt and the device restarts onto
// it. A bootloader mismatch cannot be repaired from here and only warns.
static void flashSelfCheck()
{
  uint32_t t0 = millis();
  bool bl = bootloaderOk();
  bool pt = partitionTableOk();
  uint32_t elapsed = millis() - t0;

  if(bl && pt)
  {
    Serial.printf("self check: pass in %lu ms\n", (unsigned long)elapsed);
    return;
  }

  if(!pt)
  {
    // Keep the environment that is already selected so a rebuild does not
    // silently flip App0/App1 settings.
    uint8_t env = desGetCurrentEnv();
    if(env == 0xFF) env = 0;

    Serial.printf("self check: partition table MISMATCH (%lu ms) -> rebuild\n",
                  (unsigned long)elapsed);
    uiSelfCheckStatus("Partitions: WRONG", COL_WARN,
                      "Rebuilding table...", COL_GOLD);

    uint32_t waitStart = millis();
    while(millis() - waitStart < 1500 && readButton() == 0) delay(10);

    if(desRebuildTable(env))
    {
      gSprite.setTextColor(COL_OK, COL_PANEL);
      gSprite.setTextDatum(TC_DATUM);
      gSprite.drawString("Repaired, restarting", 160, 78, FONT_SMALL);
      gSprite.setTextDatum(TL_DATUM);
      gSprite.pushSprite(0, 0);
      delay(800);
      ESP.restart();
    }

    uiBootWarning("FLASH CHECK",
                  "Partitions: WRONG", COL_WARN,
                  "Repair FAILED", COL_WARN,
                  "Check the image");
    return;
  }

  Serial.printf("self check: bootloader MISMATCH (%lu ms)\n",
                (unsigned long)elapsed);
  uiBootWarning("FLASH CHECK",
                "Bootloader: MISMATCH", COL_WARN,
                "Partitions: OK", COL_OK,
                "Reflash bootloader");
}

// A valid ESP32 application image starts with 0xE9 and declares 1..16
// segments; an erased slot reads 0xFF.
static bool appImageOk(const esp_partition_t *part)
{
  if(!part) return false;
  uint8_t hdr[24];
  if(esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK) return false;
  return hdr[0] == 0xE9 && hdr[1] >= 1 && hdr[1] <= 16;
}

static bool setBootSlot(int targetSlot);

static bool bootToApp(esp_partition_subtype_t appSubtype)
{
  const esp_partition_t *app = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, appSubtype, NULL);

  // Checked on every way of leaving for an app (auto boot, Boot App0/1 and
  // hold mode), so an empty slot warns instead of dropping into the
  // ROM loader.
  if(!appImageOk(app))
  {
    const char *name = (appSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
      ? "APP0" : "APP1";
    Serial.printf("boot blocked: %s has no valid image\n", name);
    uiBootWarning(name, "Slot is empty or", COL_WARN,
                  "not flashed correctly", COL_WARN,
                  "Slot must be flashed first");
    return false;                           // stay in the recovery
  }

  // The environment is a property of the slot, so the table is refreshed
  // before the slot is committed: patch, then otadata, then restart.
  if(!setBootSlot(appSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? 0 : 1))
  {
    uiWarn("Env switch failed");
    return false;                           // stay in the recovery
  }

  ESP.restart();
  return true;
}

// Selects a boot slot and keeps the partition table in step with it, which is
// what makes App0 and App1 independent: App0 always reads settings0 and App1
// always reads settings1, whichever path chose the slot. Returns false when
// the environment could not be switched, before any slot state is touched.
static bool setBootSlot(int targetSlot)
{
  if(!patchPartitions((uint8_t)targetSlot))
  {
    Serial.printf("slot %d: environment switch failed\n", targetSlot);
    return false;
  }

  esp_partition_subtype_t sub = (targetSlot == 0)
    ? static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0)
    : static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1);
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, NULL);
  bool ok = p && (esp_ota_set_boot_partition(p) == ESP_OK);
  // Recorded here so every path that commits a slot (auto boot, menu, web)
  // updates the value setup() reads on the next power-up.
  if(ok) saveBootSlot((uint8_t)targetSlot);
  return ok;
}

static bool bootToApp0()
{
  return bootToApp(static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0));
}

static bool bootToApp1()
{
  return bootToApp(static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1));
}

static void drawBootModeMenu(bool isApp1, int selected)
{
  static const int icons[BOOT_MODE_COUNT] = {ICON_BOOT, ICON_HOLD};
  static const uint16_t accs[BOOT_MODE_COUNT] = {COL_OK, COL_GOLD};

  static const char *desc[BOOT_MODE_COUNT] = {
    "Boots the app normally",
    "Press and keep holding"
  };

  drawTilePage(isApp1 ? "BOOT APP1" : "BOOT APP0", NULL,
               desc[selected], "CLICK=BACK  HOLD=SELECT",
               bootModeMenu, icons, accs, BOOT_MODE_COUNT, selected);
}

// Hold mode: only a notice, then the app boots with the encoder button still
// down so the target firmware sees a plain press. Nothing here looks at the
// button, so holding it never reboots the device and never re-enters the
// recovery menu.
static void runHoldModeBoot(bool isApp1)
{
  gSprite.fillScreen(COL_BG);
  drawHeader("HOLD MODE", isApp1 ? "App1" : "App0");
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_OK, COL_BG);
  gSprite.drawString("Press and keep holding", 160, 44, FONT_SMALL);
  gSprite.setTextColor(COL_TEXT, COL_BG);
  gSprite.drawString("Do not release the button", 160, 110, FONT_SMALL);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("Booting to the app...", 160, 152, FONT_SMALL);
  gSprite.drawRoundRect(28, 134, 264, 16, COL_MUTED);

  uint32_t heldSince = 0;
  int lastFrame = -1;

  while(true)
  {
    bool down = (digitalRead(ENCODER_PUSH_BUTTON) == LOW);
    uint32_t el = 0;

    if(down)
    {
      if(!heldSince) heldSince = millis();
      el = millis() - heldSince;
      if(el >= HOLD_NOTICE_MS) break;
    }
    else
    {
      heldSince = 0;
    }

    int frame = (down ? 2 : 0) + (int)((millis() / 400) & 1);
    if(frame != lastFrame)
    {
      lastFrame = frame;
      bool blink = frame & 1;
      if(down)
      {
        gSprite.fillRoundRect(6, 66, 308, 40, 6, blink ? COL_KEY : COL_WARN);
        gSprite.setTextColor(blink ? COL_OK : COL_TEXT, blink ? COL_KEY : COL_WARN);
        gSprite.drawString("KEEP HOLDING", 160, 70, FONT_LARGE);
      }
      else
      {
        gSprite.fillRoundRect(6, 66, 308, 40, 6, COL_KEY);
        gSprite.setTextColor(blink ? COL_OK : COL_TEXT, COL_KEY);
        gSprite.drawString("PRESS NOW", 160, 70, FONT_LARGE);
      }
    }

    uint32_t w = down ? (260UL * el / HOLD_NOTICE_MS) : 0;
    if(down && w < 2) w = 2;
    gSprite.fillRoundRect(30, 136, 260, 12, 5, COL_KEY);
    if(w) gSprite.fillRoundRect(30, 136, w, 12, 5, COL_OK);

    gSprite.pushSprite(0, 0);
    delay(10);
  }

  Serial.printf("step: hold mode -> boot App%d\n", isApp1 ? 1 : 0);
  if(!(isApp1 ? bootToApp1() : bootToApp0())) return;   // warned -> back to the menus
}

static void runBootModeMenu(bool isApp1)
{
  int selected = 0;
  drawBootModeMenu(isApp1, selected);

  while(true)
  {
    int8_t dir = readEncoder();
    if(dir)
    {
      selected += dir;
      while(selected < 0) selected += BOOT_MODE_COUNT;
      while(selected >= (int)BOOT_MODE_COUNT) selected -= BOOT_MODE_COUNT;
      drawBootModeMenu(isApp1, selected);
    }

    uint32_t held = readButton();
    if(held == 0) { delay(10); continue; }
    if(held < 300) return;                            // click = back

    if(selected == 0)
    {
      bool ok = isApp1 ? bootToApp1() : bootToApp0();
      if(!ok) return;                      // empty slot -> back to main menu
    }
    else runHoldModeBoot(isApp1);
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void drawMenu(int selected)
{
  static const int icons[MENU_COUNT] =
    { ICON_SLOT0, ICON_SLOT1, ICON_UPDATE, ICON_ERASE, ICON_SETTINGS };
  static const uint16_t accs[MENU_COUNT] =
    { COL_ACC, COL_ACC, COL_GOLD, COL_WARN, COL_OK };

  drawTilePage("Boot Manager", gBootSlot, "TURN=MOVE", "HOLD=OPEN",
               menu, icons, accs, MENU_COUNT, selected);
}

static void drawWifiMenu(int selected)
{
  static const int icons[WIFI_MENU_COUNT] = {ICON_WIFI, ICON_WEB};
  static const uint16_t accs[WIFI_MENU_COUNT] = {COL_ACC, COL_GOLD};

  String status;
  if(apModeActive) status = "AP Mode";
  else if(WiFi.status() == WL_CONNECTED) status = WiFi.SSID();
  else status = "Offline";

  drawTilePage("Network", NULL, status.c_str(), "CLICK=BACK  HOLD=OPEN",
               wifiMenu, icons, accs, WIFI_MENU_COUNT, selected);
}

// ---------------------------------------------------------------------------
// Settings submenu (WiFi / About / Brightness)
// ---------------------------------------------------------------------------

static void drawSettingsMenu(int selected)
{
  static const int icons[SETTINGS_COUNT] =
    {ICON_WIFI, ICON_BRIGHT, ICON_ABOUT};
  static const uint16_t accs[SETTINGS_COUNT] = {COL_ACC, COL_OK, COL_GOLD};

  static const char *hints[SETTINGS_COUNT] = {
    "Connect to a network",
    "LCD backlight level",
    "Version, licenses, QR"
  };

  drawTilePage("SETTINGS", NULL, hints[selected], "CLICK=BACK  HOLD=OPEN",
               settingsMenu, icons, accs, SETTINGS_COUNT, selected);
}

static void drawBrightness(int pct)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("BRIGHTNESS");

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_ACC2, COL_BG);
  gSprite.drawString(buf, 160, 52, FONT_LARGE);

  gSprite.fillRoundRect(30, 112, 260, 16, 8, COL_TRACK);
  gSprite.drawRoundRect(30, 112, 260, 16, 8, COL_LINE);
  int w = (pct * 256) / 100;
  if(w > 0) gSprite.fillRoundRect(32, 114, w, 12, 6, COL_ACC);

  char range[24];
  snprintf(range, sizeof(range), "%d%% - %d%%", BRIGHT_MIN, BRIGHT_MAX);
  uiHintBar(range, "TURN=ADJUST  CLICK=BACK");
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
}

static void runBrightness()
{
  int b = loadBrightness();
  drawBrightness(b);

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      b += d * BRIGHT_STEP;
      if(b < BRIGHT_MIN) b = BRIGHT_MIN;
      if(b > BRIGHT_MAX) b = BRIGHT_MAX;
      applyBrightness(b);
      drawBrightness(b);
    }

    uint32_t h = readButton();
    if(h > 0)
    {
      saveBrightness(b);
      return;
    }
    delay(10);
  }
}

static void runAbout();
static void runWifiMenu();

static void runSettingsMenu()
{
  int selected = 0;
  gUiRefresh = false;
  drawSettingsMenu(selected);

  while(true)
  {
    if(gUiRefresh) { gUiRefresh = false; drawSettingsMenu(selected); }

    int8_t dir = readEncoder();
    if(dir)
    {
      selected = (selected + dir) % (int)SETTINGS_COUNT;
      if(selected < 0) selected += SETTINGS_COUNT;
      drawSettingsMenu(selected);
    }

    uint32_t held = readButton();
    if(held == 0) { delay(10); continue; }
    if(held < 300) return;                                    // click = back

    if(selected == 0) runWifiMenu();
    else if(selected == 1) runBrightness();
    else runAbout();

    drawSettingsMenu(selected);
    delay(10);
  }
}

static void drawUpdateProgress(int percent, const char *status, const char *detail = nullptr)
{
  gSprite.fillRect(0, 26, SCR_W, 120, COL_BG);

  const int x0 = 20, y0 = 42, w = 280, h = 94;
  uiPanel(x0, y0, w, h, COL_ACC);

  int cxc = SCR_W / 2;
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString(status, cxc, y0 + 16, FONT_SMALL);
  if(detail)
  {
    gSprite.setTextColor(COL_MUTED, COL_PANEL);
    gSprite.drawString(detail, cxc, y0 + 36, FONT_TINY);
  }

  gSprite.fillRoundRect(x0 + 16, y0 + 60, w - 32, 18, 9, COL_TRACK);
  gSprite.drawRoundRect(x0 + 16, y0 + 60, w - 32, 18, 9, COL_LINE);
  int bw = (w - 38) * percent / 100;
  if(bw > 0) gSprite.fillRoundRect(x0 + 19, y0 + 63, bw, 12, 6, COL_ACC);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  gSprite.setTextColor(COL_GOLD, COL_TRACK);
  gSprite.drawString(buf, cxc, y0 + 63, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);

  drawNetIcon();
  gSprite.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// Firmware flashing (app OTA)
// ---------------------------------------------------------------------------

static int listFirmwareFiles(String *names, int maxCount)
{
  int count = 0;
  File root = LittleFS.open("/", "r");
  if(!root || !root.isDirectory()) return 0;

  File file = root.openNextFile();
  while(file && count < maxCount)
  {
    String name = file.name();
    if(name.endsWith(".bin"))
      names[count++] = name;
    file = root.openNextFile();
  }
  root.close();
  return count;
}

static void drawFilePage(String *files, int count, int selected, const char *targetLabel)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("FIRMWARE UPDATE");

  gSprite.setTextColor(COL_MUTED, COL_BG);
  String target = String("Target: ") + targetLabel + "  Select .bin:";
  gSprite.drawString(target, 8, 28, FONT_SMALL);

  const int total = count;
  int page = selected / FILE_PAGE_SIZE;
  int start = page * FILE_PAGE_SIZE;
  int end = start + FILE_PAGE_SIZE;
  if(end > total) end = total;

  for(int i = start; i < end; i++)
    uiRow(8, 48 + (i - start) * 25, 304, 23, files[i].c_str(),
          i == selected, COL_GOLD);

  int pages = (total + FILE_PAGE_SIZE - 1) / FILE_PAGE_SIZE;
  if(pages < 1) pages = 1;
  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d/%d", page + 1, pages);
  uiHintBar(pageBuf, "CLICK=BACK  HOLD=FLASH");
  gSprite.pushSprite(0, 0);
}

// One row of drawChoiceList. scrollX > 0 shifts the text left inside the row
// box, so a focused row too wide for the box (long URLs) can be read to the end.
static void drawChoiceRow(const char *item, int row, bool focused, int scrollX)
{
  int y = 48 + row * 25;
  if(focused)
  {
    gSprite.fillRoundRect(8, y, 304, 23, 6, COL_HL);
    gSprite.drawRoundRect(8, y, 304, 23, 6, COL_ACC);
    gSprite.fillRoundRect(10, y + 4, 4, 15, 2, COL_ACC);
    gSprite.setTextColor(COL_ACC, COL_HL);
  }
  else
  {
    gSprite.fillRoundRect(8, y, 304, 23, 6, COL_PANEL);
    gSprite.drawRoundRect(8, y, 304, 23, 6, COL_LINE);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
  }

  gSprite.setTextDatum(TL_DATUM);
  gSprite.setClipRect(18, y, 294, 23);
  gSprite.drawString(item, 24 - scrollX, y + 3, FONT_SMALL);
  gSprite.clearClipRect();
}

static void drawChoiceList(const char *title, const char *hint,
                           const char **items, int itemCount, int selected)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader(title);

  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString(hint, 8, 28, FONT_SMALL);

  int page = selected / FILE_PAGE_SIZE;
  int start = page * FILE_PAGE_SIZE;
  int end = start + FILE_PAGE_SIZE;
  if(end > itemCount) end = itemCount;

  for(int i = start; i < end; i++)
    drawChoiceRow(items[i], i - start, i == selected, 0);

  char pageBuf[24] = "";
  if(itemCount > FILE_PAGE_SIZE)
  {
    int pages = (itemCount + FILE_PAGE_SIZE - 1) / FILE_PAGE_SIZE;
    snprintf(pageBuf, sizeof(pageBuf), "Page %d/%d", page + 1, pages);
  }
  uiHintBar(pageBuf, "CLICK=BACK  HOLD=SELECT");
  gSprite.pushSprite(0, 0);
}

// Generic choice list: rotate to move, hold to pick, click to back out
// (returns -1).
static int runChoice(const char *title, const char *hint,
                     const char **items, int itemCount)
{
  int total = itemCount;
  if(total > 15) total = 15;

  int selected = 0;
  drawChoiceList(title, hint, items, total, selected);

  // Marquee: a focused row wider than the box scrolls until it has passed
  // completely, then starts over from the head of the item. Four frames per
  // second; the step is sized so one loop takes about 10 s.
  int32_t scroll = 0;
  int32_t step = 1;
  uint32_t scrollAt = millis();

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      selected = (selected + d) % total;
      if(selected < 0) selected += total;
      scroll = 0;
      scrollAt = millis();
      drawChoiceList(title, hint, items, total, selected);
    }

    int32_t width = gSprite.textWidth(items[selected], FONT_SMALL);
    if(width > 300 && millis() - scrollAt >= 250)
    {
      scrollAt = millis();
      if(scroll == 0)
      {
        step = (width + 45) / 40;        // one loop = ~40 ticks = 10 s
        if(step < 1) step = 1;
      }
      scroll += step;
      if(scroll > width + 5) scroll = 0; // fully left of the window: loop
      drawChoiceRow(items[selected], selected % FILE_PAGE_SIZE, true, scroll);
      gSprite.pushSprite(0, 0);           // row redraw is not pushed elsewhere
    }

    uint32_t h = readButton();
    if(h >= 300) return selected;
    if(h > 0) return -1;                         // click = back
    delay(10);
  }
}

static uint32_t findFirstAppOffset(File &file, size_t fileSize)
{
  if(fileSize < (0x8000 + 32)) return 0;

  file.seek(0x8000);
  for(int i = 0; i < 95; i++)
  {
    uint8_t e[32];
    if(file.read(e, 32) != 32) break;
    if(e[0] != 0xAA || e[1] != 0x50) break;

    uint8_t type = e[2];
    uint8_t sub  = e[3];
    uint32_t addr = e[4] | (e[5] << 8) | (e[6] << 16) | ((uint32_t)e[7] << 24);

    if(type == 0x00 && sub >= 0x10 && sub <= 0x1F) return addr;
  }
  return 0;
}

static size_t computeAppImageSize(File &file, uint32_t appOffset, size_t fileSize)
{
  if((appOffset + 24) > fileSize) return 0;

  file.seek(appOffset);
  uint8_t hdr[24];
  if(file.read(hdr, 24) != 24 || hdr[0] != 0xE9) return 0;

  uint8_t segCount = hdr[1];
  uint32_t pos = 24;

  for(uint8_t i = 0; i < segCount; i++)
  {
    uint8_t sh[8];
    if(file.read(sh, 8) != 8) return 0;
    uint32_t dlen = sh[4] | (sh[5] << 8) | (sh[6] << 16) | ((uint32_t)sh[7] << 24);
    pos += 8 + dlen;
    if((appOffset + pos) > fileSize) return 0;
    file.seek(appOffset + pos);
  }

  pos += 1;
  if(pos % 16) pos += 16 - (pos % 16);
  if(hdr[23] == 1) pos += 32;

  return pos;
}

// ---------------------------------------------------------------------------
// Web flash progress
// Written by the one-shot flash task, read by the web task while it serves
// /flashstatus.  gFlashPct: -1 idle, 0..100 running, 101 ok, 102 failed.
// ---------------------------------------------------------------------------

static volatile bool gWebFlash = false;
static volatile uint32_t gUploadLastMs = 0;   // 0 = no upload in flight
static volatile int  gFlashPct = -1;
static char          gFlashMsg[48];
static portMUX_TYPE  gFlashMux = portMUX_INITIALIZER_UNLOCKED;

static const uint32_t UPLOAD_BUSY_MS = 15000;

static void flashSetProgress(int pct, const char *msg)
{
  taskENTER_CRITICAL(&gFlashMux);
  strlcpy(gFlashMsg, msg, sizeof(gFlashMsg));
  gFlashPct = pct;
  taskEXIT_CRITICAL(&gFlashMux);
}

static void flashGetProgress(int *pct, char *msg, size_t len)
{
  taskENTER_CRITICAL(&gFlashMux);
  *pct = gFlashPct;
  strlcpy(msg, gFlashMsg, len);
  taskEXIT_CRITICAL(&gFlashMux);
}

// An upload owns the device while bytes are arriving.  It expires on its own
// so a client that drops mid-upload can never leave the device stuck busy.
static bool uploadBusy()
{
  uint32_t last = gUploadLastMs;
  return last != 0 && (millis() - last) < UPLOAD_BUSY_MS;
}

static void uiWarn(const char *msg)
{
  if(gWebFlash) flashSetProgress(102, msg);
  if(gUiQuiet) return;

  int w = gSprite.textWidth(msg, FONT_SMALL) + 44;
  if(w < 250) w = 250;
  int x0 = (SCR_W - w) / 2, y0 = 60;
  uiPanel(x0, y0, w, 46, COL_WARN);

  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_WARN, COL_PANEL);
  gSprite.drawString(msg, 160, y0 + 20, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);
   gSprite.pushSprite(0, 0);
  delay(2000);
  gSprite.pushSprite(0, 0);
}

static void uiNote(const char *msg)
{
  if(gUiQuiet) return;
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString(msg, 8, 28, FONT_SMALL);
  gSprite.pushSprite(0, 0);
}

static void uiProgress(int percent, const char *status, const char *detail = nullptr)
{
  if(gWebFlash) flashSetProgress(percent, status);
  if(gUiQuiet) return;
  drawUpdateProgress(percent, status, detail);
}

static bool flashFirmware(const String &filename, int targetSlot)
{
  File file = LittleFS.open("/" + filename, "r");
  if(!file || file.size() == 0)
  {
    uiWarn("Failed to open file");
    return false;
  }

  esp_partition_subtype_t targetSubtype = (targetSlot == 0)
    ? static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0)
    : static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1);

  const esp_partition_t *target = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, targetSubtype, NULL);

  if(!target)
  {
    uiWarn("Target partition missing");
    file.close();
    return false;
  }

  if(target == esp_ota_get_running_partition())
  {
    uiWarn("Cannot update running slot");
    file.close();
    return false;
  }

  size_t fileSize = file.size();
  uint32_t appOffset = 0;
  size_t appSize = fileSize;
  bool merged = false;

  if(fileSize > 0x8002)
  {
    uint8_t magic[2];
    file.seek(0);
    file.read(magic, 2);
    bool imageAtZero = (magic[0] == 0xE9);
    file.seek(0x8000);
    file.read(magic, 2);
    bool tableAt8000 = (magic[0] == 0xAA && magic[1] == 0x50);

    if(imageAtZero && tableAt8000)
    {
      uint32_t off = findFirstAppOffset(file, fileSize);
      size_t sz = off ? computeAppImageSize(file, off, fileSize) : 0;

      if(off && sz && sz <= fileSize)
      {
        appOffset = off;
        appSize = sz;
        merged = true;
      }
    }
  }

  if(merged) uiNote("Merged image -> app only");

  esp_ota_handle_t handle = 0;
  if(esp_ota_begin(target, appSize, &handle) != ESP_OK)
  {
    uiWarn("Update begin failed");
    file.close();
    return false;
  }

  file.seek(appOffset);
  uint8_t buf[1024];
  size_t written = 0;
  int lastPercent = -1;

  char detail[64];
  snprintf(detail, sizeof(detail), "%s -> App%d", filename.c_str(), targetSlot);

  while(written < appSize)
  {
    size_t toRead = file.read(buf, sizeof(buf));
    if(toRead == 0) break;
    if((written + toRead) > appSize) toRead = appSize - written;

    if(esp_ota_write(handle, buf, toRead) != ESP_OK)
    {
      uiWarn("Write error");
      esp_ota_abort(handle);
      file.close();
      return false;
    }
    written += toRead;
    int percent = (written * 100) / appSize;
    if(percent != lastPercent)
    {
      uiProgress(percent, "Flashing...", detail);
      lastPercent = percent;
    }
  }
  file.close();

  if(written != appSize || esp_ota_end(handle) != ESP_OK)
  {
    uiWarn("Update failed");
    return false;
  }

  if(!gUiQuiet)
  {
    drawUpdateProgress(100, "Done!", detail);
    delay(1000);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Network download
// ---------------------------------------------------------------------------

static String urlToFileName(const String &url)
{
  String u = url;
  int q = u.indexOf('?');
  if(q > 0) u = u.substring(0, q);
  int hash = u.indexOf('#');
  if(hash > 0) u = u.substring(0, hash);
  u.trim();
  int sl = u.lastIndexOf('/');
  String name = (sl >= 0) ? u.substring(sl + 1) : u;
  if(name.length() == 0) name = "download.bin";
  return name;
}

static void appendUrlLines(const String &text, String *urls, int maxCount, int &n)
{
  int start = 0;
  while(start < (int)text.length() && n < maxCount)
  {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    line.trim();
    start = (nl < 0) ? text.length() : nl + 1;
    if(line.length() == 0) continue;
    if(line.startsWith("#")) continue;
    if(line.startsWith("http://") || line.startsWith("https://"))
      urls[n++] = line;
  }
}

static bool fetchTextUrl(const String &url, String &out)
{
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if(!http.begin(url)) return false;
  int code = http.GET();
  if(code != HTTP_CODE_OK)
  {
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  return out.length() > 0;
}

static int loadUrlList(String *urls, int maxCount)
{
  int n = 0;

  // Remote update_url.txt takes priority
  if(WiFi.status() == WL_CONNECTED && REMOTE_UPDATE_URL_TXT[0])
  {
    String body;
    if(fetchTextUrl(REMOTE_UPDATE_URL_TXT, body))
      appendUrlLines(body, urls, maxCount, n);
  }

  // Local LittleFS copy
  File f = LittleFS.open("/update_url.txt", "r");
  if(f)
  {
    String body = f.readString();
    f.close();
    appendUrlLines(body, urls, maxCount, n);
  }

  // Compiled fallbacks last
  for(int i = 0; i < MAX_URLS && n < maxCount; i++)
  {
    if(DEFAULT_UPDATE_URLS[i] && DEFAULT_UPDATE_URLS[i][0])
      urls[n++] = DEFAULT_UPDATE_URLS[i];
  }
  return n;
}

static bool downloadToFile(const String &url, const String &fname)
{
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  if(!http.begin(url)) return false;

  int code = http.GET();
  if(code != HTTP_CODE_OK)
  {
    http.end();
    return false;
  }

  int total = http.getSize();
  File out = LittleFS.open("/" + fname, FILE_WRITE);
  if(!out)
  {
    http.end();
    return false;
  }

  uint8_t buf[1024];
  int got = 0;
  int lastPercent = -1;
  uint32_t lastData = millis();
  WiFiClient *stream = http.getStreamPtr();

  gSprite.fillScreen(COL_BG);
  drawHeader("DOWNLOAD");
  gSprite.setTextColor(COL_TEXT, COL_BG);
  gSprite.drawString(fname, 10, 40, FONT_SMALL);
  gSprite.pushSprite(0, 0);

  while(http.connected() || stream->available())
  {
    size_t avail = stream->available();
    if(avail)
    {
      size_t r = stream->readBytes(buf, avail < sizeof(buf) ? avail : sizeof(buf));
      if(r == 0) break;
      out.write(buf, r);
      got += (int)r;
      lastData = millis();
      if(total > 0)
      {
        int percent = (int)((int64_t)got * 100 / total);
        if(percent != lastPercent)
        {
          drawUpdateProgress(percent, "Downloading...", fname.c_str());
          lastPercent = percent;
        }
        if(got >= total) break;
      }
    }
    else
    {
      if((millis() - lastData) > 15000) break;
      delay(1);
    }
  }

  out.close();
  http.end();

  if(total > 0) return got == total;
  return got > 0;
}

static int downloadAllFromUrls()
{
  String urls[MAX_URLS];
  int urlCount = loadUrlList(urls, MAX_URLS);

  if(urlCount == 0)
  {
    gSprite.fillScreen(COL_BG);
    drawHeader("NETWORK");
    gSprite.setTextColor(COL_WARN, COL_BG);
    gSprite.drawString("No URLs configured", 10, 50, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("Upload update_url.txt", 10, 80, FONT_SMALL);
    gSprite.drawString("or edit DEFAULT_UPDATE_URLS", 10, 98, FONT_SMALL);
    gSprite.drawString("Click to go back", 10, 140, FONT_SMALL);
    gSprite.pushSprite(0, 0);
    while(true)
    {
      uint32_t h = readButton();
      if(h > 0) break;
      delay(10);
    }
    return 0;
  }

  const char *pick[MAX_URLS];
  int pickCount = 0;
  for(int i = 0; i < urlCount; i++)
    pick[pickCount++] = urls[i].c_str();

  if(pickCount >= 2)
  {
    int sel = runChoice("Download", "Select URL:", pick, pickCount);
    if(sel < 0) return 0;
    String chosen = pick[sel];
    String fname = urlToFileName(chosen);
    if(downloadToFile(chosen, fname)) return 1;
    gSprite.fillScreen(COL_BG);
    drawHeader("DOWNLOAD");
    gSprite.setTextColor(COL_WARN, COL_BG);
    gSprite.drawString("Failed:", 10, 50, FONT_SMALL);
    gSprite.drawString(fname, 10, 70, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("Click to go back", 10, 140, FONT_SMALL);
    gSprite.pushSprite(0, 0);
    while(true)
    {
      uint32_t h = readButton();
      if(h > 0) break;
      delay(10);
    }
    return 0;
  }

  String fname = urlToFileName(pick[0]);
  bool ok = downloadToFile(pick[0], fname);
  if(!ok)
  {
    gSprite.fillScreen(COL_BG);
    drawHeader("DOWNLOAD");
    gSprite.setTextColor(COL_WARN, COL_BG);
    gSprite.drawString("Failed:", 10, 50, FONT_SMALL);
    gSprite.drawString(fname, 10, 70, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("Click to go back", 10, 140, FONT_SMALL);
    gSprite.pushSprite(0, 0);
    while(true)
    {
      uint32_t h = readButton();
      if(h > 0) break;
      delay(10);
    }
    return 0;
  }
  return 1;
}

// ---------------------------------------------------------------------------
// Firmware update flow: target -> source -> file -> flash
// ---------------------------------------------------------------------------

static void runFirmwareUpdate()
{
  static const char *slotNames[] = {"App0", "App1"};
  static const int slotIcons[] = {ICON_SLOT0, ICON_SLOT1};
  static const uint16_t slotAccs[] = {COL_ACC, COL_ACC};
  int slotSel = runTileChoice("FIRMWARE UPDATE", "Select target slot:",
                              "CLICK=BACK  HOLD=SELECT",
                              slotNames, slotIcons, slotAccs, 2);
  if(slotSel < 0) return;

  static const char *sourceNames[] = {"Local files", "Network"};
  static const int sourceIcons[] = {ICON_FOLDER, ICON_NET};
  static const uint16_t sourceAccs[] = {COL_GOLD, COL_ACC};
  int srcSel = runTileChoice("FIRMWARE UPDATE", "Select source:",
                             "CLICK=BACK  HOLD=SELECT",
                             sourceNames, sourceIcons, sourceAccs, 2);
  if(srcSel < 0) return;

  if(srcSel == 1)
  {
    int got = downloadAllFromUrls();
    if(got == 0)
    {
      gSprite.fillScreen(COL_BG);
      uiDotGrid();
      drawHeader("NETWORK");
      gSprite.setTextColor(COL_WARN, COL_BG);
      gSprite.drawString("No files downloaded", 8, 60, FONT_SMALL);
      uiHintBar(NULL, "CLICK=BACK");
       gSprite.pushSprite(0, 0);
      delay(2000);
      return;
    }
  }

  String files[32];
  int fileCount = listFirmwareFiles(files, 32);
  if(fileCount == 0)
  {
    gSprite.fillScreen(COL_BG);
    uiDotGrid();
    drawHeader("FIRMWARE UPDATE");
    gSprite.setTextColor(COL_WARN, COL_BG);
    gSprite.drawString("No .bin files found", 8, 52, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("Use Network source or", 8, 86, FONT_SMALL);
    gSprite.drawString("Upload via web browser.", 8, 106, FONT_SMALL);
    uiHintBar(NULL, "CLICK=BACK");
     gSprite.pushSprite(0, 0);
    while(readButton() == 0) delay(10);
    return;
  }

  const int fileTotal = fileCount;
  int fileSel = 0;
  drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);

  while(true)
  {
    int8_t fd = readEncoder();
    if(fd)
    {
      fileSel = (fileSel + fd) % fileTotal;
      if(fileSel < 0) fileSel += fileTotal;
      drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
    }

    uint32_t h = readButton();
    if(h == 0) { delay(10); continue; }
    if(h < 300) break;                                // click = back

    if(gFlashBusy || uploadBusy())
    {
      gSprite.fillScreen(COL_BG);
      uiDotGrid();
      drawHeader("FIRMWARE UPDATE");
      gSprite.setTextColor(COL_WARN, COL_BG);
      gSprite.drawString("Web flash running", 8, 60, FONT_SMALL);
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString("Wait for it to finish", 8, 80, FONT_SMALL);
      uiHintBar(NULL, "CLICK=BACK");
       gSprite.pushSprite(0, 0);
      while(readButton() == 0) delay(10);
      drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
      continue;
    }

    String fname = files[fileSel];
    if(fname.length() > 24) fname = fname.substring(0, 21) + "...";
    char slotLine[32];
    snprintf(slotLine, sizeof(slotLine), "flash to %s?", slotNames[slotSel]);
    if(!confirmDialog("FIRMWARE UPDATE", fname.c_str(), slotLine))
    {
      drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
      continue;
    }

    gFlashBusy = true;
    bool ok = flashFirmware(files[fileSel], slotSel);
    gFlashBusy = false;
    if(ok && !setBootSlot(slotSel)) uiWarn("Could not select boot slot");
    else if(ok)
    {
      delay(500);
      ESP.restart();
    }
    drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
  }
}

// ---------------------------------------------------------------------------
// Erase
// ---------------------------------------------------------------------------

// Two scopes behind the Erase tile: Factory Reset wipes the per environment
// settings regions, Erase wipes the app slots and the filesystem. The shared
// nvs at 0x9000 is deliberately left alone. Tile labels stay short because a
// three tile row is only 96px wide.
struct EraseList
{
  const char *title;
  bool factory;
  const char *items[3];
  const int icons[3];
  const uint16_t accs[3];
};

static const EraseList kEraseLists[2] = {
  {"FACTORY RESET", true,
   {"App0 cfg", "App1 cfg", "Recovery"},
   {ICON_SLOT0, ICON_SLOT1, ICON_SETTINGS},
   {COL_ACC, COL_ACC, COL_OK}},
  {"ERASE", false,
   {"App0", "App1", "LittleFS"},
   {ICON_SLOT0, ICON_SLOT1, ICON_DB},
   {COL_ACC, COL_ACC, COL_GOLD}}
};

static void drawEraseList(const EraseList &list, const bool *checked, int selected)
{
  char buf[3][24];
  const char *labels[3];
  uint16_t accs[3];

  int checkedCount = 0;
  for(int i = 0; i < 3; i++)
  {
    snprintf(buf[i], sizeof(buf[i]), "[%c] %s",
             checked[i] ? 'X' : ' ', list.items[i]);
    labels[i] = buf[i];
    accs[i] = checked[i] ? COL_WARN : list.accs[i];
    if(checked[i]) checkedCount++;
  }

  char left[24];
  snprintf(left, sizeof(left), "Checked %d/3", checkedCount);

  drawTilePage(list.title, NULL, left, "CLICK=CHECK  HOLD=ERASE  EMPTY=BACK",
               labels, list.icons, accs, 3, selected);
}

static void drawResetProgress(const char *label, int percent, int overall)
{
  gSprite.fillRect(0, 30, SCR_W, 114, COL_BG);

  const int x0 = 20, y0 = 34, w = 280, h = 96;
  uiPanel(x0, y0, w, h, COL_WARN);

  int cxc = SCR_W / 2;
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString(label, cxc, y0 + 16, FONT_SMALL);

  gSprite.fillRoundRect(x0 + 16, y0 + 40, w - 32, 20, 10, COL_TRACK);
  gSprite.drawRoundRect(x0 + 16, y0 + 40, w - 32, 20, 10, COL_LINE);
  int bw = (w - 38) * percent / 100;
  if(bw > 0) gSprite.fillRoundRect(x0 + 19, y0 + 43, bw, 14, 7, COL_WARN);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  gSprite.setTextColor(COL_GOLD, COL_TRACK);
  gSprite.drawString(buf, cxc, y0 + 46, FONT_TINY);

  gSprite.setTextColor(COL_MUTED, COL_PANEL);
  gSprite.drawString("Overall", cxc, y0 + 66, FONT_TINY);
  gSprite.fillRoundRect(x0 + 40, y0 + 78, w - 80, 8, 4, COL_TRACK);
  int ow = (w - 86) * overall / 100;
  if(ow > 0) gSprite.fillRoundRect(x0 + 43, y0 + 80, ow, 4, 2, COL_OK);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
}

static bool erasePartitionProgress(const esp_partition_t *part, const char *label,
                                   int overallStart, int overallEnd)
{
  const size_t chunk = 0x10000;
  for(size_t done = 0; done < part->size;)
  {
    size_t len = part->size - done;
    if(len > chunk) len = chunk;
    if(esp_partition_erase_range(part, done, len) != ESP_OK) return false;
    done += len;
    int pct = (int)((uint64_t)done * 100 / part->size);
    drawResetProgress(label, pct, overallStart + (overallEnd - overallStart) * pct / 100);
  }
  return true;
}

// Raw sector erase for the two DES settings regions, which the partition table
// declares as one "settings" slot at a time and never both at once.
static bool eraseRegionProgress(uint32_t addr, uint32_t len, const char *label,
                               int overallStart, int overallEnd)
{
  if((addr & 0xFFF) || (len & 0xFFF) || len == 0) return false;

  uint32_t sectors = len / 4096;
  for(uint32_t i = 0; i < sectors; i++)
  {
    if(!ESP.flashEraseSector((addr >> 12) + i)) return false;
    int pct = (int)((uint64_t)(i + 1) * 100 / sectors);
    drawResetProgress(label, pct, overallStart + (overallEnd - overallStart) * pct / 100);
  }
  return true;
}

// ---------------------------------------------------------------------------
// littlefs upgrade guard
//
// The superblock sits at the head of block 0: magic at +0x08, block_size at
// +0x18, block_count at +0x1C (littlefs v2). A volume written by an older
// layout still reports the size it was formatted with, and littlefs asserts
// `block_count >= lfs->block_count` on the very first mount, which reboots
// the board forever. Catch it here, before anything mounts the volume.
// ---------------------------------------------------------------------------

static bool littlefsSuperblockTooBig(const esp_partition_t *fs, uint32_t *blocks)
{
  uint8_t hdr[32];
  if(esp_partition_read(fs, 0, hdr, sizeof(hdr)) != ESP_OK) return false;

  static const uint8_t magic[8] = {'l', 'i', 't', 't', 'l', 'e', 'f', 's'};
  if(memcmp(hdr + 8, magic, sizeof(magic)) != 0) return false;   // blank or junk

  uint32_t bs = (uint32_t)hdr[0x18] | ((uint32_t)hdr[0x19] << 8) |
                ((uint32_t)hdr[0x1A] << 16) | ((uint32_t)hdr[0x1B] << 24);
  uint32_t bc = (uint32_t)hdr[0x1C] | ((uint32_t)hdr[0x1D] << 8) |
                ((uint32_t)hdr[0x1E] << 16) | ((uint32_t)hdr[0x1F] << 24);
  if(blocks) *blocks = bc;
  if(bs == 0 || bc == 0) return true;

  // Only a volume bigger than its partition can trip the assert; a smaller
  // one mounts fine and just leaves the tail unused.
  return (uint64_t)bc * bs > (uint64_t)fs->size;
}

// A volume can also claim more blocks than the partition while its superblock
// header looks fine. That case only shows up once it is mounted, so it is
// probed with growth disabled: the wrapper mounts with `grow_on_mount`, and
// esp_littlefs then grows to the partition size, which asserts and reboots the
// board forever when the on-disk volume is larger. Returns true when the
// mounted volume reports more bytes than the partition holds.
static bool littlefsVolumeTooBig(const esp_partition_t *fs)
{
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/lfsprobe";
  conf.partition_label = "littlefs";
  conf.partition = NULL;
  conf.format_if_mount_failed = false;
  conf.read_only = true;
  conf.dont_mount = false;
  conf.grow_on_mount = false;

  // Blank or corrupt volumes do not mount; those are safe, because the
  // wrapper's own mount fails before it can grow and is then formatted.
  if(esp_vfs_littlefs_register(&conf) != ESP_OK) return false;

  size_t total = 0, used = 0;
  bool tooBig = (esp_littlefs_info("littlefs", &total, &used) == ESP_OK)
             && (total > fs->size);
  esp_vfs_littlefs_unregister("littlefs");
  return tooBig;
}

// Returns true when littlefs may be mounted. A stale superblock is confirmed
// with one button and erased with progress (about 20 s for 5.58 MB, once);
// if that erase fails nothing mounts the volume and the app is not started.
static bool littlefsUpgradeCheck()
{
  const esp_partition_t *fs = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, NULL);
  if(!fs) return true;

  uint32_t blocks = 0;
  bool headerTooBig = littlefsSuperblockTooBig(fs, &blocks);

  // A volume whose header looks fine can still claim more blocks than the
  // partition once mounted, so it is probed too. A blank volume carries no
  // superblock and is left to the wrapper, which simply formats it.
  bool probeTooBig = false;
  if(!headerTooBig)
  {
    static const uint8_t magic[8] = {'l', 'i', 't', 't', 'l', 'e', 'f', 's'};
    uint8_t m[8];
    probeTooBig = (esp_partition_read(fs, 8, m, sizeof(m)) == ESP_OK)
               && (memcmp(m, magic, sizeof(magic)) == 0)
               && littlefsVolumeTooBig(fs);
  }

  if(!headerTooBig && !probeTooBig) return true;

  if(headerTooBig)
    Serial.printf("littlefs: stale superblock %lu blocks for %u B partition -> erase\n",
                  (unsigned long)blocks, (unsigned)fs->size);
  else
    Serial.printf("littlefs: volume larger than %u B partition -> erase\n",
                  (unsigned)fs->size);

  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("LITTLEFS");
  confirmOne("LITTLEFS", "Old format found", "Erase it to continue", "Erase");

  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("LITTLEFS");
  if(!erasePartitionProgress(fs, "LittleFS", 0, 100))
  {
    Serial.println("littlefs: erase FAILED");
    uiBootWarning("LITTLEFS",
                  "Erase FAILED", COL_WARN,
                  "Volume left as is", COL_WARN,
                  "Menu only, no boot");
    return false;
  }

  drawResetProgress("Done", 100, 100);
  delay(500);
  Serial.println("littlefs: erased, volume blank");
  return true;
}

// ---------------------------------------------------------------------------
// nvs upgrade guard (settings / rec_settings)
//
// Both labels are new in v4.0.0 and, on a board upgraded from v3.2.1, sit on
// top of the old, larger littlefs: their first 160 KB holds file data instead
// of NVS pages. Preferences then refuses to open and every write is dropped
// for good - no crash, just settings that never come back. The recovery owns
// rec_settings and the applications own settings, so both are checked here,
// before anything opens them. A partition is erased only when init itself
// refused, which is the one case where its contents cannot be read back
// anyway; a volume that opens stays exactly as it is.
// ---------------------------------------------------------------------------

static const char *nvsErrText(esp_err_t err)
{
  if(err == ESP_ERR_NVS_NO_FREE_PAGES) return "No free pages";
  if(err == ESP_ERR_NVS_NEW_VERSION_FOUND) return "New version found";
  return "Init failed";
}

// True when the label can be used, either as it stands or after the one
// button repair below.
static bool nvsUpgradeCheckLabel(const char *label, const char *title)
{
  const esp_partition_t *part = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, label);
  if(!part)
  {
    Serial.printf("nvs: %s partition missing\n", label);
    return false;
  }

  esp_err_t err = nvs_flash_init_partition(label);
  if(err == ESP_OK) return true;

  Serial.printf("nvs: %s init failed %d (%s)\n",
                label, (int)err, nvsErrText(err));

  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader(title);
  confirmOne(title, nvsErrText(err), "Erase it to continue", "Erase");

  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader(title);
  if(nvs_flash_erase_partition(label) != ESP_OK)
  {
    Serial.printf("nvs: %s erase FAILED\n", label);
    uiBootWarning(title, "Erase FAILED", COL_WARN,
                  "Left as it is", COL_WARN, "Not saved");
    return false;
  }

  err = nvs_flash_init_partition(label);
  if(err != ESP_OK)
  {
    Serial.printf("nvs: %s re-init failed %d\n", label, (int)err);
    uiBootWarning(title, "Init FAILED", COL_WARN,
                  "Left empty", COL_WARN, "Not saved");
    return false;
  }

  drawResetProgress("Done", 100, 100);
  delay(300);
  Serial.printf("nvs: %s repaired\n", label);
  return true;
}

static void nvsUpgradeCheck()
{
  nvsUpgradeCheckLabel(STORAGE_PARTITION, "REC SETTINGS");
  nvsUpgradeCheckLabel("settings", "SETTINGS");
  // settings belongs to the applications, so unmount it again rather than
  // hold the DRAM a second NVS instance needs on this screen.
  nvs_flash_deinit_partition("settings");
}

static void runEraseList(const EraseList &list)
{
  bool checked[3] = {false, false, false};
  int selected = 0;
  drawEraseList(list, checked, selected);

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      selected = (selected + d) % 3;
      if(selected < 0) selected += 3;
      drawEraseList(list, checked, selected);
    }

    uint32_t h = readButton();
    if(h == 0) { delay(10); continue; }

    if(h < 300)                                       // click = check toggle
    {
      checked[selected] = !checked[selected];
      drawEraseList(list, checked, selected);
      continue;
    }

    int totalUnits = 0;
    for(int i = 0; i < 3; i++)
      if(checked[i]) totalUnits++;

    if(totalUnits == 0) return;                       // hold + empty = back

    char line1[40];
    snprintf(line1, sizeof(line1), "Erase %d selected now?", totalUnits);
    if(!confirmDialog("ERASE", line1, "This cannot be undone"))
    {
      drawEraseList(list, checked, selected);
      continue;
    }

    // rec_settings is the only NVS this screen touches, so only take the NVS
    // layer down when it is one of the targets.
    bool nvsDown = list.factory && checked[2];
    if(nvsDown) nvs_flash_deinit();
    if(!list.factory && checked[2]) LittleFS.end();

    gSprite.fillScreen(COL_BG);
    drawHeader(list.title);

    int unit = 0;
    bool ok = true;
    for(int i = 0; i < 3 && ok; i++)
    {
      if(!checked[i]) continue;
      int start = unit * 100 / totalUnits;
      int end = (unit + 1) * 100 / totalUnits;

      if(list.factory)
      {
        uint32_t addr = DES_SETTINGS_ENV0;
        uint32_t len = DES_SETTINGS_SIZE;
        if(i == 1) addr = DES_SETTINGS_ENV1;
        else if(i == 2)
        {
          const esp_partition_t *rec = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
            STORAGE_PARTITION);
          if(!rec) { ok = false; break; }
          addr = rec->address;
          len = rec->size;
        }
        ok = eraseRegionProgress(addr, len, list.items[i], start, end);
      }
      else
      {
        const esp_partition_t *part = NULL;
        if(i == 0)
          part = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
            static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0), NULL);
        else if(i == 1)
          part = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
            static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1), NULL);
        else
          part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, NULL);
        ok = part && erasePartitionProgress(part, list.items[i], start, end);
      }

      unit++;
    }

    if(!ok)
    {
      gSprite.setTextColor(COL_WARN, COL_BG);
      gSprite.drawString("Erase failed", 10, 150, FONT_SMALL);
      gSprite.pushSprite(0, 0);
      delay(2500);
      if(nvsDown) ESP.restart();                      // NVS is down, re-init
      return;
    }

    drawResetProgress("Done", 100, 100);
    delay(1000);
    ESP.restart();
  }
}

static void runErase()
{
  static const char *modes[2] = {"Factory Reset", "Erase"};
  static const int modeIcons[2] = {ICON_TRASH, ICON_DB};
  static const uint16_t modeAccs[2] = {COL_WARN, COL_GOLD};

  while(true)
  {
    int mode = runTileChoice("ERASE", "TURN=MOVE", "CLICK=BACK  HOLD=OPEN",
                             modes, modeIcons, modeAccs, 2);
    if(mode < 0) return;
    runEraseList(kEraseLists[mode]);
  }
}

// ---------------------------------------------------------------------------
// WiFi Setting: scan (4/page) + encoder keypad + connect/save
// ---------------------------------------------------------------------------

enum KbType : uint8_t { KB_CHAR = 0, KB_MODE, KB_BSP, KB_SPACE, KB_CANCEL };

struct KbKey
{
  int16_t x, y, w, h;
  char ch;
  uint8_t type;
};

static const char *KB_SETS[4] = {
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "1234567890",
  "!@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~"
};
static const char *KB_MODE_NAMES[4] = { "abc", "ABC", "123", "#!@" };

static KbKey kbKeys[KB_MAX_KEYS];
static int kbKeyCount = 0;
static int kbCursor = 0;
static int kbMode = 0;

static void kbAddKey(int16_t x, int16_t y, int16_t w, int16_t h, char ch, uint8_t type)
{
  if(kbKeyCount >= KB_MAX_KEYS) return;
  kbKeys[kbKeyCount++] = {x, y, w, h, ch, type};
}

static void kbBuild(int mode, int contentBottom)
{
  kbMode = mode;
  kbKeyCount = 0;
  if(kbCursor >= KB_MAX_KEYS) kbCursor = 0;

  const int16_t kbTop = contentBottom + 4;
  const int16_t colW = 31;
  const int16_t rowH = 16;
  const int16_t gap = 1;
  const int16_t ox = 5;

  for(int m = 0; m < 4; m++)
    kbAddKey(ox + m * 79, kbTop, 77, 15, (char)m, KB_MODE);

  const char *set = KB_SETS[mode];
  int len = (int)strlen(set);
  int16_t ky = kbTop + 18;
  for(int i = 0; i < len; i++)
  {
    int row = i / 10;
    int col = i % 10;
    kbAddKey(ox + col * (colW + gap), ky + row * (rowH + gap), colW, rowH, set[i], KB_CHAR);
  }

  int rows = (len + 9) / 10;
  if(rows < 1) rows = 1;
  int16_t by = ky + rows * (rowH + gap) + 1;
  kbAddKey(ox, by, 50, rowH, 0, KB_BSP);
  kbAddKey(ox + 52, by, 50, rowH, 0, KB_CANCEL);
  kbAddKey(ox + 104, by, 210, rowH, ' ', KB_SPACE);

  if(kbCursor >= kbKeyCount) kbCursor = kbKeyCount - 1;
  if(kbCursor < 0) kbCursor = 0;
}

static void drawWifiSetting(int *nets, int netCount, int selected,
                            const String &ssid, const String &password,
                            bool kbOpen)
{
  gSprite.fillScreen(COL_BG);
  drawHeader("WIFI");

  int listBottom;
  if(kbOpen)
  {
    gSprite.setTextColor(COL_TEXT, COL_BG);
    gSprite.drawString("SSID: " + ssid, 10, 32, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("Password:", 10, 50, FONT_SMALL);
    gSprite.setTextColor(COL_TEXT, COL_BG);
    String shown = password;
    if(shown.length() > 24) shown = shown.substring(shown.length() - 24);
    gSprite.drawString(shown + "_", 90, 50, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("CLICK=TYPE  HOLD=CONNECT", 10, 68, FONT_SMALL);

    listBottom = 84;
    kbBuild(kbMode, listBottom);

    for(int i = 0; i < kbKeyCount; i++)
    {
      const KbKey &k = kbKeys[i];
      bool sel = (i == kbCursor);
      uint16_t bg = sel ? COL_KEYSEL : COL_KEY;
      uint16_t fg = sel ? COL_BG : COL_TEXT;
      gSprite.fillRoundRect(k.x, k.y, k.w, k.h, 3, bg);
      gSprite.setTextColor(fg, bg);
      gSprite.setTextDatum(MC_DATUM);
      int cx = k.x + k.w / 2;
      int cy = k.y + k.h / 2;
      if(k.type == KB_MODE)
        gSprite.drawString(KB_MODE_NAMES[(int)k.ch], cx, cy, FONT_SMALL);
      else if(k.type == KB_BSP)
        gSprite.drawString("BSP", cx, cy, FONT_SMALL);
      else if(k.type == KB_CANCEL)
        gSprite.drawString("X", cx, cy, FONT_SMALL);
      else if(k.type == KB_SPACE)
        gSprite.drawString("SPACE", cx, cy, FONT_SMALL);
      else
      {
        char s[2] = {k.ch, 0};
        gSprite.drawString(s, cx, cy, FONT_SMALL);
      }
    }
    gSprite.setTextDatum(TL_DATUM);
  }
  else
  {
    gSprite.setTextColor(COL_MUTED, COL_BG);
    if(netCount == 0)
      gSprite.drawString("No networks found", 8, 36, FONT_SMALL);
    else
      gSprite.drawString("Hold = connect / open keyboard", 8, 36, FONT_SMALL);

    int page = selected / WIFI_PAGE_SIZE;
    int start = page * WIFI_PAGE_SIZE;
    int end = start + WIFI_PAGE_SIZE;
    if(end > netCount) end = netCount;

    for(int i = start; i < end; i++)
      uiRow(8, 48 + (i - start) * 25, 304, 23, WiFi.SSID(i).c_str(),
            i == selected, COL_ACC);

    int pages = (netCount + WIFI_PAGE_SIZE - 1) / WIFI_PAGE_SIZE;
    if(pages < 1) pages = 1;
    char pageBuf[20];
    snprintf(pageBuf, sizeof(pageBuf), "Page %d/%d", page + 1, pages);
    uiHintBar(pageBuf, "CLICK=BACK  HOLD=CONNECT");
    listBottom = 150;
  }

  (void)listBottom;
  (void)nets;
  gSprite.pushSprite(0, 0);
}

static int kbHandleEncoder(int8_t d)
{
  if(kbKeyCount == 0 || d == 0) return 0;
  kbCursor = ((kbCursor + d) % kbKeyCount + kbKeyCount) % kbKeyCount;
  return 1;
}

static void runWifiSetting()
{
  gSprite.fillScreen(COL_BG);
  drawHeader("WIFI");
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("Scanning...", 10, 50, FONT_SMALL);
  drawNetIcon();
  gSprite.pushSprite(0, 0);

  WiFi.mode(WIFI_STA);
  applyTxPower();
  int netCount = WiFi.scanNetworks();
  if(netCount < 0) netCount = 0;
  if(netCount > MAX_SCAN_NETWORKS) netCount = MAX_SCAN_NETWORKS;

  int selected = 0;
  bool kbOpen = false;
  String activeSsid;
  String password = "";
  int dummyNets[MAX_SCAN_NETWORKS];

  if(netCount == 0) selected = 0;

  auto redraw = [&]() {
    drawWifiSetting(dummyNets, netCount, selected, activeSsid, password, kbOpen);
  };

  redraw();

  while(true)
  {
    if(!kbOpen)
    {
      int8_t d = readEncoder();
      if(netCount > 0 && d)
      {
        selected = (selected + d) % netCount;
        if(selected < 0) selected += netCount;
        redraw();
      }

      uint32_t h = readButton();
      if(h >= 300)
      {
        if(netCount == 0) return;
        activeSsid = WiFi.SSID(selected);
        wifi_auth_mode_t auth = WiFi.encryptionType(selected);
        if(auth == WIFI_AUTH_OPEN)
        {
          gSprite.fillScreen(COL_BG);
          drawHeader("WIFI");
          gSprite.setTextColor(COL_TEXT, COL_BG);
          gSprite.drawString("Connecting: " + activeSsid, 10, 50, FONT_SMALL);
          bool ok = wifiConnectBlocking(activeSsid, "", WIFI_CONNECT_TIMEOUT);
          if(ok)
          {
            saveWifiCredentials(activeSsid, "");
            gSprite.setTextColor(COL_OK, COL_BG);
            gSprite.drawString("Connected!", 10, 80, FONT_SMALL);
             gSprite.pushSprite(0, 0);
            delay(1200);
            return;
          }
          gSprite.setTextColor(COL_WARN, COL_BG);
          gSprite.drawString("Failed", 10, 80, FONT_SMALL);
           gSprite.pushSprite(0, 0);
          delay(1500);
          redraw();
        }
        else
        {
          password = "";
          kbOpen = true;
          kbMode = 0;
          kbCursor = 0;
          redraw();
        }
      }
      else if(h > 0)
      {
        WiFi.scanDelete();
        return;
      }
      delay(10);
    }
    else
    {
      int8_t d = readEncoder(false);
      if(kbHandleEncoder(d)) redraw();

      uint32_t h = readButton();
      if(h >= 300)
      {
        gSprite.fillScreen(COL_BG);
        drawHeader("WIFI");
        gSprite.setTextColor(COL_TEXT, COL_BG);
        gSprite.drawString("Connecting: " + activeSsid, 10, 50, FONT_SMALL);
        bool ok = wifiConnectBlocking(activeSsid, password, WIFI_CONNECT_TIMEOUT);
        if(ok)
        {
          saveWifiCredentials(activeSsid, password);
          gSprite.setTextColor(COL_OK, COL_BG);
          gSprite.drawString("Connected!", 10, 80, FONT_SMALL);
           gSprite.pushSprite(0, 0);
          delay(1200);
          WiFi.scanDelete();
          return;
        }
        gSprite.setTextColor(COL_WARN, COL_BG);
        gSprite.drawString("Failed - check password", 10, 80, FONT_SMALL);
         gSprite.pushSprite(0, 0);
        delay(1500);
        redraw();
      }
      else if(h > 0)
      {
        const KbKey &k = kbKeys[kbCursor];
        if(k.type == KB_CHAR)
        {
          if(password.length() < 63) password += k.ch;
          redraw();
        }
        else if(k.type == KB_MODE)
        {
          kbMode = (int)k.ch;
          kbCursor = 4;
          redraw();
        }
        else if(k.type == KB_BSP)
        {
          if(password.length() > 0) password.remove(password.length() - 1);
          redraw();
        }
        else if(k.type == KB_SPACE)
        {
          if(password.length() < 63) password += ' ';
          redraw();
        }
        else if(k.type == KB_CANCEL)
        {
          kbOpen = false;
          redraw();
        }
      }
      delay(10);
    }
  }
}

// ---------------------------------------------------------------------------
// Background web server (always running unless disabled)
// ---------------------------------------------------------------------------

static String htmlEsc(const String &s)
{
  String o = s;
  o.replace("&", "&amp;");
  o.replace("<", "&lt;");
  o.replace(">", "&gt;");
  o.replace("\"", "&quot;");
  o.replace("'", "&#39;");
  return o;
}

static String fmtBytes(uint32_t b)
{
  char buf[24];
  if(b >= 1024UL * 1024)    snprintf(buf, sizeof(buf), "%.1f MB", b / 1048576.0);
  else if(b >= 1024UL * 10) snprintf(buf, sizeof(buf), "%.0f KB", b / 1024.0);
  else if(b >= 1024UL)      snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  else                      snprintf(buf, sizeof(buf), "%u B", (unsigned)b);
  return String(buf);
}

static uint32_t appImageSize(const esp_partition_t *p)
{
  if(p->type != ESP_PARTITION_TYPE_APP) return 0;

  uint8_t hdr[24];
  if(esp_partition_read(p, 0, hdr, sizeof(hdr)) != ESP_OK) return 0;
  if(hdr[0] != 0xE9) return 0;

  uint8_t segs = hdr[1];
  if(segs == 0 || segs > 16) return 0;
  bool hashAppended = (hdr[23] != 0);

  uint32_t off = 24;
  uint32_t total = 24;
  for(uint8_t i = 0; i < segs; i++)
  {
    uint8_t sh[8];
    if(esp_partition_read(p, off, sh, sizeof(sh)) != ESP_OK) return 0;
    uint32_t len = (uint32_t)sh[4] | ((uint32_t)sh[5] << 8) |
                   ((uint32_t)sh[6] << 16) | ((uint32_t)sh[7] << 24);
    if(len == 0 || off + 8 + len > p->size) return 0;
    off += 8 + len;
    total += 8 + len;
  }
  if(hashAppended) total += 32;
  return (total <= p->size) ? total : 0;
}

static String buildPartitionRows()
{
  String rows;
  esp_partition_iterator_t it =
    esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

  while(it)
  {
    const esp_partition_t *p = esp_partition_get(it);

    uint32_t size = p->size;
    uint32_t used = 0;
    bool known = false;

    if(p->type == ESP_PARTITION_TYPE_APP)
    {
      used = appImageSize(p);
      known = true;
    }
    else if(strcmp(p->label, "littlefs") == 0)
    {
      size = (uint32_t)LittleFS.totalBytes();
      used = (uint32_t)LittleFS.usedBytes();
      known = true;
    }

    rows += "<div class='row'><span><span class='mono'>";
    rows += p->label;
    rows += "</span><span class='muted sm' style='display:block'>0x";
    {
      char a[16];
      snprintf(a, sizeof(a), "%06X", (unsigned)p->address);
      rows += a;
    }
    rows += " &middot; ";
    rows += fmtBytes(p->size);
    rows += "</span></span>";

    if(known)
    {
      uint32_t freeb = (size > used) ? (size - used) : 0;
      int pct = size ? (int)((uint64_t)used * 100 / size) : 0;
      rows += "<span class='mono sm' style='text-align:right'>" + fmtBytes(used) +
              " used<span class='muted' style='display:block'>" + fmtBytes(freeb) +
              " free &middot; " + String(pct) + "%</span></span>";
    }
    else
    {
      rows += "<span class='mono sm muted' style='text-align:right'>used &mdash;</span>";
    }

    rows += "</div>";
    it = esp_partition_next(it);
  }
  return rows;
}

static String buildFileRows()
{
  String names[32];
  int n = 0;
  File root = LittleFS.open("/", "r");
  if(root && root.isDirectory())
  {
    File f = root.openNextFile();
    while(f && n < 32)
    {
      if(!f.isDirectory()) names[n++] = String(f.name());
      f = root.openNextFile();
    }
    root.close();
  }

  for(int i = 1; i < n; i++)
  {
    String k = names[i];
    int j = i - 1;
    while(j >= 0 && names[j] > k) { names[j + 1] = names[j]; j--; }
    names[j + 1] = k;
  }

  if(n == 0) return "<div class='row'><span class='muted'>(no files)</span></div>";

  String rows;
  for(int i = 0; i < n; i++)
  {
    File f = LittleFS.open("/" + names[i], "r");
    uint32_t sz = f ? f.size() : 0;
    if(f) f.close();

    String en = htmlEsc(names[i]);
    rows += "<div class='row'><div class='fname'>" + en +
            "<div class='muted sm'>" + fmtBytes(sz) + "</div></div><div class='acts'>";
    rows += "<button class='btn sm get' data-act='get' data-name='" + en + "'>Get</button>";
    if(names[i].endsWith(".bin"))
    {
      rows += "<button class='btn sm' data-act='flash' data-slot='0' data-name='" + en + "'>App0</button>";
      rows += "<button class='btn sm' data-act='flash' data-slot='1' data-name='" + en + "'>App1</button>";
    }
    rows += "<button class='btn sm del' data-act='del' data-name='" + en + "'>Del</button>";
    rows += "</div></div>";
  }
  return rows;
}

static void handleRoot()
{
  uint32_t fsUsed  = LittleFS.usedBytes();
  uint32_t fsTotal = LittleFS.totalBytes();
  int fsPct = fsTotal ? (int)((uint64_t)fsUsed * 100 / fsTotal) : 0;

  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Boot Manager</title><style>"
    "body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#0e1116;color:#e6edf3}"
    ".wrap{max-width:640px;margin:0 auto;padding:16px}"
    "h1{font-size:18px;margin:4px 0 14px;display:flex;align-items:center;gap:8px}"
    ".dot{width:9px;height:9px;border-radius:50%;background:#3fb950;display:inline-block}"
    ".card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px;margin:12px 0}"
    ".card h2{font-size:12px;margin:0 0 10px;color:#8b949e;text-transform:uppercase;letter-spacing:.6px}"
    ".grid{display:grid;grid-template-columns:auto 1fr;gap:6px 16px;font-size:14px}"
    ".grid .k{color:#8b949e}"
    ".mono{font-family:ui-monospace,Consolas,monospace}"
    ".muted{color:#8b949e}.sm{font-size:12px}"
    ".btn{display:inline-block;padding:8px 14px;border:0;border-radius:8px;background:#1f6feb;color:#fff;"
    "font-size:14px;cursor:pointer;text-decoration:none}"
    ".btn:hover{background:#388bfd}.btn.del{background:#da3633}.btn.get{background:#238636}"
    ".btn.sm{padding:4px 10px;font-size:12px;border-radius:6px}"
    ".row{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:7px 0;border-bottom:1px solid #21262d}"
    ".row:last-child{border-bottom:0}.acts{display:flex;gap:6px}"
    ".fname{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    "select,input[type=file],input[type=text],input[type=password]{background:#0d1117;color:#e6edf3;"
    "border:1px solid #30363d;border-radius:8px;"
    "padding:8px;width:100%;box-sizing:border-box;margin:6px 0}"
    ".net{display:flex;justify-content:space-between;gap:10px;padding:8px 10px;border:1px solid #30363d;"
    "border-radius:8px;margin:5px 0;cursor:pointer;font-size:14px}"
    ".net:hover{border-color:#388bfd;background:#111722}"
    "#wlist{max-height:170px;overflow:auto}"
    ".btn.ghost{background:#30363d}.btn.ghost:hover{background:#484f58}"
    ".modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.62);z-index:20;"
    "align-items:center;justify-content:center;padding:20px}"
    ".modalBox{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:18px;"
    "max-width:360px;width:100%;box-shadow:0 10px 34px rgba(0,0,0,.55)}"
    "#mtitle{font-size:15px;font-weight:600;margin-bottom:6px}"
    "#mtext{font-size:14px;margin-bottom:16px;word-break:break-word}"
    ".pbar{width:100%;height:8px;background:#21262d;border-radius:6px;overflow:hidden;display:none;margin-top:8px}"
    ".pfill{width:0%;height:100%;background:#1f6feb;transition:width .2s}"
    "</style></head><body><div class='wrap'>"
    "<h1><span class='dot'></span>Boot Manager</h1>");

  html += F("<div class='card'><h2>Device</h2><div class='grid'>");
  html += "<div class='k'>Recovery</div><div>v" RECOVERY_VERSION "</div>";
  html += "<div class='k'>Chip</div><div>" + String(ESP.getChipModel()) +
          " rev " + String((int)ESP.getChipRevision()) + "</div>";
  html += "<div class='k'>Flash</div><div>" + fmtBytes(ESP.getFlashChipSize()) + "</div>";
  html += "<div class='k'>MAC</div><div class='mono'>" + WiFi.macAddress() + "</div>";
  html += "<div class='k'>Boot slot</div><div>" + String(gBootSlot) + "</div>";
  html += "<div class='k'>Free heap</div><div>" + fmtBytes(ESP.getFreeHeap()) + "</div>";
  bool sta = (WiFi.status() == WL_CONNECTED);
  bool ap  = apModeActive;
  String ssid = sta ? WiFi.SSID() : (ap ? "ats-recovery" : "");
  html += "<div class='k'>SSID</div><div>" + htmlEsc(ssid.length() ? ssid : "-") + "</div>";
  html += "<div class='k'>IP</div><div class='mono'>" + htmlEsc(currentIp()) + "</div>";
  html += "<div class='k'>Mode</div><div>" +
          String(sta ? (ap ? "AP+STA" : "Station") : (ap ? "AP" : "Offline")) + "</div>";
  html += "<div class='k'>AP IP</div><div class='mono'>" +
          htmlEsc(ap ? WiFi.softAPIP().toString() : String("-")) + "</div>";
  html += "<div class='k'>Storage</div><div>" + fmtBytes(fsUsed) + " / " + fmtBytes(fsTotal) +
          " (" + String(fsPct) + "%)</div>";
  html += F("</div></div>");

  html += F("<div class='card'><h2>WiFi</h2>"
    "<input type='text' id='wssid' placeholder='SSID'>"
    "<input type='password' id='wpass' placeholder='Password'>"
    "<div class='acts'>"
    "<button class='btn' data-act='scan'>Scan</button>"
    "<button class='btn' data-act='connect'>Connect</button>"
    "</div>"
    "<div id='wlist'></div>"
    "<div id='wmsg' class='muted sm'></div></div>");

  html += F("<div class='card'><h2>Web server</h2><div class='grid'>");
  html += "<div class='k'>Status</div><div>" + String(gWebEnabled ? "ON" : "OFF") + "</div>";
  html += F("</div><div class='acts' style='margin-top:10px'>");
  if(gWebEnabled)
    html += F("<button class='btn del' data-act='web' data-href='/web?on=0'>Turn off</button>");
  else
    html += F("<button class='btn' data-act='web' data-href='/web?on=1'>Turn on</button>");
  html += F("</div></div>");

  html += F("<div class='card'><h2>Actions</h2><div class='acts'>"
    "<button class='btn' data-act='boot' data-href='/boot?slot=0'>Boot App0</button>"
    "<button class='btn' data-act='boot' data-href='/boot?slot=1'>Boot App1</button>"
    "</div></div>");

  html += F("<div class='card'><h2>Upload</h2>"
    "<input type='file' id='fi' accept='.bin,.txt'>"
    "<select id='tg'>"
    "<option value='store'>Save to storage</option>"
    "<option value='0'>Upload &amp; flash to App0</option>"
    "<option value='1'>Upload &amp; flash to App1</option>"
    "</select>"
    "<button class='btn' id='ubtn' data-act='upload'>Upload</button>"
    "<div class='pbar' id='ubar'><div class='pfill' id='ufill'></div></div>"
    "<div id='ptxt' class='muted sm'></div></div>");

  html += F("<div class='card' id='fcard' style='display:none'><h2>Flash</h2>"
    "<div class='muted sm' id='fname'></div>"
    "<div class='pbar' id='fbar'><div class='pfill' id='ffill'></div></div>"
    "<div id='ftxt' class='muted sm'></div></div>");

  html += F("<div class='card'><h2>Files</h2>");
  html += buildFileRows();
  html += F("</div>");

  html += F("<div class='card'><h2>Partitions</h2>");
  html += buildPartitionRows();
  html += F("</div>");

  html += F("<div class='modal' id='mbox'><div class='modalBox'>"
    "<div id='mtitle'></div><div id='mtext' class='muted'></div>"
    "<div class='acts'><button class='btn' id='myes'>Yes</button>"
    "<button class='btn ghost' id='mno'>No</button></div>"
    "</div></div>");

  html += F("<script>"
    "var busy=false;"
    "function $(id){return document.getElementById(id);}"

    // ---- upload ----
    "function go(){if(busy)return;var f=$('fi').files[0];if(!f)return;"
    "var tg=$('tg').value;busy=true;$('ubtn').disabled=true;"
    "var fd=new FormData();fd.append('file',f);var x=new XMLHttpRequest();"
    "$('ubar').style.display='block';"
    "x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);"
    "$('ufill').style.width=p+'%';$('ptxt').textContent='Uploading '+p+'%';}};"
    "x.onload=function(){busy=false;$('ubtn').disabled=false;"
    "if(x.status>=400){$('ptxt').textContent='Rejected - device busy';return;}"
    "if(tg!=='store'){$('ptxt').textContent='';startFlash(tg,f.name);}"
    "else{$('ptxt').textContent='Done!';setTimeout(function(){location.reload();},800);}};"
    "x.onerror=function(){$('ptxt').textContent='Failed.';"
    "busy=false;$('ubtn').disabled=false;};"
    "x.open('POST','/upload');x.send(fd);}"

    // ---- flash progress ----
    "function startFlash(slot,name){"
    "$('fcard').style.display='block';"
    "$('fname').textContent=name+' -> App'+slot+' (reboots into it)';"
    "$('fbar').style.display='block';$('ffill').style.width='0%';"
    "$('ftxt').textContent='Starting...';"
    "fetch('/flash?slot='+slot+'&name='+encodeURIComponent(name))"
    ".then(function(r){if(!r.ok)throw new Error('busy');return r.json();}).then(function(){"
    "var last=Date.now();"
    "var iv=setInterval(function(){"
    "if(Date.now()-last>30000){clearInterval(iv);"
    "$('ftxt').textContent='Lost contact with the device';return;}"
    "fetch('/flashstatus').then(function(r){return r.json();}).then(function(s){"
    "last=Date.now();"
    "if(s.pct>=0&&s.pct<=100){$('ffill').style.width=s.pct+'%';"
    "$('ftxt').textContent=s.msg+' '+s.pct+'%';}"
    "else if(s.pct===101){clearInterval(iv);$('ffill').style.width='100%';"
    "$('ftxt').textContent='Done! Booting App'+slot+'...';}"
    "else if(s.pct===102){clearInterval(iv);$('ftxt').textContent='Failed: '+s.msg;}"
    "else if(!s.busy){clearInterval(iv);"
    "$('ftxt').textContent='Flash is not running - device may have restarted';}"
    "}).catch(function(){});},400);})"
    ".catch(function(){$('ftxt').textContent='Flash request failed - device busy?';});"
    "return false;}"

    // ---- wifi ----
    "function scan(){$('wmsg').textContent='Scanning...';"
    "fetch('/scan').then(function(r){return r.json();}).then(function(a){"
    "var l=$('wlist');l.innerHTML='';"
    "if(!a.length){$('wmsg').textContent='No networks found';return;}"
    "a.forEach(function(n){var d=document.createElement('div');d.className='net';"
    "var s=document.createElement('span');s.textContent=n.ssid;"
    "var m=document.createElement('span');m.className='muted sm';"
    "m.textContent=n.rssi+' dBm'+(n.secure?' - sec':' - open');"
    "d.appendChild(s);d.appendChild(m);"
    "d.onclick=function(){$('wssid').value=n.ssid;$('wpass').focus();};"
    "l.appendChild(d);});"
    "$('wmsg').textContent=a.length+' networks - tap one to fill SSID';"
    "}).catch(function(){$('wmsg').textContent='Scan failed';});}"
    "function connect(){var s=$('wssid').value;"
    "if(!s){$('wmsg').textContent='Enter an SSID first';return;}"
    "var p=$('wpass').value;"
    "$('wmsg').textContent='Connecting to '+s+' ...';"
    "fetch('/connect?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))"
    ".then(function(r){return r.json();}).then(function(){"
    "var t=Date.now();"
    "var iv=setInterval(function(){"
    "fetch('/status').then(function(r){return r.json();}).then(function(st){"
    "if(st.connected){clearInterval(iv);"
    "$('wmsg').textContent='Connected: '+st.ssid+' @ '+st.ip;"
    "setTimeout(function(){location.reload();},2500);}"
    "else if(Date.now()-t>20000){clearInterval(iv);"
    "$('wmsg').textContent='Failed or timeout - check the password';}"
    "}).catch(function(){});},1000);})"
    ".catch(function(){$('wmsg').textContent='Connect request failed';});}"

    // ---- confirmation modal: every action asks Yes/No first ----
    "var CONFIRM={"
    "scan:['Scan','Search for nearby WiFi networks?'],"
    "connect:['Connect','Switch network? The page will reload.'],"
    "upload:['Upload','Send this file to the device?'],"
    "flash:['Flash','Overwrite the slot, then reboot into it?'],"
    "boot:['Boot','Reboot into this app?'],"
    "web:['Web server','Change the web server state?'],"
    "del:['Delete','Delete this file permanently?'],"
    "get:['Download','Download this file?']};"
    "var mtarget=null;"
    "function askText(el,def){var s=el.getAttribute('data-name');"
    "var slot=el.getAttribute('data-slot');var p=[];"
    "if(slot!==null)p.push('App'+slot);if(s)p.push(s);"
    "return p.length?p.join(' / ')+': '+def:def;}"
    "function askClose(){$('mbox').style.display='none';mtarget=null;}"
    "function runAction(el){var a=el.getAttribute('data-act');"
    "if(a==='scan')scan();"
    "else if(a==='connect')connect();"
    "else if(a==='upload')go();"
    "else if(a==='flash')startFlash(el.getAttribute('data-slot'),el.getAttribute('data-name'));"
    "else if(a==='get')location.href='/download?name='+encodeURIComponent(el.getAttribute('data-name'));"
    "else if(a==='del')location.href='/delete?name='+encodeURIComponent(el.getAttribute('data-name'));"
    "else location.href=el.getAttribute('data-href');}"
    "document.addEventListener('click',function(e){"
    "var el=e.target.closest?e.target.closest('[data-act]'):null;"
    "if(!el)return;"
    "e.preventDefault();"
    "var c=CONFIRM[el.getAttribute('data-act')];"
    "if(!c)return;"
    "mtarget=el;$('mtitle').textContent=c[0];"
    "$('mtext').textContent=askText(el,c[1]);"
    "$('mbox').style.display='flex';});"
    "$('myes').onclick=function(){var el=mtarget;askClose();if(el)runAction(el);};"
    "$('mno').onclick=askClose;"
    "$('mbox').onclick=function(e){if(e.target===this)askClose();};"
    "document.addEventListener('keydown',"
    "function(e){if(e.key==='Escape')askClose();});"
    "</script>");

  html += F("</div></body></html>");
  server.send(200, "text/html", html);
}

static File uploadFile;
static bool uploadRejected = false;

static void handleUploadDone()
{
  gUploadLastMs = 0;
  if(uploadRejected)
  {
    uploadRejected = false;
    server.send(409, "text/plain", "device is busy flashing");
    return;
  }
  server.send(200, "text/plain", "OK");
}

static void handleUpload()
{
  HTTPUpload& up = server.upload();
  if(up.status == UPLOAD_FILE_START)
  {
    uploadRejected = gFlashBusy;
    gUploadLastMs = millis();
    if(uploadRejected) uploadFile = File();
    else uploadFile = LittleFS.open("/" + String(up.filename.c_str()), FILE_WRITE);
  }
  else if(up.status == UPLOAD_FILE_WRITE)
  {
    gUploadLastMs = millis();
    if(uploadFile) uploadFile.write(up.buf, up.currentSize);
  }
  else if(up.status == UPLOAD_FILE_END)
  {
    if(uploadFile) uploadFile.close();
    gUiRefresh = true;
  }
}

// Shared answer when a request arrives while an upload/flash owns the device.
static void busyPage()
{
  server.send(409, "text/html",
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<body style='font-family:sans-serif;background:#0e1116;color:#e6edf3;text-align:center;padding:48px'>"
    "<h2>Device is busy</h2>"
    "<p style='color:#8b949e'>Wait for the upload or flash to finish, then try again.</p>"
    "<a href='/' style='color:#58a6ff'>Back</a></body>");
}

static void handleDelete()
{
  if(gFlashBusy || uploadBusy()) { busyPage(); return; }

  if(server.hasArg("name"))
  {
    String name = server.arg("name");
    if(!name.startsWith("/")) name = "/" + name;
    LittleFS.remove(name);
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

static void handleBoot()
{
  if(gFlashBusy || uploadBusy()) { busyPage(); return; }

  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : 0;
  server.send(200, "text/html",
    "<meta charset='utf-8'><body style='font-family:sans-serif;background:#0e1116;color:#e6edf3;"
    "text-align:center;padding:48px'><h2>Booting...</h2></body>");
  delay(600);
  bootToApp(static_cast<esp_partition_subtype_t>(
    slot == 1 ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0));
}

// Runs the flash off the web task so / keeps answering while it progresses.
struct FlashJob
{
  String name;
  int slot;
};

static void flashTask(void *arg)
{
  FlashJob *job = (FlashJob *)arg;
  String name = job->name;
  int slot = job->slot;
  delete job;

  flashSetProgress(0, "Starting...");
  gUiQuiet = true;
  bool ok = flashFirmware(name, slot);
  gUiQuiet = false;

  int pct;
  char msg[sizeof(gFlashMsg)];
  flashGetProgress(&pct, msg, sizeof(msg));

  if(ok && !setBootSlot(slot))
  {
    flashSetProgress(102, "Could not select boot slot");
    ok = false;
  }
  else if(ok) flashSetProgress(101, "Booting...");
  else if(pct != 102) flashSetProgress(102, "Flash failed");

  gUiRefresh = true;
  gWebFlash = false;

  if(ok)
  {
     gSprite.pushSprite(0, 0);
    delay(2500);   // let the browser read "Booting..." before the socket drops
    ESP.restart();
  }

  gFlashBusy = false;
  vTaskDelete(NULL);
}

static void handleFlash()
{
  if(!server.hasArg("name") || !server.hasArg("slot"))
  {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing name/slot\"}");
    return;
  }
  if(gFlashBusy || uploadBusy())
  {
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"busy\"}");
    return;
  }

  FlashJob *job = new FlashJob;
  if(!job)
  {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"oom\"}");
    return;
  }
  job->name = server.arg("name");
  job->slot = server.arg("slot").toInt();

  gFlashBusy = true;
  gWebFlash = true;
  flashSetProgress(0, "Starting...");

  if(xTaskCreate(flashTask, "flashui", 8192, job, 1, nullptr) != pdPASS)
  {
    delete job;
    gFlashBusy = false;
    gWebFlash = false;
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"task\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

static void setWebEnabled(bool on)
{
  if(on == gWebEnabled) return;
  gWebEnabled = on;
  saveWebEnabled(gWebEnabled);
  gUiRefresh = true;
}

static void drawWifiAbout()
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("NETWORK ABOUT");
  uiPanel(8, 34, 304, 110, COL_ACC);

  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_PANEL);

  bool sta = (WiFi.status() == WL_CONNECTED);
  bool ap  = apModeActive;
  int y = 42;

  if(sta)
  {
    gSprite.drawString("SSID: " + WiFi.SSID(), 18, y, FONT_SMALL); y += 17;
  }
  else if(ap)
  {
    gSprite.drawString("AP:   ats-recovery", 18, y, FONT_SMALL); y += 17;
  }

  if(ap)
  {
    gSprite.drawString("PASS: 12345678", 18, y, FONT_SMALL); y += 17;
  }

  const char *mode = sta ? (ap ? "AP+STA" : "Station") : (ap ? "AP" : "Offline");
  gSprite.drawString("Mode: " + String(mode), 18, y, FONT_SMALL); y += 17;

  String ip = currentIp();
  gSprite.drawString(ip == "-" ? String("IP:   -") : String("IP:   http://" + ip),
                 18, y, FONT_SMALL);
  y += 17;

  gSprite.setTextColor(gWebEnabled ? COL_OK : COL_WARN, COL_PANEL);
  gSprite.drawString(gWebEnabled ? "WEB:  ON" : "WEB:  OFF", 18, y, FONT_SMALL);

  uiHintBar("Rotate=Web on/off", "CLICK=BACK");
  gSprite.pushSprite(0, 0);
}

static void runWifiAbout()
{
  gUiRefresh = false;
  drawWifiAbout();

  while(true)
  {
    if(gUiRefresh) { gUiRefresh = false; drawWifiAbout(); }

    int8_t dir = readEncoder();
    if(dir)
    {
      setWebEnabled(dir > 0);
      drawWifiAbout();
    }

    uint32_t held = readButton();
    if(held == 0) { delay(10); continue; }
    if(held < 300) return;
    delay(10);
  }
}

static void handleDownload()
{
  if(!server.hasArg("name"))
  {
    server.send(400, "text/plain", "missing name");
    return;
  }

  String name = server.arg("name");
  if(!name.startsWith("/")) name = "/" + name;
  if(!LittleFS.exists(name))
  {
    server.send(404, "text/plain", "not found");
    return;
  }

  File f = LittleFS.open(name, "r");
  if(!f)
  {
    server.send(500, "text/plain", "open failed");
    return;
  }

  String fn = name.substring(1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fn + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handleWebToggle()
{
  if(gFlashBusy || uploadBusy()) { busyPage(); return; }

  bool on = server.hasArg("on") ? (server.arg("on") == "1") : true;
  if(on != gWebEnabled)
  {
    gWebEnabled = on;
    saveWebEnabled(gWebEnabled);
  }

  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'><style>"
    "body{font-family:sans-serif;background:#0e1116;color:#e6edf3;text-align:center;padding:48px}</style>"
    "</head><body><h2>Web server: ");
  page += gWebEnabled ? F("ON") : F("OFF");
  page += F("</h2>");
  if(gWebEnabled)
    page += F("<a href='/'><button style='padding:10px 18px'>Back</button></a>");
  else
    page += F("<p>Turn it back on from WiFi &gt; WiFi About on the LCD.</p>");
  page += F("</body></html>");
  server.send(200, "text/html", page);
  gUiRefresh = true;
}

static String jsonEsc(const String &s)
{
  String o;
  o.reserve(s.length() + 8);
  for(size_t i = 0; i < s.length(); i++)
  {
    char c = s.charAt(i);
    if(c == '"' || c == '\\') { o += '\\'; o += c; }
    else if((uint8_t)c < 0x20)
    {
      char b[8];
      snprintf(b, sizeof(b), "\\u%04x", (unsigned)(uint8_t)c);
      o += b;
    }
    else o += c;
  }
  return o;
}

// Enables the STA interface for a scan/connect while keeping the access point
// alive, so the browser session (and the LCD) do not lose the device.
static void ensureApStaMode()
{
  WiFi.mode(apModeActive ? WIFI_AP_STA : WIFI_STA);
}

static void handleScan()
{
  ensureApStaMode();

  int n = WiFi.scanNetworks();
  String json = "[";
  int added = 0;
  for(int i = 0; i < n; i++)
  {
    String s = WiFi.SSID(i);
    if(!s.length()) continue;
    if(added++) json += ",";
    json += "{\"ssid\":\"" + jsonEsc(s) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
            ",\"secure\":" +
            String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]";
  WiFi.scanDelete();

  server.send(200, "application/json", json);
}

static void handleConnect()
{
  if(!server.hasArg("ssid") || !server.arg("ssid").length())
  {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";

  saveWifiCredentials(ssid, pass);

  // answer first: WiFi.begin() may drop the link we are answering on
  server.send(200, "application/json",
              "{\"ok\":true,\"ssid\":\"" + jsonEsc(ssid) + "\"}");

  ensureApStaMode();
  applyTxPower();
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  gUiRefresh = true;
}

static void handleStatus()
{
  bool sta = (WiFi.status() == WL_CONNECTED);
  String j = "{\"connected\":";
  j += sta ? "true" : "false";
  j += ",\"ssid\":\"" + jsonEsc(sta ? WiFi.SSID() : String()) + "\"";
  j += ",\"ip\":\"" + (sta ? WiFi.localIP().toString() : String()) + "\"";
  j += ",\"ap\":";
  j += apModeActive ? "true" : "false";
  j += ",\"apip\":\"" + (apModeActive ? WiFi.softAPIP().toString() : String()) + "\"";
  j += ",\"web\":";
  j += gWebEnabled ? "true" : "false";
  j += "}";
  server.send(200, "application/json", j);
}

static void handleFlashStatus()
{
  int pct = -1;
  char msg[sizeof(gFlashMsg)] = {0};
  flashGetProgress(&pct, msg, sizeof(msg));

  String j = "{\"busy\":";
  j += gFlashBusy ? "true" : "false";
  j += ",\"pct\":" + String(pct);
  j += ",\"msg\":\"" + jsonEsc(String(msg)) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

static void webServerTask(void *)
{
  bool applied = false;

  for(;;)
  {
    if(gWebEnabled != applied)
    {
      if(!gWebEnabled)
      {
        vTaskDelay(pdMS_TO_TICKS(400));
        if(!gWebEnabled)
        {
          if(serverRunning) { server.stop(); serverRunning = false; }
          if(apModeActive)
          {
            WiFi.softAPdisconnect(true);
            apModeActive = false;
          }
        }
      }
      else
      {
        if(WiFi.status() != WL_CONNECTED && !apModeActive) wifiStartAp();
        if(!serverRunning) { server.begin(); serverRunning = true; }
      }
      applied = gWebEnabled;
      gUiRefresh = true;
      continue;
    }

    if(gWebEnabled && serverRunning) server.handleClient();
    vTaskDelay(1);
  }
}

static void startWebBackground()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/boot", HTTP_GET, handleBoot);
  server.on("/flash", HTTP_GET, handleFlash);
  server.on("/flashstatus", HTTP_GET, handleFlashStatus);
  server.on("/web", HTTP_GET, handleWebToggle);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_GET, handleConnect);
  server.on("/status", HTTP_GET, handleStatus);

  if(xTaskCreate(webServerTask, "webui", 6144, nullptr, 1, &webTaskHandle) != pdPASS)
    Serial.println("step: web task create failed");
}

// ---------------------------------------------------------------------------
// About (5 pages)
// ---------------------------------------------------------------------------

#define ABOUT_PAGES 5

static void displayQRCode(esp_qrcode_handle_t qrcode)
{
  int size = esp_qrcode_get_size(qrcode);
  const int scale = 3;
  const int ox = 10, oy = 34;
  int qr = size * scale;

  gSprite.fillRect(ox - 4, oy - 4, qr + 8, qr + 8, COL_TEXT);
  for(int y = 0; y < size; y++)
    for(int x = 0; x < size; x++)
      if(esp_qrcode_get_module(qrcode, x, y))
        gSprite.fillRect(ox + x * scale, oy + y * scale, scale, scale, COL_BG);
}

static void drawAboutFooter(int page)
{
  (void)page;
  uiHintBar("Rotate=Page", "CLICK=BACK");
}

static void drawAboutPage(int page)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("Boot Manager", "v" RECOVERY_VERSION);

  switch(page)
  {
    case 0:
    {
      esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
      cfg.display_func = displayQRCode;
      esp_qrcode_generate(&cfg, "https://github.com/GNBD/ats-mini-dualboot");
      gSprite.setTextColor(COL_TEXT, COL_BG);
      gSprite.drawString("GitHub", 120, 58, FONT_SMALL);
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString("github.com/", 120, 76, FONT_SMALL);
      gSprite.drawString("GNBD/ats-mini-", 120, 94, FONT_SMALL);
      gSprite.drawString("dualboot", 120, 112, FONT_SMALL);
      gSprite.drawString("Scan for source", 120, 134, FONT_SMALL);
      break;
    }
    case 1:
    {
      gSprite.setTextColor(COL_TEXT, COL_BG);
      gSprite.drawString("Libraries", 8, 38, FONT_SMALL);
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString("LovyanGFX     BSD-2-Clause", 8, 56, FONT_SMALL);
      gSprite.drawString("ESP32 Core    LGPL-2.1", 8, 72, FONT_SMALL);
      gSprite.drawString("ESP-IDF       Apache-2.0", 8, 88, FONT_SMALL);
      gSprite.drawString("LittleFS      Apache-2.0", 8, 104, FONT_SMALL);
      gSprite.drawString("QR code       Apache-2.0", 8, 120, FONT_SMALL);
      break;
    }
    case 2:
    {
      gSprite.setTextColor(COL_TEXT, COL_BG);
      gSprite.drawString("License", 8, 34, FONT_SMALL);
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString("Original code: MIT", 8, 48, FONT_SMALL);
      gSprite.drawString("Rotary.cpp/h: GPL-3.0", 8, 62, FONT_SMALL);
      gSprite.drawString("  Ben Buxton 2011", 8, 76, FONT_SMALL);
      gSprite.drawString("Bootloader: Apache-2.0", 8, 90, FONT_SMALL);
      gSprite.drawString("Libraries: see page 2", 8, 104, FONT_SMALL);
      gSprite.drawString("Hardware: CC BY-NC-SA (separate)", 8, 118, FONT_SMALL);
      gSprite.drawString("See repo NOTICE + LICENSES", 8, 132, FONT_SMALL);
      break;
    }
    case 3:
    {
      gSprite.setTextColor(COL_TEXT, COL_BG);
      gSprite.drawString("Recovery Help", 8, 34, FONT_SMALL);
      gSprite.drawString("Boot App0/1", 8, 50, FONT_SMALL);
      gSprite.drawString("Firmware Upd", 8, 66, FONT_SMALL);
      gSprite.drawString("WiFi", 8, 82, FONT_SMALL);
      gSprite.drawString("Setting/About", 8, 98, FONT_SMALL);
      gSprite.drawString("Erase", 8, 114, FONT_SMALL);
      gSprite.drawString("About", 8, 130, FONT_SMALL);
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString("slot + mode", 110, 50, FONT_SMALL);
      gSprite.drawString("Local/Network", 110, 66, FONT_SMALL);
      gSprite.drawString("scan+password", 110, 82, FONT_SMALL);
      gSprite.drawString("IP + web switch", 110, 98, FONT_SMALL);
      gSprite.drawString("Factory/partitions", 110, 114, FONT_SMALL);
      gSprite.drawString("this screen", 110, 130, FONT_SMALL);
      break;
    }
    case 4:
    {
      gSprite.setTextColor(COL_TEXT, COL_BG);
      gSprite.drawString("Flash Help (esptool)", 8, 34, FONT_SMALL);
      gSprite.setTextColor(COL_MUTED, COL_BG);

      // 현재 파티션 테이블을 그대로 나열 (2열)
      int idx = 0;
      char buf[40];
      esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
      while(it)
      {
        const esp_partition_t *p = esp_partition_get(it);
        snprintf(buf, sizeof(buf), "0x%06X %s", (unsigned)p->address, p->label);
        gSprite.drawString(buf, 6 + (idx / 5) * 162, 46 + (idx % 5) * 16, FONT_SMALL);
        idx++;
        it = esp_partition_next(it);
      }
      break;
    }
  }

  drawAboutFooter(page);
  gSprite.pushSprite(0, 0);
}

static void runAbout()
{
  int page = 0;
  drawAboutPage(page);

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      page = (page + d) % ABOUT_PAGES;
      if(page < 0) page += ABOUT_PAGES;
      drawAboutPage(page);
    }

    uint32_t h = readButton();
    if(h > 0) return;                      // click = back
    delay(10);
  }
}

// ---------------------------------------------------------------------------
// WiFi submenu
// ---------------------------------------------------------------------------

static void runWifiMenu()
{
  int selected = 0;
  gUiRefresh = false;
  drawWifiMenu(selected);

  while(true)
  {
    if(gUiRefresh) { gUiRefresh = false; drawWifiMenu(selected); }

    int8_t dir = readEncoder();
    if(dir)
    {
      selected = (selected + dir) % (int)WIFI_MENU_COUNT;
      if(selected < 0) selected += WIFI_MENU_COUNT;
      drawWifiMenu(selected);
    }

    uint32_t held = readButton();
    if(held == 0) { delay(10); continue; }
    if(held < 300) return;                            // click = back

    if(selected == 0)
    {
      runWifiSetting();
      if(WiFi.status() == WL_CONNECTED)
      {
        apModeActive = false;
        apIP = WiFi.localIP().toString();
      }
      else if(gWebEnabled)
      {
        wifiStartAp();
      }
    }
    else
    {
      runWifiAbout();
    }

    drawWifiMenu(selected);
    delay(10);
  }
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

static void runRecoveryMenu()
{
  int selected = 0;
  drawMenu(selected);

  while(true)
  {
    if(gUiRefresh) { gUiRefresh = false; drawMenu(selected); }

    int8_t dir = readEncoder();
    if(dir)
    {
      selected = (selected + dir) % (int)MENU_COUNT;
      if(selected < 0) selected += MENU_COUNT;
      drawMenu(selected);
    }

    uint32_t held = readButton();
    if(held == 0 || held < 300) { delay(10); continue; }   // hold = open

    switch(selected)
    {
      case 0:
        runBootModeMenu(false);
        break;

      case 1:
        runBootModeMenu(true);
        break;

      case 2:
        runFirmwareUpdate();
        break;

      case 3:
        runErase();
        break;

      case 4:
        runSettingsMenu();
        break;
    }

    drawMenu(selected);
  }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== RECOVERY BOOT (reset reason %d) ===\n", (int)esp_reset_reason());

  // The last committed slot is read from the recovery's own settings, so a
  // power cycle always returns to the same application. otadata is only a
  // fallback for a device that has no record yet (fresh install or upgrade).
  uint8_t savedSlot = loadBootSlot();
  if(savedSlot <= 1)
  {
    gBootIsApp1 = (savedSlot == 1);
  }
  else
  {
    const esp_partition_t *bootPart = esp_ota_get_boot_partition();
    gBootIsApp1 = bootPart && bootPart->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1;
  }
  gBootSlot = gBootIsApp1 ? "App1" : "App0";
  Serial.printf("active boot slot: %s\n", gBootSlot);

  esp_ota_mark_app_valid_cancel_rollback();

  const esp_partition_t *recovery = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP,
    static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_2), NULL);
  if(recovery) esp_ota_set_boot_partition(recovery);

  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  pinMode(ENCODER_PUSH_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoderISR, CHANGE);

  ledcAttach(PIN_LCD_BL, 16000, 8);
  ledcWrite(PIN_LCD_BL, 0);

  tft.init();
  tft.setRotation(3);
  gSprite.setPsram(true);
  gSprite.createSprite(SCR_W, SCR_H);
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_GOLD, COL_BG);
  gSprite.drawString("Boot Manager", 160, 50, FONT_LARGE);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("github.com/GNBD/ats-mini-dualboot", 160, 100, FONT_TINY);
  {
    char slotLine[40];
    snprintf(slotLine, sizeof(slotLine), "Current slot: %s", gBootSlot);
    gSprite.setTextColor(COL_GOLD, COL_BG);
    gSprite.drawString(slotLine, 160, 128, FONT_SMALL);
  }
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("v" RECOVERY_VERSION " (DES)", 160, 150, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
  applyBrightness(loadBrightness());

  // Quick bootloader + partition check while the splash is up; silent on
  // pass, a warning popup only when something is wrong.
  flashSelfCheck();

  Serial.println("step: 1s encoder check");
  bool encoderPressed = false;
  unsigned long start = millis();
  while(millis() - start < 1000)
  {
    if(digitalRead(ENCODER_PUSH_BUTTON) == LOW)
    {
      encoderPressed = true;
      break;
    }
    delay(10);
  }

  // A volume formatted by an older layout cannot be mounted and cannot be
  // handed to the app either, so it is repaired here before either happens.
  // If the erase fails, stay in recovery and leave littlefs alone.
  if(!littlefsUpgradeCheck())
  {
    encoderPressed = true;
    gLittleFsBlocked = true;
  }

  // Same for the two NVS labels, which fail silently instead of looping: the
  // menu path and the auto boot path both run this, because the applications
  // are the ones that would lose their settings.
  nvsUpgradeCheck();

  if(!encoderPressed)
  {
    Serial.printf("step: no encoder -> boot %s\n", gBootSlot);
    delay(300);
    if(gBootIsApp1) bootToApp1();
    else bootToApp0();
  }

  Serial.println("step: encoder held -> recovery menu");
  while(digitalRead(ENCODER_PUSH_BUTTON) == LOW) delay(50);
  delay(100);

  if(!gLittleFsBlocked && !LittleFS.begin(false, "/littlefs", 10, "littlefs"))
  {
    LittleFS.format();
    LittleFS.begin(false, "/littlefs", 10, "littlefs");
  }

  WiFi.mode(WIFI_MODE_NULL);
  WiFi.onEvent([](WiFiEvent_t e){ applyTxPower(); }, ARDUINO_EVENT_WIFI_STA_START);
  WiFi.onEvent([](WiFiEvent_t e){ applyTxPower(); }, ARDUINO_EVENT_WIFI_AP_START);

  String ssid, pass;
  WiFi.mode(WIFI_STA);
  applyTxPower();
  WiFi.setSleep(false);
  if(loadWifiCredentials(ssid, pass))
  {
    Serial.printf("step: STA connect to %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while(WiFi.status() != WL_CONNECTED && (millis() - t0) < BOOT_CONNECT_TIMEOUT)
      delay(50);
    if(WiFi.status() == WL_CONNECTED)
      Serial.printf("step: STA ok %s\n", WiFi.localIP().toString().c_str());
    else
    {
      Serial.println("step: STA failed (ignored)");
      WiFi.disconnect(false);
    }
  }
  else
  {
    Serial.println("step: no saved WiFi");
  }

  gWebEnabled = loadWebEnabled();

  if(WiFi.status() == WL_CONNECTED)
  {
    apIP = WiFi.localIP().toString();
    apModeActive = false;
  }
  else if(gWebEnabled)
  {
    Serial.println("step: no STA -> start AP");
    wifiStartAp();
  }
  else
  {
    Serial.println("step: no STA, web off -> no AP");
  }

  Serial.printf("web server: %s (%s)\n", gWebEnabled ? "on" : "off", apIP.c_str());

  startWebBackground();

  runRecoveryMenu();
}

void loop() {}
