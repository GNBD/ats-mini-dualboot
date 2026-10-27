# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 앱 버전 문자열(스플래시/Info 표시). 지시 없으면 close, 배포판은 stable.
# CMake project(VERSION ...)은 숫자만 허용하므로 분리해서 둔다.
set(PROJECT_VER "stable 1.1.22")
project(miniradio VERSION 1.1.22)
