/********************************************************************************************************
 * @file     AAA_app_config.h
 *
 * @brief    This is the header file for KMD SDK
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

#pragma once

/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C" {
#endif

#define DEBUG_MODE				0 //DEBUG printf mode

#if(DEBUG_MODE)
#define PRINT_BAUD_RATE			1000000	//1M baud rate,should Not bigger than 1Mb/s
#define DEBUG_INFO_TX_PIN		GPIO_PA3
#endif

/* Flash lock controy */
#if (ALL_SRAM_CODE)
	#define FLASH_LOCK_ENABLE_AAA		0 //1-lock 0-unlock
#else
	#define FLASH_LOCK_ENABLE_AAA		1 //1-lock 0-unlock
#endif

#if (_CHIP_IS_OTP_ && FLASH_LOCK_ENABLE_AAA)
	#undef FLASH_LOCK_ENABLE_AAA
	#define FLASH_LOCK_ENABLE_AAA	0
#endif

/* Define system clock */
#define CLOCK_SYS_CLOCK_HZ  	48000000

#if(MCU_CORE_B89)
#if(CLOCK_SYS_CLOCK_HZ==12000000)
	#define SYS_CLK  	SYS_CLK_12M_Crystal
#elif (CLOCK_SYS_CLOCK_HZ==16000000)
	#define SYS_CLK  	SYS_CLK_16M_Crystal
#elif (CLOCK_SYS_CLOCK_HZ==24000000)
	#define SYS_CLK  	SYS_CLK_24M_Crystal
#endif
#else
#if(CLOCK_SYS_CLOCK_HZ==12000000)
	#define SYS_CLK  	SYS_CLK_12M_Crystal
#elif (CLOCK_SYS_CLOCK_HZ==16000000)
	#define SYS_CLK  	SYS_CLK_16M_Crystal
#elif (CLOCK_SYS_CLOCK_HZ==24000000)
	#define SYS_CLK  	SYS_CLK_24M_Crystal
#elif (CLOCK_SYS_CLOCK_HZ==32000000)
	#define SYS_CLK  	SYS_CLK_32M_Crystal
#elif (CLOCK_SYS_CLOCK_HZ==48000000)
	#define SYS_CLK  	SYS_CLK_48M_Crystal
#endif
#endif

enum{
	CLOCK_SYS_CLOCK_1S = CLOCK_SYS_CLOCK_HZ,
	CLOCK_SYS_CLOCK_1MS = (CLOCK_SYS_CLOCK_1S / 1000),
	CLOCK_SYS_CLOCK_1US = (CLOCK_SYS_CLOCK_1S / 1000000),
};

/////////////////// watchdog  //////////////////////////////
#define MODULE_WATCHDOG_ENABLE			0 //Disable normal watch_dog
#define MODULE_32K_WATCHDOG_ENABLE		1 //Enable 32K watch_dog
#define WATCHDOG_INIT_TIMEOUT			1500 //unit 1ms

#define LED1     		        GPIO_PA4
#define LED2     		        GPIO_PA5
#define LED3     		        GPIO_PA6
#define LED4     		        GPIO_PA7

/*#if 0//degug rf timer
#define PB2_INPUT_ENABLE	0
#define PB2_OUTPUT_ENABLE	1
#define PB2_DATA_OUT	0
#define PIN_DEBUG_RF_RX_IRQ_TOGGLE   	gpio_toggle(GPIO_PB2)
									
#define PB3_INPUT_ENABLE	0
#define PB3_OUTPUT_ENABLE	1
#define PB3_DATA_OUT	0
#define PIN_DEBUG_RF_RX_CRC_OK_TOGGLE   gpio_toggle(GPIO_PB3)
									
#define PB4_INPUT_ENABLE	0
#define PB4_OUTPUT_ENABLE	1
#define PB4_DATA_OUT	0
#define PIN_DEBUG_RF_RX_DATA_OK_TOGGLE   	gpio_toggle(GPIO_PB4)

#define PB5_INPUT_ENABLE	0
#define PB5_OUTPUT_ENABLE	1
#define PB5_DATA_OUT	0
#define PIN_DEBUG_RF_TX_LEVEL(x)   	gpio_write(GPIO_PB5,x)

#define PD7_INPUT_ENABLE	0
#define PD7_OUTPUT_ENABLE	1
#define PD7_DATA_OUT	0
#define PIN_DEBUG_RF_CHN_NEXT_TOGGLE   	gpio_toggle(GPIO_PD7)

#else*/
#define PIN_DEBUG_RF_RX_IRQ_TOGGLE
#define PIN_DEBUG_RF_RX_CRC_OK_TOGGLE
#define PIN_DEBUG_RF_RX_DATA_OK_TOGGLE
#define PIN_DEBUG_RF_TX_LEVEL(x)
#define PIN_DEBUG_RF_CHN_NEXT_TOGGLE
//#endif

#ifndef USER_DEBUG
#define USER_DEBUG   0
#endif

#if USER_DEBUG
#define IO_PRINT				1
#define USB_PRINT				2
#define PRINT_MODE 				IO_PRINT

#define D1_LED_PIN  GPIO_PA2
#define D2_LED_PIN  GPIO_PA3
#define D3_LED_PIN  GPIO_PA4
#define D4_LED_PIN  GPIO_PB0
#define D5_LED_PIN  GPIO_PB1
#define RF_CHAN_CHANGE_PIN  GPIO_PC7


#define BNT_1_PIN  GPIO_PD5
#define BNT_2_PIN  GPIO_PD6
#endif

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
