/********************************************************************************************************
 * @file    usbmouse.h
 *
 * @brief   This is the header file for B85
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
 *
 *******************************************************************************************************/
#pragma once


#include <application/usbstd/HIDClassCommon.h>
#include <application/usbstd/HIDReportData.h>
#include "tl_common.h"
//#include "../common/types.h"
//#include "../common/static_assert.h"
//#include "../common/bit.h"

/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
	extern "C" {
#endif


typedef struct {
	u8 btn;
	s8 x;
	s8 y;
	s8 wheel;
}mouse_data_t;


#define MOUSE_REPORT_DATA_LEN     (sizeof(mouse_data_t))
#define MEDIA_REPORT_DATA_LEN		4
int usbmouse_hid_report(u8 report_id, u8 *data, int cnt);

/*BIBOO UX diag: mouse-forward pipeline counters (host link MSTAT 0x03, see
 *vendor/user/host_link/ux_host_link.c). Defined unconditionally so both the
 *dongle (read via host link) and the mouse builds (read via UART) can use
 *them without extra switch macros.*/
extern volatile u32 usbmouse_dbg_ring_in;		/*frames written into the ring buffer*/
extern volatile u32 usbmouse_dbg_ring_drop;	/*frames lost: ring buffer full*/
extern volatile u32 usbmouse_dbg_tx_ok;			/*frames handed to the USB EP*/
extern volatile u32 usbmouse_dbg_epq;			/*EP busy: queued into usb_fifo*/
extern volatile u32 usbmouse_dbg_fifo_drop;		/*frames lost: usb_fifo full*/


/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
	}
#endif
