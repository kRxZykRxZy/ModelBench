<#
.SYNOPSIS
  Package ModelBench for Windows: windeployqt + llama libs + (optional) installer.
.PARAMETER BuildDir
  CMake build directory containing ModelBench.exe (default: build/windows-mingw-release)
.PARAMETER QtPath
  Qt installation root (default: C:\Qt\6.12.0\mingw_64)
.PARAMETER OutputDir
  Directory for the packaged output (default: dist/windows)
.PARAMETER Installer
  Installer backend: "nsis" (default), "inno", or "none" (portable zip only)
#>
param(
    [string]$BuildDir = "build/windows-mingw-release",
    [string]$QtPath = "C:\Qt\6.12.0\mingw_64",
    [string]$OutputDir = "dist/windows",
    [ValidateSet("nsis","inno","none")]
    [string]$Installer = "nsis"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
Push-Location $repoRoot
try {
    $buildPath = Resolve-Path $BuildDir
    $exe = Join-Path $buildPath "ModelBench.exe"
    if (-not (Test-Path $exe)) { throw "ModelBench.exe not found at $exe" }

    if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $stage = Join-Path $OutputDir "ModelBench"
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    # Copy executable
    Copy-Item $exe $stage

    # Qt deployment
    $windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
    if (-not (Test-Path $windeployqt)) { throw "windeployqt not found at $windeployqt" }
    & $windeployqt --release --no-translations --qmldir (Join-Path $repoRoot "app\ui") (Join-Path $stage "ModelBench.exe")
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

    # Copy llama.cpp runtime libs if present in build tree
    Get-ChildItem -Path $buildPath -Recurse -Include "llama*.dll","ggml*.dll" -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName $stage -Force }

    # Icon (optional)
    $icon = Join-Path $repoRoot "assets\icon.ico"
    if (Test-Path $icon) { Copy-Item $icon $stage }

    # Portable zip
    $zip = Join-Path $OutputDir "ModelBench-portable.zip"
    Compress-Archive -Path "$stage\*" -DestinationPath $zip -Force

    # Installer
    if ($Installer -eq "nsis") {
        $nsis = (Get-Command makensis.exe -ErrorAction SilentlyContinue)
        if ($nsis) {
            $nsi = Join-Path $PSScriptRoot "ModelBench.nsi"
            if (-not (Test-Path $nsi)) {
                # Generate a minimal NSIS script on the fly
                $nsi = Join-Path $OutputDir "ModelBench.nsi"
                @"
!define APPNAME "ModelBench"
!define APPVER "0.1.0"
OutFile "$OutputDir\ModelBench-Setup.exe"
InstallDir "`$PROGRAMFILES64\`${APPNAME}"
RequestExecutionLevel admin
Page directory
Page instfiles
Section "Install"
  SetOutPath "`$INSTDIR"
  File /r "$stage\*.*"
  CreateShortcut "`$DESKTOP\`${APPNAME}.lnk" "`$INSTDIR\ModelBench.exe"
  WriteUninstaller "`$INSTDIR\Uninstall.exe"
SectionEnd
Section "Uninstall"
  Delete "`$INSTDIR\Uninstall.exe"
  RMDir /r "`$INSTDIR"
  Delete "`$DESKTOP\`${APPNAME}.lnk"
SectionEnd
"@ | Set-Content -Path $nsi -Encoding ASCII
            }
            & makensis.exe $nsi
            if ($LASTEXITCODE -ne 0) { throw "NSIS installer build failed" }
        } else {
            Write-Warning "makensis.exe not found; skipping installer (portable zip available)"
        }
    } elseif ($Installer -eq "inno") {
        $iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue)
        if ($iscc) {
            Write-Warning "Inno Setup template not generated; provide .iss manually"
        } else {
            Write-Warning "iscc.exe not found; skipping installer"
        }
    }

    Write-Host "Packaging complete: $OutputDir"
} finally {
    Pop-Location
}