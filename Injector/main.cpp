#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <filesystem>
#include <sstream>

static HWND g_hWnd;
static HWND g_hStatus;
static HWND g_hDllPath;
static HWND g_hInjectBtn;
static HWND g_hBrowseBtn;
static HWND g_hProcessList;
static HWND g_hRefreshBtn;
static std::string g_DllPath;

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

bool InjectDLL(DWORD pid, const std::string& dllPath) {
    if (dllPath.empty() || !std::filesystem::exists(dllPath)) {
        SetWindowTextA(g_hStatus, "[-] DLL file not found!");
        return false;
    }

    std::string fullPath = std::filesystem::absolute(dllPath).string();
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        SetWindowTextA(g_hStatus, ("[-] OpenProcess failed: " + std::to_string(GetLastError())).c_str());
        return false;
    }

    SIZE_T pathLen = fullPath.size() + 1;
    LPVOID remoteMem = VirtualAllocEx(hProc, nullptr, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        CloseHandle(hProc);
        SetWindowTextA(g_hStatus, "[-] VirtualAllocEx failed!");
        return false;
    }

    if (!WriteProcessMemory(hProc, remoteMem, fullPath.c_str(), pathLen, nullptr)) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        SetWindowTextA(g_hStatus, "[-] WriteProcessMemory failed!");
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"),
        remoteMem, 0, nullptr);

    if (!hThread) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        SetWindowTextA(g_hStatus, ("[-] CreateRemoteThread failed: " + std::to_string(GetLastError())).c_str());
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProc);
    SetWindowTextA(g_hStatus, "[+] Injected successfully!");
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
            if (!pid) { SetWindowTextA(g_hStatus, "[-] PUBG not found!"); return 0; }
            char pathBuf[MAX_PATH]{};
            GetWindowTextA(g_hDllPath, pathBuf, MAX_PATH);
            g_DllPath = pathBuf;
            SetWindowTextA(g_hStatus, ("[*] Injecting into PID: " + std::to_string(pid)).c_str());
            InjectDLL(pid, g_DllPath);
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
        "[*] Ready. Select DLL and click Inject.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 375, 442, 40, g_hWnd, nullptr, hInst, nullptr);
    SendMessage(g_hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hInjectBtn = CreateWindowExA(0, "BUTTON", "INJECT",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 130, 422, 200, 36, g_hWnd, (HMENU)2, hInst, nullptr);
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
