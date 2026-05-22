/*++
OmniLog.c - 文件日志引擎
直接写 C:\OmniHID_Trace.log，不依赖 DbgPrint 或调试器
--*/

#include "OmniHID.h"

// 日志文件路径
#define LOG_FILE_PATH L"\\??\\C:\\OmniHID_Trace.log"

// 自旋锁保护文件写入
KSPIN_LOCK g_LogLock;
BOOLEAN g_LogLockInitialized = FALSE;

// 简单的 vsnprintf 实现（内核模式不支持 vsprintf）
// 使用 DbgPrintEx 的内部格式化器
NTSTATUS KernelVsnPrintf(PCHAR Buffer, SIZE_T Size, PCCH Format, va_list Args)
{
    // 使用 _vsnprintf（WDK 支持）
    return _vsnprintf(Buffer, Size, Format, Args);
}

VOID OmniLogInit()
{
    if (!g_LogLockInitialized) {
        KeInitializeSpinLock(&g_LogLock);
        g_LogLockInitialized = TRUE;
    }
}

VOID OmniLog(PCCH Format, ...)
{
    KIRQL oldIrql;
    HANDLE hFile = NULL;
    IO_STATUS_BLOCK ioStatus;
    OBJECT_ATTRIBUTES objAttrs;
    UNICODE_STRING filePath;
    CHAR buffer[512];
    SIZE_T length;
    LARGE_INTEGER systemTime;
    TIME_FIELDS timeFields;

    if (!g_LogLockInitialized) OmniLogInit();

    // 获取当前时间
    KeQuerySystemTime(&systemTime);
    RtlTimeToTimeFields(&systemTime, &timeFields);

    // 格式化时间戳前缀
    length = _snprintf(buffer, sizeof(buffer) - 1,
        "[%02d:%02d:%02d.%03d] ",
        timeFields.Hour, timeFields.Minute,
        timeFields.Second, timeFields.Milliseconds);

    // 格式化用户消息
    va_list args;
    va_start(args, Format);
    length += _vsnprintf(buffer + length, sizeof(buffer) - length - 3, Format, args);
    va_end(args);

    // 添加换行符
    if (length >= sizeof(buffer) - 3) length = sizeof(buffer) - 3;
    buffer[length++] = '\r';
    buffer[length++] = '\n';
    buffer[length] = '\0';

    // 自旋锁保护（防止并发写入）
    KeAcquireSpinLock(&g_LogLock, &oldIrql);

    // 打开/创建日志文件
    RtlInitUnicodeString(&filePath, LOG_FILE_PATH);
    InitializeObjectAttributes(&objAttrs, &filePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS status = ZwCreateFile(&hFile,
        FILE_APPEND_DATA | SYNCHRONIZE,
        &objAttrs, &ioStatus, NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
        NULL, 0);

    if (NT_SUCCESS(status) && hFile != NULL) {
        ZwWriteFile(hFile, NULL, NULL, NULL, &ioStatus,
            buffer, (ULONG)length, NULL, NULL);
        ZwClose(hFile);
    }

    KeReleaseSpinLock(&g_LogLock, oldIrql);
}