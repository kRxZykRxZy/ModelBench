<#
.SYNOPSIS
  Code-sign a Windows installer/binary with Authenticode (signtool).
.PARAMETER File
  Path to the file to sign.
.PARAMETER CertPath
  Path to PFX certificate (or use env WINDOWS_CERT_PFX).
.PARAMETER CertPassword
  PFX password (or env WINDOWS_CERT_PASSWORD).
.PARAMETER TimestampUrl
  RFC3161 timestamp server (default: http://timestamp.digicert.com).
#>
param(
    [Parameter(Mandatory=$true)][string]$File,
    [string]$CertPath = $env:WINDOWS_CERT_PFX,
    [string]$CertPassword = $env:WINDOWS_CERT_PASSWORD,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $File)) { throw "File not found: $File" }
if (-not $CertPath -or -not (Test-Path $CertPath)) { throw "Certificate PFX not found" }

$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue)
if (-not $signtool) {
    # Try Windows SDK default location
    $candidates = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue
    $signtool = $candidates | Select-Object -First 1
}
if (-not $signtool) { throw "signtool.exe not found" }

& $signtool.FullName sign /f $CertPath /p $CertPassword /tr $TimestampUrl /td sha256 /fd sha256 $File
if ($LASTEXITCODE -ne 0) { throw "Signing failed" }

# Verify
& $signtool.FullName verify /pa $File
if ($LASTEXITCODE -ne 0) { throw "Verification failed" }
Write-Host "Signed and verified: $File"