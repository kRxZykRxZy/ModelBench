# Build on macOS

## Prerequisites
- macOS 12+ (Apple Silicon or Intel)
- Xcode command line tools
- CMake >= 3.21
- Ninja
- Qt 6.12 LTS for macOS

## Configure (Apple Silicon)
```
cmake --preset macos-arm64 -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/macos
```

## Configure (Intel)
```
cmake --preset macos-x86_64 -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/macos
```

## Build
```
cmake --build --preset macos-arm64
```

## Notes
- Metal backend preferred on Apple Silicon.
- Keychain used for secret storage.
- Universal binary only if all native deps (incl. llama.cpp) support both arches.
- Not verified on this Windows dev machine; CI required.
