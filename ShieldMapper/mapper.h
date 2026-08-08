#pragma once
#include "shield_driver.h"
#include <vector>
#include <string>

namespace Mapper {
    bool MapDriver(HANDLE hDevice, const std::vector<uint8_t>& driverImage);
    bool ResolveImports(HANDLE hDevice, uint64_t mappedBase, PIMAGE_NT_HEADERS64 pNt, const uint8_t* localImage);
    bool FixRelocations(HANDLE hDevice, uint64_t mappedBase, uint64_t delta, PIMAGE_NT_HEADERS64 pNt, const uint8_t* localImage);
    bool MapSections(HANDLE hDevice, uint64_t mappedBase, PIMAGE_NT_HEADERS64 pNt, const uint8_t* localImage);
}
