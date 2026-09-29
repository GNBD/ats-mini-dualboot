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
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include <qrcode.h>
#include "Display.h"
#include "Rotary.h"
#include <freertos/task.h>

#define PIN_LCD_BL          38
#define ENCODER_PIN_A        2
#define ENCODER_PIN_B        1
#define ENCODER_PUSH_BUTTON 21
#define STORAGE_PARTITION    "settings"

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

#define COL_BG      0x0000
#define COL_TEXT    0xFFFF
#define COL_MUTED   0x8410
#define COL_WARN    0xF800
#define COL_OK      0x07E0
#define COL_AP      0xFFE0
#define COL_KEY     0x1082
#define COL_KEYSEL  0xFFFF

#define RECOVERY_VERSION "3.1.0"

// Default: fetch this .txt (one URL per line). Local /update_url.txt and
// DEFAULT_UPDATE_URLS are fallbacks when the remote list is unavailable.
static const char *REMOTE_UPDATE_URL_TXT =
  "https://gnbdatsmini.netlify.app/update_url.txt";

static const char *DEFAULT_UPDATE_URLS[MAX_URLS] = {};

LGFX tft;
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

static const char *menu[] = {
  "Boot App0",
  "Boot App1",
  "Firmware Update",
  "WiFi",
  "Erase",
  "About"
};
#define MENU_COUNT (sizeof(menu) / sizeof(menu[0]))

static const char *wifiMenu[] = {
  "WiFi Setting",
  "WiFi About"
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

static uint32_t readButton()
{
  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW) return 0;
  delay(30);
  if(digitalRead(ENCODER_PUSH_BUTTON) != LOW) return 0;

  uint32_t start = millis();
  while(digitalRead(ENCODER_PUSH_BUTTON) == LOW) delay(10);
  return millis() - start;
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
  tft.fillRect(290, 4, 30, 18, COL_BG);
  if(apModeActive)
  {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_AP, COL_BG);
    tft.drawString("AP", 300, 6, FONT_SMALL);
    return;
  }
  if(WiFi.status() != WL_CONNECTED)
  {
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawLine(300, 7, 310, 17, COL_WARN);
    tft.drawLine(310, 7, 300, 17, COL_WARN);
    return;
  }
  tft.drawCircle(305, 17, 2, COL_OK);
  tft.drawArc(305, 17, 5, 6, 225, 315, COL_OK);
  tft.drawArc(305, 17, 9, 10, 225, 315, COL_OK);
}

static void drawHeader(const char *title, const char *right = nullptr)
{
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(title, 10, 6, FONT_LARGE);
  drawNetIcon();
  if(right)
  {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(COL_OK, COL_BG);
    tft.drawString(right, 294, 8, FONT_SMALL);
    tft.setTextDatum(TL_DATUM);
  }
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
// Boot helpers
// ---------------------------------------------------------------------------

static void bootToApp(esp_partition_subtype_t appSubtype)
{
  const esp_partition_t *app = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, appSubtype, NULL);
  if(app) esp_ota_set_boot_partition(app);
  ESP.restart();
}

// Selects the freshly flashed app as the next boot target.
static bool setBootSlot(int targetSlot)
{
  esp_partition_subtype_t sub = (targetSlot == 0)
    ? static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0)
    : static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1);
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, NULL);
  return p && (esp_ota_set_boot_partition(p) == ESP_OK);
}

static void bootToApp0()
{
  bootToApp(static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0));
}

static void bootToApp1()
{
  bootToApp(static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1));
}

static void drawBootModeMenu(bool isApp1, int selected)
{
  tft.fillScreen(COL_BG);
  drawHeader(isApp1 ? "BOOT APP1" : "BOOT APP0");
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Choose how to boot", 10, 30, FONT_SMALL);

  for(int i = 0; i < (int)BOOT_MODE_COUNT; i++)
  {
    int y = 56 + i * 22;
    if(i == selected)
    {
      tft.fillRoundRect(5, y - 3, 310, 22, 4, COL_TEXT);
      tft.setTextColor(COL_BG, COL_TEXT);
    }
    else
    {
      tft.setTextColor(COL_TEXT, COL_BG);
    }
    tft.drawString(bootModeMenu[i], 15, y, FONT_SMALL);
  }

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(selected == 0 ? "Boots the app normally"
                               : "Press + keep holding the button",
                 10, 116, FONT_SMALL);
  tft.drawString("Hold = Select  Click = Back", 10, 146, FONT_SMALL);
}

// Hold mode: only a notice, then the app boots with the encoder button still
// down so the target firmware sees a plain press. Nothing here looks at the
// button, so holding it never reboots the device and never re-enters the
// recovery menu.
static void runHoldModeBoot(bool isApp1)
{
  tft.fillScreen(COL_BG);
  drawHeader("HOLD MODE", isApp1 ? "App1" : "App0");
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_OK, COL_BG);
  tft.drawString("Press and keep holding", 160, 44, FONT_SMALL);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Do not release the button", 160, 110, FONT_SMALL);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Booting to the app...", 160, 152, FONT_SMALL);
  tft.drawRoundRect(28, 134, 264, 16, COL_MUTED);

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
        tft.fillRoundRect(6, 66, 308, 40, 6, blink ? COL_KEY : COL_WARN);
        tft.setTextColor(blink ? COL_OK : COL_TEXT, blink ? COL_KEY : COL_WARN);
        tft.drawString("KEEP HOLDING", 160, 70, FONT_LARGE);
      }
      else
      {
        tft.fillRoundRect(6, 66, 308, 40, 6, COL_KEY);
        tft.setTextColor(blink ? COL_OK : COL_TEXT, COL_KEY);
        tft.drawString("PRESS NOW", 160, 70, FONT_LARGE);
      }
    }

    uint32_t w = down ? (260UL * el / HOLD_NOTICE_MS) : 0;
    if(down && w < 2) w = 2;
    tft.fillRoundRect(30, 136, 260, 12, 5, COL_KEY);
    if(w) tft.fillRoundRect(30, 136, w, 12, 5, COL_OK);

    delay(10);
  }

  Serial.printf("step: hold mode -> boot App%d\n", isApp1 ? 1 : 0);
  if(isApp1) bootToApp1();
  else bootToApp0();
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
    if(held < 300) return;                 // click = back to the main menu

    if(selected == 0)
    {
      if(isApp1) bootToApp1();
      else bootToApp0();
    }
    runHoldModeBoot(isApp1);
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void drawMenu(int selected)
{
  tft.fillScreen(COL_BG);
  drawHeader("Boot Manager", gBootSlot);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Hold = Select", 10, 30, FONT_SMALL);

  for(uint8_t i = 0; i < MENU_COUNT; i++)
  {
    int y = 48 + i * 18;
    if(i == selected)
    {
      tft.fillRoundRect(5, y - 1, 310, 18, 4, COL_TEXT);
      tft.setTextColor(COL_BG, COL_TEXT);
    }
    else
    {
      tft.setTextColor(COL_TEXT, COL_BG);
    }
    tft.drawString(menu[i], 15, y, FONT_SMALL);
  }
}

static void drawWifiMenu(int selected)
{
  tft.fillScreen(COL_BG);
  drawHeader("WiFi");
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Hold = Select  Click = Back", 10, 30, FONT_SMALL);

  String status;
  if(apModeActive) status = "AP Mode";
  else if(WiFi.status() == WL_CONNECTED) status = WiFi.SSID();
  else status = "Offline";
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(status, 10, 150, FONT_SMALL);

  for(uint8_t i = 0; i < WIFI_MENU_COUNT; i++)
  {
    int y = 52 + i * 22;
    if(i == selected)
    {
      tft.fillRoundRect(5, y - 2, 310, 22, 4, COL_TEXT);
      tft.setTextColor(COL_BG, COL_TEXT);
    }
    else
    {
      tft.setTextColor(COL_TEXT, COL_BG);
    }
    tft.drawString(wifiMenu[i], 15, y, FONT_SMALL);
  }
}

static void drawUpdateProgress(int percent, const char *status)
{
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(status, 10, 65, FONT_SMALL);
  drawNetIcon();

  tft.fillRoundRect(10, 90, 300, 20, 4, 0x4208);
  int w = (percent * 296) / 100;
  if(w > 0) tft.fillRoundRect(12, 92, w, 16, 3, COL_TEXT);

  char buf[16];
  sprintf(buf, "%d%%", percent);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, 160, 100, FONT_SMALL);
  tft.setTextDatum(TL_DATUM);
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
  tft.fillScreen(COL_BG);
  drawHeader("FIRMWARE UPDATE");
  tft.setTextColor(COL_MUTED, COL_BG);
  String target = String("Target: ") + targetLabel + "  Select .bin:";
  tft.drawString(target, 10, 35, FONT_SMALL);

  int page = selected / FILE_PAGE_SIZE;
  int start = page * FILE_PAGE_SIZE;
  int end = start + FILE_PAGE_SIZE;
  if(end > count) end = count;

  for(int i = start; i < end; i++)
  {
    int row = i - start;
    int y = 58 + row * 24;
    if(i == selected)
    {
      tft.fillRoundRect(5, y - 2, 310, 22, 4, COL_TEXT);
      tft.setTextColor(COL_BG, COL_TEXT);
    }
    else
    {
      tft.setTextColor(COL_TEXT, COL_BG);
    }
    tft.drawString(files[i], 15, y, FONT_SMALL);
  }

  int pages = (count + FILE_PAGE_SIZE - 1) / FILE_PAGE_SIZE;
  if(pages < 1) pages = 1;
  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d/%d", page + 1, pages);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(pageBuf, 10, 155, FONT_SMALL);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("Click=Back Hold=Flash", 312, 155, FONT_SMALL);
  tft.setTextDatum(TL_DATUM);
}

static void drawChoiceList(const char *title, const char *hint,
                           const char **items, int itemCount, int selected)
{
  tft.fillScreen(COL_BG);
  drawHeader(title);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(hint, 10, 35, FONT_SMALL);

  int page = selected / FILE_PAGE_SIZE;
  int start = page * FILE_PAGE_SIZE;
  int end = start + FILE_PAGE_SIZE;
  if(end > itemCount) end = itemCount;

  for(int i = start; i < end; i++)
  {
    int row = i - start;
    int y = 58 + row * 24;
    if(i == selected)
    {
      tft.fillRoundRect(5, y - 2, 310, 22, 4, COL_TEXT);
      tft.setTextColor(COL_BG, COL_TEXT);
    }
    else
    {
      tft.setTextColor(COL_TEXT, COL_BG);
    }
    tft.drawString(items[i], 15, y, FONT_SMALL);
  }

  tft.setTextColor(COL_MUTED, COL_BG);
  if(itemCount > FILE_PAGE_SIZE)
  {
    int pages = (itemCount + FILE_PAGE_SIZE - 1) / FILE_PAGE_SIZE;
    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "Page %d/%d", page + 1, pages);
    tft.drawString(pageBuf, 10, 155, FONT_SMALL);
  }
  tft.setTextDatum(TR_DATUM);
  tft.drawString("Click=Back Hold=Select", 312, 155, FONT_SMALL);
  tft.setTextDatum(TL_DATUM);
}

// Generic single-page choice: rotate + hold=select, short=back.
// Returns selected index, or -1 on back.
static int runChoice(const char *title, const char *hint,
                     const char **items, int itemCount)
{
  int selected = 0;
  drawChoiceList(title, hint, items, itemCount, selected);

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      selected = (selected + d) % itemCount;
      if(selected < 0) selected += itemCount;
      drawChoiceList(title, hint, items, itemCount, selected);
    }

    uint32_t h = readButton();
    if(h >= 300) return selected;
    if(h > 0) return -1;
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

static volatile bool gFlashBusy = false;
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
  tft.setTextColor(COL_WARN, COL_BG);
  tft.drawString(msg, 10, 65, FONT_SMALL);
  delay(2000);
}

static void uiNote(const char *msg)
{
  if(gUiQuiet) return;
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString(msg, 10, 45, FONT_SMALL);
}

static void uiProgress(int percent, const char *status)
{
  if(gWebFlash) flashSetProgress(percent, status);
  if(gUiQuiet) return;
  drawUpdateProgress(percent, status);
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
      uiProgress(percent, "Flashing...");
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
    drawUpdateProgress(100, "Done!");
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

  tft.fillScreen(COL_BG);
  drawHeader("DOWNLOAD");
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(fname, 10, 40, FONT_SMALL);

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
          drawUpdateProgress(percent, "Downloading...");
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
    tft.fillScreen(COL_BG);
    drawHeader("NETWORK");
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString("No URLs configured", 10, 50, FONT_SMALL);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("Upload update_url.txt", 10, 80, FONT_SMALL);
    tft.drawString("or edit DEFAULT_UPDATE_URLS", 10, 98, FONT_SMALL);
    tft.drawString("Click to go back", 10, 140, FONT_SMALL);
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
    tft.fillScreen(COL_BG);
    drawHeader("DOWNLOAD");
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString("Failed:", 10, 50, FONT_SMALL);
    tft.drawString(fname, 10, 70, FONT_SMALL);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("Click to go back", 10, 140, FONT_SMALL);
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
    tft.fillScreen(COL_BG);
    drawHeader("DOWNLOAD");
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString("Failed:", 10, 50, FONT_SMALL);
    tft.drawString(fname, 10, 70, FONT_SMALL);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("Click to go back", 10, 140, FONT_SMALL);
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
  const char *slotNames[] = {"App0", "App1"};
  int slotSel = runChoice("FIRMWARE UPDATE", "Select target slot:", slotNames, 2);
  if(slotSel < 0) return;

  const char *sourceNames[] = {"Local files", "Network"};
  int srcSel = runChoice("FIRMWARE UPDATE", "Select source:", sourceNames, 2);
  if(srcSel < 0) return;

  if(srcSel == 1)
  {
    int got = downloadAllFromUrls();
    if(got == 0)
    {
      tft.fillScreen(COL_BG);
      drawHeader("NETWORK");
      tft.setTextColor(COL_WARN, COL_BG);
      tft.drawString("No files downloaded", 10, 60, FONT_SMALL);
      delay(2000);
      return;
    }
  }

  String files[32];
  int fileCount = listFirmwareFiles(files, 32);
  if(fileCount == 0)
  {
    tft.fillScreen(COL_BG);
    drawHeader("FIRMWARE UPDATE");
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString("No .bin files found", 10, 10, FONT_LARGE);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("Use Network source or", 10, 50, FONT_SMALL);
    tft.drawString("Upload via web browser.", 10, 70, FONT_SMALL);
    delay(2500);
    return;
  }

  int fileSel = 0;
  drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);

  while(true)
  {
    int8_t fd = readEncoder();
    if(fd)
    {
      fileSel = (fileSel + fd) % fileCount;
      if(fileSel < 0) fileSel += fileCount;
      drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
    }

    uint32_t h = readButton();
    if(h >= 300)
    {
      if(gFlashBusy || uploadBusy())
      {
        tft.fillScreen(COL_BG);
        drawHeader("FIRMWARE UPDATE");
        tft.setTextColor(COL_WARN, COL_BG);
        tft.drawString("Web flash running", 10, 60, FONT_SMALL);
        tft.setTextColor(COL_MUTED, COL_BG);
        tft.drawString("Wait for it to finish", 10, 80, FONT_SMALL);
        delay(1500);
        drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
      }
      else
      {
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
    else if(h > 0)
    {
      break;
    }
    delay(10);
  }
}

// ---------------------------------------------------------------------------
// Erase
// ---------------------------------------------------------------------------

static const char *resetItems[] = {"Factory Reset", "App0", "App1", "LittleFS"};
#define RESET_ITEM_COUNT (sizeof(resetItems) / sizeof(resetItems[0]))
#define RESET_FACTORY_INDEX 0

static void drawResetMenu(const bool *checked, int selected)
{
  tft.fillScreen(COL_BG);
  drawHeader("ERASE");
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Click=Check  Hold=Erase", 10, 30, FONT_SMALL);

  for(int i = 0; i < (int)RESET_ITEM_COUNT; i++)
  {
    int y = 54 + i * 22;
    if(i == selected)
    {
      tft.fillRoundRect(5, y - 2, 310, 22, 4, COL_TEXT);
      tft.setTextColor(COL_BG, COL_TEXT);
    }
    else
    {
      tft.setTextColor(COL_TEXT, COL_BG);
    }
    char line[24];
    snprintf(line, sizeof(line), "[%c] %s", checked[i] ? 'X' : ' ', resetItems[i]);
    tft.drawString(line, 15, y, FONT_SMALL);
  }

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Hold with nothing = Back", 10, 155, FONT_SMALL);
}

static void drawResetProgress(const char *label, int percent, int overall)
{
  tft.fillRect(0, 42, 320, 120, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(label, 10, 46, FONT_SMALL);

  tft.fillRoundRect(10, 70, 300, 22, 4, 0x4208);
  int w = (percent * 296) / 100;
  if(w > 0) tft.fillRoundRect(12, 72, w, 18, 3, COL_TEXT);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(buf, 160, 81, FONT_SMALL);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Overall", 10, 104, FONT_SMALL);
  tft.fillRoundRect(70, 104, 240, 12, 3, 0x4208);
  int ow = (overall * 236) / 100;
  if(ow > 0) tft.fillRoundRect(72, 106, ow, 8, 2, COL_OK);
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

static void eraseMenu()
{
  bool checked[RESET_ITEM_COUNT] = {false, false, false, false};
  int selected = 0;
  drawResetMenu(checked, selected);

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      selected = (selected + d) % (int)RESET_ITEM_COUNT;
      if(selected < 0) selected += (int)RESET_ITEM_COUNT;
      drawResetMenu(checked, selected);
    }

    uint32_t h = readButton();
    if(h == 0) { delay(10); continue; }

    if(h < 300)
    {
      checked[selected] = !checked[selected];
      drawResetMenu(checked, selected);
      continue;
    }

    int totalUnits = 0;
    for(int i = 0; i < (int)RESET_ITEM_COUNT; i++)
      if(checked[i]) totalUnits += (i == RESET_FACTORY_INDEX) ? 2 : 1;
    if(totalUnits == 0) return;

    const esp_partition_t *nvsPart = NULL, *settingsPart = NULL;
    if(checked[RESET_FACTORY_INDEX])
    {
      nvsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
      settingsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS, STORAGE_PARTITION);
    }

    const esp_partition_t *app0 = NULL, *app1 = NULL, *fs = NULL;
    if(checked[1]) app0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0), NULL);
    if(checked[2]) app1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1), NULL);
    if(checked[3]) fs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, NULL);

    if(fs) LittleFS.end();
    if(checked[RESET_FACTORY_INDEX]) nvs_flash_deinit();

    tft.fillScreen(COL_BG);
    drawHeader("ERASE");

    int unit = 0;
    bool ok = true;
    for(int i = 0; i < (int)RESET_ITEM_COUNT && ok; i++)
    {
      if(!checked[i]) continue;
      int units = (i == RESET_FACTORY_INDEX) ? 2 : 1;
      int start = unit * 100 / totalUnits;
      int end = (unit + units) * 100 / totalUnits;

      if(i == RESET_FACTORY_INDEX)
      {
        int mid = start + (end - start) / 2;
        ok = nvsPart && erasePartitionProgress(nvsPart, resetItems[i], start, mid);
        if(ok) ok = settingsPart && erasePartitionProgress(settingsPart, resetItems[i], mid, end);
      }
      else if(i == 1) ok = app0 && erasePartitionProgress(app0, resetItems[i], start, end);
      else if(i == 2) ok = app1 && erasePartitionProgress(app1, resetItems[i], start, end);
      else if(i == 3) ok = fs && erasePartitionProgress(fs, resetItems[i], start, end);

      unit += units;
    }

    if(!ok)
    {
      tft.setTextColor(COL_WARN, COL_BG);
      tft.drawString("Erase failed", 10, 150, FONT_SMALL);
      delay(2500);
      return;
    }

    drawResetProgress("Done", 100, 100);
    delay(1000);
    ESP.restart();
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
  tft.fillScreen(COL_BG);
  drawHeader("WIFI SETTING");

  int listBottom;
  if(kbOpen)
  {
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString("SSID: " + ssid, 10, 32, FONT_SMALL);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("Password:", 10, 50, FONT_SMALL);
    tft.setTextColor(COL_TEXT, COL_BG);
    String shown = password;
    if(shown.length() > 24) shown = shown.substring(shown.length() - 24);
    tft.drawString(shown + "_", 90, 50, FONT_SMALL);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString("Click=Type Hold=OK", 10, 68, FONT_SMALL);

    listBottom = 84;
    kbBuild(kbMode, listBottom);

    for(int i = 0; i < kbKeyCount; i++)
    {
      const KbKey &k = kbKeys[i];
      bool sel = (i == kbCursor);
      uint16_t bg = sel ? COL_KEYSEL : COL_KEY;
      uint16_t fg = sel ? COL_BG : COL_TEXT;
      tft.fillRoundRect(k.x, k.y, k.w, k.h, 3, bg);
      tft.setTextColor(fg, bg);
      tft.setTextDatum(MC_DATUM);
      int cx = k.x + k.w / 2;
      int cy = k.y + k.h / 2;
      if(k.type == KB_MODE)
        tft.drawString(KB_MODE_NAMES[(int)k.ch], cx, cy, FONT_SMALL);
      else if(k.type == KB_BSP)
        tft.drawString("BSP", cx, cy, FONT_SMALL);
      else if(k.type == KB_CANCEL)
        tft.drawString("X", cx, cy, FONT_SMALL);
      else if(k.type == KB_SPACE)
        tft.drawString("SPACE", cx, cy, FONT_SMALL);
      else
      {
        char s[2] = {k.ch, 0};
        tft.drawString(s, cx, cy, FONT_SMALL);
      }
    }
    tft.setTextDatum(TL_DATUM);
  }
  else
  {
    tft.setTextColor(COL_MUTED, COL_BG);
    if(netCount == 0)
      tft.drawString("No networks found", 10, 36, FONT_SMALL);
    else
      tft.drawString("Hold=Connect Click=Rescan", 10, 36, FONT_SMALL);

    int page = selected / WIFI_PAGE_SIZE;
    int start = page * WIFI_PAGE_SIZE;
    int end = start + WIFI_PAGE_SIZE;
    if(end > netCount) end = netCount;

    for(int i = start; i < end; i++)
    {
      int row = i - start;
      int y = 56 + row * 24;
      if(i == selected)
      {
        tft.fillRoundRect(5, y - 2, 310, 22, 4, COL_TEXT);
        tft.setTextColor(COL_BG, COL_TEXT);
      }
      else
      {
        tft.setTextColor(COL_TEXT, COL_BG);
      }
      tft.drawString(WiFi.SSID(i), 15, y, FONT_SMALL);
    }

    int pages = (netCount + WIFI_PAGE_SIZE - 1) / WIFI_PAGE_SIZE;
    if(pages < 1) pages = 1;
    char pageBuf[20];
    snprintf(pageBuf, sizeof(pageBuf), "Page %d/%d", page + 1, pages);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString(pageBuf, 10, 155, FONT_SMALL);
    listBottom = 150;
  }

  (void)listBottom;
  (void)nets;
}

static int kbHandleEncoder(int8_t d)
{
  if(kbKeyCount == 0 || d == 0) return 0;
  kbCursor = ((kbCursor + d) % kbKeyCount + kbKeyCount) % kbKeyCount;
  return 1;
}

static void runWifiSetting()
{
  tft.fillScreen(COL_BG);
  drawHeader("WIFI SETTING");
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Scanning...", 10, 50, FONT_SMALL);
  drawNetIcon();

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
          tft.fillScreen(COL_BG);
          drawHeader("WIFI SETTING");
          tft.setTextColor(COL_TEXT, COL_BG);
          tft.drawString("Connecting: " + activeSsid, 10, 50, FONT_SMALL);
          bool ok = wifiConnectBlocking(activeSsid, "", WIFI_CONNECT_TIMEOUT);
          if(ok)
          {
            saveWifiCredentials(activeSsid, "");
            tft.setTextColor(COL_OK, COL_BG);
            tft.drawString("Connected!", 10, 80, FONT_SMALL);
            delay(1200);
            return;
          }
          tft.setTextColor(COL_WARN, COL_BG);
          tft.drawString("Failed", 10, 80, FONT_SMALL);
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
        tft.fillScreen(COL_BG);
        drawHeader("WIFI SETTING");
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.drawString("Connecting: " + activeSsid, 10, 50, FONT_SMALL);
        bool ok = wifiConnectBlocking(activeSsid, password, WIFI_CONNECT_TIMEOUT);
        if(ok)
        {
          saveWifiCredentials(activeSsid, password);
          tft.setTextColor(COL_OK, COL_BG);
          tft.drawString("Connected!", 10, 80, FONT_SMALL);
          delay(1200);
          WiFi.scanDelete();
          return;
        }
        tft.setTextColor(COL_WARN, COL_BG);
        tft.drawString("Failed - check password", 10, 80, FONT_SMALL);
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
  tft.fillScreen(COL_BG);
  drawHeader("WIFI ABOUT");
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_MUTED, COL_BG);

  bool sta = (WiFi.status() == WL_CONNECTED);
  bool ap  = apModeActive;
  int y = 42;

  if(sta)
  {
    tft.drawString("SSID: " + WiFi.SSID(), 10, y, FONT_SMALL); y += 17;
  }
  else if(ap)
  {
    tft.drawString("AP:   ats-recovery", 10, y, FONT_SMALL); y += 17;
  }

  if(ap)
  {
    tft.drawString("PASS: 12345678", 10, y, FONT_SMALL); y += 17;
  }

  const char *mode = sta ? (ap ? "AP+STA" : "Station") : (ap ? "AP" : "Offline");
  tft.drawString("Mode: " + String(mode), 10, y, FONT_SMALL); y += 17;

  String ip = currentIp();
  tft.drawString(ip == "-" ? String("IP:   -") : String("IP:   http://" + ip),
                 10, y, FONT_SMALL);
  y += 17;

  tft.setTextColor(gWebEnabled ? COL_OK : COL_WARN, COL_BG);
  tft.drawString(gWebEnabled ? "WEB:  ON" : "WEB:  OFF", 10, y, FONT_SMALL); y += 23;

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Rotate = Web on/off", 10, y, FONT_SMALL); y += 17;
  tft.drawString("Click = Back", 10, y, FONT_SMALL);
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
  const int ox = 10, oy = 42;
  int qr = size * scale;

  tft.fillRect(ox - 4, oy - 4, qr + 8, qr + 8, COL_TEXT);
  for(int y = 0; y < size; y++)
    for(int x = 0; x < size; x++)
      if(esp_qrcode_get_module(qrcode, x, y))
        tft.fillRect(ox + x * scale, oy + y * scale, scale, scale, COL_BG);
}

static void drawAboutFooter(int page)
{
  if(page != 0) return;
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.drawString("Rotate=Page Hold=Back", 312, 153, FONT_SMALL);
  tft.setTextDatum(TL_DATUM);
}

static void drawAboutPage(int page)
{
  tft.fillScreen(COL_BG);
  drawHeader("Boot Manager");

  switch(page)
  {
    case 0:
    {
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(COL_OK, COL_BG);
      tft.drawString("v" RECOVERY_VERSION, 294, 10, FONT_SMALL);
      tft.setTextDatum(TL_DATUM);
      esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
      cfg.display_func = displayQRCode;
      esp_qrcode_generate(&cfg, "https://github.com/GNBD/ats-mini-dualboot");
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.drawString("GitHub", 120, 58, FONT_SMALL);
      tft.setTextColor(COL_MUTED, COL_BG);
      tft.drawString("github.com/", 120, 76, FONT_SMALL);
      tft.drawString("GNBD/ats-mini-", 120, 94, FONT_SMALL);
      tft.drawString("dualboot", 120, 112, FONT_SMALL);
      tft.drawString("Scan for source", 120, 134, FONT_SMALL);
      break;
    }
    case 1:
    {
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.drawString("Libraries", 8, 38, FONT_SMALL);
      tft.setTextColor(COL_MUTED, COL_BG);
      tft.drawString("LovyanGFX     BSD-2-Clause", 8, 56, FONT_SMALL);
      tft.drawString("ESP32 Core    LGPL-2.1", 8, 72, FONT_SMALL);
      tft.drawString("ESP-IDF       Apache-2.0", 8, 88, FONT_SMALL);
      tft.drawString("LittleFS      Apache-2.0", 8, 104, FONT_SMALL);
      break;
    }
    case 2:
    {
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.drawString("License", 8, 38, FONT_SMALL);
      tft.setTextColor(COL_MUTED, COL_BG);
      tft.drawString("Original code: MIT", 8, 56, FONT_SMALL);
      tft.drawString("Rotary.cpp/h: GPL-3.0", 8, 72, FONT_SMALL);
      tft.drawString("  Ben Buxton 2011", 8, 88, FONT_SMALL);
      tft.drawString("Libraries: see page 2", 8, 104, FONT_SMALL);
      tft.drawString("Hardware: CC BY-NC-SA", 8, 120, FONT_SMALL);
      tft.drawString("  may apply separately", 8, 136, FONT_SMALL);
      tft.drawString("See repo NOTICE + LICENSES", 8, 152, FONT_SMALL);
      break;
    }
    case 3:
    {
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.drawString("Recovery Help", 8, 38, FONT_SMALL);
      tft.drawString("Boot App0/1", 8, 56, FONT_SMALL);
      tft.drawString("Firmware Upd", 8, 74, FONT_SMALL);
      tft.drawString("WiFi", 8, 92, FONT_SMALL);
      tft.drawString("Setting/About", 8, 110, FONT_SMALL);
      tft.drawString("Erase", 8, 128, FONT_SMALL);
      tft.drawString("About", 8, 146, FONT_SMALL);
      tft.setTextColor(COL_MUTED, COL_BG);
      tft.drawString("slot + mode", 110, 56, FONT_SMALL);
      tft.drawString("Local/Network", 110, 74, FONT_SMALL);
      tft.drawString("scan+password", 110, 92, FONT_SMALL);
      tft.drawString("IP + web switch", 110, 110, FONT_SMALL);
      tft.drawString("Factory/partitions", 110, 128, FONT_SMALL);
      tft.drawString("this screen", 110, 146, FONT_SMALL);
      break;
    }
    case 4:
    {
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.drawString("Flash Help (esptool)", 8, 38, FONT_SMALL);
      tft.setTextColor(COL_MUTED, COL_BG);

      // 현재 파티션 테이블을 그대로 나열 (2열)
      int idx = 0;
      char buf[40];
      esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
      while(it)
      {
        const esp_partition_t *p = esp_partition_get(it);
        snprintf(buf, sizeof(buf), "0x%06X %s", (unsigned)p->address, p->label);
        tft.drawString(buf, 6 + (idx / 8) * 162, 52 + (idx % 8) * 14, FONT_SMALL);
        idx++;
        it = esp_partition_next(it);
      }
      break;
    }
  }

  drawAboutFooter(page);
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
    if(h >= 300) return;
    if(h > 0)
    {
      page = (page + 1) % ABOUT_PAGES;
      drawAboutPage(page);
    }
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
    if(held < 300) return;

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
    if(held == 0 || held < 300) { delay(10); continue; }

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
        runWifiMenu();
        break;

      case 4:
        eraseMenu();
        break;

      case 5:
        runAbout();
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

  const esp_partition_t *bootPart = esp_ota_get_boot_partition();
  gBootIsApp1 = bootPart && bootPart->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1;
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
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Boot Manager", 160, 50, FONT_LARGE);
  tft.setTextColor(COL_WARN, COL_BG);
  tft.drawString("Hold encoder for Recovery", 160, 80, FONT_SMALL);
  tft.setTextColor(COL_MUTED, COL_BG);
  {
    char slotLine[32];
    snprintf(slotLine, sizeof(slotLine), "Current: %s", gBootSlot);
    tft.drawString(slotLine, 160, 105, FONT_SMALL);
  }
  ledcWrite(PIN_LCD_BL, 255);

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

  if(!LittleFS.begin(false, "/littlefs", 10, "littlefs"))
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
