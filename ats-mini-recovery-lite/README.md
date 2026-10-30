# ats-mini-recovery-lite

8MB (N8R2) minimal boot manager for the ATS Mini. This is the small
external-flash recovery; it does not do web/OTA self-updates.

- Hardware: ESP32-S3 **N8R2** (8MB flash + 2MB PSRAM)
- Recovery partition: **`0x660000`** (0x070000)
- App slots: `app0` `0x10000` / `app1` `0x290000`, 2.5MB each
- LittleFS: `0x6D0000` (0xF0000)
- Partitions: [`partitions.csv`](partitions.csv)
- Flasher: [`flash-lite.ps1`](flash-lite.ps1)
- Bootloader: [`bootloader.bin`](bootloader.bin)

> 8MB layout only. Never mix this table with the 16MB N16R8 layout.

## Flash

```powershell
.\flash-lite.ps1 -Install -Target 8MB -Port COM7
```

The bootloader image reports 8MB flash. See the sketch and repository `NOTICE`
for licensing.
