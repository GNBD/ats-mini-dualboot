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
- Also update the fallback firmware list `NEED` near the top of `docs/install.html`
  when it still names the previous recovery file.
- New releases follow the fixed note format in "## Release notes" below, and ship
  these assets: `ats-mini-recovery-<ver>.bin`, `ats-mini-bootloader-v4.bin`,
  `ats-mini-partitions-v4.bin`, `boot_app0.bin`, `flash-recovery.ps1`.
- Do not compile anything unless asked.
- Report validation performed and any behavior needing hardware testing.

## Release notes

Use this exact shape (Korean body), matching the `v4.1.0` / `v4.1.1` releases:

```
# ats-mini dualboot (<ver> DES) — Boot Manager (테마)

### [원클릭 웹 플래셔 (설치 페이지): **https://gnbd.github.io/ats-mini-dualboot/install.html**](https://gnbd.github.io/ats-mini-dualboot/install.html)

<한 줄 소개: SI4732 (ESP32-S3 N16R8)용 듀얼 부트 매니저(리커버리) 테마>

> **테스트 버전 안내:** ...

소스: `ats-mini-dualboot/ats-mini-recovery-theme/ats-mini-recovery-theme.ino` / 버전: `<ver>`

| 빌드 | 값 |
|---|---|
| FQBN | `esp32:esp32:esp32s3` + `CDCOnBoot=cdc, USBMode=hwcdc, FlashMode=qio, FlashSize=16M, PSRAM=opi, PartitionScheme=custom` |
| 리커버리 이미지 | `ats-mini-recovery-<ver>.bin` — <size> B |
| MD5 | `<md5>` |
| 플래시 주소 | `0x860000` |

---

## 이번 버전에서 할 수 있는 것
<...>

참고(DES): app0는 ENV0 주소, app1은 ENV1 주소를 사용해 **두 앱의 설정이 분리**되어 있습니다.

---

## 리커버리(부트 매니저) 플래시 방법
<파일/주소 표 + esptool 명령>

자세한 사용은 소스의 `ats-mini-recovery-theme/README.md` 를 참고하세요.
```

- State plainly, in the body of "이번 버전에서 할 수 있는 것" (not as a small
  blockquote), when the feature set matches a previous version — e.g.
  "**기능은 4.1.0과 같습니다.** 이번 버전에서 추가된 것은 …뿐입니다."
- Keep the small blockquote for the beta/test warning only.
