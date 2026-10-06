#!/bin/sh
# Builds and runs the Pinboard tests. No device and no PlatformIO:
# PinboardCore and PinboardSaved are freestanding C++17.
#
#   host-tests/pinboard/run.sh
#
# Nothing but the standard library and lib/Utf8 is on the include path, which is
# the point. If the token parser, the list-query builder or the saved index ever
# reach for HttpDownloader, ArduinoJson or the SD card, this build fails loudly
# instead of the logic quietly becoming device-only and therefore untested.
# lib/Utf8 is on it because display titles fold typographic punctuation to the
# ASCII the reading cut can actually draw, the same way Hacker News's tests
# build it.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name, so two worktrees cannot
# share -- and pass -- a binary only one of them built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-pinboard-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

# The freestanding brains: token, list query, bookmark model.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../lib/Utf8 \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/apps_local/pinboard/PinboardCore.cpp \
  test_core.cpp -o "$BUILD_DIR/test_core"
"$BUILD_DIR/test_core"

# The saved library: the piece that has to keep working when everything around
# it does not. No network, no service, and a reader should still open the device
# and find their articles.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../lib/Utf8 \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/apps_local/pinboard/PinboardSaved.cpp \
  test_saved.cpp -o "$BUILD_DIR/test_saved"
"$BUILD_DIR/test_saved"
