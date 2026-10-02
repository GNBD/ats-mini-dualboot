# Firmware for the web flasher

These files are served to `docs/install.html` (same origin, so the browser can
read them — GitHub release assets cannot be fetched from a page because the
asset CDN sends no CORS header).

## Updating for a new release

1. Replace the files here with the new release's:
   - `ats-mini-bootloader-v4.bin`   → offset `0x0`
   - `ats-mini-partitions-v4.bin`   → offset `0x8000`
   - `ats-mini-recovery-<ver>.bin`  → offset `0x860000`
2. Edit `manifest.json`:
   - `version` → the new version string shown in the page footer.
   - `files[].name` → the new recovery file name.
   - `files[].offset` → the flash offsets (unchanged unless the layout changes).

The installer reads `manifest.json` at load time, so no JavaScript changes are
needed. If the manifest is missing it falls back to the names hard-coded in
`docs/install.html`.
