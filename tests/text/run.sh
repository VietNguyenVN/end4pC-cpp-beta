#!/usr/bin/env bash
# Builds and runs the single-line text selection test (src/textsel.h). Also run by `meson test -C build`.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++20 -O1 -Wall -Wextra -fsanitize=address,undefined "$here/test_textsel.cpp" -o "$out/test_textsel"
"$out/test_textsel"
