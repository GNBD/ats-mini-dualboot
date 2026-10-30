// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

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
  uiHintBar(pageBuf, "CLICK=FLASH  HOLD=BACK");
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
  uiHintBar(pageBuf, "CLICK=OPEN  HOLD=BACK");
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

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_CLICK) return selected;
    if(ev == BTN_HOLD) return -1;
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

  {
    esp_app_desc_t nd;
    String ver;
    if(esp_ota_get_partition_description(target, &nd) == ESP_OK) ver = nd.version;
    saveFwMeta((uint8_t)targetSlot, filename, ver);
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

  // Refuse the download before touching the volume when the server tells us
  // the size and LittleFS cannot hold it.
  if(total > 0)
  {
    size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    if((size_t)total > freeBytes)
    {
      uiWarn("Not enough storage");
      http.end();
      return false;
    }
  }

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

  bool ok = (total > 0) ? (got == total) : (got > 0);
  if(!ok) LittleFS.remove("/" + fname);
  return ok;
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

static bool downloadOneFromUrls(String &outName)
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
    gSprite.pushSprite(0, 0);
    delay(1800);
    return false;
  }

  const char *pick[MAX_URLS];
  for(int i = 0; i < urlCount; i++) pick[i] = urls[i].c_str();

  int sel = 0;
  if(urlCount > 1)
  {
    sel = runChoice("FIRMWARE", "Select firmware:", pick, urlCount);
    if(sel < 0) return false;
  }

  String fname = urlToFileName(pick[sel]);
  if(downloadToFile(pick[sel], fname))
  {
    outName = fname;
    return true;
  }

  gSprite.fillScreen(COL_BG);
  drawHeader("DOWNLOAD");
  gSprite.setTextColor(COL_WARN, COL_BG);
  gSprite.drawString("Download failed", 10, 54, FONT_SMALL);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString(fname, 10, 74, FONT_TINY);
  gSprite.pushSprite(0, 0);
  delay(1800);
  return false;
}

// Source -> slot -> firmware. Local files and network downloads both flash
// immediately after selection (no extra confirmation).
static void runFirmwareUpdate()
{
  static const char *sourceNames[] = {"Local files", "Network"};
  static const int sourceIcons[] = {ICON_FOLDER, ICON_NET};
  static const uint16_t sourceAccs[] = {COL_GOLD, COL_ACC};
  int srcSel = runTileChoice("FIRMWARE", "Select source:",
                             "CLICK=OPEN  HOLD=BACK",
                             sourceNames, sourceIcons, sourceAccs, 2);
  if(srcSel < 0) return;

  static const char *slotNames[] = {"App0", "App1"};
  static const int slotIcons[] = {ICON_SLOT0, ICON_SLOT1};
  static const uint16_t slotAccs[] = {COL_OK, COL_VIOLET};
  int slotSel = runTileChoice("FIRMWARE", "Select target slot:",
                              "CLICK=OPEN  HOLD=BACK",
                              slotNames, slotIcons, slotAccs, 2);
  if(slotSel < 0) return;

  auto flashNow = [&](const String &name) -> bool
  {
    gFlashBusy = true;
    bool ok = flashFirmware(name, slotSel);
    gFlashBusy = false;
    if(ok && !setBootSlot(slotSel))
    {
      uiWarn("Could not select boot slot");
      return false;
    }
    if(ok)
    {
      delay(500);
      ESP.restart();
    }
    return ok;
  };

  if(srcSel == 1)
  {
    String fname;
    if(!downloadOneFromUrls(fname)) return;
    flashNow(fname);
    return;
  }

  String files[32];
  int fileCount = listFirmwareFiles(files, 32);
  if(fileCount == 0)
  {
    gSprite.fillScreen(COL_BG);
    drawHeader("FIRMWARE");
    gSprite.setTextColor(COL_WARN, COL_BG);
    gSprite.drawString("No .bin files found", 8, 54, FONT_SMALL);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("Use Network or web upload.", 8, 88, FONT_SMALL);
    gSprite.pushSprite(0, 0);
    delay(1800);
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

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_NONE) { delay(10); continue; }
    if(ev == BTN_HOLD) return;

    if(gFlashBusy || uploadBusy())
    {
      gSprite.fillScreen(COL_BG);
      drawHeader("FIRMWARE");
      gSprite.setTextColor(COL_WARN, COL_BG);
      gSprite.drawString("Web flash running", 8, 60, FONT_SMALL);
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString("Wait for it to finish", 8, 80, FONT_SMALL);
      gSprite.pushSprite(0, 0);
      delay(1200);
      drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
      continue;
    }

    flashNow(files[fileSel]);
    drawFilePage(files, fileCount, fileSel, slotNames[slotSel]);
  }
}
