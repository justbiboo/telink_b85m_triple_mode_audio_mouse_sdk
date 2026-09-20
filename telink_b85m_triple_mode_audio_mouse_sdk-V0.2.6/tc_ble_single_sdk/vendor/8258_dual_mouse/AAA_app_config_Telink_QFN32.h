#ifndef _TELINK_QFN32_AAA_H_
#define _TELINK_QFN32_AAA_H_

#if (PRJ_NAME==Telink_QFN32_PRJ)
	#define ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG    0
    //-----------------------------for debug-----------------------------
    #define BOARD1_EN            0
	#define TELINK_BOARD         1
	#define HID_REPORT_MIC_EN    0
	#define TEST_LOST_RATE		 0   //test 2.4g rf lost rate
	
	#define  BLE_SNIFF_DEBUG    0   //for ti packet sniff   if enable mouse do not enter sleep for ever
    #define  BLE_OTA_LED_DEBUG   1   //led show  ota result
    #define DEBUG_GPIO_AAA		 0  //
    #define TEST_DRAW_A_SQUARE	 (ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG||0)   //when mouse  connected auto draw  a square
    
    #define AUDIO_AUTO_OPEN_TEST_DEBUG  0 //823x not support
    #define DEVICE_NAME_INCLUDE_MAC_DEBUG 0 //for debug
    #define MUTI_DEVICE_SWITCH_DEBUG     1//for debug

    #define ADC_BATT_DEBUG   0  //enbale : in ble mode £¬notify adc value.

    // test  mcu current ,in the mode,sensor shut down ,
    // when both left and right  button are pressed £¬mouse auto draw a square in conencted
    #define TEST_MCU_CURRENT_DEBUG       0

    #define SHOW_MAST_REAL_MAC_DEBUG    0//for test get master real addr

    #define DEBUG_TOGGLE_GPIO_ENABLE    0
    //-----------------------------PM --------------------------------

    #define BLE_APP_PM_ENABLE					1
    #define PM_DEEPSLEEP_RETENTION_ENABLE		0//must set 0
    #define BLE_ADV_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA      ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)
    #define BLE_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA  ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)


    #define D24G_ADV_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA     ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)
    #define D24G_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA  ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)


    #define BLE_ADV_TIMER_OUT  60 //unit 1s
    #define D24G_ADV_TIMER_OUT  60 //unit 1s

    #define BLE_CONNECT_TIME_OUT  600 //unit 1s
    #define D24G_CONNECT_TIME_OUT  600 //unit 1s
    #define D24G_PAIR_TIMER_OUT 			350 //unit 1us
	#define D24G_COMMUNICATION_TIMER_OUT	300 //unit 1us

	#define KEY_PRESS_HOLD_CHECK_TIME		980 //unit 1ms
    #if PM_DEEPSLEEP_RETENTION_ENABLE
        #define _attribute_data_retention_user    _attribute_data_retention_
    #else
        #define _attribute_data_retention_user
    #endif

    //-----------------------product function referance---------------------------------------

	#define DEVICE_APPEARANCE      0X03C2// 0X0XC1 KEYBOARD 0X03C2 MOUSE
    #define DEVICE_NAME_AAA   "AI_58M"//"58_T_M"//58:8258  32: QFN32

    #define MOUSE_PIPE1_DATA_WITH_DID	1//If you want to fit the old hamster dongle (firmware version :v5.3) it must set 0

    #define MOUSE_DATA_LEN_AAA	6   //If you want to fit the old hamster dongle (firmware version :v5.3)  it must set 4

    //#define OTA_ENABLE_AAA     1
    #define BLE_OTA_SERVER_ENABLE  1

    #define EMI_TEST_FUN_ENABLE_AAA   ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)//If the flash  is 128k, must be set 0£¬save 2k

    #define BATT_CHECK_ENABLE     ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)

    #define WHEEL_FUN_ENABLE_AAA   ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)

    #define BUTTON_FUN_ENABLE_AAA   ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)

    #define SENSOR_FUN_ENABLE_AAA  ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)
    #define SENSOR_MOTION_ENABLE   TELINK_BOARD//0//1
    #define SENSOR_CS_ENABLE       0
    #define SENSOR_SHUT_DOWN_ENABLE 0

    #define BLT_APP_LED_ENABLE   ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)
    #define	LED_OFF_AAA	1
    #define LED_ON_AAA	0
	
//	#define KEYBOARD_FUN_ENABLE_AAA 1


    #define DIRECT_ADV_TO_UNDIRECT_ENABLE  0

    #define DOUBLE_CLICK_LEFT_FUN_ENABLE    0
	#define APP_24G_AUDIO_EN     1 //24G audio
    #define BLE_AUDIO_ENABLE     0
    #define AUTO_CHECK_OS_TYPE   0
    #define Microsoft_Swift_Pairing_ENABLE 1//if enable then device_name_len<=11

    #define	BLT_SOFTWARE_TIMER_ENABLE	0

    #define MUTI_SENSOR_ENABLE  1
    #define DPI_SAVE_FLASH      1
	#define ENTER_PAIR_WHEN_NEVER_PAIRED_ENABLE 1//For the better production of the factory

    // aes method
    //0:no aes encryption   no key					old method
    //1:aes128 encryption   User burn fixed key   	old method  not used
    //2:aes128 encryption	Dynamic random key
    //3:aes128 decryption	Dynamic random key  for 8366 dongle
    
   // If the dongle  is 8366, it is recommended to set it to 0 or 3
   // if it is set to 3 , the algorithm will consume less time for 8366
   //If the dongle  is 8355, it can be set to any value
   //attention :Keyboard, mouse and receiver must have the same parameters
    #define AES_METHOD 0  

	#define HOPING_METHOD  1
	//------------------------usb ----------------------------------------
 #define MODULE_USB_ENABLE  1     //mouse usb en

#if(MODULE_USB_ENABLE) //en
	#define	FLOW_NO_OS							1  //1
	#define	USB_PRINTER_ENABLE 					0  //printer
	#define	USB_MOUSE_ENABLE					1 //mouse
	#define	USB_KEYBOARD_ENABLE					1 //keyborad
    #define	USB_MIC_ENABLE						0 //mic
	#define	USB_SPEAKER_ENABLE					0 //speaker
	#define	USB_CDC_ENABLE						0 //cdc
	#define	USB_SOMATIC_ENABLE					0		//  when USB_SOMATIC_ENABLE, USB_EDP_PRINTER_OUT disable
	#define	USB_CUSTOM_HID_REPORT				1 //coustom hid report
	#define	USB_CUSTOM_HID_REPORT_REG_ACCESS	0 //custom hid report reg access
	#define	USB_DESCRIPTOR_MY_SELF				0 //usb descriptor
	#define USB_APP_ENABLE      (!USB_MIC_ENABLE)
	#define	USB_UPGRATE_OTA		(!USB_MIC_ENABLE)	//USB-OTA
	#define MIC_RESOLUTION_BIT		16
	#define MIC_SAMPLE_RATE			16000//set sample for mic and spk
	#define MIC_CHANNLE_COUNT		1
	#define	MIC_ENOCDER_ENABLE		0

	#define SPK_RESOLUTION_BIT		16

//	#define PIN_USB_INSERT			GPIO_PB1	//39a30_v1.0 j4 S-tx
   //	#define PB1_INPUT_ENABLE		1 //input en
  	//#define PB1_OUTPUT_ENABLE		0 //output dis
	//#define PB1_DATA_OUT			0 //out data 0
	//#define	PULL_WAKEUP_SRC_PB1		PM_PIN_PULLDOWN_100K //pull down 100k
	
#endif
    //---------------------------gpio---------------------------------------------
    #if SENSOR_FUN_ENABLE_AAA
	#if(BOARD1_EN||TELINK_BOARD)
	//#if(TELINK_BOARD)
	#define PIN_SIF_MOTION		GPIO_PA0	//39a30_v1.0 j4 S-tx
	#define PA0_INPUT_ENABLE	1
	#define PA0_OUTPUT_ENABLE	0
	#define PA0_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PA0 PM_PIN_PULLUP_1M
	//#endif
	#define PIN_SIF_SCL 		GPIO_PD7	//39a30_v1.0  j15
	#define PD7_INPUT_ENABLE	0
	#define PD7_OUTPUT_ENABLE	1
	#define PD7_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PD7 PM_PIN_PULLUP_1M

	#define PIN_SIF_SDA 	GPIO_PD4		//39a30_v1.0  j15
	#define PD4_INPUT_ENABLE	1
	#define PD4_OUTPUT_ENABLE	1
	#define PD4_DATA_OUT	1
	#define PULL_WAKEUP_SRC_PD4 PM_PIN_PULLUP_10K
	#else
	#define PIN_SIF_MOTION		GPIO_PA1	//39a30_v1.0 j4 S-tx
	#define PA1_INPUT_ENABLE	1
	#define PA1_OUTPUT_ENABLE	0
	#define PA1_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PA1 PM_PIN_PULLUP_1M

	#define PIN_SIF_SCL 		GPIO_PB1	//39a30_v1.0  j15
	#define PB1_INPUT_ENABLE	0
	#define PB1_OUTPUT_ENABLE	1
	#define PB1_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PB1 PM_PIN_PULLUP_1M

	#define PIN_SIF_SDA 	GPIO_PA0		//39a30_v1.0  j15
	#define PA0_INPUT_ENABLE	1
	#define PA0_OUTPUT_ENABLE	1
	#define PA0_DATA_OUT	1
	#define PULL_WAKEUP_SRC_PA0 PM_PIN_PULLUP_10K
	#endif


        #if SENSOR_CS_ENABLE
            #define PIN_SENSOR_CS   GPIO_PA7//39a30_v1.0  SWS
            #define PA7_INPUT_ENABLE	0
            #define PA7_OUTPUT_ENABLE	1
            #define PA7_DATA_OUT	1
            #define PULL_WAKEUP_SRC_PA7 PM_PIN_PULLUP_1M
        #endif

    #endif

    #if BUTTON_FUN_ENABLE_AAA
#if(0)
        #define TOTAL_COL   2
		#define TOTAL_ROW   2
        #define  GPIO_ROW_PIN	{GPIO_PB4, GPIO_PB5}
        #define  GPIO_COL_PIN	{GPIO_PB2, GPIO_PB3}
				   
				   //drive pin as gpio
        #define PB4_FUNC				AS_GPIO
        #define PB5_FUNC				AS_GPIO
				   
				   //drive pin need 100K pulldown
        #define PULL_WAKEUP_SRC_PB4 	MATRIX_ROW_PULL
        #define PULL_WAKEUP_SRC_PB5 	MATRIX_ROW_PULL
				   
				   //drive pin open input to read gpio wakeup level
        #define PB4_INPUT_ENABLE		1
        #define PB5_INPUT_ENABLE		1
				   
				   //scan pin as gpio
        #define PB2_FUNC				AS_GPIO
        #define PB3_FUNC				AS_GPIO
				   
				   //scan  pin need 10K pullup
        #define PULL_WAKEUP_SRC_PB2 	MATRIX_COL_PULL
        #define PULL_WAKEUP_SRC_PB3 	MATRIX_COL_PULL
				   
				   //scan pin open input to read gpio level
        #define PB2_INPUT_ENABLE		1
        #define PB3_INPUT_ENABLE		1
		#define BTN_PM_WAKEUP_SRC   PM_PIN_PULLUP_1M
#elif(BOARD1_EN)
        #define BTN_NUM_AAA  4
		#define BTN_PM_WAKEUP_SRC   PM_PIN_PULLUP_1M
		
		#define PIN_BTN_RIGHT  		GPIO_PD2
        #define PD2_INPUT_ENABLE	1
        #define PD2_OUTPUT_ENABLE	0
        #define PD2_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PD2 BTN_PM_WAKEUP_SRC
		
        #define PIN_BTN_LEFT  		GPIO_PA3
        #define PA3_INPUT_ENABLE	1
        #define PA3_OUTPUT_ENABLE	0
        #define PA3_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PA3 BTN_PM_WAKEUP_SRC
		
        #define PIN_BTN_MIDDLE  	GPIO_PD5
        #define PD5_INPUT_ENABLE	1
        #define PD5_OUTPUT_ENABLE	0
        #define PD5_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PD5 BTN_PM_WAKEUP_SRC

		#define PIN_BTN_CPI  	GPIO_PB5
        #define PB5_INPUT_ENABLE	1
        #define PB5_OUTPUT_ENABLE	0
        #define PB5_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PB5 BTN_PM_WAKEUP_SRC

		#define PIN_BTN_C0  	GPIO_PA2
        #define PA2_INPUT_ENABLE	0
        #define PA2_OUTPUT_ENABLE	1
        #define PA2_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PA2 PM_PIN_PULLUP_10K
		
		

		#define PIN_MODE_SWITCH      GPIO_PC7
		#define PC7_FUNC    AS_GPIO
        #define PC7_INPUT_ENABLE	1
      	#define PC7_OUTPUT_ENABLE	0
        #define PC7_DATA_OUT	1
        #define PULL_WAKEUP_SRC_PC7 BTN_PM_WAKEUP_SRC
		#define IS_BLE_MODE_AAA  (gpio_read(PIN_MODE_SWITCH)!=0)
		#define BTN_MATRIX {PIN_BTN_LEFT,PIN_BTN_RIGHT,PIN_BTN_MIDDLE,PIN_MODE_SWITCH}
#elif(TELINK_BOARD)
		#define BTN_NUM_AAA  6
		#define BTN_PM_WAKEUP_SRC   PM_PIN_PULLUP_1M
		#define PIN_BTN_RIGHT  		GPIO_PD2
        #define PD2_INPUT_ENABLE	1
        #define PD2_OUTPUT_ENABLE	0
        #define PD2_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PD2 BTN_PM_WAKEUP_SRC
		
        #define PIN_BTN_LEFT  		GPIO_PA2
        #define PA2_INPUT_ENABLE	1
        #define PA2_OUTPUT_ENABLE	0
        #define PA2_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PA2 BTN_PM_WAKEUP_SRC
		
        #define PIN_BTN_MIDDLE  	GPIO_PD5
        #define PD5_INPUT_ENABLE	1
        #define PD5_OUTPUT_ENABLE	0
        #define PD5_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PD5 BTN_PM_WAKEUP_SRC

		#define PIN_BTN_K4 	GPIO_PA3
        #define PA3_INPUT_ENABLE	1
        #define PA3_OUTPUT_ENABLE	0
        #define PA3_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PA3 BTN_PM_WAKEUP_SRC

		#define PIN_BTN_K5 	GPIO_PA4
        #define PA4_INPUT_ENABLE	1
        #define PA4_OUTPUT_ENABLE	0
        #define PA4_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PA4 BTN_PM_WAKEUP_SRC

		#define PIN_BTN_CPI  	GPIO_PB5
        #define PB5_INPUT_ENABLE	1
        #define PB5_OUTPUT_ENABLE	0
        #define PB5_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PB5 BTN_PM_WAKEUP_SRC
		
		#define PIN_MODE_BLE      GPIO_PD0
        #define PD0_INPUT_ENABLE	1
      	#define PD0_OUTPUT_ENABLE	0
        #define PD0_DATA_OUT	0
        #define PULL_WAKEUP_SRC_PD0 PM_PIN_UP_DOWN_FLOAT
		//#define IS_BLE_MODE_AAA  (gpio_read(PIN_MODE_SWITCH)==0)

		#define PD0_FUNC    AS_GPIO
		#define IS_BLE_MODE_AAA  (gpio_read(PIN_MODE_BLE)!=0)

		#define PIN_MODE_24G	  GPIO_PD1
		#define PD1_INPUT_ENABLE	1
		#define PD1_OUTPUT_ENABLE	0
		#define PD1_DATA_OUT	0
		#define PULL_WAKEUP_SRC_PD1 PM_PIN_UP_DOWN_FLOAT
		#define PD1_FUNC	AS_GPIO
		#define IS_24_MODE (gpio_read(PIN_MODE_24G)!=0)

		#define PIN_5V_DET              GPIO_PC5
		#define PC5_FUNC                AS_GPIO
		#define PC5_INPUT_ENABLE		1
		#define PC5_OUTPUT_ENABLE		0
		#define PC5_DATA_OUT			0
		#define PULL_WAKEUP_SRC_PC5 	PM_PIN_UP_DOWN_FLOAT
		#define PC5_FUNC	AS_GPIO

		
        #define BTN_MATRIX {PIN_BTN_LEFT,PIN_BTN_RIGHT,PIN_BTN_MIDDLE,PIN_BTN_K4,PIN_BTN_K5,PIN_BTN_CPI}
		
#else
        #define BTN_NUM_AAA  4

        #define BTN_PM_WAKEUP_SRC   PM_PIN_PULLUP_1M

        #define PIN_BTN_LEFT  		GPIO_PD2
        #define PD2_INPUT_ENABLE	1
        #define PD2_OUTPUT_ENABLE	0
        #define PD2_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PD2 BTN_PM_WAKEUP_SRC

        #define PIN_BTN_RIGHT  		GPIO_PD3
        #define PD3_INPUT_ENABLE	1
        #define PD3_OUTPUT_ENABLE	0
        #define PD3_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PD3 BTN_PM_WAKEUP_SRC

        #define PIN_BTN_MIDDLE  	GPIO_PB5
        #define PB5_INPUT_ENABLE	1
        #define PB5_OUTPUT_ENABLE	0
        #define PB5_DATA_OUT		1
        #define PULL_WAKEUP_SRC_PB5 BTN_PM_WAKEUP_SRC

		#define PIN_MODE_SWITCH      GPIO_PB6
        #define PB6_INPUT_ENABLE	1
      	#define PB6_OUTPUT_ENABLE	0
        #define PB6_DATA_OUT	1
        #define PULL_WAKEUP_SRC_PB6 BTN_PM_WAKEUP_SRC
		//#define IS_BLE_MODE_AAA  (gpio_read(PIN_MODE_SWITCH)==0)

		#define PB6_FUNC    AS_GPIO
		#define IS_BLE_MODE_AAA  (gpio_read(PIN_MODE_SWITCH)!=0)
        #define BTN_MATRIX {PIN_BTN_LEFT,PIN_BTN_RIGHT,PIN_BTN_MIDDLE,PIN_MODE_SWITCH}
#endif
    #endif

    #if WHEEL_FUN_ENABLE_AAA
	#if(BOARD1_EN||TELINK_BOARD)
	#define PIN_WHEEL_2 		GPIO_PC3		//39a30_v1.0 j15
	#define PC3_INPUT_ENABLE	1
	#define PC3_OUTPUT_ENABLE	0
	#define PC3_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PC3 PM_PIN_PULLUP_1M

	#define PIN_WHEEL_1 		GPIO_PC2		//39a30_v1.0 j22 d7
	#define PC2_INPUT_ENABLE	1
	#define PC2_OUTPUT_ENABLE	0
	#define PC2_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PC2 PM_PIN_PULLUP_1M

	#define WHEEL_ADDRES_D2   0X04
	#define WHEEL_ADDRES_D3   0X05
	#else
	#define PIN_WHEEL_1 		GPIO_PB7		//39a30_v1.0 j15
	#define PB7_INPUT_ENABLE	1
	#define PB7_OUTPUT_ENABLE	0
	#define PB7_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PB7 PM_PIN_PULLUP_1M

	#define PIN_WHEEL_2 		GPIO_PD7		//39a30_v1.0 j22 d7
	#define PD7_INPUT_ENABLE	1
	#define PD7_OUTPUT_ENABLE	0
	#define PD7_DATA_OUT		1
	#define PULL_WAKEUP_SRC_PD7 PM_PIN_PULLUP_1M

	#define WHEEL_ADDRES_D2   0X03
	#define WHEEL_ADDRES_D3   0X07
	#endif
    #endif


    #if BLT_APP_LED_ENABLE

	#if(BOARD1_EN||TELINK_BOARD)
		#define PIN_24G_LED 		GPIO_PD6
		#define PD6_INPUT_ENABLE	0
		#define PD6_OUTPUT_ENABLE	1
		#define PD6_DATA_OUT		LED_OFF_AAA
		#define PULL_WAKEUP_SRC_PD6 PM_PIN_UP_DOWN_FLOAT
		
		#define PIN_BLE_LED 		GPIO_PA7
		#define PA7_INPUT_ENABLE	0
		#define PA7_OUTPUT_ENABLE	1
		#define PA7_DATA_OUT		LED_OFF_AAA
		#define PULL_WAKEUP_SRC_PA7 PM_PIN_UP_DOWN_FLOAT
        #define PA7_FUNC    AS_GPIO

        
		#define PIN_USB_LED 		GPIO_PB4
		#define PB4_INPUT_ENABLE	0
		#define PB4_OUTPUT_ENABLE	1
		#define PB4_DATA_OUT		LED_OFF_AAA
		#define PULL_WAKEUP_SRC_PB4 PM_PIN_UP_DOWN_FLOAT
        #define PB4_FUNC    AS_GPIO
	#else
		#define PIN_24G_LED 		GPIO_PC3
		#define PC3_INPUT_ENABLE	0
		#define PC3_OUTPUT_ENABLE	1
		#define PC3_DATA_OUT		LED_OFF_AAA
		#define PULL_WAKEUP_SRC_PC3 PM_PIN_UP_DOWN_FLOAT
		
		#define PIN_BLE_LED 		GPIO_PC2
		#define PC2_INPUT_ENABLE	0
		#define PC2_OUTPUT_ENABLE	1
		#define PC2_DATA_OUT		LED_OFF_AAA
		#define PULL_WAKEUP_SRC_PC2 PM_PIN_UP_DOWN_FLOAT
        #define PC2_FUNC    AS_GPIO
	#endif

    #endif


    #if (BATT_CHECK_ENABLE)
        //telink device: you must choose one gpio with adc function to output high level(voltage will equal to vbat), then use adc to measure high level voltage
        //use PB7 output high level, then adc measure this high level voltage
        #if(TELINK_BOARD)
		#define PIN_VBAT_DET_EN 		GPIO_PB0
		#define PB0_INPUT_ENABLE	0
		#define PB0_OUTPUT_ENABLE	1
		#define PB0_DATA_OUT		1
		#define PULL_WAKEUP_SRC_PB0 PM_PIN_UP_DOWN_FLOAT
        #define PB0_FUNC    AS_GPIO
		
		#define GPIO_VBAT_DETECT				GPIO_PB1
        #define PB1_FUNC						AS_GPIO
        #define PB1_INPUT_ENABLE				0
        #define ADC_INPUT_PCHN					B1P   
		#else
		#define GPIO_VBAT_DETECT				GPIO_PB4
        #define PB4_FUNC						AS_GPIO
        #define PB4_INPUT_ENABLE				0
        #define ADC_INPUT_PCHN					B4P      //corresponding  ADC_InputPchTypeDef in adc.h
        #endif
    #endif

    ////////////////////////// AUDIO CONFIG (RCU board) /////////////////////////////
    #if (BLE_AUDIO_ENABLE)
        #define BLE_DMIC_ENABLE					0  //0: Amic   1: Dmic
        #define	ADPCM_PACKET_LEN				128
        #define TL_MIC_ADPCM_UNIT_SIZE			248

        #define	TL_MIC_BUFFER_SIZE				992

        #define GPIO_AMIC_BIAS					GPIO_PC4

    #endif
	#if(APP_24G_AUDIO_EN )
		#define BLE_DMIC_ENABLE					0
		//#define	ADPCM_PACKET_LEN				128
		//#define MIC_SHORT_DEC_SIZE				248
		//#define	TL_MIC_BUFFER_SIZE				992
		//#define GPIO_AMIC_BIAS					GPIO_PC0//GPIO_PC0// need check ,v1.0 PC4, V1.1 PC0
		//#define GPIO_AMIC_SP					GPIO_PC1
		#define GPIO_AMIC_BIAS					GPIO_PC4
		#define GPIO_AMIC_SP					GPIO_PC0
		#define GPIO_AMIC_SN					GPIO_PC1

		#define TL_AUDIO_MODE  					TL_AUDIO_RCU_MSBC_HID//TL_AUDIO_RCU_ADPCM_GATT_TLEINK//TL_AUDIO_RCU_ADPCM_GATT_TLEINK
	#if(1)
		#if(TL_AUDIO_MODE == TL_AUDIO_RCU_ADPCM_GATT_TLEINK )
		//#define	ADPCM_PACKET_LEN				128
		//#define MIC_SHORT_DEC_SIZE				248
		//#define	TL_MIC_BUFFER_SIZE				992
		#endif
	#endif
	#endif

#endif

#endif
