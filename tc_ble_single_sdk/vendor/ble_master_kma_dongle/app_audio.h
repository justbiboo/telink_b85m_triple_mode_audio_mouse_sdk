/********************************************************************************************************
 * @file    app_audio.h
 *
 * @brief   This is the header file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
 *
 *******************************************************************************************************/
#ifndef APP_AUDIO_H_
#define APP_AUDIO_H_

/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
void  	usb_endpoints_irq_handler (void);

/**
 * @brief		call this function to process when attHandle equal to AUDIO_HANDLE_MIC
 * @param[in]	conn - connect handle
 * @param[in]	p - Pointer point to l2cap data packet.
 * @return      none
 */
void	att_mic (u16 conn, u8 *p);

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
void 	proc_audio (void);

/**
 * @brief		reset mic_packet,reset audio id and writer pointer and read pointer
 * @param[in]	none
 * @return      none
 */
void 	mic_packet_reset(void);

/**
 * @brief		push_mic_packet
 * @param[in]	p - Pointer point to l2cap data packet
 * @return      none
 */
void 	push_mic_packet(unsigned char *p);

/**
 * @brief		copy packet data to defined buffer to process
 * @param[in]	data - Pointer point to l2cap data packet
 * @param[in]	length - the data length
 * @return      none
 */
void    app_audio_data(u8 * data, u16 length);

/**
 * @brief		usb_report_hid_mic
 * @param[in]	data - Pointer point to l2cap data packet
 * @param[in]	report_id - the data packet of report id
 * @return      0 - usb is busy and forbidden report hid mic
 *              1 - usb allow to report hid mic
 */
unsigned char 	usb_report_hid_mic(u8* data, u8 report_id);

#if (BIBOO_UX_ENABLE)
/**
 * @brief		BIBOO custom: data from mouse voice stream characteristic (my_Data, uuid16 0xB03E)
 * @param[in]	len - att data length (57 for mSBC frame)
 * @param[in]	p - Pointer point to att data
 * @return      none
 */
void	bibo_att_mic (u16 len, u8 *p);

/**
 * @brief		BIBOO custom: check whether mouse voice stream is active (within 200ms of last frame)
 * @param[in]	none
 * @return      1 - voice stream active; 0 - inactive
 */
u8		bibo_voice_active (void);

/**
 * @brief		BIBOO custom: decode pending mSBC frames to PCM (called in main loop)
 * @param[in]	none
 * @return      none
 */
void	bibo_abuf_mic_dec (void);

/**
 * @brief		BIBOO custom: feed decoded PCM to USB ISO IN EP7 (called in USB irq)
 * @param[in]	none
 * @return      none
 */
void	bibo_abuf_dec_usb (void);

#if (BIBOO_UX_SINE_TEST)
/**
 * @brief		BIBOO test: output 1kHz sine test tone on USB ISO IN EP7 (called in USB irq)
 * @param[in]	none
 * @return      none
 */
void	bibo_abuf_dec_usb_sine (void);
#endif
#endif

#endif /* APP_AUDIO_H_ */
