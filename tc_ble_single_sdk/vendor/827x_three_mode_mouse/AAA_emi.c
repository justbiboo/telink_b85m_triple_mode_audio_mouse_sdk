/********************************************************************************************************
 * @file     emi.c
 *
 * @brief    for TLSR chips
 *
 * @author	 public@telink-semi.com;
 * @date     May. 12, 2018
 *
 * @par      Copyright (c) Telink Semiconductor (Shanghai) Co., Ltd.
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
/*
 * emi.c
 *
 *  Created on: 2018-5-31
 *      Author: Administrator
 */
#include "AAA_public_config.h"

#if EMI_TEST_FUN_ENABLE_AAA

_attribute_data_retention_user u8 mode = 0;
_attribute_data_retention_user	u8 channel = 0;
_attribute_data_retention_user	u8 run_flag = 0;



/**
 * @brief       This function check emi key and change emi mode and channel
 * @return      
 * @note        
 */
u8 emi_has_change()
{
    static u32 tick;
    u16 now_value = 0;

    if (clock_time_exceed(tick, 10000))
    {
        tick = clock_time();
#if BUTTON_FUN_ENABLE_AAA
        now_value = btn_scan();
#endif
        if (last_btn_value != now_value)
        {
            if (last_btn_value == MS_BTN_LEFT)
            {
                channel = (channel + 1) % 0x03;
                run_flag = 1;
            }
            else if (last_btn_value == MS_BTN_MIDDLE)
            {
                mode = (mode + 1) % 0x04;
                run_flag = 1;
            }

        }
        last_btn_value = now_value;
    }
    return run_flag;
}

void emicarrieronly(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
	//RF_PowerTypeDef power = rf_power_Level_list[pwr];
    rf_emi_single_tone(pwr, rf_chn);
    while (!emi_has_change())
    {

    }
    rf_emi_stop();
}
/*#if 0
void emi_con_prbs9(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
    rf_emi_tx_continue_setup(rf_mode, pwr, rf_chn, 0);

    while (!emi_has_change())
    {
        rf_continue_mode_run();
    }
    rf_emi_stop();
}
void emi_con_tx0f(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
    rf_emi_tx_continue_setup(rf_mode, pwr, rf_chn, 1);

    while (!emi_has_change())
    {
        rf_continue_mode_run();
    }
    rf_emi_stop();
}
#endif*/
void emi_con_tx55(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
    rf_emi_tx_continue_setup(rf_mode, pwr, rf_chn, 0);

    while (!emi_has_change())
    {
        rf_continue_mode_run();

    }
    rf_emi_stop();
}
/**
 * @brief       This function emi carrier data
 * @param[in]   data_type	-  
 * @param[in]   pwr	- 
 * @param[in]   rf_chn	- 
 * @param[in]   rf_mode	- 
 * @return      
 * @note        
 */
void emi_carrier_data(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn,u8 data_type)
{
    rf_emi_tx_continue_setup(rf_mode, pwr, rf_chn, data_type);

    while (!emi_has_change())
    {
        rf_continue_mode_run();
    }
    rf_emi_stop();
}

/*#if 0
void emitxprbs9(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
    rf_emi_tx_brust_setup(rf_mode, pwr, rf_chn, 0);
    while (!emi_has_change())
    {
        rf_emi_tx_brust_loop(rf_mode, 0);
    }
    rf_emi_stop();
}

void emitx0f(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
    rf_emi_tx_brust_setup(rf_mode, pwr, rf_chn, 1);
    while (!emi_has_change())
    {
        rf_emi_tx_brust_loop(rf_mode, 1);
    }
    rf_emi_stop();
}
#endif*/
void emitx55(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
    rf_emi_tx_burst_setup(rf_mode, pwr, rf_chn, 0);
    while (!emi_has_change())
    {
        rf_emi_tx_burst_loop(rf_mode, 0);
    }
    rf_emi_stop();
}
/**
* @brief	   This function emi tx mode
* @param[in]   data_type   - The type of data sent
* @param[in]   pwr - rf power
* @param[in]   rf_chn  - rf channel 
* @param[in]   rf_mode - rf mode
* @return	   
* @note 	   
*/
void emi_tx_mode(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn,u8 data_type)
{
    rf_emi_tx_burst_setup(rf_mode, pwr, rf_chn, data_type);
    while (!emi_has_change())
    {
        rf_emi_tx_burst_loop(rf_mode, data_type);
    }
    rf_emi_stop();
}


/**
 * @brief       This function emi rx mode
 * @param[in]   pwr	- rf_power
 * @param[in]   rf_chn	- rf_channel
 * @param[in]   rf_mode	- rf mode
 * @return      
 * @note        
 */
void emirx(RF_ModeTypeDef rf_mode, RF_PowerTypeDef pwr, signed char rf_chn)
{
    rf_emi_rx(rf_mode, rf_chn);
    while (!emi_has_change())
    {
        rf_emi_rx_loop();
#if 0
        if (rf_emi_get_rxpkt_cnt() != read_reg32(0x84000c))
        {
            write_reg8(RF_RSSI, rf_emi_get_rssi_avg());
            write_reg32(RF_REC_NUMB, rf_emi_get_rxpkt_cnt());
        }
#endif
    }
    rf_emi_stop();//stop rf emi
}
void emi_init()
{
    write_reg32(0x408,0x29417671 );//access code  0xf8118ac9
	write_reg8(0x13c,0x10); // print buffer size set

	//write_reg8(0x40005,tx_cnt);//tx_cnt
	//write_reg8(0x40006,run);//run
	//write_reg8(0x40007,cmd_now);//cmd
	//write_reg8(0x40008,power_level);//power
	//write_reg8(0x40009,chn);//chn
	//write_reg8(0x4000a,mode);//mode
	//write_reg8(0x4000b,hop);//hop
	//write_reg8(0x40004,0);
	//write_reg32(0x4000c,0);
	//gpio_shutdown(GPIO_ALL);//for pm
	analog_write(0x33,0xff);
}
#define EMI_BTN  (MS_BTN_LEFT | MS_BTN_RIGHT | MS_BTN_MIDDLE)
void emi_process()
{
    u8 tbl_chn[3] = {05, 30, 70};
    u16 now_value = 0;
#if 1
    for (u8 i = 0; i < 2; i++)
    {
        now_value = btn_scan();
        if (now_value != EMI_BTN)
        {
            return;
        }
        sleep_us(100000);//debounce
    }
	
    while (1) //waite for btn release		
    {
        now_value = btn_scan();
        if (now_value == 0)
        {
            break;
        }
    }
#endif

#if USE_EXTERNAL_CAP
    blc_app_setExternalCrystalCapEnable(1);
#endif
	blc_app_loadCustomizedParameters();

    u32 led_pin = PIN_24G_LED;
    int rf_mode = RF_MODE_BLE_1M;
    if (fun_mode == RF_1M_BLE_MODE)
    {
       // rf_mode = RF_MODE_BLE_1M;
        led_pin = PIN_BLE_LED;
    }
    gpio_set_output_en(led_pin, 1);
    gpio_write(led_pin, LED_ON_AAA);

	wd_stop();

    emi_init();
    run_flag = 1;
	
    while (1)
    {

        if(run_flag)
        {
            run_flag = 0;
            irq_disable();

	#if  BLT_APP_LED_ENABLE
            for (u8 i = 0; i < (mode + 1); i++)
            {
                sleep_us(250000 * (channel + 1));
                gpio_write(led_pin, LED_OFF_AAA);
                sleep_us(250000 * (channel + 1));
                gpio_write(led_pin, LED_ON_AAA);
            }
	#endif

            if (mode == 0) //carri
            {
                emicarrieronly(rf_mode, user_cfg.emi_tx_power, tbl_chn[channel]);
            }
            else if (mode == 1) //cd   emi
            {
                //emi_con_tx55(rf_mode, user_cfg.emi_tx_power, tbl_chn[channel]);
		   		emi_carrier_data(rf_mode, user_cfg.emi_tx_power, tbl_chn[channel],0);
            }
            else if (mode == 2) //rx
            {
                emirx(rf_mode, user_cfg.emi_tx_power, tbl_chn[channel]);
            }
            else if (mode == 3) //tx
            {
           	 	emi_tx_mode(rf_mode, user_cfg.emi_tx_power, tbl_chn[channel],0);
               // emitx55(rf_mode, user_cfg.emi_tx_power, tbl_chn[channel]);
            }
        }
    }
	
}
#endif
