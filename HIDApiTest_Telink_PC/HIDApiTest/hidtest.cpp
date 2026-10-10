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

// ---------------- Telink BLE dongle config ----------------
// BLE dongle (tc_ble_single_sdk / vendor ble_master_kma_dongle):
// VID 0x248A, PID 0xCA10 (usbdesc.c: #define ID_PRODUCT 0xca10, hard-coded)
// No dedicated vendor interface: the vendor data lives INSIDE the MOUSE
// interface (IF4, top-level usage = Generic Desktop / Mouse 0x01:0x02).
// Report ID 0x05 (usage page 0xFF00): Feature only, 7 bytes payload.
// BOTH directions use EP0 control transfers (the mouse report descriptor is
// NOT touched -- adding an Input item to the 0x05 collection makes Windows
// fail to start the mouse interface MI_04 with CM_PROB_FAILED_START):
//   downlink: hid_send_feature_report -> SET_REPORT(Feature,0x05), length 8
//             [0]=report id, [1..7]=payload; the wire data stage carries all
//             8 bytes (report id included) -- the dongle skips byte 0
//   uplink  : hid_get_feature_report  -> GET_REPORT(Feature,0x05), returns
//             [0]=report id + the last payload written by SET_REPORT
#define DONGLE_VID         0x248A
#define DONGLE_PID         0xCA10
// mouse interface top-level usage (hidapi reports the top-level collection only)
#define MOUSE_USAGE_PAGE   0x0001
#define MOUSE_USAGE        0x0002
#define SPP_REPORT_ID      0x05
#define SPP_REPORT_SIZE    8

int main(int argc, char* argv[])
{
	int res;

#ifdef WIN32
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
#endif
	
	if (hid_init()) return -1;

	// 1. enumerate all dongle HID interfaces (printer/mic/kb/mouse) and collect
	//    candidates on the mouse interface (IF4). Report ID 0x05 (usage page
	//    0xFF00) belongs to its OWN top-level collection with usage 0x01:0x00 --
	//    on IF4 that is the first such collection (col04; col05 is the OTA /
	//    report 0x07 collection). Windows HIDClass validates the report id
	//    against the opened top-level collection, so col04 must be opened first;
	//    the plain mouse collection (0x01:0x02, col01) is only a fallback.
	struct hid_device_info* devs = hid_enumerate(DONGLE_VID, DONGLE_PID);
	struct hid_device_info* cur_dev = devs;
	char cand_vendor[4][512];   // usage 0x01:0x00 -> col04 (report 0x05), col05 (0x07)
	char cand_mouse[2][512];    // usage 0x01:0x02 -> col01
	int n_vendor = 0, n_mouse = 0;

	printf("---- enumerate 0x%04X:0x%04X ----\n", DONGLE_VID, DONGLE_PID);
	while (cur_dev) {
		printf("path: %s\n", cur_dev->path);
		printf("  usage_page=0x%04X usage=0x%04X interface=%d\n",
			cur_dev->usage_page, cur_dev->usage, cur_dev->interface_number);
		if (cur_dev->usage_page == MOUSE_USAGE_PAGE) {
			char (*dst)[512] = NULL;
			int* cnt = NULL;
			if (cur_dev->usage == 0x0000) { dst = cand_vendor; cnt = &n_vendor; }
			else if (cur_dev->usage == MOUSE_USAGE) { dst = cand_mouse; cnt = &n_mouse; }
			if (dst) {
				size_t n = strlen(cur_dev->path);
				if (n >= 512) n = 511;
				memcpy(dst[*cnt], cur_dev->path, n);
				dst[*cnt][n] = 0;
				(*cnt)++;
			}
		}
		cur_dev = cur_dev->next;
	}
	hid_free_enumeration(devs);

	if (n_vendor == 0 && n_mouse == 0) {
		printf("dongle HID collections not found, plug in dongle first\n");
		system("pause");
		return 1;
	}

	// 2. try candidates in order: vendor col04 (report 0x05) -> vendor col05 ->
	//    mouse col01. Each opened handle is probed with GET_REPORT(Feature,0x05);
	//    the first collection that accepts it wins.
	//    NOTE: this modified hidapi opens paths with CreateFileA (ANSI); path
	//    strings from hid_enumerate() are char*, hence the (LPCTSTR) cast.
	handle = NULL;
	char used_path[512];
	used_path[0] = 0;
	{
		bool done = false;
		for (int list = 0; list < 2 && !done; list++) {
			char (*arr)[512] = list ? cand_mouse : cand_vendor;
			int cnt = list ? n_mouse : n_vendor;
			for (int c = 0; c < cnt && !done; c++) {
				printf("try open: %s\n", arr[c]);
				handle = hid_open_path((LPCTSTR)arr[c]);
				if (!handle) {
					printf("  open FAILED, GetLastError=%lu\n", GetLastError());
					HANDLE hTest = CreateFileA(arr[c], GENERIC_READ | GENERIC_WRITE,
						FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
						FILE_FLAG_OVERLAPPED, NULL);
					if (hTest == INVALID_HANDLE_VALUE)
						printf("  direct CreateFileA also FAILED, GetLastError=%lu\n", GetLastError());
					else {
						printf("  direct CreateFileA delivered a handle -- hidapi internal step failed\n");
						CloseHandle(hTest);
					}
					continue;
				}
				{
					unsigned char probe[SPP_REPORT_SIZE];
					memset(probe, 0, sizeof(probe));
					probe[0] = SPP_REPORT_ID;
					res = hid_get_feature_report(handle, probe, SPP_REPORT_SIZE);
				}
				if (res > 0) {
					size_t n = strlen(arr[c]);
					if (n >= sizeof(used_path)) n = sizeof(used_path) - 1;
					memcpy(used_path, arr[c], n);
					used_path[n] = 0;
					printf("  OK: feature report 0x05 is accessible on this collection\n");
					done = true;
				} else {
					printf("  opened, but report 0x05 rejected here -- trying next\n");
					hid_close(handle);
					handle = NULL;
				}
			}
		}
	}

	if (!handle) {
		printf("no HID collection accepted feature report 0x05\n");
		system("pause");
		return 1;
	}
	printf("using interface: %s\n", used_path);

	// 3. send a Feature Report: [0]=Report ID 0x05, [1..7]=payload (8 bytes total)
	//    dongle side: usb.c usb_handle_out_class_intf_req()
	//                 -> SetReport -> HID_REPORT_CUSTOM case (the SDK enum maps
	//                    the standard Feature type 3 to HID_REPORT_CUSTOM)
	//                 -> stores the 7 payload bytes into bibo_feat_rx_buf[]
	unsigned char buf[SPP_REPORT_SIZE];
	memset(buf, 0, sizeof(buf));
	buf[0] = SPP_REPORT_ID;   // report id, must match dongle mouse_report_desc
	buf[1] = 0xA1;            // test payload
	buf[2] = 0xB2;
	printf("Data send (report id=0x%02X, %d bytes):\n ", buf[0], SPP_REPORT_SIZE);
	for (int i = 0; i < SPP_REPORT_SIZE; i++)
		printf("%02hhx ", buf[i]);
	printf("\n");
	res = hid_send_feature_report(handle, buf, SPP_REPORT_SIZE);
	printf("hid_send_feature_report ret=%d  (-1 = failed)\n", res);

	// 4. read back: GET_REPORT(Feature id 0x05) -> dongle returns the stored
	//    payload (echo). Expect rbuf[1..7] == buf[1..7].
	{
		unsigned char rbuf[SPP_REPORT_SIZE];
		memset(rbuf, 0, sizeof(rbuf));
		rbuf[0] = SPP_REPORT_ID;
		res = hid_get_feature_report(handle, rbuf, SPP_REPORT_SIZE);
		printf("hid_get_feature_report ret=%d:\n  ", res);
		for (int i = 0; i < SPP_REPORT_SIZE; i++)
			printf("%02hhx ", rbuf[i]);
		printf("\n");
		if (res >= 2 && memcmp(&buf[1], &rbuf[1], 7) == 0)
			printf("ECHO OK: dongle returned the same 7 bytes\n");
		else
			printf("ECHO MISMATCH (check dongle firmware / report id)\n");
	}

	//hid_close(handle);

	//Free static HIDAPI objects. 
	//hid_exit();

	Sleep(20000);

#ifdef WIN32
	system("pause");
#endif

	return 0;
}

