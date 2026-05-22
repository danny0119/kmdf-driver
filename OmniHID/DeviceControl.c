/*++
DeviceControl.c - 外部 IOCTL 处理
修复：移除 KeInsertQueueDpc，改用 DataPending 标志
--*/

#include "OmniHID.h"
#include "LogitechDescriptors.h"

NTKERNELAPI PCHAR PsGetProcessImageFileName(_In_ PEPROCESS Process);

FORCEINLINE BOOLEAN KernelStringEqualI(PCHAR Str1, const CHAR* Str2) {
    while (*Str1 && *Str2) {
        UCHAR c1 = *Str1; UCHAR c2 = (UCHAR)*Str2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= ('a' - 'A');
        if (c2 >= 'a' && c2 <= 'z') c2 -= ('a' - 'A');
        if (c1 != c2) return FALSE;
        Str1++; Str2++;
    }
    return (*Str1 == '\0' && *Str2 == '\0');
}

BOOLEAN IsAllowedCaller()
{
    PEPROCESS process = PsGetCurrentProcess();
    if (!process) return FALSE;
    PCHAR processName = PsGetProcessImageFileName(process);
    if (!processName) return FALSE;

    // 【隐蔽性修正】：适配现代 Logitech G HUB 架构
    if (KernelStringEqualI(processName, "LGHUB.EXE") ||
        KernelStringEqualI(processName, "LGHUB_AGENT.EXE") ||         // <--- 最完美的借壳目标
        KernelStringEqualI(processName, "LGHUB_SYSTEM_AGENT.EXE") ||  // <--- SYSTEM 权限进程
        KernelStringEqualI(processName, "LOGIOPTIONS.EXE") ||          // Logi Options+
        KernelStringEqualI(processName, "OMNIINJECTOR.EXE")) {         // 我们的独立测试器
        return TRUE;
    }
    return FALSE;
}

VOID EvtPdoIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength, ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    NTSTATUS status = STATUS_NOT_SUPPORTED;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PPDO_CONTEXT pCtx = PdoGetContext(device);
    PIRP irp = WdfRequestWdmGetIrp(Request);

    switch (IoControlCode) {
    case IOCTL_HID_SET_FEATURE:
    case IOCTL_HID_WRITE_REPORT:
    {
        // 【架构修正】：所有数据注入已由 UrbForge.c 的 URB_FUNCTION_CLASS_INTERFACE 统一接管！
        // 这里直接返回成功，避免 HidClass 内部重试报错
        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_HID_GET_FEATURE:
    {
        // 目前只对鼠标返回伪造的电池/固件数据
        if (pCtx->DeviceType != OmniHID_DeviceType_Mouse) {
            status = STATUS_NOT_SUPPORTED;
            break;
        }

        PHID_XFER_PACKET xferPacket = (PHID_XFER_PACKET)irp->UserBuffer;
        if (!xferPacket || !xferPacket->reportBuffer || xferPacket->reportBufferLen < 2) {
            status = STATUS_INVALID_PARAMETER; break;
        }
        UCHAR reportId = (UCHAR)(ULONG_PTR)xferPacket->reportId;
        if (reportId == 0 && xferPacket->reportBufferLen > 0) reportId = xferPacket->reportBuffer[0];

        if (reportId == LIGHTSPEED_REPORT_ID_BATTERY) {
            if (xferPacket->reportBufferLen >= 3) {
                xferPacket->reportBuffer[0] = LIGHTSPEED_REPORT_ID_BATTERY;
                xferPacket->reportBuffer[1] = 0x5A;
                xferPacket->reportBuffer[2] = 0x01;
                status = STATUS_SUCCESS;
            }
        }
        else if (reportId == LIGHTSPEED_REPORT_ID_FIRMWARE) {
            if (xferPacket->reportBufferLen >= 5) {
                xferPacket->reportBuffer[0] = LIGHTSPEED_REPORT_ID_FIRMWARE;
                xferPacket->reportBuffer[1] = 0x03;
                xferPacket->reportBuffer[2] = 0x02;
                xferPacket->reportBuffer[3] = 0x0A;
                xferPacket->reportBuffer[4] = 0x00;
                status = STATUS_SUCCESS;
            }
        }
        else {
            RtlZeroMemory(xferPacket->reportBuffer, xferPacket->reportBufferLen);
            if (xferPacket->reportBufferLen > 0) xferPacket->reportBuffer[0] = reportId;
            status = STATUS_SUCCESS;
        }
        break;
    }
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
    {
        PVOID outBuffer;
        ULONG descSize = (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) ? sizeof(g_KeyboardHidDescriptor) : sizeof(g_LogitechHidDescriptor);
        status = WdfRequestRetrieveOutputBuffer(Request, descSize, &outBuffer, NULL);
        if (NT_SUCCESS(status)) {
            if (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) {
                RtlCopyMemory(outBuffer, &g_KeyboardHidDescriptor, sizeof(g_KeyboardHidDescriptor));
                WdfRequestSetInformation(Request, sizeof(g_KeyboardHidDescriptor));
            }
            else {
                RtlCopyMemory(outBuffer, &g_LogitechHidDescriptor, sizeof(g_LogitechHidDescriptor));
                WdfRequestSetInformation(Request, sizeof(g_LogitechHidDescriptor));
            }
        }
        break;
    }
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
    {
        PVOID outBuffer;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(HID_DEVICE_ATTRIBUTES), &outBuffer, NULL);
        if (NT_SUCCESS(status)) {
            PHID_DEVICE_ATTRIBUTES attrs = (PHID_DEVICE_ATTRIBUTES)outBuffer;
            RtlZeroMemory(attrs, sizeof(HID_DEVICE_ATTRIBUTES));
            attrs->Size = sizeof(HID_DEVICE_ATTRIBUTES);
            if (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) {
                attrs->VendorID = 0x046D;
                attrs->ProductID = 0xC33E;
                attrs->VersionNumber = 0x0200;
            }
            else {
                attrs->VendorID = 0x046D;
                attrs->ProductID = 0xC547;
                attrs->VersionNumber = 0x0102;
            }
            WdfRequestSetInformation(Request, sizeof(HID_DEVICE_ATTRIBUTES));
        }
        break;
    }
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
    {
        PVOID outBuffer;
        ULONG reportDescSize = (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) ? g_KeyboardReportDescSize : g_LogitechReportDescSize;
        status = WdfRequestRetrieveOutputBuffer(Request, reportDescSize, &outBuffer, NULL);
        if (NT_SUCCESS(status)) {
            if (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) {
                RtlCopyMemory(outBuffer, g_KeyboardReportDescriptor, g_KeyboardReportDescSize);
                WdfRequestSetInformation(Request, g_KeyboardReportDescSize);
            }
            else {
                RtlCopyMemory(outBuffer, g_LogitechReportDescriptor, g_LogitechReportDescSize);
                WdfRequestSetInformation(Request, g_LogitechReportDescSize);
            }
        }
        break;
    }
    default:
        break;
    }

    WdfRequestComplete(Request, status);
}