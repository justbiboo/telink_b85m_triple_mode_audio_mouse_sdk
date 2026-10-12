/********************************************************************************************************
 * @file    ux_ft_touch.c
 *
 * @brief   BIBOO UX: DUT-side touch sample source of the factory test
 *          (implementation).
 *
 *          - ux_ft_touch_proc() (main loop, dispatched by
 *            user_module_manage.c): while the touch stream is on
 *            (ux_ft_dut_touch_on()), every UX_FT_TOUCH_POLL_US it calls
 *            ux_ft_touch_hw_read() and feeds the sample to ux_ft_touch_poll()
 *          - ux_ft_touch_poll() forwards to ux_ft_dut_touch_report(), the
 *            0x22 event notify on 0x33F4 (see ux_ft_dut.c)
 *          - ux_ft_touch_hw_read() default implementation is a WEAK simulated
 *            sweep (finger down, position 0 -> 992 -> 0 at 16 units/sample,
 *            lifted for one sample at both ends) so the dongle + PC tool
 *            chain is testable without touch hardware; the real touch driver
 *            overrides the symbol with a strong definition
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"

#include "ux_ft_touch.h"
#include "ux_ft_dut.h"		/*ux_ft_dut_touch_on / ux_ft_dut_touch_report*/

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT)

/* ================ state ================ */

static u32 ux_ft_touch_tick;	/*clock_time() of the last sample*/


/* ================ driver adapter (weak default: simulated source) ================ */

/**
 * @brief      simulated touch sample source: finger-down sweep, position
 *             0 -> 992 -> 0 in 16-unit steps (~1.25s per half sweep at the
 *             20ms period), finger lifted for one sample at both ends.
 *             WEAK: a real touch driver provides a strong
 *             ux_ft_touch_hw_read() and overrides this definition.
 * @param[out] pressed - 1 = finger down / 0 = finger up
 * @param[out] pos     - touch position
 * @return     1 = fresh sample available (the simulation always has one)
 */
__attribute__((weak)) u8 ux_ft_touch_hw_read(u8 *pressed, u16 *pos)
{
	static u16 sim_pos;
	static u8  sim_dir;		/*0 = sweep up / 1 = sweep down*/
	u8 lift = 0;

	if (sim_dir == 0)
	{
		if (sim_pos >= 992)
		{
			sim_dir = 1;	/*reverse at the right end*/
			lift = 1;
		}
		else
		{
			sim_pos = (u16)(sim_pos + 16);
		}
	}
	else
	{
		if (sim_pos == 0)
		{
			sim_dir = 0;	/*reverse at the left end*/
			lift = 1;
		}
		else
		{
			sim_pos = (u16)(sim_pos - 16);
		}
	}

	*pressed = lift ? 0 : 1;
	*pos = sim_pos;
	return 1;
}


/* ================ module entry ================ */

void ux_ft_touch_init(void)
{
	ux_ft_touch_tick = clock_time();

	printf("[ux_ft_touch] init (source: weak default -> simulated)\r\n");
}

void ux_ft_touch_proc(void)
{
	u8 pressed;
	u16 pos;

	if (!ux_ft_dut_touch_on())
	{
		return;				/*touch stream off (outside the 0x20..0x21 window)*/
	}

	if (!clock_time_exceed(ux_ft_touch_tick, UX_FT_TOUCH_POLL_US))
	{
		return;				/*sampling period not reached*/
	}
	ux_ft_touch_tick = clock_time();

	if (ux_ft_touch_hw_read(&pressed, &pos))
	{
		ux_ft_touch_poll(pressed, pos);
	}
}

void ux_ft_touch_poll(u8 pressed, u16 pos)
{
	if (!ux_ft_dut_touch_on())
	{
		return;
	}

	ux_ft_dut_touch_report(pressed, pos);	/*-> 0x22 notify, seq maintained there*/
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT */
