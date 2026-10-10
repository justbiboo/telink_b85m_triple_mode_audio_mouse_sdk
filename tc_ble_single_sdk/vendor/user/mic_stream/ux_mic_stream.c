/********************************************************************************************************
 * @file    ux_mic_stream.c
 *
 * @brief   BIBOO UX module: BLE mouse continuous mic stream (mouse side).
 *          Logic migrated from vendor/8258_dual_mouse/AAA_app.c
 *          (bibo_ble_mic_proc() + the MTU exchange block of main_loop()).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "../user_module_manage.h"
#include "ux_mic_stream.h"


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MIC_STREAM)

#include "drivers.h"
#include "stack/ble/ble.h"
/* mouse project public API: fun_mode, ui_mic_enable, audio_stick, audio_start,
 * ui_enable_mic, ui_mtu_size_exchange_req, RF_1M_BLE_MODE, BLS_CONN_HANDLE ... */
#include "../../8258_dual_mouse/AAA_public_config.h"
#include "application/audio/tl_audio.h"		/* audio_mic_param_init */


void ux_mic_stream_init(void)
{
	/* nothing to init: the stream starts automatically on the first
	 * BLE connection (see ux_mic_stream_proc) */
}

/* BIBOO UX: after BLE link-up, automatically open the mic and keep the
 * audio stream alive. proc_audio_ble() then keeps encoding the REAL mic
 * audio (16kHz/16bit/mono) into mSBC frames (57B/7.5ms), which are sent
 * to the dongle through GATT notify.
 * (With BIBOO_UX_SINE_TEST=1, proc_mic_encoder replaces the samples by a
 *  1kHz sine tone instead - end-to-end chain sanity check.) */
void ux_mic_stream_proc(void)
{
	if (fun_mode != RF_1M_BLE_MODE)
	{
		return;
	}

	if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
	{
		if (!ui_mic_enable)
		{
			audio_mic_param_init();             //reset mSBC encoder state
			audio_stick = clock_time() | 1;
			ui_enable_mic(1);                   //start AMIC -> 7.5ms encode tick
		}
		else if (audio_start)
		{
			audio_stick = clock_time() | 1;     //keep stream alive (no duration limit)
		}
	}

#if (!BLE_AUDIO_ENABLE)
	/* MTU exchange for the 57B notify: user_requestMtuSizeExchange() is only
	 * built when BLE_AUDIO_ENABLE==1, so perform the exchange here instead.
	 * (moved from the BIBOO_UX_MIC_STREAM block of AAA_app.c main_loop) */
	if (ui_mtu_size_exchange_req && blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
	{
		ui_mtu_size_exchange_req = 0;
		blc_att_requestMtuSizeExchange(BLS_CONN_HANDLE, 0x009e);
	}
#endif
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_MIC_STREAM */
