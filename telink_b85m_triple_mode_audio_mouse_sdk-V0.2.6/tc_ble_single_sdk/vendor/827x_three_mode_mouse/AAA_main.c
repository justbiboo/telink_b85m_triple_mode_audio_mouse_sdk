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

_attribute_data_retention_user u8 report_rate=4;			// ms
_attribute_data_retention_user u8 last_report_rate=0xff;	// ms
_attribute_data_retention_user int dev_info_idx;
_attribute_data_retention_user u8 pub_key[16] =
{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

_attribute_data_retention_user OUTPUT_DEV_INFO_AAA output_dev_info =
{
    0,//u32 bin_crc;
    0x01020304, //u8 fw version
    0,//u8 sensor_type;
    0,//u8 sensor_pd1;
    0,//u8 sensor_pd2;
    0,//u8 sensor_pd3;
};

unsigned int clock_sys_hz = 16000000;//16mhz
u32 usb_mode_start_tick;//mode start tick
void irq_handle_usb(void)
{
	#if(USB_MIC_ENABLE)
	if(reg_irq_src & FLD_IRQ_IRQ4_EN){
		usb_endpoints_irq_handler();
	}
	#endif
}

/**
 * @brief       This function handler irq
 * @return      
 * @note        
 */
_attribute_ram_code_ void irq_handler(void)
{
    if (fun_mode == RF_2M_2P4G_MODE) //2.4g mode
    {
        irq_handle_private_2m(); //pri 2m
    }
    else if(fun_mode == RF_1M_BLE_MODE) //led mode
    {
        irq_blt_sdk_handler(); //ble  handler
    }
	else if(fun_mode == USB_MODE)
	{
	#if(USB_MIC_ENABLE)
		irq_handle_usb();
	#endif
	}
}

//len must 4 bytes bei beisu  beacuse *(volatile u32 *)(s_addr + idx) must 4 bytes bei beisu

/**
 * @brief       This function load flash info
 * @param[in]   d_addr	- destination addr
 * @param[in]   len	-  len
 * @param[in]   s_addr	- source add
 * @return      
 * @note        
 */
int flash_info_load_aaa(u32 s_addr, u8 *d_addr,  int len)
{
    int idx;
    u32 buf;
    for (idx = 0; idx < (4096 - len); idx += len)
    {
        flash_read_page((u32)(s_addr + idx), 4, (u8 *)(&buf));//read flash to buff
        if (buf == U32_MAX) //no valid data
        {
            break;
        }
    }
    idx -= len; //idx -len
    if (idx < 0) 		// no binding
    {
        return idx;
    }
    flash_read_page((u32)(s_addr + idx), len, d_addr);//read len data to d_addr

    if (idx > 3000) 			//3k, erase flash
    {
        flash_erase_sector((u32)s_addr);//erase s_addr

        sleep_us(10);//sleep 10 us

        flash_write_page((u32)s_addr, len, d_addr);//write flash
        idx = 0; //index 0
    }
    return idx;
}



/**
 * @brief       This function config user need 
 * @return      
 * @note        
 */
void user_config()
{
    random_generator_init();  //this is must
    flash_read_page(CFG_ADR_MAC, sizeof(custom_cfg_t), (u8 *)&user_cfg.dev_mac);
    if (user_cfg.dev_mac == U32_MAX) //no mac
    {
        generateRandomNum(4, (u8 *) &user_cfg.dev_mac); //get rand dev mac
        flash_write_page(CFG_ADR_MAC, 4, (u8 *)&user_cfg.dev_mac); //write mac to mac addr
    }

    if (user_cfg.paring_tx_power == U8_MAX) // no set pair tx power
    {
        user_cfg.paring_tx_power = DEFAULT_PAIR_TX_POWER; //defualt pair tx power
    }
    if (user_cfg.tx_power == U8_MAX) // no set tx power
    {
        user_cfg.tx_power = DEFAULT_NORMAL_TX_POWER; //defualt tx power
    }
    if (user_cfg.emi_tx_power == U8_MAX)//no set emi
    {
        user_cfg.emi_tx_power = DEFAULT_EMI_TX_POWER;//emi tx power defualt
    }

    if ((user_cfg.sensor_direct == U8_MAX))//no set sensor dir
    {
		
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_6; //SENSOR_DIRECTION_CLOCK_6;//def 6 clock
    }
    else if (user_cfg.sensor_direct == 0)//0
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_3;//3clock
    }
    else if (user_cfg.sensor_direct == 1)//1
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_6;//6clock
    }
    else if (user_cfg.sensor_direct == 2)//2
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_9;//9clock
    }
    else if (user_cfg.sensor_direct == 3)//3
    {
        user_cfg.sensor_direct = SENSOR_DIRECTION_CLOCK_12;//12clock
    }
     //if(user_cfg.internal_cap!=0)
     //{
	 	//blc_app_setExternalCrystalCapEnable(1);
    // }
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
        if (user_cfg.device_name[i] != 0xff) //valid did name
        {
            device_name_len++; //did name len ++
        }
        else
        {
            break;
        }
    }

}

int deepRetWakeUp;


/**
 * @brief       This function set power led
 * @param[in]   inx	- index
 * @param[in]   pin	- pin
 * @return      
 * @note        
 */
void powerLED(u32 pin, u8 inx)
{
	gpio_set_output_en(pin, 1);//output en
	for(int i = 0; i < inx; ++i) //loop inx
	{
		gpio_write(pin, 1);//on
		cpu_sleep_wakeup(SUSPEND_MODE, PM_WAKEUP_TIMER, clock_time()+ 300*CLOCK_SYS_CLOCK_1MS);//300ms suspend
		gpio_write(pin, 0);//off
		cpu_sleep_wakeup(SUSPEND_MODE, PM_WAKEUP_TIMER, clock_time()+ 300*CLOCK_SYS_CLOCK_1MS);//300ms suspend
	}
	gpio_set_output_en(pin, 0);//output dis
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

/**
 * @brief       This is main function 
 * @return      
 * @note        
 */
_attribute_ram_code_ int main(void)     				//must run in ramcode
{
    blc_pm_select_internal_32k_crystal();//32k crystal
 
#if(MCU_CORE_TYPE == MCU_CORE_825x)//8258
	cpu_wakeup_init();//init
#elif(MCU_CORE_TYPE == MCU_CORE_827x)//8278
	//cpu_wakeup_init(DCDC_MODE, EXTERNAL_XTAL_24M);//dcdc 24m init
	cpu_wakeup_init(LDO_MODE, EXTERNAL_CAP_XTAL24M);
#endif

    deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();//deepret wakeup

    gpio_init(!deepRetWakeUp);    				//analog resistance will keep available in deepSleep mode, so no need initialize again
	
    report_rate = analog_read(DEEP_ANA_REG2);	//default 0x0f
    if(report_rate == 0x0f)//no set report rate
	{
		report_rate = 4; //set 8
	}

	clock_init(SYS_CLK_48M_Crystal);//48m crystal
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
		user_battery_power_check(VBAT_DEEP_THRES_MV);
	#endif


	#if (APP_FLASH_PROTECTION_ENABLE)
		app_flash_protection_operation(FLASH_OP_EVT_APP_INITIALIZATION, 0, 0);
		blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation); //register flash operation callback for stack
	#endif
	u32 offset = ota_program_bootAddr - ota_program_offset;//off set
	u32 bin_size = 0;//bin size
	flash_read_page(offset + 0x18, 4, (u8 *)&bin_size);//read bin size
	flash_read_page(offset + (bin_size - 4), 4, (u8 *)&output_dev_info.bin_crc);//read bin crc
	flash_read_page(offset + 2, 4, (u8 *)&output_dev_info.fw_version);//read fw_version

	//my_printf_aaa("ota_program_offset=%x\r\n",ota_program_offset);
	//my_printf_aaa("bin_crc=%x\r\n",output_dev_info.bin_crc);

	if (!deepRetWakeUp)//not deepret wak
	{
		

		deep_flag = analog_read(DEEP_ANA_REG0);//read deep flag
		ana_reg1_aaa = analog_read(USED_DEEP_ANA_REG1);//read ana reg1
		pair_flag = ana_reg1_aaa & PAIR_ANA_FLG;//get pair flag

		user_config();//user config

		dev_info_idx = flash_info_load_aaa(CFG_DEVICE_MODE_ADDR, (u8 *)&flash_dev_info.dongle_id, SAVE_MAX_IN_FLASH);//read flash info
		if (dev_info_idx < 0)//no binding
		{
			flash_dev_info.mode = RF_2M_2P4G_MODE;//RF_1M_BLE_MODE;//USB_MODE;//RF_2M_2P4G_MODE;//24g
			flash_dev_info.mast_id = 0; //mast id 0
			flash_dev_info.slave_mac_addr[0] = 0; //mac 0
			flash_dev_info.slave_mac_addr[1] = 0;
			flash_dev_info.slave_mac_addr[2] = 0;
			flash_dev_info.slave_mac_addr[3] = 0;
			flash_dev_info.dongle_id = 0; //dongle 0
		}

		//mode_gpio_check(); //check mode gpio
		fun_mode = flash_dev_info.mode; //fun mode update

#if (BLE_SNIFF_DEBUG)
		fun_mode = RF_1M_BLE_MODE;//ble mode
#endif

#if ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG
        fun_mode = RF_2M_2P4G_MODE;//24g mode
#endif

#if DPI_SAVE_FLASH
		flash_dpi_info.idx = flash_info_load_aaa((MOUSE_DPI_ADDR), (u8 *)&flash_dpi_info.bt_dpi, 8);//get dpi value
    	if (flash_dpi_info.idx < 0)//no dpi
#endif
    	{
        	flash_dpi_info.bt_dpi = 1;//set 1
#if DPI_SAVE_FLASH
		    flash_dpi_info.d24g_dpi = 1;//set 1
#endif
		    flash_dpi_info.usb_dpi = 1;	//set 1
    	}
    }

	hw_init();//init hw
#if( 0)
	battery_led_on(0);
	gpio_write(PIN_BLE2_LED ,0);
	gpio_write(PIN_LOGO_LED,0);
	gpio_write(PIN_BLE_LED ,0);
	gpio_write(PIN_24G_LED,0);
	//gpio_write(PIN_SENSOR_CS,0);
#endif
	my_printf_aaa("---deep_flag=%d.\n",deep_flag);//debug deep sleep reason
	
	if (deep_flag == POWER_ON_ANA_AAA) //power on
	{
		power_on_tick = clock_time() | 1; //power on tick now
#if EMI_TEST_FUN_ENABLE_AAA  //emi en
		emi_process(); //emi proc
#endif
	}
#if D24G_OTA_ENABLE_AAA  //ota en
	else if (deep_flag == D24G_OTA_ENABLE_REBOOT_ANA_AAA)//ota reboot
	{
		d24g_ota_status = 1;	//Enter 2.4g ota mode
	}
#endif
	else if(deep_flag == MODE_CHANGE_REBOOT_ANA_AAA)
	{
		mode_change_flag = 1;
	}
	
	if (fun_mode == RF_1M_BLE_MODE)	 {//1m ble
		rf_drv_init(RF_MODE_BLE_1M);//rf frv init ble 1m

		if (deepRetWakeUp) {//dep ret
			user_init_deepRetn();//deep ret init
		} else {
			user_init_normal();//normal init
		}
	} else if(fun_mode == RF_2M_2P4G_MODE) {//24g
		rf_drv_private_2m_init(); //2m init
		
		if (!deepRetWakeUp) { //no deep ret
			d24_user_init(); //normal init
		}
	}
	else if(fun_mode == USB_MODE)
	{
		usb_user_init();
		//usb_custom_init();
	}

    blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K); //set deepsleep ret sram
#if USE_EXTERNAL_CAP
	blc_app_setExternalCrystalCapEnable(1);
#endif
	//blc_app_loadCustomizedParameters();//load custom para
	irq_enable();//irq en
	#if(0)
	unsigned char inbuf[57]={
0xad, 0x00 , 0x00 , 0x8f , 0x20 , 0x00 , 0x00 , 0x00 , 0x7f , 0xdb, 0x55 ,0x5f , 0xf6 ,

0xd5 , 0x57 , 0xfd , 0xb5 , 0x56 , 0x16 , 0x51, 0x55 ,0xc8 , 0x2a , 0x55 , 0x6b , 0x44 , 0x95 , 0x57 , 0xb5 ,

0x49,0x45 ,0xce , 0x89 , 0x59 , 0x6b , 0x1c , 0x55 , 0x57 , 0xf6 , 0xd5, 0x55 ,0x35 ,0xa4 ,0x55 ,0x31 ,

0x71 ,0x55 ,0x3e ,0xd2 ,0x55 ,0x51 ,0x36 ,0xd5 ,0x63 ,0xed ,0xa5 ,0x14
};
  u8 outbuf[240];
  size_t len=0;
      sbc_decode(inbuf, 57,outbuf, 240, &len);
	  for(int i =0; i<240;i++){
	  printf("%d,",outbuf[i]);
	  	}
	#endif
	//my_printf_aaa("---mouse init,fun_mode=%d.\n",fun_mode);// show fun mode



	while (1)
	{
	#if (MODULE_WATCHDOG_ENABLE)
		wd_clear(); //clear watch dog
	#endif

	#if DEBUG_TOGGLE_GPIO_ENABLE //debug gpio en
		debug_loop_toggle(); //debug loop 
	#endif
		if (fun_mode == RF_1M_BLE_MODE)	 { //1m ble mode
			main_loop(); //main loop
		} else if(fun_mode == RF_2M_2P4G_MODE)	{ //24g mode
			d24_main_loop();//24g main loop
		#if D24G_OTA_ENABLE_AAA //ota en
			d24g_ota_loop(); //ota loop
		#endif
		} else if(fun_mode == USB_MODE)  { //usb mode
			usb_mode_loop(); //usb loop
		#if USB_UPGRATE_OTA //usb ota upgrate 
			usb_update_loop();//usb update loop
		#endif
		}
	
	#if (BATT_CHECK_ENABLE) //batt check en
		user_batt_check_proc();//bett check
	#endif

        last_btn_value = btn_value;//update btn value
	}
}

