#!/usr/bin/env python3
"""
tools/update_landing.py - sync build number / size / version into docs/index.html

Reads the freshly built OTA image, extracts the ESP app descriptor date/time
(the same values the firmware prints on the splash and About screens) and
rewrites the landing page chips.

Usage:
    python tools/update_landing.py [path-to-bin]
Default: build/GNBD_miniradio_<version>_ota.bin if found, else build/miniradio.bin
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANDING = os.path.join(ROOT, "docs", "index.html")

MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def pick_bin(argv):
    if len(argv) > 1:
        return argv[1]
    build = os.path.join(ROOT, "build")
    found = []
    if os.path.isdir(build):
        for name in os.listdir(build):
            m = re.match(r"GNBD_miniradio_(\d+\.\d+\.\d+)_ota\.bin$", name)
            if m:
                found.append((tuple(int(x) for x in m.group(1).split(".")), name))
    if found:
        found.sort()
        return os.path.join(build, found[-1][1])
    return os.path.join(build, "miniradio.bin")


# ESP-IDF esp_app_desc_t starts at file offset 0x20:
#   0x20 magic, 0x24 secure_version, 0x28 reserv1[2],
#   0x30 version[32], 0x50 project_name[32], 0x70 time[16], 0x80 date[16]
APP_DESC_OFF = 0x20
DIGITS = re.compile(r"\d+")


def build_number(data):
    if len(data) < 0x90:
        return None
    magic = int.from_bytes(data[0x20:0x24], "little")
    if magic != 0xABCD5432:
        return None
    tm = data[0x70:0x80].split(b"\x00")[0].decode("latin-1")
    dt = data[0x80:0x90].split(b"\x00")[0].decode("latin-1")

    d = re.match(r"([A-Z][a-z]{2})\s+(\d{1,2})\s+(\d{4})$", dt.strip())
    t = re.match(r"(\d{1,2}):(\d{2}):(\d{2})$", tm.strip())
    if not d or not t or d.group(1) not in MONTHS:
        return None
    month = MONTHS.index(d.group(1)) + 1
    day = int(d.group(2))
    year = int(d.group(3))
    return "%02d%02d%02d%02d%02d%02d" % (year % 100, month, day,
                                         int(t.group(1)), int(t.group(2)),
                                         int(t.group(3)))


def main():
    path = pick_bin(sys.argv)
    if not os.path.isfile(path):
        print("missing binary:", path)
        return 1
    with open(path, "rb") as f:
        data = f.read()

    no = build_number(data)
    if not no:
        print("build number not found in", path)
        return 1

    size = len(data)
    with open(LANDING, encoding="utf-8") as f:
        html = f.read()

    before = html
    html = re.sub(r"BUILD \d{12}", "BUILD " + no, html)
    html = re.sub(r"[\d,]+ bytes", "{:,} bytes".format(size), html)

    if html != before:
        with open(LANDING, "w", encoding="utf-8", newline="") as f:
            f.write(html)

    print("bin       :", os.path.relpath(path, ROOT))
    print("build no  :", no)
    print("size      : {:,} bytes".format(size))
    print("landing   :", "updated" if html != before else "already up to date")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
