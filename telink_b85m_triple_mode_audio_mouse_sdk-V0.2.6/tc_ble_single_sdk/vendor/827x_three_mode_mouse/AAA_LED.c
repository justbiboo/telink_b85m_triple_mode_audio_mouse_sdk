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
	CONN_LED_PRIORITY,
	DPI_LED_PRIORITY,
	RECONN_LED_PRIORITY,
	ADV_LED_PRIORITY,
	MODE_LED_PRIORITY,
	OTA_LED_PRIORITY,
} LED_CFG_ENUM_AAA;//led priority enum 

#if 1
_attribute_data_retention_user led_cfg_t dpi_led = {250, 250, 1, DPI_LED_PRIORITY};
const led_cfg_t adv_24g_led = {250,	250,	1,	ADV_LED_PRIORITY};
const led_cfg_t adv_ble_led = {250,	250,	1,	ADV_LED_PRIORITY};

#if((PRJ_NAME==WANG_HONG_TAI_BEI_PRJ))
const led_cfg_t bat_lvd_led = {125,	125,  44,	BAT_LVD_LED_PRIORITY};
#else
const led_cfg_t bat_lvd_led = {125,	875,   1,	BAT_LVD_LED_PRIORITY};
#endif


#if((PRJ_NAME==WANG_HONG_TAI_BEI_PRJ))
const led_cfg_t mod_24_led =	{10000,	100,	1,	MODE_LED_PRIORITY};
const led_cfg_t mod_ble_led = {10000,	100,	1,	MODE_LED_PRIORITY};
#else
const led_cfg_t mod_24_led =	{500,	500,	1,	MODE_LED_PRIORITY};
const led_cfg_t mod_ble_led = {500,	500,	2,	MODE_LED_PRIORITY};
#endif

const led_cfg_t reconn_24g_led = {1000, 1000,	1,	RECONN_LED_PRIORITY};
const led_cfg_t reconn_ble_led = {1000, 1000,	1,	RECONN_LED_PRIORITY};

const led_cfg_t ota_ready_24g_led = {250, 250,	1,	OTA_LED_PRIORITY};
#endif


/**
 * @brief       This function set  dpi led
 * @param[in]   idx	- dpi value
 * @return      
 * @note        
 */
void dpi_led_set(u8 idx)
{
    dpi_led.repeatCount = idx;//repeat count
    device_led_setup(dpi_led); //dpi led cfg
}


/**
 * @brief       This function set ota ready led 
 * @return      
 * @note        
 */
void led_ota_ready_set()
{
    device_led_setup(ota_ready_24g_led);
}


/**
 * @brief       This function  poll ble adv led
 * @return      
 * @note        
 */
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


/**
 * @brief       This function poll 24g adv led
 * @return      
 * @note        
 */
void led_2p4_Adv_poll()
{
    if (pair_flag)//if pair flag is 1
    {
        device_led_setup(adv_24g_led);//set adv led
    }
    else
    {
        device_led_setup(reconn_24g_led);//set reconn led
    }

}


/**
 * @brief       This function set led_ble mode
 * @return      
 * @note        
 */
void led_ble_mode_display()
{
    gpio_write(PIN_24G_LED, LED_OFF_AAA);

    device_led_init(PIN_BLE_LED, LED_ON_AAA);
    device_led_setup(mod_ble_led);
}
/**
 * @brief       This function set led_24g mode
 * @return      
 * @note        
 */
void led_24g_mode_display()
{
    gpio_write(PIN_BLE_LED, LED_OFF_AAA);
    device_led_init(PIN_24G_LED, LED_ON_AAA);
    device_led_setup(mod_24_led);
}

/**
 * @brief       This function set led_bat_lvd mode
 * @return      
 * @note        
 */
void led_bat_lvd()
{
    device_led_setup(bat_lvd_led);
}

void led_ble_ConnectedStatus()
{
    //gpio_write(PIN_BLE_LED,LED_OFF_AAA);

}

void led_24g_ConnectedStatus()
{
    //gpio_write(PIN_24G_LED,LED_OFF_AAA);
}
/**
 * @brief       This function init led hardware
 * @return      
 * @note        
 */
void led_hw_init()
{
#if 1
    if (fun_mode == RF_1M_BLE_MODE)
    {
        // gpio_set_output_en(PIN_24G_LED,0);
        //gpio_write(PIN_24G_LED,LED_OFF_AAA);

        //gpio_set_output_en(PIN_BLE_LED,1);
        //gpio_write(PIN_BLE_LED,LED_OFF_AAA);
        device_led_init(PIN_BLE_LED, LED_ON_AAA);
    }
    else if(fun_mode == RF_2M_2P4G_MODE)
    {
        // gpio_set_output_en(PIN_BLE_LED,0);
        //gpio_write(PIN_BLE_LED,LED_OFF_AAA);

        //gpio_set_output_en(PIN_24G_LED,1);
        //gpio_write(PIN_24G_LED,LED_OFF_AAA);
        device_led_init(PIN_24G_LED, LED_ON_AAA);
    }
	else
	{
		device_led_init(PIN_BLE2_LED, LED_ON_AAA);
	}
#endif
}
#if DEBUG_TOGGLE_GPIO_ENABLE

/**
 * @brief       This function toggle debug led
 * @return      
 * @note        
 */
void debug_loop_toggle()
{
    static u8 flag = 0;
    gpio_write(PIN_TOGGLE, flag);
    flag ^= 0xff;
}
#endif


#endif


//#endif




