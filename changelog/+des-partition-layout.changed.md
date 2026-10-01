The theme boot manager becomes **v4.0.0 (DES)** and moves to the Dual
Environment partition table. `nvs` through `recovery` keep their v3.x offsets,
`littlefs` shrinks from 6MB to 5.58MB to make room for a recovery-only
`rec_settings` NVS, a 128KB `settings` region per environment, the two raw
DES metadata slots and the backup table at `0xFFF000`. The splash now labels
the build `v4.0.0 (DES)`.

Both environments address their settings through the same `settings` label,
so app0 and app1 resolve different NVS regions without either application
changing: switching environments only rewrites the one `settings` offset in
the table at `0x8000` and recomputes its MD5.

The splash checks the flash while it is up, on every boot and inside a second:
a CRC32 over the 20000 byte bootloader plus an entry by entry comparison of
the slot at `0x8000` against a copy of the canonical table compiled into the
recovery. A bootloader that does not match can only warn, but a partition
table that differs is rebuilt from that embedded copy and the device restarts
onto it, so flashing the wrong table no longer needs a full `erase-flash` to
recover. Only the `settings` offset is allowed to differ, and only between the
two DES environments.

The active environment follows the boot slot, which is what makes **App0 and
App1 independent**: `Boot App0`, `Boot App1`, hold mode, the web `Boot App0/1`
actions and the automatic boot after the one second encoder window all refresh
the table's `settings` offset before the slot is committed. App0 therefore
always reads `settings0` and App1 always reads `settings1`, without either
application changing. A switch that cannot be verified warns and leaves the
recovery in place without committing the slot.

The slot itself is remembered in the recovery's own settings rather than in
`otadata`, because the bootloader patch forces the recovery on every power-up
and the recovery rewrites `otadata` to its own slot as it starts. Reading it
back therefore returned the recovery, which falls back to App0, and a power
cycle lost the application that was running. Every path that commits a slot
now also records it under `bootcfg/slot`, and the boot manager reads that value
for `Current slot`, so booting App1 keeps booting App1. A device with no record
yet still falls back to `otadata`, which keeps the first boot after flashing
behaving as before.

Upgrading from v3.2.1 needs the new table flashed at `0x8000`. The stale
`littlefs` superblock that used to trip a `lfs_fs_grow_` assert and reboot the
board forever is now caught from the splash too: the recovery reads the 32 byte
superblock header, and when the volume reports more blocks than the partition
holds it asks once (`Old format found` / `Erase`) and wipes the region with a
progress bar before anything mounts it. A volume whose header looks correct but
still claims more space than the partition is caught the same way: it is probed
once with growth disabled, because that mismatch only shows up when the volume
is mounted. There is no way to decline, because the
application cannot boot onto that volume either; a failed erase leaves the
device in the menu with `littlefs` unmounted instead of starting a boot loop.

The two new NVS labels get the same treatment one level down. On that same
upgrade `settings` and `rec_settings` also land on the old littlefs, so their
first pages hold file data rather than NVS pages: `Preferences` then refuses to
open and every write is dropped for good, with no crash and no warning to show
for it. Both are opened on the splash, and only when init itself refuses is
the partition erased behind a single button (`No free pages` / `New version
found` / `Init failed`) and re-created. A label that opens normally is never
touched, so a full but healthy `settings` keeps its contents.

The recovery moves its own `wificfg` / `uicfg` onto `rec_settings`
(`STORAGE_PARTITION`, the single line v4.x allows), so the recovery keeps its
WiFi and brightness settings no matter which environment an app boots with.

`Erase` becomes a two level menu. `Factory Reset` offers `App0 cfg`,
`App1 cfg` and `Recovery`, which wipe the two DES `settings` regions
outright plus `rec_settings`; the shared `nvs` at `0x9000` is left alone.
`Erase` offers `App0`, `App1` and `LittleFS` and wipes those partitions as
before. Both lists run behind one confirm dialog and restart when done.
