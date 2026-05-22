/*++
UrbForge.c - URB 伪造引擎（DPC + 工作项架构）
--*/

#include "OmniHID.h"
#include "LogitechDescriptors.h"
// 全局上下文指针，用于跨 PDO 联动（借壳上市）
PPDO_CONTEXT g_MouseCtx = NULL;
PPDO_CONTEXT g_KeyboardCtx = NULL;
#pragma warning(disable : 4996)

#define DIAG(fmt, ...) DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "OmniHID: " fmt "\n", __VA_ARGS__)

USBD_PIPE_HANDLE AllocateFakePipeHandle(PPDO_CONTEXT pCtx) {
    KIRQL oldIrql;
    KeAcquireSpinLock(&pCtx->HandlePoolLock, &oldIrql);
    if (pCtx->FakeHandleCount >= MAX_FAKE_HANDLES) {
        KeReleaseSpinLock(&pCtx->HandlePoolLock, oldIrql);
        return (USBD_PIPE_HANDLE)0xDEADDEAD;
    }
    PVOID handle = ExAllocatePoolWithTag(NonPagedPool, 1, 'PpH0');
    if (handle) { pCtx->FakeHandlePool[pCtx->FakeHandleCount++] = handle; }
    KeReleaseSpinLock(&pCtx->HandlePoolLock, oldIrql);
    return (USBD_PIPE_HANDLE)handle;
}

NTSTATUS HandleUrbGetDescriptor(PPDO_CONTEXT pCtx, PURB Urb)
{
    UCHAR descType = Urb->UrbControlDescriptorRequest.DescriptorType;
    DIAG("URB_DESC: Type=0x%02X DevType=%d", descType, pCtx->DeviceType);
    PVOID descData = NULL;
    ULONG descLength = 0;

    // ============================================================
    // 【多管线路由】：根据设备类型提供不同的描述符
    // ============================================================
    switch (pCtx->DeviceType) {
    case OmniHID_DeviceType_Mouse:
        switch (descType) {
        case USB_DEVICE_DESCRIPTOR_TYPE: descData = &g_LogitechDeviceDescriptor; descLength = sizeof(g_LogitechDeviceDescriptor); break;
        case USB_CONFIGURATION_DESCRIPTOR_TYPE: descData = g_LogitechFullConfigDescriptor; descLength = g_LogitechFullConfigDescriptorSize; break;
        case 0x21: descData = &g_LogitechHidDescriptor; descLength = sizeof(g_LogitechHidDescriptor); break;
        case 0x22: descData = g_LogitechReportDescriptor; descLength = g_LogitechReportDescSize; break;
        }
        break;
    case OmniHID_DeviceType_Keyboard:
        switch (descType) {
        case USB_DEVICE_DESCRIPTOR_TYPE: descData = &g_KeyboardDeviceDescriptor; descLength = sizeof(g_KeyboardDeviceDescriptor); break;
        case USB_CONFIGURATION_DESCRIPTOR_TYPE: descData = g_KeyboardFullConfigDescriptor; descLength = g_KeyboardFullConfigDescriptorSize; break;
        case 0x21: descData = &g_KeyboardHidDescriptor; descLength = sizeof(g_KeyboardHidDescriptor); break;
        case 0x22: descData = g_KeyboardReportDescriptor; descLength = g_KeyboardReportDescSize; break;
        }
        break;
    case OmniHID_DeviceType_Gamepad:
        // 手柄暂不支持描述符请求，返回 NOT_SUPPORTED
        Urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
        return STATUS_NOT_SUPPORTED;
    default:
        Urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
        return STATUS_NOT_SUPPORTED;
    }

    if (!descData) {
        Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        Urb->UrbControlDescriptorRequest.TransferBufferLength = 0;
        return STATUS_SUCCESS;
    }

    ULONG bytesToCopy = Urb->UrbControlDescriptorRequest.TransferBufferLength;
    if (bytesToCopy > descLength) bytesToCopy = descLength;

    PVOID systemAddress = NULL;
    if (Urb->UrbControlDescriptorRequest.TransferBuffer)
        systemAddress = Urb->UrbControlDescriptorRequest.TransferBuffer;
    else if (Urb->UrbControlDescriptorRequest.TransferBufferMDL)
        systemAddress = MmGetSystemAddressForMdlSafe(Urb->UrbControlDescriptorRequest.TransferBufferMDL, NormalPagePriority);

    if (bytesToCopy > 0 && systemAddress)
        RtlCopyMemory(systemAddress, descData, bytesToCopy);
    else if (bytesToCopy > 0) {
        Urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
        Urb->UrbControlDescriptorRequest.TransferBufferLength = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Urb->UrbControlDescriptorRequest.TransferBufferLength = bytesToCopy;
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS HandleUrbSelectConfiguration(PPDO_CONTEXT pCtx, PURB Urb)
{
    PUSBD_INTERFACE_INFORMATION ifaceInfo = &Urb->UrbSelectConfiguration.Interface;

    PVOID configHandle = ExAllocatePoolWithTag(NonPagedPool, 1, 'CfH0');
    if (!configHandle) return STATUS_INSUFFICIENT_RESOURCES;
    Urb->UrbSelectConfiguration.ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)configHandle;

    if (ifaceInfo->NumberOfPipes == 0) {
        PVOID ifaceHandle = ExAllocatePoolWithTag(NonPagedPool, 1, 'IfH0');
        if (ifaceHandle) ifaceInfo->InterfaceHandle = (USBD_INTERFACE_HANDLE)ifaceHandle;
        Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    PVOID ifaceHandle = ExAllocatePoolWithTag(NonPagedPool, 1, 'IfH0');
    if (!ifaceHandle) { ExFreePool(configHandle); return STATUS_INSUFFICIENT_RESOURCES; }
    ifaceInfo->InterfaceHandle = (USBD_INTERFACE_HANDLE)ifaceHandle;

    ifaceInfo->Class = 0x03;
    ifaceInfo->SubClass = 0x01;

    // ============================================================
    // 【关键修复】：根据设备类型动态设置 Interface Protocol
    // ============================================================
    if (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) {
        ifaceInfo->Protocol = 0x01; // Boot Keyboard
    }
    else if (pCtx->DeviceType == OmniHID_DeviceType_Mouse) {
        ifaceInfo->Protocol = 0x02; // Boot Mouse
    }
    else {
        ifaceInfo->Protocol = 0x00; // Gamepad / Other
    }

    ULONG pipesToFill = ifaceInfo->NumberOfPipes;
    ULONG baseLength = sizeof(USBD_INTERFACE_INFORMATION) - sizeof(USBD_PIPE_INFORMATION);
    if (ifaceInfo->Length < baseLength) {
        Urb->UrbHeader.Status = USBD_STATUS_BUFFER_TOO_SMALL;
        return STATUS_DEVICE_DATA_ERROR;
    }
    ULONG extraSpace = ifaceInfo->Length - baseLength;
    ULONG maxPipesAllowed = 1 + (extraSpace / sizeof(USBD_PIPE_INFORMATION));
    if (pipesToFill > maxPipesAllowed) pipesToFill = maxPipesAllowed;
    if (pipesToFill > MAX_FAKE_HANDLES) pipesToFill = MAX_FAKE_HANDLES;
    if (pipesToFill > 1) pipesToFill = 1;

    // ============================================================
    // 【关键修复】：根据设备类型设定端点包大小
    // ============================================================
    USHORT maxPacketSize = 64;
    if (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) {
        maxPacketSize = 8; // Boot Keyboard 必须是 8
    }

    for (ULONG i = 0; i < pipesToFill; i++) {
        USBD_PIPE_HANDLE pipeHandle = AllocateFakePipeHandle(pCtx);
        if (pipeHandle == (USBD_PIPE_HANDLE)0xDEADDEAD) {
            ExFreePool(ifaceHandle); ExFreePool(configHandle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        ifaceInfo->Pipes[i].PipeHandle = pipeHandle;
        ifaceInfo->Pipes[i].MaximumTransferSize = 4096;
        ifaceInfo->Pipes[i].PipeFlags = 0;
        pCtx->InterruptInPipeHandle = pipeHandle;
        ifaceInfo->Pipes[i].EndpointAddress = 0x81;
        ifaceInfo->Pipes[i].Interval = 1;
        ifaceInfo->Pipes[i].PipeType = UsbdPipeTypeInterrupt;
        ifaceInfo->Pipes[i].MaximumPacketSize = maxPacketSize; // 使用动态值
    }
    ifaceInfo->NumberOfPipes = pipesToFill;
    ifaceInfo->Length = (USHORT)(sizeof(USBD_INTERFACE_INFORMATION) + (pipesToFill - 1) * sizeof(USBD_PIPE_INFORMATION));
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    DIAG("URB_CONFIG: MaxPacket=%d Protocol=%d", maxPacketSize, ifaceInfo->Protocol);
    return STATUS_SUCCESS;
}

NTSTATUS HandleUrbSelectInterface(PPDO_CONTEXT pCtx, PURB Urb)
{
    PUSBD_INTERFACE_INFORMATION ifaceInfo = &Urb->UrbSelectInterface.Interface;
    ifaceInfo->Class = 0x03; ifaceInfo->SubClass = 0x01;

    // 动态 Protocol
    if (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) {
        ifaceInfo->Protocol = 0x01;
    }
    else if (pCtx->DeviceType == OmniHID_DeviceType_Mouse) {
        ifaceInfo->Protocol = 0x02;
    }
    else {
        ifaceInfo->Protocol = 0x00;
    }

    ULONG baseLength = sizeof(USBD_INTERFACE_INFORMATION) - sizeof(USBD_PIPE_INFORMATION);
    ULONG requiredLength = baseLength + sizeof(USBD_PIPE_INFORMATION);

    if (ifaceInfo->Length >= requiredLength && ifaceInfo->NumberOfPipes > 0 && pCtx->InterruptInPipeHandle != NULL) {
        ifaceInfo->Pipes[0].PipeHandle = pCtx->InterruptInPipeHandle;
        ifaceInfo->Pipes[0].MaximumTransferSize = 4096;
        ifaceInfo->Pipes[0].EndpointAddress = 0x81;
        ifaceInfo->Pipes[0].Interval = 1;
        ifaceInfo->Pipes[0].PipeType = UsbdPipeTypeInterrupt;

        // 动态 MaxPacketSize
        ifaceInfo->Pipes[0].MaximumPacketSize = (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) ? 8 : 64;

        ifaceInfo->Pipes[0].PipeFlags = 0;
        ifaceInfo->NumberOfPipes = 1;
        ifaceInfo->Length = (USHORT)requiredLength;
    }
    else {
        ifaceInfo->NumberOfPipes = 0;
        ifaceInfo->Length = (USHORT)baseLength;
    }
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

NTSTATUS PdoInternalDeviceControlPreprocess(WDFDEVICE Device, PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PPDO_CONTEXT pCtx = PdoGetContext(Device);
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB) {
        PURB urb = (PURB)stack->Parameters.Others.Argument1;
        if (urb) {
            switch (urb->UrbHeader.Function) {
            case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
            case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
                status = HandleUrbGetDescriptor(pCtx, urb);
                break;

            case URB_FUNCTION_SELECT_CONFIGURATION:
                status = HandleUrbSelectConfiguration(pCtx, urb);
                break;

            case URB_FUNCTION_SELECT_INTERFACE:
                status = HandleUrbSelectInterface(pCtx, urb);
                break;

            case URB_FUNCTION_CLASS_INTERFACE:
            {
                UCHAR request = urb->UrbControlVendorClassRequest.Request;
                USHORT value = urb->UrbControlVendorClassRequest.Value;
                UCHAR reportType = (UCHAR)(value >> 8);

                PVOID buffer = urb->UrbControlVendorClassRequest.TransferBuffer;
                if (!buffer && urb->UrbControlVendorClassRequest.TransferBufferMDL) {
                    buffer = MmGetSystemAddressForMdlSafe(
                        urb->UrbControlVendorClassRequest.TransferBufferMDL,
                        NormalPagePriority);
                }
                ULONG transferLength = urb->UrbControlVendorClassRequest.TransferBufferLength;

                // 1. Set Idle (0x0A) / Set Protocol (0x0B)
                if (request == 0x0A || request == 0x0B) {
                    urb->UrbControlVendorClassRequest.TransferBufferLength = 0;
                    urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
                    status = STATUS_SUCCESS;
                    break;
                }

                // 2. Get Protocol (0x03)
                if (request == 0x03) {
                    if (buffer && transferLength >= 1) {
                        ((PUCHAR)buffer)[0] = 0x00;
                        urb->UrbControlVendorClassRequest.TransferBufferLength = 1;
                    }
                    else {
                        urb->UrbControlVendorClassRequest.TransferBufferLength = 0;
                    }
                    urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
                    status = STATUS_SUCCESS;
                    break;
                }

                // 3. Set Report (0x09) - 单帧即时触发引擎 (绝无死锁)
                if (request == 0x09) {

                    if (reportType == 0x03 && pCtx->DeviceType == OmniHID_DeviceType_Mouse) {
                        if (buffer && transferLength >= sizeof(OMNIHID_COVERT_FUTURE_DATA)) {
                            POMNIHID_COVERT_FUTURE_DATA futureData = (POMNIHID_COVERT_FUTURE_DATA)buffer;

                            KIRQL oldIrql;

                            // 只取第一帧，直接放入活跃数据区，立即触发
                            KeAcquireSpinLock(&pCtx->FrameQueueLock, &oldIrql);
                            RtlCopyMemory(&pCtx->ActiveFrameData, &futureData->Frames[0], sizeof(OMNIHID_FUTURE_FRAME));
                            pCtx->ActiveDataPending = TRUE;
                            KeReleaseSpinLock(&pCtx->FrameQueueLock, oldIrql);

                            // 联动键盘：只有当帧明确包含键盘事件时才推
                            if (g_KeyboardCtx && futureData->Frames[0].HasKeyboardEvent) {
                                KeAcquireSpinLock(&g_KeyboardCtx->FrameQueueLock, &oldIrql);
                                RtlCopyMemory(&g_KeyboardCtx->ActiveFrameData, &futureData->Frames[0], sizeof(OMNIHID_FUTURE_FRAME));
                                g_KeyboardCtx->ActiveDataPending = TRUE;
                                KeReleaseSpinLock(&g_KeyboardCtx->FrameQueueLock, oldIrql);

                                if (g_KeyboardCtx->PulseWorkItem) {
                                    IoQueueWorkItem(g_KeyboardCtx->PulseWorkItem, PulseWorkItemRoutine, DelayedWorkQueue, g_KeyboardCtx);
                                }
                            }

                            // 触发鼠标
                            if (pCtx->PulseWorkItem) {
                                IoQueueWorkItem(pCtx->PulseWorkItem, PulseWorkItemRoutine, DelayedWorkQueue, pCtx);
                            }
                        }
                    }

                    urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
                    status = STATUS_SUCCESS;
                    break;
                }

                // 4. 兜底防御
                urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
                status = STATUS_SUCCESS;
                break;
            }
            case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER: {
                if (urb->UrbBulkOrInterruptTransfer.PipeHandle == pCtx->InterruptInPipeHandle) {

                    KIRQL oldIrql;
                    KeAcquireSpinLock(&pCtx->PendingIrpLock, &oldIrql);

                    if (pCtx->DeviceRemoving) {
                        KeReleaseSpinLock(&pCtx->PendingIrpLock, oldIrql);
                        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
                        IoCompleteRequest(Irp, IO_NO_INCREMENT);
                        return STATUS_DELETE_PENDING;
                    }

                    PPENDING_IRP_ENTRY entry = (PPENDING_IRP_ENTRY)
                        ExAllocateFromNPagedLookasideList(&pCtx->PendingIrpLookaside);
                    if (entry) {
                        entry->Irp = Irp;
                        entry->Urb = urb;
                        IoMarkIrpPending(Irp);
                        InsertTailList(&pCtx->PendingIrpList, &entry->ListEntry);

                        // ============================================================
                        // 【致命修复】：打破管线死锁！
                        // 如果定时器引擎已经有到期数据，立刻触发 WorkItem 消费！
                        // ============================================================
                        if (pCtx->ActiveDataPending) { // 改为 ActiveDataPending
                            if (pCtx->PulseWorkItem) {
                                IoQueueWorkItem(pCtx->PulseWorkItem, PulseWorkItemRoutine, DelayedWorkQueue, pCtx);
                            }
                        }

                        KeReleaseSpinLock(&pCtx->PendingIrpLock, oldIrql);
                        return STATUS_PENDING;
                    }

                    KeReleaseSpinLock(&pCtx->PendingIrpLock, oldIrql);
                    urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                // ... 其他无关 URB 处理保持不变
                else {
                    urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
                    urb->UrbBulkOrInterruptTransfer.TransferBufferLength = 0;
                    status = STATUS_SUCCESS;
                    break;
                }
            }

            case URB_FUNCTION_ABORT_PIPE:
            case URB_FUNCTION_SYNC_RESET_PIPE:
            case URB_FUNCTION_SYNC_CLEAR_STALL:
            case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
                urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
                status = STATUS_SUCCESS;
                break;

            default:
                urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
                status = STATUS_SUCCESS;
                break;
            }
        }
    }
    else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_GET_PORT_STATUS) {
        PULONG portStatus = (PULONG)Irp->UserBuffer;
        if (portStatus) {
            *portStatus = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
            Irp->IoStatus.Information = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
    }
    else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_GET_CONTROLLER_INFO) {
        POMNIHID_USB_CONTROLLER_INFO_0 ci = (POMNIHID_USB_CONTROLLER_INFO_0)Irp->UserBuffer;
        if (ci) {
            RtlZeroMemory(ci, sizeof(OMNIHID_USB_CONTROLLER_INFO_0));
            ci->PciVendorId = 0x8086;
            ci->PciDeviceId = 0x1E26;
            ci->PciRevision = 0x04;
            ci->NumberOfRootPorts = 2;
            ci->ControllerFlavor = OMNIHID_EHCI_Generic;
            ci->HcFeatureFlags = 0;
            ci->PciBusNumber = 0;
            ci->PciBusDevice = 29;
            ci->PciBusFunction = 0;
            Irp->IoStatus.Information = sizeof(OMNIHID_USB_CONTROLLER_INFO_0);
            status = STATUS_SUCCESS;
        }
    }
    else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_GET_HUB_COUNT) {
        PULONG hubCount = (PULONG)Irp->UserBuffer;
        if (hubCount) {
            *hubCount = 1;
            Irp->IoStatus.Information = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
    }
    else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_RESET_PORT ||
        stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_CYCLE_PORT) {
        status = STATUS_SUCCESS;
    }

    if (status != STATUS_NOT_SUPPORTED && status != STATUS_PENDING) {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    return WdfDeviceWdmDispatchPreprocessedIrp(Device, Irp);
}