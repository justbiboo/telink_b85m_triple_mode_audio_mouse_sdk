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
//#include "../application/audio/tl_audio.h"

#define  CUST_SAVE_MASTER_INFO_ENABLE_AAA      1

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

#define  SIG_PROC_ENABLE			0
#define LONG_SUSPEND_TIMER_AAA 200


#define  DEFAULT_LATENCY  0x10//99

#define PARAMS_GROUP_LEN  4

#define DEFAULT_INTERVAL  6
#define DEFAULT_TIMEOUT  300

#define  REPORT_RATE_100   0  //0 =125~133
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

#if 0
MYFIFO_INIT(blt_rxfifo, RX_FIFO_SIZE, RX_FIFO_NUM);
#else
_attribute_data_retention_user  u8 		 	blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_user	my_fifo_t	blt_rxfifo =
{
    RX_FIFO_SIZE,
    RX_FIFO_NUM,
    0,
    0,
    blt_rxfifo_b,
};
#endif

#if 0
MYFIFO_INIT(blt_txfifo, TX_FIFO_SIZE, TX_FIFO_NUM);
#else
_attribute_data_retention_  u8 		 	blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_txfifo =
{
    TX_FIFO_SIZE,
    TX_FIFO_NUM,
    0,
    0,
    blt_txfifo_b,
};
#endif
_attribute_data_retention_user	my_fifo_t	d24g_txfifo =
{
    TX_FIFO_SIZE,
    TX_FIFO_NUM,
    0,
    0,
    blt_txfifo_b,
};

//////////Adv Packet, Response Packet//////////////////////////////////////////////

#if (Microsoft_Swift_Pairing_ENABLE&&(DEVICE_NAME_INCLUDE_MAC_DEBUG==0))
#define DEV_POSITION   20
#define USER_DEVICE_NAME_MAX_LEN  11
_attribute_data_retention_user u8	tbl_advData_aaa[31] =
{

    0x02, 0x01, 0x05, 		// BLE limited discoverable mode and BR/EDR not supported
    0x03, 0x19, 0xc2, 0x03, // 384, Keyboard, Generic category,
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
    0x03, 0x19, 0xc2, 0x03, // 384, Keyboard, Generic category,
    0x03, 0x03, 0x12, 0x18,	// incomplete list of service class UUIDs (0x1812-HID SERVICE, 180F-BATTERY)
    19, 0x09,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

};
#endif

#if (DEVICE_NAME_INCLUDE_MAC_DEBUG)//dev inc mac

/**
 * @brief       This function set buf data to hex
 * @param[in]   len	- len
 * @param[in]   rp	- receive pkt
 * @param[in]   sp	- source pkt
 * @return      
 * @note        
 */
void BufToHexString(u8 *rp, u8 *sp, u8 len)
{
    u8  hex[17] = "0123456789ABCDEF";//0-f
    u8 count = 0;
    u8      i;
    for (i = len; i > 0; i--)//loop len times
    {
        rp[count++] = hex[sp[i - 1] >> 4];//high 4 hex
        rp[count++] = hex[sp[i - 1] & 0x0F];//low 4 hex
    }
}
#endif

int kb_led_out_write_aaa(void* p)
{



    return 0;
}


/**
 * @brief       This function set adv and scan rsp data
 * @return      
 * @note        
 */
void set_adv_scanRsp_data()
{

    if (device_name_len == 0)//no set device name
    {
#if (DEVICE_NAME_INCLUDE_MAC_DEBUG)//include mac addr
        device_name_len = 16;//16 len
        memcpy(user_cfg.device_name, "78m_", 4);//78m
		;
        BufToHexString(&user_cfg.device_name[4], blc_ll_get_macAddrPublic(), 6);//6 hex mac addr
#else
        device_name_len = sizeof(DEVICE_NAME_AAA) - 1;//get dv name len
        if (device_name_len > USER_DEVICE_NAME_MAX_LEN)//>max len
        {
            device_name_len = USER_DEVICE_NAME_MAX_LEN;//max len
        }
        memcpy(user_cfg.device_name, DEVICE_NAME_AAA, device_name_len);//set dev name
#endif
    }
    else
    {
        if (device_name_len > USER_DEVICE_NAME_MAX_LEN)//>max len
        {
            device_name_len = USER_DEVICE_NAME_MAX_LEN;//max len
        }
    }
    memset(&user_cfg.device_name[device_name_len], 0, 18 - device_name_len);//set 0 to other plack

    memcpy(&tbl_advData_aaa[DEV_POSITION], user_cfg.device_name, device_name_len);//cpy user device name to adv data
    tbl_advData_aaa[DEV_POSITION - 2] = device_name_len + 1;//data len
    bls_ll_setAdvData((u8 *)tbl_advData_aaa, device_name_len + DEV_POSITION);//set adv data
    bls_ll_setScanRspData(&tbl_advData_aaa[DEV_POSITION - 2], device_name_len + 2);//set scan rsp data
}


/**
 * @brief       This function update con params
 * @return      
 * @note        
 */
void connect_params_proc()
{
    if (bls_ll_getConnectionInterval() <= DEFAULT_INTERVAL)//not the default
    {
        conn_params_cout = 0;//cout 0
    }
    else if ((conn_params_cout < PARAMS_GROUP_LEN) && clock_time_exceed(conn_params_tick, 600000))//600ms &&<4times
    {
    	//my_printf_aaa("conn param update--1 \n");
        conn_params_tick = clock_time() | 1;//update con params tick 
        bls_l2cap_requestConnParamUpdate(DEFAULT_INTERVAL, DEFAULT_INTERVAL, DEFAULT_LATENCY, DEFAULT_TIMEOUT);//req update con params
        conn_params_cout++;//cout ++
    }
}

/**
 * @brief       This function switch dir to undir adv
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
void 	app_switch_to_indirect_adv(u8 e, u8 *p, int n)
{
    Switch_Adv_Type += 1;//switch type ++

    if (Switch_Adv_Type & 0x01)//1
    {

        bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                           ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE,
                           0,  NULL,
                           MY_APP_ADV_CHANNEL,
					   ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);//undir


    }
    else
    {

        bls_ll_setAdvParam(ADV_INTERVAL_3_75MS, ADV_INTERVAL_3_75MS,
                           ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, USER_OWN_ADDRESS_TYPE,
                           BLE_ADDR_PUBLIC, smp_param_inf[flash_dev_info.mast_id].peer_addr,
                           BLT_ENABLE_ADV_ALL,
								   ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);//3.75ms low duty dir
    }

    bls_ll_setAdvDuration(1000000, 1);//1ms

    bls_ll_setAdvEnable(1);  //must: set adv enable
}





/**
 * @brief       This function set tx pwr
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
_attribute_ram_code_ void	user_set_rf_power(u8 e, u8 *p, int n)
{
    rf_set_power_level_index(user_cfg.tx_power);//set tx pwr
}


/**
 * @brief       This function write ccc
 * @param[in]   p	- 
 * @return      
 * @note        
 */
int cccWrite(void *p)
{
	clear_fifo();//reset fifo
    connect_begin_tick = clock_time() | 1;//con beagin tick update



    return 0;
}





/**
 * @brief       This function write ccc 1
 * @param[in]   p	- 
 * @return      
 * @note        
 */
int cccWrite1(void *p)
{
    return 0;
}


/**
 * @brief       This function is task connect callback
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
void task_connect(u8 e, u8 *p, int n)
{

	bls_l2cap_requestConnParamUpdate(6, 6, 0x2c, 300);//req conparams update

    connect_begin_tick = clock_time() | 1;//update con begin tick
    clear_fifo();//reset fifo
    reset_idle_status();//reset idle params
    clear_pair_flag();//clear pair flag
    connect_ok = 1;//con flag 1
    ui_ota_is_working = 0;//ota work flag 0
    //conn_params_pending=0;
    conn_params_cout = 0;//cout 0
    conn_params_tick = 0;//conn params tick 0
    conn_step = BEGIN_CONNECT_AAA;//con step begin conn
    bls_ll_setAdvDuration(0, 0);//set no adv
    ble_status_aaa = BEGIN_CONNECTED_STATUS_AAA;//begin conn status

#if APP_24G_AUDIO_EN //audio en
    ui_mtu_size_exchange_req = 1;//mtu size req flag 1
#if AUDIO_AUTO_OPEN_TEST_DEBUG //auto audio test en

    tick_app_mic = clock_time();//app tick update
    app_mic_enable = 0;//mic en 0
#endif
#endif

	my_printf_aaa("connect ok\n");//debug conn

    //rf_packet_connect_t *pc = (rf_packet_connect_t *)(p - 6);
//#if UART_PRINT_DEBUG_ENABLE
    // my_printf_aaa("conn req interval=%x,latency=%x,timeout=%x\r\n", pc->interval, pc->latency, pc->timeout);
    //my_printf_aaa("scan_addr=%02X ,%02X%02X%02X%02X%02X%02X\r\n", pc->txAddr, pc->initA[5], pc->initA[4], pc->initA[3], pc->initA[2], pc->initA[1], pc->initA[0]);
    // my_printf_aaa("adv_addr=%02X%02X%02X%02X%02X%02X\r\n", pc->advA[5], pc->advA[4], pc->advA[3], pc->advA[2], pc->advA[1], pc->advA[0]);
//#endif

#if AUTO_CHECK_OS_TYPE//check os type

    //peer_type=pc->type;
    //peer_con_interval=pc->interval;
    //peer_con_timeout=pc->timeout;

    if (os_type == UNKNOW_OS_TYPE)//unknow
    {
        if (pc->txAddr)
        {
            if ((pc->interval >= 0x0c) && (pc->interval <= 0x18) && (pc->timeout <= 0xc8))
            {
                os_type = APPLE_OS_TYPE;//apple os
/*#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("apple_os\r\n");
#endif*/
            }
            else
            {
                os_type = ANDROID_OS_TYPE;//android os
/*#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("android_os\r\n");
#endif*/

            }
        }
        else//public
        {
            // os_type maybe detect errors
            //because  some andriod phone is public for exampe huawei asw=4.4.3
            os_type = WINDOWS_OS_TYPE;//os window
/*#if UART_PRINT_DEBUG_ENABLE
            my_printf_aaa("window_os\r\n");
#endif*/
        }
    }
#endif

}




/**
 * @brief       This function is con terminate callback
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
void task_terminate(u8 e, u8 *p, int n) //*p is terminate reason
{
//#if 1
	bls_ll_setAdvEnable(0);//adv off

	my_printf_aaa("task_terminate %x\n", *p);//debug con dis

    if((ble_status_aaa == DEEP_TERMINATE_STATUS_AAA))//terminate status
    {
    	ble_status_aaa = DEEP_SLEEPE_STATUS_AAA;//sleep status
    }
	//else if(ble_status_aaa == LOW_ADV_STATUS_AAA)
	//{
	//	set_pair_flag();  //if paired fail,allow adv again
	//	bls_ll_setAdvEnable(1);
	//}
	else if(active_disconnect_reason==BLE_PAIR_REBOOT_ANA_AAA)//pair reboot active discon
	{
		user_reboot(BLE_PAIR_REBOOT_ANA_AAA);//reboot
	}
	else if(active_disconnect_reason == MODE_CHANGE_REBOOT_ANA_AAA)//mode change act discon
    {
        bls_ll_setAdvEnable(0);
			flash_dev_info.mode = RF_2M_2P4G_MODE;		//infact no use , it will reboot and reset
        save_dev_info_flash();
        user_reboot(MODE_CHANGE_REBOOT_ANA_AAA);
    }
     /*#if 0   
    else if (ble_status_aaa == CONN_PARAM_FAIL_TERMINATE_STATUS_AAA)
    {
        user_reboot(CONN_PARAM_FAIL_REBOOT_ANA_AA);
    }
	#endif*/
    else if(active_disconnect_reason==MUTI_DEVICE_REBOOT_ANA_AAA)
	{
		//bls_ll_setAdvEnable(0);
		save_dev_info_flash();//save dev info
		user_reboot(MUTI_DEVICE_REBOOT_ANA_AAA);//muti device reboot
	} else if (active_disconnect_reason==MODE_CHANGE_TO_USB_REBOOT_ANA_AAA) {
		//my_printf_aaa("task terminate, reboot usb mode\r\n");
		//user_reboot(MODE_CHANGE_TO_USB_REBOOT_ANA_AAA);
		fun_mode=USB_MODE;
		ble_status_aaa=0xff;
	}
    else
    {
		ble_status_aaa=POWER_ON_STATUS_AAA;//power on status
    }
/*#else
	bls_ll_setAdvEnable(0);
#endif*/


	active_disconnect_reason=0;//reset active dis reason 0
   	conn_step=0;//step 0
	ui_ota_is_working=0;//ota working 0
	connect_ok=0;//conn ok 0

#if AUTO_CHECK_OS_TYPE//os type
    os_type = UNKNOW_OS_TYPE;//init 0
#endif

#if (APP_24G_AUDIO_EN)//audio en
    if (ui_mic_enable)//mic en
    {
        ui_enable_mic(0);//mic not en
    }
#endif

#if BLT_SOFTWARE_TIMER_ENABLE
    delet_soft_time();
#endif

}

#if SHOW_MAST_REAL_MAC_DEBUG //show master mac en

/**
 * @brief       This function get master real mac
 * @return      
 * @note        
 */
void get_master_real_mac()
{
    if (temp_master_addr[0] & 0x40) //OWN_ADDRESS_RANDOM;
    {
        smp_param_save_t  bondInfo = {0};
        u32 ret = blc_smp_param_loadByAddr(1, &temp_master_addr[2], &bondInfo);//get mast info
        if (ret == 0)
        {
            memset(&output_dev_info.master_mac[0], 0, 6);
            return;
        }
        else
        {
            memcpy(&output_dev_info.master_mac[0], &bondInfo.peer_id_addr[0], 6);//get mast mac
        }
    }
    else
    {
        memcpy(&output_dev_info.master_mac[0], &temp_master_addr[2], 6);
    }
}
#endif

/**
 * @brief       This function is gpio early wakeup callback
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
void task_gpio_eary_wakeup(u8 e, u8 *p, int n)
{
    if (e == BLT_EV_FLAG_GPIO_EARLY_WAKEUP)
    {
        ui_loop();//ui task loop
    }
}

/**
 * @brief       This function save smp inf
 * @return      
 * @note        
 */
void  save_smp_inf()
{
    if (pair_success)//pair suc
    {
		pair_success=0;//suc set 0
		u32 CurStartAddr=0;
		smp_param_save_t smp_param_new;
		CurStartAddr=blc_smp_param_getCurStartAddr();//get smp para save addr now

		flash_read_page(CurStartAddr+bond_device_flash_cfg_idx, 64, &smp_param_new.flag);//read bond info now
		memcpy(&smp_param_inf[flash_dev_info.mast_id].flag, &smp_param_new.flag, 64);//cpy to smp param info
        save_data_to_flash(CFG_MAST_ADDR, LENGHT_USER_BOND_INF, (u8*)&smp_param_inf[0].flag, &binding_master_addr_idx);//save to flash

		/*#if 0
		my_printf_aaa("peer addr type:%x--- %x %x %x %x %x %x\n", smp_param_peer.peer_addr_type,	smp_param_peer.peer_addr[0], smp_param_peer.peer_addr[1], smp_param_peer.peer_addr[2], \
														smp_param_peer.peer_addr[3], smp_param_peer.peer_addr[4],smp_param_peer.peer_addr[5]);
		my_printf_aaa("peer id addr type:%x--- %x %x %x %x %x %x\n", smp_param_peer.peer_id_address_type, smp_param_peer.peer_id_address[0], smp_param_peer.peer_id_address[1], smp_param_peer.peer_id_address[2], \
																	 smp_param_peer.peer_id_address[3], smp_param_peer.peer_id_address[4],smp_param_peer.peer_id_address[5]);
		#else*/
		my_printf_aaa("peer addr type:%x--- %x %x %x %x %x %x\n", smp_param_new.peer_addr_type,	smp_param_new.peer_addr[0], smp_param_new.peer_addr[1], smp_param_new.peer_addr[2], \
														smp_param_new.peer_addr[3], smp_param_new.peer_addr[4],smp_param_new.peer_addr[5]);
		my_printf_aaa("peer id addr type:%x--- %x %x %x %x %x %x\n", smp_param_new.peer_id_adrType, smp_param_new.peer_id_addr[0], smp_param_new.peer_id_addr[1], smp_param_new.peer_id_addr[2],smp_param_new.peer_id_addr[3], smp_param_new.peer_id_addr[4],smp_param_new.peer_id_addr[5]);
		//#endif
		
		if (deep_flag == BLE_PAIR_REBOOT_ANA_AAA)//pair reboot
	   {
		   write_deep_ana0(CLEAR_FLAG_ANA_AAA);//claer reg0
		   flash_dev_info.slave_mac_addr[flash_dev_info.mast_id]++;//slave mac addr mast id ++
		   save_dev_info_flash();//save dev info
	   }

	}
}



/**
 * @brief       This function is app host evnt callback
 * @param[in]   h	- 
 * @param[in]   n	- 
 * @param[in]   para	- 
 * @return      
 * @note        
 */
int app_host_event_callback(u32 h, u8 *para, int n)
{
	u8 event = h & 0xFF;//get evnt


	switch(event)//determin event
    {
		case GAP_EVT_SMP_PARING_FAIL://pair fail
        {
            gap_smp_pairingFailEvt_t *p = (gap_smp_pairingFailEvt_t *)para;

            if (p->reason == PARING_FAIL_REASON_UNSPECIFIED_REASON)
            {
                bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN); //push terminate cmd into ble TX buffer
            }
			
        }
        break;
		case GAP_EVT_SMP_PARING_SUCCESS://pair suc
        {
			 gap_smp_pairingSuccessEvt_t *p = (gap_smp_pairingSuccessEvt_t *)para;
            if (p->bonding_result)//result
			 {
					 pair_success=1;//pair flag 1
				 #if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("pair success\r\n");
		   #endif
			 }
        }
        break;

        case GAP_EVT_SMP_CONN_ENCRYPTION_DONE://encryption done
        {

            gap_smp_connEncDoneEvt_t *p = (gap_smp_connEncDoneEvt_t *)para;
            if (p->re_connect == SMP_STANDARD_PAIR)  //first paring
            {
             
				conn_step = SMP_FIRST_CONNECT_DONE_AAA;//first con done to conn step
		   #if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("first connected\r\n");
		   #endif

            }
            else if (p->re_connect == SMP_FAST_CONNECT)  //auto connect
            {
            	clear_fifo();//reset fifo
               		conn_step = SMP_RECONNECT_DONE_AAA;//reconn step
#if UART_PRINT_DEBUG_ENABLE
                my_printf_aaa("reconnected\r\n");
#endif
            }
            connect_begin_tick = clock_time() | 1;//update con begin tick

        }
        break;
        case GAP_EVT_ATT_EXCHANGE_MTU:
        {
        	gap_gatt_mtuSizeExchangeEvt_t *p = (gap_gatt_mtuSizeExchangeEvt_t *)para;
            my_printf_aaa("peer_MTU %d,effetive_mtu %d\r\n", p->peer_MTU,p->effective_MTU);
/*#if 0
            gap_gatt_mtuSizeExchangeEvt_t *p = (gap_gatt_mtuSizeExchangeEvt_t *)para;
            if ((1 == os_check) && p->peer_MTU >= 0x87)
            {
                os_check = 3;	// ios
                os_set_user();
                //apple_need_quick_send_sec_req=1;
            }
#endif*/
        }
        break;
        default:
            break;
    }

    return 0;
}


/**
 * @brief       This function is con update req cb
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
void	task_conn_update_req(u8 e, u8 *p, int n)
{
/*#if 0
    conn_params_pending = 1;

    u16 interval = p[3] | p[4] << 8;
    u16 latency = p[5] | p[6] << 8;

    if ((interval >= ConParams[conn_params_cout].Min) && (interval <= ConParams[conn_params_cout].Max) && latency > 0)
    {
        conn_params_cout = 0xff;

    }
    else
    {
        conn_params_cout++;
    }
#endif*/
}


/**
 * @brief       This function is con update done cb
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
void	task_conn_update_done(u8 e, u8 *p, int n)
{
/*#if 0
    if (ble_status_aaa == T5S_CONNECTED_STATUS_AAA) // for windows
    {
        if ((conn_params_cout == 0) && (bls_ll_getConnectionInterval() > (ConParams[0].Min + 4)))
        {
            if (deep_flag != CONN_PARAM_FAIL_REBOOT_ANA_AA)
            {
                bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);
                ble_status_aaa = CONN_PARAM_FAIL_TERMINATE_STATUS_AAA;
            }
        }
    }
#endif*/
}



/**
 * @brief       This function set pads wk up src
 * @param[in]   e	- 
 * @param[in]   n	- 
 * @param[in]   p	- 
 * @return      
 * @note        
 */
_attribute_ram_code_ void  ble_remote_set_sleep_wakeup(u8 e, u8 *p, int n)
{
	//#if 1
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
	//#endif
}

int app_conn_param_update_response(u8 id, u16  result)
{
/*#if 0
    if (result == CONN_PARAM_UPDATE_ACCEPT)
    {
        conn_params_cout = 0;
        ble_status_aaa = T5S_CONNECTED_STATUS_AAA;
    }
    else if (result == CONN_PARAM_UPDATE_REJECT)
    {

        conn_params_cout++;
        connect_params_proc();
    }
#endif*/
    return 0;
}


int att_sig_proc_handler(u16 connHandle, u8 *p)
{
/*#if 0//SIG_PROC_ENABLE

    rf_pkt_l2cap_sig_connParaUpRsp_t *pp = (rf_pkt_l2cap_sig_connParaUpRsp_t *)p;

#if 0//test debug
    foreach (i, 16)
    {
        PrintHex(*((u8 *)pp + i));
    }
    printf(".\n");
#endif

    u8 sig_conn_param_update_rsp[9] = { 0x0A, 0x06, 0x00, 0x05, 0x00, 0x13, 0x01, 0x02, 0x00 };
    if (!memcmp(sig_conn_param_update_rsp, &pp->rf_len, 9) && ((pp->type & 0b11) == 2)) //l2cap data pkt, start pkt
    {
        if (pp->result == 0x0000)
        {
#if PRINT_DEBUG_INFO
            printf("Host has accepted conn parameters\r\n");

#endif
            conn_params_pending = 0;
            conn_params_cout = 0xff;
        }
        else if (pp->result == 0x0001)
        {
            conn_params_pending = 0;
            conn_params_cout++;
#if PRINT_DEBUG_INFO

            printf("Host has rejected conn parameters\r\n");
#endif
        }
    }
#endif*/
    return 0;
}
extern u8 usb_start_flag;

u8 pc_status_through_ble = 1;

int pc_suspend_out_write_aaa(void* p)
{
	rf_packet_att_write_t *pkt_cmd = (rf_packet_att_write_t *)p;//get data

	pc_status_through_ble = pkt_cmd->value;//read status
    return 0;
}


#if (BLE_OTA_SERVER_ENABLE)//ota en

/**
 * @brief       This function enter ota mode
 * @return      
 * @note        
 */
void app_enter_ota_mode(void)
{
	wd_stop();//wd clear
	ui_ota_is_working = 1;//ota working now
#if (BLT_APP_LED_ENABLE && BLE_OTA_LED_DEBUG)
    gpio_write(PIN_BLE_LED, 1);
#endif
#if UART_PRINT_DEBUG_ENABLE
    my_printf_aaa("---start ota---\r\n");//ota start
#endif
    blt_ota_start_tick = clock_time();  //mark time
	usb_start_flag = 1;  //mark time
	bls_ota_setTimeout(120 * 1000 * 1000); //set OTA timeout  15 seconds
}

/**
 * @brief       This function show ota result
 * @param[in]   result	- 
 * @return      
 * @note        
 */
void app_debug_ota_result(int result)
{
   irq_disable();//dis irq
   wd_stop();//clear wd
#if(BLE_OTA_LED_DEBUG && BLT_APP_LED_ENABLE)//ota led en
	gpio_set_output_en(PIN_BLE_LED, 1);//output en
    if (result == OTA_SUCCESS)  //OTA success
    {
		gpio_write(PIN_BLE_LED, 1);//on
		sleep_us(2000000);  //led on for 2 second
		gpio_write(PIN_BLE_LED, 0);//off
#if UART_PRINT_DEBUG_ENABLE

        my_printf_aaa("ota success\r\n");//debug ota suc
#endif
    }
    else   //OTA fail
    {
        for (int i = 0; i < 4; i++)//4 times
        {
			gpio_write(PIN_BLE_LED, 1);//on
			sleep_us(250000);//250ms
			gpio_write(PIN_BLE_LED, 0);//off
			sleep_us(250000);//250ms
		}
#if UART_PRINT_DEBUG_ENABLE
        my_printf_aaa("ota fail=%x\r\n",result);//debug ota err
#endif
    }
#endif
}
#endif
/*#if 0
void ble_show_mode()
{
    if ((deep_flag == POWER_ON_ANA_AAA) || (deep_flag == MODE_CHANGE_REBOOT_ANA_AAA))
    {
        if (deep_flag == MODE_CHANGE_REBOOT_ANA_AAA)
        {
            write_deep_ana0(CLEAR_FLAG_ANA_AAA);
        }
#if BLT_APP_LED_ENABLE
        led_ble_mode_display();
#endif
    }
}
#endif*/
/**
 * @brief       This function check ble stack smp info
 * @return      
 * @note        
 */
void change_ble_stack_smp_inf()
{
	smp_param_save_t tmp;
	u8* src=&smp_param_inf[0].flag;
	u8 *last=&tmp.flag;
	
	binding_master_addr_idx = flash_info_load_aaa(CFG_MAST_ADDR, src, LENGHT_USER_BOND_INF);//read smp para info
	if(binding_master_addr_idx<0)//no info
	{
		set_pair_flag();//set pair
		return;
	}

	flash_read_page(SMP_PARAM_NV_ADDR_START, sizeof(tmp), last);//read to tmp
	for(u8 i=0;i<8;i++)
	{
		if(memcmp(last, &smp_param_inf[flash_dev_info.mast_id].flag, sizeof(tmp)))//cmp not same
		{
			flash_erase_sector(SMP_PARAM_NV_ADDR_START);//erase
			flash_write_page(SMP_PARAM_NV_ADDR_START, sizeof(tmp), &smp_param_inf[flash_dev_info.mast_id].flag);//write new smp params info
			flash_read_page(SMP_PARAM_NV_ADDR_START, sizeof(tmp), last);//read again
			
		}
		else
		{
			break;
		}
	}
}



/**
 * @brief       This function init normal
 * @return      
 * @note        
 */
void user_init_normal(void)
{
    blt_txfifo.wptr = 0;//tx wprt 0
    blt_txfifo.rptr = 0;//tx fifo rptr 0
    blt_rxfifo.wptr = 0;//rx fifo wptr 0
    blt_rxfifo.rptr = 0;//rx fifo prtr 0
	

	//random_generator_init();  //this is must
    //ble_show_mode();

	
    ////////////////// BLE stack initialization ////////////////////////////////////
#if BLE_SNIFF_DEBUG //sniff mode
    u8  tbl_mac [6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xc7};
    extern 	u8	blc_adv_channel[];
    //_attribute_data_retention_	u8		blc_adv_channel[3] = {37, 38, 39};
    blc_adv_channel[0] = 37;//37
    blc_adv_channel[1] = 38;//38
    blc_adv_channel[2] = 39;//39
    memcpy(tbl_mac, (u8 *)&user_cfg.dev_mac, 6);//cpy dev mac to tbl mac
    tbl_mac[5] = 0XD0 | (flash_dev_info.mast_id + 1);//with mast id 
	
#else
    u8  tbl_mac [] = {0x12, 0x34, 0x56, 0x78, 0x11, 0x54};
    memcpy(tbl_mac, (u8 *)&user_cfg.dev_mac, 6);//cpy dev mac to tbl mac
	

    if (pair_flag)//pair flag 1
    {
        tbl_mac[4] = flash_dev_info.slave_mac_addr[flash_dev_info.mast_id] + 1;//mast id times
    }
    else
    {
        tbl_mac[4] = flash_dev_info.slave_mac_addr[flash_dev_info.mast_id];//mast id times no change
    }

    tbl_mac[5] = 0XD0 | (flash_dev_info.mast_id + 1);//with mast id
    
#endif

    blc_ll_setRandomAddr(tbl_mac);//set random mac

    ////// Controller Initialization  //////////
    blc_ll_initBasicMCU();                      //mandatory
    blc_ll_initStandby_module(tbl_mac);				//mandatory
    blc_ll_initAdvertising_module(tbl_mac); 	//adv module: 		 mandatory for BLE slave,
    blc_ll_initConnection_module(); 			//connection module  mandatory for BLE slave/master
    blc_ll_initSlaveRole_module();				//slave module: 	 mandatory for BLE slave,
    blc_ll_initPowerManagement_module();        //pm module:      	 optional

    ////// Host Initialization  //////////
    blc_gap_peripheral_init();    //gap initialization
    extern void my_att_init();
    my_att_init();  //gatt initialization
    blc_att_setRxMtuSize(MTU_SIZE_SETTING);
    blc_l2cap_register_handler(blc_l2cap_packet_receive);  	//l2cap initialization

    //// smp initialization ////
#if (BLE_REMOTE_SECURITY_ENABLE)
    //blc_smp_set_simple_multi_mac_en(1);
    //extern u8 device_mac_index;
    //device_mac_index=(flash_dev_info.mast_id + 1);

    //app change smp info must be here
	change_ble_stack_smp_inf();//check ble stack smp inf
    blc_smp_param_setBondingDeviceMaxNumber(4); 	//default is 4, can not bigger than this value
															//and this func must call before bls_smp_enableParing
	blc_smp_peripheral_init ();
	blc_smp_setSecurityLevel(Unauthenticated_Pairing_with_Encryption);

    //Hid device on android7.0/7.1 or later version
    // New paring: send security_request immediately after connection complete
    // reConnect:  send security_request 1000mS after connection complete. If master start paring or encryption before 1000mS timeout, slave do not send security_request.
    blc_smp_configSecurityRequestSending(SecReq_NOT_SEND, SecReq_NOT_SEND, 1000); //if not set, default is:  send "security request" immediately after link layer connection established(regardless of new connection or reconnection )

    blc_gap_registerHostEventHandler(app_host_event_callback);//register gap evnt callback
    blc_gap_setEventMask(GAP_EVT_MASK_SMP_PARING_FAIL				|  \
                         GAP_EVT_MASK_SMP_PARING_SUCCESS			|  \
                         GAP_EVT_MASK_ATT_EXCHANGE_MTU    |  \
                         GAP_EVT_MASK_SMP_CONN_ENCRYPTION_DONE);
    extern 	void blc_att_holdAttributeResponsePayloadDuringPairingPhase(u8 hold_enable);
    
    blc_att_holdAttributeResponsePayloadDuringPairingPhase(1);

#else
    blc_smp_setSecurityLevel(No_Security);//no security
#endif

    //HID_service_on_android7p0_init();  //hid device on android 7.0/7.1
    //blc_att_setServerDataPendingTime_upon_ClientCmd(50);

#if SIG_PROC_ENABLE
    blc_l2cap_reg_att_sig_hander(att_sig_proc_handler); 		//register sig process handler
#endif
    //blc_l2cap_registerConnUpdateRspCb(app_conn_param_update_response);

    ///////////////////// USER application initialization ///////////////////

    set_adv_scanRsp_data();//set adv and scan data

    //set rf power index, user must set it after every suspend wakeup, cause relative setting will be reset in suspend
    user_set_rf_power(0, 0, 0);//set rf tx pwr
    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);//equit supend callback set tx pwr again

    //ble event call back
    bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);//register task conn callback
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &task_terminate);//register terminate callback

    bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_indirect_adv);

    bls_app_registerEventCallback(BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &task_gpio_eary_wakeup);//register gpio early wkup cb
    //unit is 10ms
    blc_att_setServerDataPendingTime_upon_ClientCmd(10);//100ms pending time

    ///////////////////// Power Management initialization///////////////////
#if(BLE_APP_PM_ENABLE)//pm en
    blc_ll_initPowerManagement_module();//init pm module

#if (PM_DEEPSLEEP_RETENTION_ENABLE)//deep ret en
    bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);//add ret adv&con
    blc_pm_setDeepsleepRetentionThreshold(95, 95);//set threshold
    blc_pm_setDeepsleepRetentionEarlyWakeupTiming(400);//set early wkup timing
    blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);//set 32k sram deep ret mode
#else
    bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);//suspend mode adv or con
#endif

    bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &ble_remote_set_sleep_wakeup);//register suspend enter cb
#else
    bls_pm_setSuspendMask(SUSPEND_DISABLE);//dis suspend
#endif

#if (BLE_OTA_SERVER_ENABLE)//ota en
    ////////////////// OTA relative ////////////////////////
#if (UART_PRINT_DEBUG_ENABLE)
			blc_debug_addStackLog(STK_LOG_OTA_FLOW);
#endif
	blc_ota_initOtaServer_module();

	blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
	blc_ota_registerOtaResultIndicationCb(app_debug_ota_result);

#endif
	

#if BLT_SOFTWARE_TIMER_ENABLE //soft time en
    blt_soft_timer_init();//init soft timer
#endif
    start_tick = clock_time();//update start tick
}



/**
 * @brief       This function init deepret
 * @return      
 * @note        
 */
_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)//deep ret en

    blc_ll_initBasicMCU();   //mandatory
    rf_set_power_level_index(user_cfg.tx_power);//set tx pwr

    blc_ll_recoverDeepRetention();//recov from deep retention

    DBG_CHN0_HIGH;    //debug
    irq_enable();//irq en

    //pm mask disable,in main_loop according to ui task will reset pm mask
    bls_pm_setSuspendMask(SUSPEND_DISABLE);//dis suspend

    //app_ui_init_deepRetn();
#endif
}

/**
 * @brief       This function let chip enter deepsleep
 * @return      
 * @note        
 */
void enter_deep_aaa()
{
    printf("enter deep now\n");
	write_deep_ana0(DEEP_SLEEP_ANA_AAA);//wrt deep sleep to reg0
    analog_write(USED_DEEP_ANA_REG1, (fun_mode << 1));

#if BUTTON_FUN_ENABLE_AAA
    btn_set_wakeup_level_deep();
#endif

#if WHEEL_FUN_ENABLE_AAA
    wheel_set_wakeup_level_deep();
#endif

#if SENSOR_FUN_ENABLE_AAA
    sensor_set_wakeup_level_deepsleep(1);
    OPTSensor_Shutdown();
#endif

	clear_pair_flag();//clear pair flag

	usb_vbus_deepWakeup_enable();
	cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);//deep sleep
}



/**
 * @brief       This function set adv params and type
 * @return      
 * @note        
 */
void set_adv_type()
{
	u32 *adr=(u32*)&smp_param_inf[flash_dev_info.mast_id].peer_addr;
	ll_whiteList_reset(); 	  //clear whitelist
	ll_resolvingList_reset(); //clear resolving list
	tbl_advData_aaa[2] = 0x05;
    if ((binding_master_addr_idx >= 0) && (pair_flag == 0)) 	//at least 1 bonding device exist
    {
		if ((adr[0] == 0)||(adr[0]==U32_MAX)) //no valid bind
		{ //check whether this mast_id has been bonded
			my_printf_aaa("channel %d not bonded\n", flash_dev_info.mast_id);//not bonded

			set_pair_flag();//set pair
			bls_ll_setAdvDuration(0, 0);//no set adv duration
	        bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
	                           ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE,
	                           0,  NULL,
	                           MY_APP_ADV_CHANNEL,
	                           ADV_FP_NONE);
		} 
		else if(smp_param_inf[flash_dev_info.mast_id].peer_addr_type == 0) //public type
		{ //public addr
			//memcpy(bonded_peer_addr, (u8 *)&user_bond_inf[flash_dev_info.mast_id].con_bt_addr[0], 6);
			my_printf_aaa("channel %d bonded a public addr, direct\n", flash_dev_info.mast_id);//debug  mast public
			#if(0)
			ll_whiteList_reset(); 	  //clear whitelist
			ll_resolvingList_reset(); //clear resolving list
			if( IS_RESOLVABLE_PRIVATE_ADDR(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr) )
			{
				ll_resolvingList_add(0, smp_param_inf[flash_dev_info.mast_id].peer_id_addr, smp_param_inf[flash_dev_info.mast_id].peer_irk, NULL);  //no local IRK
				ll_resolvingList_setAddrResolutionEnable(1);//add resolution en
				my_printf_aaa("random addr wL is resolvable priver\n");//debug rand wl
        	}
			else
			{
				//if not resolvable random address, add peer address to whitelist
			ll_whiteList_add(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr);//add to wl
				my_printf_aaa("random addr wL is not resolvable priver\n");//debug wl
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
                               ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);//set dir adv
            bls_ll_setAdvDuration(2000000, DIRECT_ADV_TO_UNDIRECT_ENABLE);//2s adv duration
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
				ll_resolvingList_setAddrResolutionEnable(1);//add resolution en
				my_printf_aaa("random addr wL is resolvable priver\n");//debug rand wl
        	}
			else
			{
				//if not resolvable random address, add peer address to whitelist
				ll_whiteList_add(smp_param_inf[flash_dev_info.mast_id].peer_addr_type, smp_param_inf[flash_dev_info.mast_id].peer_addr);
				my_printf_aaa("random addr wL is not resolvable priver\n");//debug wl
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
            bls_ll_setAdvDuration(0, 0);//no set adv duration
			bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX, \
							ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE, \
							0,  NULL, MY_APP_ADV_CHANNEL, ADV_FP_ALLOW_SCAN_WL_ALLOW_CONN_WL);
        }
    }
    else
    {
		my_printf_aaa("normal pairing adv channel %d\n", flash_dev_info.mast_id);
		bls_ll_setAdvDuration(0, 0);//no adv duration
        bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
                           ADV_TYPE_CONNECTABLE_UNDIRECTED, USER_OWN_ADDRESS_TYPE,
                           0,  NULL,
                           MY_APP_ADV_CHANNEL,
                           ADV_FP_NONE);//undir adv
    }
}




/**
 * @brief       This function proc ble status
 * @param[in]   is_new_key_event	- 
 * @return      
 * @note        
 */
void ble_status_proc_aaa(u8 is_new_key_event)
{
    if(ble_status_aaa == T5S_CONNECTED_STATUS_AAA)
    {
	#if (BLE_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA && (!BLE_SNIFF_DEBUG))
		if ((idle_count >= BLE_CONNECT_TIME_OUT))
		{
			bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);
			ble_status_aaa = DEEP_TERMINATE_STATUS_AAA;
		}
	#endif

       // connect_params_proc();
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
        else if (adv_count >= 10)
        {
            ble_status_aaa = DEEP_SLEEPE_STATUS_AAA;
        }
	#endif
	
	#if BLT_APP_LED_ENABLE
        led_ble_Adv_poll();
	#endif
	/*#if 0
        if (is_new_key_event && (pair_flag == 0))
        {
            ble_status_aaa = POWER_ON_STATUS_AAA;
        }
	#endif*/
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
				save_smp_inf();
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
   // adv_count = 0;
}
void idle_status_poll()
{
    u32 n;
    n = ((u32)(clock_time() - idle_tick)) / CLOCK_16M_SYS_TIMER_CLK_1S;

    idle_tick += n * CLOCK_16M_SYS_TIMER_CLK_1S;

    idle_count += n;
/*#if 0
    if (idle_count > 0xfffffff0)
    {
        idle_count = 0xfffffff0;
    }
#endif*/
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
        //ble_pair_process_aaa();
        
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
        //if (conn_step > BEGIN_CONNECT_AAA)
		if(connect_ok &&(ble_status_aaa == T5S_CONNECTED_STATUS_AAA))
        {
            ble_notify_data_proc_aaa();
        }
        else
        {
            ms_data.wheel = 0;
        }

        device_led_process();
    }

}

void ble_pm_aaa()
{
	if (now_vbus_exist_flag)
	{
		 bls_pm_setSuspendMask(SUSPEND_DISABLE);
        return;
	}
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
    if(0|| btn_value  || DEVICE_LED_BUSY || ui_ota_is_working || (loop_cnt < LONG_SUSPEND_TIMER_AAA))
    {
        bls_pm_setManualLatency(0);
		bls_pm_setWakeupSource(0);

        if(suspend_wake_up_enable)
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
	
        if(suspend_wake_up_enable == 0)
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
	{
    	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
	}
#endif

    // bls_pm_setManualLatency(0);
#endif
}
#if BLT_SOFTWARE_TIMER_ENABLE
int soft_timer_suspend_proc(void)
{
    ui_loop();

    return MOUSE_TIMER_SHORT_T;
}

void add_soft_time()
{
    if (soft_time_flag == 0)
    {
        if (blt_timer.currentNum == 0)
        {
            blt_soft_timer_add(&soft_timer_suspend_proc, MOUSE_TIMER_SHORT_T);
        }
        soft_time_flag = 1;
    }
}
void delet_soft_time()
{
    if (soft_time_flag == 1)
    {
        // Ö®Ç°ÊÇ timer¶¨Ê±ÊµÏÖ
        if (blt_timer.currentNum)
        {
            blt_soft_timer_delete(&soft_timer_suspend_proc);
        }
        soft_time_flag = 0;
    }
}
void check_softe_time()
{
#if 1
    u16 interval = bls_ll_getConnectionInterval();
    if ((interval > DEFAULT_INTERVAL))
    {
        add_soft_time();
    }
    else
    {
        delet_soft_time();
    }
#endif
}
#endif

 bool start_check_usb_host = false;
 bool checking_usb_host = false;

extern u32 usb_mode_start_tick;
extern bool usb_host_conn;
 u8 usb_check_value;
 u8 usb_check_pre_value;

void main_loop(void)
{

	// BLE entry
	blt_sdk_main_loop();

    //updata_conn_param_request();
	// UI entry 
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
        soft_time_flag = 1;
        time_interval = 8000;
    }
    else
    {
        soft_time_flag = 0;
        time_interval = 4500;
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

    //idle_status_poll();
	
    //ble_status_proc_aaa(has_new_key_event);

    has_new_key_event = 0;
	
#if BLE_APP_PM_ENABLE
	
		ble_pm_aaa();
#else
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif
#if UART_PRINT_DEBUG_ENABLE
	static u32 main_loop_tick = 0;
	if(clock_time_exceed(main_loop_tick, 5000*1000))
		{
			main_loop_tick = clock_time();
			printf("the ble status is %d, interval is %d,connect is %d\n",ble_status_aaa,bls_ll_getConnectionInterval(),connect_ok);
		}
#endif
    // PM Process
    //blt_pm_proc();
    
}


