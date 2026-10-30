# ats-mini-recovery-beta

Legacy 16MB (N16R8) boot manager for the ATS Mini. This folder is kept for
reference; the current boot manager is
[`ats-mini-recovery-DBFM`](../ats-mini-recovery-DBFM/README.md).

- Hardware: ESP32-S3 **N16R8** (16MB flash + 8MB PSRAM)
- Recovery partition: **`0x860000`** (0x1A0000)
- Boot flow: recovery-first → `app0` / `app1`
- Network update, WiFi settings, web server
- Partitions: [`partitions.csv`](partitions.csv)
- Bootloader: [`bootloader.bin`](bootloader.bin), build notes in
  [`bootloader.md`](bootloader.md)

> Legacy sketch. Its partition layout differs from DBFM; do not mix tables.

## Flash

```sh
uvx --from esptool esptool.py --chip esp32s3 --port COM7 --baud 460800 \
  write-flash 0x860000 ats-mini-recovery-beta.bin
```

See the source sketch header and the repository `NOTICE` for licensing.
