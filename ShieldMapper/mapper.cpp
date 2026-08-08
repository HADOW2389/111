#include "mapper.h"
#include <cstdio>
#include <algorithm>

namespace Mapper {

    bool MapSections(HANDLE hDevice, uint64_t mappedBase, PIMAGE_NT_HEADERS64 pNt, const uint8_t* localImage) {
        auto pSection = IMAGE_FIRST_SECTION(pNt);

        for (WORD i = 0; i < pNt->FileHeader.NumberOfSections; i++) {
            if (!pSection[i].SizeOfRawData)
                continue;

            uint64_t destAddr = mappedBase + pSection[i].VirtualAddress;
            const uint8_t* srcData = localImage + pSection[i].PointerToRawData;

            if (!ShieldDriver::WriteKernel(hDevice, destAddr, srcData, pSection[i].SizeOfRawData)) {
                printf("[!] Failed to write section %.8s\n", pSection[i].Name);
                return false;
            }
            printf("[+] Mapped section %.8s -> 0x%llx (%u bytes)\n",
                pSection[i].Name, destAddr, pSection[i].SizeOfRawData);
        }
        return true;
    }

    bool FixRelocations(HANDLE hDevice, uint64_t mappedBase, uint64_t delta, PIMAGE_NT_HEADERS64 pNt, const uint8_t* localImage) {
        if (!delta) return true;

        auto& relocDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (!relocDir.VirtualAddress || !relocDir.Size) return true;

        auto* relocBlock = (IMAGE_BASE_RELOCATION*)(localImage + relocDir.VirtualAddress);
        uint64_t relocEnd = (uint64_t)relocBlock + relocDir.Size;

        while ((uint64_t)relocBlock < relocEnd && relocBlock->SizeOfBlock) {
            uint32_t entryCount = (relocBlock->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            auto* entries = (WORD*)((uint8_t*)relocBlock + sizeof(IMAGE_BASE_RELOCATION));

            for (uint32_t i = 0; i < entryCount; i++) {
                int type = entries[i] >> 12;
                int offset = entries[i] & 0xFFF;

                if (type == IMAGE_REL_BASED_DIR64) {
                    uint64_t patchAddr = mappedBase + relocBlock->VirtualAddress + offset;
                    uint64_t original = 0;
                    ShieldDriver::ReadKernel(hDevice, patchAddr, &original, sizeof(original));
                    original += delta;
                    ShieldDriver::WriteKernel(hDevice, patchAddr, &original, sizeof(original));
                }
                else if (type == IMAGE_REL_BASED_HIGHLOW) {
                    uint64_t patchAddr = mappedBase + relocBlock->VirtualAddress + offset;
                    uint32_t original = 0;
                    ShieldDriver::ReadKernel(hDevice, patchAddr, &original, sizeof(original));
                    original += (uint32_t)delta;
                    ShieldDriver::WriteKernel(hDevice, patchAddr, &original, sizeof(original));
                }
            }

            relocBlock = (IMAGE_BASE_RELOCATION*)((uint8_t*)relocBlock + relocBlock->SizeOfBlock);
        }

        printf("[+] Relocations fixed (delta: 0x%llx)\n", delta);
        return true;
    }

    bool ResolveImports(HANDLE hDevice, uint64_t mappedBase, PIMAGE_NT_HEADERS64 pNt, const uint8_t* localImage) {
        auto& importDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!importDir.VirtualAddress || !importDir.Size) return true;

        auto* importDesc = (IMAGE_IMPORT_DESCRIPTOR*)(localImage + importDir.VirtualAddress);

        while (importDesc->Name) {
            const char* moduleName = (const char*)(localImage + importDesc->Name);

            uint64_t moduleBase = ShieldDriver::GetKernelModuleBase(moduleName);
            if (!moduleBase) {
                char stripped[256]{};
                strncpy_s(stripped, moduleName, sizeof(stripped) - 1);
                char* dot = strrchr(stripped, '.');
                if (dot) *dot = 0;
                strcat_s(stripped, ".sys");
                moduleBase = ShieldDriver::GetKernelModuleBase(stripped);
            }

            if (!moduleBase) {
                printf("[!] Failed to find kernel module: %s\n", moduleName);
                return false;
            }

            printf("[+] Resolving imports from %s (0x%llx)\n", moduleName, moduleBase);

            auto* thunkRef = (IMAGE_THUNK_DATA64*)(localImage +
                (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));
            uint64_t iatAddr = mappedBase + importDesc->FirstThunk;

            while (thunkRef->u1.AddressOfData) {
                uint64_t funcAddr = 0;

                if (thunkRef->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                    printf("[!] Ordinal imports not supported\n");
                    return false;
                }
                else {
                    auto* importByName = (IMAGE_IMPORT_BY_NAME*)(localImage + thunkRef->u1.AddressOfData);
                    funcAddr = ShieldDriver::GetKernelExport(hDevice, moduleBase, importByName->Name);
                    if (!funcAddr) {
                        printf("[!] Failed to resolve: %s!%s\n", moduleName, importByName->Name);
                        return false;
                    }
                }

                ShieldDriver::WriteKernel(hDevice, iatAddr, &funcAddr, sizeof(funcAddr));

                thunkRef++;
                iatAddr += sizeof(uint64_t);
            }

            importDesc++;
        }

        printf("[+] All imports resolved\n");
        return true;
    }

    bool MapDriver(HANDLE hDevice, const std::vector<uint8_t>& driverImage) {
        if (driverImage.size() < sizeof(IMAGE_DOS_HEADER)) {
            printf("[!] Invalid driver image\n");
            return false;
        }

        auto* pDos = (IMAGE_DOS_HEADER*)driverImage.data();
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) {
            printf("[!] Invalid DOS signature\n");
            return false;
        }

        auto* pNt = (IMAGE_NT_HEADERS64*)(driverImage.data() + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE) {
            printf("[!] Invalid NT signature\n");
            return false;
        }

        if (pNt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
            printf("[!] Not a 64-bit driver\n");
            return false;
        }

        uint64_t imageSize = pNt->OptionalHeader.SizeOfImage;
        printf("[*] Image size: %llu bytes\n", imageSize);

        uint64_t mappedBase = ShieldDriver::AllocatePool(hDevice, imageSize);
        if (!mappedBase) {
            printf("[!] Failed to allocate kernel memory\n");
            return false;
        }

        printf("[+] Kernel allocation @ 0x%llx\n", mappedBase);

        uint8_t* localMapped = new uint8_t[imageSize]{};
        memcpy(localMapped, driverImage.data(), std::min((uint64_t)driverImage.size(), imageSize));

        auto* localNt = (IMAGE_NT_HEADERS64*)(localMapped + pDos->e_lfanew);
        auto* localSection = IMAGE_FIRST_SECTION(localNt);
        for (WORD i = 0; i < localNt->FileHeader.NumberOfSections; i++) {
            if (localSection[i].SizeOfRawData) {
                memcpy(
                    localMapped + localSection[i].VirtualAddress,
                    driverImage.data() + localSection[i].PointerToRawData,
                    localSection[i].SizeOfRawData
                );
            }
        }

        uint64_t delta = mappedBase - pNt->OptionalHeader.ImageBase;

        if (!FixRelocations(hDevice, mappedBase, delta, localNt, localMapped)) {
            printf("[!] Relocation fix failed\n");
            ShieldDriver::FreePool(hDevice, mappedBase);
            delete[] localMapped;
            return false;
        }

        if (!ResolveImports(hDevice, mappedBase, localNt, localMapped)) {
            printf("[!] Import resolution failed\n");
            ShieldDriver::FreePool(hDevice, mappedBase);
            delete[] localMapped;
            return false;
        }

        if (!ShieldDriver::WriteKernel(hDevice, mappedBase, localMapped, imageSize)) {
            printf("[!] Failed to write mapped image\n");
            ShieldDriver::FreePool(hDevice, mappedBase);
            delete[] localMapped;
            return false;
        }

        delete[] localMapped;

        auto& headerDir = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        (void)headerDir;

        uint64_t entryPoint = mappedBase + pNt->OptionalHeader.AddressOfEntryPoint;
        printf("[*] DriverEntry @ 0x%llx\n", entryPoint);

        if (!ShieldDriver::CallDriverEntry(hDevice, entryPoint, mappedBase)) {
            printf("[!] DriverEntry call failed\n");
            ShieldDriver::FreePool(hDevice, mappedBase);
            return false;
        }

        uint64_t peHeaderSize = pNt->OptionalHeader.SizeOfHeaders;
        std::vector<uint8_t> zeros(peHeaderSize, 0);
        ShieldDriver::WriteKernel(hDevice, mappedBase, zeros.data(), peHeaderSize);
        printf("[+] PE headers wiped\n");

        printf("[+] Driver mapped successfully @ 0x%llx\n", mappedBase);
        return true;
    }
}
