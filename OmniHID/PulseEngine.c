/*++
PulseEngine.c - 傅里叶时序混淆引擎 (防风暴/防丢包终极版)
--*/

#include "OmniHID.h"

#define DIAG(fmt, ...) DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "OmniHID: " fmt "\n", __VA_ARGS__)

extern PPDO_CONTEXT g_KeyboardCtx;

VOID PulseWorkItemRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context);

// ============================================================
// 定时器 DPC：防风暴 & 统一帧联动
// ============================================================
VOID TimerDpcRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PPDO_CONTEXT pCtx = (PPDO_CONTEXT)DeferredContext;
    if (!pCtx || pCtx->DeviceRemoving) return;

    KIRQL oldIrql;
    KeAcquireSpinLockAtDpcLevel(&pCtx->FrameQueueLock);

    LARGE_INTEGER now;
    KeQuerySystemTime(&now);

    // 1. 提取所有到期帧
    while (!IsListEmpty(&pCtx->FrameQueueListHead)) {
        PLIST_ENTRY entry = pCtx->FrameQueueListHead.Flink;
        POMNIHID_TIMED_FRAME frame = CONTAINING_RECORD(entry, OMNIHID_TIMED_FRAME, ListEntry);

        if (frame->DueTime.QuadPart <= now.QuadPart) {
            RemoveEntryList(entry);

            // 拷贝到鼠标活跃数据区
            RtlCopyMemory(&pCtx->ActiveFrameData, &frame->Data, sizeof(OMNIHID_FUTURE_FRAME));
            pCtx->ActiveDataPending = TRUE;

            // 【致命修复】：精准借壳联动！只有当帧包含真实的键盘动作时，才推给键盘！
            // 绝不能把单纯的鼠标移动帧推给键盘，否则会抽干键盘 IRP 池导致按键失效！
            if (g_KeyboardCtx && (frame->Data.KeyboardModifier != 0 || frame->Data.KeyboardKeys[0] != 0)) {
                KIRQL kbdIrql;
                KeAcquireSpinLockAtDpcLevel(&g_KeyboardCtx->FrameQueueLock);
                RtlCopyMemory(&g_KeyboardCtx->ActiveFrameData, &frame->Data, sizeof(OMNIHID_FUTURE_FRAME));
                g_KeyboardCtx->ActiveDataPending = TRUE;
                KeReleaseSpinLockFromDpcLevel(&g_KeyboardCtx->FrameQueueLock);

                if (g_KeyboardCtx->PulseWorkItem) {
                    IoQueueWorkItem(g_KeyboardCtx->PulseWorkItem, PulseWorkItemRoutine, DelayedWorkQueue, g_KeyboardCtx);
                }
            }

            ExFreeToNPagedLookasideList(&pCtx->FrameLookaside, frame);
        }
        else {
            break;
        }
    }

    // 2. 触发鼠标 WorkItem
    if (pCtx->ActiveDataPending && pCtx->PulseWorkItem) {
        IoQueueWorkItem(pCtx->PulseWorkItem, PulseWorkItemRoutine, DelayedWorkQueue, pCtx);
    }

    // 3. 【防风暴核心】：重新设置定时器时，确保到期时间严格在未来！
    if (!IsListEmpty(&pCtx->FrameQueueListHead)) {
        PLIST_ENTRY nextEntry = pCtx->FrameQueueListHead.Flink;
        POMNIHID_TIMED_FRAME nextFrame = CONTAINING_RECORD(nextEntry, OMNIHID_TIMED_FRAME, ListEntry);

        // 如果下一帧的时间已经过去或就是现在，强制推迟到 1ms 后，防止 DPC 疯狂触发
        if (nextFrame->DueTime.QuadPart <= now.QuadPart) {
            LARGE_INTEGER delay;
            delay.QuadPart = -10 * 1000; // 1ms 延迟
            KeSetTimer(&pCtx->PulseTimer, delay, &pCtx->TimerDpc);
        }
        else {
            // 正常设置绝对到期时间
            KeSetTimer(&pCtx->PulseTimer, nextFrame->DueTime, &pCtx->TimerDpc);
        }
    }
    else {
        pCtx->TimerActive = FALSE;
    }

    KeReleaseSpinLockFromDpcLevel(&pCtx->FrameQueueLock);
}

// ============================================================
// 工作项例程：严谨的反压消费逻辑
// ============================================================
VOID PulseWorkItemRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PPDO_CONTEXT pdoCtx = (PPDO_CONTEXT)Context;
    if (!pdoCtx || pdoCtx->DeviceRemoving) return;

    KIRQL oldIrql;
    PLIST_ENTRY listEntry;
    PPENDING_IRP_ENTRY pendingEntry = NULL;
    PIRP irp = NULL;
    PURB urb = NULL;
    OMNIHID_FUTURE_FRAME currentFrame = { 0 };
    BOOLEAN hasData = FALSE;

    // 1. 提取到期数据 (只看不拿，必须拿到 IRP 才算消费)
    KeAcquireSpinLock(&pdoCtx->FrameQueueLock, &oldIrql);
    if (pdoCtx->ActiveDataPending) {
        currentFrame = pdoCtx->ActiveFrameData;
        hasData = TRUE;
    }
    KeReleaseSpinLock(&pdoCtx->FrameQueueLock, oldIrql);
    // 【诊断日志】：看看 WorkItem 是否拿到了数据
    if (hasData) {
        DIAG("WORK: Got Data! DevType=%d, Mod=%d, Key1=%d, MouseX=%d",
            pdoCtx->DeviceType, currentFrame.KeyboardModifier, currentFrame.KeyboardKeys[0], currentFrame.MouseX);
    }
    if (!hasData) return;

    // 2. 取出挂起的中断 IRP
    KeAcquireSpinLock(&pdoCtx->PendingIrpLock, &oldIrql);
    if (IsListEmpty(&pdoCtx->PendingIrpList)) {
        KeReleaseSpinLock(&pdoCtx->PendingIrpLock, oldIrql);
        return; // 没有 IRP，保留数据，等 IRP 到来时在 UrbForge 中触发
    }

    listEntry = RemoveHeadList(&pdoCtx->PendingIrpList);
    pendingEntry = CONTAINING_RECORD(listEntry, PENDING_IRP_ENTRY, ListEntry);
    irp = pendingEntry->Irp;
    urb = pendingEntry->Urb;
    KeReleaseSpinLock(&pdoCtx->PendingIrpLock, oldIrql);

    // 3. 此时既有数据又有 IRP，安全消费数据
    KeAcquireSpinLock(&pdoCtx->FrameQueueLock, &oldIrql);
    pdoCtx->ActiveDataPending = FALSE; // 消费成功，清除标志
    KeReleaseSpinLock(&pdoCtx->FrameQueueLock, oldIrql);

    if (!irp || !urb) {
        if (pendingEntry) ExFreeToNPagedLookasideList(&pdoCtx->PendingIrpLookaside, pendingEntry);
        return;
    }

    // 4. 填充 URB 缓冲区
    if (urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER &&
        urb->UrbBulkOrInterruptTransfer.PipeHandle == pdoCtx->InterruptInPipeHandle) {

        struct _URB_BULK_OR_INTERRUPT_TRANSFER* urbTransfer = &urb->UrbBulkOrInterruptTransfer;
        PVOID transferBuffer = urbTransfer->TransferBuffer;
        if (!transferBuffer && urbTransfer->TransferBufferMDL) {
            transferBuffer = MmGetSystemAddressForMdlSafe(urbTransfer->TransferBufferMDL, NormalPagePriority);
        }
        ULONG bufLen = urbTransfer->TransferBufferLength;

        if (pdoCtx->DeviceType == OmniHID_DeviceType_Mouse && transferBuffer && bufLen >= 4) {
            PUCHAR buf = (PUCHAR)transferBuffer;
            UCHAR xByte = (UCHAR)((currentFrame.MouseX > 127) ? 127 : ((currentFrame.MouseX < -128) ? -128 : (CHAR)currentFrame.MouseX));
            UCHAR yByte = (UCHAR)((currentFrame.MouseY > 127) ? 127 : ((currentFrame.MouseY < -128) ? -128 : (CHAR)currentFrame.MouseY));

            buf[0] = currentFrame.MouseButtons;
            buf[1] = xByte;
            buf[2] = yByte;
            buf[3] = (UCHAR)currentFrame.MouseWheel;

            urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
            irp->IoStatus.Status = STATUS_SUCCESS;
            irp->IoStatus.Information = 4;
        }
        else if (pdoCtx->DeviceType == OmniHID_DeviceType_Keyboard && transferBuffer && bufLen >= 8) {
            PUCHAR buf = (PUCHAR)transferBuffer;
            buf[0] = currentFrame.KeyboardModifier;
            buf[1] = currentFrame.KeyboardReserved;
            RtlCopyMemory(&buf[2], currentFrame.KeyboardKeys, 6);

            urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
            irp->IoStatus.Status = STATUS_SUCCESS;
            irp->IoStatus.Information = 8;

            // 【诊断日志】：确认已写入 IRP
            DIAG("WORK: Keyboard IRP completed! Mod=%d, Key1=%d", buf[0], buf[2]);
        }
        else {
            KeAcquireSpinLock(&pdoCtx->PendingIrpLock, &oldIrql);
            InsertHeadList(&pdoCtx->PendingIrpList, &pendingEntry->ListEntry);
            KeReleaseSpinLock(&pdoCtx->PendingIrpLock, oldIrql);
            return;
        }
    }
    else {
        KeAcquireSpinLock(&pdoCtx->PendingIrpLock, &oldIrql);
        InsertHeadList(&pdoCtx->PendingIrpList, &pendingEntry->ListEntry);
        KeReleaseSpinLock(&pdoCtx->PendingIrpLock, oldIrql);
        return;
    }

    IoCompleteRequest(irp, IO_NO_INCREMENT);
    ExFreeToNPagedLookasideList(&pdoCtx->PendingIrpLookaside, pendingEntry);
}

// ============================================================
// 初始化与清理
// ============================================================
NTSTATUS PulseEngineInitialize(PPDO_CONTEXT Context, PDEVICE_OBJECT WdmPdo)
{
    ExInitializeNPagedLookasideList(&Context->PendingIrpLookaside, NULL, NULL, POOL_FLAG_NON_PAGED, sizeof(PENDING_IRP_ENTRY), 'EPmO', 0);

    KeInitializeSpinLock(&Context->PendingIrpLock);
    InitializeListHead(&Context->PendingIrpList);

    Context->DeviceRemoving = FALSE;
    Context->WdmPdo = WdmPdo;
    Context->PulseWorkItem = IoAllocateWorkItem(WdmPdo);
    if (!Context->PulseWorkItem) {
        ExDeleteNPagedLookasideList(&Context->PendingIrpLookaside);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeSpinLock(&Context->FrameQueueLock);
    InitializeListHead(&Context->FrameQueueListHead);
    ExInitializeNPagedLookasideList(&Context->FrameLookaside, NULL, NULL, POOL_FLAG_NON_PAGED, sizeof(OMNIHID_TIMED_FRAME), 'TfmO', 0);

    KeInitializeTimer(&Context->PulseTimer);
    KeInitializeDpc(&Context->TimerDpc, TimerDpcRoutine, Context);
    Context->TimerActive = FALSE;
    Context->ActiveDataPending = FALSE;

    return STATUS_SUCCESS;
}

VOID PulseEngineCleanupPendingIrps(PPDO_CONTEXT Context)
{
    KIRQL oldIrql;
    Context->DeviceRemoving = TRUE;

    KeCancelTimer(&Context->PulseTimer);
    KeRemoveQueueDpc(&Context->TimerDpc);

    KeAcquireSpinLock(&Context->FrameQueueLock, &oldIrql);
    while (!IsListEmpty(&Context->FrameQueueListHead)) {
        PLIST_ENTRY entry = RemoveHeadList(&Context->FrameQueueListHead);
        POMNIHID_TIMED_FRAME frame = CONTAINING_RECORD(entry, OMNIHID_TIMED_FRAME, ListEntry);
        ExFreeToNPagedLookasideList(&Context->FrameLookaside, frame);
    }
    KeReleaseSpinLock(&Context->FrameQueueLock, oldIrql);
    ExDeleteNPagedLookasideList(&Context->FrameLookaside);

    KeAcquireSpinLock(&Context->PendingIrpLock, &oldIrql);
    while (!IsListEmpty(&Context->PendingIrpList)) {
        PLIST_ENTRY entry = RemoveHeadList(&Context->PendingIrpList);
        PPENDING_IRP_ENTRY pendingEntry = CONTAINING_RECORD(entry, PENDING_IRP_ENTRY, ListEntry);
        if (pendingEntry->Irp) {
            pendingEntry->Irp->IoStatus.Status = STATUS_DELETE_PENDING;
            IoCompleteRequest(pendingEntry->Irp, IO_NO_INCREMENT);
        }
        ExFreeToNPagedLookasideList(&Context->PendingIrpLookaside, pendingEntry);
    }
    KeReleaseSpinLock(&Context->PendingIrpLock, oldIrql);

    if (Context->PulseWorkItem) {
        IoFreeWorkItem(Context->PulseWorkItem);
        Context->PulseWorkItem = NULL;
    }

    ExDeleteNPagedLookasideList(&Context->PendingIrpLookaside);
}