// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// Settings backup / restore
//
// Each environment keeps its NVS in a 128 KB `settings` region (ENV0 at
// DES_SETTINGS_ENV0, ENV1 at DES_SETTINGS_ENV1), so a raw copy of that region
// is a complete backup of the app's settings. Export writes it to a LittleFS
// file whose name is typed on the same keypad as the WiFi password; Import
// writes such a file back and reboots so the app re-reads it.
// ---------------------------------------------------------------------------

#define BK_CHUNK 256

static bool backupSettingsRegion(uint32_t addr, const char *path)
{
  File f = LittleFS.open(path, FILE_WRITE);
  if(!f) return false;

  uint32_t buf[BK_CHUNK / 4];
  bool ok = true;
  for(uint32_t off = 0; off < DES_SETTINGS_SIZE && ok; off += BK_CHUNK)
  {
    ok = ESP.flashRead(addr + off, buf, BK_CHUNK)
      && f.write((const uint8_t *)buf, BK_CHUNK) == BK_CHUNK;
  }
  f.close();
  return ok;
}

static bool restoreSettingsRegion(uint32_t addr, const char *path)
{
  File f = LittleFS.open(path, "r");
  if(!f) return false;
  if(f.size() != DES_SETTINGS_SIZE) { f.close(); return false; }

  uint32_t buf[BK_CHUNK / 4];
  bool ok = true;

  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  for(uint32_t s = 0; s < DES_SETTINGS_SIZE / 4096 && ok; s++)
    ok = ESP.flashEraseSector((addr >> 12) + s);
  for(uint32_t off = 0; off < DES_SETTINGS_SIZE && ok; off += BK_CHUNK)
  {
    if(f.read((uint8_t *)buf, BK_CHUNK) != BK_CHUNK) { ok = false; break; }
    ok = ESP.flashWrite(addr + off, buf, BK_CHUNK);
  }
  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  f.close();
  return ok;
}

// File picker for Import: only `.nvs` backups, hold to accept.
static bool pickBackupFile(String &out)
{
  static const int maxFiles = 40;
  String all[maxFiles];
  String files[maxFiles];
  int n = fsListFiles(all, maxFiles);
  int count = 0;
  for(int i = 0; i < n; i++)
    if(all[i].endsWith(".nvs")) files[count++] = all[i];
  if(count == 0) { uiWarn("No .nvs files"); return false; }

  int sel = 0;
  auto draw = [&]()
  {
    gSprite.fillScreen(COL_BG);
    uiDotGrid();
    char r[16];
    snprintf(r, sizeof(r), "%d", count);
    drawHeader("SELECT FILE", r);

    int start = (sel / FS_ROWS) * FS_ROWS;
    int end = start + FS_ROWS;
    if(end > count) end = count;
    for(int i = start; i < end; i++)
      uiRow(8, 40 + (i - start) * 18, 304, 16, files[i].c_str(),
            i == sel, COL_GOLD);

    uiHintBar("CLICK=OPEN", "HOLD=BACK");
    gSprite.pushSprite(0, 0);
  };

  draw();
  while(true)
  {
    int8_t d = readEncoder();
    if(d) { sel = (sel + d) % count; if(sel < 0) sel += count; draw(); }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_NONE) { delay(10); continue; }
    if(ev == BTN_HOLD) return false;          // hold = back
    out = files[sel];
    return true;
  }
}

static int pickBackupTarget()
{
  static const char *items[] = {"App0 cfg", "App1 cfg"};
  static const int icons[2] = {ICON_SLOT0, ICON_SLOT1};
  static const uint16_t accs[2] = {COL_ACC, COL_ACC};
  return runTileChoice("TARGET", "TURN=MOVE", "CLICK=OPEN  HOLD=BACK",
                       items, icons, accs, 2);
}

static void runBackupExport()
{
  int t = pickBackupTarget();
  if(t < 0) return;

  const char *initial = (t == 1) ? "app1" : "app0";
  String name;
  if(!runTextInput("SAVE AS", initial, name, ".nvs", false)) return;

  uint32_t addr = (t == 1) ? DES_SETTINGS_ENV1 : DES_SETTINGS_ENV0;
  String path = "/" + name;
  char msg[48];
  if(backupSettingsRegion(addr, path.c_str()))
    snprintf(msg, sizeof(msg), "Saved %s", name.c_str());
  else
    snprintf(msg, sizeof(msg), "Save failed");
  uiWarn(msg);
}

static void runBackupImport()
{
  String file;
  if(!pickBackupFile(file)) return;

  int t = pickBackupTarget();
  if(t < 0) return;

  if(!confirmDialog("RESTORE", file.c_str(), "overwrite that cfg?")) return;

  uint32_t addr = (t == 1) ? DES_SETTINGS_ENV1 : DES_SETTINGS_ENV0;
  String path = "/" + file;
  if(!restoreSettingsRegion(addr, path.c_str()))
  {
    uiWarn("Restore failed (size?)");
    return;
  }
  uiWarn("Restored, restarting");
  delay(600);
  ESP.restart();
}

static void runBackupMenu()
{
  static const char *items[] = {"Export", "Import"};
  static const int icons[2] = {ICON_UPDATE, ICON_DB};
  static const uint16_t accs[2] = {COL_OK, COL_GOLD};

  int m = runTileChoice("BACKUP", "TURN=MOVE", "CLICK=OPEN  HOLD=BACK",
                        items, icons, accs, 2);
  if(m < 0) return;
  if(m == 0) runBackupExport();
  else runBackupImport();
}

