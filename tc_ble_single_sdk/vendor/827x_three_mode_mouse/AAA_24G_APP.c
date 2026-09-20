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

#define A_SOLUTION   0
//#define B_SOLUTION   1
#define C_SOLUTION   2

#define SOLUTION_METHOD   C_SOLUTION

extern void mouse_task_when_rf();

#define DEVICE_TYPE_INDEX          1// 1 mouse  ,2 keyboard

typedef enum
{
    STATE_POWERON = 0,
#if (FREQUENY_HOPPING_1K==0)
    STATE_SYNCING,
#endif
    STATE_PAIRING,
    STATE_NORMAL,
	STATE_OTA,
} MOUSE_MODE;

 u8 device_status = 0; //device status
 u32 dongle_id; //dongle id
 u8 mouse_send_need_f = 0; //need send data flag
 volatile u32 no_ack = 0; //no ack
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
pair_data_t *p_pair_dat = (pair_data_t*)&rf_pair_buf.dat[0];

rf_packet_t rf_km_buf =
{
#if DATA_3_CHOOSE_1_ENABLE
	55,	// dma_len
    54,	// rf_len
#else
    sizeof(km_3_c_1_data_t)+1,//14,	// dma_len
    sizeof(km_3_c_1_data_t),	// rf_len
#endif
};
km_3_c_1_data_t *p_km_data=(km_3_c_1_data_t*)&rf_km_buf.dat[0];//point to km data packet



#if (AES_METHOD == 1)
rf_packet_t rf_km_buf_enc =
{
    19, // dma_len
    18, // rf_len
};
km_3_c_1_data_t *p_km_data_enc = (km_3_c_1_data_t*)&rf_km_buf_enc.dat[0];//point to km enc packet
#endif

#if D24G_OTA_ENABLE_AAA //24g ota en
u8 d24g_ota_status = 0; //ota status
u8 d24g_ota_start_flag = 0; //ota start flag
u8 d24g_ota_success_flag = 0; //ota success flag

rf_packet_t rf_ota_buf =
{
    sizeof(ota_data_t)+1,	// dma_len
    sizeof(ota_data_t),	// rf_len
};
ota_data_t *p_ota_data = (ota_data_t*)&rf_ota_buf.dat[0]; //ota data pointer
ota_ack_data_t p_ota_ack_data ; //ota ack data

#define D24G_OTA_LENGTH  24 //ota len 24
typedef struct{
	u8	report_id;
	u8 	opcode;
	u16	length;					
	u8	dat[20];
}ota_buff_t; //ota buff
ota_buff_t ota_buff;
u8 ota_buff_valid_flag;
#endif

u32 wakeup_next_tick = 0;// wake up tick next time
u8 pair_success_flag = 0;//use to assure pair success
u32 pair_success_tick = 0;
u8 has_mouse_data_flag = 0;


#if D24G_OTA_ENABLE_AAA
MYFIFO_INIT (fifo_km, 40, 16);//The size must be a multiple of 4 bytes
#else
MYFIFO_INIT (fifo_km, 12, 16);//The size must be a multiple of 4 bytes
#endif
_attribute_data_retention_user u8 private_key[16] =
{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

_attribute_data_retention_user u32 dongle_id;
_attribute_data_retention_user u32 golden_dongle_test = 0;

unsigned short 	crc16_poly1[2] = {0, 0xa001};
/**
 * @brief       This function crs 16
 * @param[in]   len	- data len
 * @param[in]   pD	- data
 * @return     crc 
 * @note        
 */
_attribute_ram_code_ unsigned short crc16_user (unsigned char *pD, int len)
{
	unsigned short crc = 0xffff;//crc16 init ffff
	int i,j;

	for(j=len; j>0; j--)//for every data
	{
		unsigned char ds = *pD++;//pointer pd
		for(i=0; i<8; i++)
		{
			crc = (crc >> 1) ^ crc16_poly1[(crc ^ ds ) & 1];//deal every bit
			ds = ds >> 1;//right move 1 bit
		}
	}

	return crc;//return crc
}

#if D24G_OTA_ENABLE_AAA

/**
 * @brief       This function show ota result
 * @param[in]   result	- 
 * @return      
 * @note        
 */
void d24g_ota_resut(u8 result)
{
	my_printf_aaa("---d24g_ota_result=0x%x\r\n",result);// show d24 ota rasult

	u8 led_blink_times; //led blink times
	
	if(result == OTA_SUCCESS)
	{
		led_blink_times = 3; //success 3
	}
	else
	{
		led_blink_times = 5; //fail 5
	}
#if BLT_APP_LED_ENABLE
	for(u8 i=0;i<led_blink_times;i++)//loop for blink
	{
		gpio_write(PIN_24G_LED, LED_OFF_AAA);//led off
		sleep_ms(200);//200ms 
	
		gpio_write(PIN_24G_LED, LED_ON_AAA);// led on
		sleep_ms(200);
	}
#endif	
	user_reboot(DEEP_SLEEP_ANA_AAA);//deep sleep
}


/**
 * @brief       This function for ota write
 * @param[in]   p_usb	- ota data
 * @return      
 * @note        
 */
u8 d24g_ota_write(ota_buff_t *p_usb)
{
	static	u16 ota_index=0;
	static u16 start_index=0;
	static u32 flash_write_addr=0;
	static u8 first_data_buf[16];
	static u32 fw_size=0;
	static u8 ota_error_flag=0;
	ota_data_st *pd=(ota_data_st*)&p_usb->dat[0];//ota data

	if((pd->cmd== CMD_OTA_START)&&(p_usb->length==2))	//ota start	
	{
		blt_ota_start_tick = clock_time();  //mark time
		d24g_ota_start_flag = 1;  //mark time
		//notify_rsp_buf_init();
		wd_stop();//stop wd
		ota_index=0;//ota index 0
		
		flash_write_addr=0;//write flash addr 0
		start_index=0;//start index 0
		fw_size=0;//fw size 0
		//fw_check_value=0;
		//fw_cal_crc=0xffffffff;
		ota_error_flag=OTA_SUCCESS;//ota success
		flash_erase_sector(ota_program_offset);//erase flash sector
		//app_enter_ota_mode();
	#if (BLT_APP_LED_ENABLE && BLE_OTA_LED_DEBUG)//ota led en
		gpio_set_output_en(PIN_24G_LED, 1);//led output en
		gpio_write(PIN_24G_LED, LED_ON_AAA);//led on
	#endif
		blt_ota_start_tick = clock_time();  //mark time
		my_printf_aaa("usb_ota_start\r\n");//debug ota start
		return ota_error_flag;
	}
	else if((ota_error_flag==OTA_SUCCESS)&&(d24g_ota_start_flag))//ota next data deal
	{
		if((pd->cmd == CMD_OTA_END)&&(p_usb->length==6))//ota end		
		{
			 my_printf_aaa("usb_ota_end\r\n");//debug ota end
			 u32 *telink_mark=(u32*)&first_data_buf[8];//telink mark
			 if(telink_mark[0]!=0x544c4e4b)//telink mark wrong
			 {
			 	ota_error_flag=OTA_FIRMWARE_MARK_ERR;//mark err
				return ota_error_flag;
			 }
			 u32 real_bin_size=0;//real bin size 0
			 real_bin_size=fw_size-4;//fw_size-4
			 if(real_bin_size!=(start_index*16))//not equal start_index*16
			 {
			 	ota_error_flag=OTA_FW_SIZE_ERR;//size err
				return ota_error_flag;
			 }
			
			flash_write_page(ota_program_offset,16,first_data_buf);//write first data buff to ota flash addr

			 u8 read_flash_buf[16];  //read flash buff
			 flash_read_page(ota_program_offset,16, read_flash_buf);//read first data 

			 if(memcmp(read_flash_buf, first_data_buf, 16))//not equal
			 {  //do not equal
			 	flash_erase_sector(ota_program_offset);//erase ota data 
			 	ota_error_flag=OTA_WRITE_FLASH_ERR;//flash ota write err
				return ota_error_flag;
			}
			 
			u32 temp_ota_program_offset;//ota offset
			temp_ota_program_offset=ota_program_offset;	//ota program offset

			if(!ota_program_offset) ////zero, firmware is stored at flash 0x20000.
			{ 
				ota_program_offset = ota_program_bootAddr; ///NOTE: this flash offset need to set according to OTA offset
			}
			else ////note zero, firmware is stored at flash 0x00000.
			{                   
				ota_program_offset = 0x00000;
			}
			u8 ret=flash_fw_check(0xffffffff);//check fw
			ota_program_offset=temp_ota_program_offset;
			
			if(ret==0)//check ok
			{
				extern u32 fw_crc_init;
				my_printf_aaa("fw_crc_init=%x\r\n",fw_crc_init);//show fw crc init
				//usb_dp_pullup_en (0);
				//sleep_ms(200);
				my_printf_aaa("usb_ota_success\r\n");//debug ota success
				u8 flag = 0;//flag 0
				
				flash_write_page((ota_program_offset ? 0 : ota_program_bootAddr) + 0x08, 1, (u8 *)&flag);	//Invalid flag
				 
				//d24g_ota_resut(OTA_SUCCESS);
				d24g_ota_success_flag = 1;//ota success flag set 1
			}
			else
			{
				flash_erase_sector(ota_program_offset);//erase ota program offset
				ota_error_flag=OTA_FW_CHECK_ERR;//fw_check err
				return ota_error_flag;
				
			}
		}
		else 
		{
			//my_printf_aaa("length=%d\r\n",p_usb->length);
			
			if((p_usb->length%20)!=0)//ota pdu len wrong
			{
				ota_error_flag=OTA_PDU_LEN_ERR;//ota pdu len err
				return ota_error_flag;
			}
			u8 cnt=p_usb->length/20;
			
			for(u8 i=0;i<cnt;i++)
			{
				pd=(ota_data_st*)&p_usb->dat[20*i];
				//my_printf_aaa("cmd=%d,crc=0x%04x_0x%04x.\r\n",pd->cmd,crc16((u8*)&pd->cmd,18),pd->crc);
				if(crc16((u8*)&pd->cmd,18)==pd->crc) //crc16_user
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
 * @brief       This function deal ota loop
 * @return      
 * @note        
 */
void d24g_ota_loop()
{
	if(d24g_ota_start_flag) //ota start flag 1
	{
		if(clock_time_exceed(blt_ota_start_tick , 120 *1000 *1000))//over 120 s
		{
			d24g_ota_resut(OTA_TIMEOUT); //ota time out
		}
		else if((d24g_ota_success_flag)&&(p_ota_ack_data.pno_no==0))//ota success flag and pno_no 0
		{
			d24g_ota_success_flag = 0; //reset ota success flag 0

			d24g_ota_resut(OTA_SUCCESS);//ota success show
		}
	}
}
#endif

#if(0)

/**
 * @brief       This function deal rx packet
 * @param[in]   p_rf_data	- rx packet
 * @return      
 * @note        
 */
_attribute_ram_code_ u8  rf_rx_process(rf_packet_t *p_rf_data)
{
	if(device_status == STATE_PAIRING) //pair status
	{
	    pair_ack_data_t *pair_ack_dat_ptr=(pair_ack_data_t*)&p_rf_data->dat[0]; //pair ack dat ptr
		{
#if (AES_METHOD == 0) //no aes
        if ((pair_ack_dat_ptr->cmd == PAIR_ACK_CMD) && (pair_ack_dat_ptr->did == p_pair_dat->did))//if cmd ==pair_ack_cmd, and ack data did == pair data did
#elif (AES_METHOD == 1) //aes 1
        if ((pair_ack_dat_ptr->cmd == PAIR_ACK_CMD) && (memcmp((u8 *)&pair_ack_dat_ptr->did, (u8 *)&p_pair_dat->did, 16)==0))//if cmd ==pair_ack_cmd, and ack data did == pair data did
#endif
			{
            	dongle_id = pair_ack_dat_ptr->gid;//dongle id update
            	pair_success_flag = 1;//pair success flag set 1
            	pair_success_tick = clock_time();
				printf("pair successful now\n");
            	return 1;
        	}
		}
	}
	else if(device_status == STATE_NORMAL)//normal status
	{
		if((mouse_send_need_f == 2)||(mouse_send_need_f == 4)||(mouse_send_need_f == 5))//mouse ack
		{
			km_ack_data_t *km_ack_dat_ptr = (km_ack_data_t *)&p_rf_data->dat[0]; //km ack data ptr
/*#if 0
			rf_packet_ack_mouse_t *tmp=(rf_packet_ack_mouse_t *)p_pkt;

			u16 crc_c=0;
			for(u8 i=0;i<3;i++)
		    {
				crc_c= crc16_user(&tmp->dat[i].proto, sizeof(ack_mouse_rf_user_data_t)-2);
				if((crc_c==tmp->dat[0].crc16)||(crc_c==tmp->dat[1].crc16)||(crc_c==tmp->dat[2].crc16))
				{
					if(tmp->dat[i].type == FRAME_TYPE_ACK_MOUSE)
					{
						 return 1;
					}
				}
		    }
#endif*/
			printf("km ack cmd is %d\n",km_ack_dat_ptr->cmd);
			if(km_ack_dat_ptr->pno_no >= 2)
			{
				 memcpy (&D24G_APP_cmd[1], (u8*)&km_ack_dat_ptr->pno_no,4);
				 if(memcmp(&last_D24G_APP_cmd[1],&D24G_APP_cmd[1],4))
				 {
				 	 memcpy(&last_D24G_APP_cmd[1],&D24G_APP_cmd[1],4);
					  has_d24g_app_cmd = 1;
					  printf("the cmd is %x, %x,%x,%x,%x\n",D24G_APP_cmd[0],D24G_APP_cmd[1],D24G_APP_cmd[2],D24G_APP_cmd[3],D24G_APP_cmd[4]);
				 }
				 //printf("the cmd is %x, %x,%x,%x,%x\n",D24G_APP_cmd[0],D24G_APP_cmd[1],D24G_APP_cmd[2],D24G_APP_cmd[3],D24G_APP_cmd[4]);
				// has_d24g_app_cmd = 1;
			 
			}
#if (DEVICE_TYPE_INDEX==1)//mouse 
            //my_printf_aaa("cmd %d\n", km_ack_dat_ptr->cmd);
            if(mouse_send_need_f == 2)
			{
					if(km_ack_dat_ptr->cmd == MOUSE_ACK_CMD)//ack data cmd == mouse ack cmd
					{
						printf("mouse ack now\n");
						return 1;
					}
					if(send_kb_flag)
					{

						if (km_ack_dat_ptr->cmd == KB_ACK_CMD)
						{
							//send_kb_flag = 0;
							keyboard_led_status = km_ack_dat_ptr->host_led_status;
							if(keyboard_led_status&0x02)
							{
								kb_caps_on = 1;
								printf("kb caps on\n");
							}
							else
							{
								kb_caps_on = 0;
								printf("kb caps off\n");
							}
							return 1;
						}
					}
            }
			else if(mouse_send_need_f ==5)
			{	
					if(km_ack_dat_ptr->cmd == APP_DATA_CMD_ACK)//ack data cmd == mouse ack cmd
					{
						return 1;
					}
				
			}
			else if(mouse_send_need_f ==4 )
			{
					if(km_ack_dat_ptr->cmd == MIC_DATA_CMD_ACK)//ack data cmd == mouse ack cmd
					{
						return 1;
					}
			}
				
#endif
		}
	}
#if D24G_OTA_ENABLE_AAA //ota en
	else if(device_status == STATE_OTA) //ota status
	{
		if(mouse_send_need_f == 3)//ota ack
		{
			ota_ack_data_t *ota_ack_dat_ptr = (ota_ack_data_t *)&p_rf_data->dat[0];//ota ack dat ptr

			if(ota_ack_dat_ptr->cmd == D24G_OTA_ACK_CMD)//ota ack cmd
			{
				if(ota_ack_dat_ptr->pno_no) //pno_no no 0
				{				
					static u16 packet_cnt=0;//cnt set 0
					ota_data_st *pd=(ota_data_st *)&ota_ack_dat_ptr->dat[0]; //ota data ptr
					ota_buff_t *p=(ota_buff_t *)&p_rf_data->dat[7];//ota buf ptr
					
					switch(ota_ack_dat_ptr->length)//determine ack len
					{
						case 2: //ota - start
						case 6: //ota - end
							if((pd->cmd==0xff01)||(pd->cmd==0xff02))//cmd
							{
								ota_buff_valid_flag = 1;//valid flag

								memcpy(&ota_buff.report_id,&p->report_id,D24G_OTA_LENGTH);//cpy ota received data to ota buff
								
								packet_cnt=0;//packet cnt set 0
							}
							break;
						case 20: //ota data receive
							if((crc16((u8*)&ota_ack_dat_ptr->dat[0],18) == pd->crc)&&\
								(packet_cnt==pd->cmd))
							{
								ota_buff_valid_flag = 1;//valid falg

								memcpy(&ota_buff.report_id,&p->report_id,D24G_OTA_LENGTH);//cpy ota received data to ota buff
								
								packet_cnt++;//pkt ++
							}
							break;
						default:
							printf("---err data\n");//debug err data
							break;
					};

					memcpy(&p_ota_ack_data.pno_no,&ota_ack_dat_ptr->pno_no,29);//cpy ack data to p_ota_ack_data

					my_fifo_push(&fifo_km, (u8 *)&p_ota_ack_data.pno_no,29);//push ota_ack data to fifo
				}
				
				return 1;
			}
		}
	}
#endif
	
    return 0;
}
#else
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


#endif


/**
 * @brief       This function start pair
 * @return      
 * @note        
 */
void d24_start_pair()
{
    set_pair_flag();//set pair flag
    user_reboot(CLEAR_FLAG_ANA_AAA); //reboot
	adv_count = 0; //adv count 0 
	adv_begin_tick = clock_time()|1; //adv begin tick
}

/*#if 0
void aes_user_encryption(u8 *key, u8 *plaintext, u8 *encrypted_data)
{
    u8 rplaintext[16], rkey[16];
    swapX(plaintext, rplaintext, 16);
    swapX(key, rkey, 16);
    aes_ll_encryption(rkey, rplaintext, encrypted_data);
}

void aes_user_decryption(u8 *key, u8 *encrypted_data, u8 *decrypted_data)
{
    u8 rencrypted_data[16], rkey[16];
    swapX(key, rkey, 16);
    swapX(encrypted_data, rencrypted_data, 16);
    aes_ll_decryption(rkey, rencrypted_data, decrypted_data);
}
#endif*/


/**
 * @brief       This function encrypt data
 * @param[in]   encrypted_data	- 
 * @param[in]   key	- 
 * @param[in]   plaintext	- 
 * @return      
 * @note        
 */
void aes_user_encryption(u8 *key, u8 *plaintext, u8 *encrypted_data)
{
	aes_encrypt(key, plaintext, encrypted_data); //aes encrypt
}
 

/**
 * @brief       This function decrypt data
 * @param[in]   decrypted_data	- 
 * @param[in]   encrypted_data	- 
 * @param[in]   key	- 
 * @return      
 * @note        
 */
void aes_user_decryption(u8 *key, u8 *encrypted_data, u8 *decrypted_data)
{
	aes_decrypt(key, encrypted_data, decrypted_data); //aes discrypt
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

#if (AES_METHOD == 1) //aes 1
    p_km_data->cmd = MOUSE_CMD | (1 << 7); //mouse aes cmd
    memcpy((u8*)&private_key[0], (u8*)&device_id ,4); //cpy did to first 4 bytes pri key  
#else
    p_km_data->cmd = MOUSE_CMD; //mouse cmd
#endif

#if D24G_OTA_ENABLE_AAA // ota en
	p_ota_data->cmd = D24G_OTA_CMD; // ota cmd
	p_ota_data->did = device_id; //did
	p_ota_ack_data.did = device_id; //ack data did
#endif
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
	#if (AES_METHOD == 1) //aes 1
		generateRandomNum(12, &private_key[4]); // random num init
		aes_user_encryption(pub_key, private_key, (u8*)&p_pair_dat->did); //encry pair data
	#endif
    } 
#if D24G_OTA_ENABLE_AAA //ota en
	else if(d24g_ota_status) //ota status
	{
		device_status = STATE_OTA; //state ota
		rf_rx_timeout_us = D24G_OTA_TIMER_OUT; //ota time out
	}
#endif
	else 
	{
	#if(AES_METHOD == 1) //aes 1
		memcpy((u8*)&private_key[4], (u8*)&flash_dev_info.key[0], 12); //pri key
	#endif
        device_status = STATE_NORMAL; //device normal
		rf_rx_timeout_us = D24G_COMMUNICATION_TIMER_OUT; // communication time out
    }

    wakeup_next_tick = clock_time(); //next wake up tick update
    reset_idle_status(); //reset idle paras
}


u8 need_suspend_flag = 0; // need suspend flag init 0
u8 suspend_wake_up_enable; // suspend wake up en 
u8 rptr = 0; //rptr
u8 rx_fifo_count = 0; //rx fifo 


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
				#if (AES_METHOD == 1)//aes method 1
                    memcpy((u8*)&flash_dev_info.key[0], (u8*)&private_key[4], 12);//update pri key
				#endif
					if (golden_dongle_test == 0x00)//when golden_dongle_test == 0xff, is golden dongle paired ,not save info to flash
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

		mouse_send_need_f = 0;//reset mouse need send flag
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



/**
 * @brief       This function deal rf machine loop
 * @return      
 * @note        
 */
void d24g_rf_loop()//rf state machine loop
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
            PIN_DEBUG_UI_TIME_LEVEL(1);
            rf_txBuf.dma_len=rf_txBuf.rf_len+1;
		    rf_stx_to_rx((u8*)&rf_txBuf.dma_len, rf_rx_timeout_us);//rf tx send data then enter rx mode
			reg_rf_irq_status = 0xffff;//irq status reset to ffff
		}

	}
	else if (rf_state==RF_RX_END_STATUS)//rf status is rf_rx end
	{
        irq_device_rx();//deal rx receive packet
		check_rf_complet_status();//check rf complet 
	}
	else if (rf_state==RF_RX_TIMEOUT_STATUS)//rf status is timeout status
	{
		check_rf_complet_status();//check rf complet
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

#if BUTTON_FUN_ENABLE_AAA //button fun en
		has_new_key_event |= btn_get_value();//btn scan
#endif
	idle_status_poll();//poll idle status parameters
	device_led_process();// led process

        if((connect_ok==0))//no connect
        {
#if BLT_APP_LED_ENABLE //led en
    		led_2p4_Adv_poll();//adv led
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
						gpio_write(PIN_VOICE,1);
					}
			}
			//has_mouse_data_flag =1;
			idle_loop_24g_tick = clock_time();
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
			#if D24G_OTA_ENABLE_AAA //ota en
				if(d24g_ota_status) //ota status
				{
				#if D24G_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA //enterdeep when ota timeout en
					if(idle_count>=D24G_CONNECT_TIME_OUT) {// idle count > ota time out
						enter_deep_aaa();//enter deep
					}
				#endif //
				}
				else
			#endif
				if((idle_count < 60)||(DEVICE_LED_BUSY)) //idle count <3 and no led event
				{
					if (report_rate==8)//if raport_rate>=4
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

/**
 * @brief       This function 24g mouse main loop
 * @return      
 * @note        
 */
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
	#if (SOLUTION_METHOD == A_SOLUTION)// solution
		if(report_rate == 1)//report rate 1
		{
			temp = 2000;//2ms
		}
	#endif
	#if APP_24G_AUDIO_EN
		if(ui_mic_enable)
			{
			 temp = 4000;
			 bls_pm_setSuspendMask (SUSPEND_DISABLE);
			}
		else
			{
				temp =1000;
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
#if ((ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG==0)&&(TEST_DRAW_A_SQUARE == 0))//auto draw adapt 
	if(now_vbus_exist_flag)//vbus exist flga en
	{
		need_suspend_flag = 0;//no need suspend
	}
	else
	{
	pm_poll();//pm deal
	}
#endif
	pm_poll();

}
