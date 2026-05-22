/*++
IhvbPdo.c - 拓扑伪装层
--*/

#include "OmniHID.h"

VOID AcquireRealUsbHubInterface(PPDO_CONTEXT pCtx) {
    UNREFERENCED_PARAMETER(pCtx);
    return;
}

NTSTATUS PdoPnpIrpPreprocess(WDFDEVICE Device, PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = Irp->IoStatus.Status;

    switch (stack->MinorFunction) {
    case IRP_MN_QUERY_BUS_INFORMATION:
    {
        PPNP_BUS_INFORMATION busInfo = (PPNP_BUS_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, sizeof(PNP_BUS_INFORMATION), 'BsiP');
        if (busInfo) {
            busInfo->BusTypeGuid = GUID_BUS_TYPE_USB;
            busInfo->LegacyBusType = PCIBus;
            busInfo->BusNumber = 0;
            Irp->IoStatus.Information = (ULONG_PTR)busInfo;
            status = STATUS_SUCCESS;
        }
        else { status = STATUS_INSUFFICIENT_RESOURCES; }
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    case IRP_MN_QUERY_INTERFACE:
    {
        status = STATUS_NOT_SUPPORTED;
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    // ============================================================
    // 【关键移除】：删除了 IRP_MN_QUERY_ID 分支！
    // 多 PDO 架构下，硬件 ID 由 EvtChildListCreateDevice 中的 
    // WdfPdoInitAssignDeviceID 等 API 动态分配，绝不能在此硬编码拦截！
    // ============================================================
    }

    // 对于未处理的 PnP IRP，交给 WDF 框架默认处理
    return WdfDeviceWdmDispatchPreprocessedIrp(Device, Irp);
}