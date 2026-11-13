#!/bin/bash
# The Calendar's iCalendar parser, on this computer: ./run.sh [feed.ics ...]
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
CXX=${CXX:-c++}
FLAGS=(-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I../../sticky-os)
# Some macOS command line tools don't find libc++'s headers by themselves.
if ! echo '#include <new>' | "$CXX" -std=c++17 -x c++ -fsyntax-only - 2>/dev/null; then
  SDK=$(xcrun --show-sdk-path 2>/dev/null || true)
  [ -n "$SDK" ] && FLAGS+=(-isysroot "$SDK" -I"$SDK/usr/include/c++/v1")
fi
"$CXX" "${FLAGS[@]}" -o build/ics_test ics_test.cpp ../../sticky-os/ics.cpp
abs=()
for f in "$@"; do abs+=("$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"); done
./build/ics_test ${abs[@]+"${abs[@]}"}
