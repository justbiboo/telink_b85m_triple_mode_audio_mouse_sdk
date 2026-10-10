/********************************************************************************************************
 * @file    ux_voice_dec.c
 *
 * @brief   BIBOO UX module (dongle side): BLE voice stream decoder
 *          (mouse -> dongle -> PC microphone). Logic migrated from
 *          vendor/ble_master_kma_dongle/app_audio.c (originally wrapped by
 *          "BIBOO_UX_VOICE_DEC" at each site).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "ux_voice_dec.h"


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)

#include "drivers.h"							/* reg_usb_ep7_* (register.h) */
#include "stack/ble/ble.h"
#include "vendor/common/tlkapi_debug.h"			/* tlkapi_printf */
#include "vendor/common/simple_sdp.h"			/* app_sdp_register_get_att_handle_callback, blm_att_findHandleOfUuid16 */
#include "../../ble_master_kma_dongle/blm_host.h"	/* central_smp_pending */
#include "application/audio/tl_audio.h"			/* sbc_decode ... */
#include "application/audio/audio_config.h"		/* MIC_SAMPLE_RATE */
#include "application/audio/sbc.h"


/*----------------------------------------------------------------------------
 * BIBOO UX: BLE voice stream decoder (mouse -> dongle -> PC microphone).
 * In BLE mode the mouse pushes 57-byte mSBC voice frames (7.5ms @16K) on its
 * "my_Data" characteristic (uuid16 0xB03E @ service 0xFFF0). This module:
 *   1) ux_voice_dec_att_mic() - buffers each incoming 57-byte frame (called from
 *                               blm_host.c L2CAP notify handler)
 *   2) ux_voice_dec_proc()    - decodes mSBC frames to 16K PCM (main loop)
 *   3) ux_voice_dec_usb()     - feeds PCM to USB ISO IN EP7 (USB irq)
 * Used only while frames keep arriving (see ux_voice_dec_active()).
 *--------------------------------------------------------------------------*/
#define UX_MSBC_FRAME_LEN     57      //57-byte mSBC frame per 7.5ms
#define UX_MSBC_PCM_SAMPLES   120     //120 PCM samples (240 bytes) per frame
#define UX_MSBC_FRAME_NUM     8       //ring buffer frame count (power of 2)
#define UX_MSBC_DEC_SIZE      (UX_MSBC_PCM_SAMPLES * UX_MSBC_FRAME_NUM)
#define UX_USB_ISO_IN_SIZE    (MIC_SAMPLE_RATE / 1000)

/*voice stream characteristic (my_Data, uuid16 0xB03E) value handle,
 *found by the SDP callback below*/
#define UX_APP_DATA_IN_UUID   0xB03E
u16 ux_voice_dec_att_handle = 0;

static u8  ux_vdec_mic_wptr, ux_vdec_dec_wptr;
static u16 ux_vdec_dec_rptr;
static u8  ux_vdec_mic[UX_MSBC_FRAME_LEN * UX_MSBC_FRAME_NUM];
static s16 ux_vdec_pcm[UX_MSBC_DEC_SIZE];
static u8  ux_vdec_reset = 0;
static u8  ux_vdec_start = 1;
static u32 ux_vdec_tick = 0;

#if (BIBOO_UX_DEBUG)
/*BIBOO debug: voice chain counters (printed by dbg_ble_status_print in blm_host.c)*/
volatile u32 dbg_vbuf_cnt = 0;   //frames passed 57B filter & buffered
volatile u32 dbg_vdec_cnt = 0;   //frames decoded (sbc_decode called)
volatile u32 dbg_vwr_len = 0;    //last sbc_decode written bytes (expect 240)
volatile u32 dbg_visr_cnt = 0;   //USB ISO IN packets handled by BIBOO output chain
volatile u32 dbg_vusb_cnt = 0;   //USB ISO IN packets carrying real PCM
volatile u32 dbg_vcall_cnt = 0;  //ux_voice_dec_proc calls (main loop rate)
volatile u32 dbg_vdec_us = 0;    //last sbc_decode duration (us)
volatile u32 dbg_vdec_us_max = 0;//max sbc_decode duration (us)
volatile u32 dbg_rsta_cnt = 0;  //reset arm: new voice session (ux_voice_dec_att_mic)
volatile u32 dbg_rstb_cnt = 0;  //reset arm: usb iso gap>4ms (ux_voice_dec_usb)
volatile u32 dbg_rstblk_cnt = 0;//ux_voice_dec_proc calls blocked by reset flag
volatile u32 dbg_vskp_cnt = 0;  //reader-side frame drops (queue overrun guard in ux_voice_dec_usb)
volatile u32 dbg_loop_cnt = 0;  //main loop iterations (audio pacing observability)
#endif

/*user_init (dispatched from user_module_init): register the SDP callback that
 *finds the mouse voice stream characteristic (my_Data, uuid16 0xB03E), so that
 *notifications can be routed and CCC subscribed.*/
static void ux_voice_dec_sdp_cb(att_db_uuid16_t *p16, att_db_uuid128_t *p128);
void ux_voice_dec_init(void)
{
	#if (ACL_CENTRAL_SIMPLE_SDP_ENABLE) && (TL_AUDIO_MODE != TL_AUDIO_DONGLE_ADPCM_GATT_GOOGLE)
	app_sdp_register_get_att_handle_callback(&ux_voice_dec_sdp_cb);
	#endif
}

/*SDP callback: find mouse voice stream characteristic (my_Data, uuid16 0xB03E)
 *value handle, so that notifications can be routed and CCC subscribed.*/
static void ux_voice_dec_sdp_cb(att_db_uuid16_t *p16, att_db_uuid128_t *p128)
{
	(void)p128;
	ux_voice_dec_att_handle = blm_att_findHandleOfUuid16(p16, UX_APP_DATA_IN_UUID, 0);
	#if (BIBOO_UX_DEBUG)
	tlkapi_printf(APP_LOG_EN, "[DBG] BIBOO voice handle=%x\n", ux_voice_dec_att_handle);
	#endif
}

/*main loop (dispatched from user_module_proc): subscribe mouse voice notify
 *(CCC handle = voice value handle + 1) after SMP & SDP done.*/
void ux_voice_dec_ccc_proc(void)
{
	static u8 voice_ccc_done = 0;

	if(blc_ll_getCurrentState() != BLS_LINK_STATE_CONN)
	{
		voice_ccc_done = 0;  //reset for next connection
		return;
	}
	if(central_sdp_pending)  //SDP still ongoing
	{
		return;
	}
	#if (BLE_HOST_SMP_ENABLE)
	if(central_smp_pending)  //SMP pairing still ongoing
	{
		return;
	}
	#endif

	if(!voice_ccc_done && ux_voice_dec_att_handle)
	{
		u8 vccc[2] = {0x01, 0x00};
		if(!blc_gatt_pushWriteCommand(BLM_CONN_HANDLE, ux_voice_dec_att_handle + 1, vccc, 2))
		{
			voice_ccc_done = 1;
			#if (BIBOO_UX_DEBUG)
			tlkapi_printf(APP_LOG_EN, "[DBG] BIBOO voice CCC written (h=%x)\n", ux_voice_dec_att_handle + 1);
			#endif
		}
	}
}

/**
 * @brief		voice stream active? (a frame arrived within last 200ms)
 * @param[in]	none
 * @return		1 - active, 0 - idle
 */
_attribute_ram_code_ u8 ux_voice_dec_active (void)
{
	return ux_vdec_tick && !clock_time_exceed (ux_vdec_tick, 200000);
}

/**
 * @brief		call this when a notification of mouse my_Data characteristic arrives
 * @param[in]	len - value length (57 for voice frame, other lengths are ignored)
 * @param[in]	p - value data
 * @return      none
 */
void ux_voice_dec_att_mic (u16 len, u8 *p)
{
	if (len != UX_MSBC_FRAME_LEN)  //ignore short control frames on the same handle
	{
		return;
	}
	if (!ux_voice_dec_active ())  //new voice session: resync the decode chain
	{
		ux_vdec_mic_wptr = 0;
		ux_vdec_dec_wptr = 0;
			#if (BIBOO_UX_DEBUG)
				dbg_rsta_cnt ++;
			#endif
		ux_vdec_reset = 16;
		ux_vdec_start = 1;
	}
	ux_vdec_tick = clock_time () | 1;
	#if (BIBOO_UX_DEBUG)
	dbg_vbuf_cnt ++;
	#endif

	u8 *pd = ux_vdec_mic + (ux_vdec_mic_wptr & (UX_MSBC_FRAME_NUM - 1)) * UX_MSBC_FRAME_LEN;
	for (int i = 0; i < UX_MSBC_FRAME_LEN; i++)
	{
		*pd ++ = *p ++;
	}
	ux_vdec_mic_wptr ++;
}

/**
 * @brief		decode pending mSBC frames to PCM (main loop)
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void ux_voice_dec_proc (void)
{
	static u32 ux_vdec_decode_len = 0;
	#if (BIBOO_UX_DEBUG)
	dbg_vcall_cnt ++;
	#endif
	if (ux_vdec_reset)
	{
	#if (BIBOO_UX_DEBUG)
		dbg_rstblk_cnt ++;
	#endif
		ux_vdec_dec_wptr = ux_vdec_mic_wptr;
	}
	else
	{
		/*drain ALL pending frames in one call (bounded by ring size): the main loop
		 * runs well below the 133.3 frames/s required by the 7.5ms mSBC stream, so
		 * the old one-frame-per-call pacing silently dropped ~30% of the voice,
		 * heard as periodic glitches in the decoded tone*/
		for (u8 n = 0; n < UX_MSBC_FRAME_NUM; n++)
		{
			u8 num_mic = (u8)(ux_vdec_mic_wptr - ux_vdec_dec_wptr);
			if (num_mic > UX_MSBC_FRAME_NUM)  //in case of overflow
			{
				ux_vdec_dec_wptr ++;
				continue;
			}
			if ( (!ux_vdec_start && num_mic >= 1) || (ux_vdec_start && num_mic >= 2) )
			{
				#if (BIBOO_UX_DEBUG)
				u32 _vdec_t0 = clock_time ();
				#endif
				sbc_decode (ux_vdec_mic + (ux_vdec_dec_wptr & (UX_MSBC_FRAME_NUM - 1)) * UX_MSBC_FRAME_LEN, UX_MSBC_FRAME_LEN,
							ux_vdec_pcm + (ux_vdec_dec_wptr & (UX_MSBC_FRAME_NUM - 1)) * UX_MSBC_PCM_SAMPLES, UX_MSBC_PCM_SAMPLES * 2,
							&ux_vdec_decode_len);
				#if (BIBOO_UX_DEBUG)
				{
					u32 _vdec_dt = clock_time () - _vdec_t0;
					dbg_vdec_us = _vdec_dt;
					if (_vdec_dt > dbg_vdec_us_max) { dbg_vdec_us_max = _vdec_dt; }
				}
				dbg_vdec_cnt ++;
				dbg_vwr_len = ux_vdec_decode_len;
				#endif
				ux_vdec_dec_wptr ++;
				ux_vdec_start = 0;
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
_attribute_ram_code_ void ux_voice_dec_usb (void)
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
	ux_vdec_reset = 16;
	}
	tick_usb_iso_in = clock_time ();
	if (ux_vdec_reset)
	{
		ux_vdec_dec_rptr = ux_vdec_dec_wptr * UX_MSBC_PCM_SAMPLES;
		ux_vdec_reset --;
	}
	/////////////////// copy data to usb iso in buffer ///////////////
	reg_usb_ep7_ptr = 0;
	u8 num = ux_vdec_dec_wptr - (ux_vdec_dec_rptr / UX_MSBC_PCM_SAMPLES);
	if (num >= UX_MSBC_FRAME_NUM)		//queue overrun: reader fell a full ring behind, next decode would overwrite the frame being played
	{
		ux_vdec_dec_rptr += UX_MSBC_PCM_SAMPLES;	//drop the oldest queued frame (7.5ms) to restore one-slot distance
		if(ux_vdec_dec_rptr >= (UX_MSBC_PCM_SAMPLES << 8) ){
			ux_vdec_dec_rptr -= (UX_MSBC_PCM_SAMPLES << 8);
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
		if ( (buffer_empty && num >= (UX_MSBC_FRAME_NUM >> 1)) || (!buffer_empty && (num >= 1 || (n_usb_iso & (UX_MSBC_FRAME_NUM - 1)))) )
		{
			buffer_empty = 0;
			#if (BIBOO_UX_DEBUG)
			dbg_vusb_cnt ++;
			#endif
			u16 offset = ux_vdec_dec_rptr % UX_MSBC_DEC_SIZE;
			s16 *ps = ux_vdec_pcm + offset;
			if(offset == UX_MSBC_DEC_SIZE - (UX_USB_ISO_IN_SIZE / 2)){
				for (int i=0; i<(UX_USB_ISO_IN_SIZE / 2); i++)
				{
					reg_usb_ep7_dat = *ps;
					reg_usb_ep7_dat = *ps++ >> 8;
				}
				ps = ux_vdec_pcm;
				for (int i=0; i<(UX_USB_ISO_IN_SIZE / 2); i++)
				{
					reg_usb_ep7_dat = *ps;
					reg_usb_ep7_dat = *ps++ >> 8;
				}
			}
			else{
				for (int i=0; i<UX_USB_ISO_IN_SIZE; i++)
				{
					reg_usb_ep7_dat = *ps;
					reg_usb_ep7_dat = *ps++ >> 8;
				}
			}

			ux_vdec_dec_rptr += UX_USB_ISO_IN_SIZE;
			if(ux_vdec_dec_rptr >= (UX_MSBC_PCM_SAMPLES << 8) ){
				ux_vdec_dec_rptr -= (UX_MSBC_PCM_SAMPLES << 8);	//must SUBTRACT, not zero: rptr may be non-16-aligned (starts from dec_wptr*120)
			}
		}
		else
		{
			for (int i=0; i<UX_USB_ISO_IN_SIZE * 2; i++)
			{
				reg_usb_ep7_dat = 0;
			}
		}
	}
	else
	{
		for (int i=0; i<UX_USB_ISO_IN_SIZE * 2; i++)
		{
			reg_usb_ep7_dat = 0;
		}
		buffer_empty = 1;
	}
	reg_usb_ep7_ctrl = BIT(0);			//ACK iso in
}

#if (BIBOO_UX_SINE_TEST)
/*----------------------------------------------------------------------------
 * BIBOO UX test case: 1kHz sine test tone on USB ISO IN EP7.
 *   - While the PC captures the dongle microphone (EP7 polled every 1ms),
 *     each ISOC IN packet is filled with 16 samples @16K/16bit mono PCM =
 *     exactly one 1kHz sine period per packet.
 *   - Works as soon as USB is plugged and host capture starts, no BLE mouse.
 *   - 16-point LUT lives in RAM (.data): safe inside USB irq, no flash read.
 *--------------------------------------------------------------------------*/
#define UX_SINE_LUT_SIZE      16      //16 samples = one 1kHz period @16kHz

/* one full 1kHz sine period @16kHz, amplitude +/-8192 (-12dBFS) */
static s16 ux_sine_1k_lut[UX_SINE_LUT_SIZE] =
{
	    0,  3135,  5793,  7568,  8192,  7568,  5793,  3135,
	    0, -3135, -5793, -7568, -8192, -7568, -5793, -3135
};

/**
 * @brief		BIBOO UX test: fill USB ISO IN EP7 with a 1kHz sine test tone
 * 				(called in USB ISO IN irq, once per 1ms packet)
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void ux_voice_dec_usb_sine (void)
{
	reg_usb_ep7_ptr = 0;
	for (int i = 0; i < UX_SINE_LUT_SIZE; i++)
	{
		reg_usb_ep7_dat = (u8)(ux_sine_1k_lut[i]);
		reg_usb_ep7_dat = (u8)(ux_sine_1k_lut[i] >> 8);
	}
	reg_usb_ep7_ctrl = BIT(0);			//ACK iso in
}
#endif

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC */
