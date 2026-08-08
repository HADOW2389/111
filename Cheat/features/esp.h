#pragma once
#include "../sdk/memory.h"
#include "../sdk/offsets.h"
#include <vector>
#include <string>

struct CameraData {
    FVector   Location;
    FRotator  Rotation;
    float     FOV = 90.f;

    static CameraData Get(uintptr_t worldPtr) {
        CameraData cam{};
        uintptr_t gi = Memory::ReadOffset<uintptr_t>(worldPtr, Offsets::World::GameInstance);
        if (!gi) return cam;
        uintptr_t lps = Memory::ReadOffset<uintptr_t>(gi, Offsets::GameInstance::LocalPlayers);
        uintptr_t lp  = lps ? Memory::Read<uintptr_t>(lps) : 0;
        if (!lp) return cam;
        uintptr_t pc = Memory::ReadOffset<uintptr_t>(lp, Offsets::LocalPlayer::PlayerController);
        if (!pc) return cam;
        uintptr_t cm = Memory::ReadOffset<uintptr_t>(pc, Offsets::PlayerController::PlayerCameraManager);
        if (!cm) return cam;
        cam.Location = Memory::ReadVector(cm + Offsets::CameraManager::CameraLocation);
        cam.Rotation = Memory::ReadRotator(cm + Offsets::CameraManager::CameraRotation);
        cam.FOV      = Memory::ReadOffset<float>(cm, Offsets::CameraManager::FOV);
        return cam;
    }
};

inline bool WorldToScreen(
    const FVector& worldPos,
    const CameraData& cam,
    int screenW, int screenH,
    FVector2D& out)
{
    FVector delta = worldPos - cam.Location;

    float pitchRad = cam.Rotation.Pitch * (3.14159265f / 180.f);
    float yawRad   = cam.Rotation.Yaw   * (3.14159265f / 180.f);
    float rollRad  = cam.Rotation.Roll  * (3.14159265f / 180.f);

    float cosPitch = cosf(pitchRad), sinPitch = sinf(pitchRad);
    float cosYaw   = cosf(yawRad),   sinYaw   = sinf(yawRad);
    float cosRoll  = cosf(rollRad),  sinRoll  = sinf(rollRad);

    FVector axisX = { cosPitch * cosYaw, cosPitch * sinYaw, sinPitch };
    FVector axisY = {
        sinRoll * sinPitch * cosYaw - cosRoll * sinYaw,
        sinRoll * sinPitch * sinYaw + cosRoll * cosYaw,
        -sinRoll * cosPitch
    };
    FVector axisZ = {
        -(cosRoll * sinPitch * cosYaw + sinRoll * sinYaw),
        -(cosRoll * sinPitch * sinYaw - sinRoll * cosYaw),
        cosRoll * cosPitch
    };

    float projX = delta.Dot(axisY);
    float projY = delta.Dot(axisZ);
    float projZ = delta.Dot(axisX);

    if (projZ < 0.001f) return false;

    float fovRad = cam.FOV * (3.14159265f / 180.f);
    float screenCenterX = screenW / 2.f;
    float screenCenterY = screenH / 2.f;
    float scale = screenCenterX / tanf(fovRad * 0.5f);

    out.X = screenCenterX + (projX / projZ) * scale;
    out.Y = screenCenterY - (projY / projZ) * scale;

    return (out.X > 0.f && out.X < screenW && out.Y > 0.f && out.Y < screenH);
}

struct PlayerInfo {
    FVector   WorldPos;
    FVector   HeadPos;
    FVector2D ScreenFoot;
    FVector2D ScreenHead;
    float     Distance;
    float     Health;
    float     MaxHealth;
    bool      IsDBNO;
    bool      IsTeammate;
    bool      OnScreen;
    int       TeamNum;
};

class ESP {
public:
    inline static bool  ShowBox       = true;
    inline static bool  ShowSkeleton  = true;
    inline static bool  ShowHealth    = true;
    inline static bool  ShowDistance  = true;
    inline static bool  ShowTeammates = false;
    inline static float MaxDistance   = 800.f;

    inline static std::vector<PlayerInfo> Players;

    static void Update(uintptr_t worldPtr, const CameraData& cam,
                       uintptr_t localPawn,
                       int screenW, int screenH)
    {
        Players.clear();

        uintptr_t level = Memory::ReadOffset<uintptr_t>(worldPtr, Offsets::World::PersistentLevel);
        if (!level) return;

        auto actors = Memory::Read<TArray<uintptr_t>>(level + Offsets::Level::Actors);
        if (!actors.Data || actors.Count <= 0) return;

        int localTeam = -1;
        if (localPawn) {
            Character lc(localPawn);
            localTeam = lc.GetTeam();
        }

        int limit = (actors.Count < 500) ? actors.Count : 500;
        for (int i = 0; i < limit; i++) {
            uintptr_t actorPtr = Memory::Read<uintptr_t>(
                reinterpret_cast<uintptr_t>(actors.Data) + i * 8);

            if (!actorPtr || actorPtr == localPawn) continue;

            Character ch(actorPtr);
            if (!ch.IsAlive()) continue;

            float hp = ch.GetHealth();
            if (hp <= 0.f) continue;

            FVector worldFoot = ch.GetLocation();
            FVector worldHead = ch.GetHeadPos();

            float dist = cam.Location.DistanceTo(worldFoot) / 100.f;
            if (dist > MaxDistance) continue;

            PlayerInfo info{};
            info.WorldPos   = worldFoot;
            info.HeadPos    = worldHead;
            info.Distance   = dist;
            info.Health     = hp;
            info.MaxHealth  = ch.GetMaxHealth();
            info.IsDBNO     = ch.IsDBNO();
            info.TeamNum    = ch.GetTeam();
            info.IsTeammate = (info.TeamNum == localTeam);

            if (info.IsTeammate && !ShowTeammates) continue;

            FVector2D screenFoot, screenHead;
            bool footOk = WorldToScreen(worldFoot, cam, screenW, screenH, screenFoot);
            bool headOk = WorldToScreen(worldHead, cam, screenW, screenH, screenHead);

            info.ScreenFoot = screenFoot;
            info.ScreenHead = screenHead;
            info.OnScreen   = footOk && headOk;

            Players.push_back(info);
        }
    }
};
