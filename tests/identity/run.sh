#!/usr/bin/env bash
# Builds and runs the display name and host name test (src/identity.h). Also run by `meson test -C build`.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
g++ -std=c++20 -O1 -Wall -Wextra -fsanitize=address,undefined "$here/test_identity.cpp" -o "$out/test_identity"
"$out/test_identity"
