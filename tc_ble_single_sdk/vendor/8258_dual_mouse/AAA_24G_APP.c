/********************************************************************************************************
 * @file     AAA_24G_APP.c
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

#include "AAA_public_config.h"

extern void mouse_task_when_rf();

#define DEVICE_TYPE_INDEX  1// 1 mouse  ,2 keyboard

#define  DATA_3_CHOOSE_1_ENABLE   0

#define IDLE_TIME_S    3



typedef enum
{
    STATE_POWERON = 0,
    STATE_PAIRING,
    STATE_NORMAL,

} MOUSE_MODE;

 static u8 last_connect_ok=0xff;


 u8 device_status = 0;
 u32 dongle_id;
 u8 mouse_send_need_f = 0;
 volatile u32 no_ack = 0;
 volatile u32 start_rf_tick = 0; //start rf tick
 u8 send_kb_flag =0;
 u8 D24G_APP_cmd[16]={0x0a,0,0};
 u8 last_D24G_APP_cmd[16]={0x0a,0,0};
 u8 has_d24g_app_cmd = 0;
 u8 rf_loop_time =0;
 u8 has_receive_pc_data_flag =0;
 rf_packet_t rf_txBuf =
 {
	 65,//14,	 // dma_len
	 64, // rf_len
 
 };
 rf_packet_t	rf_pair_buf =
{
    20,	// dma_len
    19,	// rf_len
};
pair_data_t *p_pair_dat=(pair_data_t*)&rf_pair_buf.dat[0];

 rf_packet_t rf_km_buf =
{
#if DATA_3_CHOOSE_1_ENABLE
	55,	// dma_len
    54,	// rf_len
#else
    14,	// dma_len
    13,	// rf_len
#endif
 };
 
 km_3_c_1_data_t *p_km_data=(km_3_c_1_data_t*)&rf_km_buf.dat[0];

 u32 wakeup_next_tick = 0;

 u8 pair_success_flag = 0;
u32 pair_success_tick = 0;
u8 has_mouse_data_flag = 0;

MYFIFO_INIT (fifo_km, 12, 8);//The size must be a multiple of 4 bytes

_attribute_data_retention_user u8 private_key[16] =
{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};





_attribute_ram_code_ u8  rf_rx_process(rf_packet_t *rf_ack)
{
	if(device_status == STATE_PAIRING) //pair status
	{
	    
       if ((rf_ack->cmd == PAIR_ACK_CMD) && (memcmp((u8*)&rf_ack->did, (u8*)&(rf_txBuf.did),ID_LEN)==0))//if cmd ==pair_ack_cmd, and ack data did == pair data did
		{
			memcpy((u8*)&dongle_id, (u8*)&rf_ack->dat[0], 4);
   
        	pair_success_flag = 1;//pair success flag set 1
        	pair_success_tick = clock_time();
			printf("pair successful now\n");
        	return 1;
    	}
		
	}
	else if(device_status == STATE_NORMAL)//normal status
	{
		if((mouse_send_need_f == 2))//mouse ack
		{

#if (DEVICE_TYPE_INDEX==1)//mouse 
            //my_printf_aaa("cmd %d\n", km_ack_dat_ptr->cmd);
            //printf("the cmd is %d\n",rf_ack->cmd);
            if((rf_ack->cmd&0x40) !=0)
			{
				 has_receive_pc_data_flag =1;
				 	
				 memcpy (&D24G_APP_cmd[1], (u8*)&rf_ack->dat[1],5);
				 if(memcmp(&last_D24G_APP_cmd[1],&D24G_APP_cmd[1],5))
				 {
				 	 memcpy(&last_D24G_APP_cmd[1],&D24G_APP_cmd[1],5);
					  has_d24g_app_cmd = 1;
					  printf("the cmd is %x, %x,%x,%x,%x\n",D24G_APP_cmd[1],D24G_APP_cmd[2],D24G_APP_cmd[3],D24G_APP_cmd[4],D24G_APP_cmd[5]);
				 }
				 //printf("the cmd is %x, %x,%x,%x,%x\n",D24G_APP_cmd[0],D24G_APP_cmd[1],D24G_APP_cmd[2],D24G_APP_cmd[3],D24G_APP_cmd[4]);
				// has_d24g_app_cmd = 1;
			 
			}
			else
			{
				has_receive_pc_data_flag =0;
			}
			if((rf_ack->cmd&0x0f) == (rf_txBuf.cmd&0x0f))//ack data cmd == mouse ack cmd
			{
				return 1;
			}
#endif
		}
	}

	
    return 0;
}


void d24_start_pair()
{
    set_pair_flag();
    user_reboot(CLEAR_FLAG_ANA_AAA);
	adv_count=0;
	adv_begin_tick=clock_time()|1;
}

/**
 * @brief       This function init 
 * @return      
 * @note        
 */
void d24_user_init()
{
	printf("---d24_user_init.\n"); //user init debug
    u32 device_id = ((user_cfg.dev_mac<<8)|DEVICE_TYPE_INDEX);//did set
	//rf_txBuf.did = (0x98|DEVICE_TYPE_INDEX);
	rf_txBuf.did = device_id;
	p_pair_dat->cmd = PAIR_CMD; //pair cmd
	p_pair_dat->did = device_id; //did
	p_km_data->did = device_id; //did


    p_km_data->cmd = MOUSE_CMD; //mouse cmd



	printf("---device_id=0x%x.\n",device_id); //debug did

    dongle_id = flash_dev_info.dongle_id; //dongleid

#if  ENTER_PAIR_WHEN_NEVER_PAIRED_ENABLE //pair flag
    if ((dongle_id == U32_MAX) || (dongle_id == 0)) { // no valid dongle id
        set_pair_flag(); //set pair
    } else {
        has_been_paired_flag = 1; //record have pair flag
    }
#endif

#if ((ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG==1))//auto draw adapt en
	set_pair_flag();//set pair
#endif

    if (pair_flag) // pair flag 1
	{
        device_status = STATE_PAIRING; //device into pair status
		rf_rx_timeout_us = D24G_PAIR_TIMER_OUT; //pair time out
    } 
	else 
	{

        device_status = STATE_NORMAL; //device normal
		rf_rx_timeout_us = D24G_COMMUNICATION_TIMER_OUT; // communication time out
    }

    wakeup_next_tick = clock_time(); //next wake up tick update
    reset_idle_status(); //reset idle paras
}

////////////////////////////////////////////////////////////////////
u8 need_suspend_flag=0; 
u8 suspend_wake_up_enable; // suspend wake up en 
u8 rptr = 0;
u8 rx_fifo_count=0;
void check_rf_complet_status()
{
	static u32 ack_miss_no = 0; // ack miss no

	rf_state = RF_IDLE_STATUS; //rf reset to idle status

	if (device_ack_received) { // receive ack
		check_rf_fast_setting_flag(); // rf fasting flag check
		ack_miss_no = 0; //reset ack miss 0
		no_ack = 0; //reset no ack  0
		start_rf_tick = 0; //reset start rf tick 0

		if(device_status <= STATE_PAIRING) { // pair status
			if(pair_success_flag) { //pair success
				device_status = STATE_NORMAL; // state normal
				rf_rx_timeout_us = D24G_COMMUNICATION_TIMER_OUT; //rx timout
                connect_ok=1; // connect ok 1
    		    if (flash_dev_info.dongle_id != dongle_id) // new dongle id
		        {
		            flash_dev_info.dongle_id = dongle_id; //update dongle id
                    my_printf_aaa("pairing success------------\n"); // debug pair success
		            save_dev_info_flash(); // save dev 
		        }
				clear_pair_flag(); // clear pair flag
        		reset_idle_status(); //reset idle params
        		
			}
            my_fifo_reset(&d24g_txfifo); //reset fifo
		} 
        else if(device_status == STATE_NORMAL)//normal status
        {
			if (mouse_send_need_f)//need send data
			{
				my_fifo_pop(&d24g_txfifo);//rprt ++
			}
            connect_ok=1;//update connect ok
            //printf("the rf loop time is %d\n", rf_loop_time);
			rf_loop_time =0;
		}

		mouse_send_need_f = 0;

	}
	else
	{
		no_ack++;//noack ++
        ack_miss_no ++; //ack miss no ++

        if (ack_miss_no >=3) {//ack miss >=3
            device_channel = get_next_channel_with_mask(0, device_channel);//update channel
        }

        if(no_ack>125) {//noack times over 250
            connect_ok=0;//connect not ok
            printf("no ack over 125\n");
        }
	}  
}

unsigned short 	crc16_poly1[2] = {0, 0xa001};


_attribute_ram_code_sec_ unsigned short crc16_user (unsigned char *pD, int len)
{
    unsigned short crc = 0xffff;
    int i,j;

    for(j=len; j>0; j--)
    {
        unsigned char ds = *pD++;
        for(i=0; i<8; i++)
        {
            crc = (crc >> 1) ^ crc16_poly1[(crc ^ ds ) & 1];
            ds = ds >> 1;
        }
    }

     return crc;
}


void d24g_rf_loop()
{

	check_rf_fast_setting_time();//check fast setting time
	
	if (rf_state == RF_IDLE_STATUS)//rf status idle
	{
		if (device_status <= STATE_PAIRING)//device status not normal
		{
            if (user_cfg.rf_vid != U16_MAX) {//have rf vid
				set_pair_access_code(rf_access_code_16to32(user_cfg.rf_vid));//vid access code
            } else {
				set_pair_access_code(0x39517695);//set defualt access code
            }

			pair_success_flag = 0;//pair success flag set to 0
			if (device_status == STATE_PAIRING) {//pair status
			    rf_set_power_level_index(user_cfg.paring_tx_power);//set pair tx power
			}

			connect_ok = 0;//no connect
			
			rf_txBuf.rf_len=16+HEAD_LENGTH;
			rf_txBuf.cmd=PAIR_CMD;
			rf_txBuf.seq=0;

			//memcpy(&rf_txBuf.dat[0], (u8*)&device_id, 4);
			memcpy(&rf_txBuf.dat[0], (u8*)&private_key, 16);
			
			mouse_send_need_f = 1;//pairing
			start_rf_tick=0;//reset start rf tick
		} 
		else if(device_status == STATE_NORMAL) //status normal
		{
		
			set_data_access_code(flash_dev_info.dongle_id);//dongle id to be data access code
			rf_set_power_level_index(user_cfg.tx_power);//set data tx power

            if(mouse_send_need_f == 0)//mouse need send f
            {
                u8 *p =  my_fifo_get (&d24g_txfifo);//get data

				if (p)//if have data
                {      
					rf_txBuf.rf_len=p[0]+HEAD_LENGTH;
					rf_txBuf.cmd=p[1];
					if(has_receive_pc_data_flag)
					{
						rf_txBuf.cmd = (p[1]|0x20);
					}
					rf_txBuf.seq++;//seq_no ++
                    memcpy(&rf_txBuf.dat[0], (u8*)&p[2], p[0]);
					mouse_send_need_f=2;
				}
        	}
		}
	
		if (mouse_send_need_f) //send
		{
			rf_loop_time ++;
			rf_state=RF_TX_START_STATUS;//rf state to TX status
			rf_set_tx_rx_off();//must add
			device_ack_received = 0;//ack receive reset to 0
            rf_txBuf.dma_len=rf_txBuf.rf_len+1;
		    rf_stx_to_rx((u8*)&rf_txBuf.dma_len, rf_rx_timeout_us);//rf tx send data then enter rx mode
			reg_rf_irq_status = 0xffff;//irq status reset to ffff
		}

	}
	else if(rf_state==RF_RX_END_STATUS)
	{
        irq_device_rx();
		check_rf_complet_status();
	}
	else if(rf_state==RF_RX_TIMEOUT_STATUS)
	{
		check_rf_complet_status();
	}
}


/**
 * @brief       This function deal ui_loop
 * @return      
 * @note        
 */
void ui_loop_24g()
{
	static u32 idle_loop_24g_tick;
    static u32 tick_loop_24g;//tick 24g loop
    u8 wheel_flag = 0; //wheel flag init 0
    u32 wheel_prepare_tick; //wheel prepare tick

    ms_data.wheel=0;//wheel data set 0
	//kb_data_event();
	//kb_special_keys_event();
	if(clock_time_exceed(tick_loop_24g, 7000)) // 7ms ui loop
	{
		tick_loop_24g=clock_time();//update time

#if WHEEL_FUN_ENABLE_AAA//wheel fun en 
         wheel_flag = 1; //wheel falg set 1
		wheel_prepare_tick = mouse_wheel_prepare_tick();//prepare wheel tick
#endif

#if BUTTON_FUN_ENABLE_AAA
		 has_new_key_event |= btn_get_value();
#endif

		idle_status_poll();
		device_led_process();
	    if((connect_ok==0))
		{
#if BLT_APP_LED_ENABLE
	    	led_2p4_Adv_poll();
#endif
		adv_count_poll();//adv parameter poll
        }
	 }

#if WHEEL_FUN_ENABLE_AAA //wheel en 
     if (wheel_flag) //wheel flag en
         has_new_key_event |= wheel_get_value(wheel_prepare_tick);//get wheel data
#endif

	 mouse_task_when_rf();//get sensor data

 	 if ((device_status == STATE_NORMAL)) {//state normal
        if (has_new_key_event) {//has new key
			{
				has_new_key_event = 0;//reset has new mouse action flag 0
				reset_idle_status();//reset idle parameters
				//my_fifo_push(&fifo_km, &ms_data.btn, sizeof(mouse_data_t));//push btn data to fifo
				if(my_fifo_push_app(&d24g_txfifo, &ms_data.btn, sizeof(mouse_data_t),MOUSE_CMD)!=0)
					{
						//gpio_write(PIN_VOICE,1);
						
					}
			}
			//has_mouse_data_flag =1;
			idle_loop_24g_tick = clock_time();
        }
		else if((idle_count<3)||(ms_data.btn))
		{
			u8 *p = my_fifo_get(&d24g_txfifo);//get data
            if(p == 0) {//no data
				my_fifo_push_app(&d24g_txfifo, &ms_data.btn, sizeof(mouse_data_t),MOUSE_CMD);
			}		
		}
	 }
}


/**
 * @brief       This function deal 24g pm
 * @return      
 * @note        
 */
void pm_poll()
{
	u32 wake_src=0;//use for set wake src
	u32 interval=0;
	need_suspend_flag=0;//need suspend flag
#if(APP_24G_AUDIO_EN)
		if (ui_mic_enable)
		{
			bls_pm_setSuspendMask(SUSPEND_DISABLE);
			return;
		}
#endif

	if(rf_state==RF_IDLE_STATUS) { //rf idle status
		if(device_status <= STATE_PAIRING) { //no normal status
#if D24G_ADV_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA //enterdeep when adv timeout en
			if(adv_count>=D24G_ADV_TIMER_OUT) {//adv count>adv time out
		   		enter_deep_aaa();//enter deep
	   		}
#endif
		 	wake_src=PM_WAKEUP_TIMER;//set wake src to timer
		 	interval=8;//set wake up time interval
			need_suspend_flag = 1;//need suspend flag set to 1
		} else {
			if(my_fifo_get(&d24g_txfifo)==0) { //no data
				if((idle_count < 3)||(DEVICE_LED_BUSY)) //idle count <3 and no led event
				{
					if ((report_rate==8)||(report_rate==4))//if raport_rate>=4
					{
						need_suspend_flag = 1; //need suspend falg set 1
				 		wake_src=PM_WAKEUP_TIMER;//timer wrc
                        interval = report_rate; //wake up interval set to report rate
						
                        if(suspend_wake_up_enable) {//suspend wake up en
    					#if WHEEL_FUN_ENABLE_AAA // wheel fun en
                            wheel_set_wakeup_level_suspend(0);      //wheel gpio wake up disable
    					#endif //
						
    					#if BUTTON_FUN_ENABLE_AAA //btn fun en
                            btn_set_wakeup_level_suspend(0);//btn gpio wake up disable
    					#endif //
						
    					#if SENSOR_FUN_ENABLE_AAA //sensor fun en
                            sensor_set_wakeup_level_suspend(0);//sensor gpio wake up disable
    					#endif //
                            suspend_wake_up_enable = 0; //reset suspend wake up en
                        }

					}
				} else {
					#if D24G_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA //when connect time out en
						if(idle_count>=D24G_CONNECT_TIME_OUT) { //idle cnt > connect time out
							enter_deep_aaa();//enter deep sleep
						}
					#endif

					#if WHEEL_FUN_ENABLE_AAA //wheel fun en
						wheel_set_wakeup_level_suspend(1);//wheel wake up
					#endif

					#if BUTTON_FUN_ENABLE_AAA //btn fun en
				        btn_set_wakeup_level_suspend(1);//btn wake up
					#endif //

					#if SENSOR_FUN_ENABLE_AAA //sensor en
				        sensor_set_wakeup_level_suspend(1);//sensor wake up
					#endif //
						need_suspend_flag = 1;//need suspen 1
                        suspend_wake_up_enable = 1; //suspend wake up en
						wake_src=PM_WAKEUP_TIMER|PM_WAKEUP_PAD;//gpio and timer 
						#if SENSOR_MOTION_ENABLE
						interval = 1000;
					    printf("1s sleep now\n");
						#else
						interval = 100;//100 ms
						#endif
				}
			}
			else
			{
				if(no_ack > 2000) { //no ack over 2000
					no_ack = 2100; //set no ack 2100
					if(btn_value == 0)//no btn press
					{
						my_printf_aaa("no ack over 2000 so enter deep in 24g\n");
						enter_deep_aaa();//enter deep sleep
					}
				}
			}
			
			
		}

		if(need_suspend_flag) { //need suspend flag 1
			int wake_up_re = cpu_sleep_wakeup(SUSPEND_MODE, wake_src, (wakeup_next_tick+ interval*CLOCK_16M_SYS_TIMER_CLK_1MS));//enter pm sleep
			if((wake_up_re & WAKEUP_STATUS_PAD)== WAKEUP_STATUS_PAD)
				{
					printf("wake up from pad\n");
					reset_idle_status();
				}
            wakeup_next_tick=clock_time();//update wakeup_next_tick
			rf_drv_private_2m_init();//init rf private 2m
		}
		
	}
	
}

void d24_main_loop()
{
	u32 temp = 0;//tmp init 0
	static u32 tick_loop = 0; //tick loop 0
	#if APP_24G_AUDIO_EN
	proc_audio();
	#endif
#if (ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG||TEST_DRAW_A_SQUARE) //auto draw hw en
    //report_rate = 8;//report rate 1
#endif
	if(device_status <= STATE_PAIRING) //pairing status
	{
		temp = 8000;//8ms
	}
	else
	{
		temp = report_rate*1000;//report time
	#if APP_24G_AUDIO_EN
		if(ui_mic_enable)
			{
			 temp = 8000;
			 bls_pm_setSuspendMask (SUSPEND_DISABLE);
			}
		else
			{
				temp =report_rate*1000;
			}
		if(clock_time_exceed(pair_success_tick, 10*1000*1000)&& !ui_mic_enable)
		{
			pair_success_tick = clock_time();
			//ui_enable_mic(1);
		}
	#endif

	}

	if(need_suspend_flag)
	{
		tick_loop = clock_time()|1;
		ui_loop_24g();
	}
	else if(clock_time_exceed(tick_loop,temp))//tickloop over temp
	{
		tick_loop += temp*CLOCK_16M_SYS_TIMER_CLK_1US;//update tick loop
        wakeup_next_tick = clock_time();//update wakeup_next_tick
		ui_loop_24g();//ui loop
	}


	d24g_rf_loop();//rf state machine deal
	static u32 loop_24g = 0;
	if(clock_time_exceed(loop_24g,5*1000*1000))
		{
			printf("loop_d24g with %d loop time\n",temp);
			loop_24g = clock_time();
		}
	pm_poll();
}
