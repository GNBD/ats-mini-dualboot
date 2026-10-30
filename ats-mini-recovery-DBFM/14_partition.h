// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// Partition manager
// ---------------------------------------------------------------------------

static void runPartitionInfo()
{
  const int maxParts = 16;
  const esp_partition_t *parts[maxParts];
  int n = 0;
  esp_partition_iterator_t it =
    esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while(it && n < maxParts)
  {
    parts[n++] = esp_partition_get(it);
    it = esp_partition_next(it);
  }

  const int rowsPerPage = 6;
  int pages = (n + rowsPerPage - 1) / rowsPerPage;
  if(pages < 1) pages = 1;
  int page = 0;

  auto draw = [&]()
  {
    gSprite.fillScreen(COL_BG);
    uiDotGrid();
    char right[16];
    snprintf(right, sizeof(right), "%d/%d", page + 1, pages);
    drawHeader("PARTITIONS", right);

    int start = page * rowsPerPage;
    for(int i = start; i < n && i < start + rowsPerPage; i++)
    {
      int y = 30 + (i - start) * 18;
      char line[40];
      snprintf(line, sizeof(line), "%s", parts[i]->label);
      gSprite.setTextColor(COL_TEXT, COL_BG);
      gSprite.drawString(line, 8, y, FONT_SMALL);
      snprintf(line, sizeof(line), "0x%06X", (unsigned)parts[i]->address);
      gSprite.setTextColor(COL_MUTED, COL_BG);
      gSprite.drawString(line, 110, y, FONT_SMALL);
      snprintf(line, sizeof(line), "%uK", (unsigned)(parts[i]->size / 1024));
      gSprite.drawString(line, 240, y, FONT_SMALL);
    }

    uiHintBar("Rotate=Page", "HOLD=BACK");
    gSprite.pushSprite(0, 0);
  };

  draw();
  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      page = (page + d) % pages;
      if(page < 0) page += pages;
      draw();
    }
    uint32_t h = readButton();
    if(h > 0) return;
    delay(10);
  }
}

static void runPartitionRepair()
{
  if(!confirmDialog("REPAIR", "Rebuild partition", "table from canonical?"))
    return;

  uint8_t env = desGetCurrentEnv();
  if(env == 0xFF) env = 0;

  if(desRebuildTable(env))
  {
    uiWarn("Repaired, restarting");
    delay(600);
    ESP.restart();
  }
  uiWarn("Repair failed");
}

static uint32_t desCurrentApp0Size()
{
  uint32_t size = 0;
  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(buf)
  {
    size_t a0, a1;
    if(ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
       && desAppLayoutOk(buf, DES_TABLE_SIZE)
       && desFindAppEntries(buf, DES_TABLE_SIZE, &a0, &a1))
      size = desGet32(buf + a0 + 8);
    free(buf);
  }
  if(size == 0) size = 0x380000u;            // canonical 3.5 / 3.5
  return size;
}

// Moves the app0/app1 boundary in the table already on flash, keeping the
// `settings` retargeting untouched, writes the active slot at 0x8000 and
// refreshes the Table B backup at 0xFFF000 with the same bytes.
static bool desApplyResize(uint32_t app0Size, uint8_t env)
{
  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf) return false;

  bool ok = ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE);
  if(ok)
  {
    desSetAppSizes(buf, DES_TABLE_SIZE, app0Size);
    ok = desBuildTable(buf, DES_TABLE_SIZE, env)
      && desVerifyTable(buf, DES_TABLE_SIZE, env);
  }
  if(ok)
  {
    esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
    ok = ESP.flashEraseSector(DES_PT_SECTOR)
      && ESP.flashWrite(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE);
    esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  }
  if(ok)
  {
    ESP.flashEraseSector(DES_PT_B_SECTOR);
    ESP.flashWrite(0xFFF000, (uint32_t *)buf, DES_TABLE_SIZE);
  }
  free(buf);
  return ok;
}

static void drawResize(int step, int steps, uint32_t app0, uint32_t app1)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("RESIZE");

  const int x = 16, w = 288, y = 78, h = 28;
  int w0 = (int)((uint64_t)w * app0 / DES_APP_TOTAL);
  gSprite.fillRoundRect(x, y, w, h, 4, COL_TRACK);
  gSprite.fillRoundRect(x, y, w0, h, 4, COL_ACC);
  gSprite.fillRoundRect(x + w0, y, w - w0, h, 4, COL_GOLD);
  gSprite.drawRoundRect(x, y, w, h, 4, COL_LINE);

  char buf[40];
  gSprite.setTextDatum(TC_DATUM);
  snprintf(buf, sizeof(buf), "App0 %lu.%luM", (unsigned long)(app0 >> 20),
           (unsigned long)((app0 >> 19) & 1) * 5);
  gSprite.setTextColor(COL_ACC, COL_BG);
  gSprite.drawString(buf, SCR_W / 2, 40, FONT_SMALL);
  snprintf(buf, sizeof(buf), "App1 %lu.%luM", (unsigned long)(app1 >> 20),
           (unsigned long)((app1 >> 19) & 1) * 5);
  gSprite.setTextColor(COL_GOLD, COL_BG);
  gSprite.drawString(buf, SCR_W / 2, 58, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);

  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("Turn to move the boundary", 8, 116, FONT_TINY);

  char pg[16];
  snprintf(pg, sizeof(pg), "%d/%d", step + 1, steps);
  uiHintBar("TURN=MOVE", "CLICK=APPLY  HOLD=BACK");
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString(pg, SCR_W / 2, 156, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
}

// Shown when the boundary cannot move: both slots must be blank, because a
// slot that still holds an image would be cut in half by a smaller size.
static void showResizeBlocked()
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("RESIZE");

  gSprite.setTextColor(COL_WARN, COL_BG);
  gSprite.drawString("Cannot resize yet", 8, 34, FONT_SMALL);

  gSprite.setTextColor(COL_TEXT, COL_BG);
  gSprite.drawString("Both app slots must be", 8, 58, FONT_SMALL);
  gSprite.drawString("empty before the boundary", 8, 76, FONT_SMALL);
  gSprite.drawString("can move.", 8, 94, FONT_SMALL);

  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("Erase > App0 and App1 first.", 8, 116, FONT_SMALL);

  uiHintBar("", "HOLD=BACK");
  gSprite.pushSprite(0, 0);

  while(readButton() == 0) delay(10);
}

static void runPartitionResize()
{
  // A slot that still holds something cannot be shrunk under it, so the
  // boundary only moves while both app slots are empty.
  const esp_partition_t *a0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
    static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0), NULL);
  const esp_partition_t *a1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
    static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1), NULL);
  if(appImageOk(a0) || appImageOk(a1))
  {
    showResizeBlocked();
    return;
  }

  const int steps = (DES_APP_TOTAL - 2 * DES_APP_MIN) / DES_APP_STEP + 1;
  int step = (int)((desCurrentApp0Size() - DES_APP_MIN) / DES_APP_STEP);
  if(step < 0) step = 0;
  if(step >= steps) step = steps - 1;

  auto app0At = [&](int s) { return DES_APP_MIN + (uint32_t)s * DES_APP_STEP; };
  drawResize(step, steps, app0At(step), DES_APP_TOTAL - app0At(step));

  while(true)
  {
    int8_t d = readEncoder();
    if(d)
    {
      step += d;
      if(step < 0) step = 0;
      if(step >= steps) step = steps - 1;
      drawResize(step, steps, app0At(step), DES_APP_TOTAL - app0At(step));
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_NONE) { delay(10); continue; }
    if(ev == BTN_HOLD) return;                // hold = back
    break;                                    // click = apply
  }

  uint32_t app0Size = app0At(step);
  if(app0Size == desCurrentApp0Size()) { uiWarn("Unchanged"); return; }
  if(!confirmDialog("RESIZE", "Rewrite the partition", "table now?")) return;

  uint8_t env = desGetCurrentEnv();
  if(env == 0xFF) env = 0;

  if(!desApplyResize(app0Size, env))
  {
    uiWarn("Resize failed");
    return;
  }
  uiWarn("Resized, restarting");
  delay(600);
  ESP.restart();
}

static void runPartitionManager()
{
  static const char *items[] = {"Info", "Resize", "Repair"};
  static const int icons[3] = {ICON_DB, ICON_SETTINGS, ICON_UPDATE};
  static const uint16_t accs[3] = {COL_ACC, COL_GOLD, COL_OK};

  int m = runTileChoice("PARTITION", "TURN=MOVE", "CLICK=OPEN  HOLD=BACK",
                        items, icons, accs, 3);
  if(m < 0) return;
  if(m == 0) runPartitionInfo();
  else if(m == 1) runPartitionResize();
  else runPartitionRepair();
}

