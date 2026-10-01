// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// ATS Mini boot manager (lite) - Boot Manager
// Lite 1.0.0. Runs from its own partition.
//
// Boot sequence:
//   1. Power ON -> boot manager always boots first
//   2. Wait 1 second for encoder press
//   3. No encoder -> auto boot to app0
//   4. Encoder held -> menu (Boot App0/App1, Factory Reset, About)
//   5. Boot manager always sets itself as next boot target before jumping to app
#include <LovyanGFX.hpp>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include "Display.h"
#include "Rotary.h"

#define PIN_LCD_BL          38
#define ENCODER_PIN_A        2
#define ENCODER_PIN_B        1
#define ENCODER_PUSH_BUTTON 21
#define STORAGE_PARTITION    "settings"

#define HOLD_NOTICE_MS       500

static constexpr const lgfx::IFont* FONT_LARGE = &lgfx::fonts::Font4;
static constexpr const lgfx::IFont* FONT_SMALL = &lgfx::fonts::Font2;
static constexpr const lgfx::IFont* FONT_TINY  = &lgfx::fonts::Font0;

#define COL_BG      0x0000
#define COL_TEXT    0xFFFF
#define COL_MUTED   0x8410
#define COL_WARN    0xF800
#define COL_OK      0x07E0
#define COL_KEY     0x1082
#define COL_KEYSEL  0xFFFF

#define RECOVERY_VERSION "1.0.0"

LGFX tft;
Rotary encoder(ENCODER_PIN_B, ENCODER_PIN_A, false);

static volatile int16_t encoderCount = 0;
static volatile int16_t encoderCountAccel = 0;

static const char *gBootSlot = "App0";
static bool gBootIsApp1 = false;

static const char *menu[] = {
  "Boot App0",
  "Boot App1",
  "Factory Reset",
  "About"
};
#define MENU_COUNT (sizeof(menu) / sizeof(menu[0]))

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
// Display helpers
// ---------------------------------------------------------------------------

static void drawHeader(const char *title, const char *right = nullptr)
{
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(title, 10, 6, FONT_LARGE);
  if(right)
  {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(COL_OK, COL_BG);
    tft.drawString(right, 294, 8, FONT_SMALL);
    tft.setTextDatum(TL_DATUM);
  }
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
  drawHeader("Boot Manager Lite", gBootSlot);
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

// ---------------------------------------------------------------------------
// Erase
// ---------------------------------------------------------------------------

static const char *resetItems[] = {"Factory Reset"};
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
  bool checked[RESET_ITEM_COUNT] = {false};
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
      nvs_flash_deinit();
    }

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
// About (5 pages)
// ---------------------------------------------------------------------------

#define ABOUT_PAGES 5

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
  drawHeader("Boot Manager Lite");

  switch(page)
  {
    case 0:
    {
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(COL_OK, COL_BG);
      tft.drawString("Lite v" RECOVERY_VERSION, 294, 10, FONT_SMALL);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.drawString("GitHub", 8, 58, FONT_SMALL);
      tft.setTextColor(COL_MUTED, COL_BG);
      tft.drawString("github.com/", 8, 76, FONT_SMALL);
      tft.drawString("GNBD/ats-mini-", 8, 94, FONT_SMALL);
      tft.drawString("dualboot", 8, 112, FONT_SMALL);
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
      tft.drawString("Bootloader: Apache-2.0", 8, 104, FONT_SMALL);
      tft.drawString("Libraries: see page 2", 8, 120, FONT_SMALL);
      tft.drawString("Hardware: CC BY-NC-SA (separate)", 8, 136, FONT_SMALL);
      tft.drawString("See repo NOTICE + LICENSES", 8, 152, FONT_SMALL);
      break;
    }
    case 3:
    {
      tft.setTextColor(COL_TEXT, COL_BG);
      tft.drawString("Boot Manager Help", 8, 38, FONT_SMALL);
      tft.drawString("Boot App0/1", 8, 56, FONT_SMALL);
      tft.drawString("Factory Reset", 8, 74, FONT_SMALL);
      tft.drawString("About", 8, 92, FONT_SMALL);
      tft.setTextColor(COL_MUTED, COL_BG);
      tft.drawString("slot + mode", 110, 56, FONT_SMALL);
      tft.drawString("nvs + settings", 110, 74, FONT_SMALL);
      tft.drawString("this screen", 110, 92, FONT_SMALL);
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
// Main menu
// ---------------------------------------------------------------------------

static void runRecoveryMenu()
{
  int selected = 0;
  drawMenu(selected);

  while(true)
  {
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
        eraseMenu();
        break;

      case 3:
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
  Serial.printf("\n=== BOOT MANAGER LITE (reset reason %d) ===\n", (int)esp_reset_reason());
  Serial.printf("PSRAM: total=%u free=%u (heap=%u)\n",
                (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram(),
                (unsigned)ESP.getHeapSize());

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
  tft.drawString("Boot Manager Lite", 160, 50, FONT_LARGE);
  tft.setTextColor(COL_WARN, COL_BG);
  tft.drawString("Hold encoder for menu", 160, 80, FONT_SMALL);
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

  runRecoveryMenu();
}

void loop() {}
