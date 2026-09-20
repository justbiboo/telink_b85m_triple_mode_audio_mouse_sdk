/********************************************************************************************************
 * @file     main.c
 *
 * @brief    This is the source file for KMD SDK
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

#include "calibration.h"
#include "app_project_config.h"

#if USB_OTA_ENABLE_AAA
OUTPUT_DEV_INFO_AAA output_dev_info =
{
    0,//u32 bin_crc;
    0x01020304, //u32 fw version
};
#endif

/**
 * @brief       This function user init
 * @return      
 * @note        
 */
extern void user_init();


/**
 * @brief       This function is dongle main loop
 * @return      
 * @note        
 */
extern void main_loop (void);

/**
 * @brief       This function generate random nums
 * @param[in]   data	- buff store random nums
 * @param[in]   len	- random nums len
 * @return      
 * @note        
 */
void generateRandomNum(int len, unsigned char *data)
{
	int i;
	unsigned int randNums = 0;
    /* if len is odd */
	for (i=0; i<len; i++ ) {
		if( (i & 3) == 0 ){
			randNums = rand();
		}
		data[i] = randNums & 0xff;
		randNums >>=8;
	}
}

/**
 * @brief		This function serves to handle the interrupt of MCU
 * @param[in] 	none
 * @return 		none
 */
_attribute_ram_code_sec_noinline_ void irq_handler(void)
{
#if 0
	u32 src = reg_irq_src;
	
	if(src & FLD_IRQ_TMR1_EN)
	{
		irq_host_timer1();
		reg_tmr_sta = FLD_TMR_STA_TMR1;//write 1 to clear
	}
#endif

	unsigned short rf_irq_src = rf_irq_src_get();

   	if (rf_irq_src)
	{ //have RF interrupt
       	if (rf_irq_src & FLD_RF_IRQ_TX)
		{ //RF_TX
			//PIN_DEBUG_RF_TX_LEVEL(0);
			rf_tx_irq_handle(); //TX IQR task process
			rf_irq_clr_src(FLD_RF_IRQ_TX); //clear TX IQR
		}

		if (rf_irq_src & FLD_RF_IRQ_RX)
		{ //RF_RX
			//PIN_DEBUG_RF_RX_IRQ_TOGGLE;
			rf_rx_irq_handle(); //RX IQR task process
			rf_irq_clr_src(FLD_RF_IRQ_RX); //clear RX IQR
		}
    }
}

#if (_CHIP_IS_OTP_)
enum{
    FLD_VDD1V2      = BIT_RNG(3, 5),
};
#endif

/**
 * @brief		This is main function
 * @param[in]	none
 * @return      none
 */
int main (void)   //must on ramcode
{
	/* Use internal 32K RC crystal oscillator */
	blc_pm_select_internal_32k_crystal();

#if (MCU_CORE_B85)
	cpu_wakeup_init();
#elif (MCU_CORE_B87)
	cpu_wakeup_init(LDO_MODE, EXTERNAL_XTAL_24M);
#elif (MCU_CORE_B80) || MCU_CORE_B80B || (MCU_CORE_B89)
	/* Initialize the external 24M crystal oscillator */
	cpu_wakeup_init(INTERNAL_CAP_XTAL24M);
#endif

#if(MCU_CORE_B80 || MCU_CORE_B80B || MCU_CORE_B89)
	/* Stop 32K watch dog */
	wd_32k_stop();
#endif

#if (MCU_CORE_B85) || (MCU_CORE_B87)
	//Note: This function must be called, otherwise an abnormal situation may occur.
	//Called immediately after cpu_wakeup_init, set in other positions, some calibration values may not take effect.
	user_read_flash_value_calib();
#elif (MCU_CORE_B89)
	//Note: This function must be called, otherwise an abnormal situation may occur.
	//Called immediately after cpu_wakeup_init, set in other positions, some calibration values may not take effect.
	user_read_otp_value_calib();
#elif (MCU_CORE_B80 || MCU_CORE_B80B)
	//Note: This function must be called, otherwise an abnormal situation may occur.
	//Called immediately after cpu_wakeup_init, set in other positions, some calibration values may not take effect.
#if(PACKAGE_TYPE == OTP_PACKAGE)
	user_read_otp_value_calib();
#elif(PACKAGE_TYPE == FLASH_PACKAGE)
	user_read_flash_value_calib();
#endif
#endif
	//int deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();  //MCU deep retention wakeUp
    
	/* Initialize the system clock: 48MHz */
	clock_init(SYS_CLK);
	
#if 0//(_CHIP_IS_OTP_)
	analog_write(0x03, (analog_read(0x03) & (~FLD_VDD1V2))|(0x02<<3));
#endif

	/* Initialize the GPIO, if the GPIO state is not set, the default value is used */
	gpio_init(1);

#if HW_IS_FLASH
	/* Used to generate random numbers */
	random_generator_init();

	#if USB_OTA_ENABLE_AAA
		/* get firmware information */
		run_app_code();
	#endif
#endif

	/* get deep_flag */
	deep_flag = analog_read(SYS_DEEP_ANA_REG);
	printf("---deep_flag=%d.\r\n",deep_flag);

	/* Initialize dongle config information */
	custom_init();
	
	/* Initialize dongle hardware */
	user_init();
	
#if (FLASH_LOCK_ENABLE_AAA && (_CHIP_IS_OTP_==0))
	/* Initialize flash lock function */
	ui_ota_is_working = 0;
	flash_mid = flash_read_mid();
	flash_lock_handle(FLASH_LOCK_ALL_BLOCK);
#endif

#if (ALL_SRAM_CODE)
	/* Disable OTP read or write, can save power */
	otp_set_deep_standby_mode();
#endif

#if (MODULE_WATCHDOG_ENABLE)
	/* Start normal watch_dog */
    wd_set_interval_ms(WATCHDOG_INIT_TIMEOUT, CLOCK_SYS_CLOCK_1MS);
    wd_start();
#endif

	printf("---eaglet dongle init---\n");

#if MORE_PIPE_ENABLE
	printf("---PIPE 0 = 0x%01x%01x%01x%01x%01x.\n", read_reg8(0x80040c), read_reg8(0x80040b), read_reg8(0x80040a), read_reg8(0x800409), read_reg8(0x800400));
	printf("---PIPE 1 = 0x%01x%01x%01x%01x%01x.\n", read_reg8(0x800414), read_reg8(0x800413), read_reg8(0x800412), read_reg8(0x800411), read_reg8(0x800410));
	printf("---PIPE 2 = 0x%01x%01x%01x%01x%01x.\n", read_reg8(0x800414), read_reg8(0x800413), read_reg8(0x800412), read_reg8(0x800411), read_reg8(0x800418));
#endif

	while (1) {
	#if (MODULE_WATCHDOG_ENABLE)
		/* Clear normal watch_dog */
		wd_clear();
	#endif

	#if (MODULE_32K_WATCHDOG_ENABLE)
		/* Clear 32K watch_dog */
		wd_32k_clear();
	#endif
	
		/* Main loop poll */
		main_loop ();
		
	#if DEBUG_MODE
		static u32 loop_tick = 0;
		static u32 loop_cnt = 0;
		if (clock_time_exceed(loop_tick, 1000000))
		{
			loop_cnt ++;
			loop_tick = clock_time();
			printf("---loop_cnt = %d.\n", loop_cnt);
		}
	#endif
	}

	return 0;
}
