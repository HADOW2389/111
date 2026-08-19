# volt-bypass

Инъектор premium/whitelist байпаса для Volt (Roblox executor, Tauri 2.11 + WebView2).

## Как это работает

1. `volt-launcher.exe` запускает `tauri-app.exe` в **CREATE_SUSPENDED** состоянии.
2. Инжектит `payload.dll` через `CreateRemoteThread(LoadLibraryW)`.
3. Возобновляет главный поток Volt.
4. `payload.dll` (в DllMain) ждёт появление `WebView2Loader.dll` в процессе и MinHook'ает `CreateCoreWebView2EnvironmentWithOptions`.
5. Wrap → environment callback → v-table patch `ICoreWebView2Environment::CreateCoreWebView2Controller` → controller callback → `AddScriptToExecuteOnDocumentCreated(preload)`.
6. Preload JS оборачивает `fetch` + `XMLHttpRequest` + Tauri `invoke()` — для домена `voltbz.net` мутирует ответы (`premium:true`, `tier:"lifetime"`, `whitelisted:true`, fake user/session и т.д.); 401/403/404 превращает в 200 OK.

## Почему через инжектор, а не DLL-shim рядом с exe

Первые версии проксировали `dwmapi.dll` и `WebView2Loader.dll` — не сработало:

- `dwmapi.dll` — в KnownDLLs на некоторых сборках Windows (грузится из System32).
- `WebView2Loader.dll` — Volt использует **SxS activation** (`Microsoft.MSEdgeWebView.Loader` assembly): Windows берёт из WinSxS store, папка exe игнорируется.

Injection в suspended процесс обходит оба ограничения — payload гарантированно попадает до старта Wry.

## Компоненты

- `src/payload/dllmain.cpp` — payload DLL: MinHook + polling для loader'а + WebView2 hijack
- `src/payload/preload.h` — preload JavaScript (fetch/XHR/Tauri invoke wrap)
- `src/launcher/launcher.cpp` — CREATE_SUSPENDED launcher с DLL injection
- `.github/workflows/build.yml` — MSVC x64 сборка на `windows-latest`
- `install.ps1` — деплоит оба файла в `%LOCALAPPDATA%\VoltBypass` + создаёт ярлык на десктопе
- `uninstall.ps1` — удаляет всё

## Установка

1. Скачать артефакт `volt-bypass` из GitHub Actions (`volt-launcher.exe` + `payload.dll` + скрипты).
2. Положить всё в одну папку.
3. `powershell -ep bypass -f install.ps1`
4. Запустить Volt через ярлык **"Volt (bypassed)"** на десктопе (НЕ через оригинальный ярлык).
5. Проверить `%TEMP%\volt-bypass.log` — ожидаемые строки:
   - `payload attached to ...tauri-app.exe`
   - `initial module count: N`
   - `LoadLibraryExW hooked as fallback`
   - `loader detected via scan: ...WebView2Loader.dll` (или через LoadLibraryExW)
   - `HOOK LIVE: CreateCoreWebView2EnvironmentWithOptions ...`
   - `EnvironmentCreated hr=0x00000000`
   - `v-table patched: CreateCoreWebView2Controller -> hook`
   - `preload injected hr=0x00000000`

## Удаление

`powershell -ep bypass -f uninstall.ps1`

## Идентификаторы

- Volt 1.0.8, `com.volt.editor`
- Backend `https://api.voltbz.net`
- LocalStorage `volt.activeApiUrl`
- HWID seed: `%LOCALAPPDATA%\Volt\client-settings.json` → `hwidSeed`
