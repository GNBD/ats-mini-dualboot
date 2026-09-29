# Custom bootloader (recovery-first boot)

`ats-mini-recovery-beta/bootloader.bin` is a modified ESP-IDF second-stage
bootloader. When it is present in the sketch directory, the Arduino ESP32 build
copies it as the bootloader instead of the stock one.

## What it changes

The stock bootloader boots whatever OTA slot the `otadata` partition points to.
The ATS Mini application normally sets the recovery slot (`ota_2`) as the next
boot target itself, so the recovery-first behaviour breaks as soon as a firmware
that does not do that is flashed into `app0`/`app1`.

The modified bootloader enforces the order in `bootloader_utility_get_selected_boot_partition()`:

- If the selected OTA slot is not `ota_2` and its `otadata` state is not
  `ESP_OTA_IMG_NEW`, boot `ota_2` (the recovery) instead.
- Otherwise keep the normal selection. The recovery starts an application with
  `esp_ota_set_boot_partition()`, which marks it `ESP_OTA_IMG_NEW`, so it is
  allowed to run as a one-shot boot.
- When `otadata` is empty, boot `ota_2` if the recovery partition exists.

Result: `power on -> recovery -> application`, regardless of the firmware in
`app0`/`app1`.

## Building

The bootloader must match the ESP-IDF version and flash configuration used by the
Arduino core. This project targets **ESP-IDF v5.5.5** and the board settings:

```
FQBN: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,CPUFreq=80,
      USBMode=hwcdc,FlashMode=qio,PartitionScheme=custom,DebugLevel=none
platform: esp32:esp32 (3.3.12)
```

1. Install ESP-IDF v5.5.5.
2. Apply `bootloader.patch` to the ESP-IDF source:
   ```
   cd $IDF_PATH
   git apply /path/to/ats-mini-recovery-beta/bootloader.patch
   ```
3. Build the bootloader for a project whose `sdkconfig` matches the board
   settings above, then copy the result to `ats-mini-recovery-beta/bootloader.bin`:
   ```
   idf.py bootloader
   cp build/bootloader/bootloader.bin /path/to/ats-mini-recovery-beta/bootloader.bin
   ```
4. Rebuild the firmware with `arduino-cli`; the custom bootloader is copied
   automatically (see the `recipe.hooks.prebuild` rule in the ESP32 core's
   `platform.txt`).

### N8R2 (8MB) bootloader

`ats-mini-recovery-lite/bootloader.bin` is the same patch built for the 8MB
layout in `ats-mini-recovery-lite/partitions.csv`, so that the image header
reports the real flash size of an N8R2 board:

1. Apply `bootloader.patch` to a clean ESP-IDF v5.5.5 tree (step 2 above).
2. Create a minimal IDF project with this `sdkconfig.defaults`:
   ```
   CONFIG_IDF_TARGET="esp32s3"
   CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
   CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
   CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
   ```
3. `idf.py bootloader`, then copy `build/bootloader/bootloader.bin` to
   `ats-mini-recovery-lite/bootloader.bin`.

The 8MB image reports `SPI Flash Size : 8MB` at boot and was verified on
hardware with the recovery-first cycle
(`power on -> 0x660000 recovery -> app0`).

## Notes

- The patch only touches `components/bootloader_support/src/bootloader_utility.c`.
- `bootloader.bin` is committed so that CI and normal builds use it without
  requiring an ESP-IDF installation.
  - The bootloader is ESP-IDF (v5.5.5) code and is distributed under the
    Apache License 2.0 (see the repository `NOTICE` and
    `LICENSES/Apache-2.0.txt`).
