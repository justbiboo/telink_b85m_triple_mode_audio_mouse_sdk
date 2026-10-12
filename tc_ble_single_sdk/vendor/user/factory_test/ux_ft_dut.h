/********************************************************************************************************
 * @file    ux_ft_dut.h
 *
 * @brief   BIBOO UX: DUT-side factory test (mouse firmware).
 *          Responds to the production-test commands arriving on the factory
 *          SPP channel (ATT write 0x33F3 / notify 0x33F4, see ux_ft_proto.h
 *          surface 2):
 *            - factory auth 0x77 (store the tick, flash 0x70000) / 0x78
 *            - PING 0x01 / AES_ENC 0x10 / AES_DEC 0x11 / AUTH 0x12
 *            - TOUCH_START 0x20 / TOUCH_STOP 0x21 + touch events 0x22
 *
 *          Wiring (mouse project, per model):
 *            - switch BIBOO_UX_FACTORY_TEST_DUT + BIBOO_UX_FT_DUT_MODEL in
 *              AAA_app_config.h
 *            - SPP ATT service (0x33F0/0x33F3/0x33F4) + sppWrite in
 *              AAA_app_att.c / AAA_app.c
 *            - init/proc dispatched by user_module_manage.c
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_FT_DUT_H_
#define UX_FT_DUT_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/
#include "ux_ft_proto.h"			/*BIBOO UX factory-test protocol (surface 2)*/

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT)

/* ================ configuration ================ */

/* DUT firmware version, answered by PING (rsp [1]) */
#define UX_FT_DUT_FW_VERSION			0x01

/* reported model id (PING rsp [2]); the mouse project header defines
 * BIBOO_UX_FT_DUT_MODEL, the guard keeps the module compilable without it */
#ifndef BIBOO_UX_FT_DUT_MODEL
#define BIBOO_UX_FT_DUT_MODEL			0x58	/*8258 dual mouse*/
#endif

/* ================ factory auth protocol (mirrors ux_spp.h) ================
 * The DUT answers the factory-native auth commands; keep these values in
 * sync with vendor/user/spp/ux_spp.h (dongle side): SppAppMsgCmd_e,
 * AuthRspResult_e, auth_status_t. */
#define UX_FT_DUT_AUTH_CMD				0x77	/*AUTH_CMD: [1]=Tick -> rsp [2]=result*/
#define UX_FT_DUT_GET_AUTH_STATE_CMD	0x78	/*GET_AUTH_STATE: rsp [2]=state*/

/* [2] of the AUTH_CMD response (ux_spp.h AuthRspResult_e) */
#define UX_FT_DUT_AUTH_RESULT_FAIL		0
#define UX_FT_DUT_AUTH_RESULT_ALREADY	1
#define UX_FT_DUT_AUTH_RESULT_SUCCESS	2

/* [2] of the GET_AUTH_STATE response (ux_spp.h auth_status_t) */
#define UX_FT_DUT_AUTH_STATE_VALID		0
#define UX_FT_DUT_AUTH_STATE_INVALID	1

/* ================ AES key ================ */
/* 16-byte AES-128 key shared with the PC tool (BibooUxTool: DongleLink.cs);
 * AES_ENC 0x10 / AES_DEC 0x11 / AUTH 0x12 all use this key. The PC tool must
 * use the exact same bytes. */
#define UX_FT_DUT_AES_KEY	{0x42,0x49,0x42,0x4F,0x4F,0x2D,0x55,0x58,0x2D,0x46,0x54,0x2D,0x30,0x30,0x30,0x31}
							/*"BIBOO-UX-FT-0001" in ASCII*/

/* ================ module entry (dispatched by user_module_manage.c) ================ */
void ux_ft_dut_init(void);
void ux_ft_dut_proc(void);

/* SPP rx entry: called by sppWrite() (AAA_app.c) on a 0x33F3 write.
 * Only frames the data here; the command runs in ux_ft_dut_proc() (main
 * loop) to keep the ATT rx context short. */
void ux_ft_dut_rx(u8 *p, u8 len);

/* touch sample entry (fed by the ft_touch module):
 * pressed != 0 = finger down, pos = touch position; notifies
 * UX_FT_DUT_EVT_TOUCH when the touch stream is on (0x20 .. 0x21) */
void ux_ft_dut_touch_report(u8 pressed, u16 pos);

/* 1 while the touch event stream is active (TOUCH_START received) */
u8 ux_ft_dut_touch_on(void);

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT */

#endif /* UX_FT_DUT_H_ */
