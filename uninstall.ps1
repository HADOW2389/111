$ErrorActionPreference = 'SilentlyContinue'
$voltDir    = Join-Path $env:LOCALAPPDATA 'Volt'
$installDir = Join-Path $env:LOCALAPPDATA 'VoltBypass'
Remove-Item -Recurse -Force $installDir
Remove-Item -Force (Join-Path $voltDir 'dwmapi.dll')
Remove-Item -Force (Join-Path $voltDir 'dwmapi_real.dll')
Remove-Item -Force (Join-Path $voltDir 'WebView2Loader.dll')
Remove-Item -Force (Join-Path $env:TEMP 'volt-bypass.log')
Remove-Item -Force (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Volt (bypassed).lnk')
Write-Host "[OK] uninstalled."
