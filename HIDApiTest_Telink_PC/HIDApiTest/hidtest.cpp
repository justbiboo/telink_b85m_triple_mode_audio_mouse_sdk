/*******************************************************
 Windows HID simplification

 Alan Ott
 Signal 11 Software

 8/22/2009

 Copyright 2009
 
 This contents of this file may be used by anyone
 for any reason without any conditions and may be
 used as a starting point for your own applications
 which use HIDAPI.
********************************************************/

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include "hidapi.h"

#pragma comment (lib,"setupapi.lib")//hidapi�����lib������û�д��ļ��ᵼ�����Ӵ���

// Headers needed for sleeping.
#ifdef _WIN32
	#include <windows.h>
#else
	#include <unistd.h>
#endif

hid_device* handle;

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

int main(int argc, char* argv[])
{
	int res;

#ifdef WIN32
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
#endif
	
	if (hid_init()) return -1;

	struct hid_device_info* devs = hid_enumerate(DONGLE_VID, DONGLE_PID);
	struct hid_device_info* cur_dev = devs;
	char host_link_path[512] = {0};

	if (!devs) {
		printf("dongle HID collections not found, plug in dongle first\n");
		system("pause");
		return 1;
	}

	printf("---- enumerate 0x%04X:0x%04X ----\n", DONGLE_VID, DONGLE_PID);
	while (cur_dev) {
		printf("path: %s\n", cur_dev->path);
		printf("  usage_page=0x%04X usage=0x%04X interface=%d\n",
			cur_dev->usage_page, cur_dev->usage, cur_dev->interface_number);
		if (cur_dev->usage_page == HLINK_USAGE_PAGE && cur_dev->usage == HLINK_USAGE && !host_link_path[0]) {
			size_t n = strlen(cur_dev->path);
			if (n >= sizeof(host_link_path)) n = sizeof(host_link_path) - 1;
			memcpy(host_link_path, cur_dev->path, n);
			host_link_path[n] = 0;
		}
		cur_dev = cur_dev->next;
	}
	hid_free_enumeration(devs);

	if (!host_link_path[0]) {
		printf("host link HID collection not found\n");
		system("pause");
		return 1;
	}

	handle = hid_open_path((LPCTSTR)host_link_path);
	if (!handle) {
		printf("open host link HID collection failed\n");
		system("pause");
		return 1;
	}
	hid_set_nonblocking(handle, 0);
	printf("host link path: %s\n", host_link_path);

	unsigned char buf[HLINK_FEATURE_REPORT_SIZE];
	memset(buf, 0, sizeof(buf));
	buf[0] = HLINK_FEATURE_REPORT_ID;
	buf[1] = 0x01;
	res = hid_send_feature_report(handle, buf, sizeof(buf));
	printf("hid_send_feature_report ret=%d\n", res);
	if (res < 0) {
		printf("feature send failed\n");
		hid_close(handle);
		return 1;
	}

	{
		unsigned char rbuf[HLINK_INPUT_REPORT_SIZE];
		printf("waiting for Input Report 0x%02X ...\n", HLINK_INPUT_REPORT_ID);
		res = hid_read(handle, rbuf, sizeof(rbuf));
		printf("hid_read ret=%d:\n  ", res);
		for (int i = 0; i < HLINK_INPUT_REPORT_SIZE && i < res; i++)
			printf("%02hhx ", rbuf[i]);
		printf("\n");
		if (res == HLINK_INPUT_REPORT_SIZE && rbuf[0] == HLINK_INPUT_REPORT_ID && rbuf[1] == 0x01)
			printf("INPUT REPORT OK\n");
		else
			printf("INPUT REPORT MISMATCH\n");
	}

	hid_close(handle);
	hid_exit();

#ifdef WIN32
	system("pause");
#endif

	return 0;
}

