/********************************************************************************************************
 * @file     aaa_public_config.h
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

#ifndef _AAA_CONFIG_H_
#define _AAA_CONFIG_H_

#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "aaa_sensor_pix.h"
#include "aaa_sensor.h"
#include "../common/blt_led.h"
#include "../../vendor/common/user_config.h"
#include "aaa_24g_rf_frame.h"
#include "aaa_24g_rf.h"
#include "aaa_battery_check.h"
#include "aaa_emi.h"
#include "../common/blt_soft_timer.h"

//#include "../../application/usbstd/usbhw.h"
//#include "../../application/usbstd/usbhw_i.h"
#include "../../application/usbstd/stdDescriptors.h"
#include "../../application/audio/audio_common.h"
#include "../../application/audio/tl_audio.h"
#include"../../application/audio/audio_config.h"
#include "../../application/usbstd/usb.h"
#include "../../application/audio/adpcm.h"



#if APP_FLASH_LOCK_ENABLE
#include "drivers/8278/flash/flash_type.h"
#endif
extern my_fifo_t	d24g_txfifo;

#define TL_AUDIO_MODE  						TL_AUDIO_RCU_MSBC_HID//TL_AUDIO_RCU_ADPCM_GATT_TLEINK//TL_AUDIO_RCU_MSBC_HID //TL_AUDIO_RCU_ADPCM_GATT_TLEINK

#if APP_24G_AUDIO_EN





_attribute_data_retention_user  extern u32 audio_stick;
_attribute_data_retention_user extern u8 audio_start;
_attribute_data_retention_user	extern u8		ui_mic_enable;
_attribute_data_retention_user	extern u8 has_mic_data_flag;

extern s16		buffer_mic[TL_MIC_BUFFER_SIZE>>1];
extern u8 buffer_mic_pkt_rptr;
extern u8 buffer_mic_pkt_wptr;


#endif
_attribute_data_retention_user  extern int ui_mtu_size_exchange_req;

extern my_fifo_t	fifo_km;

typedef struct
{
    /** Minimum value for the connection event (interval. 0x0006 - 0x0C80 * 1.25 ms) */
    u16 Min;
    /** Maximum value for the connection event (interval. 0x0006 - 0x0C80 * 1.25 ms) */
    u16 Max;
    /** Number of LL latency connection events (0x0000 - 0x03e8) */
    u16 latency;
    /** Connection Timeout (0x000A - 0x0C80 * 10 ms) */
    u16 timeout;
} gap_periConnectParams_t;


//--------------------function defein----------------------------------------

#define MS_BTN_LEFT 	0X01
#define MS_BTN_RIGHT 	0X02
#define MS_BTN_MIDDLE 	0X04
#define MS_BTN_K4 		0X08
#define MS_BTN_K5 		0X10

#define MS_BTN_MODE     0X20
#define MS_BTN_PAIR     0X40
#define MS_BTN_CPI 		0X80
#define MS_BTN_DOUBLE_LEFT 		0X100
#define MS_BTN_DEVICE_1 0x200
#define MS_BTN_DEVICE_2 0x400
//#define MS_BTN_DEVICE_3 0x800
//#define MS_BTN_DEVICE_4 0x1000


//special key 
#define MS_BTN_VOICE  0X2000
#define MS_BTN_TRANSLATE 0X8000
//------------tx power define-------------------
#define DEFAULT_NORMAL_TX_POWER   RF_POWER_P8p92dBm 
#define DEFAULT_PAIR_TX_POWER	  RF_POWER_P0p52dBm
#define DEFAULT_EMI_TX_POWER      RF_POWER_P0p52dBm
//--------------------------------------------------

#define AES_SWITCH_TIME 10000
//----------------------------------------------------------------------------
typedef struct{
	u32	flash_addr;
	u8	con_bt_addr[6];	
	u8  peer_id_bt_add[6];
}STRUCT_USER_BOND_INF;
#define LENGHT_USER_BOND_INF  256

#define USB_OTA_LENGTH  64
typedef struct{
	u8	report_id;
	u8 	opcode;
	u16	length;					
	u8	dat[60];
}usb_data_t;

typedef struct
{
	u16 cmd;
	u8 buf[16];
	u16 crc;
}ota_data_st;


typedef struct
{
    u32   dev_mac; //00
    u16   group_id;//04~05
    u16  rf_vid;	//06~07
    u8  bat_type;	//008
    u8	 tx_power;	//09
    u8	 paring_tx_power;//0a
    u8   emi_tx_power;   //00b
    u8	 wheel_direct;     //0c
    u8	 internal_cap;   //0d
    u8   sensor_direct;  //0e
    u8 	 aes_enable; //0f
    u8   pub_key[16];        //10~1f
    u8   device_name[18];	//20~31

} custom_cfg_t;
extern km_3_c_1_data_t *p_km_data;

_attribute_data_retention_user extern u8 report_rate;
_attribute_data_retention_user extern u8 last_report_rate;

_attribute_data_retention_user extern custom_cfg_t   user_cfg;
_attribute_data_retention_user extern  u8 pub_key[];
_attribute_data_retention_user extern  u8 device_name_len;
_attribute_data_retention_user extern u8	tbl_advData_aaa[];

_attribute_data_retention_user extern u8 sensor_type;
_attribute_data_retention_user extern u8 product_id1;
_attribute_data_retention_user extern u8 product_id2;
_attribute_data_retention_user extern u8 product_id3;
_attribute_data_retention_user  extern u8 connect_ok;
_attribute_data_retention_user extern u8 has_double_click_left;
extern u8 pair_success_flag;




typedef enum{
	RF_IDLE_STATUS =	0,
    RF_TX_START_STATUS=1,
    RF_TX_END_STATUS=2,
    RF_RX_START_STATUS=3,
    RF_RX_END_STATUS=4,
	RF_RX_TIMEOUT_STATUS=5,
		
}RF_STATUS_USER;
extern  volatile unsigned int  rf_state;
extern  volatile int	   device_ack_received;
extern  u8		device_channel;
extern volatile unsigned int rf_rx_timeout_us;

typedef enum
{ 	
	IDLE,
	USB_DEVICE_CONNECT_PC,
	USB_DEVICE_CHECK_PC_SLEEP,
	USB_DEVICE_DISCONECT_PC,//may be pc power off
}USB_DEVICE_STATUS;

extern u8 usb_device_status;
extern u8 now_vbus_exist_flag;


typedef enum
{
    TWO_SPEED_SWITCH = 0,
    THREE_SPEED_SWITCH = 1,
} SWITCH_TYPE_AAA;

typedef enum
{
    POWER_ON_ANA_AAA = 0,
    DEEP_SLEEP_ANA_AAA = 1,
    MODE_CHANGE_REBOOT_ANA_AAA = 2,
    BLE_PAIR_REBOOT_ANA_AAA = 3,
    CONN_PARAM_FAIL_REBOOT_ANA_AA = 4,
    CLEAR_FLAG_ANA_AAA = 5,
    MUTI_DEVICE_REBOOT_ANA_AAA = 6,
    MODE_CHANGE_TO_CHARGING_REBOOT_ANA_AAA = 7,
    MODE_CHANGE_TO_USB_REBOOT_ANA_AAA = 8,
    D24G_OTA_ENABLE_REBOOT_ANA_AAA = 9,
} ANA_STATUS_AAA;


typedef enum
{
    POWER_ON_STATUS_AAA = 0,
    HIGH_ADV_STATUS_AAA = 1,
    LOW_ADV_STATUS_AAA = 2,
    BEGIN_CONNECTED_STATUS_AAA = 3,
    OK_CONNECTED_STATUS_AAA = 4,
    T5S_CONNECTED_STATUS_AAA = 5,
    DEEP_SLEEPE_STATUS_AAA = 6,
    DEEP_TERMINATE_STATUS_AAA = 7,
    MODE_CHANGE_TERMINATE_STATUS_AAA = 8,
    CONN_PARAM_FAIL_TERMINATE_STATUS_AAA = 9,
    MUTI_DEVICE_CHANGE_STATUS_AAA = 10,
} BLE_STATUS_AAA;
typedef enum
{
    KEY_PRESS_EVENT_AAA = BIT(0),
    NEW_KEY_EVENT_AAA = BIT(0),
    SENSOR_DATA_EVENT_AAA = BIT(2),
    WHEEL_DATA_EVENT_AAA = BIT(3),
} key_EVENT_AAA;
typedef enum
{
    UNKNOW_OS_TYPE = 0,
    APPLE_OS_TYPE = 1,
    ANDROID_OS_TYPE = 2,
    WINDOWS_OS_TYPE = 3,
} PC_OS_TYPE_AAA;
typedef enum
{
    HAS_MOUSE_REPORT = BIT(0),
    HAS_KEYBOARD_REPORT = BIT(1),
    HAS_CONSUMER_REPORT = BIT(2),
    HAS_JOYSTIC_REPORT = BIT(3),
} HAS_REPORT_TYPE_AAA;
typedef enum
{
    FN_PRESS_AAA = BIT(0),
    T_BIND_PRESS_AAA = BIT(1),
    MOUSE_KEY_PRESS_AAA = BIT(2),
    T_ESC_PRESS_AAA = BIT(3),
    T_Q_PRESS_AAA = BIT(4),
    T_W_PRESS_AAA = BIT(5),
    T_E_PRESS_AAA = BIT(6),
    T_R_PRESS_AAA = BIT(7),
    T_T_PRESS_AAA = BIT(8),
    T_Y_PRESS_AAA = BIT(9),
    T_U_PRESS_AAA = BIT(10),
    T_I_PRESS_AAA = BIT(11),
    T_O_PRESS_AAA = BIT(12),
    T_F1_PRESS_AAA = BIT(13),
    T_SPACE_PRESS_AAA = BIT(14),
} SPECIAL_KEY_PRESS_FLAG_AAA;

#define MAX_BTN_CNT_AAA 6
typedef struct
{
    u8 cnt;
    u8 keycode[MAX_BTN_CNT_AAA];
    u16 special_key_press_f;
    u8 press_cnt;
} kb_data_t_aaa;
_attribute_data_retention_user extern kb_data_t_aaa key_buf_aaa;

typedef enum
{
    MULTIPIPE_1 = 0,
    MULTIPIPE_1_DOT_5 = 1,
    MULTIPIPE_2_DOT_0 = 2,
    MULTIPIPE_2_DOT_5 = 3,
    MULTIPIPE_3_DOT_0 = 4,
} SENSOR_MULTIPIPE_XY;


#define BUF_SIZE_KEYBOARD_AAA	8
#define BUF_SIZE_CONSUMER_AAA	4


#define TX_FIFO_NUM_AAA   12
#define TX_BUF_SIZE_AAA   10


typedef struct
{
    u8 fifo[TX_FIFO_NUM_AAA][TX_BUF_SIZE_AAA];
    u8 wptr;
    u8 rptr;
	u16 count;

} TX_FIFO_AAA_STRUCT;
_attribute_data_retention_user extern TX_FIFO_AAA_STRUCT tx_fifo_aaa;

typedef struct
{
    u8 type;
    u8 len;
    u8 buf[TX_BUF_SIZE_AAA - 2];
} TX_PACKET_AAA;

typedef struct
{
    u32 dongle_id; //4
	
    u8 key[12];//12
	
    u8 slave_mac_addr[4];//4
	
    u8 mode;
    u8 mast_id;
	u16 temp1; //24
	
	u8  temp2[8]; //32
		
} FLASH_DEV_INFO_AAA;
#define SAVE_MAX_IN_FLASH  32
_attribute_data_retention_user extern FLASH_DEV_INFO_AAA flash_dev_info;


typedef struct
{
    u32 bin_crc;
	
    u32 fw_version;

	u8 sensor_type;
    u8 sensor_pd1;
    u8 sensor_pd2;
    u8 sensor_pd3;
	
#if SHOW_MAST_REAL_MAC_DEBUG
    u8 master_mac[6];
#endif
} OUTPUT_DEV_INFO_AAA;
_attribute_data_retention_user extern OUTPUT_DEV_INFO_AAA output_dev_info;


typedef struct
{
    u8 bt_dpi;
#if DPI_SAVE_FLASH
    u8 d24g_dpi;
    u8 usb_dpi;
    u8 rev;
    s16 idx;
#endif
 	u16 rev1;// Round up 8 bytes
} FLASH_DPI_INFO_AAA;

_attribute_data_retention_user extern FLASH_DPI_INFO_AAA flash_dpi_info;




extern u8 d24g_ota_status;

extern u32 blt_ota_start_tick;
extern int bond_device_flash_cfg_idx;
extern u32 ota_program_offset;
extern unsigned int		ota_program_bootAddr;

extern	int	SMP_PARAM_NV_ADDR_START;


_attribute_data_retention_user	extern u8  user_devName[];

extern int		device_sync;





extern u8 keyboard_buf_aaa[BUF_SIZE_KEYBOARD_AAA];
extern u8 keyboard_buf_last_aaa[BUF_SIZE_KEYBOARD_AAA];
_attribute_data_retention_user extern kb_data_t_aaa key_buf_aaa ;
extern u32 mic_duration;







_attribute_data_retention_user extern u8 deep_flag;
_attribute_data_retention_user extern u8 pair_flag;
_attribute_data_retention_user extern u8 ana_reg1_aaa;
_attribute_data_retention_user extern u8 has_been_paired_flag;


_attribute_data_retention_user extern u8 d24g_power_on_Pair_flag;

_attribute_data_retention_user extern  u8 suspend_wake_up_enable;
_attribute_data_retention_user extern u8 has_new_key_event;

_attribute_data_retention_user extern u32 row_pins[];
_attribute_data_retention_user extern u32 col_pins[];
_attribute_data_retention_user extern u8 fun_mode;

_attribute_data_retention_user extern u32 btn_tick;
_attribute_data_retention_user extern u32 power_on_tick;
_attribute_data_retention_user extern u32 start_tick;

_attribute_data_retention_user extern u8 active_disconnect_reason;

_attribute_data_retention_user extern u8 ble_status_aaa;
_attribute_data_retention_user extern u8 mode_change_flag;


//extern ble_sts_t  blc_att_setServerDataPendingTime_upon_ClientCmd(u8 num_10ms);
extern void  blc_att_setServerDataPendingTime_upon_ClientCmd(u8 num_10ms);
extern _attribute_data_retention_user u8 encKey[];
//extern _attribute_data_retention_   ll_mac_t  bltMac;
extern _attribute_data_retention_user u8 	 my_batVal[1] ;
extern _attribute_data_retention_ u8 need_batt_data_notify;
extern _attribute_data_retention_ u8 batt_data_change;


_attribute_data_retention_user extern u32		advertise_begin_tick;
_attribute_data_retention_user extern int binding_master_addr_idx;
_attribute_data_retention_user extern u8 binding_master_addr[];

_attribute_data_retention_user extern mouse_data_t ms_data;
_attribute_data_retention_user extern mouse_data_t ms_buf;

_attribute_data_retention_user extern u8 mouse_btn_in_sensor;

_attribute_data_retention_user extern u8 has_new_report_aaa;
//_attribute_data_retention_user extern u8 combination_flag;

_attribute_data_retention_user extern u32 adv_begin_tick;
_attribute_data_retention_user extern u32 adv_count;

_attribute_data_retention_user extern u32 idle_tick;
_attribute_data_retention_user extern u32  idle_count;
_attribute_data_retention_user extern u32 loop_cnt;

_attribute_data_retention_user extern u16 adv_timer_count;
_attribute_data_retention_user extern u32 adv_led_interval;

_attribute_data_retention_user extern u16 btn_value;
_attribute_data_retention_user extern u16 last_btn_value;
extern int deepRetWakeUp;
_attribute_data_retention_user extern u8 device_status;
_attribute_data_retention_user extern u32 wakeup_next_tick;

extern u8 usb_mouse_report_proto ;

#if HAS_BACK_LIGHT_AAA
    _attribute_data_retention_user extern u8 back_light_switch_flag;

#endif
#if SENSOR_FUN_ENABLE_AAA
    _attribute_data_retention_user extern u8 sensor_fun;
#endif
#if BATT_CHECK_FUN_ENABLE
    _attribute_data_retention_user extern u8 low_bat_count;
#endif

_attribute_data_retention_user  extern u8 xy_multiple_flag;
_attribute_data_retention_user extern int dev_info_idx;

extern u16 led_pin;
extern u8 low_batt_flag;

extern u8 pc_status_through_ble;
extern u8 auto_draw_flag;

extern int pc_suspend_out_write_aaa(void* p);


/**
 * @brief       This function debug loop toggle
 * @return      
 * @note        
 */
extern void debug_loop_toggle();


/**
 * @brief       This function init user normal
 * @return      
 * @note        
 */
extern void user_init_normal();

/**
 * @brief       This function init deepret 
 * @return      
 * @note        
 */
extern void user_init_deepRetn();


/**
 * @brief       This function is ble main loop
 * @return      
 * @note        
 */
extern void main_loop(void);

/**
 * @brief       This function save data into flash
 * @return      
 * @note        
 */
extern void save_dev_info_flash();
extern int cccWrite(void *p);
extern int PC_enter_sleep(void *p);
extern int cccWrite1(void *p);



/**
 * @brief       This function push data into fifo
 * @param[in]   buf	- data buff
 * @param[in]   data_type	- data type
 * @param[in]   len	- data len
 * @return      
 * @note        
 */
u8 push_data_fifo_aaa(u8 *buf, u8 data_type, u8 len);


extern void ble_pair_process_aaa();

/**
 * @brief       This function clear pair flag
 * @return      
 * @note        
 */
extern void clear_pair_flag();

/**
 * @brief       This function clear fifo
 * @return      
 * @note        
 */
extern void clear_fifo();



extern void clear_fifo_d24g();

/**
 * @brief       This function check mode task
 * @return      
 * @note        
 */


/**
 * @brief       This function check gpio mode
 * @return      
 * @note        
 */
extern void mode_gpio_check();

extern void micdemo_mode_change_check();

/**
 * @brief       This function deal mouse task
 * @return      
 * @note        
 */
extern void mouse_task_when_rf();

/**
 * @brief       This function set pair flag
 * @return      
 * @note        
 */
extern void set_pair_flag();

/**
 * @brief       This function use for reboot chip
 * @param[in]   reason	- reboot reason
 * @return      
 * @note        
 */
extern void user_reboot(u8 reason);

/**
 * @brief       This function set pair and reboot
 * @return      
 * @note        
 */
extern void d24_start_pair();

/**
 * @brief       This function reset idle status parameter
 * @return      
 * @note        
 */
extern void reset_idle_status();

/**
 * @brief       This function poll idle parameters
 * @return      
 * @note        
 */
extern void idle_status_poll();

/**
 * @brief       This function poll adv parameters
 * @return      
 * @note        
 */

/**
 * @brief       This function  adv poll
 * @return      
 * @note        
 */
extern  void adv_count_poll();

extern void wheel_set_wakeup_level_deep();
/**
 * @brief       This function get wheel value
 * @param[in]   wheel_prepare_tick	- 
 * @return      
 * @note        
 */

/**
 * @brief       This function get wheel value
 * @param[in]   wheel_prepare_tick	- 
 * @return      
 * @note        
 */
extern _attribute_ram_code_ u8 wheel_get_value(u32 wheel_prepare_tick);
extern void sensor_gpio_powerDownConfig();
extern void	rf_power_enable(int en);

/**
 * @brief       This function adaptive smoother
 * @return      
 * @note        
 */
extern u8 adaptive_smoother();
extern void battery_check();
extern void battery_hw_init(void);
extern void ble_main_loop_aaa();
extern void ble_user_init_aaa();


/**
 * @brief       This function notify ble data
 * @return      
 * @note        
 */
extern void ble_notify_data_proc_aaa();

/**
 * @brief       This function scan btn
 * @return      
 * @note        
 */
extern  u16 btn_scan();

/**
 * @brief       This function get mouse btns value
 * @return      
 * @note        
 */
extern  u8 btn_get_value();

/**
 * @brief       This function set btn gpios wakeup deep
 * @return      
 * @note        
 */
extern void btn_set_wakeup_level_deep();
/**
 * @brief       This function set btn gpio wakeup
 * @param[in]   enable	- enable
 * @return      
 * @note        
 */
extern void btn_set_wakeup_level_suspend(u8 enable);

/**
 * @brief       This function set sensor motion gpio as wakeup gpio
 * @param[in]   enable	- 
 * @return      
 * @note        
 */
extern void sensor_set_wakeup_level_suspend(u8 enable);

/**
 * @brief       This function set sensor motion gpio as deepsleep wakeup gpio
 * @param[in]   enable	- 
 * @return      
 * @note        
 */
extern void sensor_set_wakeup_level_deepsleep(u8 enable);


/**
 * @brief       This function set wheel gpios as wake up gpios
 * @param[in]   enable	- en
 * @return      
 * @note        
 */
extern void wheel_set_wakeup_level_suspend(u8 enable);

extern void change_mode_aaa(u8 event_new);

/**
 * @brief       This function check fifo has data or not
 * @return      
 * @note        
 */
extern u8 check_fifo_has_data();

/**
 * @brief       This function check sensor dir
 * @param[in]    sensor_dir  - sensor dir
 * @return      
 * @note        
 */
extern void check_sensor_dircet(u8 sensor_dir);
extern void sensor_test();


/**
 * @brief       This function check press key
 * @return      
 * @note        
 */
extern u8 clear_press_key_aaa();

/**
 * @brief       This function is 24g main loop
 * @return      
 * @note        
 */
extern void d24_main_loop();

/**
 * @brief       This function is 24g user init
 * @return      
 * @note        
 */
extern void d24_user_init();
/**
 * @brief       This function let chip enter deep sleep 
 * @return      
 * @note        
 */
 extern void enter_deep_aaa();

extern void deep_wakeup_proc_aaa();
extern  _attribute_ram_code_ int device_send_packet(u8 *p, int retry, u16 rx_waittime);

/**
 * @brief       This function set btn dpi
 * @return      
 * @note        
 */
extern void btn_dpi_set();
extern void dpi_set(u8 dpi_value);


/**
 * @brief       This function set sensor dpi
 * @param[in]    dpi - sensor dpi
 * @return      
 * @note        
 */
extern void sensor_dpi_set(u8 dpi);




/**
 * @brief       This function proc emi
 * @return      
 * @note        
 */
extern void emi_process();

/**
 * @brief       This function load info from flash
 * @param[in]   d_addr	- the buff store readed data
 * @param[in]   len	- data len need read
 * @param[in]   s_addr	- the flash addr need read
 * @return      
 * @note        
 */
extern int flash_info_load_aaa(u32 s_addr, u8 *d_addr,  int len);
extern u8 get_ble_data_report_aaa();

extern void hw_init();
/**
 * @brief       This function init led hardware
 * @return      
 * @note        
 */
extern void led_hw_init();
extern void led_ota_ready_set();

extern void led_24g_ConnectedStatus();
extern void led_ble_ConnectedStatus();

extern void led_24g_mode_display();
extern void led_ble_mode_display();
/**
 * @brief       This function set led_bat_lvd mode
 * @return      
 * @note        
 */
extern void led_bat_lvd();
void led_power_off();
extern void led_ble_Adv_poll();
/**
 * @brief       This function set adv led
 * @return      
 * @note        
 */
extern void led_2p4_Adv_poll();
extern void dpi_led_set(u8 idx);


extern u32 mouse_wheel_prepare_tick(void);

extern _attribute_ram_code_ unsigned int OPTSensor_motion_report(u32 no_overflow);
extern void OPTSensor_Shutdown(void);
extern u8 Draw_a_square_test();


extern void delet_soft_time();
extern void ui_loop();
extern void add_soft_time();
extern void check_softe_time();
extern void ble_pm_aaa();
/**
 * @brief       This function check pair key
 * @param[in]   check_ms : the pair at least last time	- 
 * @return      
 * @note        
 */
extern void d24_check_pair_process();
extern void switch_type_poll();
extern void switch_type_init();

extern void user_batt_check_init();
/**
 * @brief       This function pro battery check
 * @return      
 * @note        
 */
extern void user_batt_check_proc();

extern void aes_ll_decryption(u8 *key, u8 *plaintext, u8 *result);
extern void swapX(const u8 *src, u8 *dst, int len);
extern void aes_user_encryption(u8 *key, u8 *plaintext, u8 *encrypted_data);
extern void aes_user_decryption(u8 *key, u8 *encrypted_data, u8 *decrypted_data);
extern  void aes_user_set_key();

extern void  double_click_left_24G_mode();

extern  int speckWrite(void *p);

/**
 * @brief       This function use for reboot chip
 * @param[in]   reason	- reboot reason
 * @return      
 * @note        
 */
extern  void write_deep_ana0(u8 buf);

extern _attribute_ram_code_ void irq_handle_usb_timer();
#if (APP_FLASH_LOCK_ENABLE)
extern _attribute_ram_code_ void flash_lock_init(void);
extern _attribute_ram_code_ void flash_unlock_init(void);
#endif
extern void battery_set_detect_enable(int en);

extern void d24g_ota_loop();

extern void app_enter_ota_mode(void);
extern void app_debug_ota_result(int result);

extern void usb_user_init();
extern void usb_update_init();
extern void usb_mode_loop();
extern void usb_update_loop();
extern void usb_init_process();
extern void usb_loop_process();
extern unsigned short crc16_user (unsigned char *pD, int len);
extern void usb_resume_host();
extern int usb_keyboard_hid_report_aaa(unsigned char *data);
extern int usb_mouse_hid_report_aaa(u8 report_id,unsigned char * p,u8 len);
extern int usb_app_hid_report(u8 report_id,u8 *data, u16 length);



extern void usb_init_interrupt(void);
extern void usb_handle_irq(void) ;
extern  void rf_stx_to_rx(u8 *p,u32 rx_timeout_us);

extern  void resume_data_fifo_aaa(u8 cnt,u8 rptr);
extern  void mouse_xy_multiple();


void blc_smp_set_simple_multi_mac_en(u8 en);
void notify_rsp_add2buf(u8 *p,u16 length);

u32 blc_smp_param_getCurStartAddr();
void usb_vbus_deepWakeup_enable();
int 		blt_ota_server_main_loop(void);
extern _attribute_ram_code_ void check_rf_fast_setting_flag();
extern _attribute_ram_code_ void check_rf_fast_setting_time();
extern int save_data_to_flash(unsigned long addr, unsigned long len, unsigned char *buf, int *offset);
#if((TL_AUDIO_MODE == TL_AUDIO_RCU_MSBC_HID))
extern void amic_gpio_reset (void);
extern void ui_enable_mic (int en);
extern _attribute_ram_code_ void task_audio (void);
extern void proc_audio(void);

#endif
extern void usb_custom_init(void);
#if(USB_MIC_ENABLE)
extern _attribute_ram_code_ void  usb_endpoints_irq_handler (void);
#endif
extern _attribute_ram_code_ void task_audio_usb(void);
extern void proc_audio_usb(void);
extern _attribute_ram_code_ void task_audio_ble(void);
extern void proc_audio_ble(void);
//extern void amic_gpio_reset (void);
//#define KB_NONE 0


extern int kb_led_out_write_aaa(void* p);





#endif

