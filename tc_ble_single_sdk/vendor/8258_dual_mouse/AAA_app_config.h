/********************************************************************************************************
 * @file     AAA_app_config.h
 *
 * @brief    for TLSR chips
 *
 * @author	 telink
 * @date     Sep. 30, 2010
 *
 * @par      Copyright (c) 2010, Telink Semiconductor (Shanghai) Co., Ltd.
 *           All rights reserved.
 *
 *			 The information contained herein is confidential and proprietary property of Telink
 * 		     Semiconductor (Shanghai) Co., Ltd. and is available under the terms
 *			 of Commercial License Agreement between Telink Semiconductor (Shanghai)
 *			 Co., Ltd. and the licensee in separate contract or the terms described here-in.
 *           This heading MUST NOT be removed from this file.
 *
 * 			 Licensees are granted free, non-transferable use of the information in this
 *			 file under Mutual Non-Disclosure Agreement. NO WARRENTY of ANY KIND is provided.
 *
 *******************************************************************************************************/

#pragma once

/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C" {
#endif
//---------------------clock References-------------------------------------------------

#define CLOCK_SYS_CLOCK_HZ  	48000000

#define APP_SECURITY_ENABLE     1
//---------------------------watchdog References-------------------------------------

#define MODULE_WATCHDOG_ENABLE		0
#define WATCHDOG_INIT_TIMEOUT		500  //ms

//-----------------------------cfg address in flash--------------------------------
#define		CFG_ADR_MAC						0x76000//4 BYTES
#define 	CFG_ADDR_ACCESS_CODE0			0x76006//2 BYTES

#define		CUST_CAP_INFO_ADDR				0x77000
#define		CUST_TP_INFO_ADDR				0x77040
#define		CUST_32KPAD_CAP_INFO_ADDR		0x77080
#define		CUST_CAP_INFO_ADDR_2M			0x77020		//2M MODE
#define 	CUST_TP_INFO_ADDR_2M			0x77060


#define 	MOUSE_DPI_ADDR  			0x78000
#define 	CFG_DEVICE_MODE_ADDR   		0x79000
#define 	CFG_MAST_ADDR   		    0x7A000


//-----------------------------DEEP SAVE FLG -----------------------------
#define USED_DEEP_ANA_REG1                   DEEP_ANA_REG1 //u8,can save 8 bit info when deep
#define	PAIR_ANA_FLG					    0X01// BIT 0
#define CLEAR_PAIR_ANA_FLAG                 0XFE

#define FUN_MODE_ANA_FLG	                0X06 //BIT 1,BIT2
#define CLEAR_FUN_MODE_ANA_FLAG             0xF9

//---------------------------security References----------------------------------------

#define BLE_REMOTE_SECURITY_ENABLE      	1
#define RF_TX_FIFO_ALLOW_NUM     9//14-5 audio -1 mouse =8 
//-------------------------------24g-----------------------

enum
{
    RF_1M_BLE_MODE = 0,
    RF_2M_2P4G_MODE = 1,
    USB_MODE = 2,
};

//---------------------project---------------------------

#define Telink_QFN32_PRJ            0

#define PRJ_NAME   Telink_QFN32_PRJ


//----------------------for debug--------------
#define APP_FLASH_PROTECTION_ENABLE					1
#define APP_FLASH_PROT_LOG_EN						1


//#define DEBUG_MODE				0 //1:Enable the software uart printing function
#define APP_LOG_EN							1
#define APP_SMP_LOG_EN						0
#define APP_KEY_LOG_EN						1
#define APP_CONTR_EVENT_LOG_EN				1  //controller event log
#define APP_HOST_EVENT_LOG_EN				1  //host event log
#define APP_OTA_LOG_EN						1
#define APP_FLASH_INIT_LOG_EN				1
#define APP_FLASH_PROT_LOG_EN				1
#define APP_BATT_CHECK_LOG_EN				1

#define UART_PRINT_DEBUG_ENABLE 	1 //not support

#if UART_PRINT_DEBUG_ENABLE
#define PRINT_BAUD_RATE     1000000
#define DEBUG_INFO_TX_PIN   GPIO_PA7 //GPIO_SWS
#define PA7_INPUT_ENABLE    0
#define PA7_OUTPUT_ENABLE   1
#define PA7_FUNC    AS_GPIO

#define my_printf_aaa	 		u_printf
#else
static inline void no_printf(const char *format, ...){    return;}

#define my_printf_aaa     no_printf 


#endif
//-----------------------device information-----------------------
#if (PRJ_NAME==Telink_QFN32_PRJ)
//#include "AAA_app_config_Telink_QFN32.h"
#include "vendor/8258_dual_mouse/AAA_app_config_Telink_QFN32.h"
#endif

/* ================ BIBOO UX: BLE mouse continuous mic stream ================ */
/* BIBOO_UX_MIC_STREAM: after BLE link-up with the dongle, the mouse automatically
 * opens the AMIC and streams REAL microphone audio (16kHz/16bit/mono) as mSBC
 * 57B frames (7.5ms) via GATT notify (my_Data 0xB03E). The dongle decodes them
 * and outputs the sound on USB.
 *   1 : enable automatic wireless mic stream
 *   0 : original SDK behavior (voice key K5 push-to-talk starts/stops the mic)
 *
 * BIBOO_UX_SINE_TEST: DEBUG option of the stream above - the mic samples are
 * replaced by a continuous 1kHz sine tone (end-to-end chain sanity check).
 *   0 : normal (real microphone audio)
 *   1 : 1kHz test tone */
#ifndef BIBOO_UX_MIC_STREAM
#define BIBOO_UX_MIC_STREAM		0
#endif
#ifndef BIBOO_UX_SINE_TEST
#define BIBOO_UX_SINE_TEST		0
#endif

/* ================ BIBOO UX: K4 = keyboard space key ================ */
/* When enabled, the K4 button sends a keyboard HID space key (keycode 0x2C)
 * press/release through the BLE HID keyboard report characteristic
 * (HID_NORMAL_KB_REPORT_INPUT_DP_H). The dongle receives the notify
 * (att_keyboard) and forwards it to the PC on its USB keyboard endpoint.
 * Report format (8 bytes, kb_data_t): {cnt, ctrl_key, keycode[6]}.
 * NOTE: this also DISABLES the K4 long-press multi-device channel switch
 * (muti_device_change in AAA_Public.c): a >=3s space-key hold would
 * otherwise fire it, disconnect the link and reboot onto another channel.
 * Set to 0 to restore the original K4 channel-switch behavior. */
#ifndef BIBOO_UX_K4_SPACE_KEY
#define BIBOO_UX_K4_SPACE_KEY	1
#endif

/* ================ BIBOO UX: K4 push-to-talk mic ================ */
/* When enabled, the K4 button works as a push-to-talk key:
 *   press   -> start mic capture and stream mSBC voice to the dongle
 *              (same start sequence as the original voice key:
 *               audio_mic_param_init + audio_stick + ui_enable_mic(1))
 *   release -> stop the mic (ui_enable_mic(0))
 * The K5 voice-key toggle (press on / press again off) is kept unchanged;
 * both entries can start/stop the mic. The 60s mic_duration safety limit
 * still applies while held. */
#ifndef BIBOO_UX_K4_MIC_PTT
#define BIBOO_UX_K4_MIC_PTT		1
#endif

/* ================ BIBOO UX: K5 = Ctrl+Win push-to-talk ================ */
/* When enabled, the K5 (voice) button becomes a push-to-talk key (like K4):
 *   press   -> BLE keyboard report with Left-Ctrl + Left-Win modifiers
 *              (ctrl_key byte 0x09, no normal keycode) AND start the mic
 *              (audio_mic_param_init + audio_stick + ui_enable_mic(1))
 *   release -> stop the mic, then (in strict order, so the PC never sees
 *              Ctrl+Win+Enter): release modifiers -> Enter tap (0x28)
 * The original K5 toggle behavior (press on / press again off, in
 * voice_key_check) is bypassed in BLE mode; it is kept for 2.4G mode.
 * The 60s mic_duration safety limit still applies while held. */
#ifndef BIBOO_UX_K5_CTRL_WIN_MIC
#define BIBOO_UX_K5_CTRL_WIN_MIC	1
#endif


//--------------Debug gpio-------------------

#if DEBUG_GPIO_AAA

	#define PB2_INPUT_ENABLE	0
	#define PB2_OUTPUT_ENABLE	1
	#define PB2_DATA_OUT		0
	#define PIN_DEBUG_RF_TX_LEVEL(x)   	gpio_write(GPIO_PB2,x)
											
	#define PB3_INPUT_ENABLE	0
	#define PB3_OUTPUT_ENABLE	1
	#define PB3_DATA_OUT	0
	#define PIN_DEBUG_RF_RX_LEVEL(x)   	gpio_write(GPIO_PB3,x)
									
	#define PB4_INPUT_ENABLE	0
	#define PB4_OUTPUT_ENABLE	1
	#define PB4_DATA_OUT	0
	#define PIN_DEBUG_RF_RX_CRC_OK_LEVEL(x)   	gpio_write(GPIO_PB4,x)

	#define PB5_INPUT_ENABLE	0
	#define PB5_OUTPUT_ENABLE	1
	#define PB5_DATA_OUT	0
	#define PIN_DEBUG_RF_RX_DATA_OK_LEVEL(x)    	gpio_write(GPIO_PB5,x)

	#define PA0_INPUT_ENABLE	0
	#define PA0_OUTPUT_ENABLE	1
	#define PA0_DATA_OUT		0
	#define PIN_DEBUG_RF_RX_TIMEOUT_LEVEL(x)   	gpio_write(GPIO_PA0,x)

	#define PD7_INPUT_ENABLE	0
	#define PD7_OUTPUT_ENABLE	1
	#define PD7_DATA_OUT		0
	#define PIN_DEBUG_RF_CHN_NEXT_TOGGLE   	gpio_toggle(GPIO_PD7)

	#define PD2_INPUT_ENABLE	0
	#define PD2_OUTPUT_ENABLE	1
	#define PD2_DATA_OUT	    0
	#define PIN_DEBUG_UI_TIME_LEVEL(x)   	gpio_write(GPIO_PD2,x)

#else
	#define PIN_DEBUG_RF_TX_LEVEL(x)
	#define PIN_DEBUG_RF_RX_LEVEL(x)
	#define PIN_DEBUG_RF_RX_CRC_OK_LEVEL(x)
	#define PIN_DEBUG_RF_RX_DATA_OK_LEVEL(x)
	#define PIN_DEBUG_RF_RX_TIMEOUT_LEVEL(x)
	#define PIN_DEBUG_RF_CHN_NEXT_TOGGLE

	#define PIN_DEBUG_UI_TIME_LEVEL(x)



#endif

//-------------------------------------
///////////////////////////////////// ATT  HANDLER define ///////////////////////////////////////
#define HID_KEYBOARD_ATT_ENABLE   1
#define HID_MOUSE_ATT_ENABLE	  1
//#define CONSUMER_FUN_ENABLE_AAA   1



#define REPORT_ID_KEYBOARD_INPUT_AAA        1   //!< Keyboard input report ID
#define REPORT_ID_CONSUME_CONTROL_INPUT_AAA	2   //!< Consumer Control input report ID
#define REPORT_ID_MOUSE_INPUT_AAA           3   //!< Mouse input report ID
#define REPORT_ID_GAMEPAD_INPUT_AAA			4   //!< Gamepad  input report ID
#define REPORT_ID_LED_OUT_AAA               0   //!< LED output report ID
#define REPORT_ID_FEATURE_AAA               0   //!< Feature report ID
#define REPORT_ID_USER_FEATURE_AAA			5
#define REPORT_ID_SYSTEM_INPUT_AAA			6   //!< Gamepad  input report ID
#define REPORT_ID_SPP_INPUT_AAA				7   //!< Gamepad  input report ID

typedef enum

{
    ATT_H_START = 0,

    /*********Gap*************************************************************************************/
    GenericAccess_PS_H, 					//UUID: 2800, 	VALUE: uuid 1800
    GenericAccess_DeviceName_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read | Notify
    GenericAccess_DeviceName_DP_H,			//UUID: 2A00,   VALUE: device name
    GenericAccess_Appearance_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read
    GenericAccess_Appearance_DP_H,			//UUID: 2A01,	VALUE: appearance
    CONN_PARAM_CD_H,						//UUID: 2803, 	VALUE:  			Prop: Read
    CONN_PARAM_DP_H,						//UUID: 2A04,   VALUE: connParameter

    /*****gatt *****************************************************************************************/
    GenericAttribute_PS_H,					//UUID: 2800,	VALUE: uuid 1801
    //GenericAttribute_ServiceChanged_CD_H,	//UUID: 2803,	VALUE:				Prop: Indicate
    //GenericAttribute_ServiceChanged_DP_H,	//UUID: 2A05,	VALUE: service change
    //GenericAttribute_ServiceChanged_CCB_H,	//UUID: 2902,	VALUE: serviceChangeCCC



    /****device information******************************************************************************************/
    DeviceInformation_PS_H,					//UUID: 2800, 	VALUE: uuid 180A
    DeviceInformation_pnpID_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read
    DeviceInformation_pnpID_DP_H,			//UUID: 2A50,	VALUE: PnPtrs


    /*********battery service *************************************************************************************/
    BATT_PS_H,								//UUID: 2800,	VALUE: uuid 180f
    BATT_LEVEL_INPUT_CD_H,					//UUID: 2803,	VALUE:				Prop: Read | Notify
    BATT_LEVEL_INPUT_DP_H,					//UUID: 2A19	VALUE: batVal
    BATT_LEVEL_INPUT_CCB_H, 				//UUID: 2902,	VALUE: batValCCC
    /************Ota**********************************************************************************/
#if (BLE_OTA_SERVER_ENABLE)
	OTA_PS_H,								//UUID: 2800,	VALUE: telink ota service uuid
	OTA_CMD_OUT_CD_H,						//UUID: 2803,	VALUE:				Prop: read | write_without_rsp
	OTA_CMD_OUT_DP_H,						//UUID: telink ota uuid,  VALUE: otaData
	OTA_CMD_INF_CD_H,						//UUID: 2901,	VALUE: otaName
	OTA_CMD_INF_DP_H,
#endif


    /**************Audio********************************************************************************/
#if (BLE_AUDIO_ENABLE)

    AUDIO_PS_H, 							//UUID: 2800,	VALUE: telink audio service uuid

    //mic
    AUDIO_MIC_INPUT_CD_H,					//UUID: 2803,	VALUE:				Prop: Read | Notify
    AUDIO_MIC_INPUT_DP_H,					//UUID: telink mic uuid,  VALUE: micData
    AUDIO_MIC_INPUT_CCB_H,					//UUID: 2A19	VALUE: micDataCCC
    //AUDIO_MIC_INPUT_DESC_H,					//UUID: 2901,	VALUE: micName

    //speaker
    AUDIO_SPEAKER_OUT_CD_H, 				//UUID: 2803,	VALUE:				Prop: write_without_rsp
    AUDIO_SPEAKER_OUT_DP_H, 				//UUID: telink speaker uuid,  VALUE: speakerData
    //AUDIO_SPEAKEROUT_DESC_H,				//UUID: 2901,	VALUE: speakerName
#endif


    /**********HID************************************************************************************/
    HID_PS_H, 								//UUID: 2800, 	VALUE: uuid 1812


    //HID_INCLUDE_H,							//UUID: 2802,	VALUE: include

    //protocol
    HID_PROTOCOL_MODE_CD_H,					//UUID: 2803, 	VALUE:  			Prop: read | write_without_rsp
    HID_PROTOCOL_MODE_DP_H,					//UUID: 2A4E,	VALUE: protocolMode

#if HID_KEYBOARD_ATT_ENABLE
    HID_BOOT_KB_REPORT_INPUT_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read | Notify
    HID_BOOT_KB_REPORT_INPUT_DP_H,			//UUID: 2A22, 	VALUE: bootKeyInReport
    HID_BOOT_KB_REPORT_INPUT_CCB_H,			//UUID: 2902, 	VALUE: bootKeyInReportCCC


    HID_BOOT_KB_REPORT_OUTPUT_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read | write| write_without_rsp
    HID_BOOT_KB_REPORT_OUTPUT_DP_H,		//UUID: 2A32, 	VALUE: bootKeyOutReport


    HID_CONSUME_REPORT_INPUT_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read | Notify
    HID_CONSUME_REPORT_INPUT_DP_H,			//UUID: 2A4D, 	VALUE: reportConsumerIn
    HID_CONSUME_REPORT_INPUT_CCB_H,			//UUID: 2902, 	VALUE: reportConsumerInCCC
    HID_CONSUME_REPORT_INPUT_REF_H, 		//UUID: 2908    VALUE: REPORT_ID_CONSUMER, TYPE_INPUT


    HID_NORMAL_KB_REPORT_INPUT_CD_H,		//UUID: 2803, 	VALUE:  			Prop: Read | Notify
    HID_NORMAL_KB_REPORT_INPUT_DP_H,		//UUID: 2A4D, 	VALUE: reportKeyIn
    HID_NORMAL_KB_REPORT_INPUT_CCB_H,		//UUID: 2902, 	VALUE: reportKeyInInCCC
    HID_NORMAL_KB_REPORT_INPUT_REF_H, 		//UUID: 2908    VALUE: REPORT_ID_KEYBOARD, TYPE_INPUT

    HID_NORMAL_KB_REPORT_OUTPUT_CD_H,		//UUID: 2803, 	VALUE:  			Prop: Read | write| write_without_rsp
    HID_NORMAL_KB_REPORT_OUTPUT_DP_H,  		//UUID: 2A4D, 	VALUE: reportKeyOut
    HID_NORMAL_KB_REPORT_OUTPUT_REF_H, 		//UUID: 2908    VALUE: REPORT_ID_KEYBOARD, TYPE_OUTPUT
#endif

#if HID_MOUSE_ATT_ENABLE
    HID_BOOT_MOUSE_REPORT_INPUT_CD_H,			//UUID: 2803,	VALUE:				Prop: Read | Notify
    HID_BOOT_MOUSE_REPORT_INPUT_DP_H,			//UUID: 2A22,	VALUE: bootKeyInReport
    HID_BOOT_MOUSE_REPORT_INPUT_CCB_H, 		//UUID: 2902,	VALUE: bootKeyInReportCCC


    HID_MOUSE_REPORT_INPUT_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read | Notify
    HID_MOUSE_REPORT_INPUT_DP_H,			//UUID: 2A4D, 	VALUE: reportConsumerIn
    HID_MOUSE_REPORT_INPUT_CCB_H,			//UUID: 2902, 	VALUE: reportConsumerInCCC
    HID_MOUSE_REPORT_INPUT_REF_H, 		//UUID: 2908    VALUE: REPORT_ID_CONSUMER, TYPE_INPUT
#endif

    HID_USER_REPORT_INPUT_CD_H,			//UUID: 2803, 	VALUE:  			Prop: Read | Notify
    HID_USER_REPORT_INPUT_DP_H,			//UUID: 2A4D, 	VALUE: reportConsumerIn
    HID_USER_REPORT_INPUT_CCB_H,			//UUID: 2902, 	VALUE: reportConsumerInCCC
    HID_USER_REPORT_INPUT_REF_H, 		//UUID: 2908    VALUE: REPORT_ID_CONSUMER, TYPE_INPUT
    
#if(HID_REPORT_MIC_EN)
	HID_MIC_REPORT_INPUT_CD_H,			//UUID: 2803,	VALUE:				Prop: Read | Notify
	HID_MIC_REPORT_INPUT_DP_H,			//UUID: 2A4D,	VALUE: reportConsumerIn
	HID_MIC_REPORT_INPUT_CCB_H, 		//UUID: 2902,	VALUE: reportConsumerInCCC
	HID_MIC_REPORT_INPUT_REF_H, 
#endif

    HID_REPORT_MAP_CD_H,					//UUID: 2803, 	VALUE:  			Prop: Read
    HID_REPORT_MAP_DP_H,					//UUID: 2A4B, 	VALUE: reportKeyIn
    //HID_REPORT_MAP_EXT_REF_H,				//UUID: 2907 	VALUE: extService


    HID_INFORMATION_CD_H,					//UUID: 2803, 	VALUE:  			Prop: read
    HID_INFORMATION_DP_H,					//UUID: 2A4A 	VALUE: hidInformation

    //control point
    HID_CONTROL_POINT_CD_H,					//UUID: 2803, 	VALUE:  			Prop: write_without_rsp
    HID_CONTROL_POINT_DP_H,					//UUID: 2A4C 	VALUE: controlPoint



    //**********end********************************************************/
    MY_DATA_PS_H,
    MY_DATA_INPUT_CD_H,
    MY_DATA_INPUT_DP_H,
    MY_DATA_INPUT_CCB_H,

    ATT_END_H,

} ATT_HANDLE;


#include "vendor/common/default_config.h"

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
