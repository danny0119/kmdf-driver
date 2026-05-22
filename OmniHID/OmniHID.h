#pragma once

// ===================================================================
// WDK 依赖
// ===================================================================
#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbbusif.h>
#include <usbioctl.h>
#include <usbdrivr.h>
#include <devguid.h>
#include <wdmguid.h>
#include <hidpddi.h>
#include <hidclass.h>
#include <wdmsec.h>
#include <ntimage.h>
#include <ntstrsafe.h>

// ===================================================================
// 隐蔽性加固：Release 模式下彻底禁用日志
// ===================================================================
#if DBG
#define DIAG(fmt, ...) DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "OmniHID: " fmt "\n", __VA_ARGS__)
#else
#define DIAG(fmt, ...)  // 发布版：完全剃除，不留任何痕迹
#endif

// ===================================================================
// 解除 WDK API 宏劫持
// ===================================================================
#ifdef ExAllocatePoolWithTag
#undef ExAllocatePoolWithTag
#endif
#ifdef ExFreePool
#undef ExFreePool
#endif
NTKERNELAPI PVOID ExAllocatePoolWithTag(__in POOL_TYPE PoolType, __in SIZE_T NumberOfBytes, __in ULONG Tag);
NTKERNELAPI VOID ExFreePool(__in PVOID P);

// ===================================================================
// 自定义严格 1 字节对齐的 HID 描述符
// ===================================================================
#pragma pack(push, 1)
typedef struct _HID_DESCRIPTOR_PACKED {
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    USHORT bcdHID;
    UCHAR  bCountry;
    UCHAR  bNumDescriptors;
    struct {
        UCHAR  bReportType;
        USHORT wReportLength;
    } DescriptorList[1];
} HID_DESCRIPTOR_PACKED, * PHID_DESCRIPTOR_PACKED;
#pragma pack(pop)

// ===================================================================
// IOCTL 宏与常量补全
// ===================================================================
#ifndef FILE_DEVICE_HID
#define FILE_DEVICE_HID 0x0000003F
#endif
#ifndef FILE_DEVICE_USB
#define FILE_DEVICE_USB 0x00000022
#endif

#ifndef IOCTL_HID_GET_DEVICE_DESCRIPTOR
#define IOCTL_HID_GET_DEVICE_DESCRIPTOR CTL_CODE(FILE_DEVICE_HID, 0x00, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_HID_GET_REPORT_DESCRIPTOR
#define IOCTL_HID_GET_REPORT_DESCRIPTOR CTL_CODE(FILE_DEVICE_HID, 0x01, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_HID_GET_DEVICE_ATTRIBUTES
#define IOCTL_HID_GET_DEVICE_ATTRIBUTES CTL_CODE(FILE_DEVICE_HID, 0x02, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_HID_WRITE_REPORT
#define IOCTL_HID_WRITE_REPORT CTL_CODE(FILE_DEVICE_HID, 0x08, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_HID_SET_FEATURE
#define IOCTL_HID_SET_FEATURE CTL_CODE(FILE_DEVICE_HID, 0x0B, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_HID_GET_FEATURE
#define IOCTL_HID_GET_FEATURE CTL_CODE(FILE_DEVICE_HID, 0x0C, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_INTERNAL_USB_SUBMIT_URB
#define IOCTL_INTERNAL_USB_SUBMIT_URB CTL_CODE(FILE_DEVICE_USB, 0x0003, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_GET_PORT_STATUS
#define IOCTL_INTERNAL_USB_GET_PORT_STATUS CTL_CODE(FILE_DEVICE_USB, 0x0014, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_RESET_PORT
#define IOCTL_INTERNAL_USB_RESET_PORT CTL_CODE(FILE_DEVICE_USB, 0x0017, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_CYCLE_PORT
#define IOCTL_INTERNAL_USB_CYCLE_PORT CTL_CODE(FILE_DEVICE_USB, 0x0018, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_GET_CONTROLLER_INFO
#define IOCTL_INTERNAL_USB_GET_CONTROLLER_INFO CTL_CODE(FILE_DEVICE_USB, 0x0015, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_GET_HUB_COUNT
#define IOCTL_INTERNAL_USB_GET_HUB_COUNT CTL_CODE(FILE_DEVICE_USB, 0x0016, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO
#define IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO CTL_CODE(FILE_DEVICE_USB, 0x0019, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

#define HID_GET_REPORT_REQUEST    0x01
#define HID_GET_IDLE_REQUEST      0x02
#define HID_GET_PROTOCOL_REQUEST  0x03
#define HID_SET_REPORT_REQUEST    0x09
#define HID_SET_IDLE_REQUEST      0x0A
#define HID_SET_PROTOCOL_REQUEST  0x0B

#define HID_REPORT_TYPE_INPUT     0x01
#define HID_REPORT_TYPE_OUTPUT    0x02
#define HID_REPORT_TYPE_FEATURE   0x03

// ===================================================================
// USB 控制器信息结构体
// ===================================================================
#ifndef _OMNIHID_USB_CONTROLLER_INFO_DEFINED
#define _OMNIHID_USB_CONTROLLER_INFO_DEFINED
typedef enum _OMNIHID_USB_CONTROLLER_FLAVOR {
    OMNIHID_USB_HcGeneric = 0, OMNIHID_OHCI_Generic = 100, OMNIHID_UHCI_Generic = 200, OMNIHID_EHCI_Generic = 1000
} OMNIHID_USB_CONTROLLER_FLAVOR;
typedef struct _OMNIHID_USB_CONTROLLER_INFO_0 {
    ULONG PciVendorId; ULONG PciDeviceId; ULONG PciRevision; ULONG NumberOfRootPorts;
    OMNIHID_USB_CONTROLLER_FLAVOR ControllerFlavor; ULONG HcFeatureFlags;
    ULONG PciBusNumber; ULONG PciBusDevice; ULONG PciBusFunction;
} OMNIHID_USB_CONTROLLER_INFO_0, * POMNIHID_USB_CONTROLLER_INFO_0;
#endif

// ===================================================================
// 全局宏与常量
// ===================================================================
#define API_PLACEHOLDER 0xAAAAAAAAAAAAAAAA
#define LIGHTSPEED_VID L"046D"
#define LIGHTSPEED_PID L"C547"
#define LIGHTSPEED_REV L"0102"
#define LIGHTSPEED_REPORT_ID_DPI_CMD      0x10
#define LIGHTSPEED_REPORT_ID_BATTERY      0x15
#define LIGHTSPEED_REPORT_ID_FIRMWARE     0x20

// ===================================================================
// 【关键前置定义】：必须在 PDO_CONTEXT 之前！
// ===================================================================
#define MAX_FAKE_HANDLES 8

// 设备类型枚举
typedef enum _OMNIHID_DEVICE_TYPE {
    OmniHID_DeviceType_Unknown = 0,
    OmniHID_DeviceType_Mouse,
    OmniHID_DeviceType_Keyboard,
    OmniHID_DeviceType_Gamepad
} OMNIHID_DEVICE_TYPE;

// 挂起 IRP 条目
typedef struct _PENDING_IRP_ENTRY {
    LIST_ENTRY ListEntry;
    PIRP Irp;
    PURB Urb;
} PENDING_IRP_ENTRY, * PPENDING_IRP_ENTRY;

// 子设备识别描述符
typedef struct _OMNIHID_CHILD_ID {
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;
    OMNIHID_DEVICE_TYPE DeviceType;
} OMNIHID_CHILD_ID, * POMNIHID_CHILD_ID;

// ===================================================================
// 傅里叶时序混淆协议 (未来帧序列)
// ===================================================================
#pragma pack(push, 1)

// 单个未来帧 (极具隐蔽性的对齐结构)
typedef struct _OMNIHID_FUTURE_FRAME {
    UINT32 OffsetMs;         // 相对当前时间的毫秒偏移量 (0=立即, 8=8ms后)
    UCHAR  HasKeyboardEvent;
    UCHAR  KeyboardModifier; // 键盘修饰键
    UCHAR  KeyboardReserved;
    UCHAR  KeyboardKeys[6];  // 键盘按键
    UCHAR  MouseButtons;     // 鼠标按键
    SHORT  MouseX;           // 鼠标 X
    SHORT  MouseY;           // 鼠标 Y
    CHAR   MouseWheel;       // 滚轮
    UCHAR  MouseReserved;
} OMNIHID_FUTURE_FRAME, * POMNIHID_FUTURE_FRAME;

// 一次 SetFeature 下发的完整载荷 (3个未来帧，共 48 字节)
#define MAX_FRAMES_PER_PACKET 3
typedef struct _OMNIHID_COVERT_FUTURE_DATA {
    OMNIHID_FUTURE_FRAME Frames[MAX_FRAMES_PER_PACKET];
} OMNIHID_COVERT_FUTURE_DATA, * POMNIHID_COVERT_FUTURE_DATA;

#pragma pack(pop)

// R0 内核定时帧队列
#define MAX_PENDING_FRAMES 64
typedef struct _OMNIHID_TIMED_FRAME {
    LIST_ENTRY ListEntry;
    LARGE_INTEGER DueTime;       // 绝对到期时间 (KeQueryPerformanceCounter)
    OMNIHID_FUTURE_FRAME Data;   // 帧数据
} OMNIHID_TIMED_FRAME, * POMNIHID_TIMED_FRAME;

// ===================================================================
// 核心数据结构
// ===================================================================
typedef struct _PDO_CONTEXT {
    PDEVICE_OBJECT RealUsbHubDeviceObject;
    BUS_INTERFACE_STANDARD UsbBusInterface;

    KSPIN_LOCK HandlePoolLock;
    PVOID FakeHandlePool[MAX_FAKE_HANDLES];
    ULONG FakeHandleCount;
    USBD_PIPE_HANDLE InterruptInPipeHandle;

    KSPIN_LOCK PendingIrpLock;
    LIST_ENTRY PendingIrpList;
    BOOLEAN DeviceRemoving;

    NPAGED_LOOKASIDE_LIST PendingIrpLookaside;
    PIO_WORKITEM PulseWorkItem;
    PDEVICE_OBJECT WdmPdo;
    OMNIHID_DEVICE_TYPE DeviceType;

    // ============================================================
    // 时序混淆引擎核心组件
    // ============================================================
    KSPIN_LOCK FrameQueueLock;
    LIST_ENTRY FrameQueueListHead;              // 按时间排序的挂起帧队列
    NPAGED_LOOKASIDE_LIST FrameLookaside;       // 帧节点内存池
    KTIMER  PulseTimer;                         // 内核高精度定时器
    KDPC    TimerDpc;                           // 定时器触发的 DPC
    BOOLEAN TimerActive;                        // 定时器是否正在运行

    // 合并的待发送数据 (由 TimerDpc 填充，WorkItem 消费)
    BOOLEAN ActiveDataPending;
    OMNIHID_FUTURE_FRAME ActiveFrameData;

} PDO_CONTEXT, * PPDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PDO_CONTEXT, PdoGetContext)

// ===================================================================
// CR0 绝对隔离宏
// ===================================================================
FORCEINLINE VOID DisableWriteProtect() {
    _disable(); KeMemoryBarrier();
    __writecr0(__readcr0() & ~0x10000);
    KeMemoryBarrier();
}
FORCEINLINE VOID EnableWriteProtect() {
    KeMemoryBarrier();
    __writecr0(__readcr0() | 0x10000);
    KeMemoryBarrier();
    _enable();
}

// ===================================================================
// WDK 防御性结构体补全
// ===================================================================
#ifndef _HID_DESCRIPTOR_
#define _HID_DESCRIPTOR_
typedef struct _HID_DESCRIPTOR {
    UCHAR  bLength; UCHAR  bDescriptorType; USHORT bcdHID; UCHAR  bCountry; UCHAR  bNumDescriptors;
    struct _HID_DESCRIPTOR_DESC { UCHAR  bReportType; USHORT wReportLength; } DescriptorList[1];
} HID_DESCRIPTOR, * PHID_DESCRIPTOR;
#endif

#ifndef _HID_DEVICE_ATTRIBUTES_
#define _HID_DEVICE_ATTRIBUTES_
typedef struct _HID_DEVICE_ATTRIBUTES {
    ULONG  Size; USHORT VendorID; USHORT ProductID; USHORT VersionNumber; USHORT Reserved[11];
} HID_DEVICE_ATTRIBUTES, * PHID_DEVICE_ATTRIBUTES;
#endif

// ===================================================================
// 全局函数声明
// ===================================================================
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_WDF_CHILD_LIST_CREATE_DEVICE EvtChildListCreateDevice;
EVT_WDF_DEVICE_CONTEXT_CLEANUP EvtDeviceContextCleanup;

NTSTATUS PdoPnpIrpPreprocess(WDFDEVICE Device, PIRP Irp);
NTSTATUS PdoInternalDeviceControlPreprocess(WDFDEVICE Device, PIRP Irp);
USBD_PIPE_HANDLE AllocateFakePipeHandle(PPDO_CONTEXT pCtx);
NTSTATUS PulseEngineInitialize(PPDO_CONTEXT Context, PDEVICE_OBJECT WdmPdo);
VOID PulseEngineCleanupPendingIrps(PPDO_CONTEXT Context);
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtPdoIoDeviceControl;
BOOLEAN IsAllowedCaller(VOID);

// UrbForge 函数声明
NTSTATUS HandleUrbGetDescriptor(PPDO_CONTEXT pCtx, PURB Urb);
NTSTATUS HandleUrbSelectConfiguration(PPDO_CONTEXT pCtx, PURB Urb);
NTSTATUS HandleUrbSelectInterface(PPDO_CONTEXT pCtx, PURB Urb);

VOID PulseWorkItemRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context);

// ===================================================================
// 外部描述符变量声明
// ===================================================================
extern USB_DEVICE_DESCRIPTOR g_LogitechDeviceDescriptor;
extern UCHAR g_LogitechFullConfigDescriptor[];
extern ULONG g_LogitechFullConfigDescriptorSize;
extern HID_DESCRIPTOR_PACKED g_LogitechHidDescriptor;
extern UCHAR g_LogitechReportDescriptor[];

extern USB_DEVICE_DESCRIPTOR g_KeyboardDeviceDescriptor;
extern UCHAR g_KeyboardFullConfigDescriptor[];
extern ULONG g_KeyboardFullConfigDescriptorSize;
extern HID_DESCRIPTOR_PACKED g_KeyboardHidDescriptor;
extern UCHAR g_KeyboardReportDescriptor[];
extern USHORT g_KeyboardReportDescSize;