#include "Hooks.h"
#include <fltKernel.h>

namespace Hooks
{
	PVOID gh_ExAllocatePoolWithTag(
		POOL_TYPE PoolType,
		SIZE_T NumberOfBytes,
		ULONG Tag
	) {
		const int WhiteListSize = 1000;
		static void* WhiteList[WhiteListSize]{};
		static volatile LONG size = 0;
		void* ReturnAddress = _ReturnAddress();

		LONG currentSize = size;
		if (currentSize > WhiteListSize)
			currentSize = WhiteListSize;

		for (LONG i = 0; i < currentSize; i++) {
			if (InterlockedCompareExchangePointer(&WhiteList[i], NULL, NULL) == ReturnAddress) {
				return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
			}
		}

		if (PoolType == 1 && NumberOfBytes == 24) {
			DBG_PRINT("ExAllocatePoolWithTag called from: 0x%p rejected!", ReturnAddress);
			// Fix #3: Return dummy zeroed buffer instead of nullptr to prevent PAGE_FAULT_IN_NONPAGED_AREA
			PVOID dummy = ExAllocatePoolWithTag(NonPagedPool, 24, 'EyBd');
			if (dummy) {
				RtlZeroMemory(dummy, 24);
			}
			return dummy;
		}
		else {
			LONG idx = InterlockedIncrement(&size) - 1;
			if (idx < WhiteListSize) {
				InterlockedExchangePointer(&WhiteList[idx], ReturnAddress);
				return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
			}
			else {
				DBG_PRINT("ExAllocatePoolWithTag WhiteList is full");
				PVOID dummy = ExAllocatePoolWithTag(NonPagedPool, NumberOfBytes ? NumberOfBytes : 8, Tag);
				if (dummy) {
					RtlZeroMemory(dummy, NumberOfBytes ? NumberOfBytes : 8);
				}
				return dummy;
			}
		}
	}

	PVOID gh_ExAllocatePool(
		POOL_TYPE PoolType,
		SIZE_T NumberOfBytes
	) {
		const int WhiteListSize = 1000;
		static void* WhiteList[WhiteListSize]{};
		static volatile LONG size = 0;
		void* ReturnAddress = _ReturnAddress();

		LONG currentSize = size;
		if (currentSize > WhiteListSize)
			currentSize = WhiteListSize;

		for (LONG i = 0; i < currentSize; i++) {
			if (InterlockedCompareExchangePointer(&WhiteList[i], NULL, NULL) == ReturnAddress) {
				return ExAllocatePool(PoolType, NumberOfBytes);
			}
		}

		if (PoolType == 1 && NumberOfBytes == 24) {
			DBG_PRINT("ExAllocatePool called from: 0x%p rejected!", ReturnAddress);
			// Fix #3: Return dummy zeroed buffer instead of nullptr
			PVOID dummy = ExAllocatePool(NonPagedPool, 24);
			if (dummy) {
				RtlZeroMemory(dummy, 24);
			}
			return dummy;
		}
		else {
			LONG idx = InterlockedIncrement(&size) - 1;
			if (idx < WhiteListSize) {
				InterlockedExchangePointer(&WhiteList[idx], ReturnAddress);
				return ExAllocatePool(PoolType, NumberOfBytes);
			}
			else {
				DBG_PRINT("ExAllocatePool WhiteList is full");
				PVOID dummy = ExAllocatePool(NonPagedPool, NumberOfBytes ? NumberOfBytes : 8);
				if (dummy) {
					RtlZeroMemory(dummy, NumberOfBytes ? NumberOfBytes : 8);
				}
				return dummy;
			}
		}
	}

    PVOID gh_MmGetSystemRoutineAddress(
        PUNICODE_STRING SystemRoutineName
    )
    {
        // Fix #2 & #9: Validate pointer, length, and IRQL level
        if (!SystemRoutineName || !SystemRoutineName->Buffer || SystemRoutineName->Length == 0)
        {
            return NULL;
        }

        if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        {
            return MmGetSystemRoutineAddress(SystemRoutineName);
        }

        UNICODE_STRING targetTag = RTL_CONSTANT_STRING(L"ExAllocatePoolWithTag");
        UNICODE_STRING targetPool = RTL_CONSTANT_STRING(L"ExAllocatePool");

        if (RtlEqualUnicodeString(SystemRoutineName, &targetTag, TRUE))
        {
            DBG_PRINT("Hooking ExAllocatePoolWithTag...");
            return &gh_ExAllocatePoolWithTag;
        }
        else if (RtlEqualUnicodeString(SystemRoutineName, &targetPool, TRUE))
        {
            DBG_PRINT("Hooking ExAllocatePool...");
            return &gh_ExAllocatePool;
        }

        return MmGetSystemRoutineAddress(SystemRoutineName);
    }

    VOID LoadImageNotifyRoutine(
        PUNICODE_STRING FullImageName,
        HANDLE ProcessId,
        PIMAGE_INFO ImageInfo
    )
    {
        // Fix #2 & #9: Safe checks for FullImageName, Buffer, Length and IRQL
        if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        {
            return;
        }

        if (!ProcessId && FullImageName && FullImageName->Buffer && FullImageName->Length > 0)
        {
            UNICODE_STRING targetDriver = RTL_CONSTANT_STRING(L"BEDaisy.sys");
            if (RtlCompareUnicodeString(FullImageName, &targetDriver, TRUE) == 0 ||
                wcsstr(FullImageName->Buffer, L"BEDaisy.sys") != NULL)
            {
                DBG_PRINT("> ============= Driver %ws ================", FullImageName->Buffer);
                DriverUtil::IATHook(
                    ImageInfo->ImageBase,
                    "MmGetSystemRoutineAddress",
                    &gh_MmGetSystemRoutineAddress
                );
            }
        }
    }
}