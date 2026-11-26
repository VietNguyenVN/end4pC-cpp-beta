#!/usr/bin/env bash
# Builds and runs the Tools tab order test (src/tools_tabs.h). Also run by `meson test -C build`.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++20 -O1 -Wall -Wextra -fsanitize=address,undefined "$here/test_tools_tabs.cpp" -o "$out/test_tools_tabs"
"$out/test_tools_tabs"
