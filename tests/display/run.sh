#!/usr/bin/env bash
# Builds and runs the display map geometry test (src/displaymap.h). Also run by `meson test -C build`.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++20 -O1 -Wall -Wextra -fsanitize=address,undefined "$here/test_displaymap.cpp" -o "$out/test_displaymap"
"$out/test_displaymap"
