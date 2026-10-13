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

#if (BIBOO_UX_USER_MODULE)
#include "stack/ble/hci/hci_event.h"	/* event_adv_report_t */
#include "mouse_fwd/ux_mouse_fwd.h"
#include "voice_dec/ux_voice_dec.h"
#include "host_link/ux_host_link.h"
#include "spp/ux_spp.h"
#include "user_data/ux_user_data.h"
#include "led/ux_led.h"
#include "factory_test/ux_factory_test.h"
#include "factory_test/ux_ft_dut.h"	/*BIBOO UX: DUT-side factory test (mouse builds only, see switch BIBOO_UX_FACTORY_TEST_DUT)*/
#include "factory_test/ux_ft_touch.h"	/*BIBOO UX: DUT-side touch sample source (0x22 events, weak driver adapter)*/
#endif


/* user module init entry (see user_module_manage.h for the hook locations) */
void user_module_init(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_DEBUG)
	/*build stamp: verify at power-up that the freshly flashed binary is the
	 *one just built (helps catching stale firmware during line bring-up)*/
	printf("[ux] user modules init, build " __DATE__ " " __TIME__ "\r\n");
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MIC_STREAM)
	ux_mic_stream_init();
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MOUSE_FWD)
	ux_mouse_fwd_init();	/*BIBOO UX: power-on -> enter auto pairing automatically*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	ux_voice_dec_init();	/*BIBOO UX: register SDP callback for the voice characteristic (0xB03E)*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK)
	ux_host_link_init();	/*BIBOO UX: reset host link (HID 0x05) channel state*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_SPP)
	ux_spp_init();		/*BIBOO UX: factory SPP data channel (0x33F3/0x33F4, see vendor/user/spp)*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_LED)
	ux_led_init();		/*BIBOO UX: five-color LED engine (see vendor/user/led)*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_AUTH)
	ux_user_data_init();	/*BIBOO UX: authorization tick data layer (flash 0x70000, see vendor/user/user_data)*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST)
	ux_factory_test_init();	/*BIBOO UX: factory-test module (host link 0x10..0x13/0x40 + SPP bridging)*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT)
	ux_ft_dut_init();	/*BIBOO UX: DUT-side factory test (SPP 0x33F3/0x33F4 + auth flash + AES)*/
	ux_ft_touch_init();	/*BIBOO UX: touch sample source (weak simulated driver, real IC overrides)*/
#endif
}

/* user module loop entry (see user_module_manage.h for the hook locations) */
void user_module_proc(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MIC_STREAM)
	ux_mic_stream_proc();
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MOUSE_FWD)
	ux_mouse_fwd_notify_proc();	/*BIBOO UX: enable HID report notify (CCC) after SMP&SDP done*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	ux_voice_dec_ccc_proc();	/*BIBOO UX: subscribe mouse voice notify (CCC) after SMP&SDP done*/
	ux_voice_dec_proc();		/*BIBOO UX: decode pending mSBC voice frames to PCM*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK)
	ux_host_link_proc();		/*BIBOO UX: async host link command extension point*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_SPP)
	ux_spp_proc();			/*BIBOO UX: enable DUT factory SPP notify (CCC) after SMP&SDP done*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_LED)
	ux_led_proc();			/*BIBOO UX: non-blocking LED blink engine + mouse activity window*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_AUTH)
	ux_user_data_proc();	/*BIBOO UX: 1s tick LED indication + DUT auth state polling (0x78)*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST)
	ux_factory_test_proc();	/*BIBOO UX: FT frame bridge / DUT link events / reply timeout*/
#endif
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT)
	ux_ft_dut_proc();		/*BIBOO UX: DUT-side factory test command loop (0x33F3 rx fifo)*/
	ux_ft_touch_proc();		/*BIBOO UX: 20ms touch sampling while the 0x22 event stream is on*/
#endif
}


#if (BIBOO_UX_USER_MODULE)
/* ================ dongle-side event hooks (see user_module_manage.h) ================ */

int user_module_adv_auto_pair(void *pa, s8 rssi)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MOUSE_FWD)
	return ux_mouse_fwd_adv_auto_pair((event_adv_report_t *)pa, rssi);
#else
	return 0;
#endif
}

void user_module_disconnect_evt(u8 reason)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MOUSE_FWD)
	ux_mouse_fwd_disconnect_evt(reason);
#endif
}

void user_module_unpair_triggered(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MOUSE_FWD)
	ux_mouse_fwd_set_unpair_no_repair();
#endif
}

void user_module_att_mouse(u8 *p)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_MOUSE_FWD)
	ux_mouse_fwd_att_mouse(p);
#endif
}

void user_module_att_mouse_led(u8 *p)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_LED)
	ux_led_att_mouse(p);
#endif
}

int user_module_att_mouse_hlink(u8 *p)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK)
	return ux_host_link_att_mouse(p);
#else
	return 0;
#endif
}

u16 user_module_voice_att_handle(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	return ux_voice_dec_att_handle;
#else
	return 0;
#endif
}

void user_module_voice_rx(u16 len, u8 *p)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	ux_voice_dec_att_mic(len, p);
#endif
}

u8 user_module_voice_active(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	return ux_voice_dec_active();
#else
	return 0;
#endif
}

void user_module_voice_dec_proc(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	ux_voice_dec_proc();
#endif
}

void user_module_voice_usb(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	ux_voice_dec_usb();
#endif
}

void user_module_host_link_rx(u8 len, u8 *p)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK)
	ux_host_link_rx(len, p);
#endif
}

#if (BIBOO_UX_SINE_TEST)
void user_module_voice_usb_sine(void)
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_VOICE_DEC)
	ux_voice_dec_usb_sine();
#endif
}
#endif

#endif /* BIBOO_UX_USER_MODULE */
