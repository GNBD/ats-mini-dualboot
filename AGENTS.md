# AGENTS.md

Boot manager firmware for the ATS Mini ESP32-S3/SI4732 receiver.

- Boot managers live in `ats-mini-recovery-beta/` (16MB N16R8) and
  `ats-mini-recovery-lite/` (8MB N8R2). The two partition tables must never be
  mixed.
- Keep `bootloader.bin` next to the `.ino` in each sketch folder; the Arduino
  ESP32 build picks it up as the recovery-first bootloader automatically.
- Follow surrounding C++ style; avoid unrelated reformatting.
- Internal DRAM is scarce; PSRAM is plentiful. Balance memory savings with
  performance, safety, and simplicity.
- Run repository checks: `uv run prek run --all-files`.
- For user-visible firmware changes, edit an existing unreleased Towncrier
  fragment for the same feature in `changelog/`, or create one if none exists.
  See `pyproject.toml` for categories.
- Update documentation when user-facing behavior changes. Stay brief, use the
  existing writing style.
- The theme boot manager is in `ats-mini-recovery-theme/`; its web docs live in
  `docs/` and are published with GitHub Pages (`/docs`). On a new release,
  refresh the one-click flasher by replacing `docs/firmware/` files and editing
  `docs/firmware/manifest.json` (version + names); no script changes needed.
- Do not compile anything unless asked.
- Report validation performed and any behavior needing hardware testing.
