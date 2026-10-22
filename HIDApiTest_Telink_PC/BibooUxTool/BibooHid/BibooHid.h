#ifndef BIBOO_HID_H__
#define BIBOO_HID_H__

// BibooHid : 为 C# 上位机封装的 dongle HID 通道极简 C 接口
//
// 通道约定：
//   dongle: VID 0x2657 / PID 0x5862
//   下行: Feature Report 0x09，payload 15 字节，经 EP0 SET_REPORT（bh_send）；
//         或 Output Report 0x09，经 EP5 中断 OUT（bh_write，固件 2026-10-22 后
//         枚举 Output member + OUT 端点，~0.5ms/帧 vs ~2.4ms）。
//   上行: Input Report 0x08，payload 15 字节，经 Interrupt IN；bh_read() 阻塞等待

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIBOO_API __declspec(dllexport)

// 枚举并打开独立厂商 HID Interface 的 Collection。
// 成功返回通道句柄，失败返回 NULL（错误信息用 bh_last_error() 获取）。
BIBOO_API void* bh_open_default(void);

// 关闭句柄，NULL 安全
BIBOO_API void bh_close(void* h);

// 发送 payload（最多 15 字节，超出部分截断，不足部分补 0）。
// 内部组包 [0x09][payload0..14] 共 16 字节后调 hid_send_feature_report()。
// 成功返回 0，失败返回 -1。
BIBOO_API int bh_send(void* h, const unsigned char* payload, int payload_len);

// 发送 payload（最多 15 字节）——走中断 OUT 端点（hid_write/WriteFile）。
// 仅当固件报告描述符含 Output 0x09 member 时可用（用 bh_query_out_pipe 判定）；
// 往返成本 ~0.5ms/帧 vs bh_send ~2.4ms/帧。成功返回 0，失败返回 -1。
BIBOO_API int bh_write(void* h, const unsigned char* payload, int payload_len);

// 查询中断 OUT 快车道可用性（open_default() 时用 HID preparsed data 的
// OutputReportByteLength 判定）。返回 1=可用（可调 bh_write），0=不可用
// （旧固件，只能 bh_send 走 EP0），-1=参数错误。
BIBOO_API int bh_query_out_pipe(void* h);

// 阻塞读取 dongle 主动发送的 Input Report 0x08。
// out_payload 缓冲至少 15 字节；成功返回 15，失败返回 -1。
BIBOO_API int bh_read(void* h, unsigned char* out_payload, int buf_len);

// 最多阻塞 milliseconds 毫秒；成功返回 15，超时返回 0，失败返回 -1。
BIBOO_API int bh_read_timeout(void* h, unsigned char* out_payload, int buf_len, int milliseconds);

// 取最近一次错误描述（ASCII），返回写入 buf 的字符数
BIBOO_API int bh_last_error(char* buf, int buf_len);

// 最近一次成功打开的设备路径（ASCII，只读），未连接返回空串
BIBOO_API const char* bh_opened_path(void);

#ifdef __cplusplus
}
#endif

#endif // BIBOO_HID_H__
