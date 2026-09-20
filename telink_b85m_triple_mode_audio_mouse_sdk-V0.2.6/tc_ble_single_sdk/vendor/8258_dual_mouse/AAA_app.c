/********************************************************************************************************
 * @file     aaa_app.c
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
//#include "../common/tl_audio.h"



#define  CUST_SAVE_MASTER_INFO_ENABLE_AAA      1
#define LENGHT_USER_BOND_INF  256


#define     MY_APP_ADV_CHANNEL					BLT_ENABLE_ADV_ALL
#define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_20MS
#define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_25MS
#define		USER_OWN_ADDRESS_TYPE				OWN_ADDRESS_RANDOM


#define MOUSE_TIMER_SHORT_T			8000
extern blt_soft_timer_t	blt_timer;
#define RX_FIFO_SIZE	64
#define RX_FIFO_NUM		8

#define TX_FIFO_SIZE	64
#define TX_FIFO_NUM		16

typedef enum
{
    INIT_DONE_AAA,
    BEGIN_CONNECT_AAA,
    SMP_FIRST_CONNECT_DONE_AAA,
    SMP_RECONNECT_DONE_AAA,
    CONNECTED_DONE_AAA,
    CONNECTED_LOOP_AAA,

} BLE_MODE_CONNECT_STEP_AAA;;

#define LONG_SUSPEND_TIMER_AAA 200


#define  DEFAULT_LATENCY  0x10//99

#define PARAMS_GROUP_LEN  4

#define DEFAULT_INTERVAL  6
#define DEFAULT_TIMEOUT  300

#define  REPORT_RATE_100   0  //0 =125~133

#if BLE_AUDIO_ENABLE
    _attribute_data_retention_user	int     ui_mtu_size_exchange_req = 0;
    _attribute_data_retention_user	u8		ui_mic_enable = 0;

    #if AUDIO_AUTO_OPEN_TEST_DEBUG
        _attribute_data_retention_user u8 app_mic_enable = 0;
        _attribute_data_retention_user u32 tick_app_mic = 0;
    #endif
#endif
_attribute_data_retention_user  u8 pair_success=0;


//_attribute_data_retention_user u8 exit_suspend_flag=1;
_attribute_data_retention_user u32 start_tick;

_attribute_data_retention_user u8 Switch_Adv_Type = 0;
//_attribute_data_retention_user u8 bonded_peer_addr[6] = {0, 0, 0, 0, 0, 0};


_attribute_data_retention_user u32 loop_cnt;
_attribute_data_retention_user u32 connect_begin_tick;

_attribute_data_retention_user u8 ui_ota_is_working = 0;
//_attribute_data_retention_user u8 conn_params_pending = 0;
_attribute_data_retention_user u8 conn_params_cout = 0;
_attribute_data_retention_user u32 conn_params_tick;

_attribute_data_retention_user u8 conn_step = 0;
_attribute_data_retention_user u32 adv_begin_tick;
_attribute_data_retention_user u32 adv_count = 0;

//_attribute_data_retention_user u32 connect_begin_tick;

_attribute_data_retention_user u8 ble_status_aaa;
_attribute_data_retention_user u32 idle_tick;
_attribute_data_retention_user u32  idle_count;
_attribute_data_retention_user u8 soft_time_flag = 0;

#if AUTO_CHECK_OS_TYPE
    //_attribute_data_retention_user u8 os_check=0;//0:unknown, 1:ios pending,2:android,3: ios
    _attribute_data_retention_user u8 os_type = 0;
    //_attribute_data_retention_user u8 peer_type=0;
    //_attribute_data_retention_user u16 peer_con_interval=0;
    //_attribute_data_retention_user u16 peer_con_timeout=0;
#endif

_attribute_data_retention_user u8 temp_master_addr[8] = {0};
#if CUST_SAVE_MASTER_INFO_ENABLE_AAA
//_attribute_data_retention_user STRUCT_USER_BOND_INF user_bond_inf[4];//muti device addr
_attribute_data_retention_user _attribute_aligned_(4) smp_param_save_t smp_param_inf[4];//muti device addr

_attribute_data_retention_user int binding_master_addr_idx;
#endif


_attribute_data_retention_  u8 		 	blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_rxfifo =
{
    RX_FIFO_SIZE,
    RX_FIFO_NUM,
    0,
    0,
    blt_rxfifo_b,
};



_attribute_data_retention_  u8 		 	blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_txfifo =
{
    TX_FIFO_SIZE,
    TX_FIFO_NUM,
    0,
    0,
    blt_txfifo_b,
};
_attribute_data_retention_user	my_fifo_t	d24g_txfifo =
{
    TX_FIFO_SIZE,
    TX_FIFO_NUM,
    0,
    0,
    blt_txfifo_b,
};


//#define		MTU_RX_BUFF_SIZE_MAX			ATT_ALLIGN4_DMA_BUFF(23)
//#define		MTU_TX_BUFF_SIZE_MAX			ATT_ALLIGN4_DMA_BUFF(23)

//_attribute_data_retention_ u8 mtu_rx_fifo[MTU_RX_BUFF_SIZE_MAX];
//_attribute_data_retention_ u8 mtu_tx_fifo[MTU_TX_BUFF_SIZE_MAX];


//////////Adv Packet, Response Packet//////////////////////////////////////////////

#if (Microsoft_Swift_Pairing_ENABLE&&(DEVICE_NAME_INCLUDE_MAC_DEBUG==0))
#define DEV_POSITION   20
#define USER_DEVICE_NAME_MAX_LEN  11
_attribute_data_retention_user u8	tbl_advData_aaa[31] =
{

    0x02, 0x01, 0x05, 		// BLE limited discoverable mode and BR/EDR not supported
    0x03, 0x19, DEVICE_APPEARANCE&0XFF, (DEVICE_APPEARANCE>>8)&0xff, // 0xc2 mouse 0xc1 keyboard
    0x03, 0x03, 0x12, 0x18,	// incomplete list of service class UUIDs (0x1812-HID SERVICE, 180F-BATTERY)
    0x06, 0xff, 0x06, 0x00, 0x03, 0x00, 0x80,
    12, 0x09,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#else
#define DEV_POSITION   13
#define USER_DEVICE_NAME_MAX_LEN  18

_attribute_data_retention_user u8	tbl_advData_aaa[31] =
{

    0x02, 0x01, 0x05, 		// BLE limited discoverable mode and BR/EDR not supported
    0x03, 0x19, DEVICE_APPEARANCE&0XFF, (DEVICE_APPEARANCE>>8)&0xff, // 384, Keyboard, Generic category,
    0x03, 0x03, 0x12, 0x18,	// incomplete list of service class UUIDs (0x1812-HID SERVICE, 180F-BATTERY)
    19, 0x09,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

};
#endif
#define SCAN_DEV_POSITION   2
_attribute_data_retention_ u8	tbl_scanData_aaa[31] =
{
	30,9,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,
};

#if (DEVICE_NAME_INCLUDE_MAC_DEBUG)
void BufToHexString(u8 *rp, u8 *sp, u8 len)
{
    u8  hex[17] = "0123456789ABCDEF";
    u8 count = 0;
    u8      i;
    for (i = len; i > 0; i--)
    {
        rp[count++] = hex[sp[i - 1] >> 4];
        rp[count++] = hex[sp[i - 1] & 0x0F];
    }
}
#endif
void set_adv_scanRsp_data()
{

    if (device_name_len == 0)
    {
#if (DEVICE_NAME_INCLUDE_MAC_DEBUG)
        device_name_len = 16;
        memcpy(user_cfg.device_name, "58m_", 4);
        BufToHexString(&user_cfg.device_name[4], &bltMac.macAddress_public[0], 6);
#else
        device_name_len = sizeof(DEVICE_NAME_AAA) - 1;
        if (device_name_len > USER_DEVICE_NAME_MAX_LEN)
        {
            device_name_len = USER_DEVICE_NAME_MAX_LEN;
        }
        memcpy(user_cfg.device_name, DEVICE_NAME_AAA, device_name_len);
#endif
    }
    else
    {
        if (device_name_len > USER_DEVICE_NAME_MAX_LEN)
        {
            device_name_len = USER_DEVICE_NAME_MAX_LEN;
        }
    }
    memset(&user_cfg.device_name[device_name_len], 0, 18 - device_name_len);
#if(0)

    memcpy(&tbl_advData_aaa[DEV_POSITION], user_cfg.device_name, device_name_len);
    tbl_advData_aaa[DEV_POSITION - 2] = device_name_len + 1;
    bls_ll_setAdvData((u8 *)tbl_advData_aaa, device_name_len + DEV_POSITION);
    bls_ll_setScanRspData(&tbl_advData_aaa[DEV_POSITION - 2], device_name_len + 2);
#else
	
	if (pair_flag == 0)
	{
		/* reconnect mode */
	#if 0 //Method 1:, Set the ADV payload to empty to prevent other host from searching for it */
		u8 adv_scan_data[1];
		adv_scan_data[0] = 0;
		bls_ll_setAdvData((u8 *)adv_scan_data, 1);
		bls_ll_setScanRspData((u8 *)adv_scan_data, 1);
	#else //Method 2: (random addr) change the value of BLE_GAP_AD_TYPE_FLAGS from 0x05(Not support BR/EDR and LE limited discovery) to 0x04(Not support BR/EDR)
		if (device_name_len <= USER_DEVICE_NAME_MAX_LEN)
		{
			/* load ble name to adv content */
			memcpy(&tbl_advData_aaa[DEV_POSITION], user_cfg.device_name, device_name_len);
			tbl_advData_aaa[DEV_POSITION - 2] = device_name_len + 1;
			#if (Microsoft_Swift_Pairing_ENABLE)
				u8 adv_scan_data[31];
				memcpy(&adv_scan_data[0], &tbl_advData_aaa[0], DEV_POSITION - 9);
				/* Delete user-defined broadcast content(Microsoft swift pair - 7 bytes)!!! */
				memcpy(&adv_scan_data[DEV_POSITION - 9], &tbl_advData_aaa[DEV_POSITION - 2], device_name_len + 2);
				bls_ll_setAdvData((u8 *)adv_scan_data, device_name_len + DEV_POSITION - 7);
			#else
				bls_ll_setAdvData((u8 *)tbl_advData_aaa, device_name_len + DEV_POSITION);
			#endif
		}
		else
		{
			/* not load ble name to adv content */
			#if (Microsoft_Swift_Pairing_ENABLE)
				/* Delete user-defined broadcast content(Microsoft swift pair - 7 bytes)!!! */
				bls_ll_setAdvData((u8 *)tbl_advData_aaa, DEV_POSITION - 9);
			#else
				bls_ll_setAdvData((u8 *)tbl_advData_aaa, DEV_POSITION - 2);
			#endif
		}

		/* load ble name to scan response content */
		memcpy(&tbl_scanData_aaa[SCAN_DEV_POSITION], user_cfg.device_name, device_name_len);
		tbl_scanData_aaa[SCAN_DEV_POSITION-2] = device_name_len + 1;
		bls_ll_setScanRspData((u8 *)tbl_scanData_aaa, device_name_len + SCAN_DEV_POSITION);
	#endif
	}
	else
	{
		/* pair mode */
		if (device_name_len <= USER_DEVICE_NAME_MAX_LEN)
		{
			/* load ble name to adv content */
			memcpy(&tbl_advData_aaa[DEV_POSITION], user_cfg.device_name, device_name_len); 
			tbl_advData_aaa[DEV_POSITION-2] = device_name_len + 1;
			bls_ll_setAdvData((u8 *)tbl_advData_aaa, device_name_len + DEV_POSITION);
		}
		else
		{
			/* not load ble name to adv content */
			bls_ll_setAdvData((u8 *)tbl_advData_aaa, DEV_POSITION - 2);
		}

		/* load ble name to scan response content */
		memcpy(&tbl_scanData_aaa[SCAN_DEV_POSITION], user_cfg.device_name, device_name_len); 
		tbl_scanData_aaa[SCAN_DEV_POSITION-2] = device_name_len + 1;
		bls_ll_setScanRspData((u8 *)tbl_scanData_aaa, device_name_len + SCAN_DEV_POSITION);
	}

#endif
}
#if (BLE_AUDIO_ENABLE)

void ui_enable_mic(int en)
{
    ui_mic_enable = en;

    //AMIC Bias output
    gpio_set_output_en(GPIO_AMIC_BIAS, en);
    gpio_write(GPIO_AMIC_BIAS, en);
    sleep_us(100);
#if (BLT_APP_LED_ENABLE)
    //device_led_setup(led_cfg[en ? LED_AUDIO_ON : LED_AUDIO_OFF]);
#endif

    if (en)  //audio on
    {
        ///////////////////// AUDIO initialization///////////////////
        //buffer_mic set must before audio_init !!!
        audio_config_mic_buf(buffer_mic, TL_MIC_BUFFER_SIZE);

#if (BLE_DMIC_ENABLE)  //Dmic config

#else  //Amic config
        audio_amic_init(AUDIO_16K);
#endif

    }
    else   //audio off
    {
        adc_power_on_sar_adc(0);   //power off sar adc
    }

#if (BATT_CHECK_ENABLE)
    battery_set_detect_enable(!en);
#endif
}


void user_requestMtuSizeExchange(void)
{

    if (ui_mtu_size_exchange_req && blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
    {
        ui_mtu_size_exchange_req = 0;
        blc_att_requestMtuSizeExchange(BLS_CONN_HANDLE, 0x009e);
    }
}
void task_audio(void)
{
    static u32 audioProcTick = 0;
    if (clock_time_exceed(audioProcTick, 500))
    {
        audioProcTick = clock_time();
    }
    else
    {
        return;
    }

    ///////////////////////////////////////////////////////////////
    // log_event(TR_T_audioTask);


    proc_mic_encoder();

    //////////////////////////////////////////////////////////////////
    if (blc_ll_getTxFifoNumber() < RF_TX_FIFO_ALLOW_NUM)
    {
        int *p = mic_encoder_data_buffer();
        if (p)					//around 3.2 ms @16MHz clock
        {
            log_event(TR_T_audioData);
            if (BLE_SUCCESS == bls_att_pushNotifyData(AUDIO_MIC_INPUT_DP_H, (u8 *)p, ADPCM_PACKET_LEN))
            {
                mic_encoder_data_read_ok();
            }


        }


    }
}



int speckWrite(void *p)
{
#if AUDIO_AUTO_OPEN_TEST_DEBUG

    rf_packet_att_write_t *src = (rf_packet_att_write_t *)p;

    u8 *buf = &src->value;
    u8 len = src->l2capLen - 3;
    app_mic_enable = buf[0];
    tick_app_mic = clock_time() | 1;
#endif
    return 0;
}
#if AUDIO_AUTO_OPEN_TEST_DEBUG

void test_audio_user()
{

    if (tick_app_mic && clock_time_exceed(tick_app_mic, 10000000))
    {
        tick_app_mic = 0;
        ui_enable_mic(1);
    }
}
#endif
#endif

void connect_params_proc()
{
#if 1
    if (bls_ll_getConnectionInterval() <= DEFAULT_INTERVAL)
    {
        conn_params_cout = 0;
    }
    else if ((conn_params_cout < PARAMS_GROUP_LEN) && clock_time_exceed(conn_params_tick, 600000))
    {
    	my_printf_aaa("conn param update--1 \n");
        conn_params_tick = clock_time() | 1;
        bls_l2cap_requestConnParamUpdate(DEFAULT_INTERVAL, DEFAULT_INTERVAL, DEFAULT_LATENCY, DEFAULT_TIMEOUT);
        conn_params_cout++;
    }
#else
    if (bls_ll_getConnectionInterval() <= ConParams[conn_params_cout].Min)
    {
        conn_params_cout = 0;
    }
    else if ((conn_params_cout < PARAMS_GROUP_LEN) && clock_time_exceed(conn_params_tick, 600000))
    {
        conn_params_tick = clock_time() | 1;
        bls_l2cap_requestConnParamUpdate(ConParams[conn_params_cout].Min, ConParams[conn_params_cout].Max, ConParams[conn_params_cout].latency, ConParams[conn_params_cout].timeout);
        conn_params_cout++;
    }
#endif
}

void 	app_switch_to_indirect_adv(u8 e, u8 *p, int n)
{
    Switch_Adv_Type += 1;
	
    if (Switch_Adv_Type & 0x01)
    {

        bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                           ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE,
                           0,  NULL,
                           MY_APP_ADV_CHANNEL,
                           ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);


    }
    else
    {

        bls_ll_setAdvParam(ADV_INTERVAL_3_75MS, ADV_INTERVAL_3_75MS,
                           ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, USER_OWN_ADDRESS_TYPE,
                           BLE_ADDR_PUBLIC, smp_param_inf[flash_dev_info.mast_id].peer_addr,
                           BLT_ENABLE_ADV_ALL,
                           ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);
    }

    bls_ll_setAdvDuration(1000000, 1);

    bls_ll_setAdvEnable(1);  //must: set adv enable
}




_attribute_ram_code_ void	user_set_rf_power(u8 e, u8 *p, int n)
{
    rf_set_power_level_index(user_cfg.tx_power);
    //exit_suspend_flag=1;
}

int cccWrite(void *p)
{
	clear_fifo();
   // conn_step = SMP_DONE_AAA;
    connect_begin_tick = clock_time() | 1;

#if 0//for debug test
    gpio_write(PIN_24G_LED, 1);
#endif

    return 0;
}

u8 pc_no_sleep_status =1;

int PC_enter_sleep(void *p)
{
	rf_packet_att_write_t *pkt_cmd = (rf_packet_att_write_t *)p;

	pc_no_sleep_status = pkt_cmd->value;
	printf("pc wakeup_status is %d\n",pc_no_sleep_status);
    return 0;
}

void	task_connect(u8 e, u8 *p, int n)
{
	//if(pair_flag==0)
	//{
		bls_l2cap_requestConnParamUpdate(6, 6, 0x2c, 300);
	//}

    connect_begin_tick = clock_time() | 1;
    clear_fifo();
	clear_pair_flag();
    reset_idle_status();
    connect_ok = 1;
    ui_ota_is_working = 0;
    //conn_params_pending=0;
    conn_params_cout = 0;
    conn_params_tick = 0;
    conn_step = BEGIN_CONNECT_AAA;
    bls_ll_setAdvDuration(0, 0);
    ble_status_aaa = BEGIN_CONNECTED_STATUS_AAA;




#if APP_24G_AUDIO_EN //audio en
    ui_mtu_size_exchange_req = 1;
#if AUDIO_AUTO_OPEN_TEST_DEBUG

    tick_app_mic = clock_time();
    app_mic_enable = 0;
#endif
#endif

	my_printf_aaa("connect ok\n");


    //rf_packet_connect_t *pc = (rf_packet_connect_t *)(p - 6);
#if UART_PRINT_DEBUG_ENABLE
    // my_printf_aaa("conn req interval=%x,latency=%x,timeout=%x\r\n", pc->interval, pc->latency, pc->timeout);
    //my_printf_aaa("scan_addr=%02X ,%02X%02X%02X%02X%02X%02X\r\n", pc->txAddr, pc->initA[5], pc->initA[4], pc->initA[3], pc->initA[2], pc->initA[1], pc->initA[0]);
    // my_printf_aaa("adv_addr=%02X%02X%02X%02X%02X%02X\r\n", pc->advA[5], pc->advA[4], pc->advA[3], pc->advA[2], pc->advA[1], pc->advA[0]);
#endif

#if AUTO_CHECK_OS_TYPE

    //peer_type=pc->type;
    //peer_con_interval=pc->interval;
    //peer_con_timeout=pc->timeout;

    if (os_type == UNKNOW_OS_TYPE)
    {
        if (pc->txAddr)
        {
            if ((pc->interval >= 0x0c) && (pc->interval <= 0x18) && (pc->timeout <= 0xc8))
            {
                os_type = APPLE_OS_TYPE;
#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("apple_os\r\n");
#endif
            }
            else
            {
                os_type = ANDROID_OS_TYPE;
#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("android_os\r\n");
#endif

            }
        }
        else//public
        {

            // os_type maybe detect errors
            //because  some andriod phone is public for exampe huawei asw=4.4.3
            os_type = WINDOWS_OS_TYPE;
#if UART_PRINT_DEBUG_ENABLE
            my_printf_aaa("window_os\r\n");
#endif
        }
    }
#endif

}



void 	task_terminate(u8 e, u8 *p, int n) //*p is terminate reason
{
#if 1
	bls_ll_setAdvEnable(0);
    my_fifo_reset (&fifo_km);
	my_printf_aaa("task_terminate %x\n", *p);
    if ((ble_status_aaa == DEEP_TERMINATE_STATUS_AAA))
    {

        ble_status_aaa = DEEP_SLEEPE_STATUS_AAA;
    }
	else if(active_disconnect_reason==BLE_PAIR_REBOOT_ANA_AAA)
	{
		save_dev_info_flash();
		user_reboot(BLE_PAIR_REBOOT_ANA_AAA);
	}
	else if(active_disconnect_reason==MODE_CHANGE_REBOOT_ANA_AAA)
    {
        bls_ll_setAdvEnable(0);
        flash_dev_info.mode = RF_2M_2P4G_MODE;
        save_dev_info_flash();
        user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
    }
	else if(active_disconnect_reason==MUTI_DEVICE_REBOOT_ANA_AAA)
	{
		bls_ll_setAdvEnable(0);
		save_dev_info_flash();
		user_reboot(MUTI_DEVICE_REBOOT_ANA_AAA);
	}
	else if(*p == HCI_ERR_REMOTE_DEVICE_TERM_CONN_POWER_OFF)
	{
		bls_ll_setAdvEnable(0);
		ble_status_aaa = DEEP_SLEEPE_STATUS_AAA ;
	}
    else
    {
        ble_status_aaa = POWER_ON_STATUS_AAA;

    }
#else
    bls_ll_setAdvEnable(0);
#endif
    active_disconnect_reason=0;
    conn_step = 0;
    ui_ota_is_working = 0;

    connect_ok = 0;
#if AUTO_CHECK_OS_TYPE

    os_type = UNKNOW_OS_TYPE;
#endif

#if (APP_24G_AUDIO_EN)//audio en
    if (ui_mic_enable)//mic en
    {
        ui_enable_mic(0);
    }
#endif

#if BLT_SOFTWARE_TIMER_ENABLE
    delet_soft_time();
#endif
}


void get_master_real_mac()
{
#if SHOW_MAST_REAL_MAC_DEBUG

    if (temp_master_addr[0] & 0x40) //OWN_ADDRESS_RANDOM;
    {
        smp_param_save_t  bondInfo = {0};
        u32 ret = blc_smp_param_loadByAddr(1, &temp_master_addr[2], &bondInfo);
        if (ret == 0)
        {
            memset(&output_dev_info.master_mac[0], 0, 6);
            return;
        }
        else
        {
            memcpy(&output_dev_info.master_mac[0], &bondInfo.peer_id_addr[0], 6);
        }
    }
    else
    {
        memcpy(&output_dev_info.master_mac[0], &temp_master_addr[2], 6);
    }
#endif
}

void task_gpio_eary_wakeup(u8 e, u8 *p, int n)
{
    if (e == BLT_EV_FLAG_GPIO_EARLY_WAKEUP)
    {
        ui_loop();
    }
}

void  save_smp_inf()
{
    if (pair_success)
    {
		pair_success=0;
		u32 CurStartAddr=0;
		smp_param_save_t smp_param_new;
		extern int blc_smp_param_getCurStartAddr();
		CurStartAddr = blc_smp_param_getCurStartAddr();

		flash_read_page(CurStartAddr+bond_device_flash_cfg_idx, 64, &smp_param_new.flag);
		memcpy(&smp_param_inf[flash_dev_info.mast_id].flag, &smp_param_new.flag, 64);
		binding_master_addr_idx+=LENGHT_USER_BOND_INF;
		flash_write_page_user(CFG_MAST_ADDR + binding_master_addr_idx, LENGHT_USER_BOND_INF, (u8*)&smp_param_inf[0].flag);
#if 0
		printf("peer addr type:%01x--- 0x%01x%01x%01x%01x%01x%01x\n", smp_param_new.peer_addr_type,	smp_param_new.peer_addr[0], smp_param_new.peer_addr[1], smp_param_new.peer_addr[2], smp_param_new.peer_addr[3], smp_param_new.peer_addr[4],smp_param_new.peer_addr[5]);
		printf("peer id addr type:%01x--- 0x%01x%01x%01x%01%01x%01x\n", smp_param_new.peer_id_adrType, smp_param_new.peer_id_addr[0], smp_param_new.peer_id_addr[1], smp_param_new.peer_id_addr[2],smp_param_new.peer_id_addr[3], smp_param_new.peer_id_addr[4],smp_param_new.peer_id_addr[5]);
#endif
		if (deep_flag == BLE_PAIR_REBOOT_ANA_AAA)
	   {
		   write_deep_ana0(CLEAR_FLAG_ANA_AAA);
		   flash_dev_info.slave_mac_addr[flash_dev_info.mast_id]++;
		   save_dev_info_flash();
	   }
	}
}



_attribute_ram_code_ void  ble_remote_set_sleep_wakeup(u8 e, u8 *p, int n)
{
#if 1
    if (suspend_wake_up_enable && (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) && (((u32)(bls_pm_getSystemWakeupTick() - clock_time())) > 20 * CLOCK_16M_SYS_TIMER_CLK_1MS)) //suspend time > 30ms.add gpio wakeup
    {
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);  //gpio pad wakeup suspend/deepsleep
#if WHEEL_FUN_ENABLE_AAA
        wheel_set_wakeup_level_suspend(1);
#endif
#if BUTTON_FUN_ENABLE_AAA
        btn_set_wakeup_level_suspend(1);
#endif
#if SENSOR_FUN_ENABLE_AAA
        sensor_set_wakeup_level_suspend(1);
#endif
    }
#endif
}



void app_enter_ota_mode(void)
{
    ui_ota_is_working = 1;
#if (BLT_APP_LED_ENABLE && BLE_OTA_LED_DEBUG)
    gpio_write(PIN_BLE_LED, 1);
#endif
#if UART_PRINT_DEBUG_ENABLE

    my_printf_aaa("start ota\r\n");
#endif
    bls_ota_setTimeout(160 * 1000 * 1000); //set OTA timeout  15 seconds
}
void app_debug_ota_result(int result)
{
    //irq_disable();
    //flash_erase_sector(CFG_DEVICE_MODE_ADDR);
    #if MODULE_WATCHDOG_ENABLE
    wd_stop();
	#endif
#if(BLE_OTA_LED_DEBUG && BLE_OTA_SERVER_ENABLE)
    gpio_set_output_en(PIN_BLE_LED, 1);
    if (result == OTA_SUCCESS)  //OTA success
    {
        gpio_write(PIN_BLE_LED, LED_ON_AAA);
        sleep_us(2000000);  //led on for 2 second
        gpio_write(PIN_BLE_LED, LED_OFF_AAA);
#if UART_PRINT_DEBUG_ENABLE

        my_printf_aaa("ota success\r\n");
#endif
    }
    else   //OTA fail
    {
      #if(1)
        for (int i = 0; i < 2; i++)
        {
            gpio_write(PIN_BLE_LED, LED_ON_AAA);
            sleep_us(250000);
            gpio_write(PIN_BLE_LED, LED_OFF_AAA);
            sleep_us(250000);
        }
	 #endif
#if UART_PRINT_DEBUG_ENABLE
        my_printf_aaa("ota fail=%x\r\n",result);
#endif
    }
#endif
}

int app_host_event_callback(u32 h, u8 *para, int n)
{
    u8 event = h & 0xFF;

    switch (event)
    {
        case GAP_EVT_SMP_PARING_FAIL:
        {
            gap_smp_pairingFailEvt_t *p = (gap_smp_pairingFailEvt_t *)para;

            if (p->reason == PARING_FAIL_REASON_UNSPECIFIED_REASON)
            {
                bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN); //push terminate cmd into ble TX buffer
            }
        }
        break;
        case GAP_EVT_SMP_PARING_SUCCESS:
        {
            gap_smp_pairingSuccessEvt_t *p = (gap_smp_pairingSuccessEvt_t *)para;
            if (p->bonding_result)
            {
			 	pair_success=1;
				#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("pair success\r\n");
                //blc_att_requestMtuSizeExchange(BLS_CONN_HANDLE, 0x009e);
                //blc_att_requestMtuSizeExchange(BLS_CONN_HANDLE, 0x00de);
                #endif
			 }
        }
        break;

        case GAP_EVT_SMP_CONN_ENCRYPTION_DONE:
        {

            gap_smp_connEncDoneEvt_t *p = (gap_smp_connEncDoneEvt_t *)para;
            if (p->re_connect == SMP_STANDARD_PAIR)  //first paring
            {
       			conn_step = SMP_FIRST_CONNECT_DONE_AAA;

                    //bls_smp_enableParing (SMP_PARING_CONN_TRRIGER);
#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("first connected\r\n");
#endif
            }
            else if (p->re_connect == SMP_FAST_CONNECT)  //auto connect
            {
                my_fifo_reset(&fifo_km);
                conn_step = SMP_RECONNECT_DONE_AAA;
#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("reconnected\r\n");
#endif
            }
            connect_begin_tick = clock_time() | 1;
        }
        break;
        case GAP_EVT_ATT_EXCHANGE_MTU:
        {
#if 1
            gap_gatt_mtuSizeExchangeEvt_t *p = (gap_gatt_mtuSizeExchangeEvt_t *)para;
            my_printf_aaa("peer_MTU %d,effetive_mtu %d\r\n", p->peer_MTU,p->effective_MTU);
            if (p->peer_MTU >= 0x87)
            {
                //apple_need_quick_send_sec_req=1;
            }
#endif
        }
        break;
        default:
            break;
    }

    return 0;
}

void change_ble_stack_smp_inf()
{
	smp_param_save_t tmp[4];
	u8* src=&smp_param_inf[0].flag;
	//u8 *last=&tmp[0].flag;
	
	binding_master_addr_idx = flash_info_load_aaa(CFG_MAST_ADDR, src, LENGHT_USER_BOND_INF);
	if(binding_master_addr_idx<0)
	{
		set_pair_flag();
		printf("no pair information so enter pair\n");
		return;
	}
#if(0)
	flash_read_page(SMP_PARAM_NV_ADDR_START, 256, last);
	for(u8 i=0;i<8;i++)
	{
		if(memcmp(last,src, 256))
		{
			flash_erase_sector(SMP_PARAM_NV_ADDR_START);
			flash_write_page(SMP_PARAM_NV_ADDR_START, 256, src);
			flash_read_page(SMP_PARAM_NV_ADDR_START, 256, last);
		}
		else
		{
			break;
		}
	}
#endif
}
void app_save_smp_to_flash(smp_param_save_t* smp_param)
{
	memcpy((u8*)&smp_param_inf[flash_dev_info.mast_id].flag, &smp_param->flag, sizeof(smp_param_save_t));

	binding_master_addr_idx += LENGHT_USER_BOND_INF;

	flash_write_page_user(CFG_MAST_ADDR + binding_master_addr_idx, LENGHT_USER_BOND_INF, (u8*)&smp_param_inf[0].flag);
	
#if 0
	printf("peer addr type:%01x--- 0x%01x%01x%01x%01x%01x%01x\n", smp_param->peer_addr_type,	smp_param->peer_addr[0], smp_param->peer_addr[1], smp_param->peer_addr[2], smp_param->peer_addr[3], smp_param->peer_addr[4], smp_param->peer_addr[5]);
	printf("peer id addr type:%01x--- 0x%01x%01x%01x%01x%01x%01x\n", smp_param->peer_id_adrType, smp_param->peer_id_addr[0], smp_param->peer_id_addr[1], smp_param->peer_id_addr[2], smp_param->peer_id_addr[3], smp_param->peer_id_addr[4], smp_param->peer_id_addr[5]);
#endif

	if (deep_flag == BLE_PAIR_REBOOT_ANA_AAA)
	{
		write_deep_ana0(CLEAR_FLAG_ANA_AAA);
		flash_dev_info.slave_mac_addr[flash_dev_info.mast_id]++;
		save_dev_info_flash();
	}
}

u32 app_smp_info_custom_save(u16 connHandle, u32 current_addr, smp_param_save_t* smp_param)
{
	//(void)current_addr;
	
	//smp_write_flash_page(CUSTOM_SMP_INFO_ADDR, sizeof(smp_param_save_t), (u8*)smp_param);

	app_save_smp_to_flash(smp_param);
	
	return 0;
}

void app_smp_info_custom_load(u16 connHandle)
{
	u32 custom_bond_addr[1];

	custom_bond_addr[0] = CFG_MAST_ADDR + binding_master_addr_idx + flash_dev_info.mast_id * sizeof(smp_param_save_t);

	extern ble_sts_t blc_smp_setCustomBondingInfoAddress(u8 cur_bondNum, u32* bond_flash_idx);
	blc_smp_setCustomBondingInfoAddress(1, custom_bond_addr);
}

void user_init_normal(void)
{
    blt_txfifo.wptr = 0;
    blt_txfifo.rptr = 0;
    blt_rxfifo.wptr = 0;
    blt_rxfifo.rptr = 0;
    //random_generator_init();  //this is must
    //ble_show_mode();

        ////////////////// BLE stack initialization ////////////////////////////////////
#if BLE_SNIFF_DEBUG
    u8  tbl_mac [6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xc7};
    extern 	u8	blc_adv_channel[];
    //_attribute_data_retention_	u8		blc_adv_channel[3] = {37, 38, 39};
    blc_adv_channel[0] = 37;
    blc_adv_channel[1] = 38;
    blc_adv_channel[2] = 39;
    memcpy(tbl_mac, (u8 *)&user_cfg.dev_mac, 6);
    tbl_mac[5] = 0XD0 | (flash_dev_info.mast_id + 1);
#else
    u8  tbl_mac [] = {0x12, 0x34, 0x56, 0x78, 0x11, 0x54};
    memcpy(tbl_mac, (u8 *)&user_cfg.dev_mac, 6);

    if (pair_flag)
    {
        tbl_mac[4] = flash_dev_info.slave_mac_addr[flash_dev_info.mast_id] + 1;
    }
    else
    {
        tbl_mac[4] = flash_dev_info.slave_mac_addr[flash_dev_info.mast_id];
    }


    tbl_mac[5] = 0XD0 | (flash_dev_info.mast_id + 1);
    printf("---BLE MAC:0x%01x%01x%01x%01x%01x%01x.\n",tbl_mac[5],tbl_mac[4],tbl_mac[3],tbl_mac[2],tbl_mac[1],tbl_mac[0]);


#endif

        //change_ble_stack_smp_inf();

        blc_ll_setRandomAddr(tbl_mac);
    
        ////// Controller Initialization  //////////
        blc_ll_initBasicMCU();                      //mandatory
        blc_ll_initStandby_module(tbl_mac);             //mandatory
        blc_ll_initAdvertising_module(tbl_mac);     //adv module:        mandatory for BLE slave,
        blc_ll_initConnection_module();             //connection module  mandatory for BLE slave/master
        blc_ll_initSlaveRole_module();              //slave module:      mandatory for BLE slave,
        blc_ll_initPowerManagement_module();        //pm module:         optional
    
        ////// Host Initialization  //////////
        blc_gap_peripheral_init();    //gap initialization
        extern void my_att_init();
        my_att_init();  //gatt initialization
        blc_att_setRxMtuSize(MTU_SIZE_SETTING);
        blc_l2cap_register_handler(blc_l2cap_packet_receive);   //l2cap initialization
    
        //// smp initialization ////
#if (APP_SECURITY_ENABLE)
			//bls_smp_configpairingSecurityInfoStorageAddr(FLASH_ADR_SMP_PAIRING);
	#if CUST_SAVE_MASTER_INFO_ENABLE_AAA
				change_ble_stack_smp_inf();
	#endif
			blc_smp_param_setBondingDeviceMaxNumber(4); 	//default is 4, can not bigger than this value
															//and this func must call before bls_smp_enableParing
			blc_smp_peripheral_init ();
			blc_smp_setSecurityLevel(Unauthenticated_Pairing_with_Encryption);
#else
			blc_smp_setSecurityLevel(No_Security);
#endif
	blc_smp_configSecurityRequestSending(SecReq_NOT_SEND, SecReq_NOT_SEND, 1000); //if not set, default is:  send "security request" immediately after link layer connection established(regardless of new connection or reconnection )
	
	//extern ble_sts_t   blc_smp_initCustomBondingInfoEnable(u8 enable, smp_info_custom_save_callback_t custom_save_cb, smp_info_custom_load_callback_t custom_load_cb);
	//blc_smp_initCustomBondingInfoEnable(1, &app_smp_info_custom_save, &app_smp_info_custom_load);
	
	extern ble_sts_t   blc_smp_initCustomBondingInfoEnable(u8 enable, smp_info_custom_save_callback_t custom_save_cb, smp_info_custom_load_callback_t custom_load_cb);
    blc_smp_initCustomBondingInfoEnable(1, &app_smp_info_custom_save, &app_smp_info_custom_load);
	blc_gap_registerHostEventHandler(app_host_event_callback);
	blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_FAIL		|  \
							 GAP_EVT_MASK_SMP_PAIRING_SUCCESS	|  \
							 GAP_EVT_MASK_SMP_CONN_ENCRYPTION_DONE|GAP_EVT_ATT_EXCHANGE_MTU);


    //HID_service_on_android7p0_init();  //hid device on android 7.0/7.1
    //blc_att_setServerDataPendingTime_upon_ClientCmd(50);


    //blc_l2cap_registerConnUpdateRspCb(app_conn_param_update_response);



    ///////////////////// USER application initialization ///////////////////
    set_adv_scanRsp_data();

    //set rf power index, user must set it after every suspend wakeup, cause relative setting will be reset in suspend
    user_set_rf_power(0, 0, 0);
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);



    //ble event call back
    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &task_terminate);
    bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_indirect_adv);


    //bls_app_registerEventCallback(BLT_EV_FLAG_CONN_PARA_REQ, &task_conn_update_req);
    //bls_app_registerEventCallback(BLT_EV_FLAG_CONN_PARA_UPDATE, &task_conn_update_done);

    //bls_app_registerEventCallback (BLT_EV_FLAG_ENCRYPTION_CONN_DONE, &task_encry_done);		//¼????ê³?
    //bls_app_registerEventCallback (BLT_EV_FLAG_PAIRING_END, &task_smp_pair_end);
    bls_app_registerEventCallback(BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &task_gpio_eary_wakeup);
	if(pair_flag ==1)
		{
    		blc_att_setServerDataPendingTime_upon_ClientCmd(10);
		}
	else
		{
			blc_att_setServerDataPendingTime_upon_ClientCmd(1);
		}

    ///////////////////// Power Management initialization///////////////////
#if(BLE_APP_PM_ENABLE)
    blc_ll_initPowerManagement_module();

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
    bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
    blc_pm_setDeepsleepRetentionThreshold(95, 95);
    blc_pm_setDeepsleepRetentionEarlyWakeupTiming(400);
    blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);
#else
    bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#endif

    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &ble_remote_set_sleep_wakeup);
#else
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif

#if (BLE_OTA_SERVER_ENABLE)
		////////////////// OTA relative ////////////////////////
	#if (UART_PRINT_DEBUG_ENABLE)
			blc_debug_addStackLog(STK_LOG_OTA_FLOW);
	#endif
		blc_ota_initOtaServer_module();

		//blc_ota_setOtaProcessTimeout(30);   //OTA process timeout:  30 seconds
		//blc_ota_setOtaDataPacketTimeout(4);	//OTA data packet timeout:  4 seconds
		blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
		blc_ota_registerOtaResultIndicationCb(app_debug_ota_result);
#endif



#if BLT_SOFTWARE_TIMER_ENABLE
    blt_soft_timer_init();
#endif
    start_tick = clock_time();
}




_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)

    blc_ll_initBasicMCU();   //mandatory
    rf_set_power_level_index(user_cfg.tx_power);

    blc_ll_recoverDeepRetention();

    DBG_CHN0_HIGH;    //debug
    irq_enable();

    //pm mask disable,in main_loop according to ui task will reset pm mask
    bls_pm_setSuspendMask(SUSPEND_DISABLE);

    //app_ui_init_deepRetn();
#endif
}

void enter_deep_aaa()
{
	printf("enter deep\n");
#if (!TEST_LOST_RATE)
    write_deep_ana0(DEEP_SLEEP_ANA_AAA);
    analog_write(USED_DEEP_ANA_REG1, (fun_mode << 1));

#if BUTTON_FUN_ENABLE_AAA
    btn_set_wakeup_level_deep();
#endif

#if WHEEL_FUN_ENABLE_AAA
    wheel_set_wakeup_level_deep();
#endif
#if SENSOR_FUN_ENABLE_AAA
#if (SENSOR_SHUT_DOWN_ENABLE==0)

    sensor_set_wakeup_level_deepsleep(1);
#endif
    OPTSensor_Shutdown();

#endif

    clear_pair_flag();
    //ble_status_aaa=0;


    cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);

#endif
}

void set_adv_type()
{
	u32 *adr=(u32*)&smp_param_inf[flash_dev_info.mast_id].peer_addr;
	ll_whiteList_reset(); 	  //clear whitelist
	ll_resolvingList_reset(); //clear resolving list
	tbl_advData_aaa[2] = 0x05;
    if ((binding_master_addr_idx >= 0) && (pair_flag == 0)) //at least 1 bonding device exist
    {
		if ((adr[0] == 0)||(adr[0]==U32_MAX)) 
		{ //check whether this mast_id has been bonded
			my_printf_aaa("channel %d not bonded\n", flash_dev_info.mast_id);//not bonded

			set_pair_flag();
			bls_ll_setAdvDuration(0, 0);
	        bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
	                           ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE,
	                           0,  NULL,
	                           MY_APP_ADV_CHANNEL,
	                           ADV_FP_NONE);
			
		} 
		else if(smp_param_inf[flash_dev_info.mast_id].peer_addr_type == 0) 
		{ //public addr
			//memcpy(bonded_peer_addr, (u8 *)&user_bond_inf[flash_dev_info.mast_id].con_bt_addr[0], 6);
			my_printf_aaa("channel %d bonded a public addr, direct\n", flash_dev_info.mast_id);
			#if(0)
			ll_whiteList_reset(); 	  //clear whitelist
			ll_resolvingList_reset(); //clear resolving list
			if( IS_RESOLVABLE_PRIVATE_ADDR(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr) )
			{
				ll_resolvingList_add(0, smp_param_inf[flash_dev_info.mast_id].peer_id_addr, smp_param_inf[flash_dev_info.mast_id].peer_irk, NULL);  //no local IRK
				ll_resolvingList_setAddrResolutionEnable(1);
				my_printf_aaa("random addr wL is resolvable priver\n");
        	}
			else
			{
				//if not resolvable random address, add peer address to whitelist
				ll_whiteList_add(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr);
				my_printf_aaa("random addr wL is not resolvable priver\n");
			}
			#else
			if(blc_app_isIrkValid(smp_param_inf[flash_dev_info.mast_id].peer_irk))
			{
				/* peer device may use resolvable private address, should add peer irk to resolving list */
				blc_ll_addDeviceToResolvingList(smp_param_inf[flash_dev_info.mast_id].peer_id_adrType, smp_param_inf[flash_dev_info.mast_id].peer_id_addr, smp_param_inf[flash_dev_info.mast_id].peer_irk, NULL);
				blc_ll_setAddressResolutionEnable(1);
				printf("random addr wL is resolvable priver\n");
			}

			if( IS_RESOLVABLE_PRIVATE_ADDR(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr) ){
				/* if resolvable random address, add peer identity address to WhiteList */
				blc_ll_addDeviceToWhiteList(smp_param_inf[flash_dev_info.mast_id].peer_id_adrType, smp_param_inf[flash_dev_info.mast_id].peer_id_addr);
			}
			else{
				/* if not resolvable random address, add peer air packet address to WhiteList */
				blc_ll_addDeviceToWhiteList(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr);
				printf("random addr wL is not resolvable priver\n");
			}
			#endif
            bls_ll_setAdvParam(ADV_INTERVAL_3_75MS, ADV_INTERVAL_3_75MS,
                               ADV_TYPE_CONNECTABLE_DIRECTED_HIGH_DUTY, USER_OWN_ADDRESS_TYPE,
                               BLE_ADDR_PUBLIC, smp_param_inf[flash_dev_info.mast_id].peer_addr,
                               BLT_ENABLE_ADV_ALL,
                               ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);
            bls_ll_setAdvDuration(2000000, DIRECT_ADV_TO_UNDIRECT_ENABLE);

		} 
		else if (smp_param_inf[flash_dev_info.mast_id].peer_addr_type == 1) 
		{//random addr
			my_printf_aaa("channel %d bonded a random addr, undirect\n", flash_dev_info.mast_id);
			#if(0)
			ll_whiteList_reset(); 	  //clear whitelist
			ll_resolvingList_reset(); //clear resolving list
			if( IS_RESOLVABLE_PRIVATE_ADDR(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr) )
			{
				ll_resolvingList_add(0, smp_param_inf[flash_dev_info.mast_id].peer_id_addr, smp_param_inf[flash_dev_info.mast_id].peer_irk, NULL);  //no local IRK
				ll_resolvingList_setAddrResolutionEnable(1);
				my_printf_aaa("random addr wL is resolvable priver\n");
        	}
			else
			{
				//if not resolvable random address, add peer address to whitelist
				ll_whiteList_add(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr);
				my_printf_aaa("random addr wL is not resolvable priver\n");
			}
			#else
			if(blc_app_isIrkValid(smp_param_inf[flash_dev_info.mast_id].peer_irk)){
			/* peer device may use resolvable private address, should add peer irk to resolving list */
			blc_ll_addDeviceToResolvingList(smp_param_inf[flash_dev_info.mast_id].peer_id_adrType, smp_param_inf[flash_dev_info.mast_id].peer_id_addr, smp_param_inf[flash_dev_info.mast_id].peer_irk, NULL);
			blc_ll_setAddressResolutionEnable(1);
			printf("phone has bond so add whitelist\n");
			}

			if( IS_RESOLVABLE_PRIVATE_ADDR(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr) ){
				/* if resolvable random address, add peer identity address to WhiteList */
				blc_ll_addDeviceToWhiteList(smp_param_inf[flash_dev_info.mast_id].peer_id_adrType, smp_param_inf[flash_dev_info.mast_id].peer_id_addr);
			}
			else{
				/* if not resolvable random address, add peer air packet address to WhiteList */
				blc_ll_addDeviceToWhiteList(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr);
			}
			#endif
            tbl_advData_aaa[2] = 0x04;
            bls_ll_setAdvDuration(0, 0);
			bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX, \
							ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE, \
							0,  NULL, MY_APP_ADV_CHANNEL, ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);

        }

    }
    else
    {
		my_printf_aaa("normal pairing adv channel %d\n", flash_dev_info.mast_id);
        bls_ll_setAdvDuration(0, 0);
        bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                           ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE,
                           0,  NULL,
                           MY_APP_ADV_CHANNEL,
                           ADV_FP_NONE);
    }
}


void ble_status_proc_aaa(u8 is_new_key_event)
{
    if (ble_status_aaa == T5S_CONNECTED_STATUS_AAA)
    {
#if (BLE_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA && (!BLE_SNIFF_DEBUG))
        if ((idle_count >= BLE_CONNECT_TIME_OUT))
        {

            bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);
            ble_status_aaa = DEEP_TERMINATE_STATUS_AAA;

        }
#endif

       // connect_params_proc();
#if (BLE_AUDIO_ENABLE&&AUDIO_AUTO_OPEN_TEST_DEBUG)
        test_audio_user();
#endif
    }
    else if (ble_status_aaa == POWER_ON_STATUS_AAA)
    {
        //bls_smp_enableParing (SMP_PARING_DISABLE_TRRIGER);

        bls_ll_setAdvEnable(0);
#if (BLE_SNIFF_DEBUG)
        bls_ll_setAdvDuration(0, 0);
        bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                           ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE,
                           0,  NULL,
                           MY_APP_ADV_CHANNEL,
                           ADV_FP_NONE);
#else
        Switch_Adv_Type = 0;
        set_adv_type();
#endif

        set_adv_scanRsp_data();
        bls_ll_setAdvEnable(1);  //adv enable
        // reset_idle_status();
        adv_begin_tick = clock_time() | 1;
        adv_count = 0;
        ble_status_aaa = LOW_ADV_STATUS_AAA;

    }
    else if (ble_status_aaa == LOW_ADV_STATUS_AAA)
    {
#if (BLE_ADV_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA&&(!BLE_SNIFF_DEBUG))
        adv_count_poll();

        if (pair_flag)
        {
            if (adv_count >= BLE_ADV_TIMER_OUT)
            {
                ble_status_aaa = DEEP_SLEEPE_STATUS_AAA;
            }
        }
        else if (adv_count >= 20)
        {
            ble_status_aaa = DEEP_SLEEPE_STATUS_AAA;
        }
#endif
#if BLT_APP_LED_ENABLE
        led_ble_Adv_poll();
#endif
#if 0
        if (is_new_key_event && (pair_flag == 0))
        {
            ble_status_aaa = POWER_ON_STATUS_AAA;
        }
#endif
    }
    else if (ble_status_aaa == BEGIN_CONNECTED_STATUS_AAA)
    {
        if (conn_step == BEGIN_CONNECT_AAA)
        {
            //clear_fifo();
            if (clock_time_exceed(connect_begin_tick, 40000000))
            {
                ble_status_aaa = OK_CONNECTED_STATUS_AAA;
            }
            }
		else if(conn_step == SMP_FIRST_CONNECT_DONE_AAA)
		{
			if(pair_success)
			{
				ble_status_aaa = OK_CONNECTED_STATUS_AAA;
				//save_smp_inf();
				pair_success = 0;
				clear_pair_flag();
            }
		}
        else if (conn_step == SMP_RECONNECT_DONE_AAA)
        {
            ble_status_aaa = OK_CONNECTED_STATUS_AAA;
        }
#if BLT_APP_LED_ENABLE
        led_ble_Adv_poll();
#endif
    }
    else if (ble_status_aaa == OK_CONNECTED_STATUS_AAA)
    {
    // bls_l2cap_requestConnParamUpdate(6, 9, 0x2c, 300);
        conn_step = CONNECTED_DONE_AAA;
        conn_params_tick = clock_time() | 1;
        ble_status_aaa = T5S_CONNECTED_STATUS_AAA;
#if SHOW_MAST_REAL_MAC_DEBUG
        get_master_real_mac();
#endif
#if BLT_APP_LED_ENABLE
        led_ble_ConnectedStatus();
#endif
    }
    else if (ble_status_aaa == DEEP_SLEEPE_STATUS_AAA)
    {
        if ((ui_ota_is_working == 0) && (!blc_ll_isControllerEventPending()))
        {
            enter_deep_aaa();
        }

    }

}
void reset_idle_status()
{
    if (pair_flag)
    {
        return;
    }
    idle_count = 0;
    loop_cnt = 0;
    idle_tick = clock_time();
    //adv_begin_tick = idle_tick | 1;
    //adv_count = 0;
}
void idle_status_poll()
{
    u32 n;
    n = ((u32)(clock_time() - idle_tick)) / CLOCK_16M_SYS_TIMER_CLK_1S;

    idle_tick += n * CLOCK_16M_SYS_TIMER_CLK_1S;

    idle_count += n;
#if 0
    if (idle_count > 0xfffffff0)
    {
        idle_count = 0xfffffff0;
    }
#endif
}
void adv_count_poll()
{
    u8 n;
    n = ((u32)(clock_time() - adv_begin_tick)) / CLOCK_16M_SYS_TIMER_CLK_1S;

    adv_begin_tick += n * CLOCK_16M_SYS_TIMER_CLK_1S;

    adv_count += n;
}

void ui_loop()
{
    //if(exit_suspend_flag)
    {
        //exit_suspend_flag=0;



        get_ble_data_report_aaa();
        loop_cnt++;
        if (has_new_key_event)
        {
            reset_idle_status();
        }
		idle_status_poll();
		ble_status_proc_aaa(has_new_key_event);
		if(ble_status_aaa>=OK_CONNECTED_STATUS_AAA)
		{
			 connect_params_proc();
		}

        if (connect_ok && (ble_status_aaa==T5S_CONNECTED_STATUS_AAA))
        {
            ble_notify_data_proc_aaa();
        }
        else
        {
            ms_data.wheel = 0;
        }
#if BLT_APP_LED_ENABLE

        device_led_process();
#endif
    }



}
void ble_pm_aaa()
{
#if(APP_24G_AUDIO_EN)
    if (ui_mic_enable)
    {
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        return;
    }
#endif

#if(BLT_SOFTWARE_TIMER_ENABLE==0)
    if (soft_time_flag)
    {
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        return;
    }
#endif
#if BLE_APP_PM_ENABLE
#if BLT_APP_LED_ENABLE

    if (0 || btn_value  || ble_hw_led.repeatCount||ble_hw_led.repeatCount || ui_ota_is_working || (loop_cnt < LONG_SUSPEND_TIMER_AAA))
#else
	if (0 || btn_value || ui_ota_is_working || (loop_cnt < LONG_SUSPEND_TIMER_AAA))
#endif
	{
        bls_pm_setManualLatency(0);
		bls_pm_setWakeupSource(0);

        if (suspend_wake_up_enable)
        {

            bls_pm_setWakeupSource(0);
#if WHEEL_FUN_ENABLE_AAA
            wheel_set_wakeup_level_suspend(0);
#endif
#if BUTTON_FUN_ENABLE_AAA
            btn_set_wakeup_level_suspend(0);
#endif
#if SENSOR_FUN_ENABLE_AAA
            sensor_set_wakeup_level_suspend(0);
#endif
        }

        suspend_wake_up_enable = 0;
    }
    else
    {
        //bls_pm_setWakeupSource(PM_WAKEUP_CORE);
        loop_cnt = LONG_SUSPEND_TIMER_AAA + 1;
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
	
        if (suspend_wake_up_enable == 0)
        {
#if WHEEL_FUN_ENABLE_AAA
            wheel_set_wakeup_level_suspend(1);
#endif
#if BUTTON_FUN_ENABLE_AAA
            btn_set_wakeup_level_suspend(1);
#endif
#if SENSOR_FUN_ENABLE_AAA
            sensor_set_wakeup_level_suspend(1);
#endif
        }

        suspend_wake_up_enable = 1;

    }

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
    if (suspend_wake_up_enable && (idle_count > 3))
    {
        bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
    }
    else
    {
        bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
    }
#else
    bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#endif


    // bls_pm_setManualLatency(0);
#endif
}

/////////////////////////////////////////////////////////////////////
// main loop flow
/////////////////////////////////////////////////////////////////////

void main_loop(void)
{



    ////////////////////////////////////// BLE entry /////////////////////////////////
    blt_sdk_main_loop();


    ////////////////////////////////////// UI entry /////////////////////////////////
#if (BLE_AUDIO_ENABLE)
    //blc_checkConnParamUpdate();
    if (ui_mic_enable)
    {
        task_audio();
    }
    user_requestMtuSizeExchange();


#endif
	proc_audio_ble();
    u16 time_interval = 4500;
#if(BLT_SOFTWARE_TIMER_ENABLE)
    blt_soft_timer_process(MAINLOOP_ENTRY);

    check_softe_time();
#else
    u16 interval = bls_ll_getConnectionInterval();
    if ((interval > DEFAULT_INTERVAL) && (conn_params_cout < PARAMS_GROUP_LEN))
    {
        //soft_time_flag = 1;
        time_interval = 8000;
    }
	else if(interval>DEFAULT_INTERVAL)
	{
		time_interval = interval *1250;
	}
    else
    {
        soft_time_flag = 0;
        //time_interval = 4500;
		time_interval = 7500;
    }
#endif
#if  (BLE_APP_PM_ENABLE==0)
    soft_time_flag = 1;
    time_interval = 7500;
#endif
    if (clock_time_exceed(start_tick, time_interval))
    {
        start_tick = clock_time();
        ui_loop();

    }

   // idle_status_poll();
   // ble_status_proc_aaa(has_new_key_event);

    has_new_key_event = 0;
#if BLE_APP_PM_ENABLE

    ble_pm_aaa();
#else
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif





    ////////////////////////////////////// PM Process /////////////////////////////////
    //blt_pm_proc();
}


