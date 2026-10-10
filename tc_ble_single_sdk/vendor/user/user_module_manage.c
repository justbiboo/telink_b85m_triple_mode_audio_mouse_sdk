/********************************************************************************************************
 * @file    user_module_manage.c
 *
 * @brief   BIBOO UX user module manager — dispatches to every self-contained UX
 *          module under "vendor/user/". Add one init call and one proc call per
 *          new module here, each guarded by "BIBOO_UX_USER_MODULE && <module switch>".
 *          The module switch is undefined (== 0) on the projects that do not
 *          have the module, so this file compiles on both the mouse and the
 *          dongle builds unchanged.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "user_module_manage.h"
#include "mic_stream/ux_mic_stream.h"

#if (BIBOO_UX_ENABLE)
#include "stack/ble/hci/hci_event.h"	/* event_adv_report_t */
#include "mouse_fwd/ux_mouse_fwd.h"
#include "voice_dec/ux_voice_dec.h"
#endif


/* user module init entry (see user_module_manage.h for the hook locations) */
void user_module_init(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MIC_STREAM)
	ux_mic_stream_init();
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_ENABLE)
	ux_mouse_fwd_init();	/*BIBOO UX: power-on -> enter auto pairing automatically*/
	ux_voice_dec_init();	/*BIBOO UX: register SDP callback for the voice characteristic (0xB03E)*/
#endif
}

/* user module loop entry (see user_module_manage.h for the hook locations) */
void user_module_proc(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MIC_STREAM)
	ux_mic_stream_proc();
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_ENABLE)
	ux_mouse_fwd_notify_proc();	/*BIBOO UX: enable HID report notify (CCC) after SMP&SDP done*/
	ux_voice_dec_ccc_proc();	/*BIBOO UX: subscribe mouse voice notify (CCC) after SMP&SDP done*/
	ux_voice_dec_proc();		/*BIBOO UX: decode pending mSBC voice frames to PCM*/
#endif
}


#if (BIBOO_UX_ENABLE)
/* ================ dongle-side event hooks (see user_module_manage.h) ================ */

int user_module_adv_auto_pair(void *pa, s8 rssi)
{
#if (BIBOO_UX_USER_MODULE)
	return ux_mouse_fwd_adv_auto_pair((event_adv_report_t *)pa, rssi);
#else
	return 0;
#endif
}

void user_module_disconnect_evt(u8 reason)
{
#if (BIBOO_UX_USER_MODULE)
	ux_mouse_fwd_disconnect_evt(reason);
#endif
}

void user_module_unpair_triggered(void)
{
#if (BIBOO_UX_USER_MODULE)
	ux_mouse_fwd_set_unpair_no_repair();
#endif
}

void user_module_att_mouse(u8 *p)
{
#if (BIBOO_UX_USER_MODULE)
	ux_mouse_fwd_att_mouse(p);
#endif
}

u16 user_module_voice_att_handle(void)
{
#if (BIBOO_UX_USER_MODULE)
	return ux_voice_dec_att_handle;
#else
	return 0;
#endif
}

void user_module_voice_rx(u16 len, u8 *p)
{
#if (BIBOO_UX_USER_MODULE)
	ux_voice_dec_att_mic(len, p);
#endif
}

u8 user_module_voice_active(void)
{
#if (BIBOO_UX_USER_MODULE)
	return ux_voice_dec_active();
#else
	return 0;
#endif
}

void user_module_voice_dec_proc(void)
{
#if (BIBOO_UX_USER_MODULE)
	ux_voice_dec_proc();
#endif
}

void user_module_voice_usb(void)
{
#if (BIBOO_UX_USER_MODULE)
	ux_voice_dec_usb();
#endif
}

#if (BIBOO_UX_SINE_TEST)
void user_module_voice_usb_sine(void)
{
#if (BIBOO_UX_USER_MODULE)
	ux_voice_dec_usb_sine();
#endif
}
#endif

#endif /* BIBOO_UX_ENABLE */
