#include "mapper.h"
#include <fstream>
#include <cstdio>
#include <filesystem>

std::vector<uint8_t> ReadFileToBuffer(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    file.read((char*)buffer.data(), size);
    return buffer;
}

bool LoadShieldService(const std::string& sysPath) {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        printf("[!] Failed to open SCM (run as admin)\n");
        return false;
    }

    SC_HANDLE svc = CreateServiceA(
        scm, "ShieldSvc", "Shield Driver",
        SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        sysPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            svc = OpenServiceA(scm, "ShieldSvc", SERVICE_ALL_ACCESS);
        }
        if (!svc) {
            printf("[!] Failed to create/open service: %lu\n", GetLastError());
            CloseServiceHandle(scm);
            return false;
        }
    }

    BOOL started = StartServiceA(svc, 0, nullptr);
    if (!started) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            printf("[!] Failed to start shield service: %lu\n", err);
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return false;
        }
    }

    printf("[+] shield.sys service started\n");
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

int main(int argc, char* argv[]) {
    printf("=== ShieldMapper v2.0 ===\n");
    printf("=== shield.sys kernel mapper ===\n\n");

    bool testOnly = false;
    std::string driverPath;
    std::string shieldPath;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) {
            testOnly = true;
        }
        else if (strcmp(argv[i], "--shield") == 0 && i + 1 < argc) {
            shieldPath = argv[++i];
        }
        else {
            driverPath = argv[i];
        }
    }

    if (!testOnly && driverPath.empty()) {
        printf("Usage:\n");
        printf("  ShieldMapper.exe <driver.sys>              Map driver\n");
        printf("  ShieldMapper.exe --test                    Test kernel R/W only\n");
        printf("  ShieldMapper.exe --shield path\\shield.sys  Load shield.sys first\n");
        return 1;
    }

    std::vector<uint8_t> driverImage;
    if (!testOnly) {
        driverImage = ReadFileToBuffer(driverPath);
        if (driverImage.empty()) {
            printf("[!] Failed to read driver file: %s\n", driverPath.c_str());
            return 1;
        }
        printf("[+] Read %llu bytes from %s\n", (uint64_t)driverImage.size(), driverPath.c_str());
    }

    HANDLE hDevice = CreateFileW(
        L"\\\\.\\EAZShield",
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("[*] Device not found, trying to load shield.sys...\n");

        if (shieldPath.empty()) {
            char exePath[MAX_PATH]{};
            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            shieldPath = std::filesystem::path(exePath).parent_path().string() + "\\shield.sys";
        }

        if (!std::filesystem::exists(shieldPath)) {
            printf("[!] shield.sys not found at: %s\n", shieldPath.c_str());
            printf("[!] Place shield.sys next to ShieldMapper.exe or use --shield <path>\n");
            return 1;
        }

        char fullPath[MAX_PATH]{};
        GetFullPathNameA(shieldPath.c_str(), MAX_PATH, fullPath, nullptr);

        if (!LoadShieldService(fullPath)) {
            return 1;
        }

        Sleep(1000);

        hDevice = CreateFileW(
            L"\\\\.\\EAZShield",
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr
        );

        if (hDevice == INVALID_HANDLE_VALUE) {
            printf("[!] Still can't open device after loading: %lu\n", GetLastError());
            return 1;
        }
    }

    printf("[+] Device opened: 0x%p\n", hDevice);

    uint64_t ntBase = ShieldDriver::GetNtoskrnlBase();
    if (!ntBase) {
        printf("[!] Failed to get ntoskrnl base\n");
        CloseHandle(hDevice);
        return 1;
    }
    printf("[+] ntoskrnl.exe @ 0x%llx\n", ntBase);

    printf("[*] Testing kernel read...\n");
    uint16_t testVal = 0;
    if (!ShieldDriver::ReadKernel(hDevice, ntBase, &testVal, sizeof(testVal))) {
        printf("[!] ReadKernel failed (DeviceIoControl error)\n");
        CloseHandle(hDevice);
        return 1;
    }

    printf("[*] Read value: 0x%04X (expected 0x%04X)\n", testVal, IMAGE_DOS_SIGNATURE);

    if (testVal != IMAGE_DOS_SIGNATURE) {
        printf("[!] Kernel R/W verification FAILED\n");
        CloseHandle(hDevice);
        return 1;
    }
    printf("[+] Kernel R/W verified!\n");

    if (testOnly) {
        printf("\n[+] Test passed, kernel R/W works!\n");
        CloseHandle(hDevice);
        return 0;
    }

    bool success = Mapper::MapDriver(hDevice, driverImage);

    CloseHandle(hDevice);

    if (success) {
        printf("\n[+] Driver mapped successfully!\n");
        return 0;
    }
    else {
        printf("\n[!] Driver mapping failed\n");
        return 1;
    }
}
