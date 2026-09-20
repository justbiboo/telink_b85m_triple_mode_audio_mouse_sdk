#ifndef _TELINK_QFN32_AAA_H_
#define _TELINK_QFN32_AAA_H_

#if (PRJ_NAME==Telink_QFN32_PRJ)
	#define APP_FLASH_LOCK_ENABLE						 0

    #define ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG    0
	
    //-----------------------------for debug-----------------------------
    #define	BLE_SNIFF_DEBUG		0   //for ti packet sniff   if enable mouse do not enter sleep for ever
    #define	BLE_OTA_LED_DEBUG	1   //led show  ota result
    #define DEBUG_GPIO_AAA		0  //ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG   //
    #define TEST_DRAW_A_SQUARE	1   //when mouse  connected auto draw  a square
    #define TEST_LOST_RATE		0   //test 2.4g rf lost rate
    #define AUDIO_AUTO_OPEN_TEST_DEBUG		0 //823x not support
    #define DEVICE_NAME_INCLUDE_MAC_DEBUG	0 //for debug
    #define MUTI_DEVICE_SWITCH_DEBUG		0 //for debug

    #define ADC_BATT_DEBUG   0  //enbale : in ble mode £¬notify adc value.

    // test  mcu current ,in the mode,sensor shut down ,
    // when both left and right  button are pressed £¬mouse auto draw a square in conencted
    #define TEST_MCU_CURRENT_DEBUG		0

    #define SHOW_MAST_REAL_MAC_DEBUG    0//for test get master real addr

    #define DEBUG_TOGGLE_GPIO_ENABLE    0

    #define FREQUENY_HOPPING_1K			1  //0 for 8366 dongle  only 125 report rate  1: for 8355 1k report rate
	
    //-----------------------------PM --------------------------------
    #define BLE_APP_PM_ENABLE					1
    #define PM_DEEPSLEEP_RETENTION_ENABLE		0//must set 0
    #define BLE_ADV_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA      1
    #define BLE_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA  1


    #define D24G_ADV_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA     ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)
    #define D24G_CONNECT_ENTER_DEEPSLEEP_AFTER_TIME_OUT_ENABLE_AAA  ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1)

    #define BLE_ADV_TIMER_OUT  	60 //unit 1s
    #define D24G_ADV_TIMER_OUT  60 //unit 1s

    #define BLE_CONNECT_TIME_OUT  	3*600 //unit 1s
    #define D24G_CONNECT_TIME_OUT	600 //unit 1s
	#define KEY_PAIR_PRESS_HOLD_CHECK_TIME			2100
	
#if (AES_METHOD == 1 || DATA_3_CHOOSE_1_ENABLE == 1)
	#define D24G_PAIR_TIMER_OUT 			400 //unit 1us
	#define D24G_COMMUNICATION_TIMER_OUT	300 //unit 1us
#else
	#define D24G_PAIR_TIMER_OUT 			350 //unit 1us
	#define D24G_COMMUNICATION_TIMER_OUT	250//140 //unit 1us
#endif
	#define D24G_OTA_TIMER_OUT 				500 //unit 1us

	#define KEY_PRESS_HOLD_CHECK_TIME		980 //unit 1ms

    #if PM_DEEPSLEEP_RETENTION_ENABLE
        #define _attribute_data_retention_user    _attribute_data_retention_
    #else
        #define _attribute_data_retention_user
    #endif
	
    //-----------------------product function referance---------------------------------------
	#define  KAIFABAN_EN             0
	#define MICDEMO            1
    //#define DEVICE_NAME_AAA   "SC_T_M_"//58:8258  32: QFN32
	#define DEVICE_NAME_AAA   "iMouse"
    #define MOUSE_PIPE1_DATA_WITH_DID	0//If you want to fit the old hamster dongle (firmware version :v5.3) it must set 0

    #define MOUSE_DATA_LEN_AAA	6  //If you want to fit the old hamster dongle (firmware version :v5.3)  it must set 4

    #define BLE_OTA_ENABLE_AAA	0 //BLE-OTA Enable
    #define BLE_OTA_SERVER_ENABLE 1

	#define D24G_OTA_ENABLE_AAA	1  //2.4G-OTA Enable

    #define EMI_TEST_FUN_ENABLE_AAA   ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1&&(!TEST_DRAW_A_SQUARE))//If the flash  is 128k, must be set 0£¬save 2k

    #define BATT_CHECK_ENABLE     !KAIFABAN_EN//((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1&&(!TEST_DRAW_A_SQUARE)) //batt check

    #define WHEEL_FUN_ENABLE_AAA   !KAIFABAN_EN//((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1&&(!TEST_DRAW_A_SQUARE)) //wheel fun

    #define BUTTON_FUN_ENABLE_AAA  1// ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1&&(!TEST_DRAW_A_SQUARE)) //btn en

    #define SENSOR_FUN_ENABLE_AAA  (!KAIFABAN_EN)// ((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1&&(!TEST_DRAW_A_SQUARE)) //sensor en
    #define SENSOR_CS_ENABLE       1 //sensor cs en
    #define SENSOR_MOTION_ENABLE   1
    #define SENSOR_SHUT_DOWN_ENABLE 1 //sensor shut down en
	#define AUTO_DRAW_EN 0
    #define BLT_APP_LED_ENABLE   !KAIFABAN_EN//((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1&&(!TEST_DRAW_A_SQUARE)) //led en
    #define	LED_OFF_AAA	1 //led off
    #define LED_ON_AAA	0 //led on

    #define IS_SINGLE_GPIO_CHANGE_MODE	((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&1&&(!TEST_DRAW_A_SQUARE)) //single gpio change mode

    #define  DO_TASK_WHEN_RF_IN_24G_MODE_ENALBE  1 //task en 

    #define DIRECT_ADV_TO_UNDIRECT_ENABLE   0 //dir change to undir en

    #define DOUBLE_CLICK_LEFT_FUN_ENABLE    0 //double click left fun
	#define APP_24G_AUDIO_EN     1 //24G audio
	#define USB_MIC_ENABLE_AAA   1
    #define BLE_AUDIO_ENABLE     0 //audio
    #define AUTO_CHECK_OS_TYPE   0 //audio check os type
    #define Microsoft_Swift_Pairing_ENABLE 1//if enable then device_name_len<=11

    #define	BLT_SOFTWARE_TIMER_ENABLE	0 //soft time en

    #define MUTI_SENSOR_ENABLE  1 //multi sensor en
    #define DPI_SAVE_FLASH      1 //dpi sav flash en
	#define ENTER_PAIR_WHEN_NEVER_PAIRED_ENABLE 1//For the better production of the factory

    // aes mehtod
    //0:no aes encryption   no key					old method
    //2:aes128 encryption	Dynamic random key
    //attention :Dongle&KB must be set to the same encryption mode

	#define AES_METHOD 0  //no aes
    #define DATA_3_CHOOSE_1_ENABLE     0 //data 3 choose 1

    #if (AES_METHOD == 1 && DATA_3_CHOOSE_1_ENABLE == 1) //aes 1 &&data 3 choose 1
        #error Not support AES and DATA_3_CHOOSE_1 work at the same time
    #endif
	#define USE_EXTERNAL_CAP	((!ADAPT_ALL_HW_AUTO_DRAW_IN_D24G_MODE_DEBUG)&&0)

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
	#define MIC_CHANNEL_COUNT		1
	#define	MIC_ENOCDER_ENABLE		0

	#define SPK_RESOLUTION_BIT		16

//	#define PIN_USB_INSERT			GPIO_PB1	//39a30_v1.0 j4 S-tx
   //	#define PB1_INPUT_ENABLE		1 //input en
  	//#define PB1_OUTPUT_ENABLE		0 //output dis
	//#define PB1_DATA_OUT			0 //out data 0
	//#define	PULL_WAKEUP_SRC_PB1		PM_PIN_PULLDOWN_100K //pull down 100k
	
#endif

    //gpio
    #if SENSOR_FUN_ENABLE_AAA
        
        #define PIN_SIF_MOTION 	GPIO_PD3//39a30_v1.0 j4 S-tx
        #define PD3_INPUT_ENABLE	1
        #define PD3_OUTPUT_ENABLE	0
        #define PD3_DATA_OUT    1
        #define	PULL_WAKEUP_SRC_PD3	PM_PIN_PULLUP_1M


        #define PIN_SIF_SCL 	GPIO_PB4//39a30_v1.0  j15
        #define PB4_INPUT_ENABLE	0
        #define PB4_OUTPUT_ENABLE	1
        #define PB4_DATA_OUT	1
        #define PULL_WAKEUP_SRC_PB4 PM_PIN_PULLUP_1M

        #define PIN_SIF_SDA 	GPIO_PB5//39a30_v1.0  j15
        #define PB5_INPUT_ENABLE	1
        #define PB5_OUTPUT_ENABLE	1
        #define PB5_DATA_OUT	1
        #define PULL_WAKEUP_SRC_PB5 PM_PIN_PULLUP_10K

        #if SENSOR_CS_ENABLE
            #define PIN_SENSOR_CS   GPIO_PA1//39a30_v1.0  SWS
            #define PA1_INPUT_ENABLE	0 //input dis
            #define PA1_OUTPUT_ENABLE	1 //output en
            #define PA1_DATA_OUT	1 //data out 1
            #define PULL_WAKEUP_SRC_PA1 PM_PIN_PULLUP_10K  //pull up 10k
        #endif

    #endif

#if BUTTON_FUN_ENABLE_AAA   //btn fun en

		#define MATRIX_ROW_PULL		PM_PIN_PULLDOWN_100K
		#define	MATRIX_COL_PULL		PM_PIN_PULLUP_10K
        #define BTN_NUM_AAA  7
#if(!KAIFABAN_EN)
		#define TOTAL_COL   3
		#define TOTAL_ROW   3
		#define  GPIO_ROW_PIN {GPIO_PD0,GPIO_PD1,GPIO_PD2}			// last pin 'GPIO_PD7' abnormal
		//#define  GPIO_COL_PIN   {GPIO_PA1, GPIO_PA2,  GPIO_PA3}//
		#if(MICDEMO)
		#define  GPIO_COL_PIN   {GPIO_PA2, GPIO_PB2,  GPIO_PA3}//
		#else
		#define  GPIO_COL_PIN   {GPIO_PA1, GPIO_PA2,  GPIO_PA3}
		#endif
		//drive pin as gpio
		#define	PD0_FUNC				AS_GPIO
		#define	PD1_FUNC				AS_GPIO
		#define	PD2_FUNC				AS_GPIO
	

		//drive pin need 100K pulldown
		#define	PULL_WAKEUP_SRC_PD0		MATRIX_ROW_PULL
		#define	PULL_WAKEUP_SRC_PD1		MATRIX_ROW_PULL
		#define	PULL_WAKEUP_SRC_PD2		MATRIX_ROW_PULL


		//drive pin open input to read gpio wakeup level
		#define PD0_INPUT_ENABLE		1
		#define PD1_INPUT_ENABLE		1
		#define PD2_INPUT_ENABLE		1


		//scan pin as gpio
		
		#define	PA3_FUNC				AS_GPIO
		//#define	PA1_FUNC				AS_GPIO
		#define	PA2_FUNC				AS_GPIO
		#if(MICDEMO)
		#define PB2_FUNC               AS_GPIO
		#else
		#define	PA1_FUNC				AS_GPIO
		#endif
		

		//scan  pin need 10K pullup
		#define	PULL_WAKEUP_SRC_PA3		MATRIX_COL_PULL
		//#define	PULL_WAKEUP_SRC_PA1		MATRIX_COL_PULL
		#define	PULL_WAKEUP_SRC_PA2		MATRIX_COL_PULL
		#if(MICDEMO)
		#define	PULL_WAKEUP_SRC_PB2		MATRIX_COL_PULL
		#else
		#define	PULL_WAKEUP_SRC_PA1		MATRIX_COL_PULL
		#endif
		

		//scan pin open input to read gpio level
		#if(MICDEMO ==0)
		#define PA1_INPUT_ENABLE		1
		#else
		#define PB2_INPUT_ENABLE		1
		#endif
		#define PA2_INPUT_ENABLE		1
		#define PA3_INPUT_ENABLE		1
        #define BTN_PM_WAKEUP_SRC   PM_PIN_PULLUP_1M   //pullup 1m
#else
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
#endif

       /* #define PIN_BTN_LEFT  			GPIO_PA0    //left PA0
        #define PA0_INPUT_ENABLE		1    //input en 
        #define PA0_OUTPUT_ENABLE		0    //output dis
        #define PA0_DATA_OUT			1    //data out 1
        #define PULL_WAKEUP_SRC_PA0	BTN_PM_WAKEUP_SRC

        #define PIN_BTN_RIGHT  			GPIO_PD4   //right PD4
        #define PD4_INPUT_ENABLE		1   //input en
        #define PD4_OUTPUT_ENABLE		0   //output dis
        #define PD4_DATA_OUT			1   //data out1
        #define PULL_WAKEUP_SRC_PD4 	BTN_PM_WAKEUP_SRC

        #define PIN_BTN_MIDDLE  			GPIO_PD3  //middle PD3
        #define PD3_INPUT_ENABLE		1 //input en
        #define PD3_OUTPUT_ENABLE		0 //output dis
        #define PD3_DATA_OUT			1 //data out 1
        #define PULL_WAKEUP_SRC_PD3 	BTN_PM_WAKEUP_SRC


        #define PIN_BTN_CPI  			GPIO_PC0  //cpi PC0
        #define PC0_INPUT_ENABLE		1 //input en
        #define PC0_OUTPUT_ENABLE		0 //output en
        #define PC0_DATA_OUT			1 //data out 1
        #define PULL_WAKEUP_SRC_PC0 	BTN_PM_WAKEUP_SRC

        #define PIN_BTN_K4  			GPIO_PD1 //k4 PD1
        #define PD1_INPUT_ENABLE		1
        #define PD1_OUTPUT_ENABLE		0
        #define PD1_DATA_OUT			1
        #define PULL_WAKEUP_SRC_PD1 	BTN_PM_WAKEUP_SRC

        #define PIN_BTN_K5  			GPIO_PD0  //k5 PD0
        #define PD0_INPUT_ENABLE		1
        #define PD0_OUTPUT_ENABLE		0
        #define PD0_DATA_OUT			1
        #define PULL_WAKEUP_SRC_PD0	BTN_PM_WAKEUP_SRC


	 #define PIN_SWITCH_TYPE  		GPIO_PA1				//no use
        #define PA1_INPUT_ENABLE		0
        #define PA1_OUTPUT_ENABLE		0
        #define PA1_DATA_OUT			0
        //#define PULL_WAKEUP_SRC_PA1  BTN_PM_WAKEUP_SRC*/

        #define PIN_MODE_SWITCH 		GPIO_PC2   //mode change PB0
        #define PC2_INPUT_ENABLE		1
        #define PC2_OUTPUT_ENABLE		0
        //#define PC2_DATA_OUT			1
        #define PULL_WAKEUP_SRC_PC2 	PM_PIN_PULLUP_1M//PM_PIN_UP_DOWN_FLOAT

        //#define BTN_MATRIX {PIN_BTN_LEFT,PIN_BTN_RIGHT,PIN_BTN_MIDDLE,PIN_BTN_K4,PIN_BTN_K5,PIN_BTN_CPI,PIN_MODE_SWITCH}
	  #define IS_BLE_MODE_AAA   (!gpio_read(PIN_MODE_SWITCH))
    #endif
		#define PIN_USB_INSERT   GPIO_PA0
		#define PA0_FUNC  		 AS_GPIO
		#define PA0_INPUT_ENABLE		1 //input en
  	    #define PA0_OUTPUT_ENABLE		0 //output dis
	    #define PA0_DATA_OUT			0 //out data 0
	    #define	PULL_WAKEUP_SRC_PA0		PM_PIN_UP_DOWN_FLOAT//PM_PIN_PULLDOWN_100K //pull down 100k
	    
    #if WHEEL_FUN_ENABLE_AAA
       /* #define PIN_WHEEL_1 			GPIO_PA2 	//39a30_v1.0 j15
        #define PA2_INPUT_ENABLE		1    //input en
        #define PA2_OUTPUT_ENABLE		0    //output dis
        #define PA2_DATA_OUT			0    //data out 0
        #define PULL_WAKEUP_SRC_PA2 	PM_PIN_PULLUP_1M

        #define PIN_WHEEL_2 			GPIO_PA3 	//39a30_v1.0 j22 d7
        #define PA3_INPUT_ENABLE		1    //input en 
        #define PA3_OUTPUT_ENABLE		0    //output did
        #define PA3_DATA_OUT			0    //data out 0
        #define PULL_WAKEUP_SRC_PA3	 PM_PIN_PULLUP_1M

        #define WHEEL_ADDRES_D2   		0X00
        #define WHEEL_ADDRES_D3   		0X01*/

		#define PIN_WHEEL_1 GPIO_PD7//GPIO_PB6 //39a30_v1.0 j15
        #define PB6_INPUT_ENABLE	1
        #define PB6_OUTPUT_ENABLE	0
        #define PB6_DATA_OUT	1
        #define PULL_WAKEUP_SRC_PB6 PM_PIN_PULLUP_1M


        #define PIN_WHEEL_2 GPIO_PB6 //39a30_v1.0 j22 d7
        #define PD7_INPUT_ENABLE	1
        #define PD7_OUTPUT_ENABLE	0
        #define PD7_DATA_OUT	1
        #define PULL_WAKEUP_SRC_PD7 PM_PIN_PULLUP_1M
		
		#if(MICDEMO)
        #define WHEEL_ADDRES_D2  0x07 //0X02
        #define WHEEL_ADDRES_D3  0x02//0X07
		#else
	     #define WHEEL_ADDRES_D2   0X07
        #define WHEEL_ADDRES_D3   0X02
		#endif
    #endif


    #if (!KAIFABAN_EN&&BLT_APP_LED_ENABLE)//BLT_APP_LED_ENABLE
        #define PIN_24G_LED 			GPIO_PA4 //24g led PC2
        #define PA4_INPUT_ENABLE		0
        #define PA4_OUTPUT_ENABLE		1
        #define PA4_DATA_OUT			LED_OFF_AAA
        #define PULL_WAKEUP_SRC_PA4	PM_PIN_UP_DOWN_FLOAT

        #define PIN_BLE_LED				GPIO_PD6  //ble led PC3
        #define PD6_INPUT_ENABLE		0
        #define PD6_OUTPUT_ENABLE		1
        #define PD6_DATA_OUT			LED_OFF_AAA
        #define PULL_WAKEUP_SRC_PD6 	PM_PIN_UP_DOWN_FLOAT
  
	#else
		#define PIN_VOICE 		GPIO_PD4
       	#define PD4_INPUT_ENABLE	0
        #define PD4_OUTPUT_ENABLE	1
        #define PD4_DATA_OUT	0
       	#define PULL_WAKEUP_SRC_PD4 PM_PIN_UP_DOWN_FLOAT
	#endif
		#define PIN_VOICE 		GPIO_PD4
       // #define PD4_INPUT_ENABLE	0
        //#define PD4_OUTPUT_ENABLE	1
        //#define PD4_DATA_OUT	LED_OFF_AAA
       // #define PULL_WAKEUP_SRC_PD4 PM_PIN_UP_DOWN_FLOAT

		#define PIN_BLE2_LED 		GPIO_PC7
        #define PC7_INPUT_ENABLE	0
        #define PC7_OUTPUT_ENABLE	1
        #define PC7_DATA_OUT	LED_OFF_AAA
        #define PULL_WAKEUP_SRC_PC7 PM_PIN_UP_DOWN_FLOAT

    #if (BATT_CHECK_ENABLE)
        //telink device: you must choose one gpio with adc function to output high level(voltage will equal to vbat), then use adc to measure high level voltage
        //use PB7 output high level, then adc measure this high level voltage
        #define GPIO_VBAT_DETECT				GPIO_PB7
        #define PB7_FUNC						AS_GPIO
        #define PB7_INPUT_ENABLE				0
        #define ADC_INPUT_PCHN				B7P    //corresponding  ADC_InputPchTypeDef in adc.h
    #endif
	//#define PD0_INPUT_ENABLE	0
	//#define PD0_OUTPUT_ENABLE	1
	//#define PD0_DATA_OUT		0
	//#define PIN_DEBUG_ENCODE_TIME_LEVEL(x)		gpio_write(GPIO_PD0,x)

    ////////////////////////// AUDIO CONFIG (RCU board) /////////////////////////////
    #if (BLE_AUDIO_ENABLE)
        #define BLE_DMIC_ENABLE					0  //0: Amic   1: Dmic
        #define	ADPCM_PACKET_LEN					128
        #define TL_MIC_ADPCM_UNIT_SIZE			248
        #define	TL_MIC_BUFFER_SIZE				992
        #define GPIO_AMIC_BIAS					GPIO_PC4
    #endif

	#if(APP_24G_AUDIO_EN )
		#define BLE_DMIC_ENABLE					0
		//#define	ADPCM_PACKET_LEN				128
		//#define MIC_SHORT_DEC_SIZE				248
		//#define	TL_MIC_BUFFER_SIZE				992
		#define GPIO_AMIC_BIAS					GPIO_PC0//GPIO_PC0// need check ,v1.0 PC4, V1.1 PC0
		#define GPIO_AMIC_SP					GPIO_PC1

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
