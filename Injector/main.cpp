#include <Windows.h>
#include <commdlg.h>
#include <TlHelp32.h>
#include <string>
#include <filesystem>
#include <sstream>
#include <vector>
#include <fstream>

static HWND g_hWnd;
static HWND g_hStatus;
static HWND g_hDllPath;
static HWND g_hInjectBtn;
static HWND g_hBrowseBtn;
static HWND g_hProcessList;
static HWND g_hRefreshBtn;
static std::string g_DllPath;

void StatusMsg(const std::string& msg) {
    SetWindowTextA(g_hStatus, msg.c_str());
}

bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    LUID luid{};
    if (!LookupPrivilegeValueA(nullptr, "SeDebugPrivilege", &luid)) {
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD err = GetLastError();
    CloseHandle(hToken);
    return ok && err == ERROR_SUCCESS;
}

DWORD FindProcess(const wchar_t* name) {
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

struct ManualMapData {
    FARPROC pLoadLibraryA;
    FARPROC pGetProcAddress;
    HMODULE hModule;
};

using fnDllMain = BOOL(WINAPI*)(HMODULE, DWORD, LPVOID);

#pragma runtime_checks("", off)
void __stdcall ShellCode(ManualMapData* data) {
    if (!data) return;

    auto pBase = reinterpret_cast<BYTE*>(data->hModule);
    auto pDosH = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto pNtH  = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDosH->e_lfanew);
    auto pOptH = &pNtH->OptionalHeader;

    auto _LoadLibraryA   = reinterpret_cast<decltype(&LoadLibraryA)>(data->pLoadLibraryA);
    auto _GetProcAddress = reinterpret_cast<decltype(&GetProcAddress)>(data->pGetProcAddress);

    BYTE* locationDelta = pBase - pOptH->ImageBase;
    if (locationDelta) {
        if (!pOptH->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) return;

        auto pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
            pBase + pOptH->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);

        while (pRelocData->VirtualAddress) {
            UINT count = (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            auto pRelInfo = reinterpret_cast<WORD*>(pRelocData + 1);

            for (UINT i = 0; i < count; i++, pRelInfo++) {
                if ((*pRelInfo >> 12) == IMAGE_REL_BASED_DIR64) {
                    auto pPatch = reinterpret_cast<UINT_PTR*>(
                        pBase + pRelocData->VirtualAddress + (*pRelInfo & 0xFFF));
                    *pPatch += reinterpret_cast<UINT_PTR>(locationDelta);
                }
            }
            pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                reinterpret_cast<BYTE*>(pRelocData) + pRelocData->SizeOfBlock);
        }
    }

    if (pOptH->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto pImportDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            pBase + pOptH->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        while (pImportDesc->Name) {
            char* modName = reinterpret_cast<char*>(pBase + pImportDesc->Name);
            HMODULE hDll = _LoadLibraryA(modName);

            auto pThunk    = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->OriginalFirstThunk);
            auto pFuncRef  = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImportDesc->FirstThunk);

            if (!pImportDesc->OriginalFirstThunk)
                pThunk = pFuncRef;

            for (; pThunk->u1.AddressOfData; pThunk++, pFuncRef++) {
                if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal)) {
                    pFuncRef->u1.Function = reinterpret_cast<UINT_PTR>(
                        _GetProcAddress(hDll, reinterpret_cast<char*>(pThunk->u1.Ordinal & 0xFFFF)));
                } else {
                    auto pImport = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        pBase + pThunk->u1.AddressOfData);
                    pFuncRef->u1.Function = reinterpret_cast<UINT_PTR>(
                        _GetProcAddress(hDll, pImport->Name));
                }
            }
            pImportDesc++;
        }
    }

    if (pOptH->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
        auto pTLS = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(
            pBase + pOptH->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
        auto pCallback = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
        if (pCallback) {
            for (; *pCallback; pCallback++)
                (*pCallback)(reinterpret_cast<PVOID>(pBase), DLL_PROCESS_ATTACH, nullptr);
        }
    }

    auto pDllMain = reinterpret_cast<fnDllMain>(pBase + pOptH->AddressOfEntryPoint);
    pDllMain(reinterpret_cast<HMODULE>(pBase), DLL_PROCESS_ATTACH, nullptr);
}

void __stdcall ShellCodeEnd() {}
#pragma runtime_checks("", restore)

bool ManualMapInject(DWORD pid, const std::string& dllPath) {
    std::ifstream file(dllPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        StatusMsg("[-] Cannot open DLL file!");
        return false;
    }

    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> rawDll(fileSize);
    file.read(reinterpret_cast<char*>(rawDll.data()), fileSize);
    file.close();

    auto pDosH = reinterpret_cast<IMAGE_DOS_HEADER*>(rawDll.data());
    if (pDosH->e_magic != IMAGE_DOS_SIGNATURE) {
        StatusMsg("[-] Invalid DLL: bad DOS signature!");
        return false;
    }

    auto pNtH = reinterpret_cast<IMAGE_NT_HEADERS*>(rawDll.data() + pDosH->e_lfanew);
    if (pNtH->Signature != IMAGE_NT_SIGNATURE) {
        StatusMsg("[-] Invalid DLL: bad NT signature!");
        return false;
    }

    if (pNtH->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        StatusMsg("[-] DLL must be x64!");
        return false;
    }

    HANDLE hProc = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
        FALSE, pid);

    if (!hProc) {
        StatusMsg("[-] OpenProcess failed: " + std::to_string(GetLastError()));
        return false;
    }

    SIZE_T imageSize = pNtH->OptionalHeader.SizeOfImage;
    LPVOID pTargetBase = VirtualAllocEx(hProc, nullptr, imageSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!pTargetBase) {
        DWORD err = GetLastError();
        CloseHandle(hProc);
        StatusMsg("[-] VirtualAllocEx failed: " + std::to_string(err));
        return false;
    }

    StatusMsg("[*] Allocated " + std::to_string(imageSize) + " bytes at remote base...");

    if (!WriteProcessMemory(hProc, pTargetBase, rawDll.data(),
        pNtH->OptionalHeader.SizeOfHeaders, nullptr)) {
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        StatusMsg("[-] WriteProcessMemory (headers) failed!");
        return false;
    }

    auto pSectionH = IMAGE_FIRST_SECTION(pNtH);
    for (WORD i = 0; i < pNtH->FileHeader.NumberOfSections; i++, pSectionH++) {
        if (pSectionH->SizeOfRawData == 0) continue;

        if (!WriteProcessMemory(hProc,
            static_cast<BYTE*>(pTargetBase) + pSectionH->VirtualAddress,
            rawDll.data() + pSectionH->PointerToRawData,
            pSectionH->SizeOfRawData, nullptr))
        {
            VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            StatusMsg("[-] WriteProcessMemory (section) failed!");
            return false;
        }
    }

    ManualMapData mapData{};
    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    mapData.pLoadLibraryA   = GetProcAddress(hK32, "LoadLibraryA");
    mapData.pGetProcAddress = GetProcAddress(hK32, "GetProcAddress");
    mapData.hModule         = reinterpret_cast<HMODULE>(pTargetBase);

    SIZE_T shellSize = reinterpret_cast<BYTE*>(ShellCodeEnd) - reinterpret_cast<BYTE*>(ShellCode);
    SIZE_T totalAlloc = shellSize + sizeof(ManualMapData) + 64;

    LPVOID pShellMem = VirtualAllocEx(hProc, nullptr, totalAlloc,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!pShellMem) {
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        CloseHandle(hProc);
        StatusMsg("[-] VirtualAllocEx (shellcode) failed: " + std::to_string(GetLastError()));
        return false;
    }

    BYTE* pDataRemote = static_cast<BYTE*>(pShellMem) + shellSize + 16;

    if (!WriteProcessMemory(hProc, pShellMem, ShellCode, shellSize, nullptr)) {
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pShellMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        StatusMsg("[-] WriteProcessMemory (shellcode) failed!");
        return false;
    }

    if (!WriteProcessMemory(hProc, pDataRemote, &mapData, sizeof(mapData), nullptr)) {
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pShellMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        StatusMsg("[-] WriteProcessMemory (data) failed!");
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pShellMem),
        pDataRemote, 0, nullptr);

    if (!hThread) {
        DWORD err = GetLastError();
        VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pShellMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        StatusMsg("[-] CreateRemoteThread failed: " + std::to_string(err));
        return false;
    }

    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);

    VirtualFreeEx(hProc, pShellMem, 0, MEM_RELEASE);
    CloseHandle(hProc);

    StatusMsg("[+] ManualMap injection successful!");
    return true;
}

void RefreshProcessList() {
    SendMessage(g_hProcessList, LB_RESETCONTENT, 0, 0);
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstringstream ss;
            ss << pe.szExeFile << L" [" << pe.th32ProcessID << L"]";
            SendMessageW(g_hProcessList, LB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            char buf[MAX_PATH]{};
            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hWnd;
            ofn.lpstrFilter = "DLL Files\0*.dll\0All Files\0*.*\0";
            ofn.lpstrFile   = buf;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                g_DllPath = buf;
                SetWindowTextA(g_hDllPath, buf);
            }
        }
        else if (LOWORD(wParam) == 2) {
            DWORD pid = FindProcess(L"TslGame-Win64-Shipping.exe");
            if (!pid) pid = FindProcess(L"TslGame.exe");
            if (!pid) { StatusMsg("[-] PUBG not found!"); return 0; }
            char pathBuf[MAX_PATH]{};
            GetWindowTextA(g_hDllPath, pathBuf, MAX_PATH);
            g_DllPath = pathBuf;
            StatusMsg("[*] Enabling SeDebugPrivilege...");
            if (!EnableDebugPrivilege())
                StatusMsg("[!] SeDebugPrivilege failed, trying anyway...");
            StatusMsg("[*] ManualMap injecting into PID: " + std::to_string(pid));
            ManualMapInject(pid, g_DllPath);
        }
        else if (LOWORD(wParam) == 3) {
            RefreshProcessList();
        }
        break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(18, 18, 28));
        SetTextColor(hdc, RGB(200, 160, 255));
        static HBRUSH bg = CreateSolidBrush(RGB(18, 18, 28));
        return (LRESULT)bg;
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(80, 30, 140));
        SetTextColor(hdc, RGB(255, 255, 255));
        static HBRUSH btn = CreateSolidBrush(RGB(80, 30, 140));
        return (LRESULT)btn;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, CreateSolidBrush(RGB(12, 12, 20)));
        return 1;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    EnableDebugPrivilege();

    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "PubgCheatLauncher";
    wc.hbrBackground = CreateSolidBrush(RGB(12, 12, 20));
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    g_hWnd = CreateWindowExA(0, "PubgCheatLauncher", "PUBG Cheat Launcher",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 500,
        nullptr, nullptr, hInst, nullptr);

    HFONT hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    auto MakeLabel = [&](const char* text, int x, int y, int w, int h) {
        HWND lbl = CreateWindowExA(0, "STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, g_hWnd, nullptr, hInst, nullptr);
        SendMessage(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
        return lbl;
    };

    MakeLabel("DLL Path:", 10, 15, 80, 22);
    g_hDllPath = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 95, 12, 260, 24, g_hWnd, nullptr, hInst, nullptr);
    SendMessage(g_hDllPath, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hBrowseBtn = CreateWindowExA(0, "BUTTON", "Browse",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 362, 12, 90, 24, g_hWnd, (HMENU)1, hInst, nullptr);
    SendMessage(g_hBrowseBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    MakeLabel("Running Processes:", 10, 50, 200, 22);
    g_hRefreshBtn = CreateWindowExA(0, "BUTTON", "Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 362, 47, 90, 22, g_hWnd, (HMENU)3, hInst, nullptr);
    SendMessage(g_hRefreshBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hProcessList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        10, 75, 442, 290, g_hWnd, nullptr, hInst, nullptr);
    SendMessage(g_hProcessList, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hStatus = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC",
        "[*] Ready. Run as Admin. Select DLL and click Inject.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 375, 442, 40, g_hWnd, nullptr, hInst, nullptr);
    SendMessage(g_hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hInjectBtn = CreateWindowExA(0, "BUTTON", "INJECT (ManualMap)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 422, 240, 36, g_hWnd, (HMENU)2, hInst, nullptr);
    HFONT hBigFont = CreateFontA(18, 0, 0, 0, FW_BOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    SendMessage(g_hInjectBtn, WM_SETFONT, (WPARAM)hBigFont, TRUE);

    RefreshProcessList();
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    MSG m{};
    while (GetMessageA(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
