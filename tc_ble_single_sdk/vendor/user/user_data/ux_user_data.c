/********************************************************************************************************
 * @file    ux_user_data.c
 *
 * @brief   BIBOO UX: authorization-tick data layer (implementation).
 *          Ported from the mass-production dongle SDK
 *          (tc_ble_single_sdk_factory, vendor/user/user_data/user_data.c).
 *
 *          flash 0x70000: rolling 32-byte records (erase round-robin, 2 write
 *          retries + read-back verify); the newest record is at the highest
 *          offset. One tick is consumed per successful DUT auth challenge.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"	/*blc_ll_getCurrentState / BLS_LINK_STATE_CONN*/

#include "ux_user_data.h"

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_AUTH)
#include "../spp/ux_spp.h"	/*ux_spp_auth_cmd (0x77) / ux_spp_get_auth_state_cmd (0x78)*/
#include "../led/ux_led.h"	/*red = tick indication, blue = not connected blink*/

/* ================ flash layout (factory user_data.c) ================ */

#define UX_USER_DATA_CFG_ADDR	0x70000			/*factory CFG_DEVICE_MODE_ADDR*/
#define UX_USER_DATA_SAVE_MAX	32				/*factory SAVE_MAX_IN_FLASH: record stride of the load polling*/
#define UX_USER_DATA_MAX_LEN	64				/*factory max2(64, sizeof(FLASH_DEV_INFO_AAA))*/

#define UX_USER_DATA_SAVE_SUCCESS	0
#define UX_USER_DATA_SAVE_FAILED	-1

static ux_flash_dev_info_t ux_flash_dev_info;	/*factory kept this in the header; static here*/
static int ux_dev_info_idx;						/*rolling offset of the last stored record (-record size when empty)*/


/* ================ flash helpers ================ */

/**
 * @brief      write in 8-byte steps to avoid long flash busy times killing the
 *             BLE link (factory flash_write_page_user)
 */
static void ux_flash_write_page_user(unsigned long addr, unsigned long len, unsigned char *buf)
{
	u16 write_cnt = len / 8;
	u8 last_write_len = len % 8;

	for (int i = 0; i < write_cnt; i++)
	{
		flash_write_page((addr + 8 * i), 8, (buf + 8 * i));
		sleep_us(20);
	}

	if (last_write_len > 0)
	{
		flash_write_page((addr + 8 * write_cnt), last_write_len, (buf + 8 * write_cnt));
	}
}

/**
 * @brief      append one record at *offset (erase the sector and restart when
 *             the area is full), 2 write retries with read-back verify;
 *             updated *offset on success (factory save_data_to_flash)
 * @return     UX_USER_DATA_SAVE_SUCCESS / UX_USER_DATA_SAVE_FAILED
 */
static int ux_save_data_to_flash(unsigned long addr, unsigned long len, unsigned char *buf, int *offset)
{
	u8 tmp_buf[UX_USER_DATA_MAX_LEN];
	int tmp_offset = 0;

	if (len > UX_USER_DATA_MAX_LEN)
	{
		return UX_USER_DATA_SAVE_FAILED;
	}

	if (*offset > (4096 - 2 * len))
	{
		/*storable area exceeded: erase the sector and restart from offset 0*/
		flash_erase_sector(addr);
	}
	else
	{
		tmp_offset = *offset + len;
	}

	for (u8 i = 0; i < 2; i++)
	{
		ux_flash_write_page_user(addr + tmp_offset, len, buf);
		flash_read_page(addr + tmp_offset, len, tmp_buf);

		if (memcmp(tmp_buf, buf, len) == 0)
		{
			*offset = tmp_offset;
			return UX_USER_DATA_SAVE_SUCCESS;
		}
		else
		{
			/*write failed: erase and retry from offset 0*/
			flash_erase_sector(addr);
			tmp_offset = 0;
		}
	}

	return UX_USER_DATA_SAVE_FAILED;
}

/**
 * @brief      save the whole dev info record (factory save_dev_info_flash)
 */
static void ux_save_dev_info_flash(void)
{
	if (ux_save_data_to_flash(UX_USER_DATA_CFG_ADDR, sizeof(ux_flash_dev_info_t),
			(u8 *)&ux_flash_dev_info, &ux_dev_info_idx) == UX_USER_DATA_SAVE_SUCCESS)
	{
		printf("[ux_user_data] save success, idx:0x%x\r\n", ux_dev_info_idx);
	}
	else
	{
		printf("[ux_user_data] save failed\r\n");
	}
}

/**
 * @brief      load the newest record, returns the offset of the last stored
 *             record (-len when the sector is empty); near the end of the
 *             sector (idx > 3200) the record is rewritten at offset 0 after a
 *             sector erase (factory flash_info_load_aaa)
 */
static int ux_flash_info_load(u32 s_addr, u8 *d_addr, int len)
{
	int idx;
	u32 buf;

	/*poll the records: U32_MAX marks a free slot*/
	for (idx = 0; idx < (4096 - len); idx += len)
	{
		flash_read_page((u32)(s_addr + idx), 4, (u8 *)(&buf));
		if (buf == U32_MAX)
		{
			break;
		}
	}

	/*idx -= len -> offset of the last used record; negative: no record at all*/
	idx -= len;
	if (idx < 0)
	{
		return idx;
	}

	flash_read_page((u32)(s_addr + idx), len, d_addr);

	if (idx > 3200)
	{
		/*near the sector end: erase and restart from the beginning*/
		flash_erase_sector((u32)s_addr);
		sleep_us(10);
		flash_write_page((u32)s_addr, len, d_addr);
		idx = 0;
	}

	return idx;
}


/* ================ public API ================ */

u32 ux_user_data_get_auth_tick(void)
{
	return ux_flash_dev_info.authorization_tick;
}

void ux_user_data_save_auth_tick(u32 tick)
{
	if (tick != ux_user_data_get_auth_tick())
	{
		ux_flash_dev_info.authorization_tick = tick;
		ux_save_dev_info_flash();
	}
}

/**
 * @brief      factory userDataSetAuthorization: connection up + tick > 1 ->
 *             send the AUTH_CMD (0x77) challenge with the current tick
 */
void ux_user_data_set_authorization(void)
{
#if (BIBOO_UX_SPP)
	if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
	{
		if (ux_user_data_get_auth_tick() <= 1)
		{
			/*tick exhausted: nothing left to challenge with (factory behavior)*/
		}
		else
		{
			ux_spp_auth_cmd((u8)ux_user_data_get_auth_tick());
		}
	}
#endif
}


/* ================ module entry ================ */

static u32 ux_user_data_tick_1 = 0;		/*1s: tick LED + blue blink*/

void ux_user_data_proc(void)
{
	/*factory userDataFunMainLoop timer 1*/
	if (clock_time_exceed(ux_user_data_tick_1, 1000000))
	{
		ux_user_data_tick_1 = clock_time();

#if (BIBOO_UX_LED)
		/*red LED shows the remaining authorization tick*/
		if (ux_user_data_get_auth_tick() < DEFAULT_AUTH_OVER)
		{
			ux_led_set(UX_LED_RED, UX_LED_ON);		/*exhausted: steady on*/
		}
		else if (ux_user_data_get_auth_tick() < DEFAULT_AUTH_WARING)
		{
			ux_led_blink(UX_LED_RED, 200, 200, 1);	/*running low: blink once per second*/
		}
		else
		{
			ux_led_set(UX_LED_RED, UX_LED_OFF);
		}

		/*not connected: blue blinks once per second (connected: ux_led_set(BLUE, ON) in blm_host.c)*/
		if (blc_ll_getCurrentState() != BLS_LINK_STATE_CONN)
		{
			ux_led_blink(UX_LED_BLUE, 200, 200, 1);
		}
#endif
	}

	/*2026-10-20: timer 2 (1s DUT auth-state query, 0x78) removed - useless
	 *with the current mouse firmware. The query always answers AUTH_INVALID,
	 *so the dongle re-challenged with 0x77 every single second: two 20B
	 *frames/s of pure noise on the DUT UART log for a handshake that never
	 *completes. The rx path stays intact (ux_spp.c ux_spp_auth_rx_handle):
	 *should anything ever send 0x77/0x78 again, replies are still consumed,
	 *the tick is still spent on success and the white-LED indication still
	 *works. The factory-test STATUS tick report is unaffected.*/
}

void ux_user_data_init(void)
{
	ux_dev_info_idx = ux_flash_info_load(UX_USER_DATA_CFG_ADDR, (u8 *)&ux_flash_dev_info, UX_USER_DATA_SAVE_MAX);

	if (ux_user_data_get_auth_tick() == 0)
	{
		/*fresh device: store the default authorization tick*/
		ux_user_data_save_auth_tick(DEFAULT_AUTH_TICK);
	}

	printf("[ux_user_data] init, tick:%d idx:0x%x\r\n", ux_user_data_get_auth_tick(), ux_dev_info_idx);
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_AUTH */
