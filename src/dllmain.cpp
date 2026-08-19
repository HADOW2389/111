// dwmapi.dll shim for Volt (com.volt.editor / tauri-app.exe).
//
// Strategy:
//   * We ship a proxy dwmapi.dll next to tauri-app.exe (Volt statically
//     imports 3 functions from dwmapi.dll — the .def forwards them to
//     dwmapi_real.dll, a copy of the system dwmapi.dll).
//   * On DLL_PROCESS_ATTACH we spawn a helper thread that hooks
//     LoadLibraryExW and polls for WebView2Loader.dll to appear.
//   * As soon as WebView2Loader.dll is in the process we MinHook
//     CreateCoreWebView2EnvironmentWithOptions and wrap the environment
//     callback.
//   * When the environment is created we v-table patch its
//     CreateCoreWebView2Controller so every controller callback runs
//     AddScriptToExecuteOnDocumentCreated(kPreloadJS) before Volt sees
//     the WebView.
//
// Everything logs to %TEMP%\volt-bypass.log so we can debug from Volt
// without a debugger attached.

#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string>

#include <objbase.h>
#include <wrl/client.h>

#include "WebView2.h"
#include "MinHook.h"
#include "preload.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

static void Log(const char* fmt, ...) {
    char path[MAX_PATH]; GetTempPathA(MAX_PATH, path);
    lstrcatA(path, "volt-bypass.log");
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    SYSTEMTIME st; GetLocalTime(&st);
    char hdr[64]; wsprintfA(hdr, "[%02u:%02u:%02u.%03u pid=%lu] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId());
    DWORD w;
    WriteFile(h, hdr, lstrlenA(hdr), &w, nullptr);
    va_list a; va_start(a, fmt);
    char body[1024]; wvsprintfA(body, fmt, a); va_end(a);
    WriteFile(h, body, lstrlenA(body), &w, nullptr);
    WriteFile(h, "\r\n", 2, &w, nullptr);
    CloseHandle(h);
}

// -------- WebView2 typedefs (avoid pulling WebView2Loader.lib) --------
using PFN_CreateCoreWebView2EnvironmentWithOptions = HRESULT (STDMETHODCALLTYPE*)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler
);
static PFN_CreateCoreWebView2EnvironmentWithOptions g_origCreateEnv = nullptr;
static PFN_CreateCoreWebView2EnvironmentWithOptions g_origCreateEnvVersioned = nullptr;

using PFN_CreateController = HRESULT (STDMETHODCALLTYPE*)(
    ICoreWebView2Environment*, HWND,
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*
);
static PFN_CreateController g_origCreateController = nullptr;

// -------- Controller-completed wrapper --------
class CtrlHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::atomic<LONG> m_ref{1};
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* m_orig;
public:
    explicit CtrlHandler(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* o) : m_orig(o) { if (m_orig) m_orig->AddRef(); }
    ~CtrlHandler() { if (m_orig) m_orig->Release(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
            AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override { return ++m_ref; }
    IFACEMETHODIMP_(ULONG) Release() noexcept override {
        LONG r = --m_ref; if (!r) delete this; return r;
    }
    IFACEMETHODIMP Invoke(HRESULT hr, ICoreWebView2Controller* ctrl) noexcept override {
        if (SUCCEEDED(hr) && ctrl) {
            ICoreWebView2* wv = nullptr;
            HRESULT ghr = ctrl->get_CoreWebView2(&wv);
            if (SUCCEEDED(ghr) && wv) {
                HRESULT ihr = wv->AddScriptToExecuteOnDocumentCreated(kPreloadJS, nullptr);
                Log("preload injected hr=0x%08x", (unsigned)ihr);
                wv->Release();
            } else {
                Log("get_CoreWebView2 failed 0x%08x", (unsigned)ghr);
            }
        }
        return m_orig ? m_orig->Invoke(hr, ctrl) : hr;
    }
};

// -------- v-table hook for ICoreWebView2Environment::CreateCoreWebView2Controller --------
static HRESULT STDMETHODCALLTYPE HookedCreateController(
    ICoreWebView2Environment* self, HWND parent,
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler)
{
    Log("CreateCoreWebView2Controller called; wrapping handler");
    CtrlHandler* wrap = new CtrlHandler(handler);
    HRESULT hr = g_origCreateController(self, parent, wrap);
    wrap->Release();  // handler released once Invoke fires
    return hr;
}

static void PatchEnvVTable(ICoreWebView2Environment* env) {
    if (g_origCreateController) return;
    void** vtbl = *reinterpret_cast<void***>(env);
    // ICoreWebView2Environment method order (from WebView2.h):
    //   0-2: IUnknown  3: CreateCoreWebView2Controller
    //   4: CreateWebResourceResponse  5: get_BrowserVersionString ...
    g_origCreateController = reinterpret_cast<PFN_CreateController>(vtbl[3]);
    DWORD old;
    if (VirtualProtect(&vtbl[3], sizeof(void*), PAGE_READWRITE, &old)) {
        vtbl[3] = reinterpret_cast<void*>(&HookedCreateController);
        VirtualProtect(&vtbl[3], sizeof(void*), old, &old);
        Log("v-table patched: CreateCoreWebView2Controller -> hook");
    } else {
        Log("VirtualProtect failed for env vtable: %lu", GetLastError());
    }
}

class EnvHandler final : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    std::atomic<LONG> m_ref{1};
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* m_orig;
public:
    explicit EnvHandler(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* o) : m_orig(o) { if (m_orig) m_orig->AddRef(); }
    ~EnvHandler() { if (m_orig) m_orig->Release(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
            AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override { return ++m_ref; }
    IFACEMETHODIMP_(ULONG) Release() noexcept override {
        LONG r = --m_ref; if (!r) delete this; return r;
    }
    IFACEMETHODIMP Invoke(HRESULT hr, ICoreWebView2Environment* env) noexcept override {
        Log("EnvironmentCreated hr=0x%08x env=%p", (unsigned)hr, (void*)env);
        if (SUCCEEDED(hr) && env) PatchEnvVTable(env);
        return m_orig ? m_orig->Invoke(hr, env) : hr;
    }
};

// -------- Hooks for CreateCoreWebView2EnvironmentWithOptions (both entry names) --------
static HRESULT STDMETHODCALLTYPE HookedCreateEnv(
    PCWSTR bef, PCWSTR udf,
    ICoreWebView2EnvironmentOptions* opts,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler)
{
    Log("CreateCoreWebView2EnvironmentWithOptions called");
    EnvHandler* wrap = new EnvHandler(handler);
    HRESULT hr = g_origCreateEnv(bef, udf, opts, wrap);
    wrap->Release();
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateEnvVersioned(
    PCWSTR bef, PCWSTR udf,
    ICoreWebView2EnvironmentOptions* opts,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler)
{
    Log("CreateCoreWebView2EnvironmentWithOptionsInternal called");
    EnvHandler* wrap = new EnvHandler(handler);
    HRESULT hr = g_origCreateEnvVersioned(bef, udf, opts, wrap);
    wrap->Release();
    return hr;
}

// -------- Install the WebView2 hook once the loader is present --------
static std::atomic<bool> g_wv2_hooked{false};

static bool TryInstallWebViewHook(HMODULE loader) {
    if (g_wv2_hooked.exchange(true)) return true;

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize failed %d", (int)s);
        return false;
    }

    auto p = GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions");
    if (p) {
        if (MH_CreateHook(reinterpret_cast<void*>(p), reinterpret_cast<void*>(&HookedCreateEnv),
                          reinterpret_cast<void**>(&g_origCreateEnv)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(p));
            Log("hook installed on CreateCoreWebView2EnvironmentWithOptions @%p", (void*)p);
        }
    }
    auto p2 = GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptionsInternal");
    if (p2) {
        if (MH_CreateHook(reinterpret_cast<void*>(p2), reinterpret_cast<void*>(&HookedCreateEnvVersioned),
                          reinterpret_cast<void**>(&g_origCreateEnvVersioned)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(p2));
            Log("hook installed on CreateCoreWebView2EnvironmentWithOptionsInternal @%p", (void*)p2);
        }
    }
    return true;
}

// -------- LoadLibrary hook chain (catches WebView2Loader as it loads) --------
using PFN_LoadLibraryExW = HMODULE (WINAPI*)(LPCWSTR, HANDLE, DWORD);
static PFN_LoadLibraryExW g_origLoadLibraryExW = nullptr;

static void MaybeHookIfLoader(HMODULE m, const wchar_t* name) {
    if (!m || !name) return;
    const wchar_t* leaf = PathFindFileNameW(name);
    if (_wcsicmp(leaf, L"WebView2Loader.dll") == 0) {
        Log("WebView2Loader.dll detected via LoadLibrary");
        TryInstallWebViewHook(m);
    }
}

static HMODULE WINAPI HookedLoadLibraryExW(LPCWSTR name, HANDLE f, DWORD flags) {
    HMODULE m = g_origLoadLibraryExW(name, f, flags);
    MaybeHookIfLoader(m, name);
    return m;
}

// -------- Init thread --------
static DWORD WINAPI InitThread(LPVOID) {
    Log("init thread starting");

    // If loader already loaded (unlikely, but check), hook immediately.
    HMODULE existing = GetModuleHandleW(L"WebView2Loader.dll");
    if (existing) {
        TryInstallWebViewHook(existing);
    }

    // Hook LoadLibraryExW so any future load of the loader is caught.
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize (init) failed %d", (int)s);
    }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto pLL = k32 ? GetProcAddress(k32, "LoadLibraryExW") : nullptr;
    if (pLL) {
        if (MH_CreateHook(reinterpret_cast<void*>(pLL), reinterpret_cast<void*>(&HookedLoadLibraryExW),
                          reinterpret_cast<void**>(&g_origLoadLibraryExW)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(pLL));
            Log("LoadLibraryExW hooked");
        }
    }

    // Belt-and-braces polling: 30s window in case loader appears without LL hook firing.
    for (int i = 0; i < 300 && !g_wv2_hooked; ++i) {
        HMODULE m = GetModuleHandleW(L"WebView2Loader.dll");
        if (m) { TryInstallWebViewHook(m); break; }
        Sleep(100);
    }
    Log("init thread exiting hooked=%d", (int)g_wv2_hooked.load());
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        // Only activate inside tauri-app.exe.
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* leaf = PathFindFileNameW(exe);
        if (_wcsicmp(leaf, L"tauri-app.exe") == 0) {
            Log("attached to tauri-app.exe (%ls)", exe);
            CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr);
        }
    }
    return TRUE;
}
