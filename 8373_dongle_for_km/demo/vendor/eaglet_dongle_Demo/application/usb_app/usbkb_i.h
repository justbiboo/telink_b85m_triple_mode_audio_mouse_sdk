/********************************************************************************************************
 * @file     usbkb_i.h
 *
 * @brief    This is the header file for KMD SDK
 *
 * @author	 KMD GROUP
 * @date         01,2022
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *******************************************************************************************************/

#pragma once

#include "usbkb.h"
/*
#include "../../usbhw.h"
#include "../../usbhw_i.h"
*/


/** HID class report descriptor. This is a special descriptor constructed with values from the
 *  USBIF HID class specification to describe the reports and capabilities of the HID device. This
 *  descriptor is parsed by the host and its contents used to determine what data (and in what encoding)
 *  the device will send, and what it may be sent back from the host. Refer to the HID specification for
 *  more details on HID report descriptors.
 */
static const USB_Descriptor_HIDReport_Datatype_t keyboard_report_desc[] = {
    HID_DESCRIPTOR_KEYBOARD(KEYBOARD_REPORT_KEY_MAX),
};
static const u8 kb_report_desc[] = 
{
	 0x05, 0x01,	 // Usage Pg (Generic Desktop)
	 0x09, 0x06,	 // Usage (Keyboard)
	 0xA1, 0x01,	 // Collection: (Application)
	 0x05, 0x07,	 // Usage Pg (Key Codes)
	 0x19, 0xE0,	 // Usage Min (224)  VK_CTRL:0xe0
	 0x29, 0xE7,	 // Usage Max (231)  VK_RWIN:0xe7
	 0x15, 0x00,	 // Log Min (0)
	 0x25, 0x01,	 // Log Max (1)
	 0x75, 0x01,	 // Report Size (1)   1 bit * 8
	 0x95, 0x08,	 // Report Count (8)
	 0x81, 0x02,	 // Input: (Data, Variable, Absolute)
	 0x95, 0x01,	 // Report Count (1)
	 0x75, 0x08,	 // Report Size (8)
	 0x81, 0x01,	 // Input: (Constant)
	 0x95, 0x05,	//Report Count (5)
	 0x75, 0x01,	//Report Size (1)
	 0x05, 0x08,	//Usage Pg (LEDs )
	 0x19, 0x01,	//Usage Min
	 0x29, 0x05,	//Usage Max
	 0x91, 0x02,	//Output (Data, Variable, Absolute)
	 0x95, 0x01,	//Report Count (1)
	 0x75, 0x03,	//Report Size (3)
	 0x91, 0x01,	//Output (Constant)
	 0x95, 0x06,	 // Report Count (6)
	 0x75, 0x08,	 // Report Size (8)
	 0x15, 0x00,	 // Log Min (0)
	 0x26, 0xF1,0x00,	 // Log Max (241)
	 0x05, 0x07,	 // Usage Pg (Key Codes)
	 0x19, 0x00,	 // Usage Min (0)
	 0x2a, 0xf1,0x00,	 // Usage Max (241)
	 0x81, 0x00,	 // Input: (Data, Array)
	 0xC0,			  // End Collection
 };

static inline u8* usbkb_get_report_desc(void) {
	return (u8*) (kb_report_desc);//todo
	//return (u8*) (keyboard_report_desc);
}

static inline u16 usbkb_get_report_desc_size(void) {
	return sizeof(kb_report_desc);//todo
	//return sizeof(keyboard_report_desc);
}


