#!/usr/bin/env bash
# Builds and runs the workspace groups of the bars test (src/wsmonitors.h). Also run by `meson test -C build`.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++20 -O1 -Wall -Wextra -fsanitize=address,undefined "$here/test_wsmonitors.cpp" -o "$out/test_wsmonitors"
"$out/test_wsmonitors"
