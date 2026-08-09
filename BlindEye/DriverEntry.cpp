#include "Types.h"
#include "DriverUtil.h"
#include "Hooks.h"

using namespace DriverUtil;
using namespace Hooks;

// Fix: Don't use DriverUnload for mapped drivers
// DriverUnload requires a valid DriverObject (not NULL in mapped context)
// Callbacks will be cleaned up when kernel pool is freed

void TdDeviceUnload(
    DRIVER_OBJECT* DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    PsRemoveLoadImageNotifyRoutine(&LoadImageNotifyRoutine);
    DBG_PRINT("ImageNotifyRoutine removed on unload");
}

NTSTATUS TdDeviceClose(
    IN PDEVICE_OBJECT  DeviceObject,
    IN PIRP  Irp
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

extern "C"
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DBG_PRINT("BlindEye DriverEntry called - DriverObject=0x%p", DriverObject);

    // Fix #3: When mapped via ShieldMapper/kdmapper, DriverObject is NULL.
    // PsSetLoadImageNotifyRoutine from a mapped driver triggers PatchGuard (0x109)
    // AND the callback address won't pass MmVerifyCallbackFunction checks.
    // 
    // For mapped drivers: use the LoadImageNotifyRoutine WITHOUT the callback.
    // Instead, we'll rely on IAT hooking when MmGetSystemRoutineAddress is called.
    // The hook on MmGetSystemRoutineAddress in BEDaisy.sys happens
    // when BEDaisy.sys itself is loaded and calls MmGetSystemRoutineAddress.
    // We don't need PsSetLoadImageNotifyRoutine for this.
    //
    // Strategy: Only register the callback when loaded as a normal driver (DriverObject != NULL).
    // When mapped (DriverObject == NULL), just return success - the hooking happens
    // via IATHook called from other mechanisms.

    if (DriverObject)
    {
        // Normal driver load - safe to use DriverObject
        DriverObject->MajorFunction[IRP_MJ_CLOSE] = &TdDeviceClose;
        DriverObject->DriverUnload = &TdDeviceUnload;

        NTSTATUS status = PsSetLoadImageNotifyRoutine(&LoadImageNotifyRoutine);
        if (NT_SUCCESS(status))
        {
            DBG_PRINT("Installed ImageNotifyRoutine... 0x%p", &LoadImageNotifyRoutine);
        }
        else
        {
            DBG_PRINT("Failed to install ImageNotifyRoutine: 0x%08X", status);
        }
        return status;
    }
    else
    {
        // Mapped driver - don't register callbacks, don't use DriverObject
        // The driver hooks happen when LoadImageNotifyRoutine would have been triggered
        // We'll use a system thread to poll for BEDaisy.sys instead (see InitMappedDriver)
        DBG_PRINT("BlindEye loaded as mapped driver - skipping callback registration");
        DBG_PRINT("BlindEye will hook BEDaisy.sys via IAT when MmGetSystemRoutineAddress is called");
        
        // Create a system thread to poll for and hook BEDaisy.sys
        InitMappedDriver();
        
        return STATUS_SUCCESS;
    }
}
