#pragma once
#include "../sdk/memory.h"
#include "../sdk/offsets.h"
#include "../features/esp.h"
#include <cmath>

class Aimbot {
public:
    inline static bool  Enabled    = false;
    inline static bool  AimOnKey   = true;
    inline static int   AimKey     = VK_RBUTTON;
    inline static float FOVRadius  = 100.f;
    inline static float Smoothness = 8.f;
    inline static bool  AimHead    = true;
    inline static bool  AimChest   = false;

    static FRotator CalcAngle(const FVector& src, const FVector& dst) {
        FVector delta = dst - src;
        float dist    = sqrtf(delta.X * delta.X + delta.Y * delta.Y);
        float pitch   = -atan2f(delta.Z, dist) * (180.f / 3.14159265f);
        float yaw     =  atan2f(delta.Y, delta.X) * (180.f / 3.14159265f);
        return FRotator(pitch, yaw, 0.f);
    }

    static float NormalizeAngle(float angle) {
        while (angle > 180.f)  angle -= 360.f;
        while (angle < -180.f) angle += 360.f;
        return angle;
    }

    static float ScreenDist(const FVector2D& pt, int sw, int sh) {
        float dx = pt.X - sw / 2.f;
        float dy = pt.Y - sh / 2.f;
        return sqrtf(dx * dx + dy * dy);
    }

    static void Tick(const CameraData& cam,
                     uintptr_t playerController,
                     int screenW, int screenH)
    {
        if (!Enabled) return;
        if (AimOnKey && !(GetAsyncKeyState(AimKey) & 0x8000)) return;
        if (ESP::Players.empty()) return;

        PlayerInfo* best = nullptr;
        float bestDist = FOVRadius;

        for (auto& p : ESP::Players) {
            if (p.IsTeammate || !p.OnScreen) continue;
            FVector2D aimPt = AimHead ? p.ScreenHead : p.ScreenFoot;
            float d = ScreenDist(aimPt, screenW, screenH);
            if (d < bestDist) {
                bestDist = d;
                best = &p;
            }
        }

        if (!best) return;

        FVector aimTarget = AimHead ? best->HeadPos : best->WorldPos;
        FRotator target   = CalcAngle(cam.Location, aimTarget);

        float dPitch = NormalizeAngle(target.Pitch - cam.Rotation.Pitch);
        float dYaw   = NormalizeAngle(target.Yaw   - cam.Rotation.Yaw);

        float movePitch = dPitch / Smoothness;
        float moveYaw   = dYaw   / Smoothness;

        constexpr float SENSITIVITY_SCALE = 17.6f;
        LONG mx = static_cast<LONG>(moveYaw   * SENSITIVITY_SCALE);
        LONG my = static_cast<LONG>(movePitch * SENSITIVITY_SCALE);

        if (mx != 0 || my != 0)
            mouse_event(MOUSEEVENTF_MOVE, mx, my, 0, 0);
    }
};
