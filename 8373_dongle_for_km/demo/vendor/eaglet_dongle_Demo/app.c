/********************************************************************************************************
 * @file     AAA_app.c
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

#define  SIMU_DATA_DEBUG_ENABLE 	0

#define  HOPING_FREQ    			0

#define  RF_FAST_TX_SETTING_ENABLE  1 //0:disable fast TX, 1:enable fast TX(can save power and improve report rate)
#define  RF_FAST_RX_SETTING_ENABLE  1 //0:disable fast RX, 1:enable fast RX(can save power and improve report rate)

#if (RF_FAST_TX_SETTING_ENABLE || RF_FAST_RX_SETTING_ENABLE)
volatile u32 tick_rf_fast_setting = 0;
volatile int rf_fast_setting_flag = 0; //0 init, 1:time out, 2:enable fast set
#endif

//t_mouse_inf mouse_inf,last_mouse_inf; 

volatile u8 rf_pair_enable = 1; //0:enter normal mode when power on, 1:enter pair mode when power on !!!

u32 first_rx_normal_data_tick = 0;
u32 pair_tick = 0;
u8  usb_device_status;
u8  usb_device_status_last;
u8  need_enter_suspend_flag = 0;
u32 need_enter_suspend_tick = 0;

volatile int rx_fail = 0;
volatile u8	 chn_mask = 0x80;
volatile u8  last_chn_mask = 0;
u8 deep_flag = POWER_ON_ANA_AAA;
u8 app_len = 0;
//u32 rx_tick=0;
//u32 rx_mouse_cnt=0;
//u32 mouse_to_pc_cnt=0;
//u8  has_write_cfg=0;
//u8  debug_report_rate=0;
//u32 tick_pc_breath=0;

#if (AES_METHOD == 0)
	//#define ID_LEN 1 
#elif (AES_METHOD == 1)
	#define ID_LEN 16 
#endif
#define PC_APP_LENGTH  64//32
u8 dev_to_pc_data[PC_APP_LENGTH] = {0};
typedef void (*callback_rx_func)(u8 *);

u8 * raw_pkt = NULL;
callback_rx_func p_post = NULL;

u8 bin_crc[4];
u32 pair_accesscode = 0x39517695; //default access code
#if(0)
USB_FIFO_DATA_S  usb_fifo_aaa; //usb data fifo, storage mouse or keyboard data
#else
u8 device_fifo_buff[USB_FIFO_MAX_LEN*USB_FIFO_NUM1] = {0};
my_fifo_t usb_fifo_aaa=
{
    USB_FIFO_MAX_LEN,
    USB_FIFO_NUM1,
    0,
    0,
    device_fifo_buff,
};
#endif
u8  mouse_fifo_buff[8*8]={0};
my_fifo_t mouse_fifo_t=
{
    8,
    8,
    0,
    0,
    mouse_fifo_buff,
};

USB_RX_FIFO_DATA_S  usb_rx_fifo_aaa;

u8 mouse_not_release = 0;
volatile u32 dongle_id = 0;

volatile u32 kb_tick=0;
volatile u32 ms_tick=0;

s16 x_smoother;
s16 y_smoother;

// u8 allow_suspend = 0;
//u32 tick_allow_suspend = 0;
//u32 tick_host_start_power_on = 0;
//u32 tick_host_start_power_off = 0;
u32 tick_suspend_interval;

static mouse_data_t mouse_last_data = {0,0,0,0};
static u8 consume_last_data[2] = {0,0};
static u8 sys_last_data = 0;
static u8 normal_last_data[8] = {0,0,0,0,0,0,0,0};

callback_rx_func p_keyboard_data_handle_post = NULL; //keyboard data call back flag
volatile u8 to_usb_keyboard_data[8]; //keyboard data

callback_rx_func p_mouse_data_handle_post = NULL; //mouse data call back flag
volatile u8 to_usb_mouse_data[8]; //mouse data


//u8 need_wake_up_host_flag=0;
//u32 tick_resume_host;
u8  allow_send_to_usb_flag = 1;
int	custom_binding_idx = 0; //The offset of the latest pairing information

u8 encKey[16]={
	0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10};
u8 pub_key[16] =
{
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

rf_packet_t	ack_rf_dat ;

#if (HOPING_FREQ==1)
volatile u32 tick_fh = 0;
frqHopping_data_t  frq_hopping_data = 
{
	FRQ_HOPPING_MODE_NORMAL,
	8,
	2,
	0,
	120000,
	0
};
volatile u8		ll_chn_sel;
volatile u8		ll_chn_pkt[16] = {0};
volatile int		device_packet_received;
#endif	
volatile unsigned char  rf_rx_buff[PKT_BUFF_SIZE*2] __attribute__((aligned(4)));
int		rf_rx_wptr;
volatile u8		host_channel = 0;
int		device_packet_received;

AES_KEY key_enc[2]; //paired device information
static u8 app_out_data_buf[64] = {0};
u8 need_send_app_data =0;
u32 need_send_app_tick = 0;
u8 has_pc_data = 0;
u8 mic_msbc[63] = {1,57,0,0};
u8 last_mic_index = 0;

u32 power_on_tick = 0;
u8  pair_success_flag;
custom_cfg_t user_config;

u32 ota_program_offset = 0; //OTA start address
//int ota_program_bootAddr;

//u32 rx_cnt_aaa=0;
//u16 rf_state_aaa=0;
//s8  manual_paring_enable;
//s8  golden_dongle_enable;

/**
 * @brief       This function crs 16
 * @param[in]   len	- data len
 * @param[in]   pD	- data
 * @return     crc 
 * @note        
 */
unsigned short crc16_user (unsigned char *pD, int len)
{
	unsigned short 		crc16_poly_user[2] = {0, 0xa001}; 
	unsigned short crc = 0xffff;//crc16 init ffff
	int i,j;

	for(j=len; j>0; j--)//for every data
	{
		unsigned char ds = *pD++;//pointer pd
		for(i=0; i<8; i++)
		{
			crc = (crc >> 1) ^ crc16_poly_user[(crc ^ ds ) & 1];//deal every bit
			ds = ds >> 1;//right move 1 bit
		}
	}
	
	return crc;//return crc
}
/**
 * @brief       This function reset fifo
 * @param[in]   f	- the fifo needed reset
 * @return      
 * @note        
 */
void my_fifo_reset (my_fifo_t *f)
{
	
	f->wptr = 0; //wpt 0
	f->rptr = 0; //rpt 0
}


/**
 * @brief       This function init fifo
 * @param[in]   f	- pointer to fifo struct
 * @param[in]   n	- fifo num
 * @param[in]   p	- fifo data pointer
 * @param[in]   s	- fifo size
 * @return      
 * @note        
 */
void my_fifo_init (my_fifo_t *f, int s, u8 n, u8 *p)
{
	f->size = s;//set fifo size to s
	f->num = n;//set fifo num to n
	f->wptr = 0;//set wptr to 0
	f->rptr = 0;//set rptr to 0
	f->p = p;//fifo data pointer to p
}


/**
 * @brief       This function return fifo write pointer
 * @param[in]   f	- fifo
 * @return      
 * @note        
 */
u8* my_fifo_wptr (my_fifo_t *f)
{
	if (((f->wptr - f->rptr) & 255) < f->num)
	{
		return f->p + (f->wptr & (f->num-1)) * f->size;
	}
	return 0;
}


/**
 * @brief       This function wptr ++
 * @param[in]   f	- fifo
 * @return      
 * @note        
 */
void my_fifo_next (my_fifo_t *f)
{
	f->wptr++;//wptr++
}


/**
 * @brief       This function push data to fifo f
 * @param[in]   f	- fifo f
 * @param[in]   n	- data len
 * @param[in]   p	- the data pointer need sended
 * @return      
 * @note        
 */
int my_fifo_push (my_fifo_t *f, u8 *p, int n)
{
	if (((f->wptr - f->rptr) & 255) >= f->num)//if write wptr overlap num
	{
		return -1;
	}

	if (n >= f->size)//if data len over fifo size
	{
		return -1;
	}
	u8 *pd = f->p + (f->wptr++ & (f->num-1)) * f->size;
	//*pd++ = n & 0xff;
	//*pd++ = (n >> 8) & 0xff;
	memcpy (pd, p, n);//cpy data to fifo
	return 0;
}


/**
 * @brief       This function get fifo number
 * @param[in]   f	- fifo
 * @return      
 * @note        
 */
u8 my_fifo_number(my_fifo_t *f)
{
	u8 num =(f->wptr - f->rptr);
	return num;
}

/**
 * @brief       This function rptr ++
 * @param[in]   f	- fifo
 * @return      
 * @note        
 */
void my_fifo_pop (my_fifo_t *f)
{
	f->rptr++;
}


/**
 * @brief       This function get fifo data
 * @param[in]   f	- fifo
 * @return      
 * @note        
 */
u8 * my_fifo_get (my_fifo_t *f)
{
	if (f->rptr != f->wptr)//if have data
	{
		u8 *p = f->p + (f->rptr & (f->num-1)) * f->size;//pointer to read data
		return p;
	}
	return 0;
}



#if (USB_OTA_ENABLE_AAA||D24G_OTA_ENABLE_AAA) //ota en
int notify_rsp_buf2hci(u8 *data,u16 length);//notify rsp
void notify_rsp_update ();//rsp update

//bool fw_start_addr_offset=0;	//0-FW_ADDR:0X00000  1-FW_ADDR:0x10000
#define FW_SECTOR_LENGTH		0x10000	//64k
#define USER_FW_ADDR			(FW_SECTOR_LENGTH-ota_program_offset)
#define START_UP_FLAG			(0x544c4e4b)
#define MCU_RAM_START_ADDR		(0x840000)  // for 825x: 0x840000, don't change.

static u32 notify_rsp_tick;


u32 blt_ota_start_tick;

u8 usb_start_flag = 0;
#endif



int push_fifo_app(my_fifo_t *f, u8 type, u8 *buf, u8 len)
{
    if (((f->wptr - f->rptr) & 255) >= f->num)
    {
        return 1;
    }

    if (len > f->size)
    {
        return 2;
    }
    u8 *pd = f->p + (f->wptr++ & (f->num - 1)) * f->size;
    *pd++ = len ;
    *pd++ = type ;
	for(int i=0;i<len;i++)
	{
		*pd++=buf[i];
	}
	
    return 0;
}


#if(D24G_OTA_ENABLE_AAA)
u8 cb_fifo_rf_rx[48 * 16];
my_fifo_t	rx_rf_fifo =
{
    48,
    16,
    0,
    0,
    cb_fifo_rf_rx,

};

u8 cb_fifo_rf_tx[48 * 16];
my_fifo_t	tx_rf_fifo =
{
    48,
    16,
    0,
    0,
    cb_fifo_rf_tx,

}; 

extern my_fifo_t usb_to_pc_data;
#define		MYFIFO_INIT(name,size,n)		u8 name##_b[size * n]={0};my_fifo_t name = {size,n,0,0, name##_b}
MYFIFO_INIT(usb_to_pc_data, 80, 4);

/**
 * @brief	usb ota result process
 * @param	result
 * @return	ret
 */
int usb_ota_resut(u8 result)
{
	usb_data_t p;
	int count = 0;
	u32 tick = 0;
	int ret = 0;

	tick = clock_time();

	/* Fill response data for send to PC */
	p.report_id = USB_OTA_REPORT_ID;
	p.opcode = 0x02;
	p.length = 3;
	p.dat[0] = 0x06;
	p.dat[1] = 0xff;
	p.dat[2] = result;

    while(1)
	{
    	/* USB IRQ process */
		usb_handle_irq();

		if (clock_time_exceed(tick, 50000))
		{
			tick = clock_time();

			if (count == 0)
			{
				/* send response data to PC */
				ret = notify_rsp_buf2hci(&p.report_id, USB_OTA_LENGTH);

				if (ret)
				{ //send success
					count=1;
				}
			}
			else
			{
				break;
			}
		}
	}

    printf("---usb_ota_result=0x%01x.\r\n",result);

	//app_debug_ota_result(result);
	
	analog_write(SYS_DEEP_ANA_REG, OTA_STOP_AAA); //set deep_flag = OTA_STOP_AAA
	
	start_reboot(); //reboot
	
	return ret;
}

/**
 * @brief	USB OTA polling
 * @param	none
 * @return	none
 */
void usb_update_loop()
{
	//static u32 dbg_m_loop;
	//dbg_m_loop ++;

	/* if OTA is working but already overtime(120s), set OTA result = OTA_TIMEOUT */
	if ((usb_start_flag == 1) && clock_time_exceed(blt_ota_start_tick , 120000000))
	{
		usb_ota_resut(OTA_TIMEOUT); //means OTA failed
	}

	//proc_host();
	
	notify_rsp_update(); //check weather notify response data to PC
}

/**
 * @brief	load next firmware to ram and run
 * @param	addr_load£º firmware start address
 * @param	ramcode_size£º firmware ramcode size
 * @return	none
 */
void load_next_firmware(u32 addr_load, u32 ramcode_size)
{
	//func_test();	// just for compile error indication when over flow, but make no sense now.

	irq_disable();	// must, can't enter irq, because cstartup will be changed.

	// copy ramcode of next firmware.
	#if 0
		boot_load_memcpy4((u8 *)MCU_RAM_START_ADDR, (u8 *)addr_load, ramcode_size);	// have been sure that ramcode_size is 4 bytes align.
	#else
		flash_read_page(addr_load, ramcode_size, (u8 *)MCU_RAM_START_ADDR); // if use flash read, all flash API need to be located at "_attribute_no_over_written_code_"
	#endif

	printf("---addr_load=0x%03x,ramcode_size=%d.\n",addr_load,ramcode_size);

	//while(1);

#if 0 //no need restart
	// jump to next image
	WRITE_REG8(0x602, 0x88);	// reboot from RAM, and the IO setting will remain unchanged, such as GPIO's function of DP.
    while(1){// have been reboot before, just make sure not go ahead.
        //static volatile u32 boot_load_err;boot_load_err++; // can not use global variable here, incase there is some delay for reboot.
    };
#endif
}

/**
 * @brief	get firmware information
 * @param	none
 * @param	none
 * @return	none
 */
u8 run_app_code()
{	
	irq_disable();  // must, can't enter irq, because cstartup will be changed.

	u32 startup_flag = 0;
	
	/* get startup_flag value*/
	flash_read_page (8, sizeof(startup_flag), (u8 *)&startup_flag);
	
	if (START_UP_FLAG == startup_flag)
	{ //means firmware address in block A(0x00000), so OTA address in block B(0x10000)
		//fw_start_addr_offset = 1;
		ota_program_offset = FW_SECTOR_LENGTH;
	}

	//ota_program_offset = ota_program_bootAddr = USER_OTA_ADDR;

	printf("***********************************************************\r\n");
	printf("****** firmware_addr = 0x%03x, ota_addr = 0x%03x ******\r\n",USER_FW_ADDR,ota_program_offset);
	printf("***********************************************************\r\n");

	//crc32_init(0x04C11DB7, AA_crc_table);  //1600us

	u32 bin_size = 0;
	flash_read_page(USER_FW_ADDR + 0x18, 4, (u8 *)&bin_size);
	flash_read_page(USER_FW_ADDR + (bin_size - 4), 4, (u8 *)&output_dev_info.bin_crc);
	//flash_read_page(offset + 2, 4, (u8 *)&output_dev_info.fw_version);

	printf("---bin_crc=0x%04x.\r\n",output_dev_info.bin_crc);

	return 1;
}

/**
 * @brief	initial notify FIFO
 * @param	none
 * @return	none
 */
void notify_rsp_buf_init()
{
    //memset(notify_rsp_buf, 0, sizeof(notify_rsp_buf));
    //notify_rsp_buf_wptr = notify_rsp_buf_rptr = 0;
    my_fifo_reset(&usb_to_pc_data);
}

/**
 * @brief 	notify data to PC
 * @param	data
 * @param	length
 * @return	none
 */
int notify_rsp_buf2hci(u8 *data, u16 length)
{
	if (reg_irq_src & FLD_IRQ_USB_PWDN_EN)
	{
		//user_resume_host();
		reg_usb_ep_ctrl(USB_EDP_SPP_IN) = 0;
		return 1;
	}

	if (!reg_usb_host_conn)
	{
		reg_usb_ep_ctrl(USB_EDP_SPP_IN) = 0;
		return 1;
	}
	
	if (usbhw_is_ep_busy(USB_EDP_SPP_IN))
	{
		return 0;
	}

	reg_usb_ep_ptr(USB_EDP_SPP_IN) = 0;

	//printf("tx=0x");
	for (u8 i=0; i<length; i++)
	{
		//printf("%01x",data[i]);
		reg_usb_ep_dat(USB_EDP_SPP_IN) = data[i];
	}
	//printf("\r\n");	
	
	usbhw_data_ep_ack(USB_EDP_SPP_IN);

	return 1;
}

/**
 * @brief 	enter OTA mode call back process
 * @param	none
 * @return	none
 */
void app_enter_ota_mode(void)
{
#if MODULE_WATCHDOG_ENABLE
	wd_stop(); //stop normal watch dog
#endif

#if (MODULE_32K_WATCHDOG_ENABLE)
	wd_32k_stop(); //stop 32K watch dog
#endif

#if (FLASH_LOCK_ENABLE_AAA && (_CHIP_IS_OTP_==0))
	flash_lock_handle(FLASH_LOCK_NONE_BLOCK); //unlock flash
#endif

	ui_ota_is_working = 1; //set OTA working flag

#if (BLT_APP_LED_ENABLE && BLE_OTA_LED_DEBUG)
	gpio_write(PIN_BLE_LED, 1);
#endif

	printf("---start ota\r\n");

	blt_ota_start_tick = clock_time();  //mark time

	usb_start_flag = 1; //set OTA start flag
	
	//bls_ota_setTimeout(120 * 1000 * 1000); //set OTA timeout  15 seconds
}

/**
 * @brief 	End OTA mode call back process
 * @param	result
 * @return	none
 */
void app_debug_ota_result(int result)
{
    irq_disable();

    //flash_erase_sector(CFG_DEVICE_MODE_ADDR);

#if MODULE_WATCHDOG_ENABLE
    wd_stop(); //stop normal watch dog
#endif

#if (MODULE_32K_WATCHDOG_ENABLE)
	wd_32k_stop(); //stop 32K watch dog
#endif

#if(BLE_OTA_LED_DEBUG && BLT_APP_LED_ENABLE)
    gpio_set_output_en(PIN_BLE_LED, 1);
    if (result == OTA_SUCCESS)  //OTA success
    {
        gpio_write(PIN_BLE_LED, 1);
        sleep_us(2000000);  //led on for 2 second
        gpio_write(PIN_BLE_LED, 0);

        printf("ota success\r\n");
    }
    else   //OTA fail
    {
        for (int i = 0; i < 4; i++)
        {
            gpio_write(PIN_BLE_LED, 1);
            sleep_us(250000);
            gpio_write(PIN_BLE_LED, 0);
            sleep_us(250000);
        }

        printf("ota fail=%x\r\n",result);
    }
#endif
}

/**
 * @brief 	USB OTA mode write data to flash
 * @param	p_usb: OTA data
 * @return	result
 */
u8 usb_ota_write(usb_data_t *p_usb)
{
	static u16 ota_index = 0;
	static u16 start_index = 0;
	static u32 flash_write_addr = 0;
	static u8  first_data_buf[16];
	static u32 fw_size = 0;
	static u8  ota_error_flag = 0;
	ota_data_st *pd = (ota_data_st*)&p_usb->dat[0];

	if ((pd->cmd == CMD_OTA_START) && (p_usb->length == 2))
	{ //receive OTA Start CMD
		blt_ota_start_tick = clock_time();  //mark time
		usb_start_flag = 1;  //set OTA start flag
		notify_rsp_buf_init(); //initial notify FIFO
	#if MODULE_WATCHDOG_ENABLE
		wd_stop(); //stop normal watch dog
	#endif
	#if (MODULE_32K_WATCHDOG_ENABLE)
		wd_32k_stop(); //stop 32K watch dog
	#endif
		ota_index = 0;
		flash_write_addr = 0;
		start_index = 0;
		fw_size = 0;
		//fw_check_value = 0;
		//fw_cal_crc = 0xffffffff;
		ota_error_flag = OTA_SUCCESS;
		flash_erase_sector(ota_program_offset); //erase OTA start address
		app_enter_ota_mode(); //enter OTA mode call back process
		
		printf("******** usb_ota_start at=0x%03x ********\r\n",ota_program_offset);

		return ota_error_flag;
	}
	else if (ota_error_flag == OTA_SUCCESS)
	{ //OTA working right
		if ((pd->cmd == CMD_OTA_END) && (p_usb->length == 6))
		{ //receive OTA End CMD
			printf("---usb_ota_end\r\n");

			/* check first packet data is right or not */
			u32 *telink_mark = (u32*)&first_data_buf[8];
			if (telink_mark[0] != 0x544c4e4b)
			{ //first packet data is wrong, return error flag
				ota_error_flag = OTA_FIRMWARE_MARK_ERR;
				return ota_error_flag;
			}

			u32 real_bin_size = 0;
			//real_bin_size = fw_size-4;
			real_bin_size = fw_size;
			printf("---real_bin_size=%d_%d, fw_size=%d.\n", real_bin_size, (start_index*16), fw_size);

			if ((real_bin_size%16) == 0)
			{ //if real_bin_size is a multiple of 16
				printf("---1 real_bin_size=%d_%d.\n", real_bin_size, ((start_index+1)*16));
				
				if (real_bin_size != ((start_index+1)*16)) //
				{ //real_bin_size wrong, return error flag
					ota_error_flag = OTA_FW_SIZE_ERR;
					return ota_error_flag;
				}
			}
			else
			{ //if real_bin_size is not a multiple of 16
				printf("---2 real_bin_size=%d_%d.\n", (real_bin_size-(real_bin_size%16)), ((start_index+1)*16));
				
				if ((real_bin_size - (real_bin_size%16)) != (start_index * 16))
				{ //real_bin_size wrong, return error flag
					ota_error_flag = OTA_FW_SIZE_ERR;
					return ota_error_flag;
				}
			}

			//printf("******** write first packet to OTA addr:0x%03x ********\n",ota_program_offset);
			flash_write_page(ota_program_offset,16,first_data_buf);

			/* read back first packet to check weather write is right or not */
			u8 read_flash_buf[16];  
			flash_read_page(ota_program_offset,16, read_flash_buf);

			if (memcmp(read_flash_buf, first_data_buf, 16))
			{  //do not equal, erase OTA start address data, return error flag
				flash_erase_sector(ota_program_offset);
				ota_error_flag = OTA_WRITE_FLASH_ERR;
				return ota_error_flag;
			}

			u32 flag = 0;
			/* check OTA data */
			u8 ret = flash_fw_check(0xffffffff,ota_program_offset);

			if (ret==0)
			{ //OTA data is right, run OTA success result
				//extern u32 fw_crc_init;
				//printf("---fw_crc_init=0x%04x\r\n",fw_crc_init);

				printf("---usb ota success, erase Firmware\r\n");
				flash_write_page(USER_FW_ADDR + 0x08, 4, (u8 *)&flag);	//set invalid flag to firmware block
				usb_ota_resut(OTA_SUCCESS);
			}
			else
			{ //OTA data is wrong, return error flag

				printf("---usb ota fail, erase OTA\r\n");
				flash_erase_sector(ota_program_offset);
				ota_error_flag = OTA_FW_CHECK_ERR;
				return ota_error_flag;
				
			}
		}
		else 
		{ //receive OTA normal data, normal data lengt = 2byte CMD(packet's count) + 16byte valid data + 2byte crc16 = 20byte
			//check data packet length is 20 or not
			if ((p_usb->length % 20) != 0)
			{
				ota_error_flag = OTA_PDU_LEN_ERR;
				return ota_error_flag;
			}

			//get data packet's count
			u8 cnt = p_usb->length/20;
			
			//check every packet is right or not
			for(u8 i=0; i<cnt; i++)
			{
				pd = (ota_data_st*)&p_usb->dat[20*i];
				//printf("pd_crc=%02x, user_crc=0x%02x.\n", pd->crc, crc16_user((u8*)&pd->cmd,18));

				if (crc16_user((u8*)&pd->cmd, 18) == pd->crc)
				{ //The calculated crc16 is equal to the crc16 in the packet, so the data is right
					if (pd->cmd == 0x0000)
		 			{ //first packet
						memcpy(first_data_buf,pd->buf,16); //save first packet data
						start_index = 0; //clear packet count
		 			}
					else
					{ //else packet
						if ((start_index+1) != pd->cmd)
						{ //if received packet count plus 1 is not equal to CMD(packet's count), means packet count is wrong, return wrong flag
							ota_error_flag = OTA_DATA_PACKET_SEQ_ERR;
							return ota_error_flag;
						}

						start_index = pd->cmd; //save new received packet
						//printf("---start_index=%d.\n",start_index);

						if (pd->cmd == 0x0001) //second packet
						{
							/* get firmware size */
							fw_size = pd->buf[8] | (pd->buf[9] <<8) |(pd->buf[10]<<16) | (pd->buf[11]<<24);
							printf("---fw_size=%d.\n",fw_size);

							if (fw_size > FW_SECTOR_LENGTH)//128k
							{ //if fw_size more than OTA block, return error flag
								ota_error_flag = OTA_FW_SIZE_ERR;
								return ota_error_flag;
							}
						}
						
						if (fw_size < flash_write_addr)
						{ //if fw_size less then received data, return error flag
							ota_error_flag = OTA_FW_SIZE_ERR;
							return ota_error_flag;
						}
						
						if (ota_error_flag == OTA_SUCCESS)
						{
							if ((flash_write_addr % 4096) == 0)
					   		{ //if received data is a multiple of 4096, erase next flash block
								flash_erase_sector(flash_write_addr + ota_program_offset);
					   		}

							//write current packet's data to flash
					   		flash_write_page(flash_write_addr+ota_program_offset, 16, pd->buf);
						}
						
					}
					
					//received data plus 16
					flash_write_addr += 16;
				}
				else
				{
					//The calculated crc16 is not equal to the crc16 in the packet, return error flag
					ota_error_flag = OTA_DATA_CRC_ERR;
					return ota_error_flag;
				}
			}
		}
	}
	
	return ota_error_flag;
}

/**
 * @brief 	process received USB data from PC
 * @param	p: data start address
 * @param	length: data length
 * @return	0:fail 1:success 
 */
u8 usb_data_handle(usb_data_t *p, u16 length)
{
	if (((p->report_id == USB_OTA_REPORT_ID) && (length == USB_OTA_LENGTH)) ||\
		((p->report_id == D24G_OTA_REPORT_ID) && (length == D24G_OTA_LENGTH)))
	{ //if received USB data is OTA data
		if ((p->opcode == 0x01) && (p->length == 0))
		{ //PC get firmware version,
			p->length = 8;
			memcpy(&p->dat[0], (u8 *)&output_dev_info.fw_version, 4);
			memcpy(&p->dat[4], (u8 *)&output_dev_info.bin_crc, 4);
		}
		else if (p->opcode == 0x02)
		{ //PC send OTA data, check OTA data is right or wrong, if right write data to flash ,if wrong return ota_error_flag

			if (p->report_id == USB_OTA_REPORT_ID)
			{
				u8 ota_error_flag = usb_ota_write(p);
				
				if(ota_error_flag)
				{ //OTA data is wrong, run OTA result call back
					usb_ota_resut(ota_error_flag);
				}
			}
		}
	
		/* ACK received packet data to PC */
		if (notify_rsp_buf2hci(&p->report_id, length) == 0)
		{
			if (p->report_id == USB_OTA_REPORT_ID)
			{	//notify fail, mark time and push ACK data to fifo, wait for next notify, until notify success or OTA timeout fail
				notify_rsp_tick = clock_time()|1;
				my_fifo_push(&usb_to_pc_data, &p->report_id, length);
			}

			return 0;
		}
	}

	return 1;
}

/**
 * @brief   PC set report process
 * @param	data_request
 * @param	report_id
 * @param	length
 * @return	none
 */
void app_hid_set_report_handle(u8 data_request, u8 report_id, u16 length)
{
	static u16 ep0_out_data_len=0;
	static u8  ep0_out_data_buf[USB_OTA_LENGTH]={0};
	static u16 ep0_out_index=0;
	u8 i;

#if USB_OTA_ENABLE_AAA
	if (report_id == USB_OTA_REPORT_ID)
	{
		if (data_request) //PC set report happened
		{
			if (ep0_out_data_len < USB_OTA_LENGTH)
			{ //Data is not received completely, read 8 byte data
				for (i=0; i<8; i++)
				{
					ep0_out_data_buf[ep0_out_index] = usbhw_read_ctrl_ep_data(); //read data
					ep0_out_index ++; //index plus 1
				}

				//received data length plus 8
				ep0_out_data_len += 8;

				if(ep0_out_data_len == USB_OTA_LENGTH)
				{ //Data received completely

					/* clear index and date length wait for next time that PC set report data */
					ep0_out_index = 0;
					ep0_out_data_len = 0;
					
					//printf("rx=0x");
					//for(i=0;i<length;i++)
					//{
						//printf("%01x",ep0_out_data_buf[i]);
					//}
					//printf("\r\n");

					usb_data_t *p = (usb_data_t *)&ep0_out_data_buf[0];
					usb_data_handle (p, length); //process received data from PC set report data
				}
			}
		}
		else
		{
			/* clear index and date length wait for next time that PC set report data */
			ep0_out_data_len = 0;
			ep0_out_index = 0;
			
			printf("set_report_cmd=0x%01x,length=%d.\r\n",report_id,length);
		}
	}
#endif

#if D24G_OTA_ENABLE_AAA
	if (report_id == D24G_OTA_REPORT_ID)
	{
		if (data_request) //PC set report happened
		{
			if (ep0_out_data_len < D24G_OTA_LENGTH)
			{ //Data is not received completely, read 8 byte data
				for (i=0; i<8; i++)
				{
					ep0_out_data_buf[ep0_out_index] = usbhw_read_ctrl_ep_data(); //read data
					ep0_out_index ++; //index plus 1
				}

				//received data length plus 8
				ep0_out_data_len += 8;

				if(ep0_out_data_len == D24G_OTA_LENGTH)
				{ //Data received completely

					/* clear index and date length wait for next time that PC set report data */
					ep0_out_index = 0;
					ep0_out_data_len = 0;

					//printf("rx=0x");
					//for (i=0; i<length; i++)
					{
						//printf("%01x", ep0_out_data_buf[i]);
					}
					//printf("\r\n");
					
					push_fifo_app(&tx_rf_fifo, D24G_OTA_ACK_CMD, &ep0_out_data_buf[0], D24G_OTA_LENGTH);
				}
			}
		}
		else
		{
			/* clear index and date length wait for next time that PC set report data */
			ep0_out_data_len = 0;
			ep0_out_index = 0;
			
			printf("set_report_cmd=0x%01x,length=%d.\r\n",report_id,length);
		}
	}
#endif

}

/**
 * @brief   notify fifo's ACK data
 * @param	none
 * @return	none
 */
void notify_rsp_update ()
{
    if (notify_rsp_tick && clock_time_exceed(notify_rsp_tick, 600))
	{ //It runs every 600us
		u8 *p = my_fifo_get(&usb_to_pc_data);
		
		if (p)
		{ //fifo has data
			int length = p[0] + (p[1]<<8);
			
			if (notify_rsp_buf2hci(&p[2], length))
			{ //notify success
				my_fifo_pop(&usb_to_pc_data); //pop fifo
				notify_rsp_tick = 0; //clear time_tick
			}
		}
		else
		{ //fifo empty
			notify_rsp_tick = 0; //clear time_tick
		}	
	}
}
#endif

/**
 * @brief   update_channel_mask
 * @param	none
 * @return	none
 */
u8 update_channel_mask(u8 mask, u8 chn, u8 *chn_pkt)
{
#if (HOPING_FREQ==1)	
	static int ll_chn_sel_chg, ll_chn_hold;
	if (clock_time_exceed(tick_fh, frq_hopping_data.fre_hp_always_time_us))  {
		chn_pkt[ll_chn_sel_chg] = 0;
		chn_pkt[!ll_chn_sel_chg] = frq_hopping_data.frq_hp_chn_pkt_rcvd_max;
	}
	if (ll_chn_hold) {
		ll_chn_hold--;
		chn_pkt[0] = chn_pkt[1] = 0;
	}
	int diff = chn_pkt[ll_chn_sel] - chn_pkt[!ll_chn_sel];
	int hit_th = diff > frq_hopping_data.frq_hp_hit_diff_num;
	if (chn_pkt[ll_chn_sel] >= frq_hopping_data.frq_hp_chn_pkt_rcvd_max || hit_th) {
		int dual_chn[2];
		dual_chn[0] = mask & 0x0f;
		dual_chn[1] = mask >> 4;
		if (hit_th) { //change channel
			ll_chn_hold = 32;//32ms
			chn = dual_chn[!ll_chn_sel];
			for (int i=0; i<8; i++) {
				chn = LL_NEXT_CHANNEL (chn);
				if ((ll_chn_sel && chn != dual_chn[1])) {
					mask = (mask & 0xf0) | chn;
					break;
				}
				else if (!ll_chn_sel && chn != dual_chn[0]) {
					mask = (mask & 0x0f) | (chn << 4);
					break;
				}
			}
			tick_fh = clock_time ();
			ll_chn_sel_chg = !ll_chn_sel;		 //remember latest channel change
		}
		chn_pkt[0] = chn_pkt[1] = 0;
	}
#endif
return mask;
}

#define FRE_OFFSET 	0
//#define FRE_STEP 	5

const unsigned char rf_chn[5] =
{
    FRE_OFFSET + 5, FRE_OFFSET + 17, FRE_OFFSET + 30,
    FRE_OFFSET + 45, FRE_OFFSET + 60,
};

/**
 * @brief   set RF channel
 * @param	chn: frequency point
 * @param	set:
 * @return	none
 */
_attribute_ram_code_sec_ void rf_set_channel_aaa(signed char chn, unsigned short set)
{
	rf_set_channel (rf_chn[chn],0);
	//rf_set_channel (chn, set);
}

/**
 * @brief   ACK rf data
 * @param	rf_length
 * @return	none
 */
_attribute_ram_code_ void user_rf_ack_send(int rf_length)
{
	ack_rf_dat.rf_len = rf_length+HEAD_LENGTH;//fr len
	ack_rf_dat.dma_len = ack_rf_dat.rf_len+1;//dma len
    //PIN_DEBUG_RF_TX_LEVEL(1);

#if D24G_OTA_ENABLE_AAA
	u8 *p = my_fifo_get(&tx_rf_fifo);
	
	if (p)
	{
		if (p[1] == D24G_OTA_ACK_CMD)
		{
			ack_rf_dat.dat[2] = 1; //pno_no = 1
			memcpy(&ack_rf_dat.dat[7], &p[2], 24);
		}
	}
#endif

	rf_set_tx_rx_off();
	rf_start_stx((void *)&ack_rf_dat.dma_len, clock_time());
}

//rf_set_channel(59,0);

/**
 * @brief   clear pair flag
 * @param	none
 * @return	none
 * @note	means exit pair mode
 */
void clear_pair_enable_flag()
{
	printf("---clear_pair_enable_flag\n");

#if (GOLD_TEST_DONGLE == 0)
	rf_pair_enable = 0;
#endif

	pair_tick = 0;
	pair_success_flag = 0;
	first_rx_normal_data_tick = 0;

	// TODO:rf_rx_acc_code_enable(0x06);//mouse&keboard
}

/**
 * @brief   check_first_normal_data
 * @param	none
 * @return	none
 * @note	if received normal overtime(1s), clear pair flag
 */
void check_first_normal_data()
{
	if(first_rx_normal_data_tick == 0)
	{
		first_rx_normal_data_tick = clock_time()|1;
	}
	else if(rf_pair_enable && clock_time_exceed(first_rx_normal_data_tick,100000))
	{
	    clear_pair_enable_flag();
	}
}

#if(0)
/**
 * @brief   push keyboard or mouse data to fifo
 * @param	none
 * @return	push result
 */
u8 push_usb_fifo_aaa(u8 type,u8 *buf,u8 len)
{
	/* get next buffer first address p */
	USB_DATA_S *p = (USB_DATA_S*)usb_fifo_aaa.fifo[usb_fifo_aaa.wptr & (USB_FIFO_NUM-1)];

	p->type = type; //save type
	memcpy(p->buf, buf, len); //save buffer

	usb_fifo_aaa.wptr++; //skip to next buffer

	/* get used fifo's num */
	int fifo_use = (usb_fifo_aaa.wptr - usb_fifo_aaa.rptr) & (USB_FIFO_NUM*2-1);

	if (fifo_use > USB_FIFO_NUM)
	{ //used fifo's num overflow
		usb_fifo_aaa.rptr++; //overlap older data
	}
	return 1;
}
#else
u8 push_usb_fifo_aaa(u8 type, u8 *p, int n)
{
	my_fifo_t *f = &usb_fifo_aaa;
	if (((f->wptr - f->rptr) & 255) >= f->num)//if write wptr overlap num
	{
		printf("fifo over 16\n");
		return -1;
	}

	if (n >= f->size)//if data len over fifo size
	{
		return -1;
	}
	u8 *pd = f->p + (f->wptr++ & (f->num-1)) * f->size;
	*pd++ = type;
	//*pd++ = (n >> 8) & 0xff;
	memcpy (pd, p, n);//cpy data to fifo
	return 0;
}
#endif
/**
 * @brief       This function push pc data to usb rx fifo
 * @param[in]   buf	- 
 * @param[in]   len	- 
 * @param[in]   type	- 
 * @return      
 * @note        
 */
u8 push_rx_usb_fifo_aaa(u8 *buf,u8 len)
{
	u8 *p=(u8*)usb_rx_fifo_aaa.fifo[usb_rx_fifo_aaa.wptr&(USB_RX_FIFO_NUM-1)];//pointer to usb fifo
	memcpy(p,buf,len);//copy data
	usb_rx_fifo_aaa.wptr++;//wptr ++
	int fifo_use=(usb_rx_fifo_aaa.wptr-usb_rx_fifo_aaa.rptr)&(USB_RX_FIFO_NUM*2-1);//use fifo
	if(fifo_use>USB_RX_FIFO_NUM)//>fifo num
	{
		usb_rx_fifo_aaa.rptr++;//overlap older data
	}
	return 1;
}
void push_pc_data()
{
	pc_ack_data_t *p_km_ack_data=(pc_ack_data_t*)&ack_rf_dat.dat[0];
	if((usb_rx_fifo_aaa.wptr!=usb_rx_fifo_aaa.rptr)&&(has_pc_data==0))//have data
	{
		u8 *p=(u8*)usb_rx_fifo_aaa.fifo[usb_rx_fifo_aaa.rptr&(USB_RX_FIFO_NUM-1)];//usb data
			memcpy((u8*)&p_km_ack_data->pc_dat[0],(u8*)&p[1],p[2]+2);
			has_pc_data = 1;
	}
	else
	{
			has_pc_data = 0;
	}
}

/**
 * @brief   Convert the access code
 * @param	code
 * @return	result
 */
static inline u32 rf_access_code_24to32 (u32 code)
{
	u32 r = (code & 0x00ffff00)>>8;
	u32 p = code & 0xff;
	u32 t = 0;
	for (int i=0; i<8; i++) {
		t = t << 2;
		t |= ((p & BIT(i)) ? 1 : 2);
		t = t << 2;
		t |= (r & 0x03);
		r = r >>2;
	}
	return t;
}

/**
 * @brief   set pair access code
 * @param	code
 * @return	none
 */
void set_pair_access_code(u32 code)
{
    write_reg32(0x800408, ((code & 0xffffff00) | 0x71));
    write_reg8(0x80040c, code);
}

/**
 * @brief   set data access code
 * @param	code
 * @return	none
 */
void set_data_access_code(u32 code)
{
#if MORE_PIPE_ENABLE
	write_reg32(0x800410, ((code & 0xffffff00) | 0x77));
	write_reg8(0x800414, code);
#else
	write_reg32(0x800408, ((code & 0xffffff00) | 0x77));
	write_reg8(0x80040c, code);
#endif
}


/**
 * @brief       This function get access code
 * @return      
 * @note        
 */
static inline u32 rf_get_access_code1 (void)
{
	return read_reg8 (0x800414) | (read_reg32(0x800410) & 0xffffff00);
}

#if (RF_FAST_TX_SETTING_ENABLE||RF_FAST_RX_SETTING_ENABLE)//fast tx or rx enable

#if RF_FAST_RX_SETTING_ENABLE//fast rx en

/**
 * @brief       This function init rf_rx fast settle
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_rx_fast_settle_init()
{
	//RX_SETTLE_TIME_44US)
	write_reg8(0x1284,(read_reg8(0x1284)&0x87)|(0x6<<3));

	write_reg8(0x12b0,0x00);
	write_reg8(0x12b1,read_reg8(0x12b1)&0xfe);
	write_reg8(0x12b2,0x10);
	write_reg8(0x12b3,read_reg8(0x12b3)&0xfe);
	write_reg8(0x12b4,0x10);
	write_reg8(0x12b5,read_reg8(0x12b5)&0xfe);
	write_reg8(0x12b6,0x32);
	write_reg8(0x12b7,read_reg8(0x12b7)&0xfe);
	write_reg8(0x12b8,0x58);
	write_reg8(0x12b9,read_reg8(0x12b9)&0xfe);
	write_reg8(0x12ba,0x58 );
	write_reg8(0x12bb,read_reg8(0x12bb)&0xfe);
}


/**
 * @brief       This function set_rx_fast_settle en
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_rx_fast_settle_en(void)
{
	write_reg8(0x1229,read_reg8(0x1229)|0x01);
}

/**
 * @brief       This function set fast_settle dis
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_rx_fast_settle_dis(void)
{
	write_reg8(0x1229,read_reg8(0x1229)&0xfe);
}

#endif

#if RF_FAST_TX_SETTING_ENABLE //if rf tx setting enable

/**
 * @brief       This function init rf_tx fast settle
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_tx_fast_settle_init()
{
	//TX_SETTLE_TIME_53US)
	write_reg8(0x1284,(read_reg8(0x1284)&0xf8)|0x02);

	write_reg8(0x12a4,0x00);
	write_reg8(0x12a5,read_reg8(0x12a5)&0xfe);
	write_reg8(0x12a6,0x10);
	write_reg8(0x12a7,read_reg8(0x12a7)&0xfe);
	write_reg8(0x12a8,0x62);
	write_reg8(0x12a9,read_reg8(0x12a9)&0xfe);
	write_reg8(0x12aa,0x64);
	write_reg8(0x12ab,read_reg8(0x12ab)&0xfe);
	write_reg8(0x12ac,0x6a);
	write_reg8(0x12ad,read_reg8(0x12ad)&0xfe);
	write_reg8(0x12ae,0x6a);
	write_reg8(0x12af,read_reg8(0x12af)&0xfe);
	write_reg8(0x12bc,0x62);
	write_reg8(0x12bd,read_reg8(0x12bd)&0xfe);
}


/**
 * @brief       This function set rf_tx fast settle en
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_tx_fast_settle_en(void)
{
	write_reg8(0x1229,read_reg8(0x1229)|0x02);
}

/**
 * @brief       This function set rf_tx fast settle dis
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_tx_fast_settle_dis(void)
{
	write_reg8(0x1229,read_reg8(0x1229)&0xfd);
}
#endif


/**
 * @brief       This function check rf fast setting time
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void check_rf_fast_setting_time()
{
#if (RF_FAST_TX_SETTING_ENABLE||RF_FAST_RX_SETTING_ENABLE)//rx tx fast en
	if(clock_time_exceed(tick_rf_fast_setting, 2000000))//2s
	{
		tick_rf_fast_setting=clock_time();//update time
		rf_fast_setting_flag=1;//flag 1
	}
#endif
}


/**
 * @brief       This function check rf fasting setting flag
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void check_rf_fast_setting_flag()
{
#if (RF_FAST_TX_SETTING_ENABLE||RF_FAST_RX_SETTING_ENABLE)
	if(rf_fast_setting_flag==1)//timeout
	{
		rf_fast_setting_flag=0;//defualt
	}
	else
	{
		rf_fast_setting_flag=2;//enable rf fast setting flag
	}
#endif
}


/**
 * @brief       This function set rf_tx_rx setting
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_set_tx_rx_setting()
{
#if (RF_FAST_TX_SETTING_ENABLE || RF_FAST_RX_SETTING_ENABLE)
	if (rf_fast_setting_flag < 2)
	{ //normal set
		#if RF_FAST_TX_SETTING_ENABLE
			write_reg8(0x0f04, 0x70);//tx settle time: Default 150us, minimum 113us(0x70+1)
			rf_tx_fast_settle_dis();
		#endif
		
		#if RF_FAST_RX_SETTING_ENABLE
			write_reg8(0x0f0c, 0x54); //rx settle time: Default 150us, minimum 85us(0x54+1)
			rf_rx_fast_settle_dis();
		#endif
	}
	else //if(rf_ca_cnt==1)
	{ //fast set
		#if RF_FAST_TX_SETTING_ENABLE
			//write_reg8(0x0f04, 70);//tx settle time: Default 150us, minimum 113us(0x70+1)
			write_reg8(0x0f04, 65);
			rf_tx_fast_settle_en();
		#endif

		#if RF_FAST_RX_SETTING_ENABLE
			//write_reg8(0x0f0c, 54); //rx settle time: Default 150us, minimum 85us(0x54+1)
			write_reg8(0x0f0c, 48);
			rf_rx_fast_settle_en();
		#endif
	}
#endif
}
#endif


/**
 * @brief       This function init rf user
 * @return      
 * @note        
 */
void rf_user_init()
{
	rf_mode_init();//mode init
	rf_set_pri_2M_mode();//pri 2m

	rf_rx_buffer_set((u8*)rf_rx_buff, PKT_BUFF_SIZE, 0);//set rf_rxbuff

	write_reg8(0x405, (read_reg8(0x405)&0xf8)|5); //access_byte_num[2:0]
	write_reg8(0x420, 35);

	rf_set_power_level_index(DEFAULT_NORMAL_TX_POWER);//normak tx power

	// write_reg32(0x800408,0x39517671);  //for 8366
	// write_reg8(0x80040c,0x95);
	set_pair_access_code(pair_accesscode);//pair accesscode

	// write_reg32(0x800410,0xd3f03577);  //for 8366
	// write_reg8(0x800414,0xe7);

	//write_reg32(0x800418,0xc0300c03); //for 8366

	write_reg16(0xf06, 0);//rx wait time
	write_reg8(0x0f0c, 0x54); //rx settle time: Default 150us, minimum 85us(0x54+1)

	write_reg16(0xf0e, 0);//tx wait time
	write_reg8(0x0f04, 0x70);//tx settle time: Default 150us, minimum 113us(0x70+1)

	write_reg8(0xf10, 0);// wait time on NAK

	write_reg8(0x402, 0x44); //preamble length 4 bytes

#if (RF_FAST_TX_SETTING_ENABLE||RF_FAST_RX_SETTING_ENABLE)	//rf tx rx fast en
	rf_fast_setting_flag=0;//0
	tick_rf_fast_setting=clock_time()|1;//update fast setting tick
	#if RF_FAST_RX_SETTING_ENABLE	//must fist  call rx fast settle init
		rf_rx_fast_settle_init();
		rf_rx_fast_settle_dis();
	#endif
	
	#if RF_FAST_TX_SETTING_ENABLE	
		rf_tx_fast_settle_init();
		rf_tx_fast_settle_dis();
	#endif
#endif

	rf_set_tx_rx_off();//tx rx mode reset
#if MCU_CORE_B80B
	#if MCU_CORE_B80B
		rf_rx_acc_code_enable(0X07);//enable pipe 0 1 2
		rf_tx_acc_code_select(0);//
	
		set_data_access_code(dongle_id);
	#else
		rf_rx_acc_code_enable(0X01);//enable pipe 0 1 2
		rf_tx_acc_code_select(0);//
	
		//set_data_access_code(dongle_id);
	#endif
#endif
	irq_disable();//dis irq
	rf_irq_clr_src(0xffff);//clear irq src
	irq_enable_type(FLD_IRQ_ZB_RT_EN);
	rf_irq_disable(FLD_RF_IRQ_ALL);
	rf_irq_enable(FLD_RF_IRQ_TX|FLD_RF_IRQ_RX);
}


/**
 * @brief       This function handle rf tx handle
 * @return      
 * @note        
 */
_attribute_ram_code_sec_ void rf_tx_irq_handle()
{
	rf_set_tx_rx_off();		//add for save

#if (RF_FAST_TX_SETTING_ENABLE||RF_FAST_RX_SETTING_ENABLE)	//fast en
	check_rf_fast_setting_flag();//check flag
	rf_set_tx_rx_setting();//setting
#endif	
	rf_set_channel_aaa (host_channel, RF_CHN_TABLE);//set chn
	rf_set_rxmode ();//rxmode
}


/**
 * @brief       This function cheak ram data
 * @param[in]   p	- 
 * @return      
 * @note        
 */
u8 check_from_ram(rf_packet_t *p)
{
	static u16 buf[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};//init

	if((memcmp(buf, (u8*)&p->did, ID_LEN)))
	{
		memcpy(buf, (u8*)&p->did, ID_LEN);
		return 0;
	}

	return 1;
}

int set_device_id_in_firmware (u8 *p)
{
    rf_packet_t *p_rf_data_pkt = (rf_packet_t *)p;//rf pkt

	u8 dec[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
#if (AES_METHOD == 0)	
	memcpy(&dec[0],(u8*)&p_rf_data_pkt->did, ID_LEN);
#elif (AES_METHOD == 1)
		memcpy(encKey, pub_key, 16);//pubkey
    //aes_user_decryption((u8 *)&p_pair_data->did, &dec[0]);
#endif

	u8 device_index = 0;//0 mouse 1 keyboard
    if ((p_rf_data_pkt->did&0x03)==2)//identify device type
    {
		device_index=1;//keyboard
    }
#if GOLD_TEST_DONGLE
		//no need check device_id
		memcpy(&key_enc[device_index].did, dec, ID_LEN); //when gold_dongle enable, save device_id to ram directly
#endif

    //if(memcmp((u8*)&device_id, dec, ID_LEN) == 0)
    if (memcmp((u8*)&key_enc[device_index].did, dec, ID_LEN) == 0)
    {
		return 1;
    }
	else if(custom_binding_idx < PARING_MAX_NUM)
	{
		//memcpy((u8*)&device_id, dec, ID_LEN);
		memcpy((u8*)&key_enc[device_index].did, dec, ID_LEN);
		u32 adr = (ID_SAVE_STORAGE_ADDR + ID_LEN * custom_binding_idx);
		u8 tmp_buf[ID_LEN];
	#if HW_IS_FLASH
				/* save device_id to flash */
				flash_write_page(adr, ID_LEN, (u8*)&dec[0]);
				flash_read_page(adr, ID_LEN, tmp_buf);	//Read the stored content back out
				custom_binding_idx++;
	#else
				/* save device_id to OTP */
				rf_set_tx_rx_off(); //disable RF TX RX
				unsigned char r = irq_disable(); //disable IRQ
				for (u8 i=0; i<2; i++)
				{
					otp_set_active_mode(); //active OTP
					otp_write(adr, ID_LEN/4, (u32*)&dec[0]); //write device_id
					otp_read(adr, ID_LEN/4, (u32*)&tmp_buf[0]); //read back device_id
					/* check weather write success or fail */
					if (memcmp(&dec[0], &tmp_buf[0], ID_LEN) == 0)
					{ //write success, break
						custom_binding_idx ++;
						break;
					}
					else
					{ //write fail, retry
						custom_binding_idx ++;
						if (custom_binding_idx >= PARING_MAX_NUM)
						{ //index overflow, break
							break;
						}
						/* skip to next storage address */
						adr = (ID_SAVE_STORAGE_ADDR + ID_LEN * custom_binding_idx);
					}
				}
				irq_restore(r); //restore IRQ
				/* set to RX mode */
				rf_set_channel_aaa (host_channel, RF_CHN_TABLE);
				rf_set_rxmode ();
	#endif
			
	#if HW_IS_FLASH 
				//When the offset exceeds 3000, or the stored data is incorrect, than storage data from first address
				if ((ID_LEN * custom_binding_idx > 3000) || (memcmp(tmp_buf, dec, ID_LEN) != 0))
				{
					custom_binding_idx = 0;
					
					flash_erase_sector(ID_SAVE_STORAGE_ADDR);
			
					if ((key_enc[0].did != U32_MAX) && (key_enc[0].did != 0))
					{ //valid device_id, write to flash
						flash_write_page(ID_SAVE_STORAGE_ADDR + custom_binding_idx * ID_LEN, ID_LEN, (u8*)&key_enc[0].did);
						/* read back mouse id(device_index == 0) to ram */
						if (device_index == 0)
						{
							flash_read_page(ID_SAVE_STORAGE_ADDR + custom_binding_idx * ID_LEN, ID_LEN, tmp_buf);
						}
						custom_binding_idx ++;
					}
			
					if ((key_enc[1].did != U32_MAX) && (key_enc[1].did != 0))
					{ //valid device_id, write to flash
						flash_write_page(ID_SAVE_STORAGE_ADDR + custom_binding_idx * ID_LEN, ID_LEN, (u8*)&key_enc[1].did);
						/* read back keyboard id(device_index == 1) to ram */
						if(device_index == 1)
						{
							flash_read_page(ID_SAVE_STORAGE_ADDR + custom_binding_idx * ID_LEN, ID_LEN, tmp_buf);
						}
						custom_binding_idx ++;
					}
				}
	#endif

		return 1;
	}

	return 0;
}





_attribute_ram_code_ u8 rf_pair_response(rf_packet_t *p_rf_rx_data)
{
	static u8 rx_id_same_cnt = 0;



#if PAIR_CHECK_RSSI_ENABLE_AAA//check rssi en
	static u32 rx_rssi = 0;//0
	static u8 rssi_last;//last rssi
	u8 *p_rf_rx_data_start=(u8 *)&p_rf_rx_data->dma_len;//dma len
	u8 rssi_now=GET_RX_RSSI(p_rf_rx_data_start);//get rssi 

	if(!rx_rssi){// 0
		rx_rssi = rssi_now;//update
    }

	if ( abs_aaa(rssi_last - rssi_now) < 17 ){//<17
		rx_rssi = ( ( rx_rssi << 1) + rssi_now ) / 3;//2/3last +1/3now
    }

	rssi_last = rssi_now;//update rssi last
    if(rx_rssi>(50-user_config.rssi_threshold))//>rssi
#endif
    {
        if (check_from_ram(p_rf_rx_data)) //check ram
        {
			rx_id_same_cnt++;//cnt++
			
			if (rx_id_same_cnt>=2)//>=2 time
			{
				
				
				ack_rf_dat.cmd = PAIR_ACK_CMD;//pair
				memcpy((u8 *)&ack_rf_dat.dat[0], (u8*)&dongle_id, 4);
				memcpy((u8 *)&ack_rf_dat.did, (u8*)&p_rf_rx_data->did, ID_LEN);

				user_rf_ack_send(20);

				
				p_post = (void *)set_device_id_in_firmware;//save did in fw

				printf("pair success-----------------\n");//debug pair suc
				return 1;
			}

        } 
		else {
		    rx_id_same_cnt = 0;//id same_cnt
        }
    }

    return 0;
}



/**
 * @brief       This function get next chn
 * @param[in]   chn	- 
 * @param[in]   mask	- 
 * @return      
 * @note        
 */
u8 get_next_channel_with_mask(u32 mask,u8 chn)
{
	//PIN_DEBUG_RF_CHN_NEXT_TOGGLE;//debug
#if (HOPING_FREQ==0)
    //printf("next_channel=%d.\n",(chn+3)%15);
   	return (chn+1)%5;//update chn
#else
	int chn_high = (mask >> 4) & 0x0f;
	if (mask & LL_CHANNEL_SEARCH_FLAG) {
		return LL_NEXT_CHANNEL (chn);
	}
	else if (chn_high != chn) {
		ll_chn_sel = 1;
		return chn_high;
	}
	else {
		ll_chn_sel = 0;
		return mask & 0x0f;
	}
#endif
}


/**
 * @brief       This function poll soft tick
 * @return      
 * @note        
 */
void soft_tick_poll()
{
	static u32 tick;//tick
	static u8 flag=0;//0
	
#if (RF_FAST_TX_SETTING_ENABLE||RF_FAST_RX_SETTING_ENABLE)	//fast en
	check_rf_fast_setting_time();//setting fast time
#endif

	if(clock_time_exceed(tick,1000))//1ms
	{
		tick=clock_time();//update tick

		rx_fail++;//rxfail ++

		if (rx_fail > 18)
		{
			if (rf_pair_enable == 0)
			{ //normal mode
				rx_fail = 14;
			#if (MORE_PIPE_ENABLE == 0)
				set_data_access_code(dongle_id); //normal access code
			#endif
			}
			else
			{ //pair mode
				rx_fail = 17;

				/* auto switch between pair access code and normal access code */
				if (flag)
				{
					flag = 0;
				#if (MORE_PIPE_ENABLE == 0)
					set_pair_access_code(pair_accesscode); //pair access code
				#endif
				}
				else
				{
					flag = 1;
				#if (MORE_PIPE_ENABLE == 0)
					set_data_access_code(dongle_id); //normal access code
				#endif
				}
			}
			
			host_channel = get_next_channel_with_mask(chn_mask, host_channel);//update chn
			//get_next_channel_with_mask(0, 0);
			rf_set_tx_rx_off();//reset  tx rx mode

		#if (RF_FAST_TX_SETTING_ENABLE||RF_FAST_RX_SETTING_ENABLE)//fast en
			if(rf_fast_setting_flag==2)//flag 2
			{
				rf_fast_setting_flag=1;//flag 1
				rf_set_tx_rx_setting();//setting
			#if (MORE_PIPE_ENABLE == 0)
				user_rf_ack_send(1);//rf ack
			#endif
				return ;
			}
			else
			{
				rf_fast_setting_flag=1;//1
				rf_set_tx_rx_setting();//setting
			}
		#endif	

			rf_set_channel_aaa (host_channel, RF_CHN_TABLE);//set chn
			rf_set_rxmode();//rx mode
		}
		
	#if (HOPING_FREQ==1)
		chn_mask = update_channel_mask(chn_mask, host_channel, (u8*)&ll_chn_pkt[0]);
	#endif
	}
}

/**
 * @brief   TIME1 IRQ
 * @param	none
 * @return	none
 */
/*_attribute_ram_code_sec_ void irq_host_timer1 (void)
{

}*/

void app_data_set_report_handle(u8 data_request,u8 report_id,u16 length)
{
	static 	u16 ep0_out_data_len=0;
	static 	u16 ep0_out_index=0;
	if (data_request)//has req
	{
        //printf("app_hid_set_report_handle data_request %d\r\n", data_request);
		if (ep0_out_data_len>8) //>8
		{
			ep0_out_data_len-=8;//-8
			
			for(u8 i=0;i<8;i++)
			{
					app_out_data_buf[ep0_out_index+i]=usbhw_read_ctrl_ep_data();//read data
			}
			ep0_out_index+=8;//index ++
		}
		else
		{
			for(u8 i=0;i<ep0_out_data_len;i++)//last data
			{
					app_out_data_buf[ep0_out_index+i]=usbhw_read_ctrl_ep_data();//read data
			}
			#if(0)
			printf("app_to_device=");
			for(u8 i=0;i<length;i++)
			{
				printf("%x ",app_out_data_buf[i]);
			}
			printf("\r\n");
			#endif
			//u8 app_d[64] = {5,0,0};
			#if(0)
			if(app_len>20)
			{
				u8 pkt_num = (app_len-1)/19;
				u8 pkt_last = app_len -pkt_num*19-1;
				for(u8 i1=0;i1<pkt_num;i1++)
				{
					//memcpy((u8*)&app_d[1],(u8*)&app_out_data_buf[i1*19+1],19);
					memset((u8*)&app_d[1],0,19);
					memcpy((u8*)&app_d[1],(u8*)&app_out_data_buf[i1*19+1],18);
					push_rx_usb_fifo_aaa((u8*)&app_d[0],20);
				}
				if(pkt_last>0)
				{
					memset((u8*)&app_d[1],0,19);
					memcpy((u8*)&app_d[1],(u8*)&app_out_data_buf[pkt_num*19+1],pkt_last);
					push_rx_usb_fifo_aaa((u8*)&app_d[0],20);
				}
			}
			else
			#else
			{
				push_rx_usb_fifo_aaa((u8*)&app_out_data_buf[0],app_out_data_buf[2]+3);
			}
			#endif

		}		
	}
	else
	{
		ep0_out_data_len=length; //datalen length
		ep0_out_index=0;//index 0
		app_len = length;
		printf("set_reprot_cmd=%x,%x\r\n",report_id,length);
	}
}

/**
 * @brief       This function call mouse data
 * @param[in]   buf	- 
 * @return      
 * @note        
 */
void call_mouse_data_handle(u8 *buf)
{
    ms_tick=clock_time();//update ms tick
    mouse_data_t *ms_dat=(mouse_data_t*)&buf[0];//ms data 

    check_first_normal_data();//check first normal data

    if(ms_dat->x||ms_dat->y)//has x or y data
    {
		x_smoother=ms_dat->x;//x smooth
		y_smoother=ms_dat->y;//y smooth
		adaptive_smoother();//adative smother
		ms_dat->x=x_smoother;//update ms x
		ms_dat->y=y_smoother;//update ms y
    }
	if(memcmp(&mouse_last_data.btn,&ms_dat->btn,sizeof(mouse_data_t))||(ms_dat->x!=0)||(ms_dat->y!=0)||(ms_dat->wheel!=0))//new ms data or x y wh no 0
	{
		memcpy(&mouse_last_data.btn, &ms_dat->btn,sizeof(mouse_data_t));//update last ms data
		//push_usb_fifo_aaa(MOUSE_DATA_TYPE,&ms_dat->btn,sizeof(mouse_data_t));//push ms data to usb fifo
		my_fifo_push(&mouse_fifo_t,&ms_dat->btn,sizeof(mouse_data_t));
	}
}


/**
 * @brief       This function push kb data to usb
 * @param[in]   buf	- 
 * @return      
 * @note        
 */
void call_keyboard_data_handle(u8 *buf)
{
	check_first_normal_data();//check first normal data
	kb_data_handle(buf);//update kb data to usb
}

#if (AES_METHOD == 1)
volatile u8  aes_dec[59];
#endif
/**
 * @brief   RF keyboard or mouse data process and ACK
 * @param	p_rf_rx_data: RF received data
 * @param	hw_crc_ok: hardware crc16 check result
 * @return	0:no need ACK, 1:need ACK
 */
_attribute_ram_code_sec_ u8 rf_km_data_repsonse(rf_packet_t *p_rf_rx_data, u8 hw_crc_ok)
{
    u8 device_index = 0;//0 mouse 1 keyboard
	u8 ret=0;//
	#if(1)
	static u16 slave_seq_no[2]={0xffff,0xffff};//
	#else
	static u8 slave_seq_no[2]={0xff,0xff};//
	#endif
	u8 cmd=p_rf_rx_data->cmd;
	
	if (hw_crc_ok)//crc ok
	{
		ret = 1;
	} 

    if (ret == 1)
	{
		ack_rf_dat.seq=p_rf_rx_data->seq;//seq
	    ack_rf_dat.cmd=cmd;
		pc_ack_data_t *p_km_ack_data=(pc_ack_data_t*)&ack_rf_dat.dat[0];
		p_km_ack_data->host_led_status=(host_keyboard_status&0x1f)|((usb_device_status&0x7)<<5);//led status
		
	    if ((p_rf_rx_data->did & 0x03) == 2)
				{
					device_index = 1;//1
				}


			//packet have aes
			//aes decrypt
			//copy cmd pn_no to aes_dec
			//set private_key
			//decryption data
    #if CHECK_DEVICE_ID_ENABLE
	
	    if (key_enc[device_index].did != p_rf_rx_data->did)
		{
	        printf("back\n");
			return 0;
	    }
	#endif
		if(has_pc_data)
		{
	   		ack_rf_dat.cmd |= 0x40;
			int ack_len = p_km_ack_data->pc_dat[1] +3;
			
			user_rf_ack_send(ack_len);
		}
	   else
		{
	   		ack_rf_dat.cmd = (cmd&0x0f);
			
			user_rf_ack_send(1);
		}
#if 0//CHECK_DEVICE_ID_ENABLE
        //if(device_id != p_rf_rx_data->did)
        if (key_enc[device_index].did != p_rf_rx_data->did)
		{
	        printf("back\n");
			return 0;
	    }
#endif

		if(p_rf_rx_data->seq == slave_seq_no[device_index])//seq same
		{
					//printf("---rf err\n");
			return 1;
		}
		if(has_pc_data)
		{
			if((cmd & 0x20)!= 0)
					{
						usb_rx_fifo_aaa.rptr++;//rptr ++
						ack_rf_dat.cmd = (cmd&0x0f); 
						has_pc_data =0;
						printf("receive confirm pc pkt\n");
					}
		}
		slave_seq_no[device_index]=p_rf_rx_data->seq;//update seq no
		if((cmd&0x0f)== MOUSE_CMD)//mouse cmd
		{
			call_mouse_data_handle((u8 *)&p_rf_rx_data->dat[0]);//push to usb
			return 1;
			}
		else if((cmd&0x0f) == KB_CMD)
		{
			call_keyboard_data_handle((u8 *)&p_rf_rx_data->dat[0]);
			return 1;
		}
		else if((cmd&0x0f)== MIC_DATA_CMD)
			{
			static u8 i =0;
			 if(last_mic_index == p_rf_rx_data->dat[0])
			 {
				if(last_mic_index ==0)
				{
					memcpy((u8*)&mic_msbc[2],(u8 *)&p_rf_rx_data->dat[1],29);
					
				}
				else
					{
					memcpy((u8*)&mic_msbc[31],(u8 *)&p_rf_rx_data->dat[1],28);
					push_usb_fifo_aaa(MIC_DATA_TYPE,(u8 *)&mic_msbc[0],63);
					}
				last_mic_index++;
				i=last_mic_index;
				}
			else
			{
				last_mic_index = 0;
				i=0;
			}
			if(i>=2)
			{
				last_mic_index = 0;
				i =0;
			}
			return 1;
		}
		else if((cmd&0x0f)==APP_DATA_CMD)
		{
			#if(0)
			memcpy((u8*)&dev_to_pc_data[0],(u8 *)&p_rf_rx_data->dat[0],7);
			push_usb_fifo_aaa(PC_DATA_TYPE,&dev_to_pc_data[0],7);
			#else
			u8 data_len = p_rf_rx_data->rf_len - HEAD_LENGTH;
			memcpy((u8*)&dev_to_pc_data[0],(u8 *)&p_rf_rx_data->dat[0],data_len);
			if(data_len<PC_APP_LENGTH)
			{
				memset((u8*)&dev_to_pc_data[data_len],0,(PC_APP_LENGTH-data_len));
			}
			push_usb_fifo_aaa(PC_DATA_TYPE,(u8 *)&dev_to_pc_data[0],63);
			#endif
			return 1;
		}
		else if((cmd&0x0f)== EMPTY_CMD)
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
	
	return 0;
}



/**
 * @brief       This function handle rf_rx irq
 * @return      
 * @note        
 */
_attribute_ram_code_ void rf_rx_irq_handle()
{
	u8 need_ack=0;//
	u8 hw_crc_ok=0;//

	raw_pkt = (u8 *) (rf_rx_buff + rf_rx_wptr * PKT_BUFF_SIZE);
	rf_rx_wptr = (rf_rx_wptr + 1) & 1;
	rf_rx_buffer_set((u8*)(rf_rx_buff + rf_rx_wptr * PKT_BUFF_SIZE),PKT_BUFF_SIZE,0);
	rf_packet_t *p_rf_rx_data=(rf_packet_t*)raw_pkt;

    if(RF_TPLL_PACKET_CRC_OK(raw_pkt)&&RF_TPLL_PACKET_LENGTH_OK(raw_pkt))//crc ok and length ok
    {
		PIN_DEBUG_RF_RX_CRC_OK_TOGGLE;//debug rf rx crc ok
		hw_crc_ok=1;
    }

    if(rf_pair_enable)//pair en
    {
		if(hw_crc_ok)
		{
			#if (GOLD_TEST_DONGLE)
			if((p_rf_rx_data->cmd == PAIR_CMD)&& (rf_pair_enable))
			#else
			if(p_rf_rx_data->cmd == PAIR_CMD) 
			#endif
			{
					need_ack = rf_pair_response(p_rf_rx_data);//pair rsp
            }
			else
					need_ack = rf_km_data_repsonse(p_rf_rx_data, hw_crc_ok);//kmdata rsp
		}
    } 
	else 
	{
		need_ack = rf_km_data_repsonse(p_rf_rx_data, hw_crc_ok);//kmdata rsp
	}

	if(need_ack)
	{
		PIN_DEBUG_RF_RX_DATA_OK_TOGGLE;//debug
		
		rx_fail=0;//rx fail 0
	}

    raw_pkt[0] = 1;//rx fail 1
    device_packet_received++;
}



/**
 * @brief       This function get crc 16
 * @param[in]   crc	- 
 * @param[in]   len	- 
 * @param[in]   pD	- 
 * @return      
 * @note        
 */
static unsigned short tsync_crc16(unsigned short crc, unsigned char *pD, int len)
{
	#define	poly			0x8408
	int i,j;
    for(j=len; j>0; j--)
    {
        unsigned int ds = *pD++;
        for(i=0; i<8; i++)
        {
        	int bit = (crc ^ ds ) & 1;
        	crc = crc >> 1;
        	if (bit)
        	{
				crc = crc ^ poly;
			}
			ds = ds >> 1;
        }
    }
	return crc;
}


/**
 * @brief       This function sync access code 16 to 32 bits
 * @param[in]   code	- 
 * @return      
 * @note        
 */
static inline unsigned int tsync_access_code_16to32 (unsigned short code)
{
	unsigned int r = 0;
	for (int i=0; i<16; i+=2) {
		r = r << 4;
		int c = (code >> i) & 3;
		r |= (c<<2) | ((~c) & 3);
	}
	return r;
}



/**
 * @brief       This function custom init
 * @return      
 * @note        
 */
void custom_init()
{
#if HW_IS_FLASH
	/* Read user configuration information from CFG_ADR_MAC */
	flash_read_page (CFG_MAC_ADDR, sizeof(custom_cfg_t), (u8*)&user_config.mac_addr);

	/* If the MAC address is not burned, 4-byte MAC is randomly generated and written to the corresponding address */
	if (user_config.mac_addr == U32_MAX)
    {
        generateRandomNum(4, (u8 *)&user_config.mac_addr);
        flash_write_page(CFG_MAC_ADDR, 4, (u8 *)&user_config.mac_addr);
    }
#else
	otp_set_active_mode();
	/* Read user configuration information from CFG_ADR_MAC */
	otp_read(CFG_MAC_ADDR, sizeof(custom_cfg_t)/4, (u32*)&user_config.mac_addr);
#endif
	printf("---user_cfg.dev_mac = 0x%04x.\r\n",user_config.mac_addr);

	/* Get dongle_id */
	if (user_config.access_code_type != U8_MAX)
	{ //use MAC as dongle_id
		dongle_id = user_config.mac_addr;
	}
	else
	{ //conversion MAC to dongle_id
		int c = tsync_crc16 (0xffff, (u8*)&user_config.mac_addr, 4);
		dongle_id = tsync_access_code_16to32 (c);
	}
	printf("---dongle_id = 0x%04x.\n", dongle_id);
	
	/* Get pair access code, if no burned will be default value */
    if (user_config.rf_id != 0xffff)
    {
		pair_accesscode = rf_access_code_16to32(user_config.rf_id);
    }
	
    /* Get rssi_threshold */
	if (user_config.rssi_threshold == U8_MAX)
	{
		user_config.rssi_threshold = 0;
	}

	/* Get paring limit time */
	if ((user_config.paring_limit_t == 0) || (user_config.paring_limit_t == U8_MAX))
	{
		user_config.paring_limit_t = 60; //unit 1s
	}
	
#if EMI_TEST_FUN_ENABLE_AAA
	/* Get EMI tesr TX power */
	if ((user_config.tx_power_emi == 0) || (user_config.tx_power_emi == U8_MAX))
	{
		user_config.tx_power_emi = DEFAULT_EMI_TX_POWER;
	}
#endif

#if (AES_METHOD==1)
	/* If the AES public key is not burned (that is, full FF), use the default value as the AES public key,
	 * if it is burned (not all FF), use the burned information as the public key */
	for (u8 i = 0; i < 16; i++)
	{
   	    if (user_config.encKey[i] != 0xff)
    	{
    		memcpy(pub_key, &user_config.encKey[0], 16);

    		break;
    	}
	}
#endif

	/* modify internal cap */
	if (user_config.internal_cap != U8_MAX)
	{
		rf_update_internal_cap(user_config.internal_cap);
	}

#if USB_DESCRIPTOR_MY_SELF
	/* set custom config params */
	custom_set_usb_cfg_params(&user_config);
#endif

    AES_KEY tmp_key;
	u8 index = 0;

    for (u8 i = 0; i < PARING_MAX_NUM; i++)
    {
    	/* get device_id */
	#if HW_IS_FLASH
	    flash_read_page(ID_SAVE_STORAGE_ADDR + ID_LEN*i, ID_LEN, (u8*)&tmp_key.did);
	#else
	    otp_read(ID_SAVE_STORAGE_ADDR+ID_LEN*i, ID_LEN/4, (u32*)&tmp_key.did);
	#endif

        if (tmp_key.did == U32_MAX)
        { //invalid
			break;
        }
		else
		{ //valid
			index = (tmp_key.did  & 0x03) - 1; //get device type 0:mouse, 1:keyboard
			memcpy(&key_enc[index].did, &tmp_key.did, ID_LEN); //save device_id to ram
			custom_binding_idx ++; //index plus 1
		}
    }
	
#if HW_IS_FLASH //When the offset exceeds 3000, Store from the start
	if ((ID_LEN * custom_binding_idx) > 3000)
	{
		custom_binding_idx = 0;
		
		flash_erase_sector(ID_SAVE_STORAGE_ADDR);

		if ((key_enc[0].did != U32_MAX) && (key_enc[0].did != 0))
		{ //valid mouse device_id, write to flash
			flash_write_page(ID_SAVE_STORAGE_ADDR + custom_binding_idx * ID_LEN, ID_LEN, (u8*)&key_enc[0].did);
			custom_binding_idx ++;
		}

		if ((key_enc[1].did != U32_MAX) && (key_enc[1].did != 0))
		{ //valid keyboard device_id, write to flash
			flash_write_page(ID_SAVE_STORAGE_ADDR + custom_binding_idx * ID_LEN, ID_LEN, (u8*)&key_enc[1].did);
			custom_binding_idx ++;
		}
	}
#endif

	printf("---custom_binding_idx=%d.\n",custom_binding_idx);
    printf("---mouse_id=0x%04x.\n",key_enc[0].did);
    printf("---kb_id=0x%04x.\n",key_enc[1].did);
}

/**
 * @brief       This function init usb custom 
 * @return      
 * @note        
 */
void usb_custom_init()
{
	//enable USB manual interrupt(in auto interrupt mode,USB device would be USB printer device)
#if MCU_CORE_B80B
	usb_init();//usb init
#else
	usb_init_interrupt();//usb init interrupt
#endif

#if USB_DESCRIPTOR_MY_SELF
	#if (USB_OTA_ENABLE_AAA||D24G_OTA_ENABLE_AAA)  //ota en
		usb_register_set_report(app_hid_set_report_handle);//register app hid set report_handle
	
		write_reg8(0x10e,(1<<USB_EDP_MOUSE)|(1<<USB_EDP_KEYBOARD_IN)|(1<<USB_EDP_SPP_IN));//three ep

		notify_rsp_buf_init();//notify buf init
	#else
		write_reg8(0x10e,(1<<USB_EDP_MOUSE)|(1<<USB_EDP_KEYBOARD_IN));//other only ms kb ep
	#endif
#endif


	//deepsleep_dp_dm_gpio_low_wake_enable(); // TODO: check B80 have not this interface
	usbhw_set_eps_en(FLD_USB_EDP1_EN|FLD_USB_EDP2_EN|FLD_USB_EDP4_EN);

	//enable USB DP pull up 1.5k
	usb_set_pin_en();//dp pull up
	printf("usb cumtom init ok\n");
	
}

/**
 * @brief       This function init timer 1
 * @return      
 * @note        
 */
void timer1_init_aaa()
{
#if (MY_CHANNEL_ENABLE==0)
	timer1_set_mode(TIMER_MODE_SYSCLK,0,8* CLOCK_SYS_CLOCK_1MS);//timer1 set
	timer_start(TIMER1);//start timer 1
#endif
}

#define FW_OFFSET_IN_FLASH      0x0000  //0000


/**
 * @brief       This function user init
 * @return      
 * @note        
 */
void user_init()
{
	/* RF hardware initial */
	rf_user_init();

	/* usb hardware initial */
   	usb_custom_init();
	
   	/* USB pin config */
	pm_set_suspend_power_cfg(PM_POWER_USB, 1);

	/* IRQ enable */
   	irq_enable();

	//power_on_tick = clock_time()|1;
	
#if GOLD_TEST_DONGLE
	rf_pair_enable = 1;
#endif

	if (rf_pair_enable)
	{
		pair_tick = clock_time()|1;
	}
}

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
    0x00,		//0xb5 telinkԶ
    0x22D,		//0xb6 USAGE ZOOM IN
    0x22E,		//0xb7	 USAGE ZOOM OUT 
    0x236,		//0xb8	 USAGE PAN LEFT
    0x237,		//0xb9	 USAGE PAN RIGHT
    0x30B,		//0xba	C_BRIGHT_INC	
    	
    0x30A,		//0xbb	C_BRIGHT_DEC
    0xB8,		//0Xbc	 c_rject
    0x30,		//0Xbd	C_POWER 		
    0x19E,		//0Xbe	C_TERMINAL_LOCK 
};


volatile u8 has_consume_key_press=0;
volatile u8 has_normal_key_press=0;
volatile u8 has_system_key_press=0;


/**
 * @brief       This function wake up pc
 * @return      
 * @note        
 */
void special_key_wake_up_pc()
{
#if 1 
	/* for example  fn conusmer key  wake up window pc
	The disadvantage is that an extra L is sent_ ctrl
	the user decides whether to need this function*/
	if(usb_device_status==USB_DEVICE_CHECK_PC_SLEEP)//pc sleep
    {
		if(need_enter_suspend_flag)//enter suspend flag
		{
		  	u8 x[8]={0,0,0,0,0,0,0,0};
		/*#if 0
			x[0]=1;//l_ctrl
		  	push_usb_fifo_aaa(NORMAL_KB_DATA_TYPE, x, 8);
		  	x[0]=0;
		  	push_usb_fifo_aaa(NORMAL_KB_DATA_TYPE, x, 8);
		#else*/
			x[1]=0;//0
			push_usb_fifo_aaa(MOUSE_DATA_TYPE, x, 6);//push empty mouse pkt to usb
		//#endif
		}
    }
#endif
}



/**
 * @brief       This function update kb data to usb
 * @param[in]   buf	- 
 * @return      
 * @note        
 */
u8 kb_data_handle(u8 *buf)
{
	u16 consume_key = 0;
	u8 system_key = 0;
	u8 normal_key[8] = {0,0,0,0,0,0,0,0};
	u8 nk_offset = 2;
	u8 len = 0;

	/* get valid key nums */
	for (u8 i=0; i<6; i++)
	{
		if (buf[i] != 0)
			len ++; //key num plus 1
		else
			break;
	}

	if (len == 0)//release
	{ //have no valid key
		has_normal_key_press = 0;
		has_consume_key_press = 0;
		has_system_key_press = 0;

		if (need_enter_suspend_tick == 0)
		{
			special_key_wake_up_pc(); //for fn
		}
	}
	else
	{ //have valid key
		//need_enter_suspend_tick = 0;
		
		for(u8 i=0; i<len; i++)
		{
			if ((buf[i] >= 0xa0) && (buf[i] <= 0xa2))
			{ //system_key set
				system_key = 1<<(buf[i]-0xa0);
				has_system_key_press = 1;
			}
			else if ((buf[i] >= 0xa3) && (buf[i] <= 0xbe))
			{ //consume_key set
				consume_key = consumer_list[buf[i]-0xa3];
				has_consume_key_press = 1;
			}
			else if ((buf[i] >= 0xe0) && (buf[i] <= 0xe7))
			{ //normal_key[0] set
				normal_key[0] |= (1 << (buf[i]-0xe0));
				has_normal_key_press = 1;
			}
			else
			{ //normal_key set
				normal_key[nk_offset++] = buf[i];
				has_normal_key_press = 1;
			}
		}
	}

	kb_tick = clock_time();

	if (memcmp(normal_last_data, normal_key, 8))
	{ //new normal key, push to fifo
		memcpy(normal_last_data, normal_key, 8);
		push_usb_fifo_aaa(NORMAL_KB_DATA_TYPE, normal_key, 8);
	}

	if (memcmp(consume_last_data, (u8*)&consume_key, 2))
	{ //new consumer key, push to fifo
		memcpy(consume_last_data, (u8*)&consume_key, 2);
		special_key_wake_up_pc(); //for fn
		push_usb_fifo_aaa(CONSUME_DATA_TYPE, (u8*)&consume_key, 2);
	}

	if (sys_last_data!=system_key)
	{ //new system key, push to fifo
		sys_last_data=system_key;
		special_key_wake_up_pc(); //for fn
		push_usb_fifo_aaa(SYSTEM_DATA_TYPE, &system_key, 1);
	}

	return 1;
}

/**
 * @brief	notify fifo data to host
 * @param	none
 * @return	none
 */
void pull_usb_data()
{
	int success=0;
	u8 *p1 =  my_fifo_get (&usb_fifo_aaa);
	//if(usb_fifo_aaa.wptr!=usb_fifo_aaa.rptr)//have data
	if(p1)
	{
		/* get fifo buffer address */
		//USB_DATA_S *p=(USB_DATA_S*)usb_fifo_aaa.fifo[usb_fifo_aaa.rptr&(USB_FIFO_NUM1-1)];//usb data
		USB_DATA_S *p=(USB_DATA_S*)p1;
		if(p->type==NORMAL_KB_DATA_TYPE)
		{
        //mouse_data_t *now_ms=(mouse_data_t *)&p->buf[0];
			{
				success = usb_keyboard_hid_report_aaa(p->buf); //notify
			}
		}
		#if(0)
		else if(p->type==MOUSE_DATA_TYPE)//ms type
		{ //mouse data
			//if (memcmp_aaa(&mouse_last_data.btn, p->buf,sizeof(mouse_data_t))||(now_ms->x!=0)||(now_ms->y!=0)||(now_ms->wheel!=0))
			{
				//memcpy(&mouse_last_data.btn, p->buf,sizeof(mouse_data_t));
				success = usb_mouse_hid_report_aaa(1, p->buf, sizeof(mouse_data_t)); //notify
			}
			
     		if ((mouse_last_data.btn == 0) && (mouse_last_data.wheel == 0))
			{
				mouse_not_release = 0; //no key
			}
			else
			{
				mouse_not_release = 1; //has key
			}

			//if (memcmp_aaa(normal_last_data, p->buf,8))
				//memcpy(normal_last_data, p->buf,8);
			}
		#endif
		else if (p->type == CONSUME_DATA_TYPE)
		{ //consumer data
			//if (memcmp_aaa(consume_last_data, p->buf,2))
			{
				//memcpy(consume_last_data, p->buf,2);
				success = usb_mouse_hid_report_aaa(2, p->buf, 2); //notify
			}
		}
		else if (p->type == SYSTEM_DATA_TYPE)
		{ //system data
			//if (sys_last_data!=p->buf[0])
			{
				//sys_last_data=p->buf[0];
				success = usb_mouse_hid_report_aaa(3, p->buf, 1); //notify
			}
		}
		else if( p->type== MIC_DATA_TYPE)
		{
				success = usb_app_hid_report(0x0c, p->buf, 63);
		}
		else if(p->type==PC_DATA_TYPE)
		{
			success=usb_app_hid_report(0x0a,p->buf, 63);
		}
		if(success)
		{ //notify success, pop fifo
			//usb_fifo_aaa.rptr++;//rptr ++
			my_fifo_pop(&usb_fifo_aaa);
		}
	}
    
	/* check weather key release because overtime(2s) */
    {
		u8 release_data[8] = {0,0,0,0,0,0,0,0};

		if (mouse_not_release && clock_time_exceed(ms_tick, 2000000))
		{
			if (usb_mouse_hid_report_aaa(1, release_data, 6))
			{
				mouse_not_release = 0; //mouse release
			}
		}
		else if (has_consume_key_press && clock_time_exceed(kb_tick, 2000000))
		{
			if (usb_mouse_hid_report_aaa(2, release_data, 2))
			{
				has_consume_key_press = 0; //consumer release
			}
		}
		else if (has_system_key_press && clock_time_exceed(kb_tick, 2000000))
		{
			if (usb_mouse_hid_report_aaa(3, release_data, 1))
			{
				has_system_key_press = 0; //system release
			}
		}
		
		if (has_normal_key_press && clock_time_exceed(kb_tick, 2000000))
		{
			if (usb_keyboard_hid_report_aaa(release_data))
			{
				has_normal_key_press = 0; //normal release
			}
		}
	}
}

void pull_mouse_data()
{
	int success=0;
	u8 *p1 =  my_fifo_get (&mouse_fifo_t);
	if(p1)
	{
		

		success = usb_mouse_hid_report_aaa(1, p1, sizeof(mouse_data_t)); //notify

			
 		if ((mouse_last_data.btn == 0) && (mouse_last_data.wheel == 0))
		{
			mouse_not_release = 0; //no key
		}
		else
		{
			mouse_not_release = 1; //has key
		}
		if(success)
		{ //notify success, pop fifo
			//usb_fifo_aaa.rptr++;//rptr ++
			my_fifo_pop(&mouse_fifo_t);
		}
	}
	
}


/**
 * @brief       This function check kb led status
 * @return      
 * @note        
 */
static inline void check_kb_led_status()
{
#if USER_DEBUG//for user debug
	static u8 last_led_status=0xff;
	if(last_led_status!=host_keyboard_status)//led status diff
	{
		last_led_status=host_keyboard_status;//update
		gpio_write(D1_LED_PIN, last_led_status&0x01);//show
		gpio_write(D2_LED_PIN, last_led_status&0x02);//show
		gpio_write(D3_LED_PIN, last_led_status&0x04);//show
	}
#endif	
}


/**
 * @brief       This function let mcu suspend
 * @return      
 * @note        
 */
void mcu_enter_suspend()
{
	if(clock_time_exceed (tick_suspend_interval, 40000))//40ms
	{
		u8 r=irq_disable();//irq disble
		
		cpu_sleep_wakeup (SUSPEND_MODE, PM_WAKEUP_TIMER|PM_WAKEUP_CORE_USB, clock_time()+200*CLOCK_16M_SYS_TIMER_CLK_1MS);//suspend 200ms
		
		rf_user_init();//rf user init

		//timer1_init_aaa();

		irq_restore(r);//irq restore
		tick_suspend_interval = clock_time()|1;//update tick suspend tick
		irq_enable();//irq en	   
	}
}


/**
 * @brief       This function check pair time
 * @return      
 * @note        
 */
static inline void check_pair_timeout()
{
	if (pair_tick && clock_time_exceed(pair_tick, user_config.paring_limit_t*1000000))//over pair time
	{
		clear_pair_enable_flag();//clear pair
	}
}


/**
 * @brief       This function check usb host status
 * @return      
 * @note        
 */
void usb_host_status_check(void)
{
	static u8 current_status = 0xff;
	static u8 last_status = 0xff;
	static u32 status_change_tick;
	static u16 status_change_cnt;
	
	if (reg_usb_host_conn & BIT(7))
	{
		if ((reg_irq_src & FLD_IRQ_USB_PWDN_EN))
		{
			if (reg_usb_mdev & BIT(2))
			{
				current_status = USB_DEVICE_CHECK_PC_SLEEP;
			}
			else
			{
				current_status = IDLE;
			}
		} 
		else 
		{
			current_status = USB_DEVICE_CONNECT_PC;
		}
	}
	else
	{
		if ((reg_irq_src & FLD_IRQ_USB_PWDN_EN))
		{
			if (reg_usb_mdev & BIT(2))
			{
				current_status = USB_DEVICE_CHECK_PC_SLEEP;
			}
			else
			{
				current_status = USB_DEVICE_UNPLUG;
			}
		} 
		else 
		{
			current_status = USB_DEVICE_DISCONECT_PC;
		}
	}

	if (last_status != current_status)
	{
		printf("---1 usb_device_status=%d.\r\n", current_status);
		
		last_status = current_status;; //save current status
			
		status_change_tick = clock_time()|1; //mark status change tick

		if (current_status == USB_DEVICE_UNPLUG)
		{
			status_change_cnt = 800;
		}
		else
		{
			status_change_cnt = 200;
		}

		if ((current_status == USB_DEVICE_CHECK_PC_SLEEP) || (current_status == USB_DEVICE_DISCONECT_PC))
		{
			need_enter_suspend_tick = clock_time()|1; //mart enter sleep tick
		}
	}
	else if (status_change_tick && (clock_time_exceed(status_change_tick, status_change_cnt*1000)))
	{ //debounce status 200ms !!!
		status_change_tick = 0; //clear status change tick

		if (usb_device_status != current_status)
		{
			printf("---2 usb_device_status=%d.\r\n", current_status);

			usb_device_status = current_status; //save current status

			/* current status is connect, last status is disconnect, maybe PC has reboot, if has normal key, notify to PC*/
	        if (usb_device_status == USB_DEVICE_CONNECT_PC)
			{
				//led_status_out(host_keyboard_status);

				//allow_send_to_usb_flag = 1;

				if (usb_device_status_last == USB_DEVICE_DISCONECT_PC)
				{
					//usb_io_printf("pc_power_on");
					if (has_normal_key_press)
					{
						push_usb_fifo_aaa(NORMAL_KB_DATA_TYPE, normal_last_data, 8);
					}
				}
				
				need_enter_suspend_flag = 0;//need_enter suspend flag 0
				need_enter_suspend_tick = 0;//need enter suspend tick 0
			}
			else if (usb_device_status == USB_DEVICE_CHECK_PC_SLEEP) //sleep
			{
				need_enter_suspend_flag = 1;//need_enter suspend flag 1
				need_enter_suspend_tick = clock_time()|1;//update need enter suspend tick
				//led_status_out(0);
			}
			else if (usb_device_status == USB_DEVICE_UNPLUG) //unplug
			{
				need_enter_suspend_flag = 0;//need enter_suspend_flag 1
				need_enter_suspend_tick = clock_time()|1;//update need enter suspend tick
				//led_status_out(0);
			}

			usb_device_status_last = usb_device_status; //save host status
		}
	}
	
	if (need_enter_suspend_flag)
	{ //enter suspend
		mcu_enter_suspend();
	}
}


/**
 * @brief       This function is dongle main loop
 * @return      
 * @note        
 */
void main_loop (void)
{
	/* USB IRQ process */
	usb_handle_irq();//must first 

#if USB_OTA_ENABLE_AAA
	/* USB OTA polling */
	usb_update_loop();
#endif

	/* USB status check */
	usb_host_status_check();

#if USER_DEBUG
	test_button();
#endif

#if EMI_TEST_FUN_ENABLE_AAA
	/* USB host cmd process */
	usb_host_cmd_proc();

	if(emi_flg) 
	{
		return;
	}
#endif

#if KM_DATA_HANDLE_CALL_BACK_ENABLE
	if( p_keyboard_data_handle_post)
	{ //keyboard data process
		(*p_keyboard_data_handle_post)((u8 *)to_usb_keyboard_data);	//Run the data handler function
		p_keyboard_data_handle_post = NULL;
	}

	if(p_mouse_data_handle_post)
	{ //mouse data process
		(*p_mouse_data_handle_post)((u8 *)to_usb_mouse_data);	//Run the data handler function
		p_mouse_data_handle_post = NULL;
	}
#endif

	if (p_post)
    { //save device_id to flash or OTP process
        (*p_post)(raw_pkt);
        p_post = NULL;
    }

	/* notify fifo data to host */
	push_pc_data();
	pull_usb_data();
	pull_mouse_data();
	/* check_pair_timeout */
    check_pair_timeout();

    /* RF status poll */
	soft_tick_poll();

	if (need_enter_suspend_tick && clock_time_exceed(need_enter_suspend_tick, 3200000))
	{
		need_enter_suspend_tick = 0;
	}
}
int sm_sum_x, sm_pre_x, sm_sum_y, sm_pre_y;


/**
 * @brief       This function smoother x,y data
 * @return      
 * @note        
 */
void iir_smoother()
{
    sm_sum_x = sm_sum_x - sm_pre_x + x_smoother;//sum -pre +now
    sm_pre_x = sm_sum_x / 2;//pre = sum/2
    x_smoother = sm_pre_x;//pkt is now pre

    sm_sum_y = sm_sum_y - sm_pre_y + y_smoother;//sum -pre +now
    sm_pre_y = sm_sum_y / 2;//pre = sum/2
    y_smoother = sm_pre_y;//pkt is now pre
}


/**
 * @brief       This function clear smoother
 * @return      
 * @note        
 */
static inline void iir_smoother_clear(void)
{
    sm_sum_x = 0;//0
    sm_pre_x = 0;//0

    sm_sum_y = 0;//0
    sm_pre_y = 0;//0
}
#define sm_dyn_pth1  6
#define  sm_dyn_pth2 4

/**
 * @brief       This function adaptive smoother
 * @return      
 * @note        
 */
u8 adaptive_smoother()
{
     static u8 asm_flg = 0;
     static u32 sm_last_smoother_tick = 0;

    //auto clear asm sum when no data for a long time
    if (asm_flg && clock_time_exceed(sm_last_smoother_tick, 100000))//100ms
    {
        asm_flg = 0;//reset asm_flg 0
        iir_smoother_clear();//smooth
    }
    if (!asm_flg)
    {
        if ((abs(x_smoother) > sm_dyn_pth1) || (abs(y_smoother) > sm_dyn_pth1))
        {
            asm_flg = 1;
            iir_smoother();//smoother
        }
        else
        {
            asm_flg = 0;
            iir_smoother_clear();//smooth clear
        }
    }
    else
    {
        if ((abs(x_smoother) < sm_dyn_pth2) && (abs(y_smoother) < sm_dyn_pth2))
        {
            asm_flg = 0;
            iir_smoother_clear();//smooth clear
        }
        else
        {
            asm_flg = 1;
            iir_smoother();//smoother
        }
    }

    if (asm_flg)
    {
        sm_last_smoother_tick = clock_time();//update tick
    }

    return asm_flg;
}

