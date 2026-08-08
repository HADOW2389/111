#pragma once
#include "types.h"

namespace Offsets {

    inline constexpr uintptr_t GWorld   = 0x8E61090;
    inline constexpr uintptr_t GNames   = 0x8E2C880;
    inline constexpr uintptr_t GObjects = 0x8E2CC40;

    namespace World {
        inline constexpr uintptr_t GameInstance    = 0x180;
        inline constexpr uintptr_t PersistentLevel = 0x30;
    }

    namespace GameInstance {
        inline constexpr uintptr_t LocalPlayers = 0x38;
    }

    namespace LocalPlayer {
        inline constexpr uintptr_t PlayerController = 0x30;
    }

    namespace PlayerController {
        inline constexpr uintptr_t AcknowledgedPawn    = 0x04B0;
        inline constexpr uintptr_t PlayerCameraManager = 0x04D8;
        inline constexpr uintptr_t PlayerState         = 0x0438;
    }

    namespace CameraManager {
        inline constexpr uintptr_t CameraLocation = 0x0490;
        inline constexpr uintptr_t CameraRotation = 0x0A2C;
        inline constexpr uintptr_t FOV            = 0x0A44;
    }

    namespace Actor {
        inline constexpr uintptr_t RootComponent = 0x01D0;
    }

    namespace SceneComponent {
        inline constexpr uintptr_t RelativeLocation = 0x128;
    }

    namespace Character {
        inline constexpr uintptr_t Mesh          = 0x0480;
        inline constexpr uintptr_t BuffComponent = 0x09A0;
        inline constexpr uintptr_t BC_AttrSet    = 0x0038;
        inline constexpr uintptr_t AS_Health     = 0x00AC;
        inline constexpr uintptr_t AS_HealthMax  = 0x00B8;
        inline constexpr uintptr_t bGroggy       = 0x0F6C;
        inline constexpr uintptr_t bIsDead       = 0x0620;
    }

    namespace SkeletalMesh {
        inline constexpr uintptr_t ComponentToWorld = 0x240;
        inline constexpr uintptr_t BoneArray        = 0x570;
    }

    namespace Level {
        inline constexpr uintptr_t Actors     = 0x98;
        inline constexpr uintptr_t ActorCount = 0xA0;
    }

    namespace PlayerState {
        inline constexpr uintptr_t TeamNumber  = 0x06A0;
        inline constexpr uintptr_t SquadIndex  = 0x06E8;
        inline constexpr uintptr_t AccountId   = 0x0940;
        inline constexpr uintptr_t DamageTaken = 0x0930;
    }

    namespace Bone {
        inline constexpr int Head          = 8;
        inline constexpr int Neck          = 7;
        inline constexpr int Chest         = 5;
        inline constexpr int Pelvis        = 2;
        inline constexpr int LeftShoulder  = 34;
        inline constexpr int RightShoulder = 9;
        inline constexpr int LeftElbow     = 35;
        inline constexpr int RightElbow    = 10;
        inline constexpr int LeftHand      = 36;
        inline constexpr int RightHand     = 11;
        inline constexpr int LeftKnee      = 77;
        inline constexpr int RightKnee     = 72;
        inline constexpr int LeftFoot      = 78;
        inline constexpr int RightFoot     = 73;
    }

    namespace Weapon {
        inline constexpr uintptr_t WeaponID    = 0x3F0;
        inline constexpr uintptr_t CurrentAmmo = 0xC28;
        inline constexpr uintptr_t TotalAmmo   = 0xC2C;
    }
}
