/********************************************************************************************************
 * @file    ux_led.c
 *
 * @brief   BIBOO UX: five-color LED indication engine (implementation).
 *          Ported from the mass-production dongle SDK
 *          (tc_ble_single_sdk_factory, vendor/user/user_module_manage.c /
 *          blm_att.c control_led_by_mouse_btn), with the blocking sleep_ms
 *          blink replaced by a timer-driven state machine.
 *
 *          Boards: BOARD_825X_DONGLE_C1T139A3 (B85) / BOARD_827X_DONGLE_C1T201A3 (B87),
 *          LED gpio macros come from the board header (UI_LED_ENABLE=1).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"

#include "ux_led.h"

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_LED)

/* ================ state ================ */

static u8 ux_led_state[UX_LED_COLOR_NUM];	/*last commanded logical state per color (mirror, dedupes the gpio access)*/

typedef struct
{
	u8  active;			/*blink sequence armed*/
	u8  phase;			/*0 = on phase, 1 = off phase*/
	u8  times_left;		/*remaining on pulses*/
	u32 on_time_us;
	u32 off_time_us;
	u32 tick;			/*clock_time() at the start of the current phase*/
} ux_led_blink_slot_t;

static ux_led_blink_slot_t ux_led_blink_slot[UX_LED_COLOR_NUM];

static u8  ux_led_mouse_active = 0;		/*mouse movement activity flag (factory mouse_state)*/
static u32 ux_led_tick_xy = 0;			/*factory tick_xy: 7ms counter of the activity window*/
static u32 ux_led_tick_7ms = 0;			/*7ms scheduler tick*/


/* ================ low level write ================ */

/**
 * @brief      write one LED, with the board active level applied and a
 *             per-color state mirror to dedupe redundant gpio accesses
 * @param[in]  color - target LED
 * @param[in]  state - logical state
 * @return     none
 */
static void ux_led_write(ux_led_color_t color, ux_led_state_t state)
{
	if (color >= UX_LED_COLOR_NUM)
	{
		return;
	}

	if (ux_led_state[color] == (u8)state)
	{
		return;		/*already at that level*/
	}

	u8 level = (state == UX_LED_ON) ? (u8)LED_ON_LEVEL : (u8)(!LED_ON_LEVEL);

	switch (color)
	{
		case UX_LED_RED:	gpio_write(GPIO_LED_RED, level);	break;
		case UX_LED_WHITE:	gpio_write(GPIO_LED_WHITE, level);	break;
		case UX_LED_GREEN:	gpio_write(GPIO_LED_GREEN, level);	break;
		case UX_LED_BLUE:	gpio_write(GPIO_LED_BLUE, level);	break;
		case UX_LED_YELLOW:	gpio_write(GPIO_LED_YELLOW, level);	break;
		default:											return;
	}

	ux_led_state[color] = (u8)state;
}


/* ================ public API ================ */

void ux_led_set(ux_led_color_t color, ux_led_state_t state)
{
	if (color >= UX_LED_COLOR_NUM)
	{
		return;
	}

	ux_led_blink_slot[color].active = 0;	/*manual control cancels the blink sequence*/
	ux_led_write(color, state);
}

/**
 * @brief      all LEDs off (five colors; factory led_all_off left the red LED
 *             alone, here everything is turned off as required by the plan)
 */
void ux_led_all_off(void)
{
	ux_led_set(UX_LED_RED, UX_LED_OFF);
	ux_led_set(UX_LED_WHITE, UX_LED_OFF);
	ux_led_set(UX_LED_GREEN, UX_LED_OFF);
	ux_led_set(UX_LED_BLUE, UX_LED_OFF);
	ux_led_set(UX_LED_YELLOW, UX_LED_OFF);
}

/**
 * @brief      factory led_all_on semantics: red/white/green/yellow on, blue untouched
 */
void ux_led_all_on(void)
{
	ux_led_set(UX_LED_RED, UX_LED_ON);
	ux_led_set(UX_LED_WHITE, UX_LED_ON);
	ux_led_set(UX_LED_GREEN, UX_LED_ON);
	ux_led_set(UX_LED_YELLOW, UX_LED_ON);
}

void ux_led_blink(ux_led_color_t color, u32 on_time_ms, u32 off_time_ms, u8 times)
{
	if (color >= UX_LED_COLOR_NUM || times == 0)
	{
		return;
	}

	ux_led_blink_slot_t *slot = &ux_led_blink_slot[color];

	slot->active     = 1;
	slot->phase      = 0;					/*start with the on phase*/
	slot->times_left = times;
	slot->on_time_us  = on_time_ms * 1000;
	slot->off_time_us = off_time_ms * 1000;
	slot->tick       = clock_time();

	ux_led_write(color, UX_LED_ON);
}

/**
 * @brief      main loop housekeeping (user_module_manage.c dispatcher):
 *             drives the blink slots and the mouse activity window
 */
void ux_led_proc(void)
{
	u8 color;

	/* ============ non-blocking blink engine ============ */
	for (color = 0; color < UX_LED_COLOR_NUM; color++)
	{
		ux_led_blink_slot_t *slot = &ux_led_blink_slot[color];

		if (!slot->active)
		{
			continue;
		}

		if (slot->phase == 0)
		{
			/*on phase -> off when the on time has elapsed*/
			if (clock_time_exceed(slot->tick, slot->on_time_us))
			{
				ux_led_write((ux_led_color_t)color, UX_LED_OFF);
				slot->phase = 1;
				slot->tick  = clock_time();
			}
		}
		else
		{
			/*off phase -> next on pulse, or the sequence is finished*/
			if (clock_time_exceed(slot->tick, slot->off_time_us))
			{
				slot->times_left--;
				if (slot->times_left == 0)
				{
					slot->active = 0;	/*finished, LED stays off*/
				}
				else
				{
					ux_led_write((ux_led_color_t)color, UX_LED_ON);
					slot->phase = 0;
					slot->tick  = clock_time();
				}
			}
		}
	}

	/* ============ mouse activity window (factory tick_xy logic) ============
	 * ux_led_att_mouse() flags any mouse movement (x/y/wheel != 0) and resets
	 * the counter; here the flag is dropped after ~77ms without new movement
	 * (11 x 7ms), so the green LED mirrors the recent activity */
	if (clock_time_exceed(ux_led_tick_7ms, 7000))
	{
		ux_led_tick_7ms = clock_time();

		if (ux_led_mouse_active && (ux_led_tick_xy > 10))
		{
			ux_led_mouse_active = 0;
			ux_led_tick_xy = 0;
		}
		else
		{
			ux_led_tick_xy++;
		}

		ux_led_write(UX_LED_GREEN, ux_led_mouse_active ? UX_LED_ON : UX_LED_OFF);
	}
}

/**
 * @brief      mouse report hook, called by att_mouse() through
 *             user_module_att_mouse_led() (blm_att.c, module dispatcher)
 * @param[in]  p - BLE mouse report (btn + x + y + wheel, layout per MOUSE_DATA_LEN_AAA)
 * @return     none
 */
void ux_led_att_mouse(u8 *p)
{
	u8 btn = p[0];
	u8 moving = 0;

#if (MOUSE_DATA_LEN_AAA == 6)
	/*btn + s16 x + s16 y + wheel*/
	s16 x = (s16)(p[1] | (p[2] << 8));
	s16 y = (s16)(p[3] | (p[4] << 8));
	if (x != 0 || y != 0 || p[5] != 0)
	{
		moving = 1;
	}
#elif (MOUSE_DATA_LEN_AAA == 4)
	/*btn + s8 x + s8 y + wheel*/
	if (p[1] != 0 || p[2] != 0 || p[3] != 0)
	{
		moving = 1;
	}
#endif

	if (moving)
	{
		ux_led_mouse_active = 1;
		ux_led_tick_xy = 0;
	}
	else
	{
		ux_led_mouse_active = 0;
	}

	/*factory control_led_by_mouse_btn semantics: any button pressed -> yellow on*/
	ux_led_set(UX_LED_YELLOW, btn ? UX_LED_ON : UX_LED_OFF);
}


/* ================ module entry ================ */

void ux_led_init(void)
{
	/* the original dongle code never explicitly initialized the LED gpio;
	 * configure all five colors here so that ux_led owns the full set */
	gpio_set_func(GPIO_LED_RED, AS_GPIO);
	gpio_set_output_en(GPIO_LED_RED, 1);
	gpio_set_input_en(GPIO_LED_RED, 0);
	gpio_write(GPIO_LED_RED, !LED_ON_LEVEL);

	gpio_set_func(GPIO_LED_WHITE, AS_GPIO);
	gpio_set_output_en(GPIO_LED_WHITE, 1);
	gpio_set_input_en(GPIO_LED_WHITE, 0);
	gpio_write(GPIO_LED_WHITE, !LED_ON_LEVEL);

	gpio_set_func(GPIO_LED_GREEN, AS_GPIO);
	gpio_set_output_en(GPIO_LED_GREEN, 1);
	gpio_set_input_en(GPIO_LED_GREEN, 0);
	gpio_write(GPIO_LED_GREEN, !LED_ON_LEVEL);

	gpio_set_func(GPIO_LED_BLUE, AS_GPIO);
	gpio_set_output_en(GPIO_LED_BLUE, 1);
	gpio_set_input_en(GPIO_LED_BLUE, 0);
	gpio_write(GPIO_LED_BLUE, !LED_ON_LEVEL);

	gpio_set_func(GPIO_LED_YELLOW, AS_GPIO);
	gpio_set_output_en(GPIO_LED_YELLOW, 1);
	gpio_set_input_en(GPIO_LED_YELLOW, 0);
	gpio_write(GPIO_LED_YELLOW, !LED_ON_LEVEL);

	memset(ux_led_state, UX_LED_OFF, sizeof(ux_led_state));
	memset(ux_led_blink_slot, 0, sizeof(ux_led_blink_slot));

	ux_led_mouse_active = 0;
	ux_led_tick_xy = 0;
	ux_led_tick_7ms = clock_time();
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_LED */
