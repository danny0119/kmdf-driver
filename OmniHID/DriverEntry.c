/*++
DriverEntry.c - 驱动入口（零完成路径版）
--*/

#include <initguid.h>
#include "OmniHID.h"

extern VOID InitLogitechDescriptors();
extern PPDO_CONTEXT g_MouseCtx;
extern PPDO_CONTEXT g_KeyboardCtx;
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    InitLogitechDescriptors();

    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, EvtDriverDeviceAdd);
    status = WdfDriverCreate(DriverObject, RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
    return status;
}

NTSTATUS EvtDriverDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    WDF_CHILD_LIST_CONFIG childListConfig;
    WDF_OBJECT_ATTRIBUTES fdoAttrs;
    NTSTATUS status;

    WDF_CHILD_LIST_CONFIG_INIT(&childListConfig, sizeof(OMNIHID_CHILD_ID), EvtChildListCreateDevice);
    WdfFdoInitSetDefaultChildListConfig(DeviceInit, &childListConfig, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT(&fdoAttrs);
    WDFDEVICE hDevice;

    status = WdfDeviceCreate(&DeviceInit, &fdoAttrs, &hDevice);
    if (!NT_SUCCESS(status)) return status;

    // ============================================================
    // 【多 PDO 孵化】：一次性宣告三个子设备的存在
    // ============================================================
    OMNIHID_CHILD_ID childId = { 0 };
    childId.Header.IdentificationDescriptionSize = sizeof(OMNIHID_CHILD_ID);

    // 1. 枚举鼠标
    childId.DeviceType = OmniHID_DeviceType_Mouse;
    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
        WdfFdoGetDefaultChildList(hDevice), &childId.Header, NULL);
    if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_EXISTS) return status;

    // 2. 枚举键盘
    childId.DeviceType = OmniHID_DeviceType_Keyboard;
    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
        WdfFdoGetDefaultChildList(hDevice), &childId.Header, NULL);
    if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_EXISTS) return status;

    // 3. 枚举手柄 (暂时砍掉，后续复合设备再做)
    // childId.DeviceType = OmniHID_DeviceType_Gamepad;
    // status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
    //     WdfFdoGetDefaultChildList(hDevice), &childId.Header, NULL);
    // if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_EXISTS) return status;

    return STATUS_SUCCESS;
}

NTSTATUS EvtChildListCreateDevice(
    WDFCHILDLIST ChildList,
    PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    PWDFDEVICE_INIT ChildInit)
{
    UNREFERENCED_PARAMETER(ChildList);

    NTSTATUS status;
    WDFDEVICE childDevice;
    WDF_OBJECT_ATTRIBUTES pdoAttrs;
    PPDO_CONTEXT pCtx = NULL;

    // 获取我们传入的设备类型
    POMNIHID_CHILD_ID childIdDesc = (POMNIHID_CHILD_ID)IdentificationDescription;
    OMNIHID_DEVICE_TYPE deviceType = childIdDesc->DeviceType;

    // 动态 ID 缓冲区
    DECLARE_UNICODE_STRING_SIZE(deviceId, 128);
    DECLARE_UNICODE_STRING_SIZE(instanceId, 8);

    // ============================================================
    // 【硬件指纹分配】：根据设备类型伪装不同的硬件身份
    // ============================================================
    switch (deviceType) {
    case OmniHID_DeviceType_Mouse:
        // 鼠标：Logitech G502 (VID_046D&PID_C547)
        RtlUnicodeStringPrintf(&deviceId, L"HID\\VID_046D&PID_C547&REV_0102");
        RtlUnicodeStringPrintf(&instanceId, L"0");
        break;
    case OmniHID_DeviceType_Keyboard:
        // 键盘：Logitech G915 (VID_046D&PID_C33E)
        RtlUnicodeStringPrintf(&deviceId, L"HID\\VID_046D&PID_C33E&REV_0200");
        RtlUnicodeStringPrintf(&instanceId, L"1");
        break;
    case OmniHID_DeviceType_Gamepad:
        // 手柄降级：使用 HID\ 前缀，伪装成标准 HID 游戏控制器
        RtlUnicodeStringPrintf(&deviceId, L"HID\\VID_045E&PID_028E&REV_0114");
        RtlUnicodeStringPrintf(&instanceId, L"2");
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }

    status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
    if (!NT_SUCCESS(status)) goto Exit;

    // ---------- 动态添加 Hardware IDs ----------
    DECLARE_UNICODE_STRING_SIZE(hardwareId, 128);
    RtlUnicodeStringCopy(&hardwareId, &deviceId); // HW ID 通常与 Device ID 一致
    status = WdfPdoInitAddHardwareID(ChildInit, &hardwareId);
    if (!NT_SUCCESS(status)) goto Exit;

    // ---------- 动态添加 Compatible IDs (极其关键，决定加载哪个类驱动) ----------
    DECLARE_UNICODE_STRING_SIZE(compatId1, 64);
    DECLARE_UNICODE_STRING_SIZE(compatId2, 64);
    DECLARE_UNICODE_STRING_SIZE(compatId3, 64);

    switch (deviceType) {
    case OmniHID_DeviceType_Mouse:
        RtlUnicodeStringPrintf(&compatId1, L"USB\\Class_03&SubClass_01&Prot_02"); // Boot Mouse
        RtlUnicodeStringPrintf(&compatId2, L"USB\\Class_03&SubClass_01");
        RtlUnicodeStringPrintf(&compatId3, L"HID_CLASS");
        WdfPdoInitAddCompatibleID(ChildInit, &compatId1);
        WdfPdoInitAddCompatibleID(ChildInit, &compatId2);
        WdfPdoInitAddCompatibleID(ChildInit, &compatId3);
        break;
    case OmniHID_DeviceType_Keyboard:
        RtlUnicodeStringPrintf(&compatId1, L"USB\\Class_03&SubClass_01&Prot_01"); // Boot Keyboard
        RtlUnicodeStringPrintf(&compatId2, L"USB\\Class_03&SubClass_01");
        RtlUnicodeStringPrintf(&compatId3, L"HID_CLASS");
        WdfPdoInitAddCompatibleID(ChildInit, &compatId1);
        WdfPdoInitAddCompatibleID(ChildInit, &compatId2);
        WdfPdoInitAddCompatibleID(ChildInit, &compatId3);
        break;
    case OmniHID_DeviceType_Gamepad:
        // 强制加载 xusb22.sys (XInput 管线) 的核心兼容 ID
        RtlUnicodeStringPrintf(&compatId1, L"USB\\MS_COMP_XUSB20");
        RtlUnicodeStringPrintf(&compatId2, L"USB\\Class_FF&SubClass_5D&Prot_01");
        RtlUnicodeStringPrintf(&compatId3, L"USB\\Class_FF");
        WdfPdoInitAddCompatibleID(ChildInit, &compatId1);
        WdfPdoInitAddCompatibleID(ChildInit, &compatId2);
        WdfPdoInitAddCompatibleID(ChildInit, &compatId3);
        break;
    }

    status = WdfPdoInitAssignInstanceID(ChildInit, &instanceId);
    if (!NT_SUCCESS(status)) goto Exit;

    // ---------- IRP 预处理 ----------
    // 注意：移除了 IRP_MN_QUERY_ID，交由 WDF 框架根据我们设置的 ID 自行处理
    UCHAR MinorFunctions[] = { IRP_MN_QUERY_BUS_INFORMATION, IRP_MN_QUERY_INTERFACE };
    status = WdfDeviceInitAssignWdmIrpPreprocessCallback(ChildInit, PdoPnpIrpPreprocess, IRP_MJ_PNP, MinorFunctions, ARRAYSIZE(MinorFunctions));
    if (!NT_SUCCESS(status)) goto Exit;

    UCHAR internalIoctlMinor[] = { 0 };
    status = WdfDeviceInitAssignWdmIrpPreprocessCallback(ChildInit, PdoInternalDeviceControlPreprocess, IRP_MJ_INTERNAL_DEVICE_CONTROL, internalIoctlMinor, ARRAYSIZE(internalIoctlMinor));
    if (!NT_SUCCESS(status)) goto Exit;

    // ---------- 创建设备与上下文 ----------
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttrs, PDO_CONTEXT);
    pdoAttrs.EvtCleanupCallback = EvtDeviceContextCleanup;

    status = WdfDeviceCreate(&ChildInit, &pdoAttrs, &childDevice);
    if (!NT_SUCCESS(status)) goto Exit;

    pCtx = PdoGetContext(childDevice);
    RtlZeroMemory(pCtx, sizeof(PDO_CONTEXT));
    pCtx->DeviceType = deviceType; // <--- 将设备类型写入上下文，供 URB 拦截使用

    KeInitializeSpinLock(&pCtx->HandlePoolLock);
    KeInitializeSpinLock(&pCtx->PendingIrpLock);
    InitializeListHead(&pCtx->PendingIrpList);

    status = PulseEngineInitialize(pCtx, WdfDeviceWdmGetDeviceObject(childDevice));
    if (!NT_SUCCESS(status)) goto Exit;

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = EvtPdoIoDeviceControl;
    status = WdfIoQueueCreate(childDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);

Exit:
    // ============================================================
    // 【借壳上市核心】：注册全局上下文，供 UrbForge 拦截时使用
    // ============================================================
    if (NT_SUCCESS(status) && pCtx != NULL) { // <--- 【修复2】：增加 pCtx 空指针保护
        if (pCtx->DeviceType == OmniHID_DeviceType_Mouse) {
            g_MouseCtx = pCtx;
        }
        else if (pCtx->DeviceType == OmniHID_DeviceType_Keyboard) {
            g_KeyboardCtx = pCtx;
        }
    }

    return status;
}

VOID EvtDeviceContextCleanup(WDFOBJECT Device)
{
    PPDO_CONTEXT pCtx = PdoGetContext(Device);

    pCtx->DeviceRemoving = TRUE;

    KIRQL oldIrql;
    KeAcquireSpinLock(&pCtx->HandlePoolLock, &oldIrql);
    for (ULONG i = 0; i < pCtx->FakeHandleCount; i++) {
        if (pCtx->FakeHandlePool[i]) {
            ExFreePool(pCtx->FakeHandlePool[i]);
            pCtx->FakeHandlePool[i] = NULL;
        }
    }
    pCtx->FakeHandleCount = 0;
    KeReleaseSpinLock(&pCtx->HandlePoolLock, oldIrql);

    // PulseEngine 清理（极简版）
    PulseEngineCleanupPendingIrps(pCtx);
}