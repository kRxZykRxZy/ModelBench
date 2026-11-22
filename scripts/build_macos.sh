#!/usr/bin/env bash
set -euo pipefail

PRESET="${1:-macos-arm64}"
QT_PATH="${QT_PATH:-/opt/Qt/6.12.0/macos}"

cmake --preset "$PRESET" -DCMAKE_PREFIX_PATH="$QT_PATH"
cmake --build --preset "$PRESET"
ctest --preset "$PRESET"
