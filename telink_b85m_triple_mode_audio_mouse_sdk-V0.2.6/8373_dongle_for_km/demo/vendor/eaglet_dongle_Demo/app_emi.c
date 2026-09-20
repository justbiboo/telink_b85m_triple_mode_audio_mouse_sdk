/********************************************************************************************************
 * @file     AAA_emi.c
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

#include "app_project_config.h"
#include "pri2m_emi.h"

#if (EMI_TEST_FUN_ENABLE_AAA)
#define RF_MODE  RF_MODE_PRIVATE_2M
u8 pwr;
u8 chn_idx = 0;
u8 test_mode_sel = 0;

typedef enum 
{
	CARRIER_MODE=0,
	CARRIER_DATA_MODE=1,
	RX_MODE=2,
	TX_MODE=3,
}EMI_MODE;

#define DATA_TYPE 0 //0:pbrs9 	1:0xf0	 2:0x55

u8 emi_flg = 0;

/**
 * @brief	emi_init
 * @param	none
 * @return	none
 */
void emi_init()
{
	//unsigned char chnidx=0;
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

/**
 * @brief	EMI test loop
 * @param	none
 * @return	none
 */
void usb_host_cmd_proc(void)
{
	extern u8 host_cmd[8];
	u8 tbl_chn[4] = {05, 30, 70, 80};
	u8 rf_chn;
	u8 dongle_host_cmd1;

	if ((host_cmd[0] == 0x5) && (host_cmd[2] == 0x3))
	{
		host_cmd[0] = 0; //clear CMD

		dongle_host_cmd1 = host_cmd[1];

		if ((dongle_host_cmd1 > 12) && (dongle_host_cmd1 < 16)) //soft paring
		{

		}
		else if ((dongle_host_cmd1 > 0) && (dongle_host_cmd1 < 13)) //1-12:
		{
			irq_disable();

			//reg_tmr_ctrl &= ~FLD_TMR1_EN; //disable TIME1

		#if (MODULE_WATCHDOG_ENABLE)
			wd_stop(); //stop normal watch dog
		#endif
		
		#if (MODULE_32K_WATCHDOG_ENABLE)
			wd_32k_stop(); //stop 32K watch dog
		#endif

			emi_init();

			rf_set_tx_rx_off();

			emi_flg = 1;

			chn_idx = (dongle_host_cmd1 - 1) / 4; //get channel
			test_mode_sel = (dongle_host_cmd1 - 1) % 4; //get mode
			rf_chn = tbl_chn[chn_idx]; //get frequency point
			pwr = user_config.tx_power_emi; //get EMI TX power

			if (test_mode_sel == CARRIER_MODE) //carri
			{
				app_rf_emi_single_tone(pwr, rf_chn);
			}
			else if (test_mode_sel == CARRIER_DATA_MODE) //cd
			{
				app_rf_emi_tx_continue_setup(RF_MODE, pwr, rf_chn, DATA_TYPE);
			}
			else if (test_mode_sel == RX_MODE) //rx
			{
				app_rf_emi_rx(RF_MODE, rf_chn);
			}
			else if (test_mode_sel == TX_MODE) //tx
			{
				app_rf_emi_tx_burst_setup(RF_MODE, pwr, rf_chn, DATA_TYPE);
			}
		}
	}

	if (emi_flg)
	{
		if (test_mode_sel == CARRIER_DATA_MODE) //cd
		{
			while(!host_cmd[0])
			{
				app_rf_continue_mode_run_for_dongle();

				while(read_reg8(0x41c) & 0x1)
				{
					usb_handle_irq();

					if(host_cmd[0])
					{
						write_reg32(0x41c, 0);
						break;
					}
				}
			}
		}
		else if (test_mode_sel == RX_MODE) //rx
		{
			app_rf_emi_rx_loop();
		}
		else if (test_mode_sel == TX_MODE) //rx
		{
			app_rf_emi_tx_burst_loop(RF_MODE, DATA_TYPE);
		}
	}
}
#endif


