/********************************************************************************************************
 * @file    app_config.h
 *
 * @brief   This is the header file for BLE SDK
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

#include  "application/audio/audio_common.h"

/* ================ BIBOO Custom UX: vendor/user module switches ================
 * All custom (non-Telink) dongle logic lives in the vendor/user directory and
 * is dispatched through user_module_manage.c; each feature has exactly ONE
 * switch below (formerly all bundled under a single BIBOO_UX_ENABLE).
 * =================================================================== */
/*BIBOO UX: per-module switches of the vendor/user directory (each module has
 *exactly one switch; the master switch BIBOO_UX_USER_MODULE lives in
 *vendor/user/user_module_manage.h). Every dongle-side switch defined here is
 *automatically undefined (=0) on the mouse project, so shared SDK files
 *(usb.c, usbmouse_i.h, usbkb.c, ...) use them as the dongle/mouse compile
 *discriminator. Search each switch to locate its module + SDK-side spots:
 *   BIBOO_UX_MOUSE_FWD : BLE mouse -> USB forwarding
 *                        (vendor/user/mouse_fwd + blm_host.c/blm_att.c/app.c
 *                         hooks + usb.c fifo + usbmouse_i.h 8-bit X/Y report)
 *   BIBOO_UX_VOICE_DEC : BLE voice stream mSBC decode -> USB mic
 *                        (vendor/user/voice_dec + app_audio.c hooks +
 *                         blm_host.c notify routing + 48MHz clock forcing)
 *   BIBOO_UX_KB_FIX    : keyboard release-report fifo ordering fix (usbkb.c,
 *                        stuck-key when EP busy)
 *   BIBOO_UX_HOST_LINK : USB HID 0x09/0x08 host channel (vendor/user/host_link) */
#ifndef BIBOO_UX_MOUSE_FWD
#define BIBOO_UX_MOUSE_FWD		1
#endif

#ifndef BIBOO_UX_VOICE_DEC
#define BIBOO_UX_VOICE_DEC		1
#endif

#ifndef BIBOO_UX_KB_FIX
#define BIBOO_UX_KB_FIX			1
#endif

/* BLE mouse report format (bytes), must match the mouse firmware config
 * (vendor/8258_dual_mouse or vendor/827x_three_mode_mouse AAA_app_config_*.h):
 *   6 : btn + s16 x + s16 y + wheel  (Telink mouse default)
 *   4 : btn + s8  x + s8  y + wheel  (legacy hamster dongle v5.3 compatible)
 * blm_att.c att_mouse() parses the BLE report accordingly, and the USB HID
 * mouse descriptor (application/app/usbmouse_i.h) keys its X/Y size off it. */
#ifndef MOUSE_DATA_LEN_AAA
#define MOUSE_DATA_LEN_AAA		4
#endif

/* USB test case: 1kHz sine test tone on the dongle USB microphone.
 * While a PC app captures audio (host polls USB ISO IN EP7 every 1ms),
 * EP7 outputs a 1kHz sine wave (16K/16bit mono PCM, one full 1kHz period
 * per 1ms ISOC packet) instead of the BLE mouse voice stream. No BLE mouse
 * needed; use it to verify the dongle -> PC USB audio path.
 *   1 : enable test tone
 *   0 : normal behavior (BLE mouse voice stream)
 */
#ifndef BIBOO_UX_SINE_TEST
#define BIBOO_UX_SINE_TEST		0
#endif

/* debug trace (prints "[DBG] ..." to UART): 0-off, 1-on */
#ifndef BIBOO_UX_DEBUG
#define BIBOO_UX_DEBUG		    1
#endif

/* USB HID host communication channel (dongle <-> PC host tool, e.g.
 * BibooUxTool), implemented by the vendor/user/host_link module:
 *   interface: dedicated non-Boot vendor HID on EP3
 *   downlink : Feature Report 0x09 via SET_REPORT
 *   uplink   : Input Report 0x08 via Interrupt IN
 *   1 : enable (default)
 *   0 : disable */
#ifndef BIBOO_UX_HOST_LINK
#define BIBOO_UX_HOST_LINK		1
#endif



/////////////////// MODULE /////////////////////////////////
#define BLE_HOST_SMP_ENABLE				    1  //Master SMP strongly recommended enabled
#define ACL_CENTRAL_SIMPLE_SDP_ENABLE		1  //simple service discovery


#define BLE_MASTER_OTA_ENABLE			    0  //slave ota test
#define AUDIO_SDM_ENABLE					0  //if using sdm playback, should better disable USB MIC


/* Flash Protection:
 * 1. Flash protection is enabled by default in SDK. User must enable this function on their final mass production application.
 * 2. User should use "Unlock" command in Telink BDT tool for Flash access during development and debugging phase.
 * 3. Flash protection demonstration in SDK is a reference design based on sample code. Considering that user's final application may
 *    different from sample code, for example, user's final firmware size is bigger, or user have a different OTA design, or user need
 *    store more data in some other area of Flash, all these differences imply that Flash protection reference design in SDK can not
 *    be directly used on user's mass production application without any change. User should refer to sample code, understand the
 *    principles and methods, then change and implement a more appropriate mechanism according to their application if needed.
 */
#define APP_FLASH_PROTECTION_ENABLE			1


///////////////////////// DEBUG  Configuration ////////////////////////////////////////////////
#define DEBUG_GPIO_ENABLE							0

/* debug channel select */
#define TLKAPI_DEBUG_CHANNEL_USB   			1 //USB debug
#define TLKAPI_DEBUG_CHANNEL_UART  			2 //GPIO simulate UART debug
#define TLKAPI_DEBUG_CHANNEL				TLKAPI_DEBUG_CHANNEL_UART

#if(TLKAPI_DEBUG_CHANNEL == TLKAPI_DEBUG_CHANNEL_UART)
	#define UART_PRINT_DEBUG_ENABLE               1   //GPIO simulate uart print func
#elif(TLKAPI_DEBUG_CHANNEL == TLKAPI_DEBUG_CHANNEL_USB)
	#define USB_PRINT_DEBUG_ENABLE   			  1   //USB print func
#endif

#define APP_LOG_EN									1
#define APP_FLASH_INIT_LOG_EN						1

/////////////////////// Master Dongle Board Select Configuration ///////////////////////////////
#if(__PROJECT_8258_MASTER_KMA_DONGLE__ )
	#define BOARD_SELECT								BOARD_825X_DONGLE_C1T139A3
#elif(__PROJECT_8278_MASTER_KMA_DONGLE__ )
	#define BOARD_SELECT								BOARD_827X_DONGLE_C1T201A3
#endif



///////////////////////// UI Configuration ////////////////////////////////////////////////////
#define UI_BUTTON_ENABLE							1
#define UI_LED_ENABLE								1
#define UI_AUDIO_ENABLE								1

//////////////////// Audio /////////////////////////////////////
#define MIC_RESOLUTION_BIT		16
#define MIC_SAMPLE_RATE			16000
#define MIC_CHANNEL_COUNT		1
#define	MIC_ENCODER_ENABLE		0

#if (UI_AUDIO_ENABLE)//Audio enable

	/* Dongle Audio MODE:
	 * TL_AUDIO_DONGLE_ADPCM_GATT_TELINK
	 * TL_AUDIO_DONGLE_ADPCM_GATT_GOOGLE
	 * TL_AUDIO_DONGLE_ADPCM_HID
	 * TL_AUDIO_DONGLE_SBC_HID
	 * TL_AUDIO_DONGLE_ADPCM_HID_DONGLE_TO_STB
	 * TL_AUDIO_DONGLE_SBC_HID_DONGLE_TO_STB
	 * TL_AUDIO_DONGLE_MSBC_HID
	 */
	#define TL_AUDIO_MODE  			TL_AUDIO_DONGLE_ADPCM_GATT_TELINK

	#if (TL_AUDIO_MODE == TL_AUDIO_DONGLE_ADPCM_GATT_GOOGLE)
		#define GOOGLE_VERSION_0_4  0
		#define GOOGLE_VERSION_1_0  1
		#define GOOGLE_VOICE_OVER_BLE_SPCE_VERSION  GOOGLE_VERSION_1_0
		#if (GOOGLE_VOICE_OVER_BLE_SPCE_VERSION == GOOGLE_VERSION_1_0)
			#define GOOGLE_VOICE_MODE	0x00	//	0x00: On-request; 0x01:PTT;	0x03:HTT
		#endif
	#endif



#endif

#define APPLICATION_DONGLE							1
#if(APPLICATION_DONGLE)
	#define PA5_FUNC				AS_USB
	#define PA6_FUNC				AS_USB
	#define PA5_INPUT_ENABLE		1
	#define PA6_INPUT_ENABLE		1

	#define	USB_PRINTER_ENABLE 		1
	#define	USB_SPEAKER_ENABLE 		0

	#if ((TL_AUDIO_MODE & TL_AUDIO_MASK_HID_SERVICE_CHANNEL) && (TL_AUDIO_MODE & TL_AUDIO_MASK_DONGLE_TO_STB))//HIDdongle to STB
	#define	USB_MIC_ENABLE 			0
	#define AUDIO_HOGP				1	//Audio HID Over GATT Profile
	#else
	#define AUDIO_HOGP				0	//Audio HID Over GATT Profile
	#define	USB_MIC_ENABLE 			1
	#endif

	#define	USB_MOUSE_ENABLE 		1
	#define	USB_KEYBOARD_ENABLE 	1
	#define	USB_SOMATIC_ENABLE      0   //  when USB_SOMATIC_ENABLE, USB_EDP_PRINTER_OUT disable
	#define USB_CUSTOM_HID_REPORT	1

	#define USB_CDC_ENABLE			0
#endif



#if ((TL_AUDIO_MODE & TL_AUDIO_MASK_HID_SERVICE_CHANNEL) && (TL_AUDIO_MODE & TL_AUDIO_MASK_DONGLE_TO_STB))//HIDdongle to STB
//////////// product  Information  //////////////////////////////
#define ID_VENDOR				0x2657			// for report
#define ID_PRODUCT_BASE			0x5862
#define STRING_VENDOR			L"Telink"
#define STRING_PRODUCT			L"BLE Remote KMA Dongle"
#define STRING_SERIAL			L"TLSR8278"
#else
//////////// product  Information  //////////////////////////////
#define ID_VENDOR				0x2657			// for report
#define ID_PRODUCT_BASE			0x5862
#define STRING_VENDOR			L"Telink"
#define STRING_PRODUCT			L"BLE Remote KMA Dongle"
#define STRING_SERIAL			L"TLSR8278"
#endif








///////////////////////// System Clock  Configuration /////////////////////////////////////////
/*BIBOO: the custom mSBC voice decode chain needs SBC-class CPU headroom (the same rule the
 * SDK itself applies to its SBC/MSBC audio modes), so force 48MHz when BIBOO_UX_VOICE_DEC */
#if ((TL_AUDIO_MODE & (TL_AUDIO_MASK_SBC_MODE|TL_AUDIO_MASK_MSBC_MODE)) || (BIBOO_UX_VOICE_DEC))
#define CLOCK_SYS_CLOCK_HZ  								48000000
#else
#define CLOCK_SYS_CLOCK_HZ  								32000000
#endif


/////////////////// watchdog  //////////////////////////////
#define MODULE_WATCHDOG_ENABLE		0
#define WATCHDOG_INIT_TIMEOUT		500  //ms





/////////////////////////////////////// PRINT DEBUG INFO ///////////////////////////////////////
#if (UART_PRINT_DEBUG_ENABLE)
	#define DEBUG_INFO_TX_PIN           					GPIO_PB5
	#define PULL_WAKEUP_SRC_PB5         					PM_PIN_PULLUP_10K
	#define PB5_OUTPUT_ENABLE         						1
	#define PB5_DATA_OUT                                    1 //must
#endif



/////////////////// set default   ////////////////

#include "vendor/common/default_config.h"
