#!/usr/bin/env bash
set -euo pipefail

# Code-sign a macOS .app/.dmg with Developer ID and notarize.
# Usage: scripts/signing/macos_sign.sh <path> [identity]
# Env: MACOS_SIGN_IDENTITY, APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD

TARGET="${1:?target required}"
IDENTITY="${2:-${MACOS_SIGN_IDENTITY:-}}"
if [[ -z "$IDENTITY" ]]; then
  echo "Set MACOS_SIGN_IDENTITY or pass identity" >&2
  exit 1
fi

# Sign nested binaries first, then the bundle.
codesign --force --options runtime --timestamp --sign "$IDENTITY" --deep "$TARGET"
codesign --verify --deep --strict --verbose=2 "$TARGET"

# Notarize if credentials provided
if [[ -n "${APPLE_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
  if [[ "$TARGET" == *.dmg || "$TARGET" == *.zip ]]; then
    xcrun notarytool submit "$TARGET" --apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$APPLE_TEAM_ID" --wait
    xcrun stapler staple "$TARGET"
  fi
fi
echo "Signed: $TARGET"