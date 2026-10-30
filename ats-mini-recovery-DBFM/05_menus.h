// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void menuSlotInfo(int index, String &name, String &ver, bool &present)
{
  const esp_partition_t *p = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP,
    static_cast<esp_partition_subtype_t>(
      index == 1 ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0),
    NULL);
  esp_app_desc_t desc;
  present = p && appImageOk(p) && esp_ota_get_partition_description(p, &desc) == ESP_OK;
  if(!present)
  {
    name = "EMPTY";
    ver = "No firmware";
    return;
  }
  String project = String(desc.project_name);
  name = project.length() ? project : "Firmware";
  ver = String("Version ") + desc.version;
}

static void drawSlotCard(int index, bool selected, int lift, int countdown, float progress, uint16_t acc)
{
  int x, y, w, h;
  mainItemRect(index, x, y, w, h);
  y -= lift;

  String name, ver;
  bool present;
  menuSlotInfo(index, name, ver, present);
  bool active = present && ((index == 1) == gBootIsApp1);
  uint16_t bg = present ? (selected ? COL_PANEL2 : COL_PANEL) : COL_CELL;
  uint16_t edge = selected ? acc : COL_LINE;

  gSprite.fillRoundRect(x, y, w, h, 10, bg);
  gSprite.drawRoundRect(x, y, w, h, 10, edge);
  if(selected && progress >= 0.0f)
  {
    int pw = (int)((w - 4) * progress);
    if(pw > 0)
      gSprite.fillRoundRect(x + 2, y + 2, pw, h - 4, 7, shade16(acc, -10));
    gSprite.drawRoundRect(x, y, w, h, 10, acc);
  }
  if(selected)
  {
    gSprite.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 8, acc);
    gSprite.fillRoundRect(x + 10, y + 7, w - 20, 2, 1, acc);
  }

  char idbuf[8];
  snprintf(idbuf, sizeof(idbuf), "APP%d", index);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(acc, bg);
  gSprite.drawString(idbuf, x + 10, y + 8, FONT_TINY);

  const char *state = !present ? "EMPTY" : (active ? "ACTIVE" : "STANDBY");
  uiBadge(x + w - 8, y + 6, state,
          !present ? COL_CELL : (active ? acc : COL_PANEL2),
          !present ? COL_DIM : (active ? COL_BG : acc));

  int maxW = w - 20;
  int tw = gSprite.textWidth(name.c_str(), FONT_LARGE);
  gSprite.setTextColor(present ? COL_TEXT : COL_WARN, bg);
  if(tw > maxW)
  {
    int gap = 24;
    int off = (int)((millis() / 45) % (uint32_t)(tw + gap));
    gSprite.setClipRect(x + 10, y + 26, maxW, 20);
    gSprite.drawString(name.c_str(), x + 10 - off, y + 28, FONT_LARGE);
    gSprite.drawString(name.c_str(), x + 10 - off + tw + gap, y + 28, FONT_LARGE);
    gSprite.clearClipRect();
  }
  else
    gSprite.drawString(name.c_str(), x + 10, y + 28, FONT_LARGE);

  gSprite.setTextColor(present ? COL_MUTED : COL_WARN, bg);
  gSprite.drawString(ver.c_str(), x + 10, y + 52, FONT_TINY);

  if(countdown >= 0 && selected)
  {
    char cd[12];
    snprintf(cd, sizeof(cd), "AUTO %d", countdown);
    gSprite.setTextDatum(TR_DATUM);
    gSprite.setTextColor(COL_ACC2, bg);
    gSprite.drawString(cd, x + w - 10, y + 52, FONT_TINY);
    gSprite.setTextDatum(TL_DATUM);
    if(countdown > 0)
      gSprite.fillRoundRect(x + 10, y + h - 6, (w - 20) * (5 - countdown) / 5, 2, 1, acc);
  }
}

static bool menuNeedsScroll()
{
  for(int i = 0; i < 2; i++)
  {
    String name, ver;
    bool present;
    menuSlotInfo(i, name, ver, present);
    if(gSprite.textWidth(name.c_str(), FONT_LARGE) > 128) return true;
  }
  return false;
}

static void drawMenuTile(int index, bool selected, int lift, uint16_t acc)
{
  int x, y, w, h;
  mainItemRect(index, x, y, w, h);
  y -= lift;

  gSprite.fillRoundRect(x, y, w, h, 8, selected ? COL_PANEL2 : COL_PANEL);
  gSprite.drawRoundRect(x, y, w, h, 8, selected ? acc : COL_LINE);
  if(selected)
    gSprite.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 6, acc);

  uiMenuIconSmall(index - 2, x + 19, y + h / 2, acc);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(selected ? COL_TEXT : COL_MUTED,
                       selected ? COL_PANEL2 : COL_PANEL);
  gSprite.drawString(menu[index], x + 32, y + 16, FONT_TINY);
}

static void drawMenuFooter()
{
  gSprite.fillRect(0, 146, SCR_W, 24, COL_BG);
  gSprite.fillRect(0, 146, SCR_W, 1, COL_LINE);
  String ip = currentIp();
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString(ip.length() ? ip.c_str() : "USB", 8, 152, FONT_TINY);

  // Control hints use their own colours: amber turn, teal click, coral hold.
  gSprite.drawArc(188, 151, 5, 3, 40, 320, COL_GOLD);
  gSprite.setTextColor(COL_GOLD, COL_BG);
  gSprite.drawString("TURN", 177, 160, FONT_TINY);

  gSprite.fillCircle(236, 151, 3, COL_OK);
  gSprite.setTextColor(COL_OK, COL_BG);
  gSprite.drawString("CLICK", 221, 160, FONT_TINY);

  gSprite.drawCircle(286, 151, 5, COL_WARN);
  gSprite.setTextColor(COL_WARN, COL_BG);
  gSprite.drawString("HOLD", 273, 160, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
}

// ringX/ringY/ringW/ringH are the animated selection outline; countdown >= 0
// shows the Boot-menu-always timer on the selected slot.
static void drawMenu(int selected, float ringX, float ringY,
                     float ringW, float ringH, int countdown, float progress, float press)
{
  static const uint16_t itemAcc[5] = {
    COL_ACC, COL_GOLD, COL_GOLD, COL_WARN, COL_OK
  };
  gSprite.fillScreen(COL_BG);
  drawTopBar();

  for(int i = 0; i < (int)MENU_COUNT; i++)
  {
    bool sel = (i == selected);
    int lift = sel ? 2 : 0;
    int inset = (sel && press > 0.01f) ? (int)(press * 2.0f + 0.5f) : 0;
    if(i < 2)
    {
      int x, y, w, h;
      mainItemRect(i, x, y, w, h);
      // The press squash is folded into the slot lift.
      (void)x; (void)y; (void)w; (void)h;
      drawSlotCard(i, sel, lift + inset, countdown, progress, itemAcc[i]);
    }
    else
      drawMenuTile(i, sel, lift, itemAcc[i]);
  }

  gSprite.drawRoundRect((int)ringX, (int)ringY, (int)ringW, (int)ringH,
                        10, itemAcc[selected]);
  gSprite.drawRoundRect((int)ringX + 2, (int)ringY + 2,
                        (int)ringW - 4, (int)ringH - 4, 8, itemAcc[selected]);

  drawMenuFooter();
  gSprite.pushSprite(0, 0);
}

static void drawWifiMenu(int selected)
{
  gSprite.fillScreen(COL_BG);
  drawHeader("WI-FI");

  bool sta = (WiFi.status() == WL_CONNECTED);
  bool ap = apModeActive;
  const char *mode = sta ? (ap ? "AP+STA" : "STA") : (ap ? "AP" : "OFF");

  gSprite.fillRoundRect(8, 24, 304, 30, 8, COL_PANEL);
  gSprite.drawRoundRect(8, 24, 304, 30, 8, COL_LINE);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(sta || ap ? COL_TEXT : COL_MUTED, COL_PANEL);
  gSprite.drawString(mode, 16, 28, FONT_SMALL);
  gSprite.setTextColor(COL_MUTED, COL_PANEL);
  if(sta)
  {
    gSprite.drawString(("SSID " + WiFi.SSID()).c_str(), 16, 40, FONT_TINY);
    gSprite.setTextDatum(TR_DATUM);
    gSprite.drawString(WiFi.localIP().toString().c_str(), 304, 40, FONT_TINY);
    gSprite.setTextDatum(TL_DATUM);
  }
  else if(ap)
  {
    gSprite.drawString("ats-recovery / 12345678", 16, 40, FONT_TINY);
    gSprite.setTextDatum(TR_DATUM);
    gSprite.drawString(WiFi.softAPIP().toString().c_str(), 304, 40, FONT_TINY);
    gSprite.setTextDatum(TL_DATUM);
  }
  else gSprite.drawString("Wi-Fi is off", 16, 40, FONT_TINY);

  static const char *const items[4] = {"OFF", "AP", "CONNECT", "SCAN"};
  static const uint16_t accs[4] = {COL_MUTED, COL_GOLD, COL_OK, COL_ACC};
  for(int i = 0; i < 4; i++)
  {
    int y = 60 + i * 21;
    bool on = (i == selected);
    gSprite.fillRoundRect(8, y, 304, 19, 6, on ? COL_PANEL2 : COL_PANEL);
    gSprite.drawRoundRect(8, y, 304, 19, 6, on ? COL_TEXT : COL_LINE);
    if(on) gSprite.fillRoundRect(13, y + 4, 3, 11, 1, accs[i]);
    gSprite.setTextDatum(TL_DATUM);
    gSprite.setTextColor(on ? COL_TEXT : COL_MUTED,
                         on ? COL_PANEL2 : COL_PANEL);
    gSprite.drawString(items[i], 22, y + 2, FONT_SMALL);
  }

  uiHintBar("TURN=MOVE  CLICK=OPEN", "HOLD=BACK");
  gSprite.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// Settings submenu (WiFi / About / Brightness)
// ---------------------------------------------------------------------------

static void drawSettingsMenu(int selected)
{
  static const char *shortNames[SETTINGS_COUNT] = {
    "Network", "Partition", "Erase", "Bright", "Files",
    "Backup", "Guide", "Callsign", "Boot", "About"
  };
  static const int icons[SETTINGS_COUNT] = {
    ICON_WIFI, ICON_DB, ICON_TRASH, ICON_BRIGHT, ICON_FOLDER,
    ICON_DB, ICON_BOOT, ICON_CALLSIGN, ICON_HOLD, ICON_ABOUT
  };
  static const uint16_t accs[SETTINGS_COUNT] = {
    COL_OK, COL_WARN, COL_WARN, COL_GOLD, COL_ACC,
    COL_ACC2, COL_OK, COL_VIOLET, COL_GOLD, COL_ROSE
  };

  gSprite.fillScreen(COL_BG);
  drawHeader("SETTINGS");

  for(int i = 0; i < (int)SETTINGS_COUNT; i++)
  {
    int col = i % 5;
    int row = i / 5;
    int x = 4 + col * 63;
    int y = 26 + row * 60;
    bool on = (i == selected);
    uint16_t acc = accs[i];

    gSprite.fillRoundRect(x, y, 60, 54, 7, on ? COL_PANEL2 : COL_PANEL);
    gSprite.drawRoundRect(x, y, 60, 54, 7, on ? acc : COL_LINE);
    uiMenuIcon(icons[i], x + 30, y + 18, acc);
    gSprite.setTextDatum(TC_DATUM);
    gSprite.setTextColor(on ? acc : COL_TEXT,
                         on ? COL_PANEL2 : COL_PANEL);
    gSprite.drawString(shortNames[i], x + 30, y + 41, FONT_TINY);
    gSprite.setTextDatum(TL_DATUM);

    if(i == 8 && gBootMenuAlways)
      gSprite.fillCircle(x + 52, y + 8, 2, COL_OK);
  }

  uiHintBar("TURN=MOVE  CLICK=OPEN", "HOLD=BACK");
  gSprite.pushSprite(0, 0);
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
  uiHintBar(range, "TURN=ADJUST  CLICK=SAVE  HOLD=BACK");
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

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_CLICK || ev == BTN_HOLD)
    {
      saveBrightness(b);
      return;
    }
    delay(10);
  }
}

// Declared here because the Callsign editor (below) runs before the shared
// keypad text input is defined later in the sketch.
static bool runTextInput(const char *title, const String &initial, String &out,
                         const char *suffix, bool allowEmpty);

// Settings > Callsign: edit the boot callsign, then choose whether the splash
// shows the greeting line. An empty callsign restores the default branding.
static void runCallsign()
{
  String value;
  if(!runTextInput("CALLSIGN", gCallsign, value, nullptr, true)) return;   // X

  bool greet = gGreeting;
  if(value.length())
  {
    static const char *gLabels[] = {"Greeting ON", "Greeting OFF"};
    static const int gIcons[] = {ICON_WEB, ICON_HOLD};
    static const uint16_t gAccs[] = {COL_OK, COL_MUTED};
    int g = runTileChoice("GREETING", "TURN=MOVE", "CLICK=OPEN  HOLD=BACK",
                          gLabels, gIcons, gAccs, 2);
    if(g < 0) return;                                                     // back
    greet = (g == 0);
  }

  gCallsign = value;
  gGreeting = greet;
  saveCallsign(value);
  saveGreeting(greet);
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

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD) return;                                  // hold = back
    if(ev == BTN_CLICK)
    {
      if(selected == 0) runWifiMenu();
      else if(selected == 1) runPartitionManager();
      else if(selected == 2) runErase();
      else if(selected == 3) runBrightness();
      else if(selected == 4) runLittleFsManager();
      else if(selected == 5) runBackupMenu();
      else if(selected == 6) runTutorial();
      else if(selected == 7) runCallsign();
      else if(selected == 8)
      {
        gBootMenuAlways = !gBootMenuAlways;
        saveBootMenuAlways(gBootMenuAlways);
        uiToast(gBootMenuAlways ? "Boot menu always: ON" : "Boot menu always: OFF");
      }
      else runAbout();

      drawSettingsMenu(selected);
    }
    delay(10);
  }
}

static void drawUpdateProgress(int percent, const char *status, const char *detail = nullptr)
{
  gSprite.fillScreen(COL_BG);
  drawTopBar(nullptr, false);

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

