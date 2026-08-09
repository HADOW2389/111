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

		// Fix #1: Use InterlockedCompareExchange for atomic read of size
		LONG currentSize = InterlockedCompareExchange(&size, 0, 0);
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
			// Use ExAllocatePool2 on Windows 10 2004+ (Fix #6)
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
				// Fix: Return dummy buffer instead of nullptr when whitelist is full
				SIZE_T allocSize = NumberOfBytes ? NumberOfBytes : 8;
				PVOID dummy = ExAllocatePoolWithTag(NonPagedPool, allocSize, Tag);
				if (dummy) {
					RtlZeroMemory(dummy, allocSize);
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

		// Fix #1: Use InterlockedCompareExchange for atomic read
		LONG currentSize = InterlockedCompareExchange(&size, 0, 0);
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
				SIZE_T allocSize = NumberOfBytes ? NumberOfBytes : 8;
				PVOID dummy = ExAllocatePool(NonPagedPool, allocSize);
				if (dummy) {
					RtlZeroMemory(dummy, allocSize);
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
        // Fix #9: Check IRQL before accessing paged memory
        if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        {
            return;
        }

        // Fix #2: Safe string comparison without wcsstr
        // wcsstr can walk past the buffer if it's not null-terminated
        if (!ProcessId && FullImageName && FullImageName->Buffer && FullImageName->Length > 0)
        {
            // Only use safe API - RtlCompareUnicodeString handles Length properly
            UNICODE_STRING targetDriver = RTL_CONSTANT_STRING(L"BEDaisy.sys");
            
            // Check if the path ends with "BEDaisy.sys" using safe comparison
            // FullImageName might be "\\SystemRoot\\System32\\drivers\\BEDaisy.sys"
            // We need to check if the tail matches
            BOOLEAN match = FALSE;
            
            if (FullImageName->Length >= targetDriver.Length)
            {
                // Compare the last part of the string
                UNICODE_STRING tailPart;
                tailPart.Buffer = (PWCH)((PUCHAR)FullImageName->Buffer + FullImageName->Length - targetDriver.Length);
                tailPart.Length = targetDriver.Length;
                tailPart.MaximumLength = targetDriver.Length;
                
                if (RtlEqualUnicodeString(&tailPart, &targetDriver, TRUE))
                {
                    match = TRUE;
                }
            }
            
            // Also try full comparison (in case it's just "BEDaisy.sys")
            if (!match)
            {
                match = (RtlEqualUnicodeString(FullImageName, &targetDriver, TRUE));
            }
            
            if (match)
            {
                DBG_PRINT("> ============= Driver %wZ ================", FullImageName);
                DriverUtil::IATHook(
                    ImageInfo->ImageBase,
                    "MmGetSystemRoutineAddress",
                    &gh_MmGetSystemRoutineAddress
                );
            }
        }
    }

    // Fix: Thread-based initialization for mapped drivers
    // When DriverObject is NULL (mapped driver), we can't use PsSetLoadImageNotifyRoutine
    // Instead, we poll for BEDaisy.sys using a system thread
    VOID MappedDriverWorker(PVOID Context)
    {
        UNREFERENCED_PARAMETER(Context);
        
        DBG_PRINT("BlindEye: MappedDriverWorker started, polling for BEDaisy.sys...");
        
        // Poll up to 60 times (1 second each = 60 seconds total)
        for (int attempt = 0; attempt < 60; attempt++)
        {
            // Sleep for 1 second between polls
            LARGE_INTEGER interval;
            interval.QuadPart = -10000000LL; // 1 second in 100ns units
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
            
            // Find BEDaisy.sys base address
            PVOID bedaisyBase = DriverUtil::GetDriverBase("BEDaisy.sys");
            if (bedaisyBase)
            {
                DBG_PRINT("BlindEye: Found BEDaisy.sys at 0x%p, applying IAT hook...", bedaisyBase);
                
                PVOID oldFunc = DriverUtil::IATHook(
                    bedaisyBase,
                    "MmGetSystemRoutineAddress",
                    &gh_MmGetSystemRoutineAddress
                );
                
                if (oldFunc)
                {
                    DBG_PRINT("BlindEye: Successfully hooked MmGetSystemRoutineAddress in BEDaisy.sys");
                }
                else
                {
                    DBG_PRINT("BlindEye: Failed to hook MmGetSystemRoutineAddress in BEDaisy.sys");
                }
                break;
            }
        }
        
        DBG_PRINT("BlindEye: MappedDriverWorker exiting");
        PsTerminateSystemThread(STATUS_SUCCESS);
    }

    VOID InitMappedDriver()
    {
        HANDLE threadHandle;
        NTSTATUS status = PsCreateSystemThread(
            &threadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            NULL,
            NULL,
            MappedDriverWorker,
            NULL
        );
        
        if (NT_SUCCESS(status))
        {
            DBG_PRINT("BlindEye: Created worker thread for BEDaisy.sys detection");
            ZwClose(threadHandle);
        }
        else
        {
            DBG_PRINT("BlindEye: Failed to create worker thread: 0x%08X", status);
        }
    }
}
