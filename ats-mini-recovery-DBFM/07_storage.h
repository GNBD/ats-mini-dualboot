// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

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
  gSprite.fillScreen(COL_BG);
  drawHeader(list.title);

  int checkedCount = 0;
  for(int i = 0; i < 3; i++)
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "[%c] %s", checked[i] ? 'X' : ' ', list.items[i]);
    uiRow(8, 32 + i * 22, 304, 20, buf, selected == i,
          checked[i] ? COL_WARN : list.accs[i]);
    if(checked[i]) checkedCount++;
  }

  bool armed = checkedCount > 0;
  uiRow(8, 104, 304, 22, armed ? "RUN" : "RUN (select an item first)",
        selected == 3, armed ? COL_WARN : COL_MUTED);

  char left[24];
  snprintf(left, sizeof(left), "Checked %d/3", checkedCount);
  uiHintBar(left, "CLICK=TOGGLE/RUN  HOLD=BACK");
  gSprite.pushSprite(0, 0);
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
      selected = (selected + d) % 4;
      if(selected < 0) selected += 4;
      drawEraseList(list, checked, selected);
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_NONE) { delay(10); continue; }
    if(ev == BTN_HOLD) return;                        // hold = back

    int totalUnits = 0;
    for(int i = 0; i < 3; i++)
      if(checked[i]) totalUnits++;

    if(selected < 3)
    {
      checked[selected] = !checked[selected];
      drawEraseList(list, checked, selected);
      continue;
    }

    if(totalUnits == 0)
    {
      uiToast("Select at least one item");
      drawEraseList(list, checked, selected);
      continue;
    }

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
    int mode = runTileChoice("ERASE", "TURN=MOVE", "CLICK=OPEN  HOLD=BACK",
                             modes, modeIcons, modeAccs, 2);
    if(mode < 0) return;
    runEraseList(kEraseLists[mode]);
  }
}

