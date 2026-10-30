// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// LittleFS file manager
// ---------------------------------------------------------------------------

#define FS_ROWS 6

static void drawFsLoading()
{
  gSprite.fillScreen(COL_BG);
  drawHeader("LITTLEFS");
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_BG);
  gSprite.drawString("LOADING...", 160, 72, FONT_LARGE);
  gSprite.setTextDatum(TL_DATUM);
  gSprite.pushSprite(0, 0);
}

static int fsListFiles(String *names, int maxCount)
{
  int count = 0;
  File root = LittleFS.open("/", "r");
  if(!root || !root.isDirectory()) return 0;

  File file = root.openNextFile();
  while(file && count < maxCount)
  {
    if(!file.isDirectory()) names[count++] = file.name();
    file = root.openNextFile();
  }
  root.close();
  return count;
}

static void drawFsPage(String *files, int count, int selected)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();

  char right[16];
  snprintf(right, sizeof(right), "%d", count);
  drawHeader("LITTLEFS", right);

  uint32_t total = LittleFS.totalBytes();
  uint32_t used = LittleFS.usedBytes();
  if(total > 0)
  {
    char cap[44];
    snprintf(cap, sizeof(cap), "Used %luK/%luK  Free %luK",
             (unsigned long)(used / 1024), (unsigned long)(total / 1024),
             (unsigned long)((total - used) / 1024));
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString(cap, 8, 28, FONT_TINY);
  }

  char pb[16] = "";

  if(count == 0)
  {
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString("No files", 8, 50, FONT_SMALL);
  }
  else
  {
    int start = (selected / FS_ROWS) * FS_ROWS;
    int end = start + FS_ROWS;
    if(end > count) end = count;
    for(int i = start; i < end; i++)
      uiRow(8, 46 + (i - start) * 17, 304, 15, files[i].c_str(),
            i == selected, COL_GOLD);

    int pages = (count + FS_ROWS - 1) / FS_ROWS;
    snprintf(pb, sizeof(pb), "%d/%d", start / FS_ROWS + 1, pages);
  }

  // The hint bar clears the bottom strip, so the page indicator is drawn after
  // it or it would be wiped before the sprite is pushed.
  uiHintBar("CLICK=ACTION", "HOLD=BACK");
  if(pb[0])
  {
    gSprite.setTextDatum(TC_DATUM);
    gSprite.setTextColor(COL_MUTED, COL_BG);
    gSprite.drawString(pb, SCR_W / 2, 156, FONT_TINY);
    gSprite.setTextDatum(TL_DATUM);
  }
  gSprite.pushSprite(0, 0);
}

static void runLittleFsManager()
{
  static const int maxFiles = 40;
  String files[maxFiles];

  drawFsLoading();
  int count = fsListFiles(files, maxFiles);
  int selected = 0;
  drawFsPage(files, count, selected);

  static const char *actions[] = {"Rename", "Delete"};
  static const int aIcons[2] = {ICON_SETTINGS, ICON_TRASH};
  static const uint16_t aAccs[2] = {COL_ACC, COL_WARN};

  while(true)
  {
    int8_t d = readEncoder();
    if(d && count > 0)
    {
      selected = (selected + d) % count;
      if(selected < 0) selected += count;
      drawFsPage(files, count, selected);
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_NONE) { delay(10); continue; }
    if(ev == BTN_HOLD) return;                // hold = back

    if(count == 0) continue;

    int act = runTileChoice("FILE", "TURN=MOVE", "CLICK=OPEN  HOLD=BACK",
                            actions, aIcons, aAccs, 2);
    if(act < 0) { drawFsPage(files, count, selected); continue; }

    String path = "/" + files[selected];
    if(act == 0)
    {
      String newName;
      if(runTextInput("RENAME", files[selected], newName, nullptr, false))
      {
        String target = "/" + newName;
        bool ok = (target != path) && LittleFS.rename(path, target);
        uiWarn(ok ? "Renamed" : "Rename failed");
      }
    }
    else if(confirmDialog("DELETE", files[selected].c_str(), "This cannot be undone"))
    {
      bool ok = LittleFS.remove(path);
      uiWarn(ok ? "Deleted" : "Delete failed");
    }

    count = fsListFiles(files, maxFiles);
    if(selected >= count) selected = (count > 0) ? count - 1 : 0;
    drawFsPage(files, count, selected);
  }
}

