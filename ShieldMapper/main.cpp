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

bool LoadShieldDriver() {
    HANDLE test = CreateFileW(L"\\\\.\\EAZShield", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (test != INVALID_HANDLE_VALUE) {
        CloseHandle(test);
        printf("[+] shield.sys already loaded\n");
        return true;
    }

    std::string sysPath;
    char winDir[MAX_PATH]{};
    GetWindowsDirectoryA(winDir, MAX_PATH);

    std::string driverDest = std::string(winDir) + "\\System32\\drivers\\shield.sys";

    if (!std::filesystem::exists(driverDest)) {
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir = std::filesystem::path(exePath).parent_path().string();
        std::string driverSrc = exeDir + "\\shield.sys";

        if (!std::filesystem::exists(driverSrc)) {
            printf("[!] shield.sys not found next to executable\n");
            return false;
        }

        CopyFileA(driverSrc.c_str(), driverDest.c_str(), FALSE);
    }

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        printf("[!] Failed to open SCM (run as admin)\n");
        return false;
    }

    SC_HANDLE svc = OpenServiceA(scm, "EAZShield", SERVICE_ALL_ACCESS);
    if (svc) {
        SERVICE_STATUS ss{};
        ControlService(svc, SERVICE_CONTROL_STOP, &ss);
        DeleteService(svc);
        CloseServiceHandle(svc);
        Sleep(300);
        svc = nullptr;
    }

    svc = CreateServiceA(
        scm, "EAZShield", "EAZ Shield Driver",
        SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        driverDest.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (!svc) {
        printf("[!] Failed to create service: %lu\n", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    if (!StartServiceA(svc, 0, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            printf("[!] Failed to start service: %lu\n", err);
            DeleteService(svc);
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return false;
        }
    }

    printf("[+] shield.sys loaded\n");
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

void UnloadShieldDriver() {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return;

    SC_HANDLE svc = OpenServiceA(scm, "EAZShield", SERVICE_ALL_ACCESS);
    if (svc) {
        SERVICE_STATUS status{};
        ControlService(svc, SERVICE_CONTROL_STOP, &status);
        DeleteService(svc);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);

    char winDir[MAX_PATH]{};
    GetWindowsDirectoryA(winDir, MAX_PATH);
    std::string driverPath = std::string(winDir) + "\\System32\\drivers\\shield.sys";
    DeleteFileA(driverPath.c_str());

    printf("[+] shield.sys unloaded and cleaned\n");
}

int main(int argc, char* argv[]) {
    printf("=== ShieldMapper v1.0 ===\n");
    printf("=== shield.sys kernel mapper ===\n\n");

    if (argc < 2) {
        printf("Usage: ShieldMapper.exe <driver.sys>\n");
        return 1;
    }

    std::string driverPath = argv[1];
    printf("[*] Target driver: %s\n", driverPath.c_str());

    auto driverImage = ReadFileToBuffer(driverPath);
    if (driverImage.empty()) {
        printf("[!] Failed to read driver file\n");
        return 1;
    }
    printf("[+] Read %llu bytes\n", (uint64_t)driverImage.size());

    if (!LoadShieldDriver()) {
        printf("[!] Failed to load shield.sys\n");
        return 1;
    }

    Sleep(500);

    HANDLE hDevice = ShieldDriver::Open();
    if (!hDevice) {
        UnloadShieldDriver();
        return 1;
    }

    uint64_t ntBase = ShieldDriver::GetNtoskrnlBase();
    if (!ntBase) {
        printf("[!] Failed to get ntoskrnl base\n");
        ShieldDriver::Close(hDevice);
        UnloadShieldDriver();
        return 1;
    }
    printf("[+] ntoskrnl.exe @ 0x%llx\n", ntBase);

    uint16_t testVal = 0;
    if (!ShieldDriver::ReadKernel(hDevice, ntBase, &testVal, sizeof(testVal)) || testVal != IMAGE_DOS_SIGNATURE) {
        printf("[!] Kernel read test failed\n");
        ShieldDriver::Close(hDevice);
        UnloadShieldDriver();
        return 1;
    }
    printf("[+] Kernel R/W verified\n");

    bool success = Mapper::MapDriver(hDevice, driverImage);

    ShieldDriver::Close(hDevice);
    UnloadShieldDriver();

    if (success) {
        printf("\n[+] Driver mapped successfully!\n");
        return 0;
    }
    else {
        printf("\n[!] Driver mapping failed\n");
        return 1;
    }
}
