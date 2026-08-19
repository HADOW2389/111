# volt-bypass

`WebView2Loader.dll` shim для Volt (Roblox executor, Tauri + WebView2). Разблокирует premium/whitelist фичи через WebView2 preload script — фронтовые проверки лицензии всегда возвращают "premium=true, tier=lifetime". Бекенд для реального контента (script hub, cloud) продолжает работать в оригинале — модифицируются только auth-поля JSON-ответов домена `voltbz.net`.

## Почему WebView2Loader.dll а не dwmapi.dll?

Первая версия проксировала `dwmapi.dll` — не сработало, потому что `dwmapi` в **KnownDLLs** Windows (грузится всегда из System32, exe dir игнорируется).

`WebView2Loader.dll` не в KnownDLLs — Wry грузит её через `LoadLibraryW("WebView2Loader.dll")`, и стандартный search order подхватит наш shim из папки Volt раньше системного.

## Компоненты

- `src/dllmain.cpp` — proxy 5 экспортов реального `WebView2Loader.dll`, wrap environment callback → v-table patch controller → inject preload JS
- `src/preload.h` — preload JavaScript, оборачивает `fetch` и `XMLHttpRequest`
- `.github/workflows/build.yml` — MSVC x64 сборка на `windows-latest`
- `install.ps1` / `uninstall.ps1` — деплой в `%LOCALAPPDATA%\Volt`

## Сборка

Push в репу → GitHub Actions собирает артефакт `volt-bypass` (`WebView2Loader.dll` + скрипты).

## Установка

1. Скачать `WebView2Loader.dll` + `install.ps1` из артефакта Actions.
2. Положить рядом друг с другом.
3. `powershell -ep bypass -f install.ps1`
4. Запустить Volt как обычно.
5. Проверить `%TEMP%\volt-bypass.log`:
   - `shim attached to tauri-app.exe`
   - `EdgeWebView pv=<version>`
   - `real loader: <path>`
   - `CreateCoreWebView2EnvironmentWithOptions (shim)`
   - `EnvironmentCreated hr=0x00000000`
   - `v-table patched`
   - `preload injected hr=0x00000000`

## Удаление

`powershell -ep bypass -f uninstall.ps1`

## Как работает

```
tauri-app.exe (Volt)
  └─ Wry вызывает LoadLibraryW("WebView2Loader.dll")
        └─ Windows loader берёт наш shim из папки Volt (перед System32)
              ├─ DllMain логирует
              └─ при первом exports call:
                    ├─ читает HKLM\SOFTWARE\...\EdgeUpdate\Clients\{F3017226-...}\pv
                    ├─ LoadLibrary настоящего WebView2Loader.dll из EdgeWebView\Application\<ver>
                    └─ GetProcAddress 5 функций
        └─ Wry вызывает CreateCoreWebView2EnvironmentWithOptions → наша функция
              └─ оборачивает environmentCreatedHandler → делегирует настоящей
                    └─ v-table patch ICoreWebView2Environment::CreateCoreWebView2Controller
                          └─ оборачивает controllerCreatedHandler
                                └─ AddScriptToExecuteOnDocumentCreated(preload)
                                      └─ preload обёртывает fetch/XHR → мутирует ответы voltbz.net
```

## Идентификаторы

- Volt 1.0.8, Tauri identifier `com.volt.editor`
- Backend `https://api.voltbz.net`
- LocalStorage ключ `volt.activeApiUrl`
- HWID seed в `%LOCALAPPDATA%\Volt\client-settings.json`
