/********************************************************************************************************
 * @file    app_audio.c
 *
 * @brief   This is the source file for BLE SDK
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
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "application/audio/tl_audio.h"
#include "application/audio/audio_config.h"
#if (BIBOO_UX_ENABLE)
#include "application/audio/sbc.h"
#endif
#include "app.h"
#include "app_audio.h"

#if (TL_AUDIO_MODE == TL_AUDIO_DONGLE_ADPCM_GATT_TELINK)			//GATT Telink

u8		att_mic_rcvd = 0;
u32		tick_adpcm;
u8		buff_mic_adpcm[MIC_ADPCM_FRAME_SIZE];

u32		tick_iso_in;
int		mode_iso_in;


/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void  usb_endpoints_irq_handler (void)
{
	u32 t = clock_time ();
	/////////////////////////////////////
	// ISO IN
	/////////////////////////////////////
	if (reg_usb_irq & BIT(7)) {
		mode_iso_in = 1;
		tick_iso_in = t;
		reg_usb_irq = BIT(7);	//clear interrupt flag of endpoint 7

		/////// get MIC input data ///////////////////////////////
		//usb_iso_in_1k_square ();
		//usb_iso_in_from_mic ();
		#if (BIBOO_UX_ENABLE && BIBOO_UX_SINE_TEST)
		bibo_abuf_dec_usb_sine ();  //BIBOO test: 1kHz sine test tone on USB EP7
		#elif (BIBOO_UX_ENABLE)
		if (bibo_voice_active ())
		{
			bibo_abuf_dec_usb ();  //BIBOO custom: output decoded mouse voice stream
		}
		else
		{
			abuf_dec_usb ();
		}
		#else
		abuf_dec_usb ();
		#endif
	}

}

/**
 * @brief		call this function to process when attHandle equal to AUDIO_HANDLE_MIC
 * @param[in]	conn - connect handle
 * @param[in]	p - Pointer point to l2cap data packet.
 * @return      none
 */
void	att_mic (u16 conn, u8 *p)
{
	(void)conn;
	att_mic_rcvd = 1;
	memcpy (buff_mic_adpcm, p, MIC_ADPCM_FRAME_SIZE);
	abuf_mic_add ((u32 *)buff_mic_adpcm);
}

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void proc_audio (void)
{
	if (att_mic_rcvd)
	{
		tick_adpcm = clock_time ();
		att_mic_rcvd = 0;
	}
	if (clock_time_exceed (tick_adpcm, 200000))
	{
		tick_adpcm = clock_time ();
		abuf_init ();
	}
	abuf_mic_dec ();
	#if (BIBOO_UX_ENABLE)
	if (bibo_voice_active ())
	{
		bibo_abuf_mic_dec ();  //BIBOO custom: decode mouse voice stream (mSBC 57B/frame)
	}
	#endif
}

#if (BIBOO_UX_ENABLE)
/*----------------------------------------------------------------------------
 * BIBOO custom: BLE voice stream decoder (mouse -> dongle -> PC microphone).
 * In BLE mode the mouse pushes 57-byte mSBC voice frames (7.5ms @16K) on its
 * "my_Data" characteristic (uuid16 0xB03E @ service 0xFFF0). This block:
 *   1) bibo_att_mic()      - buffers each incoming 57-byte frame (called from
 *                            blm_host.c L2CAP notify handler)
 *   2) bibo_abuf_mic_dec() - decodes mSBC frames to 16K PCM (main loop)
 *   3) bibo_abuf_dec_usb() - feeds PCM to USB ISO IN EP7 (USB irq)
 * Used only while frames keep arriving (see bibo_voice_active()).
 *--------------------------------------------------------------------------*/
#define BIBO_MSBC_FRAME_LEN     57      //57-byte mSBC frame per 7.5ms
#define BIBO_MSBC_PCM_SAMPLES   120     //120 PCM samples (240 bytes) per frame
#define BIBO_MSBC_FRAME_NUM     8       //ring buffer frame count (power of 2)
#define BIBO_MSBC_DEC_SIZE      (BIBO_MSBC_PCM_SAMPLES * BIBO_MSBC_FRAME_NUM)
#define BIBO_USB_ISO_IN_SIZE    (MIC_SAMPLE_RATE / 1000)

static u8  bibo_abuf_mic_wptr, bibo_abuf_dec_wptr;
static u16 bibo_abuf_dec_rptr;
static u8  bibo_abuf_mic[BIBO_MSBC_FRAME_LEN * BIBO_MSBC_FRAME_NUM];
static s16 bibo_abuf_dec[BIBO_MSBC_DEC_SIZE];
static u8  bibo_abuf_reset = 0;
static u8  bibo_dec_start = 1;
static u32 bibo_voice_tick = 0;

#if (BIBOO_UX_DEBUG)
/*BIBOO debug: voice chain counters (printed by dbg_ble_status_print in blm_host.c)*/
volatile u32 dbg_vbuf_cnt = 0;   //frames passed 57B filter & buffered
volatile u32 dbg_vdec_cnt = 0;   //frames decoded (sbc_decode called)
volatile u32 dbg_vwr_len = 0;    //last sbc_decode written bytes (expect 240)
volatile u32 dbg_visr_cnt = 0;   //USB ISO IN packets handled by BIBOO output chain
volatile u32 dbg_vusb_cnt = 0;   //USB ISO IN packets carrying real PCM
volatile u32 dbg_vcall_cnt = 0;  //bibo_abuf_mic_dec calls (main loop rate)
volatile u32 dbg_vdec_us = 0;    //last sbc_decode duration (us)
volatile u32 dbg_vdec_us_max = 0;//max sbc_decode duration (us)
volatile u32 dbg_rsta_cnt = 0;  //reset arm: new voice session (bibo_att_mic)
volatile u32 dbg_rstb_cnt = 0;  //reset arm: usb iso gap>4ms (bibo_abuf_dec_usb)
volatile u32 dbg_rstblk_cnt = 0;//bibo_abuf_mic_dec calls blocked by reset flag
volatile u32 dbg_vskp_cnt = 0;  //reader-side frame drops (queue overrun guard in bibo_abuf_dec_usb)
volatile u32 dbg_loop_cnt = 0;  //main loop iterations (audio pacing observability)
#endif

/**
 * @brief		voice stream active? (a frame arrived within last 200ms)
 * @param[in]	none
 * @return		1 - active, 0 - idle
 */
_attribute_ram_code_ u8 bibo_voice_active (void)
{
	return bibo_voice_tick && !clock_time_exceed (bibo_voice_tick, 200000);
}

/**
 * @brief		call this when a notification of mouse my_Data characteristic arrives
 * @param[in]	len - value length (57 for voice frame, other lengths are ignored)
 * @param[in]	p - value data
 * @return      none
 */
void bibo_att_mic (u16 len, u8 *p)
{
	if (len != BIBO_MSBC_FRAME_LEN)  //ignore short control frames on the same handle
	{
		return;
	}
	if (!bibo_voice_active ())  //new voice session: resync the decode chain
	{
		bibo_abuf_mic_wptr = 0;
		bibo_abuf_dec_wptr = 0;
			#if (BIBOO_UX_DEBUG)
				dbg_rsta_cnt ++;
			#endif
		bibo_abuf_reset = 16;
		bibo_dec_start = 1;
	}
	bibo_voice_tick = clock_time () | 1;
	#if (BIBOO_UX_DEBUG)
	dbg_vbuf_cnt ++;
	#endif

	u8 *pd = bibo_abuf_mic + (bibo_abuf_mic_wptr & (BIBO_MSBC_FRAME_NUM - 1)) * BIBO_MSBC_FRAME_LEN;
	for (int i = 0; i < BIBO_MSBC_FRAME_LEN; i++)
	{
		*pd ++ = *p ++;
	}
	bibo_abuf_mic_wptr ++;
}

/**
 * @brief		decode pending mSBC frames to PCM (main loop)
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void bibo_abuf_mic_dec (void)
{
	static u32 bibo_decode_len = 0;
	#if (BIBOO_UX_DEBUG)
	dbg_vcall_cnt ++;
	#endif
	if (bibo_abuf_reset)
	{
	#if (BIBOO_UX_DEBUG)
		dbg_rstblk_cnt ++;
	#endif
		bibo_abuf_dec_wptr = bibo_abuf_mic_wptr;
	}
	else
	{
		/*drain ALL pending frames in one call (bounded by ring size): the main loop
		 * runs well below the 133.3 frames/s required by the 7.5ms mSBC stream, so
		 * the old one-frame-per-call pacing silently dropped ~30% of the voice,
		 * heard as periodic glitches in the decoded tone*/
		for (u8 n = 0; n < BIBO_MSBC_FRAME_NUM; n++)
		{
			u8 num_mic = (u8)(bibo_abuf_mic_wptr - bibo_abuf_dec_wptr);
			if (num_mic > BIBO_MSBC_FRAME_NUM)  //in case of overflow
			{
				bibo_abuf_dec_wptr ++;
				continue;
			}
			if ( (!bibo_dec_start && num_mic >= 1) || (bibo_dec_start && num_mic >= 2) )
			{
				#if (BIBOO_UX_DEBUG)
				u32 _vdec_t0 = clock_time ();
				#endif
				sbc_decode (bibo_abuf_mic + (bibo_abuf_dec_wptr & (BIBO_MSBC_FRAME_NUM - 1)) * BIBO_MSBC_FRAME_LEN, BIBO_MSBC_FRAME_LEN,
							bibo_abuf_dec + (bibo_abuf_dec_wptr & (BIBO_MSBC_FRAME_NUM - 1)) * BIBO_MSBC_PCM_SAMPLES, BIBO_MSBC_PCM_SAMPLES * 2,
							&bibo_decode_len);
				#if (BIBOO_UX_DEBUG)
				{
					u32 _vdec_dt = clock_time () - _vdec_t0;
					dbg_vdec_us = _vdec_dt;
					if (_vdec_dt > dbg_vdec_us_max) { dbg_vdec_us_max = _vdec_dt; }
				}
				dbg_vdec_cnt ++;
				dbg_vwr_len = bibo_decode_len;
				#endif
				bibo_abuf_dec_wptr ++;
				bibo_dec_start = 0;
			}
			else
			{
				break;
			}
		}
	}
}

/**
 * @brief		feed decoded PCM to USB ISO IN EP7 (called in USB irq, silent on empty)
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void bibo_abuf_dec_usb (void)
{
	static u32 tick_usb_iso_in;
	static u8  buffer_empty = 1;
	static u8  n_usb_iso = 0;

	n_usb_iso++;
	#if (BIBOO_UX_DEBUG)
	dbg_visr_cnt ++;
	#endif
	if (clock_time_exceed (tick_usb_iso_in, 4000))
	{
			#if (BIBOO_UX_DEBUG)
		dbg_rstb_cnt ++;
	#endif
bibo_abuf_reset = 16;
	}
	tick_usb_iso_in = clock_time ();
	if (bibo_abuf_reset)
	{
		bibo_abuf_dec_rptr = bibo_abuf_dec_wptr * BIBO_MSBC_PCM_SAMPLES;
		bibo_abuf_reset --;
	}
	/////////////////// copy data to usb iso in buffer ///////////////
	reg_usb_ep7_ptr = 0;
	u8 num = bibo_abuf_dec_wptr - (bibo_abuf_dec_rptr / BIBO_MSBC_PCM_SAMPLES);
	if (num >= BIBO_MSBC_FRAME_NUM)		//queue overrun: reader fell a full ring behind, next decode would overwrite the frame being played
	{
		bibo_abuf_dec_rptr += BIBO_MSBC_PCM_SAMPLES;	//drop the oldest queued frame (7.5ms) to restore one-slot distance
		if(bibo_abuf_dec_rptr >= (BIBO_MSBC_PCM_SAMPLES << 8) ){
			bibo_abuf_dec_rptr -= (BIBO_MSBC_PCM_SAMPLES << 8);
		}
		num --;
		#if (BIBOO_UX_DEBUG)
		dbg_vskp_cnt ++;
		#endif
	}
	if (num)
	{
		/*start/refill at HALF the ring: filling FRAME_NUM-1 of FRAME_NUM left only one frame of
		 *margin, so any delivery jitter pushed the ring full and the next decode overwrote the
		 *frame being played (periodic dropout every 7.5ms)*/
		if ( (buffer_empty && num >= (BIBO_MSBC_FRAME_NUM >> 1)) || (!buffer_empty && (num >= 1 || (n_usb_iso & (BIBO_MSBC_FRAME_NUM - 1)))) )
		{
			buffer_empty = 0;
			#if (BIBOO_UX_DEBUG)
			dbg_vusb_cnt ++;
			#endif
			u16 offset = bibo_abuf_dec_rptr % BIBO_MSBC_DEC_SIZE;
			s16 *ps = bibo_abuf_dec + offset;
			if(offset == BIBO_MSBC_DEC_SIZE - (BIBO_USB_ISO_IN_SIZE / 2)){
				for (int i=0; i<(BIBO_USB_ISO_IN_SIZE / 2); i++)
				{
					reg_usb_ep7_dat = *ps;
					reg_usb_ep7_dat = *ps++ >> 8;
				}
				ps = bibo_abuf_dec;
				for (int i=0; i<(BIBO_USB_ISO_IN_SIZE / 2); i++)
				{
					reg_usb_ep7_dat = *ps;
					reg_usb_ep7_dat = *ps++ >> 8;
				}
			}
			else{
				for (int i=0; i<BIBO_USB_ISO_IN_SIZE; i++)
				{
					reg_usb_ep7_dat = *ps;
					reg_usb_ep7_dat = *ps++ >> 8;
				}
			}

			bibo_abuf_dec_rptr += BIBO_USB_ISO_IN_SIZE;
			if(bibo_abuf_dec_rptr >= (BIBO_MSBC_PCM_SAMPLES << 8) ){
				bibo_abuf_dec_rptr -= (BIBO_MSBC_PCM_SAMPLES << 8);	//must SUBTRACT, not zero: rptr may be non-16-aligned (starts from dec_wptr*120)
			}
		}
		else
		{
			for (int i=0; i<BIBO_USB_ISO_IN_SIZE * 2; i++)
			{
				reg_usb_ep7_dat = 0;
			}
		}
	}
	else
	{
		for (int i=0; i<BIBO_USB_ISO_IN_SIZE * 2; i++)
		{
			reg_usb_ep7_dat = 0;
		}
		buffer_empty = 1;
	}
	reg_usb_ep7_ctrl = BIT(0);			//ACK iso in
}

#if (BIBOO_UX_SINE_TEST)
/*----------------------------------------------------------------------------
 * BIBOO test case: 1kHz sine test tone on USB ISO IN EP7.
 *   - While the PC captures the dongle microphone (EP7 polled every 1ms),
 *     each ISOC IN packet is filled with 16 samples @16K/16bit mono PCM =
 *     exactly one 1kHz sine period per packet.
 *   - Works as soon as USB is plugged and host capture starts, no BLE mouse.
 *   - 16-point LUT lives in RAM (.data): safe inside USB irq, no flash read.
 *--------------------------------------------------------------------------*/
#define BIBO_SINE_LUT_SIZE      16      //16 samples = one 1kHz period @16kHz

/* one full 1kHz sine period @16kHz, amplitude +/-8192 (-12dBFS) */
static s16 bibo_sine_1k_lut[BIBO_SINE_LUT_SIZE] =
{
	    0,  3135,  5793,  7568,  8192,  7568,  5793,  3135,
	    0, -3135, -5793, -7568, -8192, -7568, -5793, -3135
};

/**
 * @brief		BIBOO test: fill USB ISO IN EP7 with a 1kHz sine test tone
 * 				(called in USB ISO IN irq, once per 1ms packet)
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void bibo_abuf_dec_usb_sine (void)
{
	reg_usb_ep7_ptr = 0;
	for (int i = 0; i < BIBO_SINE_LUT_SIZE; i++)
	{
		reg_usb_ep7_dat = (u8)(bibo_sine_1k_lut[i]);
		reg_usb_ep7_dat = (u8)(bibo_sine_1k_lut[i] >> 8);
	}
	reg_usb_ep7_ctrl = BIT(0);			//ACK iso in
}
#endif
#endif

#elif  (TL_AUDIO_MODE == TL_AUDIO_DONGLE_ADPCM_GATT_GOOGLE)			//GATT GOOGLE
u8		att_mic_rcvd = 0;
u32		tick_adpcm;
u8		buff_mic_adpcm[MIC_ADPCM_FRAME_SIZE];

u32		tick_iso_in;
int		mode_iso_in;

volatile u8 google_audio_start;

/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void  usb_endpoints_irq_handler (void)
{
	u32 t = clock_time ();
	/////////////////////////////////////
	// ISO IN
	/////////////////////////////////////
	if (reg_usb_irq & BIT(7)) {
		mode_iso_in = 1;
		tick_iso_in = t;
		reg_usb_irq = BIT(7);	//clear interrupt flag of endpoint 7

		/////// get MIC input data ///////////////////////////////
		//usb_iso_in_1k_square ();
		//usb_iso_in_from_mic ();
		abuf_dec_usb ();
	}

}

/**
 * @brief		call this function to process when attHandle equal to AUDIO_HANDLE_MIC
 * @param[in]	conn - connect handle
 * @param[in]	p - Pointer point to l2cap data packet
 * @return      none
 */
void	att_mic (u16 conn, u8 *p)
{
	(void)conn;(void)p;
	att_mic_rcvd = 1;
	memcpy (buff_mic_adpcm, p, MIC_ADPCM_FRAME_SIZE);
	abuf_mic_add ((u32 *)buff_mic_adpcm);
}

/**
 * @brief		copy packet data to defined buffer to process
 * @param[in]	data - Pointer point to l2cap data packet
 * @param[in]	length - the data length
 * @return      none
 */
void app_audio_data(u8 * data, u16 length)
{
	static u8 audio_buffer_serial;
	#if (GOOGLE_VOICE_OVER_BLE_SPCE_VERSION == GOOGLE_VERSION_1_0)
		if(length == 20)
		{
			memcpy(buff_mic_adpcm+audio_buffer_serial*20,data,length);
			audio_buffer_serial++;
			if (audio_buffer_serial == 6) {
				abuf_mic_add ((u32 *)buff_mic_adpcm);
				audio_buffer_serial = 0;
				att_mic_rcvd = 1;
			}
		}
		else if(length == 120)
		{
			memcpy(buff_mic_adpcm,data,length);
			abuf_mic_add ((u32 *)buff_mic_adpcm);
			att_mic_rcvd = 1;
		}
	#else
		if(!google_audio_start)
		{
			return ;
		}
		if(audio_buffer_serial < 6 && length == 20)
		{
			memcpy(buff_mic_adpcm+audio_buffer_serial*20,data,length);
			audio_buffer_serial++;
		}
		else if(audio_buffer_serial==6 && length ==14)
		{
			memcpy(buff_mic_adpcm+audio_buffer_serial*20,data,length);
			abuf_mic_add ((u32 *)buff_mic_adpcm);
			audio_buffer_serial = 0;
			att_mic_rcvd = 1;
		}
		else
		{
			audio_buffer_serial = 0;
		}
	#endif
}

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void proc_audio (void)
{
	if (att_mic_rcvd)
	{
		tick_adpcm = clock_time ();
		att_mic_rcvd = 0;
	}
	if (clock_time_exceed (tick_adpcm, 200000))
	{
		tick_adpcm = clock_time ();
		abuf_init ();
	}
	abuf_mic_dec ();
}
#elif  (TL_AUDIO_MODE == TL_AUDIO_DONGLE_ADPCM_HID_DONGLE_TO_STB)		//HID Service,ADPCM,Dongle to STB,STB decode

#define MIC_BUFF_NUM	32
#define MIC_DATA_LEN	20

unsigned char mic_dat_buff[MIC_BUFF_NUM][MIC_DATA_LEN];

u8 usb_mic_wptr= 0;
u8 usb_mic_rptr= 0;
u8 audio_id = 0;

/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void  usb_endpoints_irq_handler (void)
{

	if (reg_usb_irq & BIT(7)) {

		reg_usb_irq = BIT(7);	//clear interrupt flag of endpoint 7

	}

}

/**
 * @brief		usb_report_hid_mic
 * @param[in]	data - Pointer point to l2cap data packet
 * @param[in]	report_id - the data packet of report id
 * @return      0 - usb is busy and forbidden report hid mic
 *              1 - usb allow to report hid mic
 */
unsigned char usb_report_hid_mic(u8* data, u8 report_id)
{
	if(usbhw_is_ep_busy(USB_EDP_AUDIO_IN))
		return 0;

	reg_usb_ep_ptr(USB_EDP_AUDIO_IN) = 0;

	reg_usb_ep_dat(USB_EDP_AUDIO_IN) = report_id;
	foreach(i, 20){
		reg_usb_ep_dat(USB_EDP_AUDIO_IN) = data[i];
	}

	reg_usb_ep_ctrl(USB_EDP_AUDIO_IN) = FLD_EP_DAT_ACK;		// ACK
	return 1;
}

/**
 * @brief		reset mic_packet,reset audio id and writer pointer and read pointer
 * @param[in]	none
 * @return      none
 */
void mic_packet_reset(void)
{
	audio_id = 0;
	usb_mic_wptr = 0;
	usb_mic_rptr = 0;
}

/**
 * @brief		push_mic_packet
 * @param[in]	p - Pointer point to l2cap data packet
 * @return      none
 */
void push_mic_packet(unsigned char *p)
{
	memcpy(mic_dat_buff[usb_mic_wptr], p, MIC_DATA_LEN);

	usb_mic_wptr =  (usb_mic_wptr+1)&(MIC_BUFF_NUM-1);
}

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void proc_audio (void)
{
	if(usb_mic_wptr != usb_mic_rptr)
	{
		static u32 cnt = 0;
		u8 *pdat = mic_dat_buff[usb_mic_rptr];
		if(usb_report_hid_mic(pdat, (audio_id + 10))==1)
		{
			audio_id += 1;
			audio_id = audio_id%3;
			cnt += 1;
			usb_mic_rptr =  (usb_mic_rptr+1)&(MIC_BUFF_NUM-1);
		}
	}
	return;
}

#elif  (TL_AUDIO_MODE == TL_AUDIO_DONGLE_ADPCM_HID)		//HID Service,ADPCM,Dongle decode

u8		att_mic_rcvd = 0;
u32		tick_adpcm;

u32		tick_iso_in;
int		mode_iso_in;


extern u8 tmp_mic_data[];

/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void  usb_endpoints_irq_handler (void)
{
	u32 t = clock_time ();
	/////////////////////////////////////
	// ISO IN
	/////////////////////////////////////
	if (reg_usb_irq & BIT(7)) {
		mode_iso_in = 1;
		tick_iso_in = t;
		reg_usb_irq = BIT(7);	//clear interrupt flag of endpoint 7

		/////// get MIC input data ///////////////////////////////
		//usb_iso_in_1k_square ();
		//usb_iso_in_from_mic ();
		abuf_dec_usb ();
	}

}

/**
 * @brief		call this function to process when attHandle equal to AUDIO_HANDLE_MIC
 * @param[in]	conn - connect handle
 * @param[in]	p - Pointer point to l2cap data packet.
 * @return      none
 */
void	att_mic (u16 conn, u8 *p)
{
	memcpy (tmp_mic_data, p, MIC_ADPCM_FRAME_SIZE);
	abuf_mic_add ((u32 *)tmp_mic_data);
}

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void proc_audio (void)
{
	if (att_mic_rcvd)
	{
		tick_adpcm = clock_time ();
		att_mic_rcvd = 0;
	}
	if (clock_time_exceed (tick_adpcm, 3*1000*1000))
	{
		tick_adpcm = clock_time ();
	}
	abuf_mic_dec ();
}
#elif  (TL_AUDIO_MODE == TL_AUDIO_DONGLE_SBC_HID_DONGLE_TO_STB)		//HID Service,Dongle to STB,STB decode

#define MIC_BUFF_NUM	32
#define MIC_DATA_LEN	20

unsigned char mic_dat_buff[MIC_BUFF_NUM][MIC_DATA_LEN];

u8 usb_mic_wptr= 0;
u8 usb_mic_rptr= 0;
u8 audio_id = 0;

/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void  usb_endpoints_irq_handler (void)
{

	if (reg_usb_irq & BIT(7)) {

		reg_usb_irq = BIT(7);	//clear interrupt flag of endpoint 7

	}

}

/**
 * @brief		usb_report_hid_mic
 * @param[in]	data - Pointer point to l2cap data packet
 * @param[in]	report_id - the data packet of report id
 * @return      0 - usb is busy and forbidden report hid mic
 *              1 - usb allow to report hid mic
 */
unsigned char usb_report_hid_mic(u8* data, u8 report_id)
{
	if(usbhw_is_ep_busy(USB_EDP_AUDIO_IN))
		return 0;

	reg_usb_ep_ptr(USB_EDP_AUDIO_IN) = 0;

	reg_usb_ep_dat(USB_EDP_AUDIO_IN) = report_id;
	foreach(i, 20){
		reg_usb_ep_dat(USB_EDP_AUDIO_IN) = data[i];
	}

	reg_usb_ep_ctrl(USB_EDP_AUDIO_IN) = FLD_EP_DAT_ACK;		// ACK
	return 1;
}

/**
 * @brief		reset mic_packet,reset audio id and writer pointer and read pointer
 * @param[in]	none
 * @return      none
 */
void mic_packet_reset(void)
{
	audio_id = 0;
	usb_mic_wptr = 0;
	usb_mic_rptr = 0;
}

/**
 * @brief		push_mic_packet
 * @param[in]	p - Pointer point to l2cap data packet
 * @return      none
 */
void push_mic_packet(unsigned char *p)
{
	memcpy(mic_dat_buff[usb_mic_wptr], p, MIC_DATA_LEN);

	usb_mic_wptr =  (usb_mic_wptr+1)&(MIC_BUFF_NUM-1);
}

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void proc_audio (void)
{
	if(usb_mic_wptr != usb_mic_rptr)
	{
		static u32 cnt = 0;
		u8 *pdat = mic_dat_buff[usb_mic_rptr];
		if(usb_report_hid_mic(pdat, (audio_id + 10))==1)
		{
			audio_id += 1;
			audio_id = audio_id%3;
			cnt += 1;
			usb_mic_rptr =  (usb_mic_rptr+1)&(MIC_BUFF_NUM-1);
		}
	}
	return;
}

#elif  (TL_AUDIO_MODE == TL_AUDIO_DONGLE_SBC_HID)			//HID Service,SBC,Dongle decode

u8		att_mic_rcvd = 0;
u32		tick_adpcm;

u32		tick_iso_in;
int		mode_iso_in;


extern u8 tmp_mic_data[];

/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void  usb_endpoints_irq_handler (void)
{
	u32 t = clock_time ();
	/////////////////////////////////////
	// ISO IN
	/////////////////////////////////////
	if (reg_usb_irq & BIT(7)) {
		mode_iso_in = 1;
		tick_iso_in = t;
		reg_usb_irq = BIT(7);	//clear interrupt flag of endpoint 7

		/////// get MIC input data ///////////////////////////////
		//usb_iso_in_1k_square ();
		//usb_iso_in_from_mic ();
		abuf_dec_usb ();
	}

}

/**
 * @brief		call this function to process when attHandle equal to AUDIO_HANDLE_MIC
 * @param[in]	conn - connect handle
 * @param[in]	p - Pointer point to l2cap data packet.
 * @return      none
 */
void	att_mic (u16 conn, u8 *p)
{
	(void)conn;
	att_mic_rcvd = 1;
	memcpy (tmp_mic_data, p, MIC_ADPCM_FRAME_SIZE);
	abuf_mic_add ((u32 *)tmp_mic_data);
}

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void proc_audio (void)
{
	if (att_mic_rcvd)
	{
		tick_adpcm = clock_time ();
		att_mic_rcvd = 0;
	}
	if (clock_time_exceed (tick_adpcm, 500*1000))
	{
		tick_adpcm = clock_time ();
		abuf_init ();
	}
	abuf_mic_dec ();
}
#elif  (TL_AUDIO_MODE == TL_AUDIO_DONGLE_MSBC_HID)				//HID Service, MSBC, Dongle decode

u8		att_mic_rcvd = 0;
u32		tick_adpcm;

u32		tick_iso_in;
int		mode_iso_in;


extern u8 tmp_mic_data[];

/**
 * @brief		usb_endpoints_irq_handler
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void  usb_endpoints_irq_handler (void)
{
	u32 t = clock_time ();
	/////////////////////////////////////
	// ISO IN
	/////////////////////////////////////
	if (reg_usb_irq & BIT(7)) {
		mode_iso_in = 1;
		tick_iso_in = t;
		reg_usb_irq = BIT(7);	//clear interrupt flag of endpoint 7

		/////// get MIC input data ///////////////////////////////
		//usb_iso_in_1k_square ();
		//usb_iso_in_from_mic ();
		abuf_dec_usb ();
	}

}

/**
 * @brief		call this function to process when attHandle equal to AUDIO_HANDLE_MIC
 * @param[in]	conn - connect handle
 * @param[in]	p - Pointer point to l2cap data packet.
 * @return      none
 */
void	att_mic (u16 conn, u8 *p)
{
	(void)conn;
	att_mic_rcvd = 1;
	memcpy (tmp_mic_data, p, MIC_ADPCM_FRAME_SIZE);
	abuf_mic_add ((u32 *)tmp_mic_data);
}

/**
 * @brief		audio proc in main loop
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void proc_audio (void)
{
	if (att_mic_rcvd)
	{
		tick_adpcm = clock_time ();
		att_mic_rcvd = 0;
	}
	if (clock_time_exceed (tick_adpcm, 500*1000))
	{
		tick_adpcm = clock_time ();
		abuf_init ();
	}
	abuf_mic_dec ();
}
#endif

