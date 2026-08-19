$ErrorActionPreference = 'SilentlyContinue'
$voltDir = Join-Path $env:LOCALAPPDATA 'Volt'
Remove-Item -Force (Join-Path $voltDir 'WebView2Loader.dll')
Remove-Item -Force (Join-Path $voltDir 'dwmapi.dll')
Remove-Item -Force (Join-Path $voltDir 'dwmapi_real.dll')
Remove-Item -Force (Join-Path $env:TEMP  'volt-bypass.log')
Write-Host "[OK] shim removed."
