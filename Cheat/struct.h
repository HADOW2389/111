#pragma once
#include <iostream>
using namespace std;
#include <Windows.h>
#include "Offset.h"

template <typename T>
inline T RPM(void* Addr)
{
	T buf;
	buf = *reinterpret_cast<T*>(Addr);
	return buf;
}
template <typename T>
inline T RPM(uintptr_t Addr)
{
	T buf;
	buf = *reinterpret_cast<T*>(Addr);
	return buf;
}
template<typename T>
bool RPM(ULONGLONG address, T* buffer, size_t size)
{
	if (!address || !buffer)
		return false;

	memcpy(buffer, reinterpret_cast<void*>(address), size);
	return true;
}
inline bool IsBadReadPtr(void* p)
{
	return (BOOL)((uintptr_t)p < 0xFFFF || (uintptr_t)p > 0x7FFFFFFFFFFF || (uintptr_t)p % sizeof(uintptr_t));
};

struct D3DMATRIX {
	union {
		struct {
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float m[4][4];
	};
};
#define M_PI 3.1415926535
#define __ROL__(value, count) (((value) << (count)) | ((value) >> (32 - (count))))
#define __ROR4__(value, count) __ROL__((value), (32 - (count)))
#define __ROL4__(value, count) (((value) << (count)) | ((value) >> (32 - (count))))

typedef float vec_t;

#pragma once

#include <iostream>

struct pos
{
	float x, y, z;
};

typedef struct StringA
{
	char buffer[1024];
};

struct UEVector {
	float X; // 0x0(0x8)
	float Y; // 0x8(0x8)
	float Z; // 0x10(0x8)
};
// ScriptStruct CoreUObject.Rotator
struct UERotator {
	float Pitch; // 0x0(0x8)
	float Yaw; // 0x8(0x8)
	float Roll; // 0x10(0x8)
};

class FMatrix
{
public:
	float _11, _12, _13, _14;
	float _21, _22, _23, _24;
	float _31, _32, _33, _34;
	float _41, _42, _43, _44;
	float m[4][4];
	FMatrix MatrixMultiplication(const FMatrix& other);
};
struct FQuat
{
	float X;
	float Y;
	float Z;
	float W;
};
struct FTransform
{
	FQuat Rotation;
	FQuat Translation;
	FQuat Scale3D;

	FMatrix ToMatrixWithScale();
};


class Vector2D
{
public:
	Vector2D(void)
	{
		x = y = 0.0f;
	}

	Vector2D(float X, float Y)
	{
		x = X; y = Y;
	}

	Vector2D(float* v)
	{
		x = v[0]; y = v[1];
	}

	Vector2D(const float* v)
	{
		x = v[0]; y = v[1];
	}

	Vector2D(const Vector2D& v)
	{
		x = v.x; y = v.y;
	}

	Vector2D& operator=(const Vector2D& v)
	{
		x = v.x; y = v.y; return *this;
	}

	float& operator[](int i)
	{
		return ((float*)this)[i];
	}

	float operator[](int i) const
	{
		return ((float*)this)[i];
	}

	Vector2D& operator+=(const Vector2D& v)
	{
		x += v.x; y += v.y; return *this;
	}

	Vector2D& operator-=(const Vector2D& v)
	{
		x -= v.x; y -= v.y; return *this;
	}

	Vector2D& operator*=(const Vector2D& v)
	{
		x *= v.x; y *= v.y; return *this;
	}

	Vector2D& operator/=(const Vector2D& v)
	{
		x /= v.x; y /= v.y; return *this;
	}

	Vector2D& operator+=(float v)
	{
		x += v; y += v; return *this;
	}

	Vector2D& operator-=(float v)
	{
		x -= v; y -= v; return *this;
	}

	Vector2D& operator*=(float v)
	{
		x *= v; y *= v; return *this;
	}

	Vector2D& operator/=(float v)
	{
		x /= v; y /= v; return *this;
	}

	Vector2D operator+(const Vector2D& v) const
	{
		return Vector2D(x + v.x, y + v.y);
	}

	Vector2D operator-(const Vector2D& v) const
	{
		return Vector2D(x - v.x, y - v.y);
	}

	Vector2D operator*(const Vector2D& v) const
	{
		return Vector2D(x * v.x, y * v.y);
	}

	Vector2D operator/(const Vector2D& v) const
	{
		return Vector2D(x / v.x, y / v.y);
	}

	Vector2D operator+(float v) const
	{
		return Vector2D(x + v, y + v);
	}

	Vector2D operator-(float v) const
	{
		return Vector2D(x - v, y - v);
	}

	Vector2D operator*(float v) const
	{
		return Vector2D(x * v, y * v);
	}

	Vector2D operator/(float v) const
	{
		return Vector2D(x / v, y / v);
	}

	void Set(float X = 0.0f, float Y = 0.0f)
	{
		x = X; y = Y;
	}

	float Lenght(void) const
	{
		return ::sqrtf(x * x + y * y);
	}

	float LenghtSqr(void) const
	{
		return (x * x + y * y);
	}

	float DistTo(const Vector2D& v) const
	{
		return (*this - v).Lenght();
	}

	float DistToSqr(const Vector2D& v) const
	{
		return (*this - v).LenghtSqr();
	}

	float Dot(const Vector2D& v) const
	{
		return (x * v.x + y * v.y);
	}

	bool IsZero(void) const
	{
		return (x > -0.01f && x < 0.01f &&
			y > -0.01f && y < 0.01f);
	}

public:
	vec_t x, y;
};

class Vector3 {
public:
	Vector3() : x(0.f), y(0.f), z(0.f) {}
	Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
	~Vector3() {}

	float x;
	float y;
	float z;

	inline float Dot(Vector3 v) {
		return x * v.x + y * v.y + z * v.z;
	}

	inline float Distance(Vector3 v) {
		return float(sqrtf(powf(v.x - x, 2.0) + powf(v.y - y, 2.0) + powf(v.z - z, 2.0)));
	}

	Vector3 operator+(Vector3 v) {
		return Vector3(x + v.x, y + v.y, z + v.z);
	}

	Vector3 operator-(Vector3 v) {
		return Vector3(x - v.x, y - v.y, z - v.z);
	}

	Vector3 operator/(float flDiv) {
		return Vector3(x / flDiv, y / flDiv, z / flDiv);
	}

	Vector3 operator*(float Scale) {

		return Vector3(x * Scale, y * Scale, z * Scale);
	}

	Vector3 operator-=(Vector3 v) {

		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	}

	inline float Length() {
		return sqrtf((x * x) + (y * y) + (z * z));
	}
};

inline D3DMATRIX getViewMatrix()
{
	D3DMATRIX m;

	return m;
}

inline pos WorldToScreen(pos target, int mode = 0) //0 竖 1 横
{
	pos _worldToScreenPos;
	pos to;
	float w = 0.0f;
	D3DMATRIX viewmatrix = getViewMatrix();

	if (mode == 0)
	{
		// 按竖矩阵方式来修改矩阵乘法
		to.x = viewmatrix._11 * target.x + viewmatrix._12 * target.y + viewmatrix._13 * target.z + viewmatrix._14;
		to.y = viewmatrix._21 * target.x + viewmatrix._22 * target.y + viewmatrix._23 * target.z + viewmatrix._24;
		w = viewmatrix._41 * target.x + viewmatrix._42 * target.y + viewmatrix._43 * target.z + viewmatrix._44;
	}
	else
	{
		// 按横矩阵方式来修改矩阵乘法
		to.x = viewmatrix._11 * target.x + viewmatrix._21 * target.y + viewmatrix._31 * target.z + viewmatrix._41;
		to.y = viewmatrix._12 * target.x + viewmatrix._22 * target.y + viewmatrix._32 * target.z + viewmatrix._42;
		w = viewmatrix._13 * target.x + viewmatrix._23 * target.y + viewmatrix._33 * target.z + viewmatrix._43;
	}


	if (w < 0.01f)
		return pos{ 0, 0, 0 };

	float z_dis = w / 30;

	to.x *= (1.0f / w);
	to.y *= (1.0f / w);

	int width = ImGui::GetIO().DisplaySize.x; //ScreenWdith
	int height = ImGui::GetIO().DisplaySize.y; //ScreenHeight

	float x = width / 2;
	float y = height / 2;

	x += 0.5f * to.x * width + 0.5f;
	y -= 0.5f * to.y * height + 0.5f;

	to.x = x;
	to.y = y;

	_worldToScreenPos.x = to.x;
	_worldToScreenPos.y = to.y;
	_worldToScreenPos.z = z_dis;
	return _worldToScreenPos;
}

inline void Aimbot(Vector2D vec, float smooth)
{
	float x = vec.x;
	float y = vec.y;

	float center_x = ImGui::GetIO().DisplaySize.x / 2;
	float center_y = ImGui::GetIO().DisplaySize.y / 2;

	float target_x = 0.f;
	float target_y = 0.f;

	if (x != 0.f)
	{
		if (x > center_x)
		{
			target_x = -(center_x - x);
			target_x /= smooth;
			if (target_x + center_x > center_x * 2.f) target_x = 0.f;
		}

		if (x < center_x)
		{
			target_x = x - center_x;
			target_x /= smooth;
			if (target_x + center_x < 0.f) target_x = 0.f;
		}
	}
	if (y != 0.f)
	{
		if (y > center_y)
		{
			target_y = -(center_y - y);
			target_y /= smooth;
			if (target_y + center_y > center_y * 2.f) target_y = 0.f;
		}

		if (y < center_y)
		{
			target_y = y - center_y;
			target_y /= smooth;
			if (target_y + center_y < 0.f) target_y = 0.f;
		}
	}

	//SLOWLYDrv_MouseMove(static_cast<DWORD>(target_x), static_cast<DWORD>(target_y), 0, 0);
}

typedef int64_t(__fastcall* DecryptFunctoin)(uintptr_t key, uintptr_t argv);
inline DecryptFunctoin fnDecryptFunctoin = NULL;
inline uint64_t Tmpadd;

inline bool InitDecrypt(uint64_t offset)
{
	uintptr_t DecryptPtr = RPM<uintptr_t>(offset::Base + offset);

	while (!DecryptPtr)
	{
		DecryptPtr = RPM<uintptr_t>(  offset::Base + offset);
		Sleep(1000);
	}

	int32_t Tmp1Add = RPM<int32_t>(  DecryptPtr + 3);
	if (!Tmp1Add)
		return 0;

	Tmpadd = Tmp1Add + DecryptPtr + 7;
	unsigned char ShellcodeBuff[1024] = { NULL };

	ShellcodeBuff[0] = 0x90;  // NOP
	ShellcodeBuff[1] = 0x90;  // NOP

	RPM(DecryptPtr, &ShellcodeBuff[2], sizeof(ShellcodeBuff) - 2);


	if (!ShellcodeBuff)
		return 0;

	// 设置解密函数的Shellcode
	ShellcodeBuff[2] = 0x48;  // MOV RAX, [RCX]
	ShellcodeBuff[3] = 0x8B;  // MOV
	ShellcodeBuff[4] = 0xC1;  // MOV RAX, [RCX]
	ShellcodeBuff[5] = 0x90;  // NOP
	ShellcodeBuff[6] = 0x90;  // NOP
	ShellcodeBuff[7] = 0x90;  // NOP
	ShellcodeBuff[8] = 0x90;
	fnDecryptFunctoin = (DecryptFunctoin)VirtualAlloc(NULL, sizeof(ShellcodeBuff) + 4, MEM_COMMIT, PAGE_EXECUTE_READWRITE);

	if (!fnDecryptFunctoin)
		return 0;

	RtlCopyMemory((LPVOID)fnDecryptFunctoin, (LPVOID)ShellcodeBuff, sizeof(ShellcodeBuff));

	return 1;
}

inline uintptr_t xe_decrypt(const uintptr_t encrypted)
{
	uintptr_t data = fnDecryptFunctoin(Tmpadd, encrypted);

	if (!data) return 0;

	return data;
}

#if DECRYPTION_INDEX == 1
#define	offs_Actor_ObjID 28
inline DWORD DecryptCIndex(DWORD value)
{
	return (((value ^ 0x221A03AC) << 31) | ((value ^ 0x221A03ACu) >> 1) & 0x7FFF0000) ^ __ROR4__(value ^ 0x221A03AC, 17) ^ 0x12082DD7;
}
#else
#define	offs_Actor_ObjID 32
inline DWORD DecryptCIndex(DWORD value)
{
	return (((value ^ offset::DecryptNameIndexXorKey1) << 25) | ((value ^ offset::DecryptNameIndexXorKey1) >> 7) & offset::DecryptNameIndexXorKey3) ^ __ROR4__(value ^ offset::DecryptNameIndexXorKey1, 23) ^ offset::DecryptNameIndexXorKey2;
}
#endif

inline std::string GetNames(uintptr_t GNamesAddr, DWORD ID)
{
	std::string emp = "Unknown";
	if (ID <= 0) return emp;
	uint32_t IdDiv = ID / offset::ElementsPerChunk;
	uint32_t Idtemp = ID % offset::ElementsPerChunk;
	uint64_t Serial = RPM<uint64_t>(  GNamesAddr + IdDiv * 0x8);
	if (!Serial || Serial < 0x100000)
		return emp;
	uint64_t pName = RPM<uint64_t>(  Serial + 0x8 * Idtemp);
	if (!pName || pName < 0x100000)
		return emp;

	StringA names = RPM<StringA>(pName + 0x10);

	char te[256];
	memset(&te, 0, 256);
	if (memcmp(names.buffer, te, 256) == 0)
		return emp;

	std::string str(names.buffer);
	return str;
}

inline D3DMATRIX toMatrix(Vector3 Rotation, Vector3 origin = Vector3(0, 0, 0))
{
	float Pitch = (Rotation.x * float(M_PI) / 180.f);
	float Yaw = (Rotation.y * float(M_PI) / 180.f);
	float Roll = (Rotation.z * float(M_PI) / 180.f);

	float SP = sinf(Pitch);
	float CP = cosf(Pitch);
	float SY = sinf(Yaw);
	float CY = cosf(Yaw);
	float SR = sinf(Roll);
	float CR = cosf(Roll);

	D3DMATRIX Matrix;
	Matrix._11 = CP * CY;
	Matrix._12 = CP * SY;
	Matrix._13 = SP;
	Matrix._14 = 0.f;

	Matrix._21 = SR * SP * CY - CR * SY;
	Matrix._22 = SR * SP * SY + CR * CY;
	Matrix._23 = -SR * CP;
	Matrix._24 = 0.f;

	Matrix._31 = -(CR * SP * CY + SR * SY);
	Matrix._32 = CY * SR - CR * SP * SY;
	Matrix._33 = CR * CP;
	Matrix._34 = 0.f;

	Matrix._41 = origin.x;
	Matrix._42 = origin.y;
	Matrix._43 = origin.z;
	Matrix._44 = 1.f;

	return Matrix;
}

inline FMatrix FMatrix::MatrixMultiplication(const FMatrix& other)
{
	FMatrix NewMatrix;

	NewMatrix._11 = this->_11 * other._11 + this->_12 * other._21 + this->_13 * other._31 + this->_14 * other._41;
	NewMatrix._12 = this->_11 * other._12 + this->_12 * other._22 + this->_13 * other._32 + this->_14 * other._42;
	NewMatrix._13 = this->_11 * other._13 + this->_12 * other._23 + this->_13 * other._33 + this->_14 * other._43;
	NewMatrix._14 = this->_11 * other._14 + this->_12 * other._24 + this->_13 * other._34 + this->_14 * other._44;
	NewMatrix._21 = this->_21 * other._11 + this->_22 * other._21 + this->_23 * other._31 + this->_24 * other._41;
	NewMatrix._22 = this->_21 * other._12 + this->_22 * other._22 + this->_23 * other._32 + this->_24 * other._42;
	NewMatrix._23 = this->_21 * other._13 + this->_22 * other._23 + this->_23 * other._33 + this->_24 * other._43;
	NewMatrix._24 = this->_21 * other._14 + this->_22 * other._24 + this->_23 * other._34 + this->_24 * other._44;
	NewMatrix._31 = this->_31 * other._11 + this->_32 * other._21 + this->_33 * other._31 + this->_34 * other._41;
	NewMatrix._32 = this->_31 * other._12 + this->_32 * other._22 + this->_33 * other._32 + this->_34 * other._42;
	NewMatrix._33 = this->_31 * other._13 + this->_32 * other._23 + this->_33 * other._33 + this->_34 * other._43;
	NewMatrix._34 = this->_31 * other._14 + this->_32 * other._24 + this->_33 * other._34 + this->_34 * other._44;
	NewMatrix._41 = this->_41 * other._11 + this->_42 * other._21 + this->_43 * other._31 + this->_44 * other._41;
	NewMatrix._42 = this->_41 * other._12 + this->_42 * other._22 + this->_43 * other._32 + this->_44 * other._42;
	NewMatrix._43 = this->_41 * other._13 + this->_42 * other._23 + this->_43 * other._33 + this->_44 * other._43;
	NewMatrix._44 = this->_41 * other._14 + this->_42 * other._24 + this->_43 * other._34 + this->_44 * other._44;

	return NewMatrix;
}
inline Vector2D worldToScreen(Vector3 worldPos, Vector3 camPos, Vector3 camRot, float camFov)
{
	Vector2D screen_location = Vector2D(0, 0);

	D3DMATRIX tempMatrix = toMatrix(camRot);

	Vector3 vAxisX, vAxisY, vAxisZ;

	vAxisX = Vector3(tempMatrix.m[0][0], tempMatrix.m[0][1], tempMatrix.m[0][2]);
	vAxisY = Vector3(tempMatrix.m[1][0], tempMatrix.m[1][1], tempMatrix.m[1][2]);
	vAxisZ = Vector3(tempMatrix.m[2][0], tempMatrix.m[2][1], tempMatrix.m[2][2]);

	Vector3 vDelta = worldPos - camPos;
	Vector3 vTransformed = Vector3(vDelta.Dot(vAxisY), vDelta.Dot(vAxisZ), vDelta.Dot(vAxisX));

	if (vTransformed.z < .1f)
		vTransformed.z = .1f;

	float FovAngle = camFov;
	float ScreenCenterX = ImGui::GetIO().DisplaySize.x / 2.0f;
	float ScreenCenterY = ImGui::GetIO().DisplaySize.y / 2.0f;

	screen_location.x = ScreenCenterX + vTransformed.x * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	screen_location.y = ScreenCenterY - vTransformed.y * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;

	return screen_location;
}

inline int IsPlayer(std::string entity_name) {
	if (entity_name.empty() || entity_name == "" || entity_name == "Unknown") { return 0; }
	if (entity_name == "AIPawn_Base_Female_C") { return 1; }
	if (entity_name == "AIPawn_Base_Male_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_Female_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_Male_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_Pillar_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_Female_Pillar_C") { return 1; }
	if (entity_name == "UltAIPawn_Base_Male_Pillar_C") { return 1; }
	if (entity_name == "BP_MarketAI_Pawn_C") { return 1; }
	if (entity_name == "RegistedPlayer") { return 2; }
	if (entity_name == "PlayerMale_A") { return 2; }
	if (entity_name == "PlayerMale_A_C") { return 2; }
	if (entity_name == "PlayerFemale_A") { return 2; }
	if (entity_name == "PlayerFemale_A_C") { return 2; }
	return 0;
}

inline FMatrix FTransform::ToMatrixWithScale()
{
	FMatrix m;

	m._41 = Translation.X;
	m._42 = Translation.Y;
	m._43 = Translation.Z;

	float x2 = Rotation.X + Rotation.X;
	float y2 = Rotation.Y + Rotation.Y;
	float z2 = Rotation.Z + Rotation.Z;

	float xx2 = Rotation.X * x2;
	float yy2 = Rotation.Y * y2;
	float zz2 = Rotation.Z * z2;
	m._11 = (1.0f - (yy2 + zz2)) * Scale3D.X;
	m._22 = (1.0f - (xx2 + zz2)) * Scale3D.Y;
	m._33 = (1.0f - (xx2 + yy2)) * Scale3D.Z;


	float yz2 = Rotation.Y * z2;
	float wx2 = Rotation.W * x2;
	m._32 = (yz2 - wx2) * Scale3D.Z;
	m._23 = (yz2 + wx2) * Scale3D.Y;


	float xy2 = Rotation.X * y2;
	float wz2 = Rotation.W * z2;
	m._21 = (xy2 - wz2) * Scale3D.Y;
	m._12 = (xy2 + wz2) * Scale3D.X;


	float xz2 = Rotation.X * z2;
	float wy2 = Rotation.W * y2;
	m._31 = (xz2 + wy2) * Scale3D.Z;
	m._13 = (xz2 - wy2) * Scale3D.X;

	m._14 = 0.0f;
	m._24 = 0.0f;
	m._34 = 0.0f;
	m._44 = 1.0f;

	return m;
}

inline void DecryptHealth(uint8_t* value, uint32_t size, uint32_t offset)
{
	static constexpr uint32_t Health_keys[16] = {
		offset::Health_keys0,  offset::Health_keys1,
		offset::Health_keys2,  offset::Health_keys3,
		offset::Health_keys4,  offset::Health_keys5,
		offset::Health_keys6,  offset::Health_keys7,
		offset::Health_keys8,  offset::Health_keys9,
		offset::Health_keys10, offset::Health_keys11,
		offset::Health_keys12, offset::Health_keys13,
		offset::Health_keys14, offset::Health_keys15
	};
	for (auto i = 0; i < size; i++)
		value[i] ^= *((uint8_t*)Health_keys + ((i + offset) & 0x3F));
}

inline float GetHealth(uintptr_t actor)
{
	__int64 v2; // rax
	float v3; // xmm0_4
	float v5; // [rsp+30h] [rbp+8h]

	if (RPM<uint8_t>(  actor + offset::HeaFlag) != 3 && RPM<uint32_t>(actor + offset::Health1))
	{
		uint8_t a = RPM<uint8_t>(actor + offset::Health5);
		if (!a) return 0;
		bool bIsEncrypted = a != 0;
		v2 = RPM<uint8_t>(  actor + offset::Health3);
		if (!v2) return 0;


		v3 = RPM<float>(  v2 + actor + offset::Health4);
		if (!v3) return 0;

		v5 = v3;

		if (bIsEncrypted)
		{
			uintptr_t b = RPM<uint32_t>(actor + offset::Health6);
			if (!b) return 0;
			DecryptHealth((uint8_t*)&v5, 4u, b);
			v3 = v5;
		}
	}
	else
	{
		v3 = RPM<float>(  actor + offset::Health2);
		if (!v3) return 0;
	}

	return v3;
}

inline Vector3 GetBoneMatrix(FTransform Bone, FTransform ToWorld)
{
	FMatrix Matrix;
	Matrix = Bone.ToMatrixWithScale().MatrixMultiplication(ToWorld.ToMatrixWithScale());
	return { Matrix._41, Matrix._42, Matrix._43 };
}

inline Vector3 GetBonePos(FTransform ComponentToWorld, uintptr_t StaticMesh, int index)
{
	FTransform Position = RPM<FTransform>(StaticMesh + index * 0x30);
	if (!&Position)
		return {};

	Vector3 HeadPos = GetBoneMatrix(Position, ComponentToWorld);
	return HeadPos;
}

inline void FilledLine(float x, float y, float x1, float y1, int thiness, ImColor color)
{
	ImGui::GetBackgroundDrawList()->AddLine(ImVec2(x, y), ImVec2(x1, y1), color, thiness);
}