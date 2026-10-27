# -*- coding: utf-8 -*-
"""릴리즈 패키징: docs/download, releasev1, manifest, SHA256, landing."""
import hashlib
import io
import json
import os
import shutil

VER = "1.1.22"
ROOT = os.getcwd()
BUILD = os.path.join(ROOT, "build")
DOCS = os.path.join(ROOT, "docs")
DL = os.path.join(DOCS, "download")
REL = os.path.join(ROOT, "releasev1")
SRC = os.path.join(BUILD, "miniradio.bin")

assert os.path.exists(SRC), "build/miniradio.bin missing"

ota_name = "GNBD_miniradio_%s_ota.bin" % VER
app_name = "GNBD_miniradio_%s_app.bin" % VER

# 1) build/ 에 릴리즈 이름으로 복사
for name in (ota_name, app_name):
    shutil.copyfile(SRC, os.path.join(BUILD, name))

# 2) docs/download + releasev1
os.makedirs(DL, exist_ok=True)
os.makedirs(REL, exist_ok=True)
for name in (ota_name, app_name):
    src = os.path.join(BUILD, name)
    shutil.copyfile(src, os.path.join(DL, name))
    shutil.copyfile(src, os.path.join(REL, name))
    h = hashlib.sha256(open(src, "rb").read()).hexdigest()
    io.open(os.path.join(REL, name + ".sha256"), "w", encoding="utf-8",
            newline="\n").write("%s  %s\n" % (h, name))

# 3) SHA256SUMS.txt 갱신
sums_path = os.path.join(DL, "SHA256SUMS.txt")
lines = []
if os.path.exists(sums_path):
    for line in io.open(sums_path, encoding="utf-8").read().splitlines():
        if VER in line:
            continue
        lines.append(line)
for name in (ota_name, app_name):
    h = hashlib.sha256(open(os.path.join(DL, name), "rb").read()).hexdigest()
    lines.append("%s  %s" % (h, name))
io.open(sums_path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")

# 4) manifest.json
mf_path = os.path.join(DOCS, "manifest.json")
mf = json.load(io.open(mf_path, encoding="utf-8"))
mf["version"] = VER
for build in mf.get("builds", []):
    for part in build.get("parts", []):
        p = part.get("path", "")
        if p.startswith("download/GNBD_miniradio_") and p.endswith("_app.bin"):
            part["path"] = "download/" + app_name
io.open(mf_path, "w", encoding="utf-8", newline="\n").write(
    json.dumps(mf, ensure_ascii=False, indent=2) + "\n")

print("packaged", VER)
print("ota size", os.path.getsize(os.path.join(DL, ota_name)))
