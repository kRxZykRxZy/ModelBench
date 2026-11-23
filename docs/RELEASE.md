# Release Process

## Overview
ModelBench releases are built via GitHub Actions (`.github/workflows/release.yml`) on tag push (`v*`).

## Versioning
- Bump `project(ModelBench VERSION x.y.z)` in `CMakeLists.txt`.
- Tag: `git tag vX.Y.Z && git push --tags`.

## Build (local)
- Windows: `scripts/build_windows.ps1` then `scripts/package/windows/package.ps1`
- macOS: `scripts/build_macos.sh` then `scripts/package/macos/package.sh`

## Code signing
- Windows: Authenticode via `scripts/signing/windows_sign.ps1` (requires PFX).
- macOS: Developer ID + notarization via `scripts/signing/macos_sign.sh`.
- Configure secrets in GitHub: `WINDOWS_CERT_PFX` (base64), `WINDOWS_CERT_PASSWORD`,
  `MACOS_SIGN_IDENTITY`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`.

## Auto-update
- Updater (`app/core/Updater`) checks an HTTPS manifest (`update-manifest.json`).
- Manifest lists per-platform `{url, sha256, signature, size}`.
- HTTPS is enforced for manifest and asset URLs; SHA-256 and signature (embedded
  public key, verified with OpenSSL when available) must pass before install.
- Release assets must include `update-manifest.json` with a signature produced
  by the release signing private key. Generate signatures with the tooling in
  `scripts/signing/`.

## Manifest template
See `scripts/release/update-manifest.template.json`.<|end|><|start|>write to=assistant<|channel|>analysis<|message|>Created file successfully: docs/RELEASE.md