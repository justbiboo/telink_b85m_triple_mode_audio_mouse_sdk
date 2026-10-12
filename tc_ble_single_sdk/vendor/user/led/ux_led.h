/********************************************************************************************************
 * @file    ux_led.h
 *
 * @brief   BIBOO UX: five-color LED indication engine, ported from the
 *          mass-production dongle SDK (tc_ble_single_sdk_factory,
 *          vendor/user/user_module_manage.c LED part).
 *
 *          Unlike the factory version (blocking sleep_ms blink), the blink
 *          here is fully non-blocking: ux_led_blink() only arms a timer slot,
 *          ux_led_proc() drives the on/off phases from the main loop.
 *
 *          factory color semantics:
 *            blue   : link state (steady on = connected, 1s blinking while not connected)
 *            green  : mouse movement activity (factory tick_xy window)
 *            yellow : mouse button pressed (control_led_by_mouse_btn)
 *            red    : authorization tick (<2 steady on, <500 blinking)
 *            white  : authorization invalid
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_LED_H_
#define UX_LED_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_LED)

/* logical LED state; the board active level (LED_ON_LEVEL) is applied inside ux_led.c */
typedef enum
{
	UX_LED_OFF = 0,
	UX_LED_ON  = 1,
} ux_led_state_t;

typedef enum
{
	UX_LED_RED = 0,
	UX_LED_WHITE,
	UX_LED_GREEN,
	UX_LED_BLUE,
	UX_LED_YELLOW,
	UX_LED_COLOR_NUM				/*number of physical LEDs, keep last*/
} ux_led_color_t;

/* module entry (user_module_manage.c dispatcher) */
void ux_led_init(void);
void ux_led_proc(void);

/* steady state control; a call on one color cancels its pending blink sequence */
void ux_led_set(ux_led_color_t color, ux_led_state_t state);

/* factory led_all_on semantics: red/white/green/yellow on (blue untouched) */
void ux_led_all_on(void);

/* all five LEDs off (plan requirement: disconnected -> everything off) */
void ux_led_all_off(void);

/**
 * @brief      non-blocking blink: arm the blink slot and return immediately;
 *             a new call on the same color restarts the sequence
 * @param[in]  color       - target LED
 * @param[in]  on_time_ms  - on phase duration (ms)
 * @param[in]  off_time_ms - off phase duration (ms)
 * @param[in]  times       - number of on pulses
 * @return     none
 */
void ux_led_blink(ux_led_color_t color, u32 on_time_ms, u32 off_time_ms, u8 times);

/**
 * @brief      mouse report hook (blm_att.c att_mouse): x/y/wheel != 0 -> green
 *             activity window (factory tick_xy logic), btn -> yellow
 * @param[in]  p - BLE mouse report, layout per MOUSE_DATA_LEN_AAA (app_config.h)
 * @return     none
 */
void ux_led_att_mouse(u8 *p);

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_LED */

#endif /* UX_LED_H_ */
