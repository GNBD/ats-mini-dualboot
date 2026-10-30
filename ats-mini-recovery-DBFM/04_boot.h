// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// Boot helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Flash self check
//
// The ESP32-S3 second stage bootloader lives at flash offset 0x0
// (build.bootloader_addr for esp32s3), not at 0x1000, so it is read back raw.
// BOOTLOADER_FLASH_CRC32 is the CRC32 of the 20000 byte bootloader.bin shipped
// next to this sketch; recompute it whenever that file is rebuilt.
// ---------------------------------------------------------------------------

#define BOOTLOADER_FLASH_ADDR   0x0
#define BOOTLOADER_FLASH_SIZE   20000u
#define BOOTLOADER_FLASH_CRC32  0x4B2022DEu
#define PARTITION_TABLE_ADDR    0x8000

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len)
{
  for(size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for(int k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

// Bootloader image present and byte identical to our bootloader.bin?
static bool bootloaderOk()
{
  uint32_t buf[64];
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t addr = BOOTLOADER_FLASH_ADDR;

  // 20000 is not a multiple of the buffer, so the tail chunk must be clamped
  // or the CRC would cover bytes past the bootloader.
  while(addr < BOOTLOADER_FLASH_SIZE)
  {
    size_t chunk = BOOTLOADER_FLASH_SIZE - addr;
    if(chunk > sizeof(buf)) chunk = sizeof(buf);
    if(!ESP.flashRead(addr, buf, chunk)) return false;
    crc = crc32Update(crc, (const uint8_t *)buf, chunk);
    addr += chunk;
  }

  crc ^= 0xFFFFFFFFu;
  bool ok = (crc == BOOTLOADER_FLASH_CRC32);
  Serial.printf("self check: bootloader crc 0x%08lX (%s)\n",
                (unsigned long)crc, ok ? "ok" : "MISMATCH");
  return ok;
}

// ---------------------------------------------------------------------------
// DES: partition table patch
// ---------------------------------------------------------------------------
// ENV0 and ENV1 share every entry except `settings`, which points at the
// environment's own NVS region (0xF9D000 for ENV0, 0xFBD000 for ENV1).
// Switching therefore means: retarget that one offset in a RAM copy of the
// canonical table, recompute the MD5 entry over the first num_parts * 32
// bytes, then erase and rewrite 0x8000. The table is always staged in RAM so
// no mapped flash pointer survives into the write.
#define DES_TABLE_SIZE      0xC00u         // slot the bootloader scans
#define DES_ENTRY_SIZE      32u
#define DES_MAX_PARTITIONS  64u
#define DES_MAGIC_ENTRY     0x50AAu
#define DES_MAGIC_MD5       0xEBEBu
#define DES_SETTINGS_ENV0   0xF9D000u
#define DES_SETTINGS_ENV1   0xFBD000u
#define DES_SETTINGS_SIZE   0x20000u
#define DES_PT_SECTOR       (PARTITION_TABLE_ADDR / 4096u)

// The two app slots share one 7 MB region; resizing only moves the boundary
// between them, in 0.5 MB steps, so one slot can go from 1 MB to 6 MB.
#define DES_APP0_OFFSET     0x10000u
#define DES_APP_END         0x710000u
#define DES_APP_TOTAL       (DES_APP_END - DES_APP0_OFFSET)
#define DES_APP_MIN         0x100000u
#define DES_APP_STEP        0x80000u
#define DES_PT_B_SECTOR     (0xFFF000u / 4096u)

static const uint8_t DES_LABEL_SETTINGS[16] = "settings";

// Walks the entry list. Fails when the magic chain or the MD5 terminator is
// broken; otherwise reports the partition count, the index of the `settings`
// entry (0xFFFF when absent) and the offset of the MD5 entry.
static bool desParseTable(const uint8_t *buf, size_t len, uint16_t *numParts,
                          uint16_t *settingsIdx, size_t *md5Pos)
{
  *numParts = 0;
  *settingsIdx = 0xFFFF;
  *md5Pos = 0;

  for(size_t pos = 0; pos + DES_ENTRY_SIZE <= len; pos += DES_ENTRY_SIZE)
  {
    uint16_t magic = (uint16_t)buf[pos] | ((uint16_t)buf[pos + 1] << 8);

    if(magic == DES_MAGIC_MD5)
    {
      *md5Pos = pos;
      return *numParts > 0 && *numParts <= DES_MAX_PARTITIONS;
    }
    if(magic != DES_MAGIC_ENTRY) return false;
    if(memcmp(buf + pos + 12, DES_LABEL_SETTINGS, sizeof(DES_LABEL_SETTINGS)) == 0)
      *settingsIdx = (uint16_t)(pos / DES_ENTRY_SIZE);
    (*numParts)++;
  }
  return false;                             // no MD5 terminator in the slot
}

// ESP-IDF hashes exactly num_parts * 32 bytes (everything before the MD5
// entry) and stores the digest in bytes 16..31 of that entry - the same rule
// gen_esp32part.py and the host side verify_md5.py apply.
static void desStoreMd5(uint8_t *buf, uint16_t numParts, size_t md5Pos)
{
  MD5Builder md5;
  md5.begin();
  md5.add(buf, (size_t)numParts * DES_ENTRY_SIZE);
  md5.calculate();
  md5.getBytes(buf + md5Pos + 16);
}

// Retargets the `settings` entry to `env` inside a RAM copy of the table and
// refreshes the MD5. Fails if the copy does not parse.
static bool desBuildTable(uint8_t *buf, size_t len, uint8_t env)
{
  uint16_t numParts, settingsIdx;
  size_t md5Pos;
  if(!desParseTable(buf, len, &numParts, &settingsIdx, &md5Pos)) return false;
  if(settingsIdx == 0xFFFF) return false;

  size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
  uint32_t off = env ? DES_SETTINGS_ENV1 : DES_SETTINGS_ENV0;

  // Only the offset moves - both environments use the same size, which
  // desVerifyTable() checks against the source table.
  for(int i = 0; i < 4; i++) buf[e + 4 + i] = (uint8_t)(off >> (8 * i));

  desStoreMd5(buf, numParts, md5Pos);
  return true;
}

static uint32_t desGet32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void desPut32(uint8_t *p, uint32_t v)
{
  for(int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

// Locates the app0/app1 entries by label.
static bool desFindAppEntries(const uint8_t *buf, size_t len,
                              size_t *a0, size_t *a1)
{
  uint16_t numParts, settingsIdx;
  size_t md5Pos;
  if(!desParseTable(buf, len, &numParts, &settingsIdx, &md5Pos)) return false;

  *a0 = (size_t)-1;
  *a1 = (size_t)-1;
  for(uint16_t i = 0; i < numParts; i++)
  {
    size_t pos = (size_t)i * DES_ENTRY_SIZE;
    if(memcmp(buf + pos + 12, "app0", 4) == 0) *a0 = pos;
    else if(memcmp(buf + pos + 12, "app1", 4) == 0) *a1 = pos;
  }
  return *a0 != (size_t)-1 && *a1 != (size_t)-1;
}

// App0 keeps its offset and grows or shrinks; app1 starts where app0 ends and
// stretches to the end of the shared region.
static void desSetAppSizes(uint8_t *buf, size_t len, uint32_t app0Size)
{
  size_t a0, a1;
  if(!desFindAppEntries(buf, len, &a0, &a1)) return;
  uint32_t a1off = DES_APP0_OFFSET + app0Size;
  desPut32(buf + a0 + 8, app0Size);
  desPut32(buf + a1 + 4, a1off);
  desPut32(buf + a1 + 8, DES_APP_END - a1off);
}

// The app entries must form one contiguous region that ends at `ffat`, with
// each slot at least DES_APP_MIN and everything sector aligned.
static bool desAppLayoutOk(const uint8_t *buf, size_t len)
{
  size_t a0, a1;
  if(!desFindAppEntries(buf, len, &a0, &a1)) return false;

  uint32_t off0 = desGet32(buf + a0 + 4), size0 = desGet32(buf + a0 + 8);
  uint32_t off1 = desGet32(buf + a1 + 4), size1 = desGet32(buf + a1 + 8);

  if(off0 != DES_APP0_OFFSET) return false;
  if(off1 != off0 + size0) return false;
  if((uint64_t)off1 + size1 != DES_APP_END) return false;
  if(size0 < DES_APP_MIN || size1 < DES_APP_MIN) return false;
  if(((size0 | size1 | off1) & 0xFFFu) != 0) return false;
  return true;
}

// The criteria the offline verifier applies: a parseable magic chain, exactly
// one `settings` entry at the expected 64KB aligned address inside flash, and
// a stored MD5 that matches a recomputation.
static bool desVerifyTable(const uint8_t *buf, size_t len, uint8_t env)
{
  uint16_t numParts, settingsIdx;
  size_t md5Pos;
  if(!desParseTable(buf, len, &numParts, &settingsIdx, &md5Pos)) return false;
  if(settingsIdx == 0xFFFF) return false;

  size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
  uint32_t off = 0, size = 0;
  for(int i = 0; i < 4; i++)
  {
    off  |= (uint32_t)buf[e + 4 + i] << (8 * i);
    size |= (uint32_t)buf[e + 8 + i] << (8 * i);
  }

  uint32_t want = env ? DES_SETTINGS_ENV1 : DES_SETTINGS_ENV0;
  if(off != want || size != DES_SETTINGS_SIZE) return false;
  if(off & 0xFFFu) return false;            // 4KB (only app slots need 64KB)
  if((uint64_t)off + size > 0x1000000ull) return false;
  if(!desAppLayoutOk(buf, len)) return false;

  uint8_t stored[16];
  MD5Builder md5;
  md5.begin();
  md5.add(buf, (size_t)numParts * DES_ENTRY_SIZE);
  md5.calculate();
  md5.getBytes(stored);
  return memcmp(stored, buf + md5Pos + 16, sizeof(stored)) == 0;
}

// Reads the canonical table straight from flash and reports which
// environment its `settings` entry points at. 0/1 on success, 0xFF when the
// table does not parse or the offset is not one of the two DES regions.
static uint8_t desGetCurrentEnv()
{
  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf) return 0xFF;

  uint8_t env = 0xFF;
  if(ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE))
  {
    uint16_t numParts, settingsIdx;
    size_t md5Pos;
    if(desParseTable(buf, DES_TABLE_SIZE, &numParts, &settingsIdx, &md5Pos)
       && settingsIdx != 0xFFFF)
    {
      size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
      uint32_t off = 0;
      for(int i = 0; i < 4; i++) off |= (uint32_t)buf[e + 4 + i] << (8 * i);
      if(off == DES_SETTINGS_ENV0) env = 0;
      else if(off == DES_SETTINGS_ENV1) env = 1;
    }
  }

  free(buf);
  return env;
}

// IDF treats the bootloader and partition table as write-protected and abort()
// on a write that reaches them (CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS), so
// the table cannot be erased with ESP.flashEraseSector as-is. This is the
// documented toggle; it only affects the window between the two calls below.
extern "C" esp_err_t esp_flash_set_dangerous_write_protection(esp_flash_t *chip,
                                                              const bool protect);

// Rewrites the canonical table at 0x8000 so `settings` belongs to `env`. The
// staged buffer is verified before the sector is erased and again after the
// write, so any failure before the erase leaves the table byte-for-byte
// intact. The caller must ESP.restart() afterwards because the running
// firmware has already cached the old partition list.
static bool patchPartitions(uint8_t env)
{
  // Already pointing where it should be - no reason to rewrite the sector.
  if(desGetCurrentEnv() == env) return true;

  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf)
  {
    Serial.println("patch: no memory");
    return false;
  }

  bool ok = ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
         && desBuildTable(buf, DES_TABLE_SIZE, env)
         && desVerifyTable(buf, DES_TABLE_SIZE, env);
  if(!ok)
  {
    Serial.println("patch: staged table failed verification");
    free(buf);
    return false;
  }

  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  ok = ESP.flashEraseSector(DES_PT_SECTOR)
    && ESP.flashWrite(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE);
  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  if(!ok)
  {
    Serial.println("patch: flash write failed");
    free(buf);
    return false;
  }

  memset(buf, 0, DES_TABLE_SIZE);
  ok = ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
    && desVerifyTable(buf, DES_TABLE_SIZE, env);
  Serial.printf("patch: env %u -> %s\n", env, ok ? "ok" : "FAILED");
  free(buf);
  return ok;
}

// Canonical Table A (DES ENV0) embedded in the image: 10 entries plus the MD5
// terminator, exactly the bytes of des/table_a.bin. Everything past 0x160 in
// the slot is erased flash. Keeping a copy here is what lets the splash check
// rebuild a table that esptool or an older release overwrote.
static const uint8_t DES_CANONICAL_TABLE[0x160] = {
    0xAA, 0x50, 0x01, 0x02, 0x00, 0x90, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x6E, 0x76, 0x73, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x6F, 0x74, 0x61, 0x64,
    0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x38, 0x00, 0x61, 0x70, 0x70, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x00, 0x11, 0x00, 0x00, 0x39, 0x00, 0x00, 0x00, 0x38, 0x00, 0x61, 0x70, 0x70, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x81, 0x00, 0x00, 0x71, 0x00, 0x00, 0x00, 0x15, 0x00, 0x66, 0x66, 0x61, 0x74,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x00, 0x12, 0x00, 0x00, 0x86, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x72, 0x65, 0x63, 0x6F,
    0x76, 0x65, 0x72, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x83, 0x00, 0x00, 0xA0, 0x00, 0x00, 0x50, 0x59, 0x00, 0x6C, 0x69, 0x74, 0x74,
    0x6C, 0x65, 0x66, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x02, 0x00, 0x50, 0xF9, 0x00, 0x00, 0x80, 0x00, 0x00, 0x72, 0x65, 0x63, 0x5F,
    0x73, 0x65, 0x74, 0x74, 0x69, 0x6E, 0x67, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x02, 0x00, 0xD0, 0xF9, 0x00, 0x00, 0x00, 0x02, 0x00, 0x73, 0x65, 0x74, 0x74,
    0x69, 0x6E, 0x67, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0x50, 0x01, 0x03, 0x00, 0xF0, 0xFD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x63, 0x6F, 0x72, 0x65,
    0x64, 0x75, 0x6D, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x7B, 0x20, 0x02, 0x65, 0xF3, 0xDF, 0x2D, 0x81, 0x94, 0x15, 0xFB, 0x81, 0x3F, 0x1B, 0xC7, 0x76,
};

// Entry by entry comparison of the slot in flash against the embedded copy.
// Only one difference is legal: the `settings` offset, which may point at
// either DES environment. Everything else - count, order, magic, type,
// subtype, size, label and the MD5 digest - has to match byte for byte.
static bool desMatchesCanonical(const uint8_t *buf, size_t len)
{
  uint16_t numParts, settingsIdx;
  size_t md5Pos;
  if(!desParseTable(buf, len, &numParts, &settingsIdx, &md5Pos)) return false;

  uint16_t cNum, cSettings;
  size_t cMd5;
  if(!desParseTable(DES_CANONICAL_TABLE, sizeof(DES_CANONICAL_TABLE),
                    &cNum, &cSettings, &cMd5)) return false;
  if(numParts != cNum || settingsIdx != cSettings) return false;

  for(size_t i = 0; i < cNum; i++)
  {
    size_t pos = i * DES_ENTRY_SIZE;
    if(memcmp(buf + pos, DES_CANONICAL_TABLE + pos, DES_ENTRY_SIZE) == 0) continue;

    // The two app slots may move the boundary between them: app0 keeps its
    // offset and only its size changes, app1's offset follows it.
    bool isApp0 = memcmp(DES_CANONICAL_TABLE + pos + 12, "app0", 4) == 0;
    bool isApp1 = memcmp(DES_CANONICAL_TABLE + pos + 12, "app1", 4) == 0;
    if(isApp0 || isApp1)
    {
      if(memcmp(buf + pos, DES_CANONICAL_TABLE + pos, 4) != 0) return false;
      if(memcmp(buf + pos + 12, DES_CANONICAL_TABLE + pos + 12, DES_ENTRY_SIZE - 12) != 0)
        return false;
      if(isApp0 && memcmp(buf + pos + 4, DES_CANONICAL_TABLE + pos + 4, 4) != 0)
        return false;
      continue;
    }

    // The `settings` entry is the only other one allowed to differ, and only
    // in its four offset bytes.
    if(i != (size_t)cSettings) return false;
    if(memcmp(buf + pos, DES_CANONICAL_TABLE + pos, 4) != 0) return false;
    if(memcmp(buf + pos + 8, DES_CANONICAL_TABLE + pos + 8, DES_ENTRY_SIZE - 8) != 0)
      return false;

    uint32_t off = 0;
    for(int b = 0; b < 4; b++) off |= (uint32_t)buf[pos + 4 + b] << (8 * b);
    if(off != DES_SETTINGS_ENV0 && off != DES_SETTINGS_ENV1) return false;
  }

  uint8_t env = 0;
  if(settingsIdx != 0xFFFF)
  {
    size_t e = (size_t)settingsIdx * DES_ENTRY_SIZE;
    uint32_t off = 0;
    for(int b = 0; b < 4; b++) off |= (uint32_t)buf[e + 4 + b] << (8 * b);
    env = (uint8_t)(off == DES_SETTINGS_ENV1 ? 1 : 0);
  }
  return desVerifyTable(buf, len, env);
}

// Reads the slot and checks it against the embedded canonical table. Silent
// on pass; names the first entry that differs otherwise.
static bool partitionTableOk()
{
  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf) { Serial.println("self check: no memory"); return false; }

  bool ok = ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
         && desMatchesCanonical(buf, DES_TABLE_SIZE);

  if(!ok)
  {
    uint16_t numParts = 0, settingsIdx = 0xFFFF;
    size_t md5Pos = 0;
    if(ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE)
       && desParseTable(buf, DES_TABLE_SIZE, &numParts, &settingsIdx, &md5Pos))
      Serial.printf("self check: partition table differs (%u entries)\n", numParts);
    else
      Serial.println("self check: partition table unreadable");
  }
  free(buf);
  return ok;
}

// Rebuilds the slot from DES_CANONICAL_TABLE. `env` selects which DES
// environment the `settings` entry points at; ENV0 is the default so a slot
// that cannot be read at all still comes back to a known state.
static bool desRebuildTable(uint8_t env)
{
  // A resized boundary is kept: when the slot on flash still parses, its app
  // sizes are reused; otherwise the canonical 3.5 / 3.5 split comes back.
  uint32_t app0Size = 0;
  {
    uint8_t *cur = (uint8_t *)malloc(DES_TABLE_SIZE);
    if(cur)
    {
      size_t a0, a1;
      if(ESP.flashRead(PARTITION_TABLE_ADDR, (uint32_t *)cur, DES_TABLE_SIZE)
         && desAppLayoutOk(cur, DES_TABLE_SIZE)
         && desFindAppEntries(cur, DES_TABLE_SIZE, &a0, &a1))
        app0Size = desGet32(cur + a0 + 8);
      free(cur);
    }
  }

  uint8_t *buf = (uint8_t *)malloc(DES_TABLE_SIZE);
  if(!buf) { Serial.println("rebuild: no memory"); return false; }

  // Everything past the embedded entries is erased flash, not zeroes.
  memset(buf, 0xFF, DES_TABLE_SIZE);
  memcpy(buf, DES_CANONICAL_TABLE, sizeof(DES_CANONICAL_TABLE));
  if(app0Size) desSetAppSizes(buf, DES_TABLE_SIZE, app0Size);

  bool ok = desBuildTable(buf, DES_TABLE_SIZE, env)
         && desVerifyTable(buf, DES_TABLE_SIZE, env);
  if(!ok)
  {
    Serial.println("rebuild: staged table failed verification");
    free(buf);
    return false;
  }

  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  ok = ESP.flashEraseSector(DES_PT_SECTOR)
    && ESP.flashWrite(PARTITION_TABLE_ADDR, (uint32_t *)buf, DES_TABLE_SIZE);
  esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  if(!ok) Serial.println("rebuild: flash write failed");

  free(buf);
  if(!ok) return false;

  ok = partitionTableOk();
  Serial.printf("rebuild: env %u -> %s\n", env, ok ? "ok" : "FAILED");
  return ok;
}

// Blocking warning screen: header title, panel with two lines, wait for a
// click (or a hold) to continue.
static void uiBootWarning(const char *title, const char *l1, uint16_t c1,
                          const char *l2, uint16_t c2, const char *hintLeft)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader(title);

  uiPanel(24, 44, 272, 76, COL_WARN);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_WARN, COL_PANEL);
  gSprite.drawString("WARNING", 160, 58, FONT_SMALL);
  gSprite.setTextColor(c1, COL_PANEL);
  gSprite.drawString(l1, 160, 78, FONT_SMALL);
  gSprite.setTextColor(c2, COL_PANEL);
  gSprite.drawString(l2, 160, 98, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);

  uiHintBar(hintLeft, "CLICK=CONTINUE");
   gSprite.pushSprite(0, 0);
  while(readButton() == 0) delay(10);
  gSprite.pushSprite(0, 0);
}

// Splash side status screen: no input needed, it clears itself once the
// repair below has run. Click skips the wait.
static void uiSelfCheckStatus(const char *l1, uint16_t c1,
                              const char *l2, uint16_t c2)
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("FLASH CHECK");

  uiPanel(24, 44, 272, 76, COL_WARN);
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_WARN, COL_PANEL);
  gSprite.drawString("WARNING", 160, 58, FONT_SMALL);
  gSprite.setTextColor(c1, COL_PANEL);
  gSprite.drawString(l1, 160, 78, FONT_SMALL);
  gSprite.setTextColor(c2, COL_PANEL);
  gSprite.drawString(l2, 160, 98, FONT_SMALL);
  gSprite.setTextDatum(TL_DATUM);

  uiHintBar("Repairing", "CLICK=SKIP");
  gSprite.pushSprite(0, 0);
}

// Runs on every boot while the splash is up. Reads the bootloader image and
// the partition slot, compares both against what this build shipped with, and
// stays silent when they agree - the whole check is a few hundred kilobytes
// of flash reads plus two memcmp passes, well inside the splash window.
//
// A partition table that differs is not just reported: the canonical table is
// embedded in this image, so the slot is rebuilt and the device restarts onto
// it. A bootloader mismatch cannot be repaired from here and only warns.
static void flashSelfCheck()
{
  uint32_t t0 = millis();
  bool bl = bootloaderOk();
  bool pt = partitionTableOk();
  uint32_t elapsed = millis() - t0;

  if(bl && pt)
  {
    Serial.printf("self check: pass in %lu ms\n", (unsigned long)elapsed);
    return;
  }

  if(!pt)
  {
    // Keep the environment that is already selected so a rebuild does not
    // silently flip App0/App1 settings.
    uint8_t env = desGetCurrentEnv();
    if(env == 0xFF) env = 0;

    Serial.printf("self check: partition table MISMATCH (%lu ms) -> rebuild\n",
                  (unsigned long)elapsed);
    uiSelfCheckStatus("Partitions: WRONG", COL_WARN,
                      "Rebuilding table...", COL_GOLD);

    uint32_t waitStart = millis();
    while(millis() - waitStart < 1500 && readButton() == 0) delay(10);

    if(desRebuildTable(env))
    {
      gSprite.setTextColor(COL_OK, COL_PANEL);
      gSprite.setTextDatum(TC_DATUM);
      gSprite.drawString("Repaired, restarting", 160, 78, FONT_SMALL);
      gSprite.setTextDatum(TL_DATUM);
      gSprite.pushSprite(0, 0);
      delay(800);
      ESP.restart();
    }

    uiBootWarning("FLASH CHECK",
                  "Partitions: WRONG", COL_WARN,
                  "Repair FAILED", COL_WARN,
                  "Check the image");
    return;
  }

  Serial.printf("self check: bootloader MISMATCH (%lu ms)\n",
                (unsigned long)elapsed);
  uiBootWarning("FLASH CHECK",
                "Bootloader: MISMATCH", COL_WARN,
                "Partitions: OK", COL_OK,
                "Reflash bootloader");
}

// A valid ESP32 application image starts with 0xE9 and declares 1..16
// segments; an erased slot reads 0xFF.
static bool appImageOk(const esp_partition_t *part)
{
  if(!part) return false;
  uint8_t hdr[24];
  if(esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK) return false;
  return hdr[0] == 0xE9 && hdr[1] >= 1 && hdr[1] <= 16;
}

static bool setBootSlot(int targetSlot);

static bool bootToApp(esp_partition_subtype_t appSubtype)
{
  const esp_partition_t *app = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, appSubtype, NULL);

  // Checked on every way of leaving for an app (auto boot, Boot App0/1 and
  // hold mode), so an empty slot warns instead of dropping into the
  // ROM loader.
  if(!appImageOk(app))
  {
    const char *name = (appSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
      ? "APP0" : "APP1";
    Serial.printf("boot blocked: %s has no valid image\n", name);
    uiBootWarning(name, "Slot is empty or", COL_WARN,
                  "not flashed correctly", COL_WARN,
                  "Slot must be flashed first");
    return false;                           // stay in the recovery
  }

  // The environment is a property of the slot, so the table is refreshed
  // before the slot is committed: patch, then otadata, then restart.
  if(!setBootSlot(appSubtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? 0 : 1))
  {
    uiWarn("Env switch failed");
    return false;                           // stay in the recovery
  }

  ESP.restart();
  return true;
}

// Selects a boot slot and keeps the partition table in step with it, which is
// what makes App0 and App1 independent: App0 always reads settings0 and App1
// always reads settings1, whichever path chose the slot. Returns false when
// the environment could not be switched, before any slot state is touched.
static bool setBootSlot(int targetSlot)
{
  if(!patchPartitions((uint8_t)targetSlot))
  {
    Serial.printf("slot %d: environment switch failed\n", targetSlot);
    return false;
  }

  esp_partition_subtype_t sub = (targetSlot == 0)
    ? static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0)
    : static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1);
  const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, NULL);
  bool ok = p && (esp_ota_set_boot_partition(p) == ESP_OK);
  // Recorded here so every path that commits a slot (auto boot, menu, web)
  // updates the value setup() reads on the next power-up.
  if(ok) saveBootSlot((uint8_t)targetSlot);
  return ok;
}

static bool bootToApp0()
{
  return bootToApp(static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0));
}

static bool bootToApp1()
{
  return bootToApp(static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_1));
}

static void slotInfoRow(int y, const char *key, const String &value)
{
  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_PANEL);
  gSprite.drawString(key, 16, y, FONT_TINY);
  String shown = value;
  if(shown.length() > 32) shown = shown.substring(0, 31) + ".";
  gSprite.setTextDatum(TR_DATUM);
  gSprite.setTextColor(COL_TEXT, COL_PANEL);
  gSprite.drawString(shown.c_str(), 304, y, FONT_TINY);
  gSprite.setTextDatum(TL_DATUM);
}

static void runSlotInfo(bool isApp1)
{
  gSprite.fillScreen(COL_BG);
  drawHeader(isApp1 ? "APP1 INFO" : "APP0 INFO");

  const esp_partition_t *p = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP,
    static_cast<esp_partition_subtype_t>(
      isApp1 ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0),
    NULL);
  esp_app_desc_t d;
  bool ok = p && appImageOk(p) && esp_ota_get_partition_description(p, &d) == ESP_OK;

  gSprite.fillRoundRect(6, 24, 308, 116, 8, COL_PANEL);
  gSprite.drawRoundRect(6, 24, 308, 116, 8, COL_LINE);

  if(!ok)
  {
    gSprite.setTextDatum(TC_DATUM);
    gSprite.setTextColor(COL_MUTED, COL_PANEL);
    gSprite.drawString("No firmware in this slot", 160, 72, FONT_SMALL);
    gSprite.setTextDatum(TL_DATUM);
  }
  else
  {
    String file = loadFwFile(isApp1 ? 1 : 0);
    uint32_t installed = loadFwTime(isApp1 ? 1 : 0);
    if(loadFwVer(isApp1 ? 1 : 0) != String(d.version))
    {
      file = "";
      installed = 0;
    }
    char built[32];
    snprintf(built, sizeof(built), "%s %s", d.date, d.time);

    String when = "-";
    if(installed)
    {
      time_t t = (time_t)installed;
      struct tm tmv;
      if(localtime_r(&t, &tmv))
      {
        char tb[24];
        snprintf(tb, sizeof(tb), "%04d-%02d-%02d %02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min);
        when = tb;
      }
    }

    slotInfoRow(32, "NAME", d.project_name);
    slotInfoRow(46, "VERSION", d.version);
    slotInfoRow(60, "BUILT", built);
    slotInfoRow(74, "IDF", d.idf_ver);
    slotInfoRow(88, "SLOT SIZE", String(p->size / (1024 * 1024)) + " MB");
    slotInfoRow(102, "FILE", file.length() ? file : "-");
    slotInfoRow(116, "INSTALLED", when);
  }

  uiHintBar("", "HOLD=BACK");
  gSprite.pushSprite(0, 0);

  while(true)
  {
    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD) return;
    delay(10);
  }
}

static void drawBootModeMenu(bool isApp1, int selected)
{
  static const int icons[BOOT_MODE_COUNT] = {ICON_BOOT, ICON_HOLD, ICON_ABOUT};
  static const uint16_t accs[BOOT_MODE_COUNT] = {COL_OK, COL_GOLD, COL_ACC};

  static const char *desc[BOOT_MODE_COUNT] = {
    "Boots the app normally",
    "Press and keep holding",
    "Version, build, file, install"
  };

  drawTilePage(isApp1 ? "BOOT APP1" : "BOOT APP0", NULL,
               desc[selected], "CLICK=OPEN  HOLD=BACK",
               bootModeMenu, icons, accs, BOOT_MODE_COUNT, selected);
}

// Hold mode: only a notice, then the app boots with the encoder button still
// down so the target firmware sees a plain press. Nothing here looks at the
// button, so holding it never reboots the device and never re-enters the
// recovery menu.
static void runHoldModeBoot(bool isApp1)
{
  gSprite.fillScreen(COL_BG);
  drawHeader("HOLD MODE", isApp1 ? "App1" : "App0");
  gSprite.setTextDatum(TC_DATUM);
  gSprite.setTextColor(COL_OK, COL_BG);
  gSprite.drawString("Press and keep holding", 160, 44, FONT_SMALL);
  gSprite.setTextColor(COL_TEXT, COL_BG);
  gSprite.drawString("Do not release the button", 160, 110, FONT_SMALL);
  gSprite.setTextColor(COL_MUTED, COL_BG);
  gSprite.drawString("Booting to the app...", 160, 152, FONT_SMALL);
  gSprite.drawRoundRect(28, 134, 264, 16, COL_MUTED);

  uint32_t heldSince = 0;
  int lastFrame = -1;

  while(true)
  {
    bool down = (digitalRead(ENCODER_PUSH_BUTTON) == LOW);
    uint32_t el = 0;

    if(down)
    {
      if(!heldSince) heldSince = millis();
      el = millis() - heldSince;
      if(el >= HOLD_NOTICE_MS) break;
    }
    else
    {
      heldSince = 0;
    }

    int frame = (down ? 2 : 0) + (int)((millis() / 400) & 1);
    if(frame != lastFrame)
    {
      lastFrame = frame;
      bool blink = frame & 1;
      if(down)
      {
        gSprite.fillRoundRect(6, 66, 308, 40, 6, blink ? COL_KEY : COL_WARN);
        gSprite.setTextColor(blink ? COL_OK : COL_TEXT, blink ? COL_KEY : COL_WARN);
        gSprite.drawString("KEEP HOLDING", 160, 70, FONT_LARGE);
      }
      else
      {
        gSprite.fillRoundRect(6, 66, 308, 40, 6, COL_KEY);
        gSprite.setTextColor(blink ? COL_OK : COL_TEXT, COL_KEY);
        gSprite.drawString("PRESS NOW", 160, 70, FONT_LARGE);
      }
    }

    uint32_t w = down ? (260UL * el / HOLD_NOTICE_MS) : 0;
    if(down && w < 2) w = 2;
    gSprite.fillRoundRect(30, 136, 260, 12, 5, COL_KEY);
    if(w) gSprite.fillRoundRect(30, 136, w, 12, 5, COL_OK);

    gSprite.pushSprite(0, 0);
    delay(10);
  }

  Serial.printf("step: hold mode -> boot App%d\n", isApp1 ? 1 : 0);
  if(!(isApp1 ? bootToApp1() : bootToApp0())) return;   // warned -> back to the menus
}

static void runBootModeMenu(bool isApp1)
{
  int selected = 0;
  drawBootModeMenu(isApp1, selected);

  while(true)
  {
    int8_t dir = readEncoder();
    if(dir)
    {
      selected += dir;
      while(selected < 0) selected += BOOT_MODE_COUNT;
      while(selected >= (int)BOOT_MODE_COUNT) selected -= BOOT_MODE_COUNT;
      drawBootModeMenu(isApp1, selected);
    }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_NONE) { delay(10); continue; }
    if(ev == BTN_HOLD) return;                        // hold = back

    if(selected == 0)
    {
      // Default boot starts the app only once the button is released, so the
      // application is not launched with the press that chose it still down.
      while(digitalRead(ENCODER_PUSH_BUTTON) == LOW) delay(10);
      delay(30);
      bool ok = isApp1 ? bootToApp1() : bootToApp0();
      if(!ok) return;                      // empty slot -> back to main menu
    }
    else if(selected == 1) runHoldModeBoot(isApp1);
    else
    {
      runSlotInfo(isApp1);
      drawBootModeMenu(isApp1, selected);
    }
  }
}

