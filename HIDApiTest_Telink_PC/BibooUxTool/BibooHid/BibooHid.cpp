// BibooHid.cpp : dongle 0x09/0x08 通道包装层
// 枚举/探测逻辑移植自已验证的 HIDApiTest/hidtest.cpp，去除控制台输出，改为错误记录。

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "BibooHid.h"
#include "hidapi.h"

#define DONGLE_VID                 0x2657
#define DONGLE_PID                 0x5862
#define HLINK_USAGE_PAGE           0xFF00
#define HLINK_USAGE                0x0001
#define HLINK_FEATURE_REPORT_ID    0x09
#define HLINK_INPUT_REPORT_ID      0x08
#define HLINK_REPORT_SIZE          8

typedef struct {
	hid_device* device;
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
	unsigned char buf[HLINK_REPORT_SIZE];
	int n;
	int res;

	if (!link) { set_err("bh_send: not connected"); return -1; }
	if (!payload || payload_len <= 0) { set_err("bh_send: bad payload"); return -1; }

	memset(buf, 0, sizeof(buf));
	buf[0] = HLINK_FEATURE_REPORT_ID;
	n = payload_len > 7 ? 7 : payload_len;
	memcpy(&buf[1], payload, n);
	res = hid_send_feature_report(link->device, buf, sizeof(buf));
	if (res < 0) {
		const wchar_t* e = hid_error(link->device);
		set_err("hid_send_feature_report failed (%ls)", e ? e : L"");
		return -1;
	}
	return 0;
}

static int bh_read_internal(bh_device_t* link, unsigned char* out_payload, int buf_len, int milliseconds)
{
	unsigned char rbuf[HLINK_REPORT_SIZE] = {0};
	int res;

	if (!link) { set_err("bh_read: not connected"); return -1; }
	if (!out_payload || buf_len < 7) { set_err("bh_read: buffer < 7 bytes"); return -1; }

	res = milliseconds < 0 ? hid_read(link->device, rbuf, sizeof(rbuf)) :
		hid_read_timeout(link->device, rbuf, sizeof(rbuf), milliseconds);
	if (res == 0) {
		return 0;
	}
	if (res != HLINK_REPORT_SIZE || rbuf[0] != HLINK_INPUT_REPORT_ID) {
		const wchar_t* e = hid_error(link->device);
		set_err("hid_read ret=%d report=0x%02X (%ls)", res, rbuf[0], e ? e : L"");
		return -1;
	}

	memcpy(out_payload, &rbuf[1], 7);
	return 7;
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
