// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// WiFi Setting: scan (4/page) + encoder keypad + connect/save
// ---------------------------------------------------------------------------

enum KbType : uint8_t { KB_CHAR = 0, KB_DEL, KB_SHIFT, KB_SYM, KB_SPACE, KB_OK };

struct KbKey
{
  int16_t x, y, w, h;
  char ch;
  uint8_t type;
};

// miniradio keypad: four 10-key rows plus DEL / aA / !@# / SPC / OK.
static const char *KP_ROWS[4] = {
  "1234567890",
  "qwertyuiop",
  "asdfghjkl-",
  "zxcvbnm._/"
};
static const char *KP_SYMS[4] = {
  "!@#$%^&*()",
  "-_=+[]{}\\|;",
  ":;'\"<>?,./",
  "~`|\\-_=+{}"
};

static KbKey kbKeys[KB_MAX_KEYS];
static int kbKeyCount = 0;
static int kbCursor = 0;
static bool kbShift = false;
static bool kbSym = false;

static char kbChar(int idx)
{
  if(idx < 0 || idx >= 40) return 0;
  if(kbSym) return KP_SYMS[idx / 10][idx % 10];
  char c = KP_ROWS[idx / 10][idx % 10];
  if(kbShift && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  return c;
}

static void kbAddKey(int16_t x, int16_t y, int16_t w, int16_t h, char ch, uint8_t type)
{
  if(kbKeyCount >= KB_MAX_KEYS) return;
  kbKeys[kbKeyCount++] = {x, y, w, h, ch, type};
}

static void kbBuild(int contentBottom)
{
  kbKeyCount = 0;
  if(kbCursor >= KB_MAX_KEYS) kbCursor = 0;

  for(int row = 0; row < 4; row++)
  {
    for(int col = 0; col < 10; col++)
      kbAddKey(6 + col * 31, contentBottom + row * 20, 29, 18, 0, KB_CHAR);
  }

  int fy = contentBottom + 4 * 20 + 2;
  kbAddKey(6,   fy, 58, 17, 0, KB_DEL);
  kbAddKey(66,  fy, 58, 17, 0, KB_SHIFT);
  kbAddKey(126, fy, 58, 17, 0, KB_SYM);
  kbAddKey(186, fy, 58, 17, ' ', KB_SPACE);
  kbAddKey(246, fy, 68, 17, 0, KB_OK);

  if(kbCursor >= kbKeyCount) kbCursor = kbKeyCount - 1;
  if(kbCursor < 0) kbCursor = 0;
}

static void kbKeyLabel(int idx, char *buf, size_t len)
{
  const KbKey &k = kbKeys[idx];
  if(k.type == KB_CHAR)
  {
    char c = kbChar(idx);
    snprintf(buf, len, "%c", c ? c : ' ');
  }
  else if(k.type == KB_DEL) snprintf(buf, len, "DEL");
  else if(k.type == KB_SHIFT) snprintf(buf, len, kbShift ? "A" : "aA");
  else if(k.type == KB_SYM) snprintf(buf, len, kbSym ? "abc" : "!@#");
  else if(k.type == KB_SPACE) snprintf(buf, len, "SPC");
  else snprintf(buf, len, "OK");
}

static void drawKb()
{
  for(int i = 0; i < kbKeyCount; i++)
  {
    const KbKey &k = kbKeys[i];
    bool sel = (i == kbCursor);
    uint16_t bg = sel ? COL_KEYSEL : COL_KEY;
    uint16_t fg = sel ? COL_TEXT : COL_MUTED;
    gSprite.fillRoundRect(k.x, k.y, k.w, k.h, 5, bg);
    if(sel) gSprite.drawRoundRect(k.x, k.y, k.w, k.h, 5, COL_ACC);
    gSprite.setTextDatum(MC_DATUM);
    gSprite.setTextColor(fg, bg);
    char b[4];
    kbKeyLabel(i, b, sizeof(b));
    gSprite.drawString(b, k.x + k.w / 2, k.y + k.h / 2, FONT_TINY);
  }
  gSprite.setTextDatum(TL_DATUM);
}

static void drawLockSmall(int x, int y, uint16_t c)
{
  gSprite.drawRect(x + 2, y, 5, 4, c);
  gSprite.fillRect(x, y + 3, 9, 6, c);
}

static void drawWifiScanning()
{
  gSprite.fillScreen(COL_BG);
  drawHeader("WI-FI SCAN");
  gSprite.fillRoundRect(24, 54, 272, 40, 10, COL_PANEL);
  gSprite.drawRoundRect(24, 54, 272, 40, 10, COL_LINE);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString("SCANNING", 160, 64, FONT_LARGE);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("searching networks...", 160, 116, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
}

static void drawWifiSetting(int *nets, int netCount, int selected,
                            const String &ssid, const String &password,
                            bool kbOpen)
{
  gSprite.fillScreen(COL_BG);
  drawHeader("WIFI");

  if(kbOpen)
  {
    gSprite.fillRoundRect(8, 22, 304, 20, 8, COL_PANEL);
    gSprite.drawRoundRect(8, 22, 304, 20, 8, COL_ACC);
    gSprite.setTextDatum(TL_DATUM);
    gSprite.setTextColor(COL_TEXT, COL_PANEL);
    String shown = password;
    if(shown.length() > 30) shown = shown.substring(shown.length() - 30);
    gSprite.drawString((shown + "_").c_str(), 14, 27, FONT_SMALL);

    String sub = "SSID " + ssid;
    if(sub.length() > 38) sub = sub.substring(0, 37) + ".";
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString(sub.c_str(), 8, 43, FONT_TINY);

    kbBuild(60);
    drawKb();
  }
  else
  {
    if(netCount == 0)
    {
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString("No networks found", 10, 38, FONT_SMALL);
    }
    else
    {
      int page = selected / WIFI_PAGE_SIZE;
      int start = page * WIFI_PAGE_SIZE;
      int end = start + WIFI_PAGE_SIZE;
      if(end > netCount) end = netCount;

      for(int i = start; i < end; i++)
      {
        int y = 26 + (i - start) * 22;
        bool on = (i == selected);
        gSprite.fillRoundRect(6, y, 308, 20, 6, on ? COL_PANEL2 : COL_PANEL);
        gSprite.drawRoundRect(6, y, 308, 20, 6, on ? COL_ACC : COL_LINE);
        if(on) gSprite.fillRoundRect(11, y + 5, 3, 10, 1, COL_ACC);

        String name = WiFi.SSID(i);
        if(name.length() > 24) name = name.substring(0, 23) + ".";
        gSprite.setTextDatum(TL_DATUM);
        gSprite.setTextColor(on ? COL_TEXT : COL_MUTED,
                             on ? COL_PANEL2 : COL_PANEL);
        gSprite.drawString(name.c_str(), 18, y + 3, FONT_SMALL);

        char rs[8];
        snprintf(rs, sizeof(rs), "%d", WiFi.RSSI(i));
        gSprite.setTextDatum(TR_DATUM);
        gSprite.setTextColor(on ? COL_ACC : COL_MUTED,
                             on ? COL_PANEL2 : COL_PANEL);
        gSprite.drawString(rs, 278, y + 6, FONT_TINY);
        if(WiFi.encryptionType(i) != WIFI_AUTH_OPEN)
          drawLockSmall(290, y + 7, on ? COL_ACC : COL_MUTED);
        gSprite.setTextDatum(TL_DATUM);
      }
    }

    int pages = (netCount + WIFI_PAGE_SIZE - 1) / WIFI_PAGE_SIZE;
    if(pages < 1) pages = 1;
    char pageBuf[20];
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", selected / WIFI_PAGE_SIZE + 1, pages);
    uiHintBar(pageBuf, "CLICK=OPEN  HOLD=BACK");
  }

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
  drawWifiScanning();

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

      BtnEvent ev = readButtonEvent();
      if(ev == BTN_HOLD)
      {
        WiFi.scanDelete();
        return;
      }
      if(ev == BTN_CLICK)
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
          kbShift = false;
          kbSym = false;
          kbCursor = 0;
          redraw();
        }
      }
      delay(10);
    }
    else
    {
      int8_t d = readEncoder(false);
      if(kbHandleEncoder(d)) redraw();

      BtnEvent ev = readButtonEvent();
      if(ev == BTN_HOLD)
      {
        kbOpen = false;                 // hold = back to the scan list
        redraw();
        continue;
      }
      if(ev == BTN_CLICK)
      {
        const KbKey &k = kbKeys[kbCursor];
        if(k.type == KB_OK)
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
        else if(k.type == KB_CHAR)
        {
          char c = kbChar(kbCursor);
          if(c && password.length() < 63) password += c;
          redraw();
        }
        else if(k.type == KB_DEL)
        {
          if(password.length() > 0) password.remove(password.length() - 1);
          redraw();
        }
        else if(k.type == KB_SHIFT)
        {
          kbShift = !kbShift;
          redraw();
        }
        else if(k.type == KB_SYM)
        {
          kbSym = !kbSym;
          redraw();
        }
        else if(k.type == KB_SPACE)
        {
          if(password.length() < 63) password += ' ';
          redraw();
        }
      }
      delay(10);
    }
  }
}

