/********************************************************************************************************
 * @file     aaa_led.c
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

#include "AAA_public_config.h"


#if BLT_APP_LED_ENABLE

typedef enum
{
    BAT_LVD_LED_PRIORITY,
    DPI_LED_PRIORITY,
    RECONN_LED_PRIORITY,
    ADV_LED_PRIORITY,
    MODE_LED_PRIORITY,
    CONNECTED_LED_PRIORITY,
    POWER_OFF_LED_PRIORITY,
} LED_CFG_ENUM_AAA;

//u16 led_pin = 0;

_attribute_data_retention_user led_cfg_t dpi_led = {250, 250, 1, DPI_LED_PRIORITY};
const led_cfg_t adv_24g_led = {200,	200,	1,	ADV_LED_PRIORITY};
const led_cfg_t adv_ble_led = {200,	200,	1,	ADV_LED_PRIORITY};

const led_cfg_t bat_lvd_led = {125,	875,   5,	BAT_LVD_LED_PRIORITY};
const led_cfg_t power_off_led = {150, 150,  10,	POWER_OFF_LED_PRIORITY};


const led_cfg_t mod_24_led = {500,	500,	1,	MODE_LED_PRIORITY};
const led_cfg_t mod_ble_led = {500,	500,	2,	MODE_LED_PRIORITY};

const led_cfg_t reconn_24g_led = {1000, 1000,	1,	RECONN_LED_PRIORITY};
const led_cfg_t reconn_ble_led = {1000, 1000,	1,	RECONN_LED_PRIORITY};

const led_cfg_t connected_led = {3000, 500,	1,	CONNECTED_LED_PRIORITY};

device_led_t  ble_hw_led;
device_led_t  d24g_hw_led;
//device_led_t  *mode_hw_led;

void dpi_led_set(u8 idx)
{
    dpi_led.repeatCount = idx;
	if(fun_mode==RF_1M_BLE_MODE)
	{
    	device_led_setup(dpi_led);
	}
	else
	{
    	device_led_setup(dpi_led);
	}
}

void led_ble_Adv_poll()
{
    if (pair_flag)
    {
        device_led_setup(adv_ble_led);
    }
    else
    {
        device_led_setup(reconn_ble_led);
    }
}
void led_2p4_Adv_poll()
{
    if (pair_flag)
    {
        device_led_setup(adv_24g_led);
    }
    else
    {
        device_led_setup(reconn_24g_led);
    }
}

void led_ble_mode_display()
{
    gpio_write(PIN_24G_LED, LED_OFF_AAA);

    device_led_setup(mod_ble_led);
}
void led_24g_mode_display()
{
    gpio_write(PIN_BLE_LED, LED_OFF_AAA);
 
    device_led_setup(mod_24_led);
}
void led_bat_lvd()
{

    device_led_setup(bat_lvd_led);
}

void led_ble_ConnectedStatus()
{
    device_led_setup(connected_led);
}
void led_24g_ConnectedStatus()
{
    device_led_setup(connected_led);
}
void led_hw_init()
{

    if (fun_mode == RF_1M_BLE_MODE)
    {
        //device_led_init(PIN_BLE_LED, LED_ON_AAA);
        #if(UART_PRINT_DEBUG_ENABLE)
        device_led_init(PIN_24G_LED, LED_ON_AAA);
		#else
		device_led_init(PIN_BLE_LED, LED_ON_AAA);
		#endif
    }
	#if(TELINK_BOARD)
	else if(fun_mode == USB_MODE)
	{
		device_led_init(PIN_USB_LED, LED_ON_AAA);
	}
	#endif
    else
    {
        device_led_init(PIN_24G_LED, LED_ON_AAA);
    }

}


#endif
