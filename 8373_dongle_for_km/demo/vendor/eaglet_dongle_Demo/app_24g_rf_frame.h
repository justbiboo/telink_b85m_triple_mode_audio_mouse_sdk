/********************************************************************************************************
 * @file     AAA_24G_rf_frame.h
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

/*
 * led_rf_frame.h
 *
 *  Created on: Jan 13, 2014
 *      Author: xuzhen
 */

#ifndef _APP_24G_RF_FRAME_H_
#define _APP_24G_RF_FRAME_H_

#define MOUSE_DATA_LEN_AAA  6    //mouth data len 6

enum
{
    PIPE_PARING			= 0x00,//pair pipe 0
    PIPE_MOUSE			= 0x01,//mouse pipe 1
    PIPE_KEYBOARD		= 0x02,//keyboard pipe 2
    //PIPE_AUDIO			= 0x03,
    //PIPE_TOUCH			= 0x04,
    //PIPE_RC				= 0x05,
    PIPE_ALL   = 0X3F,//pipe all
};

enum
{
    EMPTY_CMD			= 0x00,
    EMPTY_ACK_CMD=1,
    
    PAIR_CMD=2,//pair cmd
    PAIR_ACK_CMD=3,//pair ack cmd

	MOUSE_CMD=4,//mouse cmd
	MOUSE_ACK_CMD=5,//mouse ack cmd

	KB_CMD=6,//kb cmd
	KB_ACK_CMD=7,//kb ack cmd

	RECONNECT_CMD=8,//reconnect cmd
	RECONNECT_ACK_CMD=9,//reconnect ack cmd

	D24G_OTA_CMD = 10,
	D24G_OTA_ACK_CMD = 11,
	MIC_DATA_CMD = 12,
	APP_DATA_CMD =13,
	CONSUMER_CMD = 14,
	AI_CMD = 9,
};

typedef struct
{
    u8 btn;//btn value
#if (MOUSE_DATA_LEN_AAA==6)
    s16 x;//x data
    s16 y;//y data
#elif (MOUSE_DATA_LEN_AAA==4)
    s8 x;
    s8 y;
#endif
    s8 wheel;//wheel data
} mouse_data_t;//mouse data struct


typedef struct
{
	u32 did;

	u8 key[12];
}AES_KEY;
#if(1)
#define ID_LEN 4
#define HEAD_LENGTH  7

typedef struct
{
    u32 dma_len;//dma len

    u8 rf_len; //rf len
    u8 cmd;
	u16 seq;
	u32 did;
    u8  dat[53];
} rf_packet_t;//rf packet struct
#else

#define HEAD_LENGTH  3
typedef struct
{	
    u32 dma_len;//dma len
    u8 rf_len; //rf len
	u8 cmd; //command word
	u8 seq;
	u8 did;
    u8  dat[57];
	
} rf_packet_t;//rf packet struct
#endif
typedef struct
{	
	u8 cmd;//data type
	u8 seq_no;
	u8 pno_no;
	u32 did;
	
	u8 key[12];
} pair_data_t;

typedef struct
{	
	u8 cmd;//data type
	u8 seq_no;
	u8 pno_no;
	
	u8 tick_0;
	u8 tick_1;
	u8 chn;
	u8 host_led_status;//host led status
	
	u32 gid;
	u32 did;
	u8 key[12];

} pair_ack_data_t;



typedef struct
{
	u8	cmd;	//command word, bit7=0: no aes  =1: aes
	u8	seq_no;	//The frame serial number
	u8	pn_no; 	//
	
	u32 did;	//Device ID

	u8 km_dat[6];//mouse data or kb
	u8  rsv1[3]; //for aes  16 bytes
	
	u16 crc16;

} km_3_c_1_data_t;


typedef struct
{
	u8 cmd;		//command word
	u8 seq_no;	//The frame serial number
	u8 pno_no;	
	
	u8 tick_0;
	u8 tick_1;
	u8 chn;//chanel
	u8 host_led_status;//host led status
	
} km_ack_data_t;//km ack data struct


typedef struct
{
	u8	cmd;//bit7=0: no aes  =1: aes
	u8	seq_no;
	u8  pno_no;
	u32 did;

	u8	report_id;
	u8 	opcode;
	u16	length;	
	u8 dat[20]; //include:u16 package_cnt; u8 data[16]; u16 crc16;

	u8	km_cmd;	//command word
	u8  km_dat[6]; //mouse data or kb
} ota_data_t;


typedef struct
{
	u8	cmd; //bit7=0: no aes  =1: aes
	u8	seq_no;
	u8  pno_no; //0-invalid data 1-valid data
	u32 did;

	u8	report_id;
	u8 	opcode;
	u16	length;	
	u8 dat[20]; //include:u16 package_cnt; u8 data[16]; u16 crc16;

	u8	km_cmd;	//command word
	u8  km_dat[6]; //mouse data or kb
} ota_ack_data_t;
#define PC_DATA_LEN 56
typedef struct
{
	u8 host_led_status;//host led status
	u8 pc_dat[PC_DATA_LEN];
} pc_ack_data_t;//km ack data struct


#endif /* LED_RF_FRAME_H_ */

