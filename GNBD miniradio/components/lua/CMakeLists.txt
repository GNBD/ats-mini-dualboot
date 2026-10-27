# SPDX-License-Identifier: MIT
# Lua 5.4.7 (MIT) - https://www.lua.org/
# 원본 소스 그대로 사용하며, 인터프리터(lua.c)/컴파일러(luac.c)는 제외한다.

set(LUA_SRCS
    "src/lapi.c"
    "src/lauxlib.c"
    "src/lbaselib.c"
    "src/lcode.c"
    "src/lcorolib.c"
    "src/lctype.c"
    "src/ldblib.c"
    "src/ldebug.c"
    "src/ldo.c"
    "src/ldump.c"
    "src/lfunc.c"
    "src/lgc.c"
    "src/linit.c"
    "src/liolib.c"
    "src/llex.c"
    "src/lmathlib.c"
    "src/lmem.c"
    "src/loadlib.c"
    "src/lobject.c"
    "src/lopcodes.c"
    "src/loslib.c"
    "src/lparser.c"
    "src/lstate.c"
    "src/lstring.c"
    "src/lstrlib.c"
    "src/ltable.c"
    "src/ltablib.c"
    "src/ltm.c"
    "src/lundump.c"
    "src/lutf8lib.c"
    "src/lvm.c"
    "src/lzio.c"
)

idf_component_register(
    SRCS ${LUA_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES esp_common
)

# 서드파티 코드: 경고는 억제하되 오류로 승격하지 않는다.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)

# ESP32-S3: 32비트 정수 + float 숫자형 (메모리/성능 우선)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    LUA_32BITS=1
    LUA_USE_C89=0
)
