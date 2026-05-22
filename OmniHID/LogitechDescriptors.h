#pragma once

extern USB_DEVICE_DESCRIPTOR g_LogitechDeviceDescriptor;
extern UCHAR g_LogitechFullConfigDescriptor[];
extern ULONG g_LogitechFullConfigDescriptorSize;
extern HID_DESCRIPTOR_PACKED g_LogitechHidDescriptor;    // 改为 HID_DESCRIPTOR_PACKED！
extern UCHAR g_LogitechReportDescriptor[];
extern USHORT g_LogitechReportDescSize;