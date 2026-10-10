/********************************************************************************************************
 * @file    ux_voice_dec.h
 *
 * @brief   BIBOO UX module (dongle side): BLE voice stream decoder
 *          (mouse -> dongle -> PC microphone). In BLE mode the mouse pushes
 *          57-byte mSBC voice frames (7.5ms @16K) on its "my_Data"
 *          characteristic (uuid16 0xB03E @ service 0xFFF0); this module
 *          buffers, decodes (mSBC -> 16K PCM) and feeds the PCM to USB
 *          ISO IN EP7. Logic migrated from
 *          vendor/ble_master_kma_dongle/{app_audio.c,blm_host.c,app.c}.
 *          Switch: BIBOO_UX_ENABLE (dongle app_config.h; undefined on the
 *          mouse project, where this module compiles to nothing).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_VOICE_DEC_H_
#define UX_VOICE_DEC_H_

#include "tl_common.h"
#include "vendor/user/user_module_manage.h"	/*BIBOO UX: brings in the master switch BIBOO_UX_USER_MODULE*/


#if (BIBOO_UX_ENABLE)

/*voice stream characteristic (my_Data, uuid16 0xB03E) value handle,
 *found by the SDP callback registered in ux_voice_dec_init*/
extern u16 ux_voice_dec_att_handle;

void ux_voice_dec_init(void);
void ux_voice_dec_ccc_proc(void);
void ux_voice_dec_att_mic(u16 len, u8 *p);
u8   ux_voice_dec_active(void);
void ux_voice_dec_proc(void);	/*main loop: decode pending mSBC frames to PCM*/
void ux_voice_dec_usb(void);	/*USB ISO IN irq: feed decoded PCM to EP7*/
#if (BIBOO_UX_SINE_TEST)
void ux_voice_dec_usb_sine(void);	/*USB ISO IN irq: 1kHz sine test tone (EP7)*/
#endif

#endif /* BIBOO_UX_ENABLE */


#endif /* UX_VOICE_DEC_H_ */
