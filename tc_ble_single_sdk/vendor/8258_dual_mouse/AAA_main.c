/********************************************************************************************************
 * @file     aaa_main.c
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
_attribute_data_retention_user u8 report_rate=4;

_attribute_data_retention_user int dev_info_idx;
_attribute_data_retention_user u8 pub_key[16] =
{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};


_attribute_data_retention_user OUTPUT_DEV_INFO_AAA output_dev_info =
{
    0,//u32 bin_crc;
    0,//{3, 4, 2,1,4,15}, //u8
    0,//u8 sensor_type;
    0,//u8 sensor_pd1;
    0,//u8 sensor_pd2;
    0,//u8 sensor_pd3;
};
_attribute_data_retention_user u8 ic_inf[20];



_attribute_ram_code_ void irq_handler(void)
{
    if (fun_mode == RF_2M_2P4G_MODE)
    {
        irq_handle_private_2m();
    }
    else
    {
        irq_blt_sdk_handler();
    }
}

//len must 4 bytes bei beisu  beacuse *(volatile u32 *)(s_addr + idx) must 4 bytes bei beisu
int flash_info_load_aaa(u32 s_addr, u8 *d_addr,  int len)
{
    int idx;
    u32 buf;
    for (idx = 0; idx < (4096 - len); idx += len)
    {
        flash_read_page((u32)(s_addr + idx), 4, (u8 *)(&buf));
        if (buf == U32_MAX)
        {
            break;
        }
    }
    idx -= len;
    if (idx < 0) 		// no binding
    {
        return idx;
    }
    flash_read_page((u32)(s_addr + idx), len, d_addr);

    if (idx > 3000) 			//3k, erase flash
    {
        flash_erase_sector((u32)s_addr);

        sleep_us(10);

        flash_write_page((u32)s_addr, len, d_addr);
        idx = 0;
    }
    return idx;
}

#if IS_SINGLE_GPIO_CHANGE_MODE

void three_switch_mode_change()
{

    //gpio_setup_up_down_resistor(PIN_MODE_SWITCH,PM_PIN_PULLUP_1M);
    //sleep_us(20);
    if (IS_BLE_MODE_AAA)
    {
        flash_dev_info.mode = RF_1M_BLE_MODE;
        //gpio_set_input_en(GPIO_MODE_SWITCH,0);

    }
    else
    {
        flash_dev_info.mode = RF_2M_2P4G_MODE;
    }
    //gpio_setup_up_down_resistor(PIN_MODE_SWITCH,PM_PIN_UP_DOWN_FLOAT);
#if 0 //test switch is good
    gpio_set_output_en(PIN_BLE_LED, 1);
    while (1)
    {
        if (IS_BLE_MODE_AAA)
        {

            gpio_write(PIN_BLE_LED, LED_ON_AAA);
        }
        else
        {
            gpio_write(PIN_24G_LED, LED_ON_AAA);
        }
    }

#endif
}
#endif
void user_config_tzy()
{
    random_generator_init();  //this is must
    flash_read_page(CFG_ADR_MAC, sizeof(custom_cfg_t), (u8 *)&user_cfg.dev_mac);
    if (user_cfg.dev_mac == U32_MAX)
    {
        generateRandomNum(4, (u8 *) &user_cfg.dev_mac);
        flash_write_page(CFG_ADR_MAC, 4, (u8 *)&user_cfg.dev_mac);
    }


    if (user_cfg.paring_tx_power == U8_MAX)
    {
        user_cfg.paring_tx_power = DEFAULT_PAIR_TX_POWER;
    }
    if (user_cfg.tx_power == U8_MAX)
    {
        user_cfg.tx_power = DEFAULT_NORMAL_TX_POWER;
    }
    if (user_cfg.emi_tx_power == U8_MAX)
    {
        user_cfg.emi_tx_power = DEFAULT_EMI_TX_POWER;
    }


    if ((user_cfg.sensor_direct == U8_MAX))
    {
		
        //user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_12;
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_6;
    }
    else if (user_cfg.sensor_direct == 0)
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_3;
    }
    else if (user_cfg.sensor_direct == 1)
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_6;
    }
    else if (user_cfg.sensor_direct == 2)
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_9;
    }
    else if (user_cfg.sensor_direct == 3)
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_12;
    }
     //if(user_cfg.internal_cap!=0)
     {
	 	//blc_app_setExternalCrystalCapEnable(1);
     }
    //aes Key
    for (u8 i = 0; i < 16; i++)
    {
        if (user_cfg.pub_key[i] != 0xff)
        {
            memcpy(pub_key, &user_cfg.pub_key[0], 16);
            break;
        }
    }

    for (u8 i = 0; i < 18; i++)
    {
        if (user_cfg.device_name[i] != 0xff)
        {
            device_name_len++;
        }
        else
        {
            break;
        }
    }

}
#if (APP_FLASH_PROTECTION_ENABLE)

/**
 * @brief      flash protection operation, including all locking & unlocking for application
 * 			   handle all flash write & erase action for this demo code. use should add more more if they have more flash operation.
 * @param[in]  flash_op_evt - flash operation event, including application layer action and stack layer action event(OTA write & erase)
 * 			   attention 1: if you have more flash write or erase action, you should should add more type and process them
 * 			   attention 2: for "end" event, no need to pay attention on op_addr_begin & op_addr_end, we set them to 0 for
 * 			   			    stack event, such as stack OTA write new firmware end event
 * @param[in]  op_addr_begin - operating flash address range begin value
 * @param[in]  op_addr_end - operating flash address range end value
 * 			   attention that, we use: [op_addr_begin, op_addr_end)
 * 			   e.g. if we write flash sector from 0x10000 to 0x20000, actual operating flash address is 0x10000 ~ 0x1FFFF
 * 			   		but we use [0x10000, 0x20000):  op_addr_begin = 0x10000, op_addr_end = 0x20000
 * @return     none
 */
_attribute_data_retention_ u16  flash_lockBlock_cmd = 0;
void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end)
{
	if(flash_op_evt == FLASH_OP_EVT_APP_INITIALIZATION)
	{
		/* ignore "op addr_begin" and "op addr_end" for initialization event
		 * must call "flash protection_init" first, will choose correct flash protection relative API according to current internal flash type in MCU */
		flash_protection_init();

		/* just sample code here, protect all flash area for old firmware and OTA new firmware.
		 * user can change this design if have other consideration */
		u32  app_lockBlock = 0;
		#if (BLE_OTA_SERVER_ENABLE)
			u32 multiBootAddress = blc_ota_getCurrentUsedMultipleBootAddress();
			if(multiBootAddress == MULTI_BOOT_ADDR_0x20000){
				app_lockBlock = FLASH_LOCK_FW_LOW_256K;
			}
			else if(multiBootAddress == MULTI_BOOT_ADDR_0x40000){
				/* attention that 512K capacity flash can not lock all 512K area, should leave some upper sector
				 * for system data(SMP storage data & calibration data & MAC address) and user data
				 * will use a approximate value */
				app_lockBlock = FLASH_LOCK_FW_LOW_512K;
			}
			#if(MCU_CORE_TYPE == MCU_CORE_827x)
			else if(multiBootAddress == MULTI_BOOT_ADDR_0x80000){
				if(blc_flash_capacity < FLASH_SIZE_1M){ //for flash capacity smaller than 1M, OTA can not use 512K as multiple boot address
					blc_flashProt.init_err = 1;
				}
				else{
					/* attention that 1M capacity flash can not lock all 1M area, should leave some upper sector for
					 * system data(SMP storage data & calibration data & MAC address) and user data
					 * will use a approximate value */
					app_lockBlock = FLASH_LOCK_FW_LOW_1M;
				}
			}
			#endif
		#else
			app_lockBlock = FLASH_LOCK_FW_LOW_256K; //just demo value, user can change this value according to application
		#endif


		flash_lockBlock_cmd = flash_change_app_lock_block_to_flash_lock_block(app_lockBlock);

		if(blc_flashProt.init_err){
			tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] flash protection initialization error!!!\n");
		}

		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] initialization, lock flash\n");
		flash_lock(flash_lockBlock_cmd);
	}
#if (BLE_OTA_SERVER_ENABLE)
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_BEGIN)
	{
		/* OTA clear old firmware begin event is triggered by stack, in "blc ota_initOtaServer_module", rebooting from a successful OTA.
		 * Software will erase whole old firmware for potential next new OTA, need unlock flash if any part of flash address from
		 * "op addr_begin" to "op addr_end" is in locking block area.
		 * In this sample code, we protect whole flash area for old and new firmware, so here we do not need judge "op addr_begin" and "op addr_end",
		 * must unlock flash */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW begin, unlock flash\n");
		flash_unlock();
	}
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_END)
	{
		/* ignore "op addr_begin" and "op addr_end" for END event
		 * OTA clear old firmware end event is triggered by stack, in "blc ota_initOtaServer_module", erasing old firmware data finished.
		 * In this sample code, we need lock flash again, because we have unlocked it at the begin event of clear old firmware */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN)
	{
		/* OTA write new firmware begin event is triggered by stack, when receive first OTA data PDU.
		 * Software will write data to flash on new firmware area,  need unlock flash if any part of flash address from
		 * "op addr_begin" to "op addr_end" is in locking block area.
		 * In this sample code, we protect whole flash area for old and new firmware, so here we do not need judge "op addr_begin" and "op addr_end",
		 * must unlock flash */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW begin, unlock flash\n");
		flash_unlock();
	}
	else if(flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END)
	{
		/* ignore "op addr_begin" and "op addr_end" for END event
		 * OTA write new firmware end event is triggered by stack, after OTA end or an OTA error happens, writing new firmware data finished.
		 * In this sample code, we need lock flash again, because we have unlocked it at the begin event of write new firmware */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
#endif
	/* add more flash protection operation for your application if needed */
}


#endif

int deepRetWakeUp;

_attribute_ram_code_ int main(void)     //must run in ramcode
{
    blc_pm_select_internal_32k_crystal();

	#if(MCU_CORE_TYPE == MCU_CORE_825x)
		cpu_wakeup_init();
	#elif(MCU_CORE_TYPE == MCU_CORE_827x)
		cpu_wakeup_init(LDO_MODE,INTERNAL_CAP_XTAL24M);
	#endif

   // cpu_wakeup_init();

    deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();  //MCU deep retention wakeUp

    gpio_init(!deepRetWakeUp);    //analog resistance will keep available in deepSleep mode, so no need initialize again

#if (CLOCK_SYS_CLOCK_HZ == 16000000)
    clock_init(SYS_CLK_16M_Crystal);
#elif (CLOCK_SYS_CLOCK_HZ == 24000000)
    clock_init(SYS_CLK_24M_Crystal);
#elif (CLOCK_SYS_CLOCK_HZ == 32000000)
	clock_init(SYS_CLK_32M_Crystal);
#elif (CLOCK_SYS_CLOCK_HZ == 48000000)
	clock_init(SYS_CLK_48M_Crystal);
#endif
#if(MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
			random_generator_init();  //this is must
#endif

	#if(UART_PRINT_DEBUG_ENABLE)
		tlkapi_debug_init();
		blc_debug_enableStackLog(STK_LOG_DISABLE);
	#endif

	blc_readFlashSize_autoConfigCustomFlashSector();

	/* attention that this function must be called after "blc_readFlashSize_autoConfigCustomFlashSector" !!!*/
	blc_app_loadCustomizedParameters_normal();


	/* attention that this function must be called after "blc_app_loadCustomizedParameters_normal" !!!
	   The reason is that the low battery check need the ADC calibration parameter, and this parameter
	   is loaded in blc_app_loadCustomizedParameters_normal.
	 */
	#if (APP_BATT_CHECK_ENABLE)
	/*The SDK must do a quick low battery detect during user initialization instead of waiting
	  until the main_loop. The reason for this process is to avoid application errors that the device
	  has already working at low power.
	  Considering the working voltage of MCU and the working voltage of flash, if the Demo is set below 2.0V,
	  the chip will alarm and deep sleep (Due to PM does not work in the current version of B92, it does not go
	  into deepsleep), and once the chip is detected to be lower than 2.0V, it needs to wait until the voltage rises to 2.2V,
	  the chip will resume normal operation. Consider the following points in this design:
		At 2.0V, when other modules are operated, the voltage may be pulled down and the flash will not
		work normally. Therefore, it is necessary to enter deepsleep below 2.0V to ensure that the chip no
		longer runs related modules;
		When there is a low voltage situation, need to restore to 2.2V in order to make other functions normal,
		this is to ensure that the power supply voltage is confirmed in the charge and has a certain amount of
		power, then start to restore the function can be safer.*/
		//user_battery_power_check(VBAT_DEEP_THRES_MV);
	#endif
		deep_flag = analog_read(DEEP_ANA_REG0);
		ana_reg1_aaa = analog_read(USED_DEEP_ANA_REG1);
		pair_flag = ana_reg1_aaa & PAIR_ANA_FLG;


	#if (APP_FLASH_PROTECTION_ENABLE)
		app_flash_protection_operation(FLASH_OP_EVT_APP_INITIALIZATION, 0, 0);
		blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation); //register flash operation callback for stack
	#endif

		printf("\r\n****** Mouse sys on -> deep_flag=0x%01x, pair_flag=0x%01x ******\n", deep_flag, pair_flag);

    if (!deepRetWakeUp)
    {

        user_config_tzy();
        dev_info_idx = flash_info_load_aaa(CFG_DEVICE_MODE_ADDR, (u8 *)&flash_dev_info.dongle_id, SAVE_MAX_IN_FLASH);
        if (dev_info_idx < 0)
        {
            flash_dev_info.mode = USB_MODE;//RF_2M_2P4G_MODE;
			//flash_dev_info.mode = RF_1M_BLE_MODE;
            flash_dev_info.mast_id = 0;
            flash_dev_info.slave_mac_addr[0] = 0;
            flash_dev_info.slave_mac_addr[1] = 0;
            flash_dev_info.slave_mac_addr[2] = 0;
            flash_dev_info.slave_mac_addr[3] = 0;
            flash_dev_info.dongle_id = 0;
        }
		 //blc_app_loadCustomizedParameters();  //load customized freq_offset cap value

    
        switch_type_init();


#if DPI_SAVE_FLASH

        flash_dpi_info.idx = flash_info_load_aaa((MOUSE_DPI_ADDR), (u8 *)&flash_dpi_info.bt_dpi, 4);

        if (flash_dpi_info.idx < 0)
#endif
        {
            flash_dpi_info.bt_dpi = 1;//default
#if DPI_SAVE_FLASH
            flash_dpi_info.d24g_dpi = 1;//default
#endif
        }

				
#if (BLE_SNIFF_DEBUG)
				flash_dev_info.mode = RF_1M_BLE_MODE;
#endif
#if (ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG==1)
				flash_dev_info.mode = RF_1M_BLE_MODE;
#endif
		//flash_dev_info.mode=RF_1M_BLE_MODE;//for test mode
		
				fun_mode = flash_dev_info.mode;

    }
    #if(TELINK_BOARD)
	#if(BUTTON_FUN_ENABLE_AAA)
	three_mode_init();
	#endif
	#endif
    hw_init();
	if (deep_flag == POWER_ON_ANA_AAA)
    {
        power_on_tick = clock_time() | 1;
#if EMI_TEST_FUN_ENABLE_AAA
        emi_process();
#endif
    }
	else if (deep_flag == MUTI_DEVICE_REBOOT_ANA_AAA)
	{
		/* BLE multi-channel switch restart */
		device_switch_flag = 1;
	}
    printf("the deep flag is %d\n", deep_flag);
#if (BATT_CHECK_ENABLE)  //battery check must do before OTA relative operation
    user_batt_check_init();
#endif
 

    if (fun_mode == RF_1M_BLE_MODE)
    {
        rf_drv_ble_init();
        if (deepRetWakeUp)
        {
            user_init_deepRetn();
        }
        else
        {
            user_init_normal();
        }
    }
    else if(fun_mode == RF_2M_2P4G_MODE) 
    {
        rf_drv_private_2m_init();//ykq change
        if (!deepRetWakeUp)
        {
            d24_user_init();
        }
		
    }
	else if(fun_mode == USB_MODE)
	{
		usb_user_init();
		//usb_custom_init();
	}
    blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);


	flash_read_mid_uid_with_check((u32 *)&ic_inf[0], (u8*)&ic_inf[4]);
	printf("init ok\r\n");
    irq_enable();

    while (1)
    {
#if (MODULE_WATCHDOG_ENABLE)
        wd_clear(); //clear watch dog
#endif
#if DEBUG_TOGGLE_GPIO_ENABLE
        debug_loop_toggle();
#endif
        if (fun_mode == RF_1M_BLE_MODE)
        {
            main_loop();
        }
        else if(fun_mode == RF_2M_2P4G_MODE)
        {
            d24_main_loop();
        }
		else if(fun_mode == USB_MODE)  
		{ //usb mode
			usb_mode_loop(); //usb loop
		#if USB_UPGRATE_OTA //usb ota upgrate 
			usb_update_loop();//usb update loop
		#endif
		}
#if (BATT_CHECK_ENABLE)
        user_batt_check_proc();
#endif


        //deepRetWakeUp = 0;
        last_btn_value = btn_value;
#if UART_PRINT_DEBUG_ENABLE
        _attribute_data_retention_user  static u32 tick = 0;
        if (clock_time_exceed(tick, 3000000))
        {
            tick = clock_time();
            my_printf_aaa("poll mode=%x,ble_status=%x,fifoNum=%x,interval=%x,latency=%x,timeout=%x\r\n", fun_mode, ble_status_aaa, blc_ll_getTxFifoNumber(), bls_ll_getConnectionInterval(), bls_ll_getConnectionLatency(), bls_ll_getConnectionTimeout());
        }
#endif
    }
}

