// volt-launcher.exe — spawns Volt in a suspended state, delivers payload.dll
// via APC injection (QueueUserAPC on the suspended main thread), then resumes.
//
// APC is picked up on the first alertable wait inside ntdll process init,
// which fires long before Wry initialises WebView2.
//
// Layout expected:
//   <same folder>\volt-launcher.exe
//   <same folder>\payload.dll
// Volt itself lives at %LOCALAPPDATA%\Volt\tauri-app.exe.

#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")

static bool g_showMessages = true;

static void Say(const wchar_t* fmt, ...) {
    if (!g_showMessages) return;
    wchar_t buf[512];
    va_list a; va_start(a, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, a);
    va_end(a);
    MessageBoxW(nullptr, buf, L"volt-launcher", MB_ICONERROR | MB_OK);
}

int wmain(int argc, wchar_t** argv) {
    // --silent suppresses error dialogs (useful when launched from a shortcut).
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--silent") == 0) g_showMessages = false;
    }

    wchar_t voltExe[MAX_PATH];
    if (!ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Volt\\tauri-app.exe", voltExe, MAX_PATH)) {
        Say(L"ExpandEnvironmentStrings failed"); return 1;
    }
    if (!PathFileExistsW(voltExe)) {
        Say(L"Volt executable not found:\n%ls", voltExe); return 1;
    }

    wchar_t payloadPath[MAX_PATH];
    GetModuleFileNameW(nullptr, payloadPath, MAX_PATH);
    PathRemoveFileSpecW(payloadPath);
    PathAppendW(payloadPath, L"payload.dll");
    if (!PathFileExistsW(payloadPath)) {
        Say(L"payload.dll not found next to launcher:\n%ls", payloadPath); return 1;
    }

    wchar_t workDir[MAX_PATH];
    lstrcpynW(workDir, voltExe, MAX_PATH);
    PathRemoveFileSpecW(workDir);

    wchar_t cmdline[MAX_PATH * 2];
    _snwprintf_s(cmdline, _TRUNCATE, L"\"%ls\"", voltExe);

    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(voltExe, cmdline, nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, workDir, &si, &pi)) {
        Say(L"CreateProcess failed: %lu", GetLastError());
        return 1;
    }

    SIZE_T pathBytes = (lstrlenW(payloadPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(pi.hProcess, nullptr, pathBytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        Say(L"VirtualAllocEx failed: %lu", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }
    if (!WriteProcessMemory(pi.hProcess, remote, payloadPath, pathBytes, nullptr)) {
        Say(L"WriteProcessMemory failed: %lu", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    PAPCFUNC pLL = reinterpret_cast<PAPCFUNC>(GetProcAddress(k32, "LoadLibraryW"));
    if (!pLL) {
        Say(L"GetProcAddress(LoadLibraryW) failed");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }

    // Primary path: APC injection. LoadLibraryW runs on the target's main
    // thread at its first alertable wait during ntdll process init — happens
    // long before Wry, so hooks are in place well ahead of WebView2 creation.
    DWORD apcResult = QueueUserAPC(pLL, pi.hThread, reinterpret_cast<ULONG_PTR>(remote));
    if (!apcResult) {
        // Fallback: resume first, then CreateRemoteThread once the process is
        // running (loader lock free). Late but usually still soon enough.
        DWORD ll = GetLastError();
        ResumeThread(pi.hThread);
        Sleep(300);
        HANDLE ht = CreateRemoteThread(pi.hProcess, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(pLL), remote, 0, nullptr);
        if (!ht) {
            Say(L"QueueUserAPC failed (%lu) and CreateRemoteThread also failed (%lu)",
                ll, GetLastError());
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            return 1;
        }
        WaitForSingleObject(ht, 5000);
        CloseHandle(ht);
    } else {
        ResumeThread(pi.hThread);
    }

    // Leak `remote` on purpose — VirtualFreeEx before LoadLibraryW touches the
    // buffer would corrupt the path the loader reads. Small permanent leak.

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
