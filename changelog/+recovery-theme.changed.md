Rebuild the recovery UI on the shared UiKit tile shell: pure black background, a
24px header showing the title, slot chip, network icon and the **battery voltage**
(read from GPIO4 with the main firmware's conversion, green/amber/red by level),
and a hint bar on every screen. The header and hint bar are black and the accent
colour is sage green instead of blue. **Every selection screen is a tile grid** -
main menu, Settings, WiFi, boot mode, firmware target/source pickers and Erase -
while the WiFi scan list, file list and URL picker keep their row layout.

**There is no Back row any more and the original controls are restored: holding
the button selects or opens, a short click goes back.** WiFi scan and keyboard
screens keep their own behaviour (click = type/back, hold = connect/OK). On the
main menu a click does nothing, and in Erase clicking toggles a checkbox while
holding erases (or backs out when nothing is checked). The Yes/No popup is drawn
once and only repaints its buttons when the selection moves, which removes the
flicker; hold confirms, click cancels. Brightness is adjustable from Settings and
still stored in the `settings` partition.

On every boot the recovery runs a fast check of the **bootloader at flash
offset 0x0 (CRC32 of the shipped bootloader.bin) and the partition table**
while the splash is up; a pass continues silently and only a failure pops up
the warning. Booting into an app slot first checks that the slot holds a
valid image and blocks the boot with a warning when it is empty.

Fixes and control changes from the first pass: the self check hashed 20224 bytes
against a 20000 byte CRC and therefore warned on every boot (the tail chunk is
now clamped); choosing an empty slot warned and then dropped straight into hold
mode (the hold screen ran even after the boot failed); a hold now triggers as
soon as the threshold is reached instead of waiting for the release, and the same
press never fires twice. The Yes/No popup marks the focused button in green, and
the blue-tinted cards, tiles and keys became neutral charcoal so the UI reads
monochrome with sage green accents. Menu order is App0, App1, Firmware Update,
Erase, Settings, and Settings lists WiFi, Brightness, About.

Screens are now rendered into an off-screen sprite (PSRAM) and pushed to the LCD
once complete, so redraws no longer flicker. The boot mode screen asks no Yes/No
question any more - holding BOOT or HOLD MODE goes straight to the action. Erase
lists App0, App1, Factory Reset, LittleFS with Factory Reset in the default
accent colour, and the splash shows the project URL in the space where the
"hold encoder" hint used to be (title stays at its original size). The About
partition page lays nine entries out in two columns of five so none falls into
the hint bar, and the long URL in the firmware picker marquee now repaints.

A review after the sprite move found five screens that drew into the sprite but
never pushed it, so they stayed blank: the **hold mode** progress screen, the
download screen's header, the no-URLs notice and both download-failure notices.
All now push, and choice rows clip the text to the row box so a long item cannot
spill out before its marquee starts. The progress panel now adds a second line
saying what is happening: the file name while downloading, and "file -> App0/1"
while flashing. The faint dot grid behind the screens was removed, leaving a
plain black background.

**Needs a hardware check of every screen** (tile menus, menu order, header
battery voltage, splash, Yes/No popup focus, empty-slot warning, self-check
popup only on failure with the test bootloader, hold-to-enter feel, brightness,
firmware update, Erase, WiFi pages, keyboard, About and boot screens).
