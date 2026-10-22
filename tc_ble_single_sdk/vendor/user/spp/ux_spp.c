/********************************************************************************************************
 * @file    ux_spp.c
 *
 * @brief   BIBOO UX: SPP-like ATT data channel with the DUT mouse, ported from
 *          the mass-production dongle SDK (tc_ble_single_sdk_factory,
 *          vendor/user/spp/spp_device.c). See ux_spp.h for the protocol.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "stack/ble/ble.h"	/*blc_gatt_pushWriteCommand / blc_ll_getCurrentState / BLM_CONN_HANDLE*/
#include "vendor/common/tlkapi_debug.h"				/* tlkapi_printf */
#include "vendor/common/simple_sdp.h"				/* central_sdp_pending */
#include "../../ble_master_kma_dongle/blm_host.h"	/* central_smp_pending */

#include "ux_spp.h"

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_SPP)
#include "../user_data/ux_user_data.h"	/*BIBOO UX: authorization tick data layer (flash 0x70000)*/
#include "../led/ux_led.h"				/*BIBOO UX: white LED = auth invalid*/

static ux_spp_rf_data_t ux_spp_rf;	/*tx/rx frame buffers (factory rf_data)*/

static u8 ux_spp_auth_state = AUTH_SEND_RSP_RESULT_FAIL;

/* production-test rx handler (registered by ux_factory_test) */
static ux_spp_rce_data_proc_t ux_spp_user_rce_handler = NULL;


/* ================ send ================ */

/**
 * @brief      send one 20-byte frame to the DUT mouse on the SPP write characteristic (0x33F3)
 * @param[in]  p_data - frame buffer
 * @param[in]  len    - frame length, <= 20
 * @return     SppAppDataSendResult_e
 */
SppAppDataSendResult_e ux_spp_send(unsigned char *p_data, unsigned int len)
{
	SppAppDataSendResult_e send_result = SPP_APP_DATA_SEND_ERR;

	if (p_data != NULL)
	{
		if (len <= SPP_DEVICE_DATA_MAX_LEN)
		{
			if ((blc_gatt_pushWriteCommand(BLM_CONN_HANDLE, spp_my_write_handler, p_data, SPP_DEVICE_DATA_MAX_LEN) == BLE_SUCCESS)
				&& (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN))
			{
				send_result = SPP_APP_DATA_SEND_SUCCESS;
			}
		}
		else
		{
			send_result = SPP_APP_DATA_SEND_TOO_LONG;
			printf("[ux_spp] send fail, data too long, max:%d len:%d\r\n", SPP_DEVICE_DATA_MAX_LEN, len);
		}
	}
	else
	{
		printf("[ux_spp] send fail, data is null\r\n");
	}
	return send_result;
}


/* ================ factory auth protocol ================ */

/**
 * @brief      factory AUTH_CMD (0x77): challenge the DUT with the current authorization tick
 * @param[in]  Tick - current authorization tick (flash, see vendor/user/user_data)
 * @return     none
 */
void ux_spp_auth_cmd(u8 Tick)
{
	memset(ux_spp_rf.tx_data, 0, SPP_DEVICE_DATA_MAX_LEN);
	ux_spp_rf.tx_data[SPP_PKT_OFFS_CMD] = SPP_APP_MSG_AUTH_CMD;
	ux_spp_rf.tx_data[SPP_PKT_OFFS_ARG] = Tick;
	ux_spp_send(ux_spp_rf.tx_data, SPP_DEVICE_DATA_MAX_LEN);

	ux_spp_auth_state = AUTH_SEND_RSP_RESULT_INIT;
}

/**
 * @brief      factory GET_AUTH_STATE_CMD (0x78): query the DUT auth state
 * @param[in]  none
 * @return     none
 */
void ux_spp_get_auth_state_cmd(void)
{
	/*clear first: tx_data is a shared static buffer - without this the
	 *query frame carries the previous 0x77 challenge tick in [1] (the
	 *DUT ignores it, but it makes DUT UART logs confusing).*/
	memset(ux_spp_rf.tx_data, 0, SPP_DEVICE_DATA_MAX_LEN);
	ux_spp_rf.tx_data[SPP_PKT_OFFS_CMD] = SPP_APP_MSG_GET_AUTH_STATE_CMD;
	ux_spp_send(ux_spp_rf.tx_data, SPP_DEVICE_DATA_MAX_LEN);
}

u8 ux_spp_auth_state_get(void)
{
	return ux_spp_auth_state;
}


/* ================ rx ================ */

/**
 * @brief      built-in factory auth response handler (AUTH_CMD / GET_AUTH_STATE_CMD)
 * @param[in]  data - received frame (>= 3 bytes)
 * @param[in]  len  - frame length
 * @return     true when the frame has been consumed here
 */
static bool ux_spp_auth_rx_handle(unsigned char *data, unsigned int len)
{
	(void)len;

	switch (data[SPP_PKT_OFFS_CMD])
	{
		case SPP_APP_MSG_AUTH_CMD:
#if (BIBOO_UX_AUTH)
			if (data[SPP_PKT_OFFS_RESULT] == AUTH_SEND_RSP_RESULT_SUCCESS)
			{
				/*factory logic: auth success -> consume one authorization tick and save it*/
				u32 Tick = ux_user_data_get_auth_tick();
				ux_user_data_save_auth_tick(--Tick);
				printf("[ux_spp] auth success, tick left:%d\r\n", Tick);
			}
#endif
			ux_spp_auth_state = data[SPP_PKT_OFFS_RESULT];
			return true;

		case SPP_APP_MSG_GET_AUTH_STATE_CMD:
			if (data[SPP_PKT_OFFS_RESULT] == AUTH_VALID)
			{
#if (BIBOO_UX_LED)
				ux_led_set(UX_LED_WHITE, UX_LED_OFF);
#endif
			}
			else if (data[SPP_PKT_OFFS_RESULT] == AUTH_INVALID)
			{
#if (BIBOO_UX_AUTH)
				ux_user_data_set_authorization();	/*send the 0x77 challenge with the current tick*/
#endif
#if (BIBOO_UX_LED)
				ux_led_set(UX_LED_WHITE, UX_LED_ON);
#endif
			}
			return true;

		default:
			return false;
	}
}

/**
 * @brief      rx entry, called by att_spp() (blm_att.c) on the 0x33F4 notify from the mouse
 * @param[in]  p   - frame data
 * @param[in]  len - frame length
 * @return     0
 */
int ux_spp_on_receive_data(unsigned char *p, u8 len)
{
	if (len == 0)
	{
		return 0;
	}

	if (len >= 3 && ux_spp_auth_rx_handle(p, len))
	{
		return 0;	/*consumed by the built-in factory auth logic (0x77/0x78)*/
	}

	if (ux_spp_user_rce_handler != NULL)
	{
		ux_spp_user_rce_handler(p, len);	/*production-test frames (vendor/user/factory_test)*/
	}
	return 0;
}

/**
 * @brief      register the production-test rx handler
 * @param[in]  handler - handler for frames not consumed by the auth logic
 * @return     none
 */
void ux_spp_rce_data_proc_register(ux_spp_rce_data_proc_t handler)
{
	ux_spp_user_rce_handler = handler;
}


/* ================ module entry ================ */

void ux_spp_init(void)
{
	ux_spp_user_rce_handler = NULL;	/*factory auth logic (0x77/0x78) is built in*/
}

/*main loop (dispatched from user_module_proc): enable the DUT factory SPP
 *notify (0x33F4) by writing its CCC (0x2902) after SMP & SDP done.
 *DUT side bls_att_pushNotifyData checks the CCC value: without this write
 *every DUT reply (0x77/0x78 auth rsp, PING/AES/TOUCH ack, 0x22 touch
 *stream) is silently dropped and the bridged PC command times out.
 *One shot per connection (reset on link loss).*/
void ux_spp_proc(void)
{
	static u8 ccc_done = 0;
	#if (BIBOO_UX_DEBUG)
	static u8 wait_printed = 0;	/*one-shot: why the CCC write is not issued yet*/
	#endif

	if (blc_ll_getCurrentState() != BLS_LINK_STATE_CONN)
	{
		ccc_done = 0;	/*reset for next connection*/
		#if (BIBOO_UX_DEBUG)
		wait_printed = 0;
		#endif
		return;
	}
	if (central_sdp_pending)	/*SDP still ongoing*/
	{
		return;
	}
	#if (BLE_HOST_SMP_ENABLE)
	if (central_smp_pending)	/*SMP pairing still ongoing*/
	{
		return;
	}
	#endif

	if (ccc_done)
	{
		return;
	}
	if (!spp_my_read_handler)
	{
		#if (BIBOO_UX_DEBUG)
		if (!wait_printed)
		{
			wait_printed = 1;	/*diagnose once per connection: SDP found no 0x33F4*/
			tlkapi_printf(APP_LOG_EN, "[DBG] SPP CCC wait: rdH=%x wrH=%x (sdp=%x smp=%x)\n",
				spp_my_read_handler, spp_my_write_handler,
				central_sdp_pending, central_smp_pending);
		}
		#endif
		return;	/*notify handle not resolved yet (SDP failed or not started)*/
	}

	/*CCC handle = notify value handle + 1 (ATT row order: prop, value, CCC)*/
	u8 ccc[2] = {0x01, 0x00};
	if (!blc_gatt_pushWriteCommand(BLM_CONN_HANDLE, spp_my_read_handler + 1, ccc, 2))
	{
		ccc_done = 1;
		#if (BIBOO_UX_DEBUG)
		tlkapi_printf(APP_LOG_EN, "[DBG] SPP notify CCC enabled (H=%x)\n", spp_my_read_handler);
		#endif
	}
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_SPP */
