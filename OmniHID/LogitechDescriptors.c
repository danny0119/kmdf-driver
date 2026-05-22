/*++
LogitechDescriptors.c - 无 Report ID 版本
单顶层集合设备禁止显式声明 Report ID
--*/

#include "OmniHID.h"
#include "LogitechDescriptors.h"

USB_DEVICE_DESCRIPTOR g_LogitechDeviceDescriptor = {
    sizeof(USB_DEVICE_DESCRIPTOR), 18,
    0x0200, 0x03, 0x00, 0x00, 0x40,
    0x046D, 0xC547, 0x0102,
    0x00, 0x00, 0x00, 0x01
};

UCHAR g_LogitechReportDescriptor[] = {
    // ============================================================
    // 单一 Mouse Collection（无任何 Report ID 声明！）
    // ============================================================
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)

    // ---- Input Report (隐式 ID=0, 4字节, 无 ID 前缀) ----
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x05,        //     Usage Maximum (5)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Const,Array,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection (Physical)

    // ---- Feature Report (隐式 ID=0, 7字节, 无 ID 前缀) ----
    // R3 注入器写入，DPC 解析后注入 Input Report
    // ---- Feature Report (隐式 ID=0, 15字节载荷) ----
    // [0..6] 鼠标数据
    // [7] 键盘修饰键
    // [8] 键盘保留
    // [9..14] 键盘按键数组
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor 0xFF00)
    0x09, 0x01,        //   Usage (Vendor 1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x3C,        //   Report Count (60)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    0xC0               // End Collection (Mouse)
};
USHORT g_LogitechReportDescSize = sizeof(g_LogitechReportDescriptor);

HID_DESCRIPTOR_PACKED g_LogitechHidDescriptor = {
    sizeof(HID_DESCRIPTOR_PACKED),
    0x21,
    0x0111,
    0x00,
    0x01,
    { { 0x22, sizeof(g_LogitechReportDescriptor) } }
};

#pragma pack(push, 1)
typedef struct _USB_CONFIGURATION_SET {
    USB_CONFIGURATION_DESCRIPTOR Config;
    USB_INTERFACE_DESCRIPTOR     Interface;
    HID_DESCRIPTOR_PACKED        Hid;
    USB_ENDPOINT_DESCRIPTOR      EndpointIn;
} USB_CONFIGURATION_SET, * PUSB_CONFIGURATION_SET;
#pragma pack(pop)

USB_CONFIGURATION_SET g_LogitechConfigSet = {
    {
        sizeof(USB_CONFIGURATION_DESCRIPTOR), 9,
        sizeof(USB_CONFIGURATION_SET),
        0x01, 0x01, 0x00, 0xA0, 0xFA
    },
    {
        sizeof(USB_INTERFACE_DESCRIPTOR), 4,
        0x00, 0x00, 0x01,
        0x03, 0x01, 0x02, 0x00
    },
    {
        sizeof(HID_DESCRIPTOR_PACKED), 0x21, 0x0111, 0x00, 0x01,
        { { 0x22, sizeof(g_LogitechReportDescriptor) } }
    },
    {
        sizeof(USB_ENDPOINT_DESCRIPTOR), 7,
        0x81, 0x03, 0x0040, 0x01
    }
};

UCHAR g_LogitechFullConfigDescriptor[sizeof(USB_CONFIGURATION_SET)];
ULONG g_LogitechFullConfigDescriptorSize = sizeof(USB_CONFIGURATION_SET);
// ===================================================================
// 键盘专属描述符 (Boot Keyboard, 8字节输入报告)
// ===================================================================

USB_DEVICE_DESCRIPTOR g_KeyboardDeviceDescriptor = {
    sizeof(USB_DEVICE_DESCRIPTOR), 18,
    0x0200, 0x03, 0x00, 0x00, 0x40,     // Class 0x03 (HID)
    0x046D, 0xC33E, 0x0200,             // Logitech G915
    0x00, 0x00, 0x00, 0x01
};

UCHAR g_KeyboardReportDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x05,        //   Usage Maximum (5)
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data,Array)
    0xC0               // End Collection
};
USHORT g_KeyboardReportDescSize = sizeof(g_KeyboardReportDescriptor);

HID_DESCRIPTOR_PACKED g_KeyboardHidDescriptor = {
    sizeof(HID_DESCRIPTOR_PACKED),
    0x21, 0x0111, 0x00, 0x01,
    { { 0x22, sizeof(g_KeyboardReportDescriptor) } }
};

#pragma pack(push, 1)
typedef struct _USB_KBD_CONFIG_SET {
    USB_CONFIGURATION_DESCRIPTOR Config;
    USB_INTERFACE_DESCRIPTOR     Interface;
    HID_DESCRIPTOR_PACKED        Hid;
    USB_ENDPOINT_DESCRIPTOR      EndpointIn;
} USB_KBD_CONFIG_SET, * PUSB_KBD_CONFIG_SET;
#pragma pack(pop)

USB_KBD_CONFIG_SET g_KeyboardConfigSet = {
    {
        sizeof(USB_CONFIGURATION_DESCRIPTOR), 9,
        sizeof(USB_KBD_CONFIG_SET),
        0x01, 0x01, 0x00, 0xA0, 0xFA
    },
    {
        sizeof(USB_INTERFACE_DESCRIPTOR), 4,
        0x00, 0x00, 0x01,
        0x03, 0x01, 0x01, 0x00  // <--- Protocol 必须是 1 (Boot Keyboard)
    },
    {
        sizeof(HID_DESCRIPTOR_PACKED), 0x21, 0x0111, 0x00, 0x01,
        { { 0x22, sizeof(g_KeyboardReportDescriptor) } }
    },
    {
        sizeof(USB_ENDPOINT_DESCRIPTOR), 7,
        0x81, 0x03, 0x0008, 0x01
    }
};

UCHAR g_KeyboardFullConfigDescriptor[sizeof(USB_KBD_CONFIG_SET)];
ULONG g_KeyboardFullConfigDescriptorSize = sizeof(USB_KBD_CONFIG_SET);

// 在 InitLogitechDescriptors() 中加入初始化
// RtlCopyMemory(g_KeyboardFullConfigDescriptor, &g_KeyboardConfigSet, sizeof(USB_KBD_CONFIG_SET));
VOID InitLogitechDescriptors() {
    RtlCopyMemory(g_LogitechFullConfigDescriptor, &g_LogitechConfigSet, sizeof(USB_CONFIGURATION_SET));
    RtlCopyMemory(g_KeyboardFullConfigDescriptor, &g_KeyboardConfigSet, sizeof(USB_KBD_CONFIG_SET));
}