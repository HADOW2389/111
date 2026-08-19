# install.ps1 — deploy WebView2Loader.dll shim into Volt.
# No admin required — target is %LOCALAPPDATA%\Volt.
$ErrorActionPreference = 'Stop'

$voltDir  = Join-Path $env:LOCALAPPDATA 'Volt'
$srcShim  = Join-Path $PSScriptRoot     'WebView2Loader.dll'
$dstShim  = Join-Path $voltDir          'WebView2Loader.dll'
$logHint  = Join-Path $env:TEMP         'volt-bypass.log'

# Clean up the previous approach if it's still there.
Remove-Item -Force (Join-Path $voltDir 'dwmapi.dll')       -ErrorAction SilentlyContinue
Remove-Item -Force (Join-Path $voltDir 'dwmapi_real.dll')  -ErrorAction SilentlyContinue

if (-not (Test-Path $voltDir)) { throw "Volt not installed at $voltDir" }
if (-not (Test-Path $srcShim)) { throw "Missing shim next to install.ps1: $srcShim" }

Copy-Item -Force $srcShim $dstShim

Write-Host "[OK] deployed: $dstShim"
Write-Host ""
Write-Host "Launch Volt. Bypass log will appear at:"
Write-Host "  $logHint"
Write-Host ""
Write-Host "Expected log lines:"
Write-Host "  shim attached to tauri-app.exe"
Write-Host "  EdgeWebView pv=..."
Write-Host "  real loader: ..."
Write-Host "  CreateCoreWebView2EnvironmentWithOptions (shim)"
Write-Host "  EnvironmentCreated hr=0x00000000"
Write-Host "  v-table patched: CreateCoreWebView2Controller -> hook"
Write-Host "  preload injected hr=0x00000000"
