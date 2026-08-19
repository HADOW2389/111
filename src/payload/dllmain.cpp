// payload.dll — injected into tauri-app.exe by volt-launcher.exe before Volt
// executes its entry point (CREATE_SUSPENDED + CreateRemoteThread(LoadLibraryW)).
//
// Strategy:
//   1. Wait for WebView2Loader.dll to appear anywhere in the process
//      (poll GetModuleHandleW under a few names, and scan
//      EnumProcessModules paths containing "WebView2Loader").
//   2. MinHook CreateCoreWebView2EnvironmentWithOptions on the loader.
//   3. Also hook LoadLibraryExW so a late-arriving loader is caught too.
//   4. In the environment-created callback: v-table patch
//      ICoreWebView2Environment::CreateCoreWebView2Controller.
//   5. In the controller-created callback: AddScriptToExecuteOnDocumentCreated
//      with kPreloadJS.
//
// Log at %TEMP%\volt-bypass.log.

#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <atomic>
#include <cstdarg>

#include "WebView2.h"
#include "MinHook.h"
#include "preload.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "psapi.lib")
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

// -------- WebView2 hook plumbing --------
using pfn_CreateEnvOpts = HRESULT (STDMETHODCALLTYPE*)(
    PCWSTR, PCWSTR,
    ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
static pfn_CreateEnvOpts g_origCreateEnv = nullptr;

using pfn_CreateCtrl = HRESULT (STDMETHODCALLTYPE*)(
    ICoreWebView2Environment*, HWND,
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*);
static pfn_CreateCtrl g_origCreateCtrl = nullptr;

class CtrlHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::atomic<LONG> m_ref{1};
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* m_orig;
public:
    explicit CtrlHandler(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* o) : m_orig(o) { if (m_orig) m_orig->AddRef(); }
    ~CtrlHandler() { if (m_orig) m_orig->Release(); }
    IFACEMETHODIMP QueryInterface(REFIID r, void** p) noexcept override {
        if (!p) return E_POINTER;
        if (r == IID_IUnknown || r == __uuidof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *p = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
            AddRef(); return S_OK;
        }
        *p = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override { return ++m_ref; }
    IFACEMETHODIMP_(ULONG) Release() noexcept override { LONG r = --m_ref; if (!r) delete this; return r; }
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

static HRESULT STDMETHODCALLTYPE HookedCreateCtrl(
    ICoreWebView2Environment* self, HWND parent,
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler)
{
    Log("CreateCoreWebView2Controller called; wrapping handler");
    auto wrap = new CtrlHandler(handler);
    HRESULT hr = g_origCreateCtrl(self, parent, wrap);
    wrap->Release();
    return hr;
}

static void PatchEnvVTable(ICoreWebView2Environment* env) {
    if (g_origCreateCtrl) return;
    void** vtbl = *reinterpret_cast<void***>(env);
    g_origCreateCtrl = reinterpret_cast<pfn_CreateCtrl>(vtbl[3]);
    DWORD old;
    if (VirtualProtect(&vtbl[3], sizeof(void*), PAGE_READWRITE, &old)) {
        vtbl[3] = reinterpret_cast<void*>(&HookedCreateCtrl);
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
    IFACEMETHODIMP QueryInterface(REFIID r, void** p) noexcept override {
        if (!p) return E_POINTER;
        if (r == IID_IUnknown || r == __uuidof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *p = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
            AddRef(); return S_OK;
        }
        *p = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override { return ++m_ref; }
    IFACEMETHODIMP_(ULONG) Release() noexcept override { LONG r = --m_ref; if (!r) delete this; return r; }
    IFACEMETHODIMP Invoke(HRESULT hr, ICoreWebView2Environment* env) noexcept override {
        Log("EnvironmentCreated hr=0x%08x env=%p", (unsigned)hr, (void*)env);
        if (SUCCEEDED(hr) && env) PatchEnvVTable(env);
        return m_orig ? m_orig->Invoke(hr, env) : hr;
    }
};

static HRESULT STDMETHODCALLTYPE HookedCreateEnv(
    PCWSTR bef, PCWSTR udf,
    ICoreWebView2EnvironmentOptions* opts,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler)
{
    Log("CreateCoreWebView2EnvironmentWithOptions intercepted");
    auto wrap = new EnvHandler(handler);
    HRESULT hr = g_origCreateEnv(bef, udf, opts, wrap);
    wrap->Release();
    return hr;
}

// -------- Loader discovery --------
static std::atomic<bool> g_hooked{false};

static bool TryHookLoader(HMODULE m) {
    if (g_hooked.load()) return true;
    auto p = GetProcAddress(m, "CreateCoreWebView2EnvironmentWithOptions");
    if (!p) return false;
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { Log("MH_Initialize failed %d", (int)s); return false; }
    if (MH_CreateHook(reinterpret_cast<void*>(p), reinterpret_cast<void*>(&HookedCreateEnv),
                      reinterpret_cast<void**>(&g_origCreateEnv)) != MH_OK) {
        Log("MH_CreateHook failed on %p", p); return false;
    }
    if (MH_EnableHook(reinterpret_cast<void*>(p)) != MH_OK) {
        Log("MH_EnableHook failed"); return false;
    }
    g_hooked = true;
    Log("HOOK LIVE: CreateCoreWebView2EnvironmentWithOptions @%p in module %p", p, m);
    return true;
}

static HMODULE FindWebViewLoader() {
    static const wchar_t* kNames[] = {
        L"WebView2Loader.dll",
        L"Microsoft.MSEdgeWebView.Loader",
        L"embeddedbrowserwebview.dll",
        L"EmbeddedBrowserWebView.dll",
    };
    for (auto n : kNames) {
        HMODULE m = GetModuleHandleW(n);
        if (m) return m;
    }
    HMODULE mods[1024]; DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        int n = (int)(needed / sizeof(HMODULE));
        for (int i = 0; i < n; i++) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameExW(GetCurrentProcess(), mods[i], path, MAX_PATH)) {
                if (StrStrIW(path, L"WebView2Loader") || StrStrIW(path, L"EmbeddedBrowserWebView")) {
                    return mods[i];
                }
            }
        }
    }
    return nullptr;
}

// -------- LoadLibrary hook to catch late loads --------
using pfn_LoadLibraryExW = HMODULE (WINAPI*)(LPCWSTR, HANDLE, DWORD);
static pfn_LoadLibraryExW g_origLL = nullptr;

static HMODULE WINAPI HookedLoadLibraryExW(LPCWSTR name, HANDLE f, DWORD flags) {
    HMODULE m = g_origLL(name, f, flags);
    if (m && name && !g_hooked.load()) {
        const wchar_t* leaf = PathFindFileNameW(name);
        if (leaf && (StrStrIW(leaf, L"WebView2Loader") || StrStrIW(leaf, L"EmbeddedBrowserWebView")
                     || _wcsicmp(leaf, L"Microsoft.MSEdgeWebView.Loader") == 0)) {
            Log("LoadLibraryExW loaded candidate: %ls", name);
            TryHookLoader(m);
        }
    }
    return m;
}

static void InstallLLHook() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) return;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto pLL = k32 ? GetProcAddress(k32, "LoadLibraryExW") : nullptr;
    if (!pLL) return;
    if (MH_CreateHook(reinterpret_cast<void*>(pLL), reinterpret_cast<void*>(&HookedLoadLibraryExW),
                      reinterpret_cast<void**>(&g_origLL)) == MH_OK) {
        MH_EnableHook(reinterpret_cast<void*>(pLL));
        Log("LoadLibraryExW hooked as fallback");
    }
}

// -------- Init thread --------
static DWORD WINAPI InitThread(LPVOID) {
    Log("payload init thread starting");

    // Snapshot loaded modules for diagnostics.
    HMODULE mods[1024]; DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        int n = (int)(needed / sizeof(HMODULE));
        Log("initial module count: %d", n);
        for (int i = 0; i < n; i++) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameExW(GetCurrentProcess(), mods[i], path, MAX_PATH)) {
                const wchar_t* leaf = PathFindFileNameW(path);
                if (StrStrIW(leaf, L"webview") || StrStrIW(leaf, L"edge") || StrStrIW(leaf, L"msedge")
                    || StrStrIW(leaf, L"browser") || StrStrIW(leaf, L"loader")) {
                    Log("  early module: %ls", path);
                }
            }
        }
    }

    InstallLLHook();

    // Poll for the loader (60 s window).
    for (int i = 0; i < 600 && !g_hooked.load(); i++) {
        HMODULE m = FindWebViewLoader();
        if (m) {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameExW(GetCurrentProcess(), m, path, MAX_PATH);
            Log("loader detected via scan: %ls", path);
            if (TryHookLoader(m)) break;
        }
        Sleep(100);
    }

    Log("payload init thread exit hooked=%d", (int)g_hooked.load());
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        Log("payload attached to %ls", exe);
        CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
