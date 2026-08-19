# volt-bypass

DLL-инжект для Volt (Roblox executor, Tauri + WebView2). Разблокирует premium/whitelist фичи через хук WebView2 preload script — фронтовые проверки лицензии всегда возвращают "premium=true, tier=lifetime". Бекенд для реального контента (script hub, cloud) продолжает работать в оригинале — модифицируются только auth-поля JSON-ответов домена `voltbz.net`.

## Компоненты

- `src/dllmain.cpp` — proxy `dwmapi.dll`, MinHook на `CreateCoreWebView2EnvironmentWithOptions`, v-table патч `CreateCoreWebView2Controller`, инжект preload JS
- `src/preload.h` — preload JavaScript, оборачивает `fetch` и `XMLHttpRequest`
- `src/dwmapi.def` — forward-экспорты в реальную `dwmapi_real.dll`
- `.github/workflows/build.yml` — MSVC x64 сборка на `windows-latest`
- `install.ps1` / `uninstall.ps1` — деплой в `%LOCALAPPDATA%\Volt`

## Сборка

Push в репо → GitHub Actions собирает артефакт `volt-bypass` (`dwmapi.dll` + скрипты). Скачать со страницы Actions или Release при пуше тега.

## Установка

1. Скачать `dwmapi.dll` + `install.ps1` из артефакта Actions.
2. Положить рядом друг с другом.
3. `powershell -ep bypass -f install.ps1`
4. Запустить Volt как обычно.
5. Проверить `%TEMP%\volt-bypass.log`:
   - `attached to tauri-app.exe`
   - `WebView2Loader.dll detected`
   - `hook installed on CreateCoreWebView2EnvironmentWithOptions`
   - `EnvironmentCreated hr=0x00000000`
   - `v-table patched`
   - `preload injected hr=0x00000000`

## Удаление

`powershell -ep bypass -f uninstall.ps1`

## Как работает

```
tauri-app.exe (Volt)
  ├─ импортирует dwmapi.dll (3 функции)
  │     └─ Windows loader берёт наш shim из папки Volt (перед System32)
  │           ├─ forward-экспорты → dwmapi_real.dll (копия системного)
  │           └─ DllMain → hook LoadLibraryExW + polling WebView2Loader.dll
  │
  └─ Tauri создаёт WebView2 через WebView2Loader.dll::CreateCoreWebView2EnvironmentWithOptions
        └─ наш MinHook перехватывает
              └─ оборачивает environmentCreatedHandler
                    └─ v-table patch ICoreWebView2Environment::CreateCoreWebView2Controller
                          └─ оборачивает controllerCreatedHandler
                                └─ AddScriptToExecuteOnDocumentCreated(preload)
                                      └─ preload обёртывает fetch/XHR → мутирует ответы voltbz.net
```

## Идентификаторы

- Volt 1.0.8, Tauri identifier `com.volt.editor`
- Backend `https://api.voltbz.net`
- LocalStorage ключ `volt.activeApiUrl`
- HWID seed в `%LOCALAPPDATA%\Volt\client-settings.json` — можно сбросить (`hwidSeed: <новое число>`) если бекенд забанил hardware
