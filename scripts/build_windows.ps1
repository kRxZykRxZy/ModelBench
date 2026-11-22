param(
    [string]$Preset = "windows-mingw-debug",
    [string]$QtPath = "C:\Qt\6.12.0\mingw_64"
)

$ErrorActionPreference = "Stop"
$env:CMAKE_PREFIX_PATH = $QtPath

cmake --preset $Preset -DCMAKE_PREFIX_PATH=$QtPath
if ($LASTEXITCODE -ne 0) { throw "Configure failed" }

cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

ctest --preset $Preset
