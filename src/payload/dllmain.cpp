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

// Internal loader export used by Wry when WebView2Loader.dll is bypassed
// (Volt hits this path via Microsoft.MSEdgeWebView.Loader SxS activation).
// Signature reverse-engineered from EmbeddedBrowserWebView.dll:
//   HRESULT WINAPI CreateWebViewEnvironmentWithOptionsInternal(
//       BOOL installCheckFlag,
//       LPCWSTR browserExecutableFolder, LPCWSTR userDataFolder,
//       LPCWSTR additionalBrowserArguments, LPCWSTR additionalClientArguments,
//       LPCWSTR targetCompatibleBrowserVersion,
//       BOOL allowSingleSignOnUsingOSPrimaryAccount,
//       ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);
using pfn_CreateEnvInternal = HRESULT (WINAPI*)(
    BOOL, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, BOOL,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
static pfn_CreateEnvInternal g_origCreateEnvInternal = nullptr;

// Forward-declared here so TryHookLoader (above InstallLLHook in the file) can
// stash the handle for the GetProcAddress hook.
static HMODULE g_ebwHandle = nullptr;
static HMODULE g_loaderHandle = nullptr;

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

static HRESULT WINAPI HookedCreateEnvInternal(
    BOOL installCheckFlag, LPCWSTR bef, LPCWSTR udf,
    LPCWSTR addlBrowserArgs, LPCWSTR addlClientArgs,
    LPCWSTR targetVersion, BOOL allowSSO,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler)
{
    Log("CreateWebViewEnvironmentWithOptionsInternal intercepted (bef=%ls udf=%ls target=%ls)",
        bef ? bef : L"(null)", udf ? udf : L"(null)", targetVersion ? targetVersion : L"(null)");
    auto wrap = new EnvHandler(handler);
    HRESULT hr = g_origCreateEnvInternal(installCheckFlag, bef, udf, addlBrowserArgs,
                                         addlClientArgs, targetVersion, allowSSO, wrap);
    wrap->Release();
    return hr;
}

// -------- Loader discovery --------
static std::atomic<bool> g_hooked{false};

// Dump every named export of a module into the log so we can see the full
// surface Volt might invoke.
static void DumpModuleExports(HMODULE m, const char* tag) {
    if (!m) return;
    BYTE* base = reinterpret_cast<BYTE*>(m);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || !dir.Size) return;
    auto exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    auto names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);
    auto ords  = reinterpret_cast<WORD*>(base + exp->AddressOfNameOrdinals);
    auto funcs = reinterpret_cast<DWORD*>(base + exp->AddressOfFunctions);
    Log("=== %s exports (nfuncs=%lu nnames=%lu) ===", tag, exp->NumberOfFunctions, exp->NumberOfNames);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* n = reinterpret_cast<const char*>(base + names[i]);
        WORD ord = ords[i];
        DWORD frva = funcs[ord];
        void* addr = base + frva;
        BYTE* p = reinterpret_cast<BYTE*>(addr);
        Log("  [%u] %s @ +0x%lx  prologue: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
            (unsigned)(exp->Base + ord), n, frva,
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
    }
}

static bool TryHookLoader(HMODULE m) {
    if (g_hooked.load()) return true;
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { Log("MH_Initialize failed %d", (int)s); return false; }

    // Diagnostics: publish handle and dump the full export set once.
    g_ebwHandle = m;
    g_loaderHandle = m;
    DumpModuleExports(m, "EBW/loader");

    // Standard entry point (present in WebView2Loader.dll).
    if (auto p = GetProcAddress(m, "CreateCoreWebView2EnvironmentWithOptions")) {
        if (MH_CreateHook(reinterpret_cast<void*>(p), reinterpret_cast<void*>(&HookedCreateEnv),
                          reinterpret_cast<void**>(&g_origCreateEnv)) == MH_OK
            && MH_EnableHook(reinterpret_cast<void*>(p)) == MH_OK) {
            g_hooked = true;
            Log("HOOK LIVE: CreateCoreWebView2EnvironmentWithOptions @%p in module %p", p, m);
            return true;
        }
        Log("MinHook failed on CreateCoreWebView2EnvironmentWithOptions");
    }

    // Internal entry point (EmbeddedBrowserWebView.dll — Volt's SxS path).
    if (auto p = GetProcAddress(m, "CreateWebViewEnvironmentWithOptionsInternal")) {
        if (MH_CreateHook(reinterpret_cast<void*>(p), reinterpret_cast<void*>(&HookedCreateEnvInternal),
                          reinterpret_cast<void**>(&g_origCreateEnvInternal)) == MH_OK
            && MH_EnableHook(reinterpret_cast<void*>(p)) == MH_OK) {
            g_hooked = true;
            Log("HOOK LIVE: CreateWebViewEnvironmentWithOptionsInternal @%p in module %p", p, m);
            return true;
        }
        Log("MinHook failed on CreateWebViewEnvironmentWithOptionsInternal");
    }

    return false;
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

// -------- GetProcAddress hook — logs every symbol resolution against the loader --------
using pfn_GetProcAddress = FARPROC (WINAPI*)(HMODULE, LPCSTR);
static pfn_GetProcAddress g_origGPA = nullptr;

static FARPROC WINAPI HookedGetProcAddress(HMODULE h, LPCSTR name) {
    FARPROC r = g_origGPA(h, name);
    if (h && (h == g_ebwHandle || h == g_loaderHandle)) {
        // name may be ordinal (low bits) or string pointer
        if (reinterpret_cast<ULONG_PTR>(name) < 0x10000) {
            Log("GetProcAddress(EBW/loader, ord#%u) -> %p", (unsigned)(ULONG_PTR)name, r);
        } else {
            Log("GetProcAddress(EBW/loader, \"%s\") -> %p", name, r);
        }
    }
    return r;
}

static void InstallLLHook() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) return;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    if (auto pLL = GetProcAddress(k32, "LoadLibraryExW")) {
        if (MH_CreateHook(reinterpret_cast<void*>(pLL), reinterpret_cast<void*>(&HookedLoadLibraryExW),
                          reinterpret_cast<void**>(&g_origLL)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(pLL));
            Log("LoadLibraryExW hooked as fallback");
        }
    }
    if (auto pGPA = GetProcAddress(k32, "GetProcAddress")) {
        if (MH_CreateHook(reinterpret_cast<void*>(pGPA), reinterpret_cast<void*>(&HookedGetProcAddress),
                          reinterpret_cast<void**>(&g_origGPA)) == MH_OK) {
            MH_EnableHook(reinterpret_cast<void*>(pGPA));
            Log("GetProcAddress hooked for diagnostics");
        }
    }
}

// -------- LdrRegisterDllNotification: catch loader-lock-held loads --------
//
// ntdll fires this synchronously while the loader lock is held, right after
// LdrpMapDll but before the DLL is exposed to the caller. That means we can
// install the MinHook trampoline in-place before Wry even sees the module.
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING_T, *PUNICODE_STRING_T;

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG                   Flags;
    const UNICODE_STRING_T* FullDllName;
    const UNICODE_STRING_T* BaseDllName;
    PVOID                   DllBase;
    ULONG                   SizeOfImage;
} LDR_DLL_LOADED_NOTIFICATION_DATA_T, *PLDR_DLL_LOADED_NOTIFICATION_DATA_T;

typedef union _LDR_DLL_NOTIFICATION_DATA {
    LDR_DLL_LOADED_NOTIFICATION_DATA_T Loaded;
    LDR_DLL_LOADED_NOTIFICATION_DATA_T Unloaded;
} LDR_DLL_NOTIFICATION_DATA_T, *PLDR_DLL_NOTIFICATION_DATA_T;

typedef VOID (CALLBACK *LDR_DLL_NOTIFICATION_FN)(ULONG, PLDR_DLL_NOTIFICATION_DATA_T, PVOID);
typedef NTSTATUS (NTAPI *fn_LdrRegisterDllNotification)(ULONG, LDR_DLL_NOTIFICATION_FN, PVOID, PVOID*);

static PVOID g_ldrCookie = nullptr;

static VOID CALLBACK DllNotifyCB(ULONG reason, PLDR_DLL_NOTIFICATION_DATA_T data, PVOID) {
    if (reason != 1 /*LDR_DLL_NOTIFICATION_REASON_LOADED*/) return;
    if (!data || !data->Loaded.BaseDllName || !data->Loaded.BaseDllName->Buffer) return;
    const wchar_t* raw = data->Loaded.BaseDllName->Buffer;
    USHORT clen = data->Loaded.BaseDllName->Length / sizeof(wchar_t);
    wchar_t name[MAX_PATH]; if (clen >= MAX_PATH) clen = MAX_PATH - 1;
    memcpy(name, raw, clen * sizeof(wchar_t)); name[clen] = 0;

    // Log every load so we can trace ordering.
    Log("LdrNotify LOADED: %ls @ %p sz=%lu", name, data->Loaded.DllBase, data->Loaded.SizeOfImage);

    if (_wcsicmp(name, L"EmbeddedBrowserWebView.dll") == 0
        || _wcsicmp(name, L"WebView2Loader.dll") == 0) {
        TryHookLoader((HMODULE)data->Loaded.DllBase);
    }
}

static void InstallLdrNotification() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return;
    auto reg = reinterpret_cast<fn_LdrRegisterDllNotification>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (!reg) { Log("LdrRegisterDllNotification not found"); return; }
    NTSTATUS s = reg(0, DllNotifyCB, nullptr, &g_ldrCookie);
    Log("LdrRegisterDllNotification status=0x%08x cookie=%p", (unsigned)s, g_ldrCookie);
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

    // Poll for the loader (60 s window). Track the last-seen module handle so we
    // don't spam the log with the same candidate every 100 ms.
    HMODULE lastSeen = nullptr;
    for (int i = 0; i < 600 && !g_hooked.load(); i++) {
        HMODULE m = FindWebViewLoader();
        if (m && m != lastSeen) {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameExW(GetCurrentProcess(), m, path, MAX_PATH);
            Log("loader candidate: %ls", path);
            lastSeen = m;
        }
        if (m && TryHookLoader(m)) break;
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

        // Register the loader notification synchronously so we hook the DLL the
        // instant ntdll finishes mapping it — before Wry can call it.
        InstallLdrNotification();

        // If the loader is already in the process (unlikely but possible if we
        // were injected late) hook it right here without waiting for the thread.
        HMODULE m = GetModuleHandleW(L"EmbeddedBrowserWebView.dll");
        if (!m) m = GetModuleHandleW(L"WebView2Loader.dll");
        if (m) TryHookLoader(m);

        // Init thread does the LoadLibraryEx hook + fallback polling. If the
        // notification path fires first, the thread just exits early.
        CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
