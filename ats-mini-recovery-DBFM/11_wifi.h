// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

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
      selected = (selected + dir) % 4;
      if(selected < 0) selected += 4;
      drawWifiMenu(selected);
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD) return;                        // hold = back
    if(ev == BTN_CLICK)
    {
      if(selected == 0)                               // OFF
      {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        apModeActive = false;
        apIP = "";
      }
      else if(selected == 1)                          // AP
      {
        wifiStartAp();
        if(gWebEnabled && !webTaskHandle) startWebBackground();
      }
      else if(selected == 2)                          // CONNECT (saved)
      {
        String ssid, pass;
        if(loadWifiCredentials(ssid, pass))
        {
          if(wifiConnectBlocking(ssid, pass, WIFI_CONNECT_TIMEOUT))
          {
            apModeActive = false;
            apIP = WiFi.localIP().toString();
          }
        }
        else runWifiSetting();
      }
      else                                            // SCAN
      {
        runWifiSetting();
        if(WiFi.status() == WL_CONNECTED)
        {
          apModeActive = false;
          apIP = WiFi.localIP().toString();
        }
      }
      drawWifiMenu(selected);
    }
    delay(10);
  }
}

