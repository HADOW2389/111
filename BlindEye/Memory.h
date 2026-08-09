#pragma once
#include <intrin.h>
#include <ntddk.h>

namespace Memory
{
	BOOLEAN SafeWriteMemory(PVOID TargetAddress, PVOID SourceAddress, SIZE_T Size);
	void WriteProtectOff();
	void WriteProtectOn();
}