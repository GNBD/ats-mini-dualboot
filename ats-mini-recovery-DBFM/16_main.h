// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

static void mainItemCenter(int index, int &x, int &y, int &w, int &h)
{
  mainItemRect(index, x, y, w, h);
}

// initialSelected < 0 = last booted slot (Boot menu always).
// idleBoot: 5 s without input boots the last slot. Any encoder/button input
// cancels the countdown and keeps the manager open.
static void runRecoveryMenu(int initialSelected = -1, bool idleBoot = false)
{
  int selected = initialSelected;
  if(selected < 0 || selected >= (int)MENU_COUNT)
    selected = gBootIsApp1 ? 1 : 0;

  UiSpring rx, ry, rw, rh;
  int x, y, w, h;
  mainItemRect(selected, x, y, w, h);
  rx.x = x; ry.x = y - 2; rw.x = w; rh.x = h;

  bool idleActive = idleBoot;
  uint32_t idleStart = millis();
  uint32_t lastFrame = millis();
  uint32_t nextFrame = millis();
  int lastCountdown = -1;
  float press = 0;
  drawMenu(selected, rx.x, ry.x, rw.x, rh.x,
           idleActive ? 5 : -1, idleActive ? 0.0f : -1.0f, press);

  static uint32_t lastWifiAnim = 0;
  while(true)
  {
    if(gUiRefresh)
    {
      gUiRefresh = false;
      drawMenu(selected, rx.x, ry.x, rw.x, rh.x, -1, -1.0f, press);
    }

    uint32_t now = millis();
    float dt = (now - lastFrame) / 1000.0f;
    bool dirty = false;

    if(gWifiConnecting && now - lastWifiAnim > 150)
    {
      drawNetIcon();
      gSprite.pushSprite(0, 0);
      lastWifiAnim = now;
    }

    int8_t dir = readEncoder();
    if(dir)
    {
      selected = (selected + dir) % (int)MENU_COUNT;
      if(selected < 0) selected += MENU_COUNT;
      if(idleActive)
      {
        idleActive = false;
        uiToast("Auto boot cancelled");
      }
      dirty = true;
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD)
    {
      if(idleActive)
      {
        idleActive = false;
        uiToast("Auto boot cancelled");
      }
      else
      {
        uiToast("CLICK=OPEN  HOLD=BACK");
      }
      dirty = true;
    }
    else if(ev == BTN_CLICK)
    {
      if(idleActive)
      {
        idleActive = false;
        uiToast("Auto boot cancelled");
      }

      for(int f = 1; f <= 3; f++)
      {
        press = (f == 1) ? 0.5f : ((f == 2) ? 1.0f : 0.4f);
        drawMenu(selected, rx.x, ry.x, rw.x, rh.x, -1, -1.0f, press);
        delay(16);
      }
      press = 0;

      if(selected < 2)
      {
        String nm, vr;
        bool present;
        menuSlotInfo(selected, nm, vr, present);
        if(!present)
        {
          uiToast("EMPTY");
          drawMenu(selected, rx.x, ry.x, rw.x, rh.x, -1, -1.0f, 0);
          idleStart = millis();
          continue;
        }
      }

      switch(selected)
      {
        case 0: runBootModeMenu(false); break;
        case 1: runBootModeMenu(true); break;
        case 2: runFirmwareUpdate(); break;
        case 3: runPartitionManager(); break;
        case 4: runSettingsMenu(); break;
      }

      mainItemRect(selected, x, y, w, h);
      rx.x = x; ry.x = y - 2; rw.x = w; rh.x = h;
      rx.v = ry.v = rw.v = rh.v = 0;
      idleStart = millis();
      drawMenu(selected, rx.x, ry.x, rw.x, rh.x, -1, -1.0f, 0);
      continue;
    }

    if(idleActive && now - idleStart >= 5000)
    {
      Serial.println("boot menu idle -> last slot");
      bool ok = gBootIsApp1 ? bootToApp1() : bootToApp0();
      if(ok) return;
      idleActive = false;
      drawMenu(selected, rx.x, ry.x, rw.x, rh.x, -1, -1.0f, 0);
    }

    int countdown = -1;
    float bootProgress = -1.0f;
    if(idleActive)
    {
      int left = 5 - (int)((now - idleStart) / 1000);
      if(left < 1) left = 1;
      countdown = left;
      bootProgress = (now - idleStart) / 5000.0f;
      if(bootProgress > 1.0f) bootProgress = 1.0f;
      dirty = true;                       // animate the whole-slot loading bar
    }
    if(countdown != lastCountdown)
    {
      lastCountdown = countdown;
      dirty = true;
    }

    if(menuNeedsScroll()) dirty = true;

    if(now >= nextFrame)
    {
      mainItemRect(selected, x, y, w, h);
      float nx = uiSpringStep(rx, (float)x, 340.0f, 28.0f, dt);
      float ny = uiSpringStep(ry, (float)(y - 2), 340.0f, 28.0f, dt);
      float nw = uiSpringStep(rw, (float)w, 340.0f, 28.0f, dt);
      float nh = uiSpringStep(rh, (float)h, 340.0f, 28.0f, dt);
      if(fabs(nx - x) > 0.3f || fabs(ny - (y - 2)) > 0.3f ||
         fabs(nw - w) > 0.3f || fabs(nh - h) > 0.3f) dirty = true;

      if(dirty || press > 0.01f)
      {
        drawMenu(selected, rx.x, ry.x, rw.x, rh.x,
                 idleActive ? countdown : -1, bootProgress, press);
      }
      nextFrame = now + 42;                 // 24 fps
      lastFrame = now;
    }

    delay(2);
  }
}

// WiFi and the web server run in the background, so the boot manager opens
// immediately instead of waiting for a station connection.
static void recoveryNetTask(void *)
{
  String ssid, pass;

  WiFi.mode(WIFI_MODE_NULL);
  WiFi.onEvent([](WiFiEvent_t e){ applyTxPower(); }, ARDUINO_EVENT_WIFI_STA_START);
  WiFi.onEvent([](WiFiEvent_t e){ applyTxPower(); }, ARDUINO_EVENT_WIFI_AP_START);
  WiFi.mode(WIFI_STA);
  applyTxPower();
  WiFi.setSleep(false);

  gWifiConnecting = true;
  if(loadWifiCredentials(ssid, pass))
  {
    Serial.printf("net: STA connect to %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while(WiFi.status() != WL_CONNECTED && (millis() - t0) < BOOT_CONNECT_TIMEOUT)
      delay(50);
  }
  gWifiConnecting = false;

  if(WiFi.status() == WL_CONNECTED)
  {
    apIP = WiFi.localIP().toString();
    apModeActive = false;
    Serial.printf("net: STA ok %s\n", apIP.c_str());
  }
  else if(gWebEnabled)
  {
    Serial.println("net: no STA -> AP");
    wifiStartAp();
  }

  if(gWebEnabled) startWebBackground();
  gUiRefresh = true;
  vTaskDelete(NULL);
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

  pinMode(ENCODER_PUSH_BUTTON, INPUT_PULLUP);
  if(encoder_init((gpio_num_t)ENCODER_PIN_A, (gpio_num_t)ENCODER_PIN_B) != ESP_OK)
    Serial.println("encoder init failed");

  ledcAttach(PIN_LCD_BL, 16000, 8);
  ledcWrite(PIN_LCD_BL, 0);

  tft.init();
  tft.setRotation(3);
  gSprite.setPsram(true);
  gSprite.createSprite(SCR_W, SCR_H);
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  loadUiIdentity();

  // Boot menu always: no splash, the menu is drawn as soon as the display is
  // ready. Self checks still run and only pop up on failure.
  if(!gBootMenuAlways)
  {
  gSprite.setTextDatum(TC_DATUM);
  if(gCallsign.length())
  {
    // Callsign replaces the brand on the big line; the brand moves to the
    // bottom line together with the version.
    gSprite.setTextColor(COL_GOLD, COL_BG);
    gSprite.drawString(gCallsign, 160, 46, FONT_LARGE);
    if(gGreeting)
    {
      gSprite.setTextColor(COL_ACC, COL_BG);
      gSprite.drawString("WELCOME", 160, 78, FONT_SMALL);
    }
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("github.com/GNBD/ats-mini-dualboot", 160, 104, FONT_TINY);
    {
      char slotLine[40];
      snprintf(slotLine, sizeof(slotLine), "Current slot: %s", gBootSlot);
      gSprite.setTextColor(COL_GOLD, COL_BG);
      gSprite.drawString(slotLine, 160, 126, FONT_SMALL);
    }
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("DBFM v" RECOVERY_VERSION " (DES)", 160, 150, FONT_TINY);
  }
  else
  {
    gSprite.setTextColor(COL_GOLD, COL_BG);
    gSprite.drawString("DBFM", 160, 50, FONT_LARGE);
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
  }
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
  }
  applyBrightness(loadBrightness());

  // Quick bootloader + partition check while the splash is up; silent on
  // pass, a warning popup only when something is wrong.
  flashSelfCheck();

  Serial.println("step: 1s encoder check");
  bool encoderPressed = gBootMenuAlways;
  unsigned long start = millis();
  while(!gBootMenuAlways && millis() - start < 1000)
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

  // Show the guide once, before the first auto boot.
  if(!tutorialSeen()) runTutorial();

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

  gWebEnabled = true;
  loadWebAuth();

  xTaskCreate(recoveryNetTask, "recnet", 4096, nullptr, 1, nullptr);

  runRecoveryMenu(gBootIsApp1 ? 1 : 0, gBootMenuAlways);
}

void loop() {}
