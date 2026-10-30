// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// WiFi helpers
// ---------------------------------------------------------------------------

static void applyTxPower()
{
  WiFi.setTxPower(WIFI_POWER_13dBm);
}

// miniradio signal-strength bars: 3/5/7/9 px high, RSSI thresholds.
static void drawNetIcon()
{
  gSprite.fillRect(232, 2, 34, 16, COL_BG);

  int bars = 0;
  uint16_t color = COL_DIM;
  if(gWifiConnecting)
  {
    bars = (int)((millis() / 160) % 4) + 1;
    color = COL_GOLD;
  }
  else if(apModeActive)
  {
    bars = 4;
    color = COL_AP;
  }
  else if(WiFi.status() == WL_CONNECTED)
  {
    int rssi = WiFi.RSSI();
    bars = (rssi >= -55) ? 4 : (rssi >= -65) ? 3 : (rssi >= -75) ? 2 : 1;
    color = (bars >= 3) ? COL_OK : COL_GOLD;
  }
  else
  {
    bars = 1;
    color = COL_DIM;
  }

  static const uint8_t heights[4] = {3, 5, 7, 9};
  for(int i = 0; i < bars; i++)
    gSprite.fillRect(235 + i * 3, 14 - heights[i], 2, heights[i], color);
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
  if(gBattHidden) return;
  // Clear the voltage + pill area first: progress screens do not repaint the
  // header, so a shorter value would otherwise leave ghost digits behind.
  gSprite.fillRect(248, 3, 74, 14, COL_BG);

  float v = readBatteryVolts();
  uint16_t col = (v >= 3.80f) ? COL_OK : ((v >= 3.50f) ? COL_GOLD : COL_WARN);
  char buf[8];
  snprintf(buf, sizeof(buf), "%.2fV", v);

  // Voltage text sits left of the battery pill.
  gSprite.setTextDatum(TR_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString(buf, rightX - 4, 6, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);

  int pct = (int)((v - 3.30f) * 100.0f / 0.90f);
  if(pct < 0) pct = 0;
  if(pct > 100) pct = 100;

  gSprite.drawRoundRect(292, 5, 24, 10, 3, COL_MUTED);
  gSprite.fillRect(317, 8, 2, 4, COL_MUTED);
  int fw = 20 * pct / 100;
  if(fw > 0) gSprite.fillRoundRect(294, 7, fw, 6, 2, col);

  gBattRightX = rightX;
  gBattDrawnAt = millis();
  gBattValid = true;
}

// The header voltage refreshes once a second. Skipped while a flash job owns
// the sprite.
static void batteryTick()
{
  if(!gBattValid || gFlashBusy || gUiQuiet || gBattHidden) return;
  if(millis() - gBattDrawnAt < 1000) return;
  drawBattery(gBattRightX);
  gSprite.pushSprite(0, 0);
}

// Defined early because the top bar shows the callsign instead of DBFM.
static String gCallsign = "";

// miniradio-style status bar: callsign/DBFM, optional title, signal bars,
// voltage.
static void drawTopBar(const char *title = nullptr, bool showBattery = true)
{
  gSprite.fillRect(0, 0, SCR_W, 20, COL_BG);
  gSprite.fillRoundRect(5, 7, 2, 8, 1, COL_ACC);

  String brand = gCallsign.length() ? gCallsign : String("DBFM");
  if(brand.length() > 9) brand = brand.substring(0, 8) + ".";
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_BG);
  gSprite.drawString(brand.c_str(), 12, 6, FONT_TINY);

  if(title)
  {
    gSprite.setTextDatum(TC_DATUM);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString(title, 160, 6, FONT_TINY);
    gSprite.setTextDatum(TL_DATUM);
  }

  drawNetIcon();
  gBattHidden = !showBattery;
  if(showBattery) drawBattery(286);
  gSprite.fillRect(0, 19, SCR_W, 1, COL_LINE);
}

static void drawHeader(const char *title, const char *right = nullptr)
{
  (void)right;
  drawTopBar(title);
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

// Optional HTTP Basic Auth for the web UI, kept with the other recovery
// settings. It can be switched off, and stays off while no password is set.
static bool gWebAuthOn = false;
static String gWebUser = "admin";
static String gWebPass;

static void loadWebAuth()
{
  if(!prefs.begin("webcfg", true, STORAGE_PARTITION)) return;
  gWebAuthOn = prefs.getBool("on", false);
  gWebUser = prefs.getString("user", "admin");
  gWebPass = prefs.getString("pass", "");
  prefs.end();
  if(gWebUser.length() == 0) gWebUser = "admin";
  if(gWebPass.length() == 0) gWebAuthOn = false;
}

static void saveWebAuth()
{
  if(!prefs.begin("webcfg", false, STORAGE_PARTITION)) return;
  prefs.putBool("on", gWebAuthOn);
  prefs.putString("user", gWebUser);
  prefs.putString("pass", gWebPass);
  prefs.end();
}

static bool webAuthOk()
{
  if(!gWebAuthOn) return true;
  return server.authenticate(gWebUser.c_str(), gWebPass.c_str());
}

// Sends the Basic Auth challenge when the request is not allowed.
static bool webAuthed()
{
  if(webAuthOk()) return true;
  server.requestAuthentication(BASIC_AUTH, "DBFM");
  return false;
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

// Firmware provenance recorded when a slot is flashed: original file name and
// install time (0 when no clock was available).
static String loadFwFile(uint8_t slot)
{
  if(!prefs.begin("fwmeta", true, STORAGE_PARTITION)) return "";
  String v = prefs.getString(slot ? "f1" : "f0", "");
  prefs.end();
  return v;
}

static uint32_t loadFwTime(uint8_t slot)
{
  if(!prefs.begin("fwmeta", true, STORAGE_PARTITION)) return 0;
  uint32_t v = prefs.getUInt(slot ? "t1" : "t0", 0);
  prefs.end();
  return v;
}

static String loadFwVer(uint8_t slot)
{
  if(!prefs.begin("fwmeta", true, STORAGE_PARTITION)) return "";
  String v = prefs.getString(slot ? "v1" : "v0", "");
  prefs.end();
  return v;
}

static void saveFwMeta(uint8_t slot, const String &file, const String &ver)
{
  if(!prefs.begin("fwmeta", false, STORAGE_PARTITION)) return;
  prefs.putString(slot ? "f1" : "f0", file);
  prefs.putString(slot ? "v1" : "v0", ver);
  time_t now = time(nullptr);
  prefs.putUInt(slot ? "t1" : "t0",
                (now > 1600000000) ? (uint32_t)now : 0);
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
// Callsign (Settings > Callsign): the boot splash title and the recovery menu
// title. Stored in the same uicfg namespace as the brightness. Cached in RAM so
// the menu does not hit NVS on every redraw.
// ---------------------------------------------------------------------------

#define CALLSIGN_MAX 16

static bool   gGreeting = true;

static String loadCallsign()
{
  if(!prefs.begin("uicfg", true, STORAGE_PARTITION)) return "";
  String v = prefs.getString("call", "");
  prefs.end();
  if(v.length() > CALLSIGN_MAX) v = v.substring(0, CALLSIGN_MAX);
  return v;
}

static void saveCallsign(const String &v)
{
  if(!prefs.begin("uicfg", false, STORAGE_PARTITION)) return;
  prefs.putString("call", v);
  prefs.end();
}

static bool loadGreeting()
{
  if(!prefs.begin("uicfg", true, STORAGE_PARTITION)) return true;
  bool v = prefs.getBool("greet", true);
  prefs.end();
  return v;
}

static void saveGreeting(bool v)
{
  if(!prefs.begin("uicfg", false, STORAGE_PARTITION)) return;
  prefs.putBool("greet", v);
  prefs.end();
}

static bool gBootMenuAlways = false;

static bool loadBootMenuAlways()
{
  if(!prefs.begin("uicfg", true, STORAGE_PARTITION)) return false;
  bool v = prefs.getBool("bootmenu", false);
  prefs.end();
  return v;
}

static void saveBootMenuAlways(bool v)
{
  if(!prefs.begin("uicfg", false, STORAGE_PARTITION)) return;
  prefs.putBool("bootmenu", v);
  prefs.end();
}

static void loadUiIdentity()
{
  gCallsign = loadCallsign();
  gGreeting = loadGreeting();
  gBootMenuAlways = loadBootMenuAlways();
}

// Only printable ASCII is accepted, so a non-Latin (CJK/Cyrillic) paste can
// never reach the Latin-only fonts and render as blanks.
static bool asciiPrintable(const String &s)
{
  for(size_t i = 0; i < s.length(); i++)
  {
    uint8_t c = (uint8_t)s[i];
    if(c < 0x20 || c > 0x7E) return false;
  }
  return true;
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

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_CLICK) return yes;            // click = confirm focused button
    if(ev == BTN_HOLD) return false;           // hold = cancel / back
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

