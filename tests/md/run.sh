#!/usr/bin/env bash
# Builds and runs the Markdown formatting test (src/mdedit.h). Also run by `meson test -C build`.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++20 -O1 -Wall -Wextra -fsanitize=address,undefined "$here/test_mdedit.cpp" -o "$out/test_mdedit"
"$out/test_mdedit"
