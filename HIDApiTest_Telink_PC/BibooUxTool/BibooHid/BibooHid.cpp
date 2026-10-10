// BibooHid.cpp : dongle 0x05 通道包装层
// 枚举/探测逻辑移植自已验证的 HIDApiTest/hidtest.cpp，去除控制台输出，改为错误记录。

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "BibooHid.h"
#include "hidapi.h"

#define DONGLE_VID         0x248A
#define DONGLE_PID         0xCA10
// 鼠标接口顶层 usage（hidapi 只报告顶层 collection）
#define MOUSE_USAGE_PAGE   0x0001
#define MOUSE_USAGE        0x0002
#define SPP_REPORT_ID      0x05
#define SPP_REPORT_SIZE    8   // [report id] + 7 字节 payload

static char g_last_err[256]   = "no error";
static char g_opened_path[512] = "";

static void set_err(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(g_last_err, sizeof(g_last_err) - 1, fmt, ap);
	g_last_err[sizeof(g_last_err) - 1] = 0;
	va_end(ap);
}

extern "C" BIBOO_API void* bh_open_default(void)
{
	g_last_err[0] = 0;
	g_opened_path[0] = 0;

	// 1. 枚举 dongle 的全部 HID collection（printer/mic/kb/mouse），
	//    收集鼠标接口(IF4)上的候选：usage 0x01:0x00 -> col04(report 0x05)/col05(0x07)，
	//    usage 0x01:0x02 -> col01（鼠标集合，仅作兜底）。
	//    Windows HIDClass 会按打开的顶层 collection 校验 report id，
	//    因此必须先打开 col04 才能访问 report 0x05。
	struct hid_device_info* devs = hid_enumerate(DONGLE_VID, DONGLE_PID);
	if (!devs) {
		set_err("hid_enumerate: dongle %04X:%04X not found", DONGLE_VID, DONGLE_PID);
		return NULL;
	}

	char cand_vendor[4][512];   // usage 0x01:0x00 -> col04 (report 0x05), col05 (0x07)
	char cand_mouse[2][512];    // usage 0x01:0x02 -> col01
	int n_vendor = 0, n_mouse = 0;

	struct hid_device_info* cur = devs;
	while (cur) {
		if (cur->usage_page == MOUSE_USAGE_PAGE) {
			char (*dst)[512] = NULL;
			int* cnt = NULL;
			int  cap = 0;
			if (cur->usage == 0x0000)      { dst = cand_vendor; cnt = &n_vendor; cap = 4; }
			else if (cur->usage == MOUSE_USAGE) { dst = cand_mouse;  cnt = &n_mouse;  cap = 2; }
			if (dst && *cnt < cap) {
				size_t n = strlen(cur->path);
				if (n >= 512) n = 511;
				memcpy(dst[*cnt], cur->path, n);
				dst[*cnt][n] = 0;
				(*cnt)++;
			}
		}
		cur = cur->next;
	}
	hid_free_enumeration(devs);

	if (n_vendor == 0 && n_mouse == 0) {
		set_err("dongle found but no usable HID collection (usage 0x01:0x00 / 0x01:0x02)");
		return NULL;
	}

	// 2. 按顺序尝试：vendor col04 -> col05 -> mouse col01。
	//    每个句柄用 GET_REPORT(Feature,0x05) 探测，第一个接受的 collection 胜出。
	//    注：魔改版 hidapi 用 CreateFileA 打开路径（char*）；
	//    本 DLL 以 MultiByte 编译，LPCTSTR == const char*，无需强转。
	for (int pass = 0; pass < 2; pass++) {
		char (*arr)[512] = pass ? cand_mouse : cand_vendor;
		int cnt = pass ? n_mouse : n_vendor;
		for (int i = 0; i < cnt; i++) {
			hid_device* h = hid_open_path(arr[i]);
			if (!h)
				continue;

			unsigned char probe[SPP_REPORT_SIZE];
			memset(probe, 0, sizeof(probe));
			probe[0] = SPP_REPORT_ID;
			int res = hid_get_feature_report(h, probe, SPP_REPORT_SIZE);
			if (res > 0) {
				size_t n = strlen(arr[i]);
				if (n >= sizeof(g_opened_path)) n = sizeof(g_opened_path) - 1;
				memcpy(g_opened_path, arr[i], n);
				g_opened_path[n] = 0;
				return (void*)h;
			}
			hid_close(h);
		}
	}

	set_err("no HID collection accepted feature report 0x05");
	return NULL;
}

extern "C" BIBOO_API void bh_close(void* h)
{
	if (h)
		hid_close((hid_device*)h);
	g_opened_path[0] = 0;
}

extern "C" BIBOO_API int bh_send(void* h, const unsigned char* payload, int payload_len)
{
	if (!h)                    { set_err("bh_send: not connected");          return -1; }
	if (!payload || payload_len <= 0) { set_err("bh_send: bad payload");      return -1; }

	unsigned char buf[SPP_REPORT_SIZE];
	memset(buf, 0, sizeof(buf));
	buf[0] = SPP_REPORT_ID;
	int n = payload_len > 7 ? 7 : payload_len;
	memcpy(&buf[1], payload, n);

	int res = hid_send_feature_report((hid_device*)h, buf, SPP_REPORT_SIZE);
	if (res < 0) {
		const wchar_t* e = hid_error((hid_device*)h);
		set_err("hid_send_feature_report failed (%ls)", e ? e : L"");
		return -1;
	}
	return 0;
}

extern "C" BIBOO_API int bh_read(void* h, unsigned char* out_payload, int buf_len)
{
	if (!h)            { set_err("bh_read: not connected"); return -1; }
	if (!out_payload || buf_len < 7) { set_err("bh_read: buffer < 7 bytes"); return -1; }

	unsigned char rbuf[SPP_REPORT_SIZE];
	memset(rbuf, 0, sizeof(rbuf));
	rbuf[0] = SPP_REPORT_ID;

	int res = hid_get_feature_report((hid_device*)h, rbuf, SPP_REPORT_SIZE);
	// 返回值 = bytes_returned + 1（含 report id）；正常为 9，8 即 7 字节 payload 齐全
	if (res < SPP_REPORT_SIZE) {
		const wchar_t* e = hid_error((hid_device*)h);
		set_err("hid_get_feature_report ret=%d (%ls)", res, e ? e : L"");
		return -1;
	}

	memcpy(out_payload, &rbuf[1], 7);
	return 7;
}

extern "C" BIBOO_API int bh_last_error(char* buf, int buf_len)
{
	if (!buf || buf_len <= 0) return 0;
	int n = (int)strlen(g_last_err);
	if (n > buf_len - 1) n = buf_len - 1;
	memcpy(buf, g_last_err, n);
	buf[n] = 0;
	return n;
}

extern "C" BIBOO_API const char* bh_opened_path(void)
{
	return g_opened_path;
}
