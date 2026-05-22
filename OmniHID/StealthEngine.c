#include "OmniHID.h"

KSPIN_LOCK g_Cr0Lock = { 0 };

PVOID FindCdromBaseAddress() {
    UNICODE_STRING routineName;
    RtlInitUnicodeString(&routineName, L"CdRomIsDeviceMmcDevice");

    PVOID routineAddr = MmGetSystemRoutineAddress(&routineName);
    if (!routineAddr) return NULL;

    // 页对齐向下搜索，最大跨度 0x100000 (1MB，标准驱动段大小)
    ULONG_PTR addr = (ULONG_PTR)routineAddr & ~0xFFF;
    for (ULONG offset = 0; offset < 0x100000; offset += 0x1000) {
        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)(addr - offset);
        if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
            PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((ULONG_PTR)dosHeader + dosHeader->e_lfanew);
            if (ntHeader->Signature == IMAGE_NT_SIGNATURE) {
                if ((ULONG_PTR)routineAddr >= (ULONG_PTR)dosHeader &&
                    (ULONG_PTR)routineAddr < (ULONG_PTR)dosHeader + ntHeader->OptionalHeader.SizeOfImage) {
                    return dosHeader;
                }
            }
        }
    }
    return NULL;
}

PVOID FindCodeCave(PVOID baseAddress, SIZE_T requiredSize) {
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)baseAddress;
    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((ULONG_PTR)baseAddress + dosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeader);

    for (USHORT i = 0; i < ntHeader->FileHeader.NumberOfSections; i++) {
        if (strcmp((const char*)section[i].Name, ".text") == 0) {
            ULONG trailingGap = section[i].SizeOfRawData - section[i].Misc.VirtualSize;
            if (trailingGap >= requiredSize) {
                PVOID caveAddr = (PVOID)((ULONG_PTR)baseAddress + section[i].VirtualAddress + section[i].Misc.VirtualSize);
                BOOLEAN isValid = TRUE;
                for (ULONG j = 0; j < requiredSize; j++) {
                    UCHAR byte = *(UCHAR*)((ULONG_PTR)caveAddr + j);
                    if (byte != 0x00 && byte != 0xCC) { isValid = FALSE; break; }
                }
                if (isValid) return caveAddr;
            }
        }
    }
    return NULL;
}

NTSTATUS WriteCodeCaveSafely(PVOID caveAddress, PVOID shellcode, SIZE_T length) {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_NOT_SUPPORTED;

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_Cr0Lock, &oldIrql);

    DisableWriteProtect();
    RtlCopyMemory(caveAddress, shellcode, length);
    EnableWriteProtect();

    KeReleaseSpinLock(&g_Cr0Lock, oldIrql);
    return STATUS_SUCCESS;
}

VOID InstallKppSafeHook(PVOID TargetCodeCave, PVOID ShadowCode) {
    DisableWriteProtect();
    PUCHAR pCave = (PUCHAR)TargetCodeCave;
    pCave[0] = 0xFF; pCave[1] = 0x25;
    *(PULONG)&pCave[2] = 0x00000000;
    *(PULONGLONG)&pCave[6] = (ULONGLONG)ShadowCode;
    EnableWriteProtect();
}

ULONGLONG FindAndReplaceApiAddr(PUCHAR Shellcode, SIZE_T Size, ULONGLONG RealApiAddr) {
    for (SIZE_T i = 0; i <= Size - sizeof(ULONGLONG); i++) {
        if (*(PULONGLONG)&Shellcode[i] == API_PLACEHOLDER) {
            DisableWriteProtect();
            *(PULONGLONG)&Shellcode[i] = RealApiAddr;
            EnableWriteProtect();
            return (ULONGLONG)&Shellcode[i];
        }
    }
    return 0;
}