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
//#include "vendor/common/blt_common.h"
//#include "../../application/usbstd/usbhw.h"
//#include "../../application/usbstd/usbhw_i.h"
#include "../../application/usbstd/stdDescriptors.h"
#include "../../application/audio/audio_common.h"
#include "../../application/audio/tl_audio.h"
#include"../../application/audio/audio_config.h"
#include "../../application/usbstd/usb.h"
#include "../../application/audio/adpcm.h"


extern my_fifo_t	d24g_txfifo;
#define TL_AUDIO_MODE  						TL_AUDIO_RCU_MSBC_HID 

#if APP_24G_AUDIO_EN

extern u8 usb_mouse_report_proto ;
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



extern u8 auto_draw_flag;
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
#define MS_BTN_VOICE    0X80
#define MS_BTN_CPI 		0X2000
#define MS_BTN_DOUBLE_LEFT 		0X100
#define MS_BTN_DEVICE_1 0x200
#define MS_BTN_DEVICE_2 0x400
#define MS_BTN_DEVICE_3 0x800
#define MS_BTN_DEVICE_4 0x1000
//#define MS_BTN_VOICE  0X2000
#define MS_BTN_TRANSLATE 0X8000


#if(MCU_CORE_TYPE == MCU_CORE_825x)
		#define DEFAULT_NORMAL_TX_POWER   RF_POWER_P8p13dBm
		#define DEFAULT_PAIR_TX_POWER	  RF_POWER_P0dBm
		#define DEFAULT_EMI_TX_POWER      RF_POWER_P0dBm
#elif(MCU_CORE_TYPE == MCU_CORE_827x)
	#define DEFAULT_NORMAL_TX_POWER   RF_POWER_P8p92dBm
	#define DEFAULT_PAIR_TX_POWER	  RF_POWER_P0dBm
	#define DEFAULT_EMI_TX_POWER      RF_POWER_P0dBm
#endif

//------------tx power define-------------------
//--------------------------------------------------

//----------------------------------------------------------------------------


typedef enum
{ 	
	IDLE = 0,
	USB_DEVICE_CONNECT_PC,
	USB_DEVICE_CHECK_PC_SLEEP,
	USB_DEVICE_DISCONECT_PC,//may be pc power off
	USB_DEVICE_UNPLUG,
}USB_DEVICE_STATUS;

typedef struct
{
    u32  dev_mac; //00
    u32  manufacturer_addr;//
    u8   bat_type;	//008
    u8	 tx_power;	//09
    u8	 paring_tx_power;//0a
    u8   emi_tx_power;   //00b
    u8	 wheel_direct;     //0c
    u8	 internal_cap;   //0d
    u8   sensor_direct;  //0e
    u8 	 aes_enable; //0f
    u16  rf_vid;//10-11
    u8   pub_key[16];        //12~21
    u8   device_name[18];	//22~33

} custom_cfg_t;
extern u8 report_rate;
extern u8  usb_device_status;
extern u8  usb_device_status_last;
extern u8  need_enter_suspend_flag;
extern u32 need_enter_suspend_tick;
extern u8 allow_mic_send;

_attribute_data_retention_user extern custom_cfg_t   user_cfg;
_attribute_data_retention_user extern  u8 pub_key[];
_attribute_data_retention_user extern  u8 device_name_len;
_attribute_data_retention_user extern u8	tbl_advData_aaa[];

 extern u8 sensor_type;
 extern u8 product_id1;
 extern u8 product_id2; 
 extern u8 product_id3;
  extern u8 connect_ok;
 extern u8 has_double_click_left;
 extern u8 pair_success_flag;
 extern volatile unsigned int rf_rx_timeout_us;
 extern u32 mic_duration;
 extern u8 device_switch_flag;

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
extern  u8	device_channel;
extern  u8 app_out_data_buf[];
extern  u8 app_len;
extern  u8 has_app_data_flag;
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
    RESET_PRODUCT_REBOOT_ANA_AAA=7,
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
   // MODE_CHANGE_TERMINATE_STATUS_AAA = 8,
   // CONN_PARAM_FAIL_TERMINATE_STATUS_AAA = 9,
   // MUTI_DEVICE_CHANGE_STATUS_AAA = 10,
} BLE_STATUS_AAA;
typedef enum
{
    KEY_PRESS_EVENT_AAA = BIT(0),
    NEW_KEY_EVENT_AAA = BIT(1),
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
#define BUF_SIZE_CONSUMER_AAA	1


#define TX_FIFO_NUM_AAA   16
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
extern u8 ic_inf[];


typedef struct
{
    u8 bt_dpi;
#if DPI_SAVE_FLASH
    u8 d24g_dpi;
    s16 idx;
#endif
} FLASH_DPI_INFO_AAA;

_attribute_data_retention_user extern FLASH_DPI_INFO_AAA flash_dpi_info;

extern device_led_t  ble_hw_led;
extern device_led_t  d24g_hw_led;
extern device_led_t  lvd_hw_led;
extern u32 ota_program_bootAddr;
extern u32 blt_ota_start_tick;
extern int bond_device_flash_cfg_idx;
extern u32 ota_program_offset;
extern int	SMP_PARAM_NV_ADDR_START;


_attribute_data_retention_user	extern u8  user_devName[];

extern int		device_sync;

_attribute_data_retention_user extern u8 deep_flag;
_attribute_data_retention_user extern u8 pair_flag;
_attribute_data_retention_user extern u8 ana_reg1_aaa;
_attribute_data_retention_user extern u8 has_been_paired_flag;


_attribute_data_retention_user extern u8 d24g_power_on_Pair_flag;

 extern  u8 suspend_wake_up_enable;
_attribute_data_retention_user extern u8 has_new_key_event;

_attribute_data_retention_user extern u32 row_pins[];
_attribute_data_retention_user extern u32 col_pins[];
_attribute_data_retention_user extern u8 fun_mode;

_attribute_data_retention_user extern u32 btn_tick;
_attribute_data_retention_user  extern u32 power_on_tick;
_attribute_data_retention_user extern u32 start_tick;

_attribute_data_retention_user extern u8 active_disconnect_reason;

_attribute_data_retention_user extern u8 ble_status_aaa;

//extern ble_sts_t  blc_att_setServerDataPendingTime_upon_ClientCmd(u8 num_10ms);
extern void  blc_att_setServerDataPendingTime_upon_ClientCmd(u8 num_10ms);
extern _attribute_data_retention_user u8 encKey[];
//extern _attribute_data_retention_   ll_mac_t  bltMac;
extern _attribute_data_retention_user u8 	 my_batVal[1] ;
extern _attribute_data_retention_ u8 need_batt_data_notify;


_attribute_data_retention_user extern u32		advertise_begin_tick;
_attribute_data_retention_user extern int binding_master_addr_idx;
_attribute_data_retention_user extern u8 binding_master_addr[];

_attribute_data_retention_user extern mouse_data_t ms_data;
_attribute_data_retention_user extern mouse_data_t ms_buf;

_attribute_data_retention_user extern u8 mouse_btn_in_sensor;

_attribute_data_retention_user extern u8 has_new_report_aaa;
//_attribute_data_retention_user extern u8 combination_flag;

extern u32 adv_begin_tick;
extern u32 adv_count;
extern u32 idle_tick;
extern u32  idle_count;
extern u32 loop_cnt;

_attribute_data_retention_user extern u16 adv_timer_count;
_attribute_data_retention_user extern u32 adv_led_interval;

_attribute_data_retention_user extern u16 btn_value;
_attribute_data_retention_user extern u16 last_btn_value;
extern int deepRetWakeUp;
_attribute_data_retention_user extern u8 device_status;
_attribute_data_retention_user extern u32 wakeup_next_tick;


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
extern volatile  u8 ok_auth_flag;
extern u8 pc_no_sleep_status;


extern void debug_loop_toggle();

extern void user_init_normal();
extern void user_init_deepRetn();

extern void main_loop(void);
extern void save_dev_info_flash();
extern int cccWrite(void *p);
extern int PC_enter_sleep(void *p);
extern int sppWrite(void *p);

u8 push_data_fifo_aaa(u8 *buf, u8 data_type, u8 len);


extern void ble_pair_process_aaa();
extern void clear_pair_flag();
extern void clear_fifo();

extern void set_pair_flag();
extern void user_reboot(u8 reason);

extern void d24_start_pair();
extern void reset_idle_status();
extern void idle_status_poll();
extern  void adv_count_poll();

extern void wheel_set_wakeup_level_deep();
extern _attribute_ram_code_ u8 wheel_get_value(u32 wheel_prepare_tick);
extern void sensor_gpio_powerDownConfig();
extern void	rf_power_enable(int en);
extern u8 adaptive_smoother();
extern void battery_check();
extern void battery_hw_init(void);
extern void ble_main_loop_aaa();
extern void ble_user_init_aaa();

extern void ble_notify_data_proc_aaa();
extern  u16 btn_scan();
extern  u8 btn_get_value();
extern void btn_set_wakeup_level_deep();
extern void btn_set_wakeup_level_suspend(u8 enable);
extern void sensor_set_wakeup_level_suspend(u8 enable);
extern void sensor_set_wakeup_level_deepsleep(u8 enable);

extern void wheel_set_wakeup_level_suspend(u8 enable);

extern void change_mode_aaa(u8 event_new);
extern u8 check_fifo_has_data();
extern void check_sensor_dircet(u8 sensor_dir);

extern u8 clear_press_key_aaa();
extern void d24_main_loop();
extern void d24_user_init();
extern void enter_deep_aaa();

extern void deep_wakeup_proc_aaa();
extern  _attribute_ram_code_ int device_send_packet(u8 *p, int retry, u16 rx_waittime);
extern void btn_dpi_set();
extern void sensor_dpi_set(u8 dpi);



extern void emi_process();

extern int flash_info_load_aaa(u32 s_addr, u8 *d_addr,  int len);
extern u8 get_ble_data_report_aaa();
extern void get_24g_data_report_aaa();
extern void hw_init();
extern void led_hw_init();

extern void led_24g_ConnectedStatus();
extern void led_ble_ConnectedStatus();

extern void led_24g_mode_display();
extern void led_ble_mode_display();
extern void led_bat_lvd();
void led_power_off();
extern void led_ble_Adv_poll();
extern void led_2p4_Adv_poll();
extern void dpi_led_set(u8 idx);
extern u32 mouse_wheel_prepare_tick(void);

extern _attribute_ram_code_ unsigned int OPTSensor_motion_report(u32 no_overflow);
extern void OPTSensor_Shutdown(void);
extern u8 Draw_a_square_test();
void sensor_check_lift(u8 en);


extern void delet_soft_time();
extern void ui_loop();
extern void add_soft_time();
extern void check_softe_time();
extern void ble_pm_aaa();
extern void d24_check_pair_process();
extern void switch_type_poll();
extern void switch_type_init();

extern void user_batt_check_init();
extern void user_batt_check_proc();

extern void aes_ll_decryption(u8 *key, u8 *plaintext, u8 *result);
extern void swapX(const u8 *src, u8 *dst, int len);
extern void aes_user_encryption(u8 *key, u8 *plaintext, u8 *encrypted_data);
extern void aes_user_decryption(u8 *key, u8 *encrypted_data, u8 *decrypted_data);
extern  void aes_user_set_key();

extern void  double_click_left_24G_mode();

extern  int speckWrite(void *p);
extern  void write_deep_ana0(u8 buf);
extern _attribute_ram_code_ void rf_stx_to_rx(u8 *p,u32 rx_timeout_us);
extern _attribute_ram_code_ void check_rf_fast_setting_time();

#if((TL_AUDIO_MODE == TL_AUDIO_RCU_MSBC_HID))
extern void amic_gpio_reset (void);
extern void ui_enable_mic (int en);
extern _attribute_ram_code_ void task_audio (void);
extern void proc_audio(void);

#endif
extern void mouse_task_when_rf();
extern void flash_write_page_user(unsigned long addr, unsigned long len, unsigned char *buf);
extern void app_enter_ota_mode(void);
extern void app_debug_ota_result(int result);
extern void usb_custom_init(void);
#if(USB_MIC_ENABLE)
extern _attribute_ram_code_ void  usb_endpoints_irq_handler (void);
#endif
extern _attribute_ram_code_ void task_audio_usb(void);
extern void proc_audio_usb(void);
extern void usb_resume_host();
extern int usb_keyboard_hid_report_aaa(unsigned char *data);
extern int usb_mouse_hid_report_aaa(u8 report_id,unsigned char * p,u8 len);
extern int usb_app_hid_report(u8 report_id,u8 *data, u16 length);
extern void usb_user_init();
extern void usb_update_init();
extern void usb_mode_loop();
extern void usb_update_loop();

extern void three_mode_init();
extern void three_mode_change_handle();


extern void usb_init_interrupt(void);
extern void usb_handle_irq(void) ;

extern void proc_audio_ble(void);



#endif

