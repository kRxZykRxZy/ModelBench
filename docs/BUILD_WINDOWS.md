# Build on Windows (MinGW)

## Prerequisites
- Windows 10/11 x64
- MinGW-w64 GCC (e.g. winlibs) on PATH
- CMake >= 3.21
- Ninja
- Qt 6.12 LTS for MinGW 64-bit

Install Qt via aqtinstall:
```
pip install aqtinstall
aqt install-qt windows desktop 6.12.0 win64_mingw -O C:\Qt
```

## Configure
```
cmake --preset windows-mingw-debug -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64
```

## Build
```
cmake --build --preset windows-mingw-debug
```

## Tests
```
ctest --preset windows-mingw-debug
```

## Enable llama.cpp
```
git submodule update --init --recursive
cmake --preset windows-mingw-debug -DMODELFIT_ENABLE_LLAMA_CPP=ON
```
Requires the submodule and a compatible build of llama.cpp.
