#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <thread>
#include <mutex>
#include <atomic>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include "sdk/memory.h"
#include "sdk/offsets.h"
#include "features/esp.h"
#include "features/aimbot.h"

static std::atomic<bool> g_Running{ true };
static std::mutex        g_PlayerMutex;

static uintptr_t  g_WorldPtr   = 0;
static uintptr_t  g_LocalPawn  = 0;
static uintptr_t  g_PlayerCtrl = 0;
static CameraData g_Camera;

static int g_ScreenW = GetSystemMetrics(SM_CXSCREEN);
static int g_ScreenH = GetSystemMetrics(SM_CYSCREEN);

using fnPresent = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
static fnPresent           oPresent  = nullptr;
static ID3D11Device*        g_Device  = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;
static ID3D11RenderTargetView* g_RTV  = nullptr;
static HWND                g_hWnd    = nullptr;
static WNDPROC             oWndProc  = nullptr;

static bool g_ImGuiReady = false;
static bool g_ShowMenu   = false;

void LogicThread() {
    while (g_Running) {
        g_WorldPtr = Memory::Read<uintptr_t>(Memory::GetOffset(Offsets::GWorld));

        if (g_WorldPtr) {
            uintptr_t gi = Memory::ReadOffset<uintptr_t>(g_WorldPtr, Offsets::World::GameInstance);
            if (gi) {
                uintptr_t lps = Memory::ReadOffset<uintptr_t>(gi, Offsets::GameInstance::LocalPlayers);
                uintptr_t lp  = lps ? Memory::Read<uintptr_t>(lps) : 0;
                if (lp) {
                    g_PlayerCtrl = Memory::ReadOffset<uintptr_t>(lp, Offsets::LocalPlayer::PlayerController);
                    if (g_PlayerCtrl)
                        g_LocalPawn = Memory::ReadOffset<uintptr_t>(g_PlayerCtrl, Offsets::PlayerController::AcknowledgedPawn);
                }
            }

            g_Camera = CameraData::Get(g_WorldPtr);

            std::lock_guard<std::mutex> lock(g_PlayerMutex);
            ESP::Update(g_WorldPtr, g_Camera, g_LocalPawn, g_ScreenW, g_ScreenH);
        }

        Sleep(2);
    }
}

void RenderESP() {
    auto& dl = *ImGui::GetBackgroundDrawList();
    std::lock_guard<std::mutex> lock(g_PlayerMutex);

    for (const auto& p : ESP::Players) {
        if (!p.OnScreen) continue;

        ImU32 color;
        if      (p.IsTeammate) color = IM_COL32(0, 120, 255, 255);
        else if (p.IsDBNO)     color = IM_COL32(255, 215, 0, 255);
        else                   color = IM_COL32(255, 60, 60, 255);

        float boxH = fabsf(p.ScreenFoot.Y - p.ScreenHead.Y);
        float boxW = boxH * 0.4f;
        float x1 = p.ScreenHead.X - boxW / 2.f;
        float y1 = p.ScreenHead.Y;
        float x2 = p.ScreenHead.X + boxW / 2.f;
        float y2 = p.ScreenFoot.Y;

        if (ESP::ShowBox)
            dl.AddRect(ImVec2(x1, y1), ImVec2(x2, y2), color, 0.f, 0, 1.5f);

        if (ESP::ShowHealth && p.MaxHealth > 0.f) {
            float hpRatio = p.Health / p.MaxHealth;
            float barH    = boxH * hpRatio;
            dl.AddRectFilled(ImVec2(x1 - 5.f, y2), ImVec2(x1 - 3.f, y1), IM_COL32(60, 60, 60, 180));
            dl.AddRectFilled(ImVec2(x1 - 5.f, y2), ImVec2(x1 - 3.f, y2 - barH), IM_COL32(50, 220, 50, 220));
        }

        if (ESP::ShowDistance) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fm", p.Distance);
            dl.AddText(ImVec2(x1, y2 + 2.f), IM_COL32(255, 255, 255, 200), buf);
        }
    }

    if (Aimbot::Enabled)
        dl.AddCircle(ImVec2(g_ScreenW / 2.f, g_ScreenH / 2.f),
            Aimbot::FOVRadius, IM_COL32(255, 255, 255, 60), 64, 1.f);
}

void RenderMenu() {
    if (!g_ShowMenu) return;

    ImGui::SetNextWindowSize(ImVec2(380, 480), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Once);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.05f, 0.05f, 0.08f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.15f, 0.05f, 0.25f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.25f, 0.08f, 0.45f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,     ImVec4(0.7f,  0.3f,  1.f,   1.f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,    ImVec4(0.6f,  0.2f,  0.9f,  1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       ImVec4(0.1f,  0.1f,  0.15f, 1.f));

    ImGui::Begin("PUBG Cheat [DEL = toggle]", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (ImGui::CollapsingHeader("ESP", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Box",       &ESP::ShowBox);
        ImGui::Checkbox("Show Skeleton",  &ESP::ShowSkeleton);
        ImGui::Checkbox("Show Health",    &ESP::ShowHealth);
        ImGui::Checkbox("Show Distance",  &ESP::ShowDistance);
        ImGui::Checkbox("Show Teammates", &ESP::ShowTeammates);
        ImGui::SliderFloat("Max Distance (m)", &ESP::MaxDistance, 50.f, 2000.f, "%.0f");
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Aimbot")) {
        ImGui::Checkbox("Enabled",    &Aimbot::Enabled);
        ImGui::Checkbox("Aim on Key", &Aimbot::AimOnKey);
        ImGui::Checkbox("Aim Head",   &Aimbot::AimHead);
        if (ImGui::Checkbox("Aim Chest", &Aimbot::AimChest))
            if (Aimbot::AimChest) Aimbot::AimHead = false;
        ImGui::SliderFloat("FOV Radius", &Aimbot::FOVRadius, 10.f, 300.f, "%.0f");
        ImGui::SliderFloat("Smoothness", &Aimbot::Smoothness, 1.f, 20.f, "%.1f");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f),
        "Players: %d | World: %llx", (int)ESP::Players.size(), g_WorldPtr);

    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_DELETE) g_ShowMenu = !g_ShowMenu;
    if (msg == WM_KEYDOWN && wParam == VK_END)    g_Running  = false;
    if (g_ShowMenu) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        if (ImGui::GetIO().WantCaptureMouse) return TRUE;
    }
    return CallWindowProcA(oWndProc, hWnd, msg, wParam, lParam);
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!g_ImGuiReady) {
        pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device);
        g_Device->GetImmediateContext(&g_Context);

        DXGI_SWAP_CHAIN_DESC desc{};
        pSwapChain->GetDesc(&desc);
        g_hWnd = desc.OutputWindow;

        ID3D11Texture2D* backBuf = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuf);
        if (backBuf) {
            g_Device->CreateRenderTargetView(backBuf, nullptr, &g_RTV);
            backBuf->Release();
        }

        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_hWnd);
        ImGui_ImplDX11_Init(g_Device, g_Context);

        oWndProc = (WNDPROC)SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
        g_ImGuiReady = true;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    Aimbot::Tick(g_Camera, g_PlayerCtrl, g_ScreenW, g_ScreenH);
    RenderESP();
    RenderMenu();

    ImGui::Render();
    g_Context->OMSetRenderTargets(1, &g_RTV, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return oPresent(pSwapChain, SyncInterval, Flags);
}

bool HookPresent() {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount       = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow      = GetForegroundWindow();
    desc.SampleDesc.Count  = 1;
    desc.Windowed          = TRUE;
    desc.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* tempChain = nullptr;
    ID3D11Device*   tempDev   = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &desc, &tempChain, &tempDev, nullptr, nullptr);

    if (FAILED(hr)) return false;

    void** vmt = *reinterpret_cast<void***>(tempChain);
    DWORD oldProt;
    VirtualProtect(&vmt[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    oPresent = reinterpret_cast<fnPresent>(vmt[8]);
    vmt[8]   = reinterpret_cast<void*>(hkPresent);
    VirtualProtect(&vmt[8], sizeof(void*), oldProt, &oldProt);

    tempChain->Release();
    tempDev->Release();
    return true;
}

void Cleanup() {
    g_Running = false;
    Sleep(200);

    if (g_ImGuiReady) {
        SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (g_RTV) g_RTV->Release();
    }
}

DWORD WINAPI MainThread(LPVOID) {
    Memory::Init();
    std::thread(LogicThread).detach();
    Sleep(1000);
    if (!HookPresent()) { Sleep(3000); HookPresent(); }

    while (g_Running) {
        if (GetAsyncKeyState(VK_END) & 1) g_Running = false;
        Sleep(100);
    }

    Cleanup();
    FreeLibraryAndExitThread(GetModuleHandleA(nullptr), 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CloseHandle(CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr));
    }
    return TRUE;
}
