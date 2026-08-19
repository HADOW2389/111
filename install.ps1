# install.ps1 — deploy volt-launcher.exe + payload.dll and create desktop shortcut.
$ErrorActionPreference = 'Stop'

$voltDir     = Join-Path $env:LOCALAPPDATA 'Volt'
$installDir  = Join-Path $env:LOCALAPPDATA 'VoltBypass'
$srcLauncher = Join-Path $PSScriptRoot     'volt-launcher.exe'
$srcPayload  = Join-Path $PSScriptRoot     'payload.dll'
$dstLauncher = Join-Path $installDir       'volt-launcher.exe'
$dstPayload  = Join-Path $installDir       'payload.dll'
$logHint     = Join-Path $env:TEMP         'volt-bypass.log'
$shortcut    = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Volt (bypassed).lnk'

if (-not (Test-Path $voltDir))      { throw "Volt not installed at $voltDir" }
if (-not (Test-Path $srcLauncher))  { throw "Missing: $srcLauncher" }
if (-not (Test-Path $srcPayload))   { throw "Missing: $srcPayload" }

# Clean up prior attempts.
Remove-Item -Force (Join-Path $voltDir 'dwmapi.dll')          -ErrorAction SilentlyContinue
Remove-Item -Force (Join-Path $voltDir 'dwmapi_real.dll')     -ErrorAction SilentlyContinue
Remove-Item -Force (Join-Path $voltDir 'WebView2Loader.dll')  -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item -Force $srcLauncher $dstLauncher
Copy-Item -Force $srcPayload  $dstPayload

# Desktop shortcut.
$ws = New-Object -ComObject WScript.Shell
$sh = $ws.CreateShortcut($shortcut)
$sh.TargetPath        = $dstLauncher
$sh.WorkingDirectory  = $installDir
$sh.IconLocation      = (Join-Path $voltDir 'tauri-app.exe') + ',0'
$sh.Description       = 'Launch Volt with API bypass injected'
$sh.Save()

Write-Host "[OK] deployed:"
Write-Host "     $dstLauncher"
Write-Host "     $dstPayload"
Write-Host "     $shortcut"
Write-Host ""
Write-Host "Launch Volt from the new 'Volt (bypassed)' desktop shortcut."
Write-Host "Bypass log: $logHint"
