#pragma once
#include "types.h"
#include "offsets.h"
#include <Windows.h>
#include <string>

class Memory {
public:
    static inline uintptr_t BaseAddress = 0;

    static void Init() {
        BaseAddress = reinterpret_cast<uintptr_t>(
            GetModuleHandleA("TslGame-Win64-Shipping.exe")
        );
        if (!BaseAddress)
            BaseAddress = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    }

    template<typename T>
    static T Read(uintptr_t address) {
        T result{};
        if (!address || address < 0x1000) return result;
        __try {
            result = *reinterpret_cast<T*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return result;
    }

    template<typename T>
    static T ReadOffset(uintptr_t base, uintptr_t offset) {
        return Read<T>(base + offset);
    }

    static uintptr_t ReadChain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
        uintptr_t addr = base;
        for (auto off : offsets) {
            addr = Read<uintptr_t>(addr + off);
            if (!addr) return 0;
        }
        return addr;
    }

    static FVector  ReadVector(uintptr_t address)  { return Read<FVector>(address); }
    static FRotator ReadRotator(uintptr_t address) { return Read<FRotator>(address); }
    static uintptr_t GetOffset(uintptr_t rva)      { return BaseAddress + rva; }

    static std::wstring ReadFString(uintptr_t address) {
        FString fs = Read<FString>(address);
        if (!fs.Data || fs.Count <= 0 || fs.Count > 256) return L"";
        
        wchar_t buf[256]{};
        size_t len = static_cast<size_t>(fs.Count) - 1;
        if (len >= 256) len = 255;
        
        __try {
            memcpy(buf, fs.Data, len * sizeof(wchar_t));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return L"";
        }
        return std::wstring(buf, len);
    }
};

struct FTransform {
    FQuat   Rotation;
    FVector Translation;
    float   pad;
    FVector Scale3D;
    float   pad2;

    FVector GetTranslation() const { return Translation; }
};

struct Actor {
    uintptr_t ptr;
    explicit Actor(uintptr_t p) : ptr(p) {}
    bool IsValid() const { return ptr != 0; }

    FVector GetLocation() const {
        uintptr_t root = Memory::ReadOffset<uintptr_t>(ptr, Offsets::Actor::RootComponent);
        if (!root) return {};
        return Memory::ReadVector(root + Offsets::SceneComponent::RelativeLocation);
    }
};

struct Character : Actor {
    explicit Character(uintptr_t p) : Actor(p) {}

    float GetHealth() const {
        uintptr_t bc = Memory::ReadOffset<uintptr_t>(ptr, Offsets::Character::BuffComponent);
        if (!bc) return 0.f;
        uintptr_t as = Memory::ReadOffset<uintptr_t>(bc, Offsets::Character::BC_AttrSet);
        if (!as) return 0.f;
        return Memory::ReadOffset<float>(as, Offsets::Character::AS_Health);
    }

    float GetMaxHealth() const {
        uintptr_t bc = Memory::ReadOffset<uintptr_t>(ptr, Offsets::Character::BuffComponent);
        if (!bc) return 100.f;
        uintptr_t as = Memory::ReadOffset<uintptr_t>(bc, Offsets::Character::BC_AttrSet);
        if (!as) return 100.f;
        return Memory::ReadOffset<float>(as, Offsets::Character::AS_HealthMax);
    }

    bool IsDBNO() const { return Memory::ReadOffset<bool>(ptr, Offsets::Character::bGroggy); }
    bool IsDead() const { return GetHealth() <= 0.f; }
    bool IsAlive() const { return !IsDead(); }

    int GetTeam() const {
        uintptr_t ps = Memory::ReadOffset<uintptr_t>(ptr + 0x0438 - 0x0410, 0);
        // fallback: read directly from controller chain
        return Memory::ReadOffset<int32>(ptr, 0x0758);
    }

    uintptr_t GetMesh() const {
        return Memory::ReadOffset<uintptr_t>(ptr, Offsets::Character::Mesh);
    }

    FVector GetBonePosition(int boneIdx) const {
        uintptr_t mesh = GetMesh();
        if (!mesh) return {};
        uintptr_t boneData = Memory::ReadOffset<uintptr_t>(mesh, Offsets::SkeletalMesh::BoneArray);
        if (!boneData) return {};
        uintptr_t boneAddr = boneData + boneIdx * sizeof(FTransform);
        auto transform = Memory::Read<FTransform>(boneAddr);
        return transform.Translation;
    }

    FVector GetHeadPos()  const { return GetBonePosition(Offsets::Bone::Head); }
    FVector GetChestPos() const { return GetBonePosition(Offsets::Bone::Chest); }
};
