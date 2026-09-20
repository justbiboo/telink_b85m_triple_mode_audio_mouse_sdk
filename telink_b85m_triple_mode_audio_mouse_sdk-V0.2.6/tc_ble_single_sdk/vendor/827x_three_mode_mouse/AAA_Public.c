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
	u8 usb_device_status;
u8  last_usb_connected_flag=0;
u8 	now_usb_connected_flag=0;
u8 last_vbus_exist_flag=0;
u8 now_vbus_exist_flag=0;


#if MUTI_DEVICE_SWITCH_DEBUG
#define COMB_BTN_PAIR  MS_BTN_LEFT
#else
#define COMB_BTN_PAIR  (MS_BTN_LEFT|MS_BTN_RIGHT)//(MS_BTN_LEFT|MS_BTN_MIDDLE|MS_BTN_RIGHT)
#endif

#if D24G_OTA_ENABLE_AAA
#define COMB_BTN_OTA  (MS_BTN_LEFT|MS_BTN_RIGHT)
#endif

#if DOUBLE_CLICK_LEFT_FUN_ENABLE

    _attribute_data_retention_user static u32 double_click_left_tick;
    _attribute_data_retention_user static u8 has_double_click_cnt;
    _attribute_data_retention_user u8 has_double_click_left = 0;
    #define DOUBLE_CLICK_LEFT_INTERVAL  40000

#endif
//u8 report_rate_index=0;

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


_attribute_data_retention_user u8 has_new_report_aaa=0;
_attribute_data_retention_user u8 has_new_key_event = 0;


_attribute_data_retention_user u8 combination_flag = 0;
_attribute_data_retention_user TX_FIFO_AAA_STRUCT tx_fifo_aaa;
_attribute_data_retention_user  int ui_mtu_size_exchange_req = 0;
_attribute_data_retention_user	u8	ui_mic_enable = 0;
_attribute_data_retention_user	u8 has_mic_data_flag = 0;
_attribute_data_retention_user	u8 appdata[8]={0};
u32 mic_duration = 60;


_attribute_data_retention_user u16 scan_btn_value = 0;
_attribute_data_retention_user u16 btn_value = 0;
_attribute_data_retention_user u16 last_btn_value = 0;
_attribute_data_retention_user u8 mouse_btn_in_sensor;
//u8 last_mosue_btn_in_sensor;
_attribute_data_retention_user mouse_data_t ms_data;
_attribute_data_retention_user mouse_data_t ms_buf;










_attribute_data_retention_user u8 wheel_pre = 0;
_attribute_data_retention_user u8 wheel_status;
_attribute_data_retention_user u8 mode_change_flag = 0;


#define MAX_SAVE_FLASH_LENGTH       256
#define SAVE_SUCCESS                0
#define SAVE_FAILED                 -1

u8 scan_count = 0;
u8 last_scan_result = 0;
u8 flag_count = 0;
#if(KAIFABAN_EN==0)
#if(MICDEMO)
u16 const MOUSE_MAP_NORMAL[3][3]=
	{
		{MS_BTN_LEFT, MS_BTN_RIGHT,MS_BTN_MIDDLE},\
		{MS_BTN_VOICE,0,0},\
		{0,0,0},\
	};
#endif
#else
u16 const MOUSE_MAP_NORMAL[2][2]=
	{
		{MS_BTN_MIDDLE,MS_BTN_K4},\
		{MS_BTN_VOICE,MS_BTN_TRANSLATE},\
	};

#endif
u8 keyboard_buf_aaa[BUF_SIZE_KEYBOARD_AAA] = {0};
u8 keyboard_buf_last_aaa[BUF_SIZE_KEYBOARD_AAA] = {0};
_attribute_data_retention_user  kb_data_t_aaa key_buf_aaa ;




/**
 * @brief       This function write data to flash
 * @param[in]   addr	- flash addr
 * @param[in]   buf	- data buff
 * @param[in]   len	- data len
 * @return      
 * @note        
 */
void flash_write_page_user(unsigned long addr, unsigned long len, unsigned char *buf)
{

    u16 write_cnt = len / 8; //write cnt
    u8 last_write_len = len % 8;//last write len

    for(int i = 0; i < write_cnt; i++) { //loop write
        flash_write_page((addr + 8 * i), 8, (buf + 8 * i));//write 8 bytes every time
        sleep_us(20);//sleep 20 us
    }

    if (last_write_len > 0)//has other data
        flash_write_page((addr + 8 * write_cnt), last_write_len, (buf + 8 * write_cnt));//write data
}

/**
 * @brief       This function save data to flash
 * @param[in]   addr	- the flash addr
 * @param[in]   buf	- the buff of data need save
 * @param[in]   len	- data len
 * @param[in]   offset	- the pointer offset to flash addr
 * @return      
 * @note        
 */
int save_data_to_flash(unsigned long addr, unsigned long len, unsigned char *buf, int *offset)
{
    static u8 tmp_buf[MAX_SAVE_FLASH_LENGTH];//tem buf
    int tmp_offset = 0;//tmp offset

    if (len > MAX_SAVE_FLASH_LENGTH)//len invalid
        return SAVE_FAILED;//fail

    if (*offset > (4096 - 2 * len))//offset too larger
        flash_erase_sector(addr);//erase sector
    else
        tmp_offset = *offset + len;//update tmp_offset

    for (u8 i = 0; i < 2; i++) {//2 times
        flash_write_page_user(addr + tmp_offset, len, buf);//write buf to addr
        flash_read_page(addr + tmp_offset, len, tmp_buf);//read from addr
        if (memcmp(tmp_buf, buf, len) == 0) { //cmp
            *offset = tmp_offset;//update offset
             return SAVE_SUCCESS;//save suc
        } else {
            flash_erase_sector(addr);//erase sector
            tmp_offset = 0;//tmp_offset 0
        }
    }
    return SAVE_FAILED;
}





#if((TL_AUDIO_MODE == TL_AUDIO_RCU_MSBC_HID))
u32 	key_voice_pressTick = 0;
u32 	audio_stick = 0;
_attribute_data_retention_	u8  audio_start = 0;

u8 d24g_mic_data[64]={0};
u8 index_d24g_mic[33]={0};



void amic_gpio_reset (void)
{
#if(APP_24G_AUDIO_EN)
	gpio_set_func(GPIO_AMIC_BIAS, AS_GPIO);
	gpio_set_input_en(GPIO_AMIC_BIAS, 0);
	gpio_set_output_en(GPIO_AMIC_BIAS, 1);
	gpio_write(GPIO_AMIC_BIAS, 0);

	gpio_set_func(GPIO_AMIC_SP, AS_GPIO);
	gpio_set_input_en(GPIO_AMIC_SP, 0);
	gpio_set_output_en(GPIO_AMIC_SP, 1);
	gpio_write(GPIO_AMIC_SP, 0);
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

#if BLE_DMIC_ENABLE
	//DMIC Bias output
	//gpio_set_output_en (GPIO_DMIC_BIAS, en);
	//gpio_write (GPIO_DMIC_BIAS, en);
#else
	//AMIC Bias output
	gpio_set_output_en (GPIO_AMIC_BIAS, en);
	#if (MCU_CORE_TYPE == MCU_CORE_827x)
		gpio_set_data_strength (GPIO_AMIC_BIAS, en);
	#endif
	gpio_write (GPIO_AMIC_BIAS, en);
#endif


	if(en){  //audio on

		///////////////////// AUDIO initialization///////////////////
		//buffer_mic set must before audio_init !!!
		audio_config_mic_buf (  (u16*)buffer_mic, TL_MIC_BUFFER_SIZE);
		buffer_mic_pkt_rptr = buffer_mic_pkt_wptr = 0;
		audio_stick = clock_time()|1;
			//Amic config
			audio_set_mute_pga(0);  ////enable audio need follow this step: 1 enable bias; 2 disable mute_pga;
			gpio_set_output_en(GPIO_AMIC_SP, 0);
			audio_amic_init(AUDIO_16K);							  //3 init; 4 delay about 17ms; 5 enable mute_pga.


		#if (IIR_FILTER_ENABLE)
			//only used for debugging EQ Filter parameters, removed after mass production
			extern void filter_setting();
			filter_setting();
		#endif
	}
	else{  //audio off

			audio_codec_and_pga_disable();	//power off codec and pga

			amic_gpio_reset();
			audio_stick = 0;
		
		buffer_mic_pkt_rptr = buffer_mic_pkt_wptr = 0;
	}
	#if ((BATT_CHECK_ENABLE))
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
		if(audio_start || (audio_stick && clock_time_exceed(audio_stick, 17*1000))){// for 8278
			if(!audio_start)
			{
				audio_set_mute_pga(1);
			}
			audio_start = 1;
			if(clock_time_exceed(audio_stick, mic_duration*1000*1000))
			{
				ui_enable_mic(0);
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
#if 1

void usb_custom_init()
{

	usbhw_set_eps_en(FLD_USB_EDP8_EN|FLD_USB_EDP1_EN|FLD_USB_EDP2_EN|FLD_USB_EDP4_EN);
	usb_set_pin_en();
	usb_init_interrupt();
	reg_usb_ep3_buf_addr =0xc0;
	reg_usb_ep4_buf_addr = 0x60;
	reg_usb_ep6_buf_addr =0xa0;
	reg_usb_ep7_buf_addr =0xc0;
	reg_usb_ep5_buf_addr = 0xb0;
	reg_usb_ep8_buf_addr =0xd0;
	irq_enable();

	//deepsleep_dp_dm_gpio_low_wake_enable();


	//usb_set_pin_en();//dp pull up
	
}
#if(APP_24G_AUDIO_EN)
u8 tmp_mic_data[ADPCM_PACKET_LEN];
u8 mic_data[ADPCM_PACKET_LEN];
#endif




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
			memcpy (tmp_mic_data, (u8*)&p_usb_mic[0], ADPCM_PACKET_LEN);
			u8 msbc_data[63]= {1,57,0,0};
			memcpy(&msbc_data[2],(u8*)&tmp_mic_data[0],57);
			if( usb_app_hid_report(0x0A,(u8*)&msbc_data[0],63))
			buffer_mic_pkt_rptr++;
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
			if(!audio_start)
			{
				audio_set_mute_pga(1);
			}
			audio_start = 1;
			task_audio_usb();
		}
	}
	else{
		audio_start = 0;
	}
#endif
}
#endif
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
	//if (blc_ll_getTxFifoNumber() < 12)//8 + audio_send_idx
	{

		
		int* p_ble_mic = mic_encoder_data_buffer ();
		
		if (p_ble_mic)					//around 3.2 ms @16MHz clock
		{
			if(BLE_SUCCESS == bls_att_pushNotifyData(MY_DATA_INPUT_DP_H, (u8*)p_ble_mic, 57))
			{
					buffer_mic_pkt_rptr++;
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
		if(audio_start || (audio_stick && clock_time_exceed(audio_stick, 17*1000))){// for 8278
			if(!audio_start)
			{
				audio_set_mute_pga(1);
			}
			audio_start = 1;
			if(blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
			{
			    if(clock_time_exceed(audio_stick, mic_duration*1000*1000))
			    {
			    	ui_enable_mic (0);
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


/**
 * @brief       This function save dev info to flash
 * @return      
 * @note        
 */
void save_dev_info_flash()
{
    save_data_to_flash(CFG_DEVICE_MODE_ADDR, SAVE_MAX_IN_FLASH, (u8 *)&flash_dev_info.dongle_id, &dev_info_idx);//save data to flash
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


	
	if((last_btn_value == MS_BTN_VOICE) &&(btn_value != MS_BTN_VOICE) && ui_mic_enable){

		ui_enable_mic(0);
		printf("short voice key press off\n");
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


#if BUTTON_FUN_ENABLE_AAA

_attribute_data_retention_user  u32 btn_tick = 0;
//_attribute_data_retention_user 	u32 btn_pins[BTN_NUM_AAA] = BTN_MATRIX;
u16 Mouse_DRIVE_PINS[TOTAL_ROW] = GPIO_ROW_PIN;
u16 Mouse_SCAN_PINS[TOTAL_COL] = GPIO_COL_PIN;

void btn_set_wakeup_level_suspend(u8 enable)
{
  	for (int i=0; i < TOTAL_ROW; i++)
	{
		gpio_set_output_en(Mouse_DRIVE_PINS[i], 1);
		gpio_setup_up_down_resistor(Mouse_DRIVE_PINS[i],PM_PIN_PULLDOWN_100K);
	}

	//Disable row wakeup
	for (int i=0; i <TOTAL_COL; i++)
	{
		gpio_setup_up_down_resistor(Mouse_SCAN_PINS[i],PM_PIN_PULLUP_10K);
		cpu_set_gpio_wakeup(Mouse_SCAN_PINS[i], 0, 0);
	}
	
	sleep_ms(2);
	for (int i=0; i < TOTAL_COL; i++)
	{
		if (gpio_read(Mouse_SCAN_PINS[i]))
		{
			cpu_set_gpio_wakeup(Mouse_SCAN_PINS[i], 0, 1);	
		}
		else
		{
			cpu_set_gpio_wakeup(Mouse_SCAN_PINS[i], 1, 1);
		}
	}
#if(0)
	//Enable col wakeup
	for (int i=0; i < TOTAL_ROW; i++)
	{
		#if(0)
		if (gpio_read(Mouse_DRIVE_PINS[i]))
		{
			gpio_set_input_en(Mouse_DRIVE_PINS[i], 1);
			cpu_set_gpio_wakeup(Mouse_DRIVE_PINS[i], 0, 1); //pad wakeup deep
		}
		else
		#endif
		{
			gpio_set_input_en(Mouse_DRIVE_PINS[i], 1);
			cpu_set_gpio_wakeup(Mouse_DRIVE_PINS[i], 1, 1);
		}
	}
#endif
}


void btn_set_wakeup_level_deep()
{
    	//Diable col out low
	for (int i=0; i < TOTAL_ROW; i++)
	{
		gpio_set_output_en(Mouse_DRIVE_PINS[i], 0);
		gpio_setup_up_down_resistor(Mouse_DRIVE_PINS[i],PM_PIN_PULLDOWN_100K);
	}

	//Disable row wakeup
	for (int i=0; i <TOTAL_COL; i++)
	{
		gpio_setup_up_down_resistor(Mouse_SCAN_PINS[i],PM_PIN_PULLUP_10K);
		cpu_set_gpio_wakeup(Mouse_SCAN_PINS[i], 0, 0);
	}
	
	sleep_ms(2);

	//Enable col wakeup
	for (int i=0; i < TOTAL_ROW; i++)
	{
		if (gpio_read(Mouse_DRIVE_PINS[i]))
		{
			cpu_set_gpio_wakeup(Mouse_DRIVE_PINS[i], 0, 1); //pad wakeup deep
		}
		else
		{
			cpu_set_gpio_wakeup(Mouse_DRIVE_PINS[i], 1, 1);
		}
	}

}


void btn_init_hw()
{

	u8 i;
	
	for (i = 0; i < TOTAL_ROW; i++)
	{
		/* set COL(8) pin to pull-down 100K, output low level */
		gpio_set_func(Mouse_DRIVE_PINS[i], AS_GPIO);
		gpio_set_output_en(Mouse_DRIVE_PINS[i], 1);
		gpio_set_input_en(Mouse_DRIVE_PINS[i],0);
		gpio_setup_up_down_resistor(Mouse_DRIVE_PINS[i],PM_PIN_PULLDOWN_100K);
		gpio_write(Mouse_DRIVE_PINS[i],0);
	}
	
	for (i = 0; i < TOTAL_COL; i++)
	{
		/* set ROW(18) pin to pull-up 10K */
		gpio_set_func(Mouse_SCAN_PINS[i], AS_GPIO);
		gpio_set_output_en(Mouse_SCAN_PINS[i], 0);
		gpio_set_input_en(Mouse_SCAN_PINS[i],1);
		gpio_setup_up_down_resistor(Mouse_SCAN_PINS[i],PM_PIN_PULLUP_10K);
	}
    //btn_set_wakeup_level_suspend();
}



u32 get_row_value_aaa()
{
	u32 value=0;
	
	for (u8 cnt = 0; cnt < TOTAL_COL; cnt++)
	{
		if (gpio_read(Mouse_SCAN_PINS[cnt]) == 0)
		{
			value |= 1<<cnt; //save key press
		}

	
	}

	return value; //return key press result
}


u16 scan(u8 flag)
{
	u32 scan_result = 0;
	u8  col, x;
	u16  has_new_key_event_f = 0;
	u32 delay_clock;
	u16 now_value=0;
	scan_result = get_row_value_aaa(); //get row result
	
	flag_count++; //scan count plus 1
	
	if (last_scan_result != scan_result)
	{ //current scan result != last scan result, means have new key
		last_scan_result = scan_result;
		//printf("the scan is %x\n",scan_result);
		flag_count = 0; //if have new key then clear scan count(flag_count)
	}

	if ((flag_count > 2) && (key_buf_aaa.press_cnt == 0))
	{ //means have no new key, return directly
		flag_count = 0x3F;

		return 0;
	}

	/* The row value changed, scan the keyboard to get key value */
	
	key_buf_aaa.press_cnt = 0; //clear key press cnt
	
	for (col = 1; col < TOTAL_ROW; col++)
	{ //Disable col pin output from 1 to TOTAL_COL(8)
		gpio_set_output_en(Mouse_DRIVE_PINS[col], 0);
		//gpio_write(Mouse_DRIVE_PINS[col], 0);
	}

	/* col_0 output low level */
	gpio_set_output_en(Mouse_DRIVE_PINS[0], 1);
	gpio_set_input_en(Mouse_DRIVE_PINS[0], 0);
	gpio_write(Mouse_DRIVE_PINS[0], 0);

	/* save current time */
	delay_clock = clock_time();
		
	/* Start scanning column by column */
	for (col = 0; col < TOTAL_ROW; col++)
	{
		/* Wait for the level signal to stabilize */
		//while (!clock_time_exceed(delay_clock, 50));
		sleep_us(5);
		/* get row value */
		scan_result = get_row_value_aaa();

		/* disable current COL output */
		gpio_set_output_en(Mouse_DRIVE_PINS[col], 0);
		//gpio_write(Mouse_DRIVE_PINS[col], 0);
		
		/* enable next column output low level */
		if (col < (TOTAL_ROW-1))
		{
			gpio_set_input_en(Mouse_DRIVE_PINS[col+1], 0);
			gpio_set_output_en(Mouse_DRIVE_PINS[col+1], 1); //enable output
			gpio_write(Mouse_DRIVE_PINS[col+1], 0); //set output low
			delay_clock = clock_time(); //save current time
		}
	   
		u8 cnt = 0; //The number of buttons in the current column

		//check which row have value(when key link to row be press, row value be set) in current column
		for (x = 0; x < TOTAL_COL; x++)
		{			
			if (scan_result & (1<<x)) //check row value, bit0~bit17 correspond row0~row17
	        { //row[x] have value, means Mouse_DRIVE_PINS[col]/row_pins[x] be press
				now_value |= MOUSE_MAP_NORMAL[col][x]; //set key flag for special function
				if(now_value != scan_btn_value)
					{	
						scan_btn_value = now_value; 
						reset_idle_status();
						has_new_key_event_f =1;
					}
				key_buf_aaa.press_cnt ++; //press_cnt plus 1

				cnt ++; //The number be pressed in the current column plus 1

				/*if (cnt > 1)
				{ //there is more than 1 key press in current column
					scan_result|= BIT(31); //set bit(31) to indicate there has ghost key
				}*/
	        }		
		}
		
		/*if (scan_result != scan_buf_aaa[col])
		{ //current row value(col) != last row value(col)
			scan_buf_aaa[col] = scan_result; //save current row valueto scan_buf_aaa[col]

			has_new_key_event_f = 1; //set has new key flag
		}*/
	}

	/* key scan complete, set all col output low, wait for next key scan */
	for (col = 0; col < TOTAL_ROW; col++)
	{
		gpio_set_input_en(Mouse_DRIVE_PINS[col], 0);
		gpio_set_output_en(Mouse_DRIVE_PINS[col],1); //enable output

		gpio_write(Mouse_DRIVE_PINS[col],0); //set output low
	}

	/*if (has_new_key_event_f)
	{ //if has new key, reset idle count
		reset_idle_status();
	}*/
	



	return now_value;
	//return has_new_key_event_f;
}
/**
 * @brief       This function start pair
 * @return      
 * @note        
 */
void ble_start_pair()
{
	set_pair_flag();//set pair
	if(connect_ok)//connect
	{
		active_disconnect_reason=BLE_PAIR_REBOOT_ANA_AAA;//active discon
	}
	else
	{
		user_reboot(BLE_PAIR_REBOOT_ANA_AAA);//pair reboot
	}
//telink public
}

/**
 * @brief       This function pair check
 * @return      
 * @note        
 */
void three_speed_switch_pair()
{
	if(fun_mode==RF_1M_BLE_MODE)//ble mode
	{
		if ((((btn_value&COMB_BTN_PAIR)==COMB_BTN_PAIR)||((btn_value&MS_BTN_PAIR)==MS_BTN_PAIR)) && (clock_time_exceed(btn_tick, 2100000))) 
		{
			ble_start_pair();//start pair
		}
	}
	else if(fun_mode == RF_2M_2P4G_MODE)//24g
	{
		if ((((btn_value&COMB_BTN_PAIR)==COMB_BTN_PAIR)||((btn_value&MS_BTN_PAIR)==MS_BTN_PAIR)) && (clock_time_exceed(btn_tick, 2100000))) 
		{
			d24_start_pair();//start 24g pair
		}
	}
}

void mode_change_check_aaa(u16 check_ms)
{
	static u32 key_press_hold_tick = 0;
	static u32 key_release_hold_tick = 0;
	static u8 mode_btn_release_cnt = 0;
	u8 need_change_mode=0;

	if ((btn_value==COMB_BTN_PAIR) || (btn_value==MS_BTN_PAIR))
	{ //key press
		
		if ((clock_time_exceed(key_press_hold_tick, check_ms*1000))&&(pair_flag == 0))
		{
			power_on_tick = 0;
			
			if (fun_mode == RF_1M_BLE_MODE) 
			{
	    		ble_start_pair(); //call ble_start_pair();
	    		btn_value = 0;
				last_btn_value = 0;
	        }
			else
			{
				d24_start_pair();
			}
		}
		

		key_release_hold_tick = clock_time();
	}
	else if((last_btn_value ==MS_BTN_PAIR))
	{
		
		mode_btn_release_cnt++;
		if ((pair_flag == 1)&&(deep_flag==BLE_PAIR_REBOOT_ANA_AAA))
		{
				if(mode_btn_release_cnt>=2)
				{
					need_change_mode=1;
					printf("pair  release twice\n");
				}
           }
		else
		{
				need_change_mode=1;
		}
		if ((mode_change_flag == 0) && (need_change_mode==1))
		{
			//my_fifo_reset(&fifo_km); //clear fifo

			
				mode_change_flag = 1;
				
				clear_pair_flag();
		
				if (fun_mode == RF_1M_BLE_MODE)
	            {
	              	if (connect_ok)
					{ //when BLE connecting, disconnect first and set disconnect reason = switch mode change
						active_disconnect_reason = MODE_CHANGE_REBOOT_ANA_AAA;
					}
					else
					{ //no connect, direct reboot
						flash_dev_info.mode = RF_2M_2P4G_MODE;
	    				save_dev_info_flash();
	    				user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
					}
	            }
	            else
	            { //when 2.4G mode, direct reboot
	            	flash_dev_info.mode = RF_1M_BLE_MODE;
					flash_dev_info.mast_id = 0;
	            	save_dev_info_flash();
	            	user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
	            }
		}
	}
	else
	{ //key release
		if (mode_change_flag)
		{
			if (key_release_hold_tick == 0)
			{
				key_release_hold_tick = clock_time();
			}
			else if (clock_time_exceed(key_release_hold_tick, 150*1000)) //release time enough
			{
				mode_change_flag = 0;
			}
		}
		key_press_hold_tick = clock_time(); //clear press count
	}
}


/**
 * @brief       This function change muti device
 * @return      
 * @note        
 */
void muti_device_change()
{
    if (fun_mode == RF_1M_BLE_MODE)//ble mode
    {   
        if (last_btn_value == MS_BTN_DEVICE_1) //dev 1
        {
            if (flash_dev_info.mast_id != 0) //1
            {
                flash_dev_info.mast_id = 0; //mas 0
                if (connect_ok)//con
                {

                    active_disconnect_reason=MUTI_DEVICE_REBOOT_ANA_AAA;//active discon
                }
                else
                {
					clear_pair_flag();//clear pair
                    save_dev_info_flash(); //save dev info
                    user_reboot(MUTI_DEVICE_REBOOT_ANA_AAA);//reboot
                }
            }
        }
        else if (last_btn_value == MS_BTN_DEVICE_2)//dev 2
        {

            if (flash_dev_info.mast_id != 1) //2
            {
                flash_dev_info.mast_id = 1;//mast 1
                if (connect_ok)//con
                {
                    active_disconnect_reason=MUTI_DEVICE_REBOOT_ANA_AAA;//active discon
                }
                else
                {
					clear_pair_flag();//clear pair
                    save_dev_info_flash();//save dev info
                    user_reboot(MUTI_DEVICE_REBOOT_ANA_AAA);//reboot
                }
            }
        }
        /*  else if (last_btn_value == MS_BTN_DEVICE_3) //dev 3
        {
            if (flash_dev_info.mast_id != 2) //2
            {
                flash_dev_info.mast_id = 2;//mas 2
                if (connect_ok) //con
                {
                    active_disconnect_reason=MUTI_DEVICE_REBOOT_ANA_AAA;//act discon
                }
                else
                {
					clear_pair_flag();//clear pair
                    save_dev_info_flash();//save dev
                    user_reboot(MUTI_DEVICE_REBOOT_ANA_AAA);//reboot
                }
            }
        }
		 else if (last_btn_value == MS_BTN_DEVICE_4)//dev 4
        {
            if (flash_dev_info.mast_id != 3) //2
            {
                flash_dev_info.mast_id = 3;//mas 3
                if (connect_ok)//con ok
                {
                    active_disconnect_reason=MUTI_DEVICE_REBOOT_ANA_AAA;//act discon
                }
                else
                {
					clear_pair_flag();//clear pair
                    save_dev_info_flash();//save dev info
                    user_reboot(MUTI_DEVICE_REBOOT_ANA_AAA);//reboot
                }
            }
        }*/
    }	
}


/**
 * @brief       This function proc btn
 * @param[in]   event_new	- has evnt new
 * @return      
 * @note        
 */
void button_process_telink_v21a(u8 event_new)
{
	//if ((last_btn_value & MS_BTN_AUTO_DRAW) && event_new)
	if ((last_btn_value&MS_BTN_K4)&&event_new)//k4
	{
		auto_draw_flag^=0x01;//auto draw update
	}

    //if ((last_btn_value & MS_BTN_REPORT_RATE) && event_new)
	if ((last_btn_value & MS_BTN_K5) && event_new)//k5
    {
    	if(report_rate == 8){//8
			report_rate = 4;//4
		}
		else if(report_rate == 4){//4
			report_rate = 2;//2
		}
		else if(report_rate == 2){//2
			report_rate = 1;//1
		}
		else{
			report_rate = 8;//8
		}
		#if BLT_APP_LED_ENABLE
        dpi_led_set(report_rate);//set dpi
        #endif
		//report_rate_index=(report_rate_index+1)%4;
		//report_rate=1<<(3-report_rate_index);//125/250/500/1000
		if(fun_mode ==RF_2M_2P4G_MODE)//24g mode
		{
		 	analog_write(DEEP_ANA_REG2, report_rate);//write ana reg2
		//	user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
		}
    }

    //attribute_data_retention_user static u8 d24g_power_on_Pair_flag = 0;
    //-----------------cpi ------------------------

	if((last_btn_value & MS_BTN_PAIR)&& event_new)
	{
		printf("PAIR press\n");
	}
	if((last_btn_value & MS_BTN_LEFT)&& event_new)
	{
		
		printf("left press\n");
	}
	if((last_btn_value&MS_BTN_MODE)&&event_new)
	{
		printf("btn mode press\n");
	}

	#if(MICDEMO==0)
	mode_change_check_aaa(KEY_PAIR_PRESS_HOLD_CHECK_TIME);
	#else
	micdemo_mode_change_check();
	#endif
    //-----------------pair ------------------------
    if(pair_flag==0)//no pair
    {
		//if(switch_type==THREE_SPEED_SWITCH)
	    //{
	    #if(MICDEMO)
			three_speed_switch_pair();//check pair
		#endif
		//}
		//else
		//{
			//two_speed_switch_pair();
	  	//}	
	  	#if(MICDEMO)
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

   
  //-----------------muti_device switch ------------------------
	muti_device_change();//muti device deal

}




/**
 * @brief       This function get mouse btns value
 * @return      
 * @note        
 */
u8 btn_get_value()
{
    u8 ret = 0;

    _attribute_data_retention_user static u8 debounce = 0;//debounce time
    u16 now_value = 0; //init 0
    _attribute_data_retention_user static u16 last_value = 0;//mark last value

    now_value = scan(0);

    if (last_value != now_value)	// new press and new rel is report
    {
        reset_idle_status();//reset idle parameters
        debounce = 1;//debouce set 1
        last_value = now_value;//update last value
    }

    if(debounce) //debouce not 0
    {
        debounce++; //debounce ++
        if(debounce == 3)	//debounce time = (3-1)*7ms = 14ms
        {
            ret = NEW_KEY_EVENT_AAA; //has new key evnt
            ms_data.btn = now_value & 0X1F;	//btn value	
			if((now_value &0xff00)!= 0)
			{
				ret = 0;
			}
            btn_value = now_value;	//Save the current button value
            btn_tick = clock_time();				//this two need for long press ,and not send to app use
			
	    #if DOUBLE_CLICK_LEFT_FUN_ENABLE//double left click
            if (btn_value == MS_BTN_DOUBLE_LEFT) //double left
            {
                has_double_click_left = 1;//has double click left
                double_click_left_tick = 0; //update double click tick
            }
	    #endif
		
            debounce = 0;//reset debounce value to 0
        }
    }

    button_process_telink_v21a(ret);//button proc
	
    //btn_set_wakeup_level_suspend(1);
    return ret;

}
#endif

#if WHEEL_FUN_ENABLE_AAA //wheel fun en

/**
 * @brief       This function set wheel gpios as wake up gpios
 * @param[in]   enable	- en
 * @return      
 * @note        
 */
void wheel_set_wakeup_level_suspend(u8 enable)
{
//#if 1

    if (gpio_read(PIN_WHEEL_1))//gpio 1
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_1, 0, enable); //low wakeup suspend
        //connected_idle_time_count_ykq=0;
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_1, 1, enable);//high wakeup suspend
    }

    if (gpio_read(PIN_WHEEL_2))//read 1
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 0, enable); //low wakeup suspend
        //connected_idle_time_count_ykq=0;
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 1, enable);//high wakepup suspend
    }
//#endif
}


/**
 * @brief       This function set wheel gpios as wake up gpios
 * @return      
 * @note        
 */
void wheel_set_wakeup_level_deep()
{
//#if 1
    if (gpio_read(PIN_WHEEL_1))//read 1
    {

        cpu_set_gpio_wakeup(PIN_WHEEL_1, 0, 1);//low wake up
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_1, 1, 1);//high wake up
    }

    if (gpio_read(PIN_WHEEL_2))// read 1
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 0, 1);//low wake up
    }
    else
    {
        cpu_set_gpio_wakeup(PIN_WHEEL_2, 1, 1);//high wake up
    }
//#endif
}


/**
 * @brief       This function init wheel hardware
 * @return      
 * @note        
 */
static void wheel_init_hw()
{



    write_reg8(0xd2, WHEEL_ADDRES_D2); // different gpio different value
    write_reg8(0xd3, WHEEL_ADDRES_D3);
/*#if 0
    u8 core_reg65 = read_reg8(0x65);
    core_reg65 |= (BIT(0) | BIT(5));
    write_reg8(0x65, core_reg65);  //wheel clk enable
#else*/
    rc_32k_cal();
    BM_SET(reg_clk_en0, FLD_CLK0_QDEC_EN);
//#endif

    write_reg8(0xd7, 0x01);  //BIT(0)      	BIT(1)  wakeup enable

    write_reg8(0xd1, 0x01);  //filter   00-07     00 is best


}

/**
 * @brief       This function prepare wheel data
 * @return      
 * @note        
 */
u32 mouse_wheel_prepare_tick(void)
{

    write_reg8(0xd8, 0x01);
    return clock_time();
}

/**
 * @brief       This function process wheel 
 * @param[in]   wheel_prepare_tick	-
 * @return      
 * @note        
 */
_attribute_ram_code_ s8 mouse_wheel_process(u32 wheel_prepare_tick)
{
    s8 ret = 0;
    while(read_reg8(0xd8) & 0x01) //Query whether the decoder data is loaded successfully
    {
#if (MODULE_WATCHDOG_ENABLE)
		wd_clear(); //clear watch dog
#endif
        if(clock_time_exceed(wheel_prepare_tick, 260))	////4 cylce is enough: 4*1/32k = 1/8 ms
        {
            write_reg8(0xd6, 0x01); //reset  d6[0]
            write_reg8(0xd6, 0x00);
            break;
        }
    }

//#if 1	//(WHEEL_TWO_STEP_PROC)
    _attribute_data_retention_user static signed char accumulate_wheel_cnt;
    _attribute_data_retention_user static signed char wheel_cnt;
	
    wheel_cnt = read_reg8(0xd0);//get wheel cnt
    wheel_cnt += accumulate_wheel_cnt;

    if (wheel_cnt & 1) //Ææ
    {
        accumulate_wheel_cnt = wheel_cnt > 0 ? 1 : -1;//acc wheel
    }
    else  //Å¼
    {
        accumulate_wheel_cnt = 0;//acc wheel cnt reset 0
    }
	
    ret = (wheel_cnt / 2);//wheel/2


    return ret;
}


#if PM_DEEPSLEEP_RETENTION_ENABLE
s8 wheel_vaule = 0;

/**
 * @brief       This function get wheel value 1
 * @return      
 * @note        
 */
_attribute_ram_code_ u8 wheel_get_value_1()
{
    u8 ret = 0; //init 0

    u8 wheel_now = 0;//init 0
    wheel_vaule = 0; //wheel value init 0

    if (!gpio_read(PIN_WHEEL_1)) //left
    {
        wheel_now |= 0x01; //left update
    }
    if (!gpio_read(PIN_WHEEL_2)) //left
    {
        wheel_now |= 0x02;//update
    }
    if (wheel_now != wheel_pre)//new wheel value
    {
        wheel_pre = wheel_now;//update wheel pre
        ret = WHEEL_DATA_EVENT_AAA; //has wheel evnt
        wheel_status = ((wheel_status << 2) & 0x3F) + wheel_now; //get wheel status
        if ((wheel_status == 0x07) || (wheel_status == 0x38))//wheel status
        {
            wheel_vaule++;//wheel value ++
        }
        else if ((wheel_status == 0x0b) || (wheel_status == 0x34)) //wheel status
        {
            wheel_vaule--; //wheel value--

        }

    }
    return ret;
}
#endif

/**
 * @brief       This function get wheel value
 * @param[in]   wheel_prepare_tick	- 
 * @return      
 * @note        
 */
_attribute_ram_code_ u8 wheel_get_value(u32 wheel_prepare_tick)
{
    u8 ret = 0;
    s8 wheel_now = 0;

#if PM_DEEPSLEEP_RETENTION_ENABLE //ret
    ret = wheel_get_value_1(); //get value 1
#endif

    wheel_now = mouse_wheel_process(wheel_prepare_tick);//wheel now

#if PM_DEEPSLEEP_RETENTION_ENABLE//ret 
    if(wheel_now == 0)//no wheel
    {
        wheel_now = wheel_vaule;//update wheel now
    }
#endif

    if(user_cfg.wheel_direct != U8_MAX)//if wheel dir is not ff
    {
        wheel_now = -wheel_now;//change dir
    }

    ms_data.wheel = wheel_now;//set ms wheel data now
    if (wheel_now != 0)//have wheel data
    {
        ret = WHEEL_DATA_EVENT_AAA;//report wheel action
    }

    //wheel_set_wakeup_level_suspend(1);
    return ret;
}
#endif




/**
 * @brief       This function init hardware
 * @return      
 * @note        
 */
void hw_init()
{
#if WHEEL_FUN_ENABLE_AAA //wheel fun en
    wheel_init_hw();//wheel init
#endif

#if BUTTON_FUN_ENABLE_AAA //btn fun en
    btn_init_hw(); //btn hw init
#endif

#if BLT_APP_LED_ENABLE //led en
    led_hw_init();//led init
#endif

#if SENSOR_FUN_ENABLE_AAA //sensor en
    OPTSensor_Init(1);//sensor init

    if (!deepRetWakeUp) //no ret
    {
	#if DPI_SAVE_FLASH //dpi save in flash
        if (fun_mode == RF_1M_BLE_MODE) //ble mode
        {
            sensor_dpi_set(flash_dpi_info.bt_dpi);//set dpi
        }
        else if(fun_mode == RF_2M_2P4G_MODE)//24g mode
        {
            sensor_dpi_set(flash_dpi_info.d24g_dpi);//set 24g dpi
        }
	 else
	 {
  		sensor_dpi_set(flash_dpi_info.usb_dpi);//set usb dpi
	 }
	#else
        sensor_dpi_set(flash_dpi_info.bt_dpi);//set bt dpi
	#endif
    }

    output_dev_info.sensor_type = sensor_type; //sensor type
    output_dev_info.sensor_pd1 = product_id1;  //pd1
    output_dev_info.sensor_pd2 = product_id2;  //pd2
    output_dev_info.sensor_pd3 = product_id3;  //pd3
	
    #if TEST_MCU_CURRENT_DEBUG
    OPTSensor_Shutdown();//shut down sensor if test mcu current
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

/**
 * @brief       This function resume data fifo
 * @param[in]   cnt	- txcnt
 * @param[in]   rptr	- rptr
 * @return      
 * @note        
 */
void resume_data_fifo_aaa(u8 cnt,u8 rptr)
{
	tx_fifo_aaa.count+=cnt;//update tx count
	tx_fifo_aaa.rptr=rptr;//update rptr
}



/**
 * @brief       This function .push report fifo
 * @return      
 * @note        
 */
void push_report_fifo()
{

	if (has_new_report_aaa & HAS_MOUSE_REPORT)
    {
        // if (combination_flag)
        //{

            // memset(&ms_data.btn, 0, sizeof(mouse_data_t));
        //}

        // if (fun_mode == RF_1M_BLE_MODE)
        //{
            if (push_data_fifo_aaa(&ms_data.btn, HAS_MOUSE_REPORT, sizeof(mouse_data_t)))//push mouse data to fifo
            {
                has_new_report_aaa &= ~HAS_MOUSE_REPORT;//reset mouse report
            }
        //}
        // else
        //{
            //  has_new_report_aaa &= ~HAS_MOUSE_REPORT;
        //}

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
			need_ms_notify = 1;
			bls_att_pushNotifyData(HID_MOUSE_REPORT_INPUT_DP_H, &buf.btn, sizeof(mouse_data_t));//notify btn data
			bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);//discon
			return;
	    }

		u8 txFifoNumber = blc_ll_getTxFifoNumber();//get tx fifo num
		
		if(need_ms_notify)//need send ms data
		{
			if(txFifoNumber < RF_TX_FIFO_ALLOW_NUM)//txfifo num ok
			{
				u8 status = bls_att_pushNotifyData(HID_MOUSE_REPORT_INPUT_DP_H, &buf.btn, sizeof(mouse_data_t));//att send  ms data
	            pc_status_through_ble = 1;
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
	   		if(txFifoNumber < RF_TX_FIFO_ALLOW_NUM && (pc_status_through_ble != 0))//tx fifo num ok and not en pc sleep
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

/**
 * @brief       This function check fifo has data or not
 * @return      
 * @note        
 */
u8 check_fifo_has_data()
{
	 return (tx_fifo_aaa.count>0);
   // return (tx_fifo_aaa.wptr != tx_fifo_aaa.rptr);
}

/**
 * @brief       This function clear fifo
 * @return      
 * @note        
 */
void clear_fifo()
{
    tx_fifo_aaa.wptr = 0;//wptr 0
    tx_fifo_aaa.rptr = 0;//rptr 0
	tx_fifo_aaa.count=0;//count 0
}

/**
 * @brief       This function clear press key
 * @return      
 * @note        
 */
u8 clear_press_key_aaa()
{
    u8 buf[sizeof(mouse_data_t)] = {0, 0, 0, 0};
    push_data_fifo_aaa(buf, HAS_MOUSE_REPORT, sizeof(mouse_data_t));//push mouse data
    return 1;
}


/**
 * @brief       This function init switch type
 * @return      
 * @note        
 */
void switch_type_init()
{
#if 0//BUTTON_FUN_ENABLE_AAA
	switch_type=THREE_SPEED_SWITCH; //three swhtich 
	
	if(!gpio_read(PIN_USB_INSERT))
	{
	    	if (IS_BLE_MODE_AAA)
	    	{
	        	flash_dev_info.mode = RF_1M_BLE_MODE;
	    	}
	    	else
	    	{
	        	flash_dev_info.mode = RF_2M_2P4G_MODE;
	    	}
	}
	else
	{
	    if (MODE_CHANGE_TO_CHARGING_REBOOT_ANA_AAA == analog_read(DEEP_ANA_REG0)) {

			//my_printf_aaa("usb charging run to 2.4 mode \r\n");
			flash_dev_info.mode = RF_2M_2P4G_MODE;
	    } else {
            //my_printf_aaa("boot reason is %x,  usb mode \r\n", analog_read(DEEP_ANA_REG0));
		    flash_dev_info.mode = USB_MODE;
	    }
	}
#endif
}

/**
 * @brief       This function check gpio mode
 * @return      
 * @note        
 */
void mode_gpio_check()
{
#if ((ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG==0))//no adapt hw auto draw
	if (IS_BLE_MODE_AAA)//read 0
	{
    	flash_dev_info.mode = RF_1M_BLE_MODE;//ble mode
	}
	else
#endif
	{
    	flash_dev_info.mode = RF_2M_2P4G_MODE;//24g mode
	}
}
void micdemo_mode_change_check()
{
	static u8 gpio_stable_count =0;
	//if(fun_mode!=USB_MODE)
	{
		if(gpio_read(PIN_USB_INSERT))
			{
				//printf("USB mode high \n");
				if(fun_mode!=USB_MODE)\
					{
						printf("change to usb mode\n");
						clear_pair_flag();
						flash_dev_info.mode = USB_MODE;
		    			save_dev_info_flash();
		    			user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
					}
			}
		else
		{
			mode_gpio_check();
			gpio_stable_count++;
			if(gpio_stable_count>=3)
			{
				if(fun_mode!=flash_dev_info.mode)
				{
					clear_pair_flag();
			
					if (fun_mode == RF_1M_BLE_MODE)
		            {
		            	printf("change to 24G MODE\n");
		              	if (connect_ok)
						{ //when BLE connecting, disconnect first and set disconnect reason = switch mode change
							active_disconnect_reason = MODE_CHANGE_REBOOT_ANA_AAA;
						}
						else
						{ //no connect, direct reboot
							flash_dev_info.mode = RF_2M_2P4G_MODE;
		    				save_dev_info_flash();
		    				user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
						}
		            }
		            else
		            { //when 2.4G mode, direct reboot
		            	printf("change to BLE mode\n");
		            	flash_dev_info.mode = RF_1M_BLE_MODE;
						flash_dev_info.mast_id = 0;
		            	save_dev_info_flash();
		            	user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
		            }
				}
			}
		}
	}
}
/**
 * @brief       This function check usb host status
 * @return      
 * @note        
 */
void usb_host_status_check(void)
{
	//u8 now_status = 0;//0: pc connected, 1 :PC sleep ,2: pc off	
	static u8 last_status=0xff;
	if(reg_usb_host_conn & BIT(7)) 
	{
		 now_usb_connected_flag=1;
		if((reg_usb_mdev & BIT(2)) && (reg_irq_src & FLD_IRQ_USB_PWDN_EN)) 
		{
			usb_device_status = USB_DEVICE_CHECK_PC_SLEEP;//pc sleep
		} 
		else 
		{
			usb_device_status = USB_DEVICE_CONNECT_PC;//con pc
		}
	} 
	else 
	{
		now_usb_connected_flag=0;
		usb_device_status = USB_DEVICE_DISCONECT_PC;//discon pc
	}
	if(last_status!=usb_device_status)//new usb device status
	{
		my_printf_aaa("usb_device_status=%d\r\n",usb_device_status);//debug usb device status
        if((usb_device_status==USB_DEVICE_CONNECT_PC))// fist press key then restart pc
		{
			
			if(last_status==USB_DEVICE_DISCONECT_PC)
			{
					
					//usb_io_printf("pc_power_on");
				//	if(has_normal_key_press)
					//{
						//push_usb_fifo_aaa(NORMAL_KB_DATA_TYPE, normal_last_data, 8);
				//	}
				
			}
		}
		else if(usb_device_status==USB_DEVICE_CHECK_PC_SLEEP)
		{
			//need_enter_suspend_flag=1;
			//led_status_out(0);
		}
		else if(usb_device_status==USB_DEVICE_DISCONECT_PC)
		{
			//need_enter_suspend_flag=1;
			//led_status_out(0);
		}
		
		last_status=usb_device_status;//update usb device status
		
		
	}
	if((usb_device_status==USB_DEVICE_CHECK_PC_SLEEP))
	{
		//mcu_enter_suspend();
	}
}

#if D24G_OTA_ENABLE_AAA

/**
 * @brief       This function check ota key
 * @param[in]   check_ms	- the press last time
 * @return      
 * @note        
 */
void ota_key_check_aaa(u16 check_ms)
{
	static u32 key_press_hold_tick;

	if(btn_value == COMB_BTN_OTA)//ota btn
	{
		my_fifo_reset(&fifo_km);//reset fifo

		if(clock_time_exceed(key_press_hold_tick,check_ms*1000))//over time
		{
			power_on_tick = 0;//reset power on tick
			
			clear_pair_flag();//clear pair flag

			user_reboot(D24G_OTA_ENABLE_REBOOT_ANA_AAA);//user reboot
		}
	}
	else
	{
		key_press_hold_tick=clock_time();
	}
}
#endif




void mouse_xy_multiple()
{
#if MUTI_SENSOR_ENABLE
    s32 x = 0, y = 0;//init x,y with 0
    if (xy_multiple_flag == MULTIPIPE_1_DOT_5)//x multi 1.5
    {
        x = ms_data.x;//now value
        y = ms_data.y;//now value
        x = (x * 3) / 2;//1.5 multi x now value
        y = (y * 3) / 2;//1.5 multi y now value

        ms_data.x = x;//update sensor x data in mouse pkt
        ms_data.y = y;//update sensor y data in mouse pkt
    }
#endif
}



/**
 * @brief       This function get mouse sensor data 
 * @return      
 * @note        
 */
void mouse_task_when_rf()
{
	
#if SENSOR_FUN_ENABLE_AAA

	if (OPTSensor_motion_report(0))//get sensor data
    {
		if(ms_data.x||ms_data.y)
		{
        	has_new_key_event |= SENSOR_DATA_EVENT_AAA;

        	mouse_xy_multiple();
			check_sensor_dircet(user_cfg.sensor_direct);//check sensor direction
        	//adaptive_smoother();
		}
    }
    else
    {
		ms_data.x = 0;//reset pkt x value 0
		ms_data.y = 0;//reset pkt y value 0
    }

#else
  if(auto_draw_flag){
  has_new_key_event |= Draw_a_square_test();}
#endif


}
/**
 * @brief       This function  auto draw a square
 * @return      
 * @note        
 */
u8  Draw_a_square_test()
{
    _attribute_data_retention_user static int x = 0;
    _attribute_data_retention_user static u8 flag = 0;
    int step = 4;
 #if ((ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG==0))
    if(auto_draw_flag==0)
    {
    	ms_data.x = 0;
        ms_data.y = 0;
		return 0;
    }
#endif
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


/**
 * @brief       This function clear pair flag
 * @return      
 * @note        
 */
void clear_pair_flag()
{
	printf("---clear_pair_flag=%d.\n",pair_flag);
    pair_flag = 0;
    analog_write(USED_DEEP_ANA_REG1, ana_reg1_aaa & CLEAR_PAIR_ANA_FLAG);
}


/**
 * @brief       This function set pair flag
 * @return      
 * @note        
 */
void set_pair_flag()
{
	printf("---set_pair_flag=%d.\n",pair_flag);
    pair_flag = 1;
    analog_write(USED_DEEP_ANA_REG1, ana_reg1_aaa | PAIR_ANA_FLG);
}


/**
 * @brief       This function write data to ana0
 * @param[in]   buf	- 1 byte data
 * @return      
 * @note        
 */
void write_deep_ana0(u8 buf)
{
    deep_flag = buf;
    analog_write(DEEP_ANA_REG0, buf);
}


/**
 * @brief       This function use for reboot chip
 * @param[in]   reason	- reboot reason
 * @return      
 * @note        
 */
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
		//printf("has sensor data\n");
        mouse_xy_multiple();
        check_sensor_dircet(user_cfg.sensor_direct);
        adaptive_smoother();
    }
    else
    {
        ms_data.x = 0;
        ms_data.y = 0;
    }
#else

    if ((blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) && ((ble_status_aaa == T5S_CONNECTED_STATUS_AAA)))
    {
        has_new_key_event |= Draw_a_square_test();
    }
#endif

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






