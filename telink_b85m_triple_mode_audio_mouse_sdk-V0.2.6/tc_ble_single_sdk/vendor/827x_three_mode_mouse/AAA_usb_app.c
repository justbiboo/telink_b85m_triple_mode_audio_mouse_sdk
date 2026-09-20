/********************************************************************************************************
 * @file     aaa_24g_app.c
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




static u32 notify_rsp_tick;
static u32 usb_pair_success_tick;

extern my_fifo_t usb_to_pc_data;
MYFIFO_INIT(usb_to_pc_data, 80, 4);



 u32 blt_ota_start_tick;

u8 usb_start_flag = 0; //usb start flag init 0

extern bool usb_host_conn; //usb host con



/**
 * @brief       This function en usb deep wakeup 
 * @return      
 * @note        
 */
void usb_vbus_deepWakeup_enable()
{
	 //cpu_set_gpio_wakeup(PIN_USB_INSERT, 1, 1); 
}


/*#if 0
unsigned short 		crc16_poly[2] = {0, 0xa001}; 

unsigned short crc16 (unsigned char *pD, int len)
{
    unsigned short crc = 0xffff;
    //unsigned char ds;
    int i,j;

    for(j=len; j>0; j--)
    {
        unsigned char ds = *pD++;
        for(i=0; i<8; i++)
        {
            crc = (crc >> 1) ^ crc16_poly[(crc ^ ds ) & 1];
            ds = ds >> 1;
        }
    }

     return crc;
}
#endif*/


/**
 * @brief       This function init notify rsp buf
 * @return      
 * @note        
 */
void notify_rsp_buf_init()
{
    //memset(notify_rsp_buf, 0, sizeof(notify_rsp_buf));
    //notify_rsp_buf_wptr = notify_rsp_buf_rptr = 0;
    my_fifo_reset(&usb_to_pc_data);//reset usb to pc data
}


/**
 * @brief       This function rsp buf to hci
 * @param[in]   data	- data
 * @param[in]   length	- data length
 * @return      
 * @note        
 */
int notify_rsp_buf2hci(u8 *data,u16 length)
{

	if(reg_irq_src & FLD_IRQ_USB_PWDN_EN)
	{
		//user_resume_host();
		reg_usb_ep_ctrl(USB_EDP_SPP_IN) = 0;
		return 1;
	}

	if (!reg_usb_host_conn)//no host con
	{
		reg_usb_ep_ctrl(USB_EDP_SPP_IN) = 0;//0
		return 1;
	}
	
	if(usbhw_is_ep_busy(USB_EDP_SPP_IN))//spp busy
	{
		return 0;
	}

	reg_usb_ep_ptr(USB_EDP_SPP_IN) = 0;//ptr new

	/*#if UART_PRINT_DEBUG_ENABLE
	my_printf_aaa("deviec_to_host=");
	for(u8 i=0;i<length;i++)
	{
		my_printf_aaa("%x ",data[i]);
	}
	my_printf_aaa("\r\n");
	my_printf_aaa("\r\n");
	#endif*/
					
		
	for(u8 i=0;i<length;i++)
	{
		reg_usb_ep_dat(USB_EDP_SPP_IN) = data[i];//put data to usb
	}

	usbhw_data_ep_ack(USB_EDP_SPP_IN);//usb spp ack

	return 1;
}


/**
 * @brief       This function show ota resut
 * @param[in]   result	- ota result
 * @return      
 * @note        
 */
int usb_ota_resut(u8 result)
{
	usb_data_t p;
	int count=0;
	u32 tick=0;
	tick=clock_time();
	int ret=0;
	p.report_id=0x06;
	p.opcode=0x02;
	p.length=3;
	p.dat[0]=0x06;
	p.dat[1]=0xff;
	p.dat[2]=result;
    while(1)
	{
		usb_handle_irq();//hanler usb irq
		if(clock_time_exceed(tick, 50000))//50ms
		{
			tick=clock_time();//update tick
			if(count==0)//count 0
			{
				ret=notify_rsp_buf2hci(&p.report_id,USB_OTA_LENGTH);//send ota 
				if(ret)
				{
					count=1;
				}
			}
			else
			{
				break;
			}
		}
		
	}
	//app_debug_ota_result(result);
	my_printf_aaa("usb_ota_result=%x\r\n",result);//debug result
	start_reboot();//reboot
	return ret;
}


/**
 * @brief       This function write usb ota data
 * @param[in]   p_usb	- usb ota data
 * @return      
 * @note        
 */
u8 usb_ota_write(usb_data_t *p_usb)
{
	static	u16 ota_index=0; //0
	static u16 start_index=0; //0
	static u32 flash_write_addr=0; //0
	static u8 first_data_buf[16]; //
	static u32 fw_size=0; //fw size 0
	static u8 ota_error_flag=0; //init 0
	ota_data_st *pd=(ota_data_st*)&p_usb->dat[0];

	if((pd->cmd== CMD_OTA_START)&&(p_usb->length==2))		
	{
		blt_ota_start_tick = clock_time();  //mark time
		usb_start_flag = 1;  //mark time
		notify_rsp_buf_init();
		wd_stop();
		ota_index=0;
		
		flash_write_addr=0;
		start_index=0;
		fw_size=0;
		//fw_check_value=0;
		//fw_cal_crc=0xffffffff;
		ota_error_flag=OTA_SUCCESS;
		app_enter_ota_mode();
		flash_erase_sector(ota_program_offset);
		//app_enter_ota_mode();
		
		my_printf_aaa("usb_ota_start\r\n");
		return ota_error_flag;
	}
	else if(ota_error_flag==OTA_SUCCESS)
	{
		 if((pd->cmd == CMD_OTA_END)&&(p_usb->length==6))		
		{
			 my_printf_aaa("usb_ota_end\r\n");
			 u32 *telink_mark=(u32*)&first_data_buf[8];
			 if(telink_mark[0]!=0x544c4e4b)
			 {
			 	ota_error_flag=OTA_FIRMWARE_MARK_ERR;
				return ota_error_flag;
			 }
			 u32 real_bin_size=0;
			 real_bin_size=fw_size-4;
			 if(real_bin_size!=(start_index*16))
			 {
			 	ota_error_flag=OTA_FW_SIZE_ERR;
				return ota_error_flag;
			 }
			
			flash_write_page(ota_program_offset,16,first_data_buf);

			 u8 read_flash_buf[16];  
			 flash_read_page(ota_program_offset,16, read_flash_buf);

			 if(memcmp(read_flash_buf, first_data_buf, 16))
			 {  //do not equal
			 	flash_erase_sector(ota_program_offset);
			 	ota_error_flag=OTA_WRITE_FLASH_ERR;
				return ota_error_flag;
			}
			 
			u32 temp_ota_program_offset;
			temp_ota_program_offset=ota_program_offset;	

			if(!ota_program_offset) ////zero, firmware is stored at flash 0x20000.
			{ 
				ota_program_offset = ota_program_bootAddr; ///NOTE: this flash offset need to set according to OTA offset
			}
			else ////note zero, firmware is stored at flash 0x00000.
			{                   
				ota_program_offset = 0x00000;
			}
			u8 ret=flash_fw_check(0xffffffff);
			ota_program_offset=temp_ota_program_offset;
			
			if(ret==0)
			{
				extern u32 fw_crc_init;
				my_printf_aaa("fw_crc_init=%x\r\n",fw_crc_init);
				//usb_dp_pullup_en (0);
				//sleep_ms(200);
				my_printf_aaa("usb_ota_success\r\n");
				u8 flag = 0;
				
				flash_write_page((ota_program_offset ? 0 : ota_program_bootAddr) + 0x08, 1, (u8 *)&flag);	//Invalid flag
				 
				usb_ota_resut(OTA_SUCCESS);
			}
			else
			{
				flash_erase_sector(ota_program_offset);
				ota_error_flag=OTA_FW_CHECK_ERR;
				return ota_error_flag;
				
			}
		}
		else 
		{
			if((p_usb->length%20)!=0)
			{
				ota_error_flag=OTA_PDU_LEN_ERR;
				return ota_error_flag;
			}
			u8 cnt=p_usb->length/20;
			
			for(u8 i=0;i<cnt;i++)
			{
				pd=(ota_data_st*)&p_usb->dat[20*i];
				if(crc16((u8*)&pd->cmd,18)==pd->crc)
				{
					
					if(pd->cmd==0x0000)//first_data
		 			{
						memcpy(first_data_buf,pd->buf,16);
						start_index=0;
		 			}
					else
					{
						if((start_index+1)!=pd->cmd)
						{
							ota_error_flag=OTA_DATA_PACKET_SEQ_ERR;
							return ota_error_flag;
						}
						start_index=pd->cmd;
						if(pd->cmd==0x0001)//second 
						{
							fw_size=pd->buf[8] | (pd->buf[9] <<8) |(pd->buf[10]<<16) | (pd->buf[11]<<24);
							if((fw_size)>ota_program_bootAddr)//128k
							{
								ota_error_flag=OTA_FW_SIZE_ERR;
								return ota_error_flag;
							}
						}
						
						if((fw_size)<flash_write_addr)
						{
							ota_error_flag=OTA_FW_SIZE_ERR;
							return ota_error_flag;
						}
						
						if(ota_error_flag==OTA_SUCCESS)
						{
							if((flash_write_addr%4096)==0)
					   		{
								flash_erase_sector(flash_write_addr+ota_program_offset);
					   		}
					   		flash_write_page(flash_write_addr+ota_program_offset,16,pd->buf);
						
						}
						
					}
					
					flash_write_addr+=16;
				}
				else
				{
					ota_error_flag=OTA_DATA_CRC_ERR;
					return ota_error_flag;
				}
			}
 			
			
			
       }
	}
	return ota_error_flag;
}



/**
 * @brief       This function handle usb data
 * @param[in]   length	- data len
 * @param[in]   p	- ota data
 * @return      
 * @note        
 */
void usb_data_handle(usb_data_t *p,u16 length)
{
	if((p->report_id==0x06)&&(length==USB_OTA_LENGTH))//ota 
	{
		if((p->opcode==0x01)&&(p->length==0))//get firmversion
		{
			p->length=8;
			memcpy(&p->dat[0], (u8 *)&output_dev_info.fw_version, 4);//fw ver
			memcpy(&p->dat[4], (u8 *)&output_dev_info.bin_crc, 4);//bin crc
		}
		else if(p->opcode==0x02)//0x02
		{
			u8 ota_error_flag=usb_ota_write(p);//ota write
			if(ota_error_flag)//err
			{
				usb_ota_resut(ota_error_flag);//show err
			}
		}
		if(notify_rsp_buf2hci(&p->report_id, length)==0)//
		{
			notify_rsp_tick = clock_time()|1;//update rsp tick
			my_fifo_push(&usb_to_pc_data, &p->report_id, length);//push to usb to pc data
		}
		
		
	}
	
}


/**
 * @brief       This function handle set report
 * @param[in]   data_request	- data request
 * @param[in]   length	- len
 * @param[in]   report_id	- report id 
 * @return      
 * @note        
 */
void app_hid_set_report_handle(u8 data_request,u8 report_id,u16 length)
{
	static 	u16 ep0_out_data_len=0;
	static 	u8 ep0_out_data_buf[USB_OTA_LENGTH]={0};
	static 	u16 ep0_out_index=0;
	if (data_request)//has req
	{
		if (ep0_out_data_len>8) //>8
		{
			ep0_out_data_len-=8;//-8
			
			for(u8 i=0;i<8;i++)
			{
					ep0_out_data_buf[ep0_out_index+i]=usbhw_read_ctrl_ep_data();//read data
			}
			ep0_out_index+=8;//index ++
		}
		else
		{
			for(u8 i=0;i<ep0_out_data_len;i++)//last data
			{
				ep0_out_data_buf[ep0_out_index+i]=usbhw_read_ctrl_ep_data();//read data
			}

		/*#if UART_PRINT_DEBUG_ENABLE
			my_printf_aaa("host_to_device=");
			for(u8 i=0;i<length;i++)
			{
				my_printf_aaa("%x ",ep0_out_data_buf[i]);
			}
			my_printf_aaa("\r\n");
		#endif*/
		
			usb_data_t *p=(usb_data_t *)&ep0_out_data_buf[0];//usb data
			usb_data_handle(p,length);//handle usb data
			
		}
		
		
		
	}
	else
	{
		ep0_out_data_len=length; //datalen length
		ep0_out_index=0;//index 0
		//my_printf_aaa("set_reprot_cmd=%x,%x\r\n",report_id,length);
	}
}


/**
 * @brief       This function update rsp
 * @return      
 * @note        
 */
void notify_rsp_update ()
{
    if(notify_rsp_tick&&clock_time_exceed(notify_rsp_tick, 600))//600us
	{
			u8*p=my_fifo_get(&usb_to_pc_data);//get usb to pc data
			if(p)//has data
			{
				int length=p[2]+(p[3]<<8);//len
				if(notify_rsp_buf2hci(&p[0],length))//send to pc
				{
					my_fifo_pop(&usb_to_pc_data);//rptr ++
					notify_rsp_tick = 0;//notify rsp tick 0
				}
			}
			else
			{
				notify_rsp_tick = 0;//notify rsp tick 0
			}
			
	}
}






/**
 * @brief       This function usb ui loop
 * @return      
 * @note        
 */
_attribute_ram_code_ void ui_usb_mode_1()
{

	static u32 tick_loop_24g;//tick 24g
	 
	 ms_data.wheel=0;//wheel 0
	 if(clock_time_exceed(tick_loop_24g,7000))//7ms
	 {

		 tick_loop_24g=clock_time();//update 
		 //get_24g_data_report_aaa();
#if WHEEL_FUN_ENABLE_AAA //wheel en
		 u32 wheel_prepare_tick;//pre tick
	 
		 wheel_prepare_tick = mouse_wheel_prepare_tick();//prepare wheel tick
	 
#endif
#if BUTTON_FUN_ENABLE_AAA //btn en 
	 has_new_key_event |= btn_get_value();//get btn action
#endif

		
#if WHEEL_FUN_ENABLE_AAA //wheel en
	 has_new_key_event |= wheel_get_value(wheel_prepare_tick);//wheel action
#endif

	 }
	   
	mouse_task_when_rf();//sensor action
	if(has_new_key_event)//has new evnt
	 {
	 	has_new_key_event=0;//0
	 	push_data_fifo_aaa(&ms_data.btn, HAS_MOUSE_REPORT, sizeof(mouse_data_t));//push to fifo
	 }

	 
}

u8 allow_suspend = 0; //0
u8 need_wake_up_host_flag;//0
u32 tick_resume_host;//0
u32 tick_host_start_power_off=0;//0


/**
 * @brief       This function proc usb suspend
 * @return      
 * @note        
 */
void usb_proc_suspend (void)
{
	u8 power_off_flag=0; //0
	
	if((reg_irq_src & FLD_IRQ_USB_PWDN_EN) && usb_host_conn == true)//pwn en and host conn
	{
		power_off_flag=1;//power off 1
	}
	
	if(power_off_flag)//if power off
	{
		if(tick_host_start_power_off)//tick power start
		{
			if(clock_time_exceed(tick_host_start_power_off, 1000000))//over 1s
			{
				tick_host_start_power_off=0;//reset pw off tick 0
				tick_resume_host=clock_time()+1000000*16;//resume tick update
				allow_suspend=1;//allow supend 1
			}
			else
			{
				need_wake_up_host_flag=0;//need wake up host flag 0
			}
		}
		else if(need_wake_up_host_flag)//if en
		{
			if(tick_resume_host==0)//if resum tick 0
			{
				tick_resume_host=clock_time()|1;//update resume tick
			}
			if(clock_time_exceed(tick_resume_host, 200000))//over 200ms
			{
				tick_resume_host=0;//update resume tick 0
				usb_resume_host();//resume host
				allow_suspend = 0;//no allow suspend
			}	
		}
		else if(allow_suspend)//allow
		{
			#if WHEEL_FUN_ENABLE_AAA//wheel en
	            		wheel_set_wakeup_level_suspend(1);//wheel wk up en
			#endif
			
			#if BUTTON_FUN_ENABLE_AAA//btn 
	            		btn_set_wakeup_level_suspend(1);//btn gpio wk up en
			#endif
			
			#if SENSOR_FUN_ENABLE_AAA//sensor en
	            		sensor_set_wakeup_level_suspend(1);//sensor gpio wkup en
			#endif
			printf("enter usb suspend\n");
			
			cpu_sleep_wakeup(SUSPEND_MODE, PM_WAKEUP_CORE | PM_WAKEUP_PAD, 0);//gpio wakeup

			usb_resume_host();//resume host
			need_wake_up_host_flag =1;//need wakeup_host flag en
		}
	}
	else
	{
		tick_host_start_power_off=clock_time()|1;//tick power off update
		allow_suspend=0;//no allow suspend
		
		need_wake_up_host_flag=0;//need wakeup host flag 0
		tick_resume_host=0;//tick resume 0
	}
}


/**
 * @brief       This function init usb user
 * @return      
 * @note        
 */
void usb_user_init()
{
	usb_dp_pullup_en (0);//dp 0
#if MODULE_WATCHDOG_ENABLE //wd en
    wd_stop();//clear wd
#endif
    sleep_ms(400);//sleep 400ms

	//enable USB manual interrupt(in auto interrupt mode,USB device would be USB printer device)
	usb_init_interrupt();//interrupt init usb
	usb_register_set_report(app_hid_set_report_handle);//regist report handle
#if USB_DESCRIPTOR_MY_SELF //desmyself
		write_reg8(0x10e, (1 << USB_EDP_MOUSE) |(1 << USB_EDP_SPP_IN));		//0x25
		
		//REG_ADDR8(0x74) = 0x62;
		//REG_ADDR16(0x7e) = 0x08d0;
		//REG_ADDR8(0x74) = 0x00;
		
		notify_rsp_buf_init();//init rsp buff

		bls_ota_clearNewFwDataArea();//clear new fw data area
		bls_ota_registerStartCmdCb(app_enter_ota_mode);//app enter ota mode
		bls_ota_registerResultIndicateCb(app_debug_ota_result);//app debug ota result
#if MODULE_WATCHDOG_ENABLE //wd en
			wd_start();//wd start
#endif

#endif
#if USB_UPGRATE_OTA
	//bls_ota_clearNewFwDataArea();//clear new fw data area
	blc_ota_initOtaServer_module();
	blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);//app enter ota mode
	blc_ota_registerOtaResultIndicationCb(app_debug_ota_result);//app debug ota result
#endif
	deepsleep_dp_dm_gpio_low_wake_enable();
	usb_custom_init();
	printf("usb init\n");
	//enable USB DP pull up 1.5k
	//usb_set_pin_en();//en dp
	clear_fifo();//clear fifo
	usb_pair_success_tick = clock_time();
	//connect_ok =1;
}



/**
 * @brief       This function update loop
 * @return      
 * @note        
 */
void usb_update_loop()
{
	//static u32 dbg_m_loop;
	//dbg_m_loop ++;
	if((usb_start_flag == 1) && clock_time_exceed(blt_ota_start_tick , 120000000))//ota and 120s
	{
		usb_ota_resut(OTA_TIMEOUT);//show ota time out
	}

   

	//proc_host();


	 notify_rsp_update();  //每5ms检测并把数据回传给上位机。
	
}


/**
 * @brief       This function is usb mode loop
 * @return      
 * @note        
 */
void usb_mode_loop()
{
	static u32 tick_loop;
	//u32 temp = report_rate;

	usb_handle_irq();
	proc_audio_usb();
		  

	
	#if (APP_24G_AUDIO_EN)
		if(ui_mic_enable)
			{
			  report_rate= 4;
			}
		else
			{
			  report_rate= 1;
			}
		if(clock_time_exceed(usb_pair_success_tick, 10*1000*1000)&& !ui_mic_enable)
		{
			usb_pair_success_tick = clock_time();
			#if (BUTTON_FUN_ENABLE_AAA==0)
			ui_enable_mic(1);
			printf("start usb audio\n");
			#endif
			//printf("start usb audio\n");
			//auto_draw_flag =1;
		}
	#endif
	if(clock_time_exceed(tick_loop,report_rate*1000))//report times
	{		
		tick_loop += report_rate*SYSTEM_TIMER_TICK_1MS;//loop update
		//tick_loop = clock_time();
		
		ui_usb_mode_1();//usb ui
		
	}
    if(check_fifo_has_data())//has data
	{
		//if(usb_mouse_hid_report_aaa(1, &ms_data.btn, sizeof(mouse_data_t)))
		if(usb_mouse_hid_report_aaa(1, &tx_fifo_aaa.fifo[tx_fifo_aaa.rptr][2], sizeof(mouse_data_t)))//send data to usb ok
		{
            tx_fifo_aaa.rptr = (tx_fifo_aaa.rptr + 1) % TX_FIFO_NUM_AAA;//rptr ++
            tx_fifo_aaa.count--;//count --
            //printf("update mouse data %x\n ",tx_fifo_aaa.fifo[tx_fifo_aaa.rptr][2]);
		}
	} 
	
	//device_led_process();

	usb_proc_suspend();//proc suspend

	
}

