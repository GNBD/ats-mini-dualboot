// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// About (3 pages, miniradio style): GITHUB / ABOUT / LICENSES
// ---------------------------------------------------------------------------

#define ABOUT_PAGES 4

static void displayQRCode(esp_qrcode_handle_t qrcode)
{
  int size = esp_qrcode_get_size(qrcode);
  const int scale = 3;
  const int ox = 10, oy = 38;
  int qr = size * scale;

  gSprite.fillRect(ox - 4, oy - 4, qr + 8, qr + 8, COL_TEXT);
  for(int y = 0; y < size; y++)
    for(int x = 0; x < size; x++)
      if(esp_qrcode_get_module(qrcode, x, y))
        gSprite.fillRect(ox + x * scale, oy + y * scale, scale, scale, COL_BG);
}

static void drawAboutRow(int y, const char *key, const String &value)
{
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_PANEL);
  gSprite.drawString(key, 20, y, FONT_TINY);
  gSprite.setTextDatum(TR_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString(value.c_str(), 300, y, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
}

static void drawAboutPage(int page)
{
  gSprite.fillScreen(COL_BG);
  drawHeader(gCallsign.length() ? gCallsign.c_str() : "DBFM", "ABOUT");

  char pg[8];
  snprintf(pg, sizeof(pg), "%d/%d", page + 1, ABOUT_PAGES);
  gSprite.fillRoundRect(6, 27, 308, 116, 8, COL_PANEL);
  gSprite.drawRoundRect(6, 27, 308, 116, 8, COL_LINE);
  gSprite.setTextDatum(TR_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_PANEL);
  gSprite.drawString(pg, 306, 30, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);

  if(page == 0)
  {
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = displayQRCode;
    esp_qrcode_generate(&cfg, "https://github.com/GNBD/ats-mini-dualboot");

    gSprite.setTextDatum(TL_DATUM);
    gSprite.setTextColor(COL_ACC2, COL_PANEL);
    gSprite.drawString("GITHUB", 120, 40, FONT_SMALL);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
    gSprite.drawString("GNBD", 120, 58, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_PANEL);
    gSprite.drawString("ats-mini-dualboot", 120, 76, FONT_SMALL);
    gSprite.drawString("github.com/GNBD/", 120, 94, FONT_TINY);
    gSprite.drawString("ats-mini-dualboot", 120, 108, FONT_TINY);
    gSprite.setTextColor(COL_ACC, COL_PANEL);
    gSprite.drawString("SCAN TO OPEN", 120, 128, FONT_TINY);
  }
  else if(page == 1)
  {
    gSprite.setTextDatum(TC_DATUM);
    gSprite.setTextColor(COL_ACC2, COL_PANEL);
    gSprite.drawString("DBFM", 160, 34, FONT_SMALL);
    gSprite.setTextDatum(TL_DATUM);
    gSprite.fillRect(78, 48, 164, 1, COL_LINE);

    drawAboutRow(54, "VERSION", RECOVERY_VERSION);
    drawAboutRow(69, "BUILD", String(__DATE__));
    drawAboutRow(84, "ESP-IDF", String(ESP.getSdkVersion()));
    drawAboutRow(99, "CHIP", String(ESP.getChipModel()));
    drawAboutRow(114, "SLOT", String(gBootSlot));
    drawAboutRow(129, "FLASH", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
  }
  else if(page == 2)
  {
    gSprite.setTextDatum(TL_DATUM);
    gSprite.setTextColor(COL_ACC2, COL_PANEL);
    gSprite.drawString("THIRD-PARTY", 20, 34, FONT_TINY);
    gSprite.fillRect(20, 46, 288, 1, COL_LINE);

    static const char *const names[6] = {
      "DBFM / original", "ENCODER", "LOVYANGFX",
      "ARDUINO-ESP32", "ESP-IDF", "LITTLEFS"
    };
    static const char *const lic[6] = {
      "MIT - GNBD", "MIT - miniradio", "BSD-2-Clause",
      "LGPL-2.1", "Apache-2.0", "Apache-2.0"
    };
    for(int i = 0; i < 6; i++)
    {
      int y = 52 + i * 14;
      gSprite.fillRoundRect(20, y + 3, 3, 6, 1, COL_ACC);
      gSprite.setTextColor(COL_TEXT, COL_PANEL);
      gSprite.drawString(names[i], 28, y, FONT_TINY);
      gSprite.setTextDatum(TR_DATUM);
      gSprite.setTextColor(COL_MUTED, COL_PANEL);
      gSprite.drawString(lic[i], 300, y, FONT_TINY);
      gSprite.setTextDatum(TL_DATUM);
    }
  }
  else
  {
    gSprite.setTextDatum(TC_DATUM);
    gSprite.setTextColor(COL_ACC2, COL_PANEL);
    gSprite.drawString("ESP32-S3", 160, 34, FONT_SMALL);
    gSprite.setTextDatum(TL_DATUM);
    gSprite.fillRect(78, 48, 164, 1, COL_LINE);

    drawAboutRow(54, "CHIP", String(ESP.getChipModel()));
    drawAboutRow(69, "REVISION", String("v") + String(ESP.getChipRevision()));
    drawAboutRow(84, "CORES", String(ESP.getChipCores()));
    drawAboutRow(99, "CPU", String(ESP.getCpuFreqMHz()) + " MHz");
    drawAboutRow(114, "FLASH", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
    drawAboutRow(129, "PSRAM", String(ESP.getPsramSize() / (1024 * 1024)) + " MB");
  }

  char left[24];
  snprintf(left, sizeof(left), "%s  TURN=PAGE", pg);
  uiHintBar(left, "HOLD=BACK");
  gSprite.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// First-run guide (shown once, replay from Settings > Guide)
// ---------------------------------------------------------------------------

static bool tutorialSeen()
{
  if(!prefs.begin("bootcfg", true, STORAGE_PARTITION)) return false;
  bool v = prefs.getBool("seen", false);
  prefs.end();
  return v;
}

static void markTutorialSeen()
{
  if(!prefs.begin("bootcfg", false, STORAGE_PARTITION)) return;
  prefs.putBool("seen", true);
  prefs.end();
}

// The guide is a short, self-contained tour of the new DBFM dashboard.
struct GuideStep
{
  int target;       // main item index, -1 = no highlight
  const char *title;
  const char *l1;
  const char *l2;
};

static const GuideStep kGuide[] = {
  {-1, "CONTROLS", "Turn = move, click = open", "Hold = back (ring appears)"},
  {0, "APP SLOTS", "app0 / app1 firmware", "Click a slot for boot / hold / info"},
  {2, "OTA", "Source -> slot -> firmware", "Download then flash automatically"},
  {3, "PARTITION", "Info / resize / repair", "Resize only when both slots are empty"},
  {4, "SETTINGS", "Network, files, backup", "Boot menu always and About live here"},
  {0, "CONTROLS", "Turn = move, click = open", "Hold = back (ring appears)"},
};
#define GUIDE_PAGES ((int)(sizeof(kGuide) / sizeof(kGuide[0])))

static void drawGuidePage(int page)
{
  int x, y, w, h;
  mainItemRect(0, x, y, w, h);
  drawMenu(0, x, y - 2, w, h, -1, -1.0f, 0);

  gSprite.fillRectAlpha(0, 0, SCR_W, SCR_H, 150, 0x0000);

  if(page == 0)
  {
    // Large first page: the control scheme at a glance.
    gSprite.fillRoundRect(10, 28, 300, 106, 10, COL_PANEL);
    gSprite.drawRoundRect(10, 28, 300, 106, 10, COL_LINE);
    gSprite.setTextDatum(TC_DATUM);
    gSprite.setTextColor(COL_GOLD, COL_PANEL);
    gSprite.drawString("CONTROLS", 160, 32, FONT_SMALL);

    gSprite.setTextColor(COL_GOLD, COL_PANEL);
    gSprite.drawString("TURN", 60, 56, FONT_LARGE);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
    gSprite.drawString("MOVE", 210, 56, FONT_LARGE);

    gSprite.setTextColor(COL_OK, COL_PANEL);
    gSprite.drawString("CLICK", 60, 82, FONT_LARGE);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
    gSprite.drawString("OPEN", 210, 82, FONT_LARGE);

    gSprite.setTextColor(COL_WARN, COL_PANEL);
    gSprite.drawString("HOLD", 60, 108, FONT_LARGE);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
    gSprite.drawString("BACK", 210, 108, FONT_LARGE);

    gSprite.setTextDatum(TL_DATUM);
    char pg0[24];
    snprintf(pg0, sizeof(pg0), "GUIDE 1/%d", GUIDE_PAGES);
    uiHintBar(pg0, "TURN=NEXT  HOLD=SKIP");
    gSprite.pushSprite(0, 0);
    return;
  }

  int target = kGuide[page].target;
  if(target >= 0)
  {
    mainItemRect(target, x, y, w, h);
    y -= 2;
    for(int k = 0; k < 3; k++)
      gSprite.drawRoundRect(x - k, y - k, w + 2 * k, h + 2 * k, 10 + k, COL_GOLD);
  }

  // Keep the description panel away from the highlighted item: bottom-row
  // help goes above the menu, slot help goes below it.
  const int bx = 12, bw = 296, bh = 44;
  const int by = (target >= 2) ? 40 : 98;
  uiPanel(bx, by, bw, bh, COL_ACC);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_GOLD, COL_PANEL);
  gSprite.drawString(kGuide[page].title, 160, by + 5, FONT_SMALL);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString(kGuide[page].l1, 160, by + 20, FONT_TINY);
  gSprite.setTextColor(COL_MUTED, COL_PANEL);
  gSprite.drawString(kGuide[page].l2, 160, by + 31, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);

  char pg[24];
  snprintf(pg, sizeof(pg), "GUIDE %d/%d", page + 1, GUIDE_PAGES);
  uiHintBar(pg, (page + 1 >= GUIDE_PAGES) ? "TURN=CLOSE  HOLD=SKIP"
                                          : "CLICK=NEXT  HOLD=SKIP");
  gSprite.pushSprite(0, 0);
}

static void runTutorial()
{
  int page = 0;
  drawGuidePage(page);

  // Let the rails settle and drop any boot-time button state.
  uint32_t t0 = millis();
  while(millis() - t0 < 400) delay(10);
  (void)readEncoder();
  while(readButton() > 0) delay(5);
  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      if(d > 0 && page + 1 >= GUIDE_PAGES) break;   // turn again = close
      page = (page + (d > 0 ? 1 : -1) + GUIDE_PAGES) % GUIDE_PAGES;
      drawGuidePage(page);
      continue;
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD) break;              // hold = skip
    if(ev == BTN_CLICK)
    {
      if(page + 1 >= GUIDE_PAGES) break;
      page++;
      drawGuidePage(page);
    }

    delay(10);
  }

  markTutorialSeen();
  Serial.println("tutorial: done");
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

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD) return;             // hold = back
    if(ev == BTN_CLICK)
    {
      page = (page + 1) % ABOUT_PAGES;
      drawAboutPage(page);
    }
    delay(10);
  }
}

