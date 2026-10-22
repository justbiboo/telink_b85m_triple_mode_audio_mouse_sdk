// BibooHid.cpp : dongle 0x09/0x08 通道包装层
// 枚举/探测逻辑移植自已验证的 HIDApiTest/hidtest.cpp，去除控制台输出，改为错误记录。

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>			/*GetLastError - hidapi 失败时经常不设错误串，Win32 错误码才是关键线索*/
#include <hidsdi.h>				/*HidD_GetPreparsedData - Output report 能力探测（2026-10-22）*/
#include <hidpi.h>				/*HidP_GetCaps / HIDP_CAPS*/
#include "BibooHid.h"
#include "hidapi.h"

#define DONGLE_VID                 0x2657
#define DONGLE_PID                 0x5862
#define HLINK_USAGE_PAGE           0xFF00
#define HLINK_USAGE                0x0001
#define HLINK_FEATURE_REPORT_ID    0x09
#define HLINK_INPUT_REPORT_ID      0x08
#define HLINK_FEATURE_PAYLOAD_SIZE 15
#define HLINK_INPUT_PAYLOAD_SIZE   15
#define HLINK_FEATURE_REPORT_SIZE  (HLINK_FEATURE_PAYLOAD_SIZE + 1)
#define HLINK_INPUT_REPORT_SIZE    (HLINK_INPUT_PAYLOAD_SIZE + 1)

typedef struct {
	hid_device* device;
	/*1: 报告描述符声明了 Output 0x09 member（新固件，接口有 EP5 OUT pipe），
	 *WriteFile 能走中断 OUT；0: 旧固件，WriteFile 直接失败，只能 EP0 SET_REPORT。
	 *由 open_default() 用 HID preparsed data 的 OutputReportByteLength 判定。*/
	int has_out_pipe;
} bh_device_t;

static char g_last_err[256] = "no error";
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
	char host_link_path[512] = {0};
	struct hid_device_info* devs;
	struct hid_device_info* cur;
	bh_device_t* link;

	g_last_err[0] = 0;
	g_opened_path[0] = 0;
	devs = hid_enumerate(DONGLE_VID, DONGLE_PID);
	if (!devs) {
		set_err("hid_enumerate: dongle %04X:%04X not found", DONGLE_VID, DONGLE_PID);
		return NULL;
	}

	for (cur = devs; cur; cur = cur->next) {
		if (cur->usage_page == HLINK_USAGE_PAGE && cur->usage == HLINK_USAGE && !host_link_path[0]) {
			size_t n = strlen(cur->path);
			if (n >= sizeof(host_link_path)) n = sizeof(host_link_path) - 1;
			memcpy(host_link_path, cur->path, n);
			host_link_path[n] = 0;
		}
	}
	hid_free_enumeration(devs);

	if (!host_link_path[0]) {
		set_err("required host link HID collection missing");
		return NULL;
	}

	link = (bh_device_t*)calloc(1, sizeof(*link));
	if (!link) {
		set_err("out of memory");
		return NULL;
	}
	link->device = hid_open_path(host_link_path);
	if (!link->device) {
		set_err("open host link collection failed");
		free(link);
		return NULL;
	}
	hid_set_nonblocking(link->device, 0);
	memcpy(g_opened_path, host_link_path, strlen(host_link_path) + 1);

	/*2026-10-22: 探测 Output report 能力。对同一设备路径再开一个查询句柄
	 *（hidapi 内部句柄不可达），OutputReportByteLength = 1+15=16 表示描述符
	 *有 Output 0x09 member（Windows 已建 OUT pipe），= 1（只有 report id 无
	 *output member）或 0 表示旧固件。探测失败不阻塞打开，按 0 处理（保守走
	 *EP0，兼容性优先）。不能靠错误码猜测：hid.c 的 hid_write 在无
	 *write_handle 时不 register_error，hid_error() 返回未初始化数据，实测
	 *连 %ls 格式化都被打断（17:16 日志 "hid_write failed ("）。*/
	link->has_out_pipe = 0;
	{
		HANDLE qh = CreateFileA(host_link_path, GENERIC_READ | GENERIC_WRITE,
								FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
								OPEN_EXISTING, 0, NULL);
		if (qh != INVALID_HANDLE_VALUE) {
			PHIDP_PREPARSED_DATA ppd = NULL;
			HIDP_CAPS caps;
			memset(&caps, 0, sizeof(caps));
			if (HidD_GetPreparsedData(qh, &ppd) && ppd) {
				if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS) {
					link->has_out_pipe =
						(caps.OutputReportByteLength >= HLINK_FEATURE_REPORT_SIZE) ? 1 : 0;
				}
				HidD_FreePreparsedData(ppd);
			}
			CloseHandle(qh);
		}
	}
	return link;
}

extern "C" BIBOO_API void bh_close(void* h)
{
	bh_device_t* link = (bh_device_t*)h;
	if (link) {
		if (link->device) hid_close(link->device);
		free(link);
	}
	g_opened_path[0] = 0;
}

extern "C" BIBOO_API int bh_send(void* h, const unsigned char* payload, int payload_len)
{
	bh_device_t* link = (bh_device_t*)h;
	unsigned char buf[HLINK_FEATURE_REPORT_SIZE];
	int n;
	int res;

	if (!link) { set_err("bh_send: not connected"); return -1; }
	if (!payload || payload_len <= 0) { set_err("bh_send: bad payload"); return -1; }

	memset(buf, 0, sizeof(buf));
	buf[0] = HLINK_FEATURE_REPORT_ID;
	n = payload_len > HLINK_FEATURE_PAYLOAD_SIZE ? HLINK_FEATURE_PAYLOAD_SIZE : payload_len;
	memcpy(&buf[1], payload, n);
	res = hid_send_feature_report(link->device, buf, sizeof(buf));
	if (res < 0) {
		/*hidapi 的 per-device 错误串在 HidD_SetFeature 失败时经常是空的，
		 *必须立刻抓线程的 Win32 GetLastError（先取后格式化，避免被其他
		 *调用覆盖）。常见码：121=ERROR_SEM_TIMEOUT（控制传输超时 -> EP0
		 *被卡死的特征，设备还在总线上）；1167=ERROR_DEVICE_NOT_CONNECTED
		 *（设备已消失/被重新枚举）；31=ERROR_GEN_FAILURE。*/
		const wchar_t* e = hid_error(link->device);
		DWORD gle = GetLastError();
		set_err("hid_send_feature_report failed (%ls, gle=%lu)", e ? e : L"", (unsigned long)gle);
		return -1;
	}
	return 0;
}

extern "C" BIBOO_API int bh_write(void* h, const unsigned char* payload, int payload_len)
{
	bh_device_t* link = (bh_device_t*)h;
	unsigned char buf[HLINK_FEATURE_REPORT_SIZE];
	int n;
	int res;

	if (!link) { set_err("bh_write: not connected"); return -1; }
	if (!payload || payload_len <= 0) { set_err("bh_write: bad payload"); return -1; }

	memset(buf, 0, sizeof(buf));
	buf[0] = HLINK_FEATURE_REPORT_ID;
	n = payload_len > HLINK_FEATURE_PAYLOAD_SIZE ? HLINK_FEATURE_PAYLOAD_SIZE : payload_len;
	memcpy(&buf[1], payload, n);
	res = hid_write(link->device, buf, sizeof(buf));
	if (res < 0) {
		/*旧固件无 Output 0x09 member -> 无 OUT pipe，hid.c 的 hid_write 在无
		 *write_handle 时不 register_error，hid_error() 返回未初始化数据（实测
		 *把 %ls 格式化都打断了，17:16 日志 "hid_write failed ("）——因此这里
		 *只打 Win32 错误码，不碰 hid_error()。路径选择由 open_default() 的
		 *preparsed data 探测决定（bh_query_out_pipe），不靠错误码猜测。*/
		DWORD gle = GetLastError();
		set_err("hid_write failed (gle=%lu)", (unsigned long)gle);
		return -1;
	}
	return 0;
}

extern "C" BIBOO_API int bh_query_out_pipe(void* h)
{
	bh_device_t* link = (bh_device_t*)h;
	if (!link) { set_err("bh_query_out_pipe: not connected"); return -1; }
	return link->has_out_pipe;
}

static int bh_read_internal(bh_device_t* link, unsigned char* out_payload, int buf_len, int milliseconds)
{
	unsigned char rbuf[HLINK_INPUT_REPORT_SIZE] = {0};
	int res;

	if (!link) { set_err("bh_read: not connected"); return -1; }
	if (!out_payload || buf_len < HLINK_INPUT_PAYLOAD_SIZE) { set_err("bh_read: buffer < 15 bytes"); return -1; }

	res = milliseconds < 0 ? hid_read(link->device, rbuf, sizeof(rbuf)) :
		hid_read_timeout(link->device, rbuf, sizeof(rbuf), milliseconds);
	if (res == 0) {
		return 0;
	}
	if (res != HLINK_INPUT_REPORT_SIZE || rbuf[0] != HLINK_INPUT_REPORT_ID) {
		const wchar_t* e = hid_error(link->device);
		set_err("hid_read ret=%d report=0x%02X (%ls)", res, rbuf[0], e ? e : L"");
		return -1;
	}

	memcpy(out_payload, &rbuf[1], HLINK_INPUT_PAYLOAD_SIZE);
	return HLINK_INPUT_PAYLOAD_SIZE;
}

extern "C" BIBOO_API int bh_read(void* h, unsigned char* out_payload, int buf_len)
{
	return bh_read_internal((bh_device_t*)h, out_payload, buf_len, -1);
}

extern "C" BIBOO_API int bh_read_timeout(void* h, unsigned char* out_payload, int buf_len, int milliseconds)
{
	return bh_read_internal((bh_device_t*)h, out_payload, buf_len, milliseconds);
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
