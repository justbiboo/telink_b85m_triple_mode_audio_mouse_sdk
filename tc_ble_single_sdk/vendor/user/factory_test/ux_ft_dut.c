/********************************************************************************************************
 * @file    ux_ft_dut.c
 *
 * @brief   BIBOO UX: DUT-side factory test (mouse firmware, implementation).
 *
 *          - flash 0x70000: 32-byte auth record ("UXTF" magic + state + tick)
 *          - SPP 0x33F3 writes are short-lived: ux_ft_dut_rx() only copies the
 *            frame into a small fifo, the commands run in ux_ft_dut_proc()
 *            (main loop) and answer with a 0x33F4 notify
 *          - command set: ux_ft_proto.h surface 2 (PING/AES/AUTH/TOUCH) plus
 *            the factory-native auth pair 0x77/0x78 (ux_spp.h on the dongle)
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"		/*flash helpers + aes_encrypt / aes_decrypt (driver.h pulls aes.h)*/
#include "stack/ble/ble.h"	/*bls_att_pushNotifyData / blc_ll_getCurrentState*/

#include "ux_ft_dut.h"

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT)

/* ================ flash layout ================ */

#define UX_FT_DUT_CFG_ADDR		0x70000			/*free area, ends before CFG_ADR_MAC (0x76000)*/
#define UX_FT_DUT_REC_MAGIC		0x46545855		/*"UXTF"*/

typedef struct
{
	u32 magic;
	u8  auth_valid;		/*UX_FT_DUT_AUTH_STATE_VALID / _INVALID*/
	u8  auth_tick;		/*Tick of the last successful auth challenge*/
	u8  reserved[26];
} ux_ft_dut_rec_t;		/*32 bytes*/

static ux_ft_dut_rec_t ux_ft_dut_rec;

static const u8 ux_ft_dut_key[16] = UX_FT_DUT_AES_KEY;

/* touch event stream state */
static u8 ux_ft_dut_touch_flag;
static u8 ux_ft_dut_touch_seq;


/* ================ flash helpers ================ */

/**
 * @brief      write in 8-byte steps to avoid long flash busy times killing the
 *             BLE link (same pattern as vendor/user/user_data/ux_user_data.c)
 */
static void ux_ft_dut_flash_write(u32 addr, u32 len, u8 *buf)
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
 * @brief      erase + rewrite the record with read-back verify
 * @return     0 success, -1 failed
 */
static int ux_ft_dut_flash_save(void)
{
	u8 tmp[sizeof(ux_ft_dut_rec_t)];

	flash_erase_sector(UX_FT_DUT_CFG_ADDR);
	sleep_us(10);
	ux_ft_dut_flash_write(UX_FT_DUT_CFG_ADDR, sizeof(ux_ft_dut_rec_t), (u8 *)&ux_ft_dut_rec);
	flash_read_page(UX_FT_DUT_CFG_ADDR, sizeof(ux_ft_dut_rec_t), tmp);

	return (memcmp(tmp, &ux_ft_dut_rec, sizeof(ux_ft_dut_rec_t)) == 0) ? 0 : -1;
}


/* ================ SPP tx ================ */

static void ux_ft_dut_notify(u8 *rsp)
{
	if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
	{
		bls_att_pushNotifyData(SPP_FT_NOTIFY_DP_H, rsp, UX_FT_SPP_SIZE);
	}
}


/* ================ rx fifo (filled in the ATT context) ================ */

#define UX_FT_DUT_RX_FIFO_NUM	4

static u8 ux_ft_dut_rx_fifo[UX_FT_DUT_RX_FIFO_NUM][UX_FT_SPP_SIZE];
static volatile u8 ux_ft_dut_rx_wr;
static volatile u8 ux_ft_dut_rx_rd;

void ux_ft_dut_rx(u8 *p, u8 len)
{
	u8 next;

	if (len == 0)
	{
		return;
	}
	if (len > UX_FT_SPP_SIZE)
	{
		len = UX_FT_SPP_SIZE;
	}

	next = (u8)((ux_ft_dut_rx_wr + 1) % UX_FT_DUT_RX_FIFO_NUM);
	if (next == ux_ft_dut_rx_rd)
	{
		/*fifo full: drop the frame (the dongle side times out on its own)*/
		return;
	}

	memset(ux_ft_dut_rx_fifo[ux_ft_dut_rx_wr], 0, UX_FT_SPP_SIZE);
	memcpy(ux_ft_dut_rx_fifo[ux_ft_dut_rx_wr], p, len);
	ux_ft_dut_rx_wr = next;
}


/* ================ command handling (main loop) ================ */

/**
 * @brief      factory auth challenge (0x77): store the tick + the authorized
 *             flag into flash on the first successful challenge
 */
static void ux_ft_dut_auth_cmd(u8 tick, u8 *rsp)
{
	if (ux_ft_dut_rec.auth_valid == UX_FT_DUT_AUTH_STATE_VALID)
	{
		/*already authorized: keep the stored record*/
		rsp[UX_FT_SPP_OFFS_ARGS] = UX_FT_DUT_AUTH_RESULT_ALREADY;
		return;
	}

	ux_ft_dut_rec.auth_valid = UX_FT_DUT_AUTH_STATE_VALID;
	ux_ft_dut_rec.auth_tick = tick;

	if (ux_ft_dut_flash_save() == 0)
	{
		printf("[ux_ft_dut] auth saved, tick:%d\r\n", tick);
		rsp[UX_FT_SPP_OFFS_ARGS] = UX_FT_DUT_AUTH_RESULT_SUCCESS;
	}
	else
	{
		/*rollback: the flash write failed, stay unauthorized*/
		ux_ft_dut_rec.auth_valid = UX_FT_DUT_AUTH_STATE_INVALID;
		ux_ft_dut_rec.auth_tick = 0;
		rsp[UX_FT_SPP_OFFS_ARGS] = UX_FT_DUT_AUTH_RESULT_FAIL;
	}
}

static void ux_ft_dut_handle(u8 *p)
{
	u8 cmd = p[UX_FT_SPP_OFFS_CMD];
	u8 rsp[UX_FT_SPP_SIZE];

	memset(rsp, 0, sizeof(rsp));
	rsp[UX_FT_SPP_OFFS_CMD] = cmd;

	switch (cmd)
	{
	case UX_FT_DUT_AUTH_CMD:	/*0x77 factory auth challenge*/
		ux_ft_dut_auth_cmd(p[UX_FT_SPP_OFFS_ARGS], rsp);
		ux_ft_dut_notify(rsp);
		break;

	case UX_FT_DUT_GET_AUTH_STATE_CMD:	/*0x78 factory auth state query*/
		rsp[UX_FT_SPP_OFFS_ARGS] = ux_ft_dut_rec.auth_valid;
		rsp[UX_FT_SPP_OFFS_ARGS + 1] = ux_ft_dut_rec.auth_tick;	/*extra: current tick*/
		ux_ft_dut_notify(rsp);
		break;

	case UX_FT_DUT_CMD_PING:	/*0x01 -> [1]=version [2]=model [3]=MOUSE_DATA_LEN_AAA*/
		rsp[UX_FT_SPP_OFFS_ARGS + UX_FT_PING_OFFS_VERSION] = UX_FT_DUT_FW_VERSION;
		rsp[UX_FT_SPP_OFFS_ARGS + UX_FT_PING_OFFS_MODEL] = BIBOO_UX_FT_DUT_MODEL;
		rsp[UX_FT_SPP_OFFS_ARGS + UX_FT_PING_OFFS_MOUSE_LEN] = MOUSE_DATA_LEN_AAA;
		ux_ft_dut_notify(rsp);
		break;

	case UX_FT_DUT_CMD_AES_ENC:	/*0x10: [1..16] plaintext -> [1..16] ciphertext*/
		aes_encrypt((u8 *)ux_ft_dut_key, &p[UX_FT_SPP_OFFS_ARGS], &rsp[UX_FT_SPP_OFFS_ARGS]);
		ux_ft_dut_notify(rsp);
		break;

	case UX_FT_DUT_CMD_AES_DEC:	/*0x11: [1..16] ciphertext -> [1..16] plaintext*/
		aes_decrypt((u8 *)ux_ft_dut_key, &p[UX_FT_SPP_OFFS_ARGS], &rsp[UX_FT_SPP_OFFS_ARGS]);
		ux_ft_dut_notify(rsp);
		break;

	case UX_FT_DUT_CMD_AUTH:	/*0x12: [1..8] challenge + 8 zero bytes -> [1..16] response*/
		{
			u8 in[16] = {0};

			memcpy(in, &p[UX_FT_SPP_OFFS_ARGS], 8);
			aes_encrypt((u8 *)ux_ft_dut_key, in, &rsp[UX_FT_SPP_OFFS_ARGS]);
			ux_ft_dut_notify(rsp);
		}
		break;

	case UX_FT_DUT_CMD_TOUCH_START:	/*0x20*/
		ux_ft_dut_touch_flag = 1;
		ux_ft_dut_touch_seq = 0;
		rsp[UX_FT_SPP_OFFS_ARGS] = UX_FT_DUT_ST_OK;
		ux_ft_dut_notify(rsp);
		break;

	case UX_FT_DUT_CMD_TOUCH_STOP:	/*0x21*/
		ux_ft_dut_touch_flag = 0;
		rsp[UX_FT_SPP_OFFS_ARGS] = UX_FT_DUT_ST_OK;
		ux_ft_dut_notify(rsp);
		break;

	default:
		/*unknown command: stay silent, the dongle side times out*/
		break;
	}
}


/* ================ public API ================ */

void ux_ft_dut_init(void)
{
	flash_read_page(UX_FT_DUT_CFG_ADDR, sizeof(ux_ft_dut_rec_t), (u8 *)&ux_ft_dut_rec);

	if (ux_ft_dut_rec.magic != UX_FT_DUT_REC_MAGIC)
	{
		/*fresh / never-authorized device: keep it in RAM only, the record is
		 *written on the first successful 0x77 challenge*/
		memset(&ux_ft_dut_rec, 0, sizeof(ux_ft_dut_rec_t));
		ux_ft_dut_rec.magic = UX_FT_DUT_REC_MAGIC;
		ux_ft_dut_rec.auth_valid = UX_FT_DUT_AUTH_STATE_INVALID;
	}

	printf("[ux_ft_dut] init, auth:%d tick:%d\r\n", ux_ft_dut_rec.auth_valid, ux_ft_dut_rec.auth_tick);
}

void ux_ft_dut_proc(void)
{
	while (ux_ft_dut_rx_rd != ux_ft_dut_rx_wr)
	{
		u8 *p = ux_ft_dut_rx_fifo[ux_ft_dut_rx_rd];

		ux_ft_dut_rx_rd = (u8)((ux_ft_dut_rx_rd + 1) % UX_FT_DUT_RX_FIFO_NUM);

		ux_ft_dut_handle(p);
	}
}

void ux_ft_dut_touch_report(u8 pressed, u16 pos)
{
	u8 rsp[UX_FT_SPP_SIZE];

	if (!ux_ft_dut_touch_flag)
	{
		return;
	}

	memset(rsp, 0, sizeof(rsp));
	rsp[UX_FT_SPP_OFFS_CMD] = UX_FT_DUT_EVT_TOUCH;
	rsp[UX_FT_TOUCH_OFFS_PRESSED] = pressed ? 1 : 0;
	rsp[UX_FT_TOUCH_OFFS_POS_L] = (u8)(pos & 0xFF);
	rsp[UX_FT_TOUCH_OFFS_POS_H] = (u8)((pos >> 8) & 0xFF);
	rsp[UX_FT_TOUCH_OFFS_SEQ] = ux_ft_dut_touch_seq++;

	ux_ft_dut_notify(rsp);
}

u8 ux_ft_dut_touch_on(void)
{
	return ux_ft_dut_touch_flag;
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT */
