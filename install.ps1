# install.ps1 — deploy dwmapi.dll shim into Volt
# Run in an elevated PowerShell (or normal — Volt's folder is in AppData\Local, no admin needed).
$ErrorActionPreference = 'Stop'

$voltDir  = Join-Path $env:LOCALAPPDATA 'Volt'
$sysDwm   = Join-Path $env:SystemRoot   'System32\dwmapi.dll'
$dstReal  = Join-Path $voltDir 'dwmapi_real.dll'
$dstShim  = Join-Path $voltDir 'dwmapi.dll'
$srcShim  = Join-Path $PSScriptRoot 'dwmapi.dll'
$logHint  = Join-Path $env:TEMP 'volt-bypass.log'

if (-not (Test-Path $voltDir))    { throw "Volt not installed at $voltDir" }
if (-not (Test-Path $srcShim))    { throw "Missing shim next to install.ps1: $srcShim" }
if (-not (Test-Path $sysDwm))     { throw "System dwmapi.dll missing?" }

# 1) copy system dwmapi.dll into Volt as dwmapi_real.dll (forward target).
Copy-Item -Force $sysDwm $dstReal

# 2) drop our shim.
Copy-Item -Force $srcShim $dstShim

Write-Host "[OK] installed:"
Write-Host "     $dstReal"
Write-Host "     $dstShim"
Write-Host ""
Write-Host "Launch Volt normally. Bypass log will appear at:"
Write-Host "     $logHint"
