// volt-launcher.exe — spawns Volt in a suspended state, injects payload.dll
// via CreateRemoteThread(LoadLibraryW), then resumes.
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

static void Fail(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list a; va_start(a, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, a);
    va_end(a);
    MessageBoxW(nullptr, buf, L"volt-launcher", MB_ICONERROR | MB_OK);
}

int wmain(int argc, wchar_t** argv) {
    wchar_t voltExe[MAX_PATH];
    // Argument 1 (optional) overrides the target exe.
    if (argc >= 2) {
        lstrcpynW(voltExe, argv[1], MAX_PATH);
    } else {
        if (!ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Volt\\tauri-app.exe", voltExe, MAX_PATH)) {
            Fail(L"ExpandEnvironmentStrings failed");
            return 1;
        }
    }
    if (!PathFileExistsW(voltExe)) {
        Fail(L"Volt executable not found:\n%ls", voltExe);
        return 1;
    }

    wchar_t payloadPath[MAX_PATH];
    GetModuleFileNameW(nullptr, payloadPath, MAX_PATH);
    PathRemoveFileSpecW(payloadPath);
    PathAppendW(payloadPath, L"payload.dll");
    if (!PathFileExistsW(payloadPath)) {
        Fail(L"payload.dll not found next to launcher:\n%ls", payloadPath);
        return 1;
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
        Fail(L"CreateProcess failed: %lu", GetLastError());
        return 1;
    }

    SIZE_T pathBytes = (lstrlenW(payloadPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(pi.hProcess, nullptr, pathBytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        Fail(L"VirtualAllocEx failed: %lu", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }
    if (!WriteProcessMemory(pi.hProcess, remote, payloadPath, pathBytes, nullptr)) {
        Fail(L"WriteProcessMemory failed: %lu", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE pLL =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(k32, "LoadLibraryW"));
    if (!pLL) {
        Fail(L"GetProcAddress(LoadLibraryW) failed");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }

    HANDLE ht = CreateRemoteThread(pi.hProcess, nullptr, 0, pLL, remote, 0, nullptr);
    if (!ht) {
        Fail(L"CreateRemoteThread failed: %lu", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 1;
    }

    // Wait up to 10s for LoadLibraryW to complete.
    WaitForSingleObject(ht, 10000);
    DWORD exitCode = 0;
    GetExitCodeThread(ht, &exitCode);
    CloseHandle(ht);
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);

    // Exit code of LoadLibraryW is the HMODULE (lower 32 bits). 0 == load failure.
    if (exitCode == 0) {
        Fail(L"LoadLibraryW inside target returned NULL. payload.dll failed to load.");
        // Still resume so the app runs (without bypass).
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
