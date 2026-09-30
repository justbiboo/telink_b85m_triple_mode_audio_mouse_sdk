/********************************************************************************************************
 * @file     aaa_public.c
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
#if MUTI_DEVICE_SWITCH_DEBUG
	#define COMB_BTN_PAIR  (MS_BTN_LEFT|MS_BTN_MIDDLE|MS_BTN_RIGHT)//(MS_BTN_LEFT)
#else
#define COMB_BTN_PAIR  (MS_BTN_LEFT|MS_BTN_MIDDLE|MS_BTN_RIGHT)
#endif
#if DOUBLE_CLICK_LEFT_FUN_ENABLE

    _attribute_data_retention_user static u32 double_click_left_tick;
    _attribute_data_retention_user static u8 has_double_click_cnt;
    _attribute_data_retention_user u8 has_double_click_left = 0;
    #define DOUBLE_CLICK_LEFT_INTERVAL  40000

#endif
u8 auto_draw_flag=0;
_attribute_data_retention_user  u8 switch_type=TWO_SPEED_SWITCH;
_attribute_data_retention_user  u8 active_disconnect_reason=0;

_attribute_data_retention_user  u32 power_on_tick = 0;
_attribute_data_retention_user  u8 device_name_len = 0;
_attribute_data_retention_user  u8 connect_ok = 0;


_attribute_data_retention_user custom_cfg_t   user_cfg;
_attribute_data_retention_user u8 fun_mode = RF_1M_BLE_MODE;
_attribute_data_retention_user FLASH_DEV_INFO_AAA flash_dev_info;
_attribute_data_retention_user FLASH_DPI_INFO_AAA flash_dpi_info;


_attribute_data_retention_user u8 suspend_wake_up_enable = 1;
_attribute_data_retention_user u8 deep_flag = POWER_ON_ANA_AAA;
_attribute_data_retention_user u8 pair_flag = 0;
_attribute_data_retention_user u8 ana_reg1_aaa = 0;
_attribute_data_retention_user u8 has_been_paired_flag = 0;


_attribute_data_retention_user u8 has_new_report_aaa;
_attribute_data_retention_user u8 has_new_key_event = 0;
_attribute_data_retention_ u8 device_switch_flag = 0;


_attribute_data_retention_user u8 combination_flag = 0;
_attribute_data_retention_user TX_FIFO_AAA_STRUCT tx_fifo_aaa;
_attribute_data_retention_user  int ui_mtu_size_exchange_req = 0;
_attribute_data_retention_user	u8	ui_mic_enable = 0;
_attribute_data_retention_user	u8 has_mic_data_flag = 0;
_attribute_data_retention_user	u8 appdata[8]={0};
u32 mic_duration = 60;


_attribute_data_retention_user u16 btn_value = 0;
_attribute_data_retention_user u16 last_btn_value = 0;
_attribute_data_retention_user u8 mouse_btn_in_sensor;
//u8 last_mosue_btn_in_sensor;
_attribute_data_retention_user mouse_data_t ms_data;
_attribute_data_retention_user mouse_data_t ms_buf;

_attribute_data_retention_user u8 wheel_pre = 0;
_attribute_data_retention_user u8 wheel_status;
u8  usb_device_status=0;
u8  usb_device_status_last=0;
u8  need_enter_suspend_flag = 0;
u32 need_enter_suspend_tick = 0;

u8 keyboard_buf_aaa[BUF_SIZE_KEYBOARD_AAA] = {0};
#if CONSUMER_FUN_ENABLE_AAA
u8 consumer_buf_aaa;
u8 consumer_buf_last_aaa;
u8 record_flag;
const u16 consumer_list[]={
    0x221,		//0xa3 C_WWW_SEARCH		
    0x223,		//0xa4 C_WWW_HOME
    0x224,		//0xa5 C_WWW_BACKWARD
    0x225,		//0xa6 C_WWW_FORWARD
    0x226,		//0xa7 C_WWW_STOP
    0x227,		//0xa8 C_WWW_REFRESH
    0x22A,		//0xa9 C_MY_FAVORITE
    0x183,		//0xaa C_MEDIA_SELECT
    0x18A,		//0xab C_EMAIL
    0x192,		//0xac C_CALCULATOR
    0x194,		//0xad C_MY_COMPUTER
    0xB5,		//0xae C_NEXT_TRACK
    0xB6,		//0xaf C_PRE_TRACK
    0xB7,		//0xb0 C_STOP
    0xCD,		//0xb1 C_PLAY_PAUSE
    0xE2,		//0xb2 C_MUTE	
    0xE9,		//0xb3 C_VOL_INC
    0xEA,		//0xb4 C_VOL_DEC	
    0x00,		//0xb5 telink
    0x22D,		//0xb6 USAGE ZOOM IN
    0x22E,		//0xb7 USAGE ZOOM OUT
    0x236,		//0xb8 USAGE PAN LEFT
    0x237,		//0xb9 USAGE PAN RIGHT
    0x30B,		//0xba C_BRIGHT_INC
    0x30A,		//0xbb	C_BRIGHT_DEC
    0xB8,		//0Xbc	c_rject
    0x30,		//0Xbd	C_POWER 		
    0x19E,		//0Xbe	C_TERMINAL_LOCK 
};
#else
	u32 consumer_buf_aaa;
	u32 consumer_buf_last_aaa;
#endif

void flash_write_page_user(unsigned long addr, unsigned long len, unsigned char *buf)
{

    u16 write_cnt = len / 8;
    u8 last_write_len = len % 8;

    for(int i = 0; i < write_cnt; i++) {
        flash_write_page((addr + 8 * i), 8, (buf + 8 * i));
        sleep_us(20);
    }

    if (last_write_len > 0)
        flash_write_page((addr + 8 * write_cnt), last_write_len, (buf + 8 * write_cnt));
}

void save_dev_info_flash()
{
#if (ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG==0)

    dev_info_idx += SAVE_MAX_IN_FLASH;
	if(dev_info_idx>4000)
	{
		flash_erase_sector(CFG_DEVICE_MODE_ADDR);
		dev_info_idx=0;	
	}
		

    flash_write_page_user(CFG_DEVICE_MODE_ADDR + dev_info_idx, SAVE_MAX_IN_FLASH, (u8 *)&flash_dev_info.dongle_id);
    //sleep_us(50000);
#endif
}

#if((TL_AUDIO_MODE == TL_AUDIO_RCU_MSBC_HID))
u32 	key_voice_pressTick = 0;
u32 	audio_stick = 0;
_attribute_data_retention_	u8  audio_start = 0;

u8 d24g_mic_data[64]={0};
u8 index_d24g_mic[33]={0};


/**
 * @brief      the func serve to init amic
 * @param[in]  none
 * @return     none
 */
void amic_gpio_reset (void)
{
	gpio_set_func(GPIO_AMIC_BIAS, AS_GPIO);
	gpio_set_input_en(GPIO_AMIC_BIAS, 0);
	gpio_set_output_en(GPIO_AMIC_BIAS, 1);
	gpio_write(GPIO_AMIC_BIAS, 0);

	gpio_set_func(GPIO_AMIC_SP, AS_GPIO);
	gpio_set_input_en(GPIO_AMIC_SP, 0);
	gpio_set_output_en(GPIO_AMIC_SP, 1);
	gpio_write(GPIO_AMIC_SP, 0);

#if (MCU_CORE_TYPE == MCU_CORE_825x)
	gpio_set_func(GPIO_AMIC_SN, AS_GPIO);
	gpio_set_input_en(GPIO_AMIC_SN, 0);
	gpio_set_output_en(GPIO_AMIC_SN, 1);
	gpio_write(GPIO_AMIC_SN, 0);
#endif

}

/**
 * @brief      for open the audio and mtu size exchange
 * @param[in]  en   0:close the micphone  1:open the micphone
 * @return     none
 */
void ui_enable_mic (int en)
{
	ui_mic_enable = en;

	//AMIC Bias output
	gpio_set_output_en (GPIO_AMIC_BIAS, en);
	#if (MCU_CORE_TYPE == MCU_CORE_827x)
		gpio_set_data_strength (GPIO_AMIC_BIAS, en);
	#endif
	gpio_write (GPIO_AMIC_BIAS, en);

	if(en){  //audio on

		///////////////////// AUDIO initialization///////////////////
		//buffer_mic set must before audio_init !!!
		audio_config_mic_buf (  (u16*)buffer_mic, TL_MIC_BUFFER_SIZE);
		buffer_mic_pkt_rptr = buffer_mic_pkt_wptr = 0;
	
		#if (MCU_CORE_TYPE == MCU_CORE_827x)
			//Amic config
			audio_set_mute_pga(0);  ////enable audio need follow this step: 1 enable bias; 2 disable mute_pga;
			gpio_set_output_en(GPIO_AMIC_SP, 0);
			audio_amic_init(AUDIO_16K);							  //3 init; 4 delay about 17ms; 5 enable mute_pga.
		#elif (MCU_CORE_TYPE == MCU_CORE_825x)
			gpio_set_output_en(GPIO_AMIC_SP, 0);
			gpio_set_output_en(GPIO_AMIC_SN, 0);
			audio_amic_init(AUDIO_16K);
		#endif


		#if (IIR_FILTER_ENABLE)
			//only used for debugging EQ Filter parameters, removed after mass production
			extern void filter_setting();
			filter_setting();
		#endif
	}
	else{  //audio off

			#if (MCU_CORE_TYPE == MCU_CORE_827x)
				audio_codec_and_pga_disable();	//power off codec and pga
			#elif (MCU_CORE_TYPE == MCU_CORE_825x)
				adc_power_on_sar_adc(0);   //power off sar adc
			#endif
				amic_gpio_reset();

		buffer_mic_pkt_rptr = buffer_mic_pkt_wptr = 0;
	}
	#if ((!BLE_DMIC_ENABLE) && (BATT_CHECK_ENABLE))
		battery_set_detect_enable(!en);
	#endif

}
_attribute_ram_code_ void task_audio (void)
{
#if(APP_24G_AUDIO_EN)
	///////////////////////////////////////////////////////////////
	//PIN_DEBUG_ENCODE_TIME_LEVEL(1);
	proc_mic_encoder ();
	//PIN_DEBUG_ENCODE_TIME_LEVEL(0);
	static u8 i = 0;

	//////////////////////////////////////////////////////////////////

	if(my_fifo_number(&d24g_txfifo)<9)
	{
		int* pmic = mic_encoder_data_buffer ();
		if (pmic)					//around 3.2 ms @16MHz clock
		{

			#if(TL_AUDIO_MODE == TL_AUDIO_RCU_MSBC_HID)
			memcpy((u8*)&d24g_mic_data[0],(u8*)&pmic[0],57);
			int j = 0;
			while(j<2)
			{
				index_d24g_mic[0]= j;
				if(j==0)
				{
					memcpy((u8*)&index_d24g_mic[1],(u8*)&d24g_mic_data[0],29);
					if(my_fifo_push_app(&d24g_txfifo, (u8*)&index_d24g_mic[0], 30, MIC_DATA_CMD)==0)
					{
					 j =1;
					}
				}
				else if(j==1)
				{
					memcpy((u8*)&index_d24g_mic[1],(u8*)&d24g_mic_data[29],28);
					if(my_fifo_push_app(&d24g_txfifo, (u8*)&index_d24g_mic[0], 29, MIC_DATA_CMD)==0)
					{
					 	j =2;
					}
				}
				printf("push mic data now %d\n",j);
			}
			buffer_mic_pkt_rptr++;
			#else
			buffer_mic_pkt_rptr++;
			#endif
		}
	}
	else
	{
		printf("mic tx fifo over 10\n");
	}

#endif


}
void proc_audio(void){
#if(APP_24G_AUDIO_EN)	
	if(ui_mic_enable){
		//if(audio_start || (audio_stick && clock_time_exceed(audio_stick, 17*1000))){// for 8278
		if(audio_start || (audio_stick && clock_time_exceed(audio_stick, 1*1000))){
			audio_start = 1;
			if(clock_time_exceed(audio_stick, mic_duration*1000*1000))
			{
				ui_enable_mic(0);
				reset_idle_status();
				printf("mic open too long so close\n");
			}
			else
			task_audio();
		}
	}
	else{
		audio_start = 0;
	}
#endif
}


#endif

void usb_custom_init()
{

	//usbhw_set_eps_en(FLD_USB_EDP8_EN|FLD_USB_EDP1_EN|FLD_USB_EDP2_EN|FLD_USB_EDP4_EN);
	usb_set_pin_en();
	usb_init_interrupt();
	//reg_usb_ep3_buf_addr =0xc0;
	//reg_usb_ep4_buf_addr = 0x60;
	reg_usb_ep6_buf_addr =0xa0;
	reg_usb_ep7_buf_addr =0xc0;
	reg_usb_ep5_buf_addr = 0xb0;
	reg_usb_ep8_buf_addr =0xd0;
	irq_enable();

	//deepsleep_dp_dm_gpio_low_wake_enable();


	//usb_set_pin_en();//dp pull up
	
}

u8 tmp_mic_data[ADPCM_PACKET_LEN];

/**
 * @brief      audio task in loop for encode and transmit encode data
 * @param[in]  none
 * @return     none
 */
 //u8* p_usb_mic = NULL;
_attribute_ram_code_ void task_audio_usb (void)
{
#if(APP_24G_AUDIO_EN)

	///////////////////////////////////////////////////////////////
	proc_mic_encoder ();

	//////////////////////////////////////////////////////////////////
	//if (blc_ll_getTxFifoNumber() < 12)//8 + audio_send_idx
	{

		int *p_usb_mic = mic_encoder_data_buffer ();
		
		if (p_usb_mic)					//around 3.2 ms @16MHz clock
		{
			//att_mic_rcvd = 1;
			#if(TL_AUDIO_MODE == TL_AUDIO_RCU_MSBC_HID)
			memcpy (tmp_mic_data, (u8*)&p_usb_mic[0], 57);
			u8 msbc_data[63]= {1,57,0,0};
			memcpy(&msbc_data[2],(u8*)&tmp_mic_data[0],57);
			if( usb_app_hid_report(0x0c,(u8*)&msbc_data[0],63))
			{
				buffer_mic_pkt_rptr++;
				printf("send usb mic ok\n");
			}
			//i = 0;
			#else
			buffer_mic_pkt_rptr++;
			#endif
			//i = 0;
		}
			

		

	}
#endif
}

/**
 * @brief      audio proc in main loop
 * @param[in]  none
 * @return     none
 */
void proc_audio_usb(void){
#if(APP_24G_AUDIO_EN)	
	if(ui_mic_enable){
		if(audio_start || (audio_stick && clock_time_exceed(audio_stick, 17*1000))){// for 8278
			audio_start = 1;
			if(clock_time_exceed(audio_stick, 2*1000)&&(allow_mic_send))
			{
			   task_audio_usb();
			   audio_stick = clock_time();
			}
		}
	}
	else{
		audio_start = 0;
	}
#endif
}

/**
 * @brief      audio task in loop for encode and transmit encode data
 * @param[in]  none
 * @return     none
 */
 //u8 *p_ble_mic = NULL;
_attribute_ram_code_ void task_audio_ble (void)
{
#if(APP_24G_AUDIO_EN)

	///////////////////////////////////////////////////////////////
	proc_mic_encoder ();

	//////////////////////////////////////////////////////////////////
	if (blc_ll_getTxFifoNumber() < 9)//8 + audio_send_idx
	{

		
		int* p_ble_mic = mic_encoder_data_buffer ();
		
		if (p_ble_mic)					//around 3.2 ms @16MHz clock
		{

			{
			#if(HID_REPORT_MIC_EN)
			u8 mic_usb[63] = {0x01,0x39,0};
			memcpy(&mic_usb[2],(u8*)p_ble_mic,ADPCM_PACKET_LEN);			
			if(BLE_SUCCESS == bls_att_pushNotifyData(HID_MIC_REPORT_INPUT_DP_H, (u8*)&mic_usb[0], 63))
			{
				buffer_mic_pkt_rptr++;
			}
			#else
			if(BLE_SUCCESS == bls_att_pushNotifyData(MY_DATA_INPUT_DP_H, (u8*)p_ble_mic, ADPCM_PACKET_LEN))
			{
				buffer_mic_pkt_rptr++;
			}
			#endif
			}
		}
	}
#endif
}

/**
 * @brief      audio proc in main loop
 * @param[in]  none
 * @return     none
 */
void proc_audio_ble(void){
#if(APP_24G_AUDIO_EN)	
	if(ui_mic_enable){
		if(audio_start || (audio_stick && clock_time_exceed(audio_stick, 17*1000))){// for 8258
			audio_start = 1;
			if(blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
			{
			    if(clock_time_exceed(audio_stick, mic_duration*1000*1000))
			    {
			    	ui_enable_mic (0);
					reset_idle_status();
					printf("mic open too long so close\n");
			    }
				else
				{
					task_audio_ble();
				}
			}
			else
			{
				ui_enable_mic (0);
				printf("no connect so close mic\n");
			}
		}
	}
	else{
		audio_start = 0;
	}
#endif
}

int d24g_app_data(u8* appdata,u8 len)
{
    int length = len;


	if(1)
	{
		if(my_fifo_push_app(&d24g_txfifo, appdata, length, APP_DATA_CMD)==0)
			{
			printf("send app data ok \n");
			 return 1;
			}
		else
			{
			 printf("send app data fail\n");
			return 0;
			}
	}
	else
	{
		printf("app data fifo over 8\n");
		return 0;
	}


	
}



int mouse_connect()
{
    int status = (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)|(fun_mode==USB_MODE)|(connect_ok);
	return status;
}

void voice_key_check(void)
{
#if (BIBOO_UX_K5_CTRL_WIN_MIC)
	/*BIBOO UX: in BLE mode K5 is a push-to-talk key (press = Ctrl+Win modifiers
	 * + mic on, release = modifiers released + mic off), handled by the edge
	 * detector in button_process_telink_v21a(). Bypass the original toggle
	 * logic here (press on / press again off); 2.4G mode keeps it unchanged.*/
	if (fun_mode == RF_1M_BLE_MODE)
	{
		return;
	}
#endif

	if((last_btn_value == MS_BTN_VOICE) &&(btn_value != MS_BTN_VOICE) && ui_mic_enable){

		ui_enable_mic(0);
		printf("short voice key press off\n");
		reset_idle_status();
		appdata[0]=0x03;
		if(fun_mode == RF_2M_2P4G_MODE)
		{
		  d24g_app_data(appdata,7);
		}

	}
	else if((last_btn_value == MS_BTN_VOICE)&&!ui_mic_enable&&((btn_value&MS_BTN_VOICE)!=MS_BTN_VOICE))
	{
		appdata[0]=0x01;
		if(fun_mode == RF_2M_2P4G_MODE)
		{
		  d24g_app_data(appdata,7);
		}
		audio_stick = clock_time()|1;
		ui_enable_mic(1);
		printf("short voice key press on\n");

	}

}
void app_data_send( u8 data)
{
		appdata[0]=data;
		if(fun_mode == RF_2M_2P4G_MODE)
		{
		  d24g_app_data(appdata,7);
		}
		else if(fun_mode == RF_1M_BLE_MODE)
		{
			bls_att_pushNotifyData(MY_DATA_INPUT_DP_H, (u8*)&appdata[0], 7);
		}
		else
		{
			u8 temp_data[63] = {0};
			temp_data[0] = data;
			if( usb_app_hid_report(0x0c,(u8*)&temp_data[0],63))
			{
				printf("usb send app data with %d ok\n", temp_data[0]);
			}
		}
}

#if BUTTON_FUN_ENABLE_AAA

_attribute_data_retention_user  u32 btn_tick = 0;
_attribute_data_retention_user 	u32 btn_pins[BTN_NUM_AAA] = BTN_MATRIX;


void btn_set_wakeup_level_suspend(u8 enable)
{
    for (u8 i = 0; i < BTN_NUM_AAA; i++)
    {
        if (gpio_read(btn_pins[i]))
        {
            cpu_set_gpio_wakeup(btn_pins[i], 0, enable); //low wakeup suspend

        }
        else
        {
            cpu_set_gpio_wakeup(btn_pins[i], 1, enable);
        }
    }

}


void btn_set_wakeup_level_deep()
{
    for (u8 i = 0; i < BTN_NUM_AAA; i++)
    {
        if (gpio_read(btn_pins[i]))
        {
            cpu_set_gpio_wakeup(btn_pins[i], 0, 1); //low wakeup suspend
            //connected_idle_time_count_ykq=0;
        }
        else
        {
            cpu_set_gpio_wakeup(btn_pins[i], 1, 1);
        }
    }

}


static void btn_init_hw()
{
#if 0
    for (u8 i = 0; i < BTN_NUM_AAA; i++)
    {

        gpio_set_input_en(btn_pins[i], 1);
        gpio_set_output_en(btn_pins[i], 0);
        gpio_setup_up_down_resistor(btn_pins[i], PM_PIN_PULLUP_1M);

    }
#endif
    //btn_set_wakeup_level_suspend();
}
void ble_start_pair()
{
	set_pair_flag();
	if(connect_ok)
	{
		active_disconnect_reason=BLE_PAIR_REBOOT_ANA_AAA;
	}
	else
	{
		user_reboot(BLE_PAIR_REBOOT_ANA_AAA);
	}
//telink public
}
void three_speed_switch_pair()
{
	if(fun_mode==RF_1M_BLE_MODE)
	{
		if ((((btn_value&COMB_BTN_PAIR)==COMB_BTN_PAIR)||((btn_value&MS_BTN_PAIR)==MS_BTN_PAIR)) && (clock_time_exceed(btn_tick, 2100000))) 
		{
			my_printf_aaa("pair key pressed to enter pair\n");
			ble_start_pair();
		}
	}
	else 
	{
		if ((((btn_value&COMB_BTN_PAIR)==COMB_BTN_PAIR)) && (clock_time_exceed(btn_tick, 2100000))) 
		{
			d24_start_pair();
		}
	}
}
void two_speed_switch_pair()
{
	if(fun_mode==RF_1M_BLE_MODE)
	{
		if ((((btn_value&COMB_BTN_PAIR)==COMB_BTN_PAIR)||((btn_value&MS_BTN_MODE)==MS_BTN_MODE)) && (clock_time_exceed(btn_tick, 2100000))) 
		{
			ble_start_pair();
		}
	}
	else 
	{
		if ((((btn_value&COMB_BTN_PAIR)==COMB_BTN_PAIR)) && (clock_time_exceed(btn_tick, 2100000))) 
		{
			d24_start_pair();
		}
	}
}
void three_switch_mode_change_poll()
{
    _attribute_data_retention_user static u32 tick;
    if (IS_BLE_MODE_AAA)
    {
        flash_dev_info.mode = RF_1M_BLE_MODE;
    }
    else
    {
        flash_dev_info.mode = RF_2M_2P4G_MODE;
    }
    if (fun_mode != flash_dev_info.mode)
    {
        if (clock_time_exceed(tick, 300000))
        {
            user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
        }
    }
    else
    {
        tick = clock_time();
    }
}
void three_mode_check()
{
#if(1)
	if (IS_BLE_MODE_AAA)
    {
        flash_dev_info.mode = RF_1M_BLE_MODE;
    }
    else if(IS_24_MODE)
    {
        flash_dev_info.mode = RF_2M_2P4G_MODE;
    }
	else if(gpio_read(PIN_5V_DET))
	{
		flash_dev_info.mode = USB_MODE;
	}
#else
	if(gpio_read(PIN_5V_DET))
	{
		flash_dev_info.mode = USB_MODE;
	}
	else if(IS_24_MODE)
	{
        flash_dev_info.mode = RF_2M_2P4G_MODE;
    }
	else if (IS_BLE_MODE_AAA)
    {
        flash_dev_info.mode = RF_1M_BLE_MODE;
    }
#endif
}

void three_mode_init()
{
	three_mode_check();
	if (fun_mode != flash_dev_info.mode)
	{
		fun_mode = flash_dev_info.mode;
	}
	printf("gpio check init with fun %d\n",fun_mode);
}
void three_mode_change_handle()
{
	 _attribute_data_retention_user static u32 tick;
	three_mode_check();
    if (fun_mode != flash_dev_info.mode)
    {
        if (clock_time_exceed(tick, 300000))
        {
        	clear_pair_flag();
        	save_dev_info_flash();
			printf("change from %d mode to %d mode",fun_mode, flash_dev_info.mode);
            user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
        }
    }
    else
    {
        tick = clock_time();
    }
}



#if MUTI_DEVICE_SWITCH_DEBUG
/**
 * @brief	BLE channel switch key check
 * @param	check_ms
 * @return	none
 * @note	Just BLE mode can switch muti_channel
 */
void muti_device_change(u16 check_ms)
{
#if (BIBOO_UX_K4_SPACE_KEY)
	/*BIBOO UX: K4 is repurposed as the keyboard space key (BIBOO_UX_K4_SPACE_KEY),
	 * so the K4 long-press channel switch is DISABLED: a normal space-key hold
	 * (>=3s, e.g. while the PC auto-repeats the space) would fire the switch,
	 * disconnect the link and reboot the mouse onto the next channel
	 * (deep_flag=MUTI_DEVICE_REBOOT_ANA_AAA, MAC changes, dongle can't reconnect).
	 * Set BIBOO_UX_K4_SPACE_KEY to 0 in AAA_app_config.h to restore it.*/
	(void)check_ms;
	return;
#else
	static u32 key_press_hold_tick = 0;
	static u32 key_release_hold_tick = 0;

	if (fun_mode != RF_1M_BLE_MODE)
	{
		device_switch_flag = 0;
		key_press_hold_tick = clock_time(); //clear press count
	}
	else
	{
		if ((btn_value == MS_BTN_K4))
		{ //key press
			if (device_switch_flag == 0)
			{
				//if (fun_mode == RF_1M_BLE_MODE) my_fifo_reset(&fifo_km); //clear fifo

				if (clock_time_exceed(key_press_hold_tick, check_ms*1000)) //press time enough
				{
					device_switch_flag = 1;

					if (fun_mode == RF_1M_BLE_MODE)
					{
						if (flash_dev_info.mast_id < 3)
						{
							flash_dev_info.mast_id ++;
						}
						else
						{
							flash_dev_info.mast_id = 0;
						}

					#if BLT_APP_LED_ENABLE
				        dpi_led_set(flash_dev_info.mast_id + 1); //LED indicate
					#endif

				        if (connect_ok)
				        { //when BLE connecting, disconnect first and set disconnect reason = switch muti_channel
				           active_disconnect_reason = MUTI_DEVICE_REBOOT_ANA_AAA;
				        }
				        else
				        { //no connect, direct reboot
							clear_pair_flag();
							save_dev_info_flash();
							user_reboot(MUTI_DEVICE_REBOOT_ANA_AAA);

				        }
					}
				}
			}

			key_release_hold_tick = clock_time(); //clear release count
		}
		else
		{ //key release
			if (device_switch_flag)
			{
				if (key_release_hold_tick == 0)
				{
					key_release_hold_tick = clock_time();
				}
				else if (clock_time_exceed(key_release_hold_tick, 100*1000)) //release time enough
				{
					device_switch_flag = 0;
				}
			}

			key_press_hold_tick = clock_time(); //clear press count
		}
	}
#endif
}
#endif

void button_process_telink_v21a(u8 event_new)
{
#if 1 //for test
    if((last_btn_value & MS_BTN_K4) && event_new)
    {
		printf("k4 press\n");
    }

	if((last_btn_value & MS_BTN_K5) && event_new)
    {
		printf("k5 press\n");
    }
#endif
#if (BIBOO_UX_K4_SPACE_KEY || BIBOO_UX_K4_MIC_PTT)
	/*BIBOO UX: K4 edge actions (one shared edge detector on btn_value,
	 * updated by the btn_get_value debounce; last_btn_value is only a
	 * period snapshot and can not be used for reliable edge detection):
	 *  - SPACE_KEY: press/release -> BLE keyboard report {cnt,ctrl,keycode[6]},
	 *    press = keycode[0] 0x2C (space), release = all-zero report;
	 *    dongle att_keyboard() forwards it to the PC on USB EP4.
	 *  - MIC_PTT: press = start mic capture (same sequence as the original
	 *    voice key: encoder reset + audio_stick + ui_enable_mic(1)),
	 *    release = stop the mic (ui_enable_mic(0)).*/
	{
		static u8 bibo_k4_state = 0;
		u8 bibo_k4_now = (btn_value & MS_BTN_K4) ? 1 : 0;
		if (bibo_k4_now != bibo_k4_state)
		{
			bibo_k4_state = bibo_k4_now;
			if ((fun_mode == RF_1M_BLE_MODE) && (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN))
			{
#if (BIBOO_UX_K4_MIC_PTT)
				if (bibo_k4_now)
				{
					audio_mic_param_init();			//reset mSBC encoder state
					audio_stick = clock_time() | 1;	//arm proc_audio_ble (17ms warm-up)
					ui_enable_mic(1);				//start AMIC capture & voice stream
					printf("bibo ptt on\n");
				}
				else
				{
					ui_enable_mic(0);				//stop AMIC, close the voice stream
					printf("bibo ptt off\n");
				}
#endif
#if (BIBOO_UX_K4_SPACE_KEY)
				u8 bibo_kb_rpt[8] = {0};
				if (bibo_k4_now)
				{
					bibo_kb_rpt[2] = 0x2C;	//HID keycode: Space
				}
				bls_att_pushNotifyData (HID_NORMAL_KB_REPORT_INPUT_DP_H, bibo_kb_rpt, 8);
				printf ("bibo kb space %d\n", bibo_k4_now);
#endif
			}
		}
	}
#endif
#if (BIBOO_UX_K5_CTRL_WIN_MIC)
	/*BIBOO UX: K5 (voice btn) push-to-talk with Ctrl+Win modifiers + Enter tail:
	 * press   -> BLE keyboard report {cnt,ctrl_key,keycode[6]} with
	 *            ctrl_key = 0x09 (Left-Ctrl 0x01 | Left-Win 0x08), no
	 *            normal keycode, AND start mic capture (PTT);
	 * release -> stop the mic, then a 3-report tail (one per loop call,
	 *            retried until the BLE TX FIFO accepts it - voice frames
	 *            may still be draining right after mic off):
	 *              1. all-zero report (release Ctrl+Win on the PC)
	 *              2. Enter press      (keycode 0x28)
	 *              3. all-zero report  (Enter released)
	 *            Strict order matters: Enter must NEVER be seen while the
	 *            modifiers are still down (Ctrl+Win+Enter = Windows Narrator).*/
	{
		static u8 bibo_k5_state = 0;
		static u8 bibo_k5_tail = 0;	//release tail: 0 idle, 1..3 pending reports
		u8 bibo_k5_now = (btn_value & MS_BTN_VOICE) ? 1 : 0;
		if (bibo_k5_now != bibo_k5_state)
		{
			bibo_k5_state = bibo_k5_now;
			if ((fun_mode == RF_1M_BLE_MODE) && (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN))
			{
				u8 bibo_kb_rpt[8] = {0};
				if (bibo_k5_now)
				{
					bibo_k5_tail = 0;	//cancel pending tail (report below clears any stuck key)
					bibo_kb_rpt[1] = 0x09;	//modifiers: Left-Ctrl(0x01) | Left-Win(0x08)
					audio_mic_param_init();			//reset mSBC encoder state
					audio_stick = clock_time() | 1;	//arm proc_audio_ble (17ms warm-up)
					ui_enable_mic(1);				//start AMIC capture & voice stream
					bls_att_pushNotifyData (HID_NORMAL_KB_REPORT_INPUT_DP_H, bibo_kb_rpt, 8);
					printf("bibo k5 ptt on\n");
				}
				else
				{
					ui_enable_mic(0);				//stop AMIC, close the voice stream
					bibo_k5_tail = 1;				//arm release tail: mods off -> Enter -> release
					printf("bibo k5 ptt off\n");
				}
			}
		}
		if (bibo_k5_tail)
		{
			if ((fun_mode == RF_1M_BLE_MODE) && (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN))
			{
				u8 bibo_kb_rpt[8] = {0};
				if (bibo_k5_tail == 2)
				{
					bibo_kb_rpt[2] = 0x28;	//HID keycode: Enter
				}
				if (bls_att_pushNotifyData (HID_NORMAL_KB_REPORT_INPUT_DP_H, bibo_kb_rpt, 8) == BLE_SUCCESS)
				{
					bibo_k5_tail ++;
					if (bibo_k5_tail > 3)
					{
						bibo_k5_tail = 0;	//tail done (Enter tap delivered)
						printf("bibo k5 enter\n");
					}
				}
			}
			else
			{
				bibo_k5_tail = 0;	//link gone: drop the tail
			}
		}
	}
#endif
#if SENSOR_FUN_ENABLE_AAA
		/* Switch optical sensor DPI */
		if ((last_btn_value & MS_BTN_CPI) && (!(btn_value & MS_BTN_CPI)))
		{
			if ((connect_ok) || (usb_device_status == USB_DEVICE_CONNECT_PC))
			{
			
				btn_dpi_set();
				app_data_send(1);
			}
		}
#endif

    //attribute_data_retention_user static u8 d24g_power_on_Pair_flag = 0;
    //-----------------cpi ------------------------

	if((last_btn_value== MS_BTN_VOICE)||(btn_value == MS_BTN_VOICE))
	{
		printf("voice btn press\n");
	}
    //-----------------pair ------------------------
    if(pair_flag==0)
    {
		if(switch_type==THREE_SPEED_SWITCH)
        {
			 three_speed_switch_pair();
		}
		else
		{
			two_speed_switch_pair();
        }
		#if(1)
	  		voice_key_check();
		#endif
		if((fun_mode == RF_1M_BLE_MODE)&&(ui_mic_enable))
		{
			if(ui_mtu_size_exchange_req && blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
			{
				ui_mtu_size_exchange_req = 0;
				blc_att_requestMtuSizeExchange(BLS_CONN_HANDLE, 0x009e);
			}
		}	
    }
	
    //-----------------mode change ------------------------
   #if(TELINK_BOARD)
		three_mode_change_handle();
	#endif
#if MUTI_DEVICE_SWITCH_DEBUG 
	if(fun_mode == RF_1M_BLE_MODE)
	{
		muti_device_change(3000);
	}
#endif
}




u16 btn_scan()
{
    u16 now_value = 0;
	u8 button_status;
#if(TELINK_BOARD)
	if(!gpio_read(PIN_BTN_LEFT))
	{
		now_value |= MS_BTN_LEFT;
	}
	if(!gpio_read(PIN_BTN_RIGHT))
	{
		now_value |= MS_BTN_RIGHT;
	}
	if(!gpio_read(PIN_BTN_MIDDLE))
	{
		now_value |= MS_BTN_MIDDLE;
	}
	if(!gpio_read(PIN_BTN_K4))
	{
		now_value |= MS_BTN_K4;
	}
	if(!gpio_read(PIN_BTN_K5))
	{
		now_value |= MS_BTN_VOICE;
	}
#endif
	#if(BOARD1_EN||TELINK_BOARD)
	if(!gpio_read(PIN_BTN_CPI))
	{
		now_value |= MS_BTN_CPI;
		printf("cpi press\n");
	}
	#endif
    return  now_value;
}


u8 btn_get_value()
{
    u8 ret = 0;
    _attribute_data_retention_user  static u8 debounce = 0;
    u16 now_value = 0;
    _attribute_data_retention_user static u16 last_value = 0;


    now_value = btn_scan();

    if (last_value != now_value)
    {
        reset_idle_status();
        debounce = 1;
        last_value = now_value;
    }

    if (debounce)
    {
        debounce++;
        if (debounce == 3)
        {
            ret = NEW_KEY_EVENT_AAA;
            ms_data.btn = now_value & 0X1F;
			//printf("the ms_data btn is %d\n",now_value);
			if((now_value &0xff00)!= 0)
			{
				ret = 0;
			}
            btn_value = now_value;
            btn_tick = clock_time();
#if DOUBLE_CLICK_LEFT_FUN_ENABLE
            if (btn_value == MS_BTN_DOUBLE_LEFT)
            {
                has_double_click_left = 1;
                double_click_left_tick = 0;
            }
#endif
            debounce = 0;
        }
    }

    button_process_telink_v21a(ret);



    //btn_set_wakeup_level_suspend(1);
    return ret;

}

#endif

#if WHEEL_FUN_ENABLE_AAA


void wheel_set_wakeup_level_suspend(u8 enable)
{
#if 1

    if (gpio_read(PIN_WHEEL_1))
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_1, 0, enable); //low wakeup suspend
        //connected_idle_time_count_ykq=0;
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_1, 1, enable);
    }

    if (gpio_read(PIN_WHEEL_2))
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 0, enable); //low wakeup suspend
        //connected_idle_time_count_ykq=0;
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 1, enable);
    }
#endif
}

void wheel_set_wakeup_level_deep()
{

#if 1
    if (gpio_read(PIN_WHEEL_1))
    {

        cpu_set_gpio_wakeup(PIN_WHEEL_1, 0, 1);
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_1, 1, 1);
    }

    if (gpio_read(PIN_WHEEL_2))
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 0, 1);
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 1, 1);
    }
#endif
}
static void wheel_init_hw()
{
#if 0
    gpio_set_input_en(PIN_WHEEL_1, 1);
    gpio_set_output_en(PIN_WHEEL_1, 0);
    gpio_setup_up_down_resistor(PIN_WHEEL_1, PM_PIN_PULLUP_1M);

    gpio_set_input_en(PIN_WHEEL_2, 1);
    gpio_set_output_en(PIN_WHEEL_2, 0);
    gpio_setup_up_down_resistor(PIN_WHEEL_2, PM_PIN_PULLUP_1M);


#endif


    write_reg8(0xd2, WHEEL_ADDRES_D2); // different gpio different value
    write_reg8(0xd3, WHEEL_ADDRES_D3);


    write_reg8(0xd7, 0x01);  //BIT(0) 0: 1¸ñ        1:  2¸ñ      	BIT(1)  wakeup enable

    write_reg8(0xd1, 0x01);  //filter   00-07     00 is best

	

#if 0
    u8 core_reg65 = read_reg8(0x65);
    core_reg65 |= (BIT(0) | BIT(5));
    write_reg8(0x65, core_reg65);  //wheel clk enable
#else
	reg_rst0 |= FLD_RST0_QDEC;   // for8258  power on 
	reg_rst0 &= (~FLD_RST0_QDEC);
    rc_32k_cal();
    BM_SET(reg_clk_en0, FLD_CLK0_QDEC_EN);
#endif


}
u32 mouse_wheel_prepare_tick(void)
{

    write_reg8(0xd8, 0x01);
    return clock_time();
}

_attribute_ram_code_ s8 mouse_wheel_process(u32 wheel_prepare_tick)
{
    s8 ret = 0;
    while (read_reg8(0xd8) & 0x01)
    {
#if (MODULE_WATCHDOG_ENABLE)
		wd_clear(); //clear watch dog
#endif
        if (clock_time_exceed(wheel_prepare_tick, 260))  //4 cylce is enough: 4*1/32k = 1/8 ms
        {
            write_reg8(0xd6, 0x01); //reset  d6[0]
            write_reg8(0xd6, 0x00);
            break;
        }
    }

#if 1//(WHEEL_TWO_STEP_PROC)
    _attribute_data_retention_user static signed char accumulate_wheel_cnt;
    _attribute_data_retention_user static signed char wheel_cnt;
    wheel_cnt = read_reg8(0xd0);

    wheel_cnt += accumulate_wheel_cnt;

    if (wheel_cnt & 1) //Ææ
    {
        accumulate_wheel_cnt = wheel_cnt > 0 ? 1 : -1;
    }
    else  //Å¼
    {
        accumulate_wheel_cnt = 0;
    }
    ret = (wheel_cnt / 2);
#else
    ret = read_reg8(0xd0);
#endif
    return ret;
}
#if PM_DEEPSLEEP_RETENTION_ENABLE
s8 wheel_vaule = 0;
_attribute_ram_code_ u8 wheel_get_value_1()
{
    u8 ret = 0;

    u8 wheel_now = 0;
    wheel_vaule = 0;

    if (!gpio_read(PIN_WHEEL_1)) //left
    {
        wheel_now |= 0x01;
    }
    if (!gpio_read(PIN_WHEEL_2)) //left
    {
        wheel_now |= 0x02;
    }
    if (wheel_now != wheel_pre)
    {
        wheel_pre = wheel_now;
        ret = WHEEL_DATA_EVENT_AAA;
        wheel_status = ((wheel_status << 2) & 0x3F) + wheel_now;
        if ((wheel_status == 0x07) || (wheel_status == 0x38))
        {
            wheel_vaule++;
        }
        else if ((wheel_status == 0x0b) || (wheel_status == 0x34))
        {
            wheel_vaule--;

        }

    }
    return ret;
}
#endif
_attribute_ram_code_ u8 wheel_get_value(u32 wheel_prepare_tick)
{
    u8 ret = 0;

    s8 wheel_now = 0;
#if PM_DEEPSLEEP_RETENTION_ENABLE

    ret = wheel_get_value_1();
#endif
    wheel_now = mouse_wheel_process(wheel_prepare_tick);

#if PM_DEEPSLEEP_RETENTION_ENABLE
    if (wheel_now == 0)
    {
        wheel_now = wheel_vaule;
    }
#endif

    if (user_cfg.wheel_direct != U8_MAX)
    {
        wheel_now = -wheel_now;
    }



    ms_data.wheel = wheel_now;
    if (wheel_now != 0)
    {
        ret = WHEEL_DATA_EVENT_AAA;
    }






    //wheel_set_wakeup_level_suspend(1);
    return ret;
}





#endif


void hw_init()
{



#if WHEEL_FUN_ENABLE_AAA

    wheel_init_hw();
#endif

#if BUTTON_FUN_ENABLE_AAA
    btn_init_hw();
#endif
#if BLT_APP_LED_ENABLE
    led_hw_init();
#endif
#if SENSOR_FUN_ENABLE_AAA

    OPTSensor_Init(1);
    if (!deepRetWakeUp)
    {
#if DPI_SAVE_FLASH
        if (fun_mode == RF_1M_BLE_MODE)
        {
            sensor_dpi_set(flash_dpi_info.bt_dpi);
        }
        else
        {
            sensor_dpi_set(flash_dpi_info.d24g_dpi);
        }
#else
        sensor_dpi_set(flash_dpi_info.bt_dpi);
#endif
    }

    output_dev_info.sensor_type = sensor_type;
    output_dev_info.sensor_pd1 = product_id1;
    output_dev_info.sensor_pd2 = product_id2;
    output_dev_info.sensor_pd3 = product_id3;
#if TEST_MCU_CURRENT_DEBUG
    OPTSensor_Shutdown();
#endif


#endif


}


/**
 * @brief       This function push data fifo
 * @param[in]   buf	- data buf
 * @param[in]   data_type	- data type
 * @param[in]   len	- data len
 * @return      
 * @note        
 */
u8 push_data_fifo_aaa(u8 *buf, u8 data_type, u8 len)
{

	if((tx_fifo_aaa.count == TX_FIFO_NUM_AAA))//full
    {                                        // full 
                return 0;      
    }
	else
    {                                        // in
            TX_PACKET_AAA *p = (TX_PACKET_AAA *)tx_fifo_aaa.fifo[tx_fifo_aaa.wptr];//ptr txfifo
		    p->type = data_type;//data type
		    p->len = len;//data len
		    memcpy(p->buf, buf, len);//fifo data set
            tx_fifo_aaa.wptr = (tx_fifo_aaa.wptr + 1) % TX_FIFO_NUM_AAA;//wprt ++
            tx_fifo_aaa.count++;//count++
            return 1;
    }

}

void push_report_fifo()
{
	

    if (has_new_report_aaa & HAS_MOUSE_REPORT)
    {

        // ms_data.sn++;
		 
            if (push_data_fifo_aaa(&ms_data.btn, HAS_MOUSE_REPORT, sizeof(mouse_data_t)))
            {
                has_new_report_aaa &= ~HAS_MOUSE_REPORT;
            }
        

    }

}

/**
 * @brief       This function notify ble data
 * @return      
 * @note        
 */
void ble_notify_data_proc_aaa()
{
   	u8 need_ms_notify = 0; //no need init
   	mouse_data_t buf;


	{
	
	    if(check_fifo_has_data() || ms_data.x || ms_data.y || ms_data.wheel)//fifo has data or ms has data
		{
	       	loop_cnt = 0;//reset loop cnt

			if(check_fifo_has_data())	//tx_fifo_aaa  fifo  just  for  button  not  for x y wheel
	        {
	        	buf.btn = tx_fifo_aaa.fifo[tx_fifo_aaa.rptr][2];    //buf btn set to tx fifo data          
	        }
	        else
	        {
	        	buf.btn = ms_data.btn;					//the last key ,may be   0   or   still press    . press and not rel and move the mouse 
	        }

	        buf.x = ms_data.x;//x
	        buf.y = ms_data.y;//y
	        buf.wheel = ms_data.wheel;//wheel
	        need_ms_notify = 1;//need ms notify
		}

		if(active_disconnect_reason)//if has active discon
		{
			buf.btn =0;//no btn
			buf.x = 0;
		    buf.y = 0;
		    buf.wheel = 0;
			need_ms_notify = 1;
			bls_att_pushNotifyData(HID_MOUSE_REPORT_INPUT_DP_H, &buf.btn, sizeof(mouse_data_t));//notify btn data
			bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);//discon
			return;
	    }

		u8 txFifoNumber = blc_ll_getTxFifoNumber();//get tx fifo num
		
		if(need_ms_notify)//need send ms data
		{
			//if(txFifoNumber < RF_TX_FIFO_ALLOW_NUM)//txfifo num ok
			if(txFifoNumber < 15)
			{
				u8 status = bls_att_pushNotifyData(HID_MOUSE_REPORT_INPUT_DP_H, &buf.btn, sizeof(mouse_data_t));//att send  ms data
				if(status == BLE_SUCCESS)//send ok
				{
					if(check_fifo_has_data())//has data still
					{
						tx_fifo_aaa.rptr = (tx_fifo_aaa.rptr + 1) % TX_FIFO_NUM_AAA;//rptr update
						tx_fifo_aaa.count--; //count--
	                }
	                ms_data.wheel = 0;//wheel reset 0
	            }		
			#if UART_PRINT_DEBUG_ENABLE //uart debug en
	            else
	            {
	            	my_printf_aaa("notify_fail_reason=%x\r\n", status);//debug notify fail
	            }
			#endif
	        }
		#if UART_PRINT_DEBUG_ENABLE// uart debug en
	        else
	        {
	        	my_printf_aaa("txFifo small=%x\r\n", txFifoNumber);//debug tx fifo num
	        }
		#endif
		}	

#if BATT_CHECK_ENABLE  //batt check en
	   	else if (need_batt_data_notify) //notify batt
	   	{
	   		if(txFifoNumber < RF_TX_FIFO_ALLOW_NUM )//tx fifo num ok and not en pc sleep
			{
				u8 status = bls_att_pushNotifyData(BATT_LEVEL_INPUT_DP_H, my_batVal, 1);//send batt notify
				if(status == BLE_SUCCESS) //ble success
				{
					need_batt_data_notify = 0;
				}
			}
		}
#endif
		
	}
}

u8 check_fifo_has_data()
{
	 return (tx_fifo_aaa.count>0);
   // return (tx_fifo_aaa.wptr != tx_fifo_aaa.rptr);
}
void clear_fifo()
{
    tx_fifo_aaa.wptr = 0;//wptr 0
    tx_fifo_aaa.rptr = 0;//rptr 0
	tx_fifo_aaa.count=0;//count 0
}
u8 clear_press_key_aaa()
{
    u8 buf[sizeof(mouse_data_t)] = {0, 0, 0, 0};
    push_data_fifo_aaa(buf, HAS_MOUSE_REPORT, sizeof(mouse_data_t));
    return 1;
}


void switch_type_init()
{

 // Hardware related
#if BUTTON_FUN_ENABLE_AAA
#if(0)
	if(gpio_read(PIN_SWITCH_TYPE))//two speed switch
	{
		switch_type=TWO_SPEED_SWITCH;
		gpio_setup_up_down_resistor(PIN_SWITCH_TYPE,PM_PIN_UP_DOWN_FLOAT);
		return;
	}
	switch_type=THREE_SPEED_SWITCH; //three swhtich 
    if (IS_BLE_MODE_AAA)
    {
        flash_dev_info.mode = RF_1M_BLE_MODE;
    }
    else
    {
        flash_dev_info.mode = RF_2M_2P4G_MODE;
    }
    gpio_setup_up_down_resistor(PIN_MODE_SWITCH,PM_PIN_UP_DOWN_FLOAT);
#endif
#if 0//test switch is good
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
#endif
}


void mouse_xy_multiple()
{
#if MUTI_SENSOR_ENABLE
    s32 x = 0, y = 0;
    if (xy_multiple_flag == MULTIPIPE_1_DOT_5)
    {
        x = ms_data.x;
        y = ms_data.y;
        x = (x * 3) / 2;
        y = (y * 3) / 2;
        ms_data.x = x;
        ms_data.y = y;
    }
#endif
}

void mouse_task_when_rf()
{


#if SENSOR_FUN_ENABLE_AAA

    if (OPTSensor_motion_report(0))
    {
		if(ms_data.x||ms_data.y)
		{
        has_new_key_event |= SENSOR_DATA_EVENT_AAA;

        mouse_xy_multiple();
        check_sensor_dircet(user_cfg.sensor_direct);
        adaptive_smoother();
		//printf("has sensor data with %02x, %02x\n",ms_data.x,ms_data.y);
		}
    }
    else
    {
        ms_data.x = 0;
        ms_data.y = 0;
    }

#endif

    has_new_key_event |= Draw_a_square_test();


}
u8  Draw_a_square_test()
{
    _attribute_data_retention_user static int x = 0;
    _attribute_data_retention_user static u8 flag = 0;
    int step = 4;
   if(auto_draw_flag==0)
   {
   		return 0;
   }
    x++;
    if (x >= 50)
    {
        x = 0;
        flag++;
        if (flag > 3)
        {
            flag = 0;
        }

    }
    if (flag == 0)
    {
        ms_data.x = step;
        ms_data.y = 0;
    }
    else if (flag == 1)
    {
        ms_data.x = 0;
        ms_data.y = step;
    }
    else if (flag == 2)
    {
        ms_data.x = -step;
        ms_data.y = 0;
    }
    else if (flag == 3)
    {
        ms_data.x = 0;
        ms_data.y = -step;
    }
    return SENSOR_DATA_EVENT_AAA;

}

void clear_pair_flag()
{
    pair_flag = 0;
    analog_write(USED_DEEP_ANA_REG1, ana_reg1_aaa & CLEAR_PAIR_ANA_FLAG);
}
void set_pair_flag()
{
    pair_flag = 1;
    analog_write(USED_DEEP_ANA_REG1, ana_reg1_aaa | PAIR_ANA_FLG);
}
void write_deep_ana0(u8 buf)
{
    deep_flag = buf;
    analog_write(DEEP_ANA_REG0, buf);
}
void user_reboot(u8 reason)
{
    write_deep_ana0(reason);
    start_reboot();

}
#if DOUBLE_CLICK_LEFT_FUN_ENABLE

void  double_click_left_24G_mode()
{

    if (double_click_left_tick == 0)
    {
        ms_buf.btn = MS_BTN_LEFT;
        double_click_left_tick = clock_time() | 1;
        has_double_click_cnt = 1;
    }
    if ((has_double_click_cnt == 1) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        has_double_click_cnt = 2;
        ms_buf.btn = 0;
        double_click_left_tick = clock_time() | 1;
    }
    else if ((has_double_click_cnt == 2) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        has_double_click_cnt = 3;
        ms_buf.btn = MS_BTN_LEFT;
        double_click_left_tick = clock_time() | 1;
    }
    else if ((has_double_click_cnt == 3) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        has_double_click_cnt = 4;
        ms_buf.btn = 0;
        double_click_left_tick = clock_time() | 1;
    }
    else if ((has_double_click_cnt == 4) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        double_click_left_tick = 0;
        ms_buf.btn = 0;
        has_double_click_left = 0;
    }
}
void  double_click_left_ble_mode()
{

    if (double_click_left_tick == 0)
    {
        ms_buf.btn = MS_BTN_LEFT;
        ms_buf.x = 0;
        ms_buf.y = 0;
        ms_buf.wheel = 0;
        double_click_left_tick = clock_time() | 1;
        has_double_click_cnt = 1;
        push_data_fifo_aaa(&ms_buf.btn, HAS_MOUSE_REPORT, sizeof(mouse_data_t));
    }
    if ((has_double_click_cnt == 1) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        has_double_click_cnt = 2;
        ms_buf.btn = 0;
        double_click_left_tick = clock_time() | 1;
        push_data_fifo_aaa(&ms_buf.btn, HAS_MOUSE_REPORT, sizeof(mouse_data_t));
    }
    else if ((has_double_click_cnt == 2) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        has_double_click_cnt = 3;
        ms_buf.btn = MS_BTN_LEFT;
        double_click_left_tick = clock_time() | 1;
        push_data_fifo_aaa(&ms_buf.btn, HAS_MOUSE_REPORT, sizeof(mouse_data_t));
    }
    else if ((has_double_click_cnt == 3) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        has_double_click_cnt = 4;
        ms_buf.btn = 0;
        double_click_left_tick = clock_time() | 1;
        push_data_fifo_aaa(&ms_buf.btn, HAS_MOUSE_REPORT, sizeof(mouse_data_t));
    }
    else if ((has_double_click_cnt == 4) && clock_time_exceed(double_click_left_tick, DOUBLE_CLICK_LEFT_INTERVAL))
    {
        double_click_left_tick = 0;
        ms_buf.btn = 0;
        has_double_click_left = 0;
    }
}
#endif


u8 get_ble_data_report_aaa()
{
    //combination_flag = 0;
    //	s8 buf[2]={0,0};

#if WHEEL_FUN_ENABLE_AAA

    u32 wheel_prepare_tick;
    wheel_prepare_tick = mouse_wheel_prepare_tick();


#endif
#if BUTTON_FUN_ENABLE_AAA

    has_new_key_event |= btn_get_value();
#endif


#if SENSOR_FUN_ENABLE_AAA


    if (OPTSensor_motion_report(0))
    {
        has_new_key_event |= SENSOR_DATA_EVENT_AAA;

        mouse_xy_multiple();

        check_sensor_dircet(user_cfg.sensor_direct);
        adaptive_smoother();
    }
    else
    {
        ms_data.x = 0;
        ms_data.y = 0;
    }

#endif

    if ((blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) && ((ble_status_aaa == T5S_CONNECTED_STATUS_AAA)))
    {
        has_new_key_event |= Draw_a_square_test();
    }


#if WHEEL_FUN_ENABLE_AAA

    has_new_key_event |= wheel_get_value(wheel_prepare_tick);



    //sensor_set_wakeup_level_suspend();
#endif
    if (has_new_key_event & (NEW_KEY_EVENT_AAA))
    {
        has_new_report_aaa |= HAS_MOUSE_REPORT;

    }
#if DOUBLE_CLICK_LEFT_FUN_ENABLE

    if (has_double_click_left)
    {
        double_click_left_ble_mode();
    }
    else
#endif
    {
        push_report_fifo();
    }
    return has_new_key_event;

}

void get_24g_data_report_aaa()
{
#if WHEEL_FUN_ENABLE_AAA
    u32 wheel_prepare_tick;

    wheel_prepare_tick = mouse_wheel_prepare_tick();

#endif
    //combination_flag = 0;


#if BUTTON_FUN_ENABLE_AAA
    has_new_key_event |= btn_get_value();
#endif

#if WHEEL_FUN_ENABLE_AAA
    has_new_key_event |= wheel_get_value(wheel_prepare_tick);
#endif

#if 0
    if (has_new_key_event)
    {
        has_new_report_aaa |= HAS_MOUSE_REPORT;

    }
    push_report_fifo();
#endif

}

