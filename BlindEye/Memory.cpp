#include "Memory.h"
#include <ntddk.h>

namespace Memory
{
    BOOLEAN SafeWriteMemory(PVOID TargetAddress, PVOID SourceAddress, SIZE_T Size)
    {
        if (!TargetAddress || !SourceAddress || !Size)
            return FALSE;

        PMDL mdl = IoAllocateMdl(TargetAddress, (ULONG)Size, FALSE, FALSE, NULL);
        if (!mdl)
            return FALSE;

        MmBuildMdlForNonPagedPool(mdl);

        PVOID mappedAddr = MmMapLockedPagesSpecifyCache(
            mdl,
            KernelMode,
            MmCached,
            NULL,
            FALSE,
            NormalPagePriority
        );

        if (!mappedAddr)
        {
            IoFreeMdl(mdl);
            return FALSE;
        }

        RtlCopyMemory(mappedAddr, SourceAddress, Size);

        MmUnmapLockedPages(mappedAddr, mdl);
        IoFreeMdl(mdl);
        return TRUE;
    }

    void WriteProtectOff()
    {
        // Legacy CR0.WP modification (deprecated and unsafe on modern Windows with HVCI/CET)
        auto cr0 = __readcr0();
        cr0 &= 0xfffffffffffeffff;
        __writecr0(cr0);
        _disable();
    }

    void WriteProtectOn()
    {
        // Legacy CR0.WP modification
        auto cr0 = __readcr0();
        cr0 |= 0x10000;
        _enable();
        __writecr0(cr0);
    }
}