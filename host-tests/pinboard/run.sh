#!/bin/sh
# Builds and runs the Pinboard tests. No device and no PlatformIO:
# PinboardCore is freestanding C++17 (standard library only), the same contract
# host-tests/hackernews/ holds its core to. If config parsing or URL building
# ever reaches for HttpDownloader, ArduinoJson or the SD card, this build fails
# loudly instead of the logic quietly becoming device-only and untested.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-pinboard-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/pinboard/PinboardCore.cpp \
  test_pinboard.cpp -o "$BUILD_DIR/test_pinboard"
"$BUILD_DIR/test_pinboard"
