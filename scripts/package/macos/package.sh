#!/usr/bin/env bash
set -euo pipefail

# Package ModelBench for macOS: macdeployqt + llama libs + .app/.dmg.
# Usage: scripts/package/macos/package.sh [build_dir] [qt_path] [output_dir]

BUILD_DIR="${1:-build/macos-arm64}"
QT_PATH="${2:-/opt/Qt/6.12.0/macos}"
OUTPUT_DIR="${3:-dist/macos}"

REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO_ROOT"

BUILD_PATH="$REPO_ROOT/$BUILD_DIR"
APP="$BUILD_PATH/ModelBench.app"
if [[ ! -d "$APP" ]]; then
  echo "ModelBench.app not found at $APP" >&2
  exit 1
fi

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
STAGE="$OUTPUT_DIR/ModelBench.app"
cp -R "$APP" "$STAGE"

# Qt deployment
MACDEPLOYQT="$QT_PATH/bin/macdeployqt"
if [[ ! -x "$MACDEPLOYQT" ]]; then
  echo "macdeployqt not found at $MACDEPLOYQT" >&2
  exit 1
fi
"$MACDEPLOYQT" "$STAGE" -always-overwrite -qmldir="$REPO_ROOT/app/ui"

# Copy llama runtime libs if present
find "$BUILD_PATH" -name "libllama*.dylib" -o -name "libggml*.dylib" 2>/dev/null | while read -r f; do
  cp -f "$f" "$STAGE/Contents/Frameworks/" 2>/dev/null || cp -f "$f" "$STAGE/Contents/MacOS/"
done

# Icon (optional)
if [[ -f "$REPO_ROOT/assets/icon.icns" ]]; then
  cp "$REPO_ROOT/assets/icon.icns" "$STAGE/Contents/Resources/"
fi

# Fix rpaths for bundled libs
install_name_tool -add_rpath "@executable_path/../Frameworks" "$STAGE/Contents/MacOS/ModelBench" 2>/dev/null || true

# Codesign (ad-hoc by default; set SIGN_IDENTITY env for real signing)
SIGN_IDENTITY="${SIGN_IDENTITY:--}"
codesign --force --deep --sign "$SIGN_IDENTITY" "$STAGE" || echo "codesign failed (continuing)"

# Create DMG
DMG="$OUTPUT_DIR/ModelBench.dmg"
hdiutil create -volname "ModelBench" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Packaging complete: $OUTPUT_DIR"