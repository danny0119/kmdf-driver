import os
import struct
import sys
import time
import shutil

# 典型的 Windows 10/11 系统驱动编译时间戳 (Unix 时间戳格式)
# 0x5D5E5F60 对应 UTC 2019-08-21 08:12:16 (常见的 mouhid.sys 时间戳)
DEFAULT_TIMESTAMP = 0x5D5E5F60 

def modify_pe_timestamp(file_path, new_timestamp):
    if not os.path.exists(file_path):
        print(f"[!] 文件不存在: {file_path}")
        return False

    # 备份原文件
    backup_path = file_path + ".bak"
    try:
        shutil.copy2(file_path, backup_path)
        print(f"[*] 原文件已备份至: {backup_path}")
    except Exception as e:
        print(f"[!] 备份失败: {e}")
        return False

    try:
        with open(file_path, 'r+b') as f:
            # 1. 读取 DOS Header，找到 e_lfanew (PE Header 偏移)
            f.seek(0x3C)
            e_lfanew_data = f.read(4)
            if len(e_lfanew_data) < 4:
                print("[!] 无效的 PE 文件：无法读取 e_lfanew")
                return False
            
            pe_offset = struct.unpack('<I', e_lfanew_data)[0]
            
            # 2. 验证 PE 签名
            f.seek(pe_offset)
            pe_signature = f.read(4)
            if pe_signature != b'PE\x00\x00':
                print("[!] 无效的 PE 文件：PE 签名不匹配")
                return False
            
            # 3. 定位 TimeDateStamp
            # 【致命修复】：COFF Header 结构如下：
            # +0: Machine (2 bytes)
            # +2: NumberOfSections (2 bytes)
            # +4: TimeDateStamp (4 bytes)  <-- 正确的偏移在这里！
            # PE Signature (4 bytes) + COFF Header Offset (4 bytes)
            timestamp_offset = pe_offset + 4 + 4 
            
            # 4. 读取并显示旧时间戳
            f.seek(timestamp_offset)
            old_timestamp_data = f.read(4)
            old_timestamp = struct.unpack('<I', old_timestamp_data)[0]
            old_time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(old_timestamp))
            print(f"[*] 原始时间戳: 0x{old_timestamp:08X} ({old_time_str} UTC)")

            # 5. 写入新时间戳
            new_timestamp_data = struct.pack('<I', new_timestamp)
            f.seek(timestamp_offset)
            f.write(new_timestamp_data)
            
            new_time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(new_timestamp))
            print(f"[+] 新时间戳已写入: 0x{new_timestamp:08X} ({new_time_str} UTC)")
            return True

    except Exception as e:
        print(f"[!] 修改失败: {e}")
        return False

if __name__ == '__main__':
    print("=== OmniHID PE TimeDateStamp Spoof Tool (Fixed) ===")
    
    if len(sys.argv) < 2:
        print("用法: python spoof_timestamp.py <驱动文件路径> [十六进制时间戳]")
        print(f"示例: python spoof_timestamp.py OmniHID.sys")
        print(f"示例: python spoof_timestamp.py OmniHID.sys 63DB2E84")
        sys.exit(1)

    target_file = sys.argv[1]
    
    # 如果用户提供了自定义时间戳，则解析，否则使用默认值
    if len(sys.argv) >= 3:
        try:
            target_timestamp = int(sys.argv[2], 16)
        except ValueError:
            print("[!] 无效的十六进制时间戳格式")
            sys.exit(1)
    else:
        target_timestamp = DEFAULT_TIMESTAMP
        
    if modify_pe_timestamp(target_file, target_timestamp):
        print("[+] 伪装完成！现在可以进行签名了。")
    else:
        print("[-] 伪装失败！")