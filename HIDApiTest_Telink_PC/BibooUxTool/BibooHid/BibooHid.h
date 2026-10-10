#ifndef BIBOO_HID_H__
#define BIBOO_HID_H__

// BibooHid : 为 C# 上位机封装的 dongle HID 0x05 通道极简 C 接口
// 底层复用 HIDApiTest/hid.c（魔改版 hidapi，CreateFileA + IOCTL_HID_GET_FEATURE）
//
// 通道约定（与 hidtest.cpp 已验证一致）：
//   dongle: VID 0x248A / PID 0xCA10
//   report 0x05 位于鼠标接口(IF4) usage 0x01:0x00 的顶层 collection (col04)，
//   Feature-only，payload 7 字节；双向均走 EP0 控制传输：
//     下行: SET_REPORT(Feature,0x05)  线上 8 字节 [0x05][p0..p6]，dongle 跳过首字节
//     上行: GET_REPORT(Feature,0x05)  dongle 返回最近一次写入的 payload（当前为回显）

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIBOO_API __declspec(dllexport)

// 枚举 dongle 全部 HID collection 并按 col04(0x01:0x00) -> col05 -> 鼠标 col01(0x01:0x02)
// 的顺序尝试打开，每个句柄用 GET_REPORT(Feature,0x05) 探测，第一个成功者胜出。
// 成功返回句柄，失败返回 NULL（错误信息用 bh_last_error() 获取）。
BIBOO_API void* bh_open_default(void);

// 关闭句柄，NULL 安全
BIBOO_API void bh_close(void* h);

// 发送 payload（最多 7 字节，超出部分截断，不足部分补 0）。
// 内部组包 [0x05][payload0..6] 共 8 字节后调 hid_send_feature_report()。
// 成功返回 0，失败返回 -1。
BIBOO_API int bh_send(void* h, const unsigned char* payload, int payload_len);

// 读取 dongle 最近一次 SET_REPORT(0x05) 的内容（固件当前为回显）。
// out_payload 缓冲至少 7 字节；成功返回 7，失败返回 -1。
BIBOO_API int bh_read(void* h, unsigned char* out_payload, int buf_len);

// 取最近一次错误描述（ASCII），返回写入 buf 的字符数
BIBOO_API int bh_last_error(char* buf, int buf_len);

// 最近一次成功打开的设备路径（ASCII，只读），未连接返回空串
BIBOO_API const char* bh_opened_path(void);

#ifdef __cplusplus
}
#endif

#endif // BIBOO_HID_H__
