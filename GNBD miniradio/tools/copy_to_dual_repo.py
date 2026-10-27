# -*- coding: utf-8 -*-
"""main1 소스를 ats-mini-dualboot/'GNBD miniradio'로 복사 (빌드/배포 산출물 제외)."""
import os
import shutil

SRC = r"C:\Users\Administrator\Desktop\Development\extfirm\main1"
DST = r"C:\Users\Administrator\Desktop\Development\extfirm\_push_tmp\GNBD miniradio"

EXCLUDE_DIRS = {".git", "build", "managed_components", "releasev1", "archive",
                "NEWPROJECT", "docs"}
EXCLUDE_FILES = {"sdkconfig"}          # 로컬 설정은 제외(기본값만)
SKIP_EXT = {".bin", ".elf", ".o", ".a"}

def copy_tree(src, dst, rel=""):
    for name in os.listdir(src):
        if rel == "" and name in EXCLUDE_DIRS:
            continue
        if rel == "" and name in EXCLUDE_FILES:
            continue
        s = os.path.join(src, name)
        d = os.path.join(dst, name)
        if os.path.isdir(s):
            os.makedirs(d, exist_ok=True)
            copy_tree(s, d, os.path.join(rel, name))
        else:
            if os.path.splitext(name)[1].lower() in SKIP_EXT:
                continue
            if name == "repomix-output.xml":
                continue
            shutil.copyfile(s, d)

# 기존 소스 폴더 비우기(README/LICENSE 등 저장소 전용 파일은 유지)
KEEP = {"README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", ".gitignore"}
for name in os.listdir(DST):
    if name in KEEP:
        continue
    p = os.path.join(DST, name)
    if os.path.isdir(p):
        shutil.rmtree(p)
    else:
        os.remove(p)

copy_tree(SRC, DST)
print("copied")
