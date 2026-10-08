#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
UNITS="ansi termbuf prompt calib keyboard layout gesture bytering"
SRCS=""
for u in $UNITS; do [ -f "src/$u.cpp" ] && SRCS="$SRCS src/$u.cpp"; done
mkdir -p build
g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -pthread -Isrc -Itest test/*.cpp $SRCS -o build/host-tests
./build/host-tests
