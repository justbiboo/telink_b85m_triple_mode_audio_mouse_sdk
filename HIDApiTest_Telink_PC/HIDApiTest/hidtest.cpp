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

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <thread>
#include "hidapi.h"

#pragma comment (lib,"setupapi.lib")//hidapi所需的lib环境，没有此文件会导致链接错误

// Headers needed for sleeping.
#ifdef _WIN32
	#include <windows.h>
#else
	#include <unistd.h>
#endif

hid_device* handle;

void receiveHIDResult() 
{
	unsigned char result[61];
	int ret;
	while (true) {
		ret = hid_read(handle, result, sizeof(result));
		if (ret == 0)
			printf("waiting...\n");
		if (ret < 0)
			printf("Unable to read()\n");

		printf("Data receive:\n   ");
		// Print out the returned buffer.
		for (int i = 0; i < ret; i++)
			printf("%02hhx ", result[i]);
			printf("\n");
		Sleep(500);
	}
}

int main(int argc, char* argv[])
{
	int res;
	unsigned char buf[61];
	int i;

#ifdef WIN32
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
#endif
	
	if (hid_init()) return -1;

	//struct hid_device_info* devs, * cur_dev;
	//devs = hid_enumerate(0xBE57, 0x0201);
	//cur_dev = devs;	
	//while (cur_dev) {
	//	printf("Device Found\n  type: %04hx %04hx\n  path: %s\n  serial_number: %ls", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
	//	printf("\n");
	//	printf("  Manufacturer: %ls\n", cur_dev->manufacturer_string);
	//	printf("  Product:      %ls\n", cur_dev->product_string);
	//	printf("  Release:      %hx\n", cur_dev->release_number);
	//	printf("  Interface:    %d\n",  cur_dev->interface_number);
	//	printf("  usage:    %d\n", cur_dev->usage);
	//	printf("  usage_page:    %d\n", cur_dev->usage_page);
	//	printf("\n");
	//	cur_dev = cur_dev->next;
	//}
	//hid_free_enumeration(devs);
	
	// Open the device using the VID, PID,
	handle = hid_open(0x17EF, 0xA07D, "col03", NULL);
	if (!handle) {
		printf("unable to open device\n");
		system("pause");
 		return 1;
	}

	std::thread receiveThread(receiveHIDResult);
	receiveThread.detach();

	memset(buf,0,sizeof(buf));
	// Send a Feature Report to the device
	buf[0] = 0x24;
	buf[1] = 0x01;
	printf("Data send:\n ");
	//Print out the returned buffer.
	for (i = 0; i < 61; i++)
		printf("%02hhx ", buf[i]);
		printf("\n");
	res = hid_send_feature_report(handle, buf, 61);
	
	//hid_close(handle);

	//Free static HIDAPI objects. 
	//hid_exit();

	Sleep(20000);

#ifdef WIN32
	system("pause");
#endif

	return 0;
}

