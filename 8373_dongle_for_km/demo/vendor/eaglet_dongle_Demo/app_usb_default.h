/********************************************************************************************************
 * @file     AAA_usb_default.h
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

#ifndef _APP_USB_DEFAULT_H_
#define _APP_USB_DEFAULT_H_

#include "types.h"


//////////// product  Information  //////////////////////////////
typedef enum
{ 	
	IDLE = 0,
	USB_DEVICE_CONNECT_PC,
	USB_DEVICE_CHECK_PC_SLEEP,
	USB_DEVICE_DISCONECT_PC,//may be pc power off
	USB_DEVICE_UNPLUG,
}USB_DEVICE_STATUS;


#define	FLOW_NO_OS					1
#define APPLICATION_DONGLE							1
#if(APPLICATION_DONGLE)
	#define	USB_PRINTER_ENABLE 		0
    #define	USB_MOUSE_ENABLE 		1
    #define	USB_KEYBOARD_ENABLE 	1
    #define	USB_MIC_ENABLE 			0
	#define	USB_SPEAKER_ENABLE 		0
	#define USB_CDC_ENABLE          0

	#define	USB_SOMATIC_ENABLE      0   //  when USB_SOMATIC_ENABLE, USB_EDP_PRINTER_OUT disable
	#define USB_CUSTOM_HID_REPORT	1
	#define	USB_CUSTOM_HID_REPORT_REG_ACCESS		0

	#define USB_DESCRIPTOR_MY_SELF   0
#endif
#define D24G_OTA_ENABLE_AAA		0 //OTA enable
#define USB_OTA_AND_APP_ENABLE      1
#if (MCU_CORE_B80 || MCU_CORE_B85 || MCU_CORE_B87 || MCU_CORE_B89)
#define USB_PHYSICAL_EDP_CDC_IN     USB_EDP_CDC_IN  /* physical in endpoint */
#define USB_PHYSICAL_EDP_CDC_OUT    USB_EDP_CDC_OUT /* physical out endpoint */
#elif (MCU_CORE_B80B)
/* control endpoint size config. */
#define USB_CTR_ENDPOINT_SIZE       8 /* 8/16/32/64 */
#define USB_CTR_SIZE                (USB_CTR_ENDPOINT_SIZE == 64) ? SIZE_64_BYTE :                 \
                                    ((USB_CTR_ENDPOINT_SIZE == 32) ? SIZE_32_BYTE :                \
                                    ((USB_CTR_ENDPOINT_SIZE == 16) ? SIZE_16_BYTE :                \
                                    ((USB_CTR_ENDPOINT_SIZE == 8) ? SIZE_8_BYTE : SIZE_64_BYTE)))

#define USB_MAP_EN                  0 /* 1:usb map function enable, 0:usb map function disable. */

#define USB_PHYSICAL_EDP_CDC_IN     USB_EDP_CDC_IN  /* physical in endpoint */
#define USB_PHYSICAL_EDP_CDC_OUT    USB_EDP_CDC_OUT /* physical out endpoint */

#if (USB_MAP_EN == 1)
#define CDC_RX_EPNUM                USB_EDP_CDC_OUT /* logical in endpoint */
#define CDC_TX_EPNUM                USB_EDP_CDC_OUT /* logical out endpoint */
#else
#define CDC_RX_EPNUM                USB_PHYSICAL_EDP_CDC_OUT /* USB_MAP_EN = 0, logical endpoint is the same as the physical endpoint */
#define CDC_TX_EPNUM                USB_PHYSICAL_EDP_CDC_IN /* USB_MAP_EN = 0, logical endpoint is the same as the physical endpoint*/
#endif

#endif

/* control endpoint size default is 8 bytes. */
#ifndef USB_CTR_ENDPOINT_SIZE
#define USB_CTR_ENDPOINT_SIZE       8
#endif

//////////////////// Audio /////////////////////////////////////
#define MIC_RESOLUTION_BIT		16
#define MIC_SAMPLE_RATE			16000//set sample for mic and spk
#define MIC_CHANNEL_COUNT		1
#define	MIC_ENCODER_ENABLE		0

#define SPK_RESOLUTION_BIT		16
#define SPEAKER_SAMPLE_RATE     16000
#define SPK_CHANNEL_COUNT     	1
#if(USB_MIC_ENABLE||USB_SPEAKER_ENABLE)
	#define USB_MODE_AUDIO_EN				1
#endif


#define ID_VENDOR				0x248a			// for report

#if(USB_CDC_ENABLE)
#define ID_PRODUCT			    0x8002
#else
#define ID_PRODUCT			    0x8006
#endif

#define  ID_VERDION            0x0100

#if(USB_MODE_CDC_EN)
#define STRING_VENDOR				L"Telink Semi-conductor Ltd, Co"
#define STRING_PRODUCT				L"Telink CDC"
#define STRING_SERIAL				L"CDC demo "
#endif

#if(USB_MOUSE_ENABLE&(!USB_KEYBOARD_ENABLE))
#define STRING_VENDOR				L"Telink Semi-conductor Ltd, Co"
#define STRING_PRODUCT				L"Telink Mouse"
#define STRING_SERIAL				L"Mouse demo"
#endif

#if((!USB_MOUSE_ENABLE)&USB_KEYBOARD_ENABLE)
#define STRING_VENDOR				L"Telink Semi-conductor Ltd, Co"
#define STRING_PRODUCT				L"Tek Keyboard"
#define STRING_SERIAL				L"Keyboard demo"
#endif

#if((USB_MOUSE_ENABLE)&USB_KEYBOARD_ENABLE)
#define STRING_VENDOR				L"Telink Semi-conductor Ltd, Co"
#define STRING_PRODUCT				L"Telink KM"
#define STRING_SERIAL				L"KM demo"
#endif

#if(USB_CDC_ENABLE)
#define STRING_VENDOR				L"Telink Semi-conductor Ltd, Co"
#define STRING_PRODUCT				L"Telink CDC"
#define STRING_SERIAL				L"CDC demo "
#endif


#if(USB_MODE_AUDIO_EN)
#define STRING_VENDOR				L"Telink Semi-conductor Ltd, Co"
#define STRING_PRODUCT				L"Telink Audio16"
#define STRING_SERIAL				L"Audio16 demo"
#endif

#if((!USB_MODE_AUDIO_EN)&&(!USB_KEYBOARD_ENABLE)&&(!USB_MOUSE_ENABLE)&&(!USB_MODE_CDC_EN)&&(!USB_CDC_ENABLE))
#define STRING_VENDOR				L"Telink Semi-conductor Ltd, Co"
#define STRING_PRODUCT				L"Telink No Product"
#define STRING_SERIAL				L"USB demo"
#endif


///////////////////  USB   /////////////////////////////////
#ifndef IRQ_USB_PWDN_ENABLE
#define	IRQ_USB_PWDN_ENABLE  		    1
#endif


#ifndef USB_PRINTER_ENABLE
#define	USB_PRINTER_ENABLE 		0
#endif
#ifndef USB_SPEAKER_ENABLE
#define	USB_SPEAKER_ENABLE 		0
#endif
#ifndef USB_MIC_ENABLE
#define	USB_MIC_ENABLE 			0
#endif
#ifndef USB_MOUSE_ENABLE
#define	USB_MOUSE_ENABLE 			0
#endif
#ifndef USB_KEYBOARD_ENABLE
#define	USB_KEYBOARD_ENABLE 		0
#endif
#ifndef USB_SOMATIC_ENABLE
#define	USB_SOMATIC_ENABLE 		0
#endif
#ifndef USB_CUSTOM_HID_REPORT
#define	USB_CUSTOM_HID_REPORT 		0
#endif
#ifndef USB_AUDIO_441K_ENABLE
#define USB_AUDIO_441K_ENABLE  	0
#endif
#ifndef USB_MASS_STORAGE_ENABLE
#define USB_MASS_STORAGE_ENABLE  	0
#endif

#ifndef MIC_CHANNEL_COUNT
#define MIC_CHANNEL_COUNT  			1
#endif

#ifndef USB_DESCRIPTOR_CONFIGURATION_FOR_KM_DONGLE
#define USB_DESCRIPTOR_CONFIGURATION_FOR_KM_DONGLE  	0 //
#endif

#ifndef USB_ID_AND_STRING_CUSTOM
#define USB_ID_AND_STRING_CUSTOM  						0 //
#endif

#define KEYBOARD_RESENT_MAX_CNT			3
#define KEYBOARD_REPEAT_CHECK_TIME		300000	// in us
#define KEYBOARD_REPEAT_INTERVAL		100000	// in us
#define KEYBOARD_SCAN_INTERVAL			16000	// in us
#define MOUSE_SCAN_INTERVAL				8000	// in us
#define SOMATIC_SCAN_INTERVAL     		8000

#define USB_KEYBOARD_POLL_INTERVAL		4		// in ms	USB_KEYBOARD_POLL_INTERVAL < KEYBOARD_SCAN_INTERVAL to ensure PC no missing key
#define USB_MOUSE_POLL_INTERVAL			1		// in ms
#define USB_SOMATIC_POLL_INTERVAL     	8		// in ms

#define USB_KEYBOARD_RELEASE_TIMEOUT    (450000) // in us
#define USB_MOUSE_RELEASE_TIMEOUT       (200000) // in us
#define USB_SOMATIC_RELEASE_TIMEOUT     (200000) // in us



extern u8 bin_crc[];

#if 0//USB_DESCRIPTOR_MY_SELF


/**
 * @brief       This function get report desc size
 * @return      
 * @note        
 */
unsigned short usbmouse_get_report_desc_size(void);



/**
 * @brief       This function get report desc
 * @return      
 * @note        
 */
unsigned char* usbmouse_get_report_desc(void);


/**
 * @brief       This function get desc size
 * @return      
 * @note        
 */
unsigned short usbkb_get_report_desc_size(void);

/**
 * @brief       This function get report desc
 * @return      
 * @note        
 */
unsigned char* usbkb_get_report_desc(void);


/**
 * @brief       This function get product size
 * @return      
 * @note        
 */
unsigned short usb_desc_get_product_size();

/**
 * @brief       This function get serial size
 * @return      
 * @note        
 */
unsigned short  usb_desc_get_serial_size(void);

/**
 * @brief       This function get vendor size
 * @return      
 * @note        
 */
unsigned short  usb_desc_get_vendor_size(void);

/**
 * @brief       This function get config size
 * @return      
 * @note        
 */
unsigned short  usb_desc_get_configuration_size();

u8* usb_get_HID_DTYPE_HID(u8 index,u16* g_response_len);
u8* usb_get_HID_DTYPE_Report(u8 index,u16* g_response_len);
#endif

#define  SPP_REORT_ID 				07 //no use
#define  USB_OTA_REPORT_ID 			06

#define  CUSTOM_INPUT_REPORT_ID		04
#define  CUSTOM_OUTPUT_REPORT_ID	05

#define  D24G_OTA_REPORT_ID 		07

#define  SPP_REORT_LEN 22

enum CMD_TYPE 
{ 	
	ID_CMD,//0
	OTA_CMD,//1
	BREATH_CMD,//2
	MOUSE_INF_CMD,//3
	
	BTN_CFG_CMD,//4
	DPI_CFG_CMD,//5
	REPORT_RATE_CFG_CMD,//6	
	DEBUG_REPORT_RATE_CMD,//7
};

#if(0)
typedef struct
{
	u8 len;//len
	u8 type;//type
	
	u8 buf[SPP_REORT_LEN-2];//spp buff
}USB_PC_DEVICE;
extern USB_PC_DEVICE d_to_p_dat;

#define FROM_PC_FIFO_NUM  4 
//#define USB_FIFO_MAX_LEN  9
typedef struct
{
	u8 fifo[FROM_PC_FIFO_NUM][23];//4 23
	u8 wptr;//wrp
	u8 rptr;//rp
}DEVICE_PC_FIFO_DATA_S;
extern  DEVICE_PC_FIFO_DATA_S  from_pc_dat;
extern  DEVICE_PC_FIFO_DATA_S  to_pc_dat;


#endif


#endif

