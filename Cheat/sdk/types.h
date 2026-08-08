#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

using int8   = std::int8_t;
using int16  = std::int16_t;
using int32  = std::int32_t;
using int64  = std::int64_t;
using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

struct FVector2D {
    float X, Y;
    FVector2D() : X(0.f), Y(0.f) {}
    FVector2D(float x, float y) : X(x), Y(y) {}
};

struct FVector {
    float X, Y, Z;
    FVector() : X(0.f), Y(0.f), Z(0.f) {}
    FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

    FVector operator-(const FVector& v) const { return { X - v.X, Y - v.Y, Z - v.Z }; }
    FVector operator+(const FVector& v) const { return { X + v.X, Y + v.Y, Z + v.Z }; }
    FVector operator*(float s)          const { return { X * s,   Y * s,   Z * s   }; }

    float Dot(const FVector& v)   const { return X * v.X + Y * v.Y + Z * v.Z; }
    float SizeSquared()           const { return X*X + Y*Y + Z*Z; }
    float Size()                  const { return sqrtf(SizeSquared()); }
    float DistanceTo(const FVector& v) const { return (*this - v).Size(); }
};

struct FRotator {
    float Pitch, Yaw, Roll;
    FRotator() : Pitch(0.f), Yaw(0.f), Roll(0.f) {}
    FRotator(float p, float y, float r) : Pitch(p), Yaw(y), Roll(r) {}
};

struct FQuat {
    float X, Y, Z, W;
};

struct FMatrix {
    float M[4][4];
    FVector GetOrigin() const { return { M[3][0], M[3][1], M[3][2] }; }
};

template<typename T>
struct TArray {
    T*    Data;
    int32 Count;
    int32 Max;

    TArray() : Data(nullptr), Count(0), Max(0) {}
    int  Num()  const { return Count; }
    bool IsValidIndex(int i) const { return i >= 0 && i < Count; }
    T&   operator[](int i) { return Data[i]; }
};

struct FString : TArray<wchar_t> {
    std::wstring ToWString() const {
        if (!Data || Count <= 0) return L"";
        return std::wstring(Data, Count - 1);
    }
    std::string ToString() const {
        auto ws = ToWString();
        return std::string(ws.begin(), ws.end());
    }
};

struct FName {
    int32 ComparisonIndex;
    int32 Number;
};

struct UObject {
    void*  VTable;
    int32  ObjectFlags;
    int32  InternalIndex;
    void*  ClassPrivate;
    FName  NamePrivate;
    void*  OuterPrivate;
};
