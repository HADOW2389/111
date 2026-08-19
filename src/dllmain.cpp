// WebView2Loader.dll shim for Volt (com.volt.editor / tauri-app.exe).
//
// Why WebView2Loader.dll and not dwmapi.dll?
//   * dwmapi is in HKLM\...\Session Manager\KnownDLLs — Windows always
//     resolves it from System32, never from the exe directory. So our
//     shim was never loaded.
//   * WebView2Loader.dll is loaded dynamically by Wry via
//     LoadLibraryW("WebView2Loader.dll") — the default DLL search order
//     picks the copy sitting next to tauri-app.exe first.
//
// What we do:
//   1. Export the 5 real WebView2Loader.dll entry points so the loader
//      is a drop-in replacement.
//   2. On demand, load the actual runtime loader from
//      C:\Program Files (x86)\Microsoft\EdgeWebView\Application\<ver>\EBWebView\x64\WebView2Loader.dll
//      (version read from HKLM\...\EdgeUpdate\Clients\{F3017226-...}\pv).
//   3. For CreateCoreWebView2EnvironmentWithOptions and its basic
//      sibling: wrap the completion handler so we can v-table patch
//      ICoreWebView2Environment::CreateCoreWebView2Controller.
//   4. In the controller-completed callback: call
//      AddScriptToExecuteOnDocumentCreated(kPreloadJS) before Volt sees
//      the WebView.
//   5. All other exports are thin proxies to the real loader.
//
// Log lives at %TEMP%\volt-bypass.log.

#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstdint>

#include <objbase.h>
#include "WebView2.h"
#include "preload.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

// ---- Log helper ----
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

// ---- Function signatures for real loader ----
typedef HRESULT (STDMETHODCALLTYPE *pfn_CreateEnvOpts)(
    PCWSTR, PCWSTR,
    ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
typedef HRESULT (STDMETHODCALLTYPE *pfn_CreateEnvBasic)(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
typedef HRESULT (STDMETHODCALLTYPE *pfn_GetAvail)(PCWSTR, LPWSTR*);
typedef HRESULT (STDMETHODCALLTYPE *pfn_GetAvailOpts)(
    PCWSTR, ICoreWebView2EnvironmentOptions*, LPWSTR*);
typedef HRESULT (STDMETHODCALLTYPE *pfn_CompareVersions)(PCWSTR, PCWSTR, int*);

static HMODULE            g_real = nullptr;
static pfn_CreateEnvOpts  g_pCreateEnvOpts  = nullptr;
static pfn_CreateEnvBasic g_pCreateEnvBasic = nullptr;
static pfn_GetAvail       g_pGetAvail       = nullptr;
static pfn_GetAvailOpts   g_pGetAvailOpts   = nullptr;
static pfn_CompareVersions g_pCompareVer    = nullptr;

// ---- Locate + load real WebView2Loader.dll ----
static bool ReadVersion(HKEY root, LPCWSTR sub, wchar_t* out, DWORD cch) {
    HKEY h; if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_32KEY, &h) != ERROR_SUCCESS) return false;
    DWORD type = 0, cb = cch * sizeof(wchar_t);
    LSTATUS s = RegQueryValueExW(h, L"pv", nullptr, &type, (LPBYTE)out, &cb);
    RegCloseKey(h);
    return s == ERROR_SUCCESS && type == REG_SZ && out[0];
}

static HMODULE LoadRealLoader() {
    if (g_real) return g_real;

    wchar_t ver[64] = {0};
    // Try WOW6432Node first (Edge is a 32-bit installer entry)
    static const wchar_t* kKeys[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
    };
    for (auto k : kKeys) {
        if (ReadVersion(HKEY_LOCAL_MACHINE, k, ver, 64)) break;
    }
    if (!ver[0]) {
        for (auto k : kKeys) {
            if (ReadVersion(HKEY_CURRENT_USER, k, ver, 64)) break;
        }
    }
    Log("EdgeWebView pv=%ls", ver[0] ? ver : L"(none)");

    static const wchar_t* kBases[] = {
        L"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\%ls\\EBWebView\\x64\\WebView2Loader.dll",
        L"C:\\Program Files\\Microsoft\\EdgeWebView\\Application\\%ls\\EBWebView\\x64\\WebView2Loader.dll",
        L"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\%ls\\WebView2Loader.dll",
        L"C:\\Program Files\\Microsoft\\EdgeWebView\\Application\\%ls\\WebView2Loader.dll",
    };
    if (ver[0]) {
        for (auto b : kBases) {
            wchar_t path[MAX_PATH];
            wsprintfW(path, b, ver);
            g_real = LoadLibraryW(path);
            if (g_real) {
                Log("real loader: %ls", path);
                break;
            }
        }
    }

    // Fallback: search EdgeWebView\Application\* for any WebView2Loader.dll
    if (!g_real) {
        static const wchar_t* kSearch[] = {
            L"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\",
            L"C:\\Program Files\\Microsoft\\EdgeWebView\\Application\\",
        };
        for (auto root : kSearch) {
            wchar_t glob[MAX_PATH];
            wsprintfW(glob, L"%ls*", root);
            WIN32_FIND_DATAW fd;
            HANDLE hf = FindFirstFileW(glob, &fd);
            if (hf == INVALID_HANDLE_VALUE) continue;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == L'.') continue;
                wchar_t candidate[MAX_PATH];
                wsprintfW(candidate, L"%ls%ls\\EBWebView\\x64\\WebView2Loader.dll", root, fd.cFileName);
                g_real = LoadLibraryW(candidate);
                if (g_real) { Log("real loader (glob): %ls", candidate); break; }
            } while (FindNextFileW(hf, &fd));
            FindClose(hf);
            if (g_real) break;
        }
    }

    if (!g_real) {
        Log("FAILED to locate real WebView2Loader.dll");
        return nullptr;
    }

    g_pCreateEnvOpts   = (pfn_CreateEnvOpts)  GetProcAddress(g_real, "CreateCoreWebView2EnvironmentWithOptions");
    g_pCreateEnvBasic  = (pfn_CreateEnvBasic) GetProcAddress(g_real, "CreateCoreWebView2Environment");
    g_pGetAvail        = (pfn_GetAvail)       GetProcAddress(g_real, "GetAvailableCoreWebView2BrowserVersionString");
    g_pGetAvailOpts    = (pfn_GetAvailOpts)   GetProcAddress(g_real, "GetAvailableCoreWebView2BrowserVersionStringWithOptions");
    g_pCompareVer      = (pfn_CompareVersions)GetProcAddress(g_real, "CompareBrowserVersions");

    Log("resolved: opts=%p basic=%p avail=%p availOpts=%p cmp=%p",
        g_pCreateEnvOpts, g_pCreateEnvBasic, g_pGetAvail, g_pGetAvailOpts, g_pCompareVer);
    return g_real;
}

// ---- Controller-completed wrapper: injects the preload script ----
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

// ---- v-table hook for ICoreWebView2Environment::CreateCoreWebView2Controller ----
typedef HRESULT (STDMETHODCALLTYPE *pfn_CreateCtrl)(
    ICoreWebView2Environment*, HWND,
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*);
static pfn_CreateCtrl g_origCreateCtrl = nullptr;

static HRESULT STDMETHODCALLTYPE HookedCreateCtrl(
    ICoreWebView2Environment* self, HWND parent,
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler)
{
    Log("CreateCoreWebView2Controller called; wrapping handler");
    CtrlHandler* wrap = new CtrlHandler(handler);
    HRESULT hr = g_origCreateCtrl(self, parent, wrap);
    wrap->Release();
    return hr;
}

static void PatchEnvVTable(ICoreWebView2Environment* env) {
    if (g_origCreateCtrl) return;
    void** vtbl = *reinterpret_cast<void***>(env);
    // ICoreWebView2Environment: 0-2 IUnknown, 3 CreateCoreWebView2Controller, ...
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

// ---- Environment-created wrapper ----
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

// ---- Exported entry points (drop-in for WebView2Loader.dll) ----
//
// WebView2.h already declares the public names via STDAPI (no dllexport),
// so redefining them causes C2375 "different linkage". Define implementations
// under unique private names and publish them under the original symbols via
// linker aliases. x64 has no name decoration for extern "C" __stdcall, so
// /EXPORT:public=private works directly.
extern "C" {

HRESULT STDMETHODCALLTYPE WvxCreateEnvOpts(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler)
{
    if (!LoadRealLoader() || !g_pCreateEnvOpts) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    Log("CreateCoreWebView2EnvironmentWithOptions (shim) bef=%ls udf=%ls",
        browserExecutableFolder ? browserExecutableFolder : L"(null)",
        userDataFolder ? userDataFolder : L"(null)");
    auto wrap = new EnvHandler(environmentCreatedHandler);
    HRESULT hr = g_pCreateEnvOpts(browserExecutableFolder, userDataFolder, environmentOptions, wrap);
    wrap->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE WvxCreateEnvBasic(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler)
{
    if (!LoadRealLoader() || !g_pCreateEnvBasic) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    Log("CreateCoreWebView2Environment (shim)");
    auto wrap = new EnvHandler(environmentCreatedHandler);
    HRESULT hr = g_pCreateEnvBasic(wrap);
    wrap->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE WvxGetAvail(PCWSTR browserExecutableFolder, LPWSTR* versionInfo)
{
    if (!LoadRealLoader() || !g_pGetAvail) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    return g_pGetAvail(browserExecutableFolder, versionInfo);
}

HRESULT STDMETHODCALLTYPE WvxGetAvailOpts(
    PCWSTR browserExecutableFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    LPWSTR* versionInfo)
{
    if (!LoadRealLoader() || !g_pGetAvailOpts) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    return g_pGetAvailOpts(browserExecutableFolder, environmentOptions, versionInfo);
}

HRESULT STDMETHODCALLTYPE WvxCompareBrowserVersions(PCWSTR v1, PCWSTR v2, int* result)
{
    if (!LoadRealLoader() || !g_pCompareVer) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    return g_pCompareVer(v1, v2, result);
}

} // extern "C"

#pragma comment(linker, "/EXPORT:CreateCoreWebView2EnvironmentWithOptions=WvxCreateEnvOpts")
#pragma comment(linker, "/EXPORT:CreateCoreWebView2Environment=WvxCreateEnvBasic")
#pragma comment(linker, "/EXPORT:GetAvailableCoreWebView2BrowserVersionString=WvxGetAvail")
#pragma comment(linker, "/EXPORT:GetAvailableCoreWebView2BrowserVersionStringWithOptions=WvxGetAvailOpts")
#pragma comment(linker, "/EXPORT:CompareBrowserVersions=WvxCompareBrowserVersions")

// ---- Entry ----
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* leaf = PathFindFileNameW(exe);
        // Only chatter about tauri-app.exe; still function normally in any host
        // (WebView2Loader.dll is expected to work as a drop-in loader).
        if (_wcsicmp(leaf, L"tauri-app.exe") == 0) {
            Log("shim attached to tauri-app.exe (%ls)", exe);
        } else {
            Log("shim attached to host %ls", leaf);
        }
    }
    return TRUE;
}
