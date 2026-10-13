/********************************************************************************************************
 * @file    ux_factory_test.c
 *
 * @brief   BIBOO UX module (dongle side): factory-test mode, driven by the PC
 *          tool over the host link (ux_host_link.c, Feature 0x09 / Input 0x08).
 *
 *          While active (0x10 FT_START):
 *            - BLE mouse reports are reported to the PC as 0xE1 events instead
 *              of being forwarded to USB (bypass hook in ux_mouse_fwd.c);
 *            - PC commands (0x40 FT_FWD fragments) are assembled into one
 *              20-byte DUT frame and bridged to the DUT mouse over the factory
 *              SPP channel (0x33F3 write, ux_spp.c);
 *            - DUT frames (0x33F4 notify) are forwarded back to the PC as
 *              0xE3 event fragments; DUT link changes as 0xE2 events;
 *            - a DUT reply timeout retransmits the last frame once.
 *          The factory-native auth protocol (0x77/0x78) keeps running inside
 *          ux_spp.c (white LED + authorization tick, vendor/user/user_data)
 *          and is deliberately NOT exposed to the PC.
 *
 *          Protocol definition: ux_ft_proto.h.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "ux_factory_test.h"

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST)

#include "ux_ft_proto.h"
#include "drivers.h"						/*clock_time / clock_time_exceed*/
#include "stack/ble/ble.h"					/*blc_ll_getCurrentState / BLM_CONN_HANDLE / blm_ll_disconnect*/
#include "../spp/ux_spp.h"					/*ux_spp_send / SPP handles / rx hook registration*/
#include "../host_link/ux_host_link.h"		/*ux_host_link_tx*/
#include "../user_data/ux_user_data.h"		/*authorization tick (BIBOO_UX_AUTH)*/


/*DUT reply timeout: retransmit the last DUT frame once, then give up
 *(the PC tool has its own, longer timeout on top)*/
#define UX_FT_RSP_TIMEOUT_US	500000	/*500ms*/

/*factory-test mode flag (0x10 FT_START / 0x11 FT_STOP)*/
static u8 ux_ft_active = 0;

/*deferred DUT disconnect (0x13): blm_ll_disconnect is a BLE stack call, run
 *from ux_factory_test_proc() instead of the EP0 irq*/
static volatile u8 ux_ft_pend_disconnect = 0;

/*PC -> DUT command assembly (host link fragments -> one 20-byte SPP frame).
 *written in ux_ft_fwd_fragment() (EP0 irq), consumed in ux_factory_test_proc()*/
static u8 ux_ft_dut_buf[UX_FT_SPP_SIZE];
static u8 ux_ft_dut_len = 0;
static volatile u8 ux_ft_dut_ready = 0;	/*1 = complete frame waiting for the SPP send*/

/*DUT reply tracking for the frame just sent (0x33F4 notify)*/
static u8  ux_ft_rsp_pending = 0;
static u8  ux_ft_rsp_retried = 0;
static u8  ux_ft_rsp_len = 0;	/*snapshot of ux_ft_dut_len at send time: the
							 *assembly counter is reset right after send, the
							 *retransmit below must not rely on it*/
static u32 ux_ft_rsp_tick = 0;

/*last DUT link state (0xE2 edge detection)*/
static u8 ux_ft_last_link = 0;

/*rolling event sequence (UX_FT_EVT_* [1])*/
static u8 ux_ft_evt_seq = 0;


/*----------------------------------------------------------------------------
 * helpers
 *--------------------------------------------------------------------------*/

#if (BIBOO_UX_HOST_LINK)
/*build an uplink response into the 15-byte r buffer:
 *[0]=cmd|RSP_MASK [1]=seq [2]=status [3..14]=0 (data filled by the caller)*/
static void ux_ft_rsp_build(u8 *r, u8 cmd, u8 seq, u8 status)
{
	memset(r, 0, UX_FT_HLINK_SIZE);
	r[UX_FT_UL_OFFS_CMD]    = cmd | UX_FT_RSP_MASK;
	r[UX_FT_UL_OFFS_SEQ]    = seq;
	r[UX_FT_UL_OFFS_STATUS] = status;
}

/*0xE2 DUT link event (up: UX_FT_LINK_UP / UX_FT_LINK_DOWN)*/
static void ux_ft_evt_link(u8 up)
{
	u8 rpt[UX_FT_HLINK_SIZE];

	memset(rpt, 0, sizeof(rpt));
	rpt[UX_FT_EVT_OFFS_ID]  = UX_FT_EVT_DUT_LINK;
	rpt[UX_FT_EVT_OFFS_SEQ] = ux_ft_evt_seq++;
	rpt[UX_FT_EVT_OFFS_DATA + UX_FT_LINK_OFFS_STATE] =
		up ? UX_FT_LINK_UP : UX_FT_LINK_DOWN;
	ux_host_link_tx(UX_FT_HLINK_SIZE, rpt);
}

/*0xE3 response fragments: one DUT frame -> 12-byte host link fragments.
 *All fragments of one frame share the same event seq (assigned here).*/
static void ux_ft_evt_fwd_rsp(const u8 *p, u8 len)
{
	u8 offs = 0;
	u8 evt_seq;

	if (len == 0)
	{
		return;
	}
	evt_seq = ux_ft_evt_seq++;
	do
	{
		u8 rpt[UX_FT_HLINK_SIZE];
		u8 chunk = len - offs;
		u8 flags;

		if (chunk > UX_FT_FWD_FRAG_MAX)
		{
			chunk = UX_FT_FWD_FRAG_MAX;
		}
		flags = (chunk & UX_FT_FWD_LEN_MASK);
		if (offs + chunk < len)
		{
			flags |= UX_FT_FWD_FLAG_MORE;
		}

		memset(rpt, 0, sizeof(rpt));
		rpt[UX_FT_EVT_OFFS_ID]  = UX_FT_EVT_FWD_RSP;
		rpt[UX_FT_EVT_OFFS_SEQ] = evt_seq;
		rpt[UX_FT_EVT_OFFS_DATA + UX_FT_FWD_OFFS_FLAGS] = flags;
		memcpy(&rpt[UX_FT_EVT_OFFS_DATA + UX_FT_FWD_OFFS_DATA], &p[offs], chunk);
		ux_host_link_tx(UX_FT_HLINK_SIZE, rpt);

		offs += chunk;
	} while (offs < len);
}
#endif /* BIBOO_UX_HOST_LINK */

/*drop the partial / awaiting frame state (mode change, link loss, error)*/
static void ux_ft_reset_session(void)
{
	ux_ft_dut_len = 0;
	ux_ft_dut_ready = 0;
	ux_ft_rsp_pending = 0;
	ux_ft_rsp_retried = 0;
}


#if (BIBOO_UX_HOST_LINK)
/*----------------------------------------------------------------------------
 * 0x40 FT_FWD: one command fragment (EP0 irq context - buffer bookkeeping
 * only, the SPP send is deferred to ux_factory_test_proc())
 *--------------------------------------------------------------------------*/
static int ux_ft_fwd_fragment(const u8 *p, u8 *r)
{
	u8 seq   = p[UX_FT_DL_OFFS_SEQ];
	u8 flags = p[UX_FT_DL_OFFS_ARGS + UX_FT_FWD_OFFS_FLAGS];
	u8 len   = flags & UX_FT_FWD_LEN_MASK;

	if (!ux_ft_active)
	{
		ux_ft_rsp_build(r, UX_FT_CMD_FWD, seq, UX_FT_ST_ERR);
		return 1;
	}
	if (ux_ft_dut_ready)	/*previous complete frame not picked up yet*/
	{
		ux_ft_rsp_build(r, UX_FT_CMD_FWD, seq, UX_FT_ST_BUSY);
		return 1;
	}
	if ((len == 0) || (len > UX_FT_FWD_FRAG_MAX) || ((ux_ft_dut_len + len) > UX_FT_SPP_SIZE))
	{
		ux_ft_reset_session();	/*drop the partial frame; the PC restarts it*/
		ux_ft_rsp_build(r, UX_FT_CMD_FWD, seq, UX_FT_ST_ERR);
		return 1;
	}

	memcpy(&ux_ft_dut_buf[ux_ft_dut_len],
		&p[UX_FT_DL_OFFS_ARGS + UX_FT_FWD_OFFS_DATA], len);
	ux_ft_dut_len += len;

	if (!(flags & UX_FT_FWD_FLAG_MORE))
	{
		ux_ft_dut_ready = 1;	/*complete -> ux_factory_test_proc sends it*/
	}
	ux_ft_rsp_build(r, UX_FT_CMD_FWD, seq, UX_FT_ST_OK);
	return 1;
}
#endif /* BIBOO_UX_HOST_LINK */


#if (BIBOO_UX_SPP)
/*----------------------------------------------------------------------------
 * SPP rx hook (registered at init): frames not consumed by the built-in
 * factory auth logic (0x77/0x78, ux_spp.c)
 *--------------------------------------------------------------------------*/
static bool ux_ft_spp_rx_handle(unsigned char *p_data, unsigned int len)
{
	u8 len8 = (len > UX_FT_SPP_SIZE) ? UX_FT_SPP_SIZE : (u8)len;

	if (!ux_ft_active || (len8 == 0))
	{
		return true;	/*not in factory-test mode: nothing to forward*/
	}

	/*the awaited DUT reply clears the retransmit tracking (command id match)*/
	if (ux_ft_rsp_pending && (p_data[UX_FT_SPP_OFFS_CMD] == ux_ft_dut_buf[UX_FT_SPP_OFFS_CMD]))
	{
		ux_ft_rsp_pending = 0;
	}

#if (BIBOO_UX_HOST_LINK)
	/*forward to the PC as 0xE3 fragments (also spontaneous frames such as
	 *the touch stream)*/
	ux_ft_evt_fwd_rsp((const u8 *)p_data, len8);
#endif
	return true;
}
#endif /* BIBOO_UX_SPP */


/*----------------------------------------------------------------------------
 * host link command entry (called first in ux_host_link_dispatch, EP0 irq)
 *--------------------------------------------------------------------------*/
#if (BIBOO_UX_HOST_LINK)
int ux_ft_host_cmd(const u8 *p, u8 *r)
{
	u8 cmd = p[UX_FT_DL_OFFS_CMD];
	u8 seq = p[UX_FT_DL_OFFS_SEQ];

	if ((cmd >= UX_FT_CMD_START) && (cmd <= UX_FT_CMD_DUT_DISCONNECT))	/*0x10..0x13*/
	{
		switch (cmd)
		{
			case UX_FT_CMD_START:
				ux_ft_reset_session();
				ux_ft_active = 1;
				ux_ft_rsp_build(r, cmd, seq, UX_FT_ST_OK);
				ux_ft_evt_link(ux_ft_last_link);	/*let the PC sync its DUT link indicator*/
				break;

			case UX_FT_CMD_STOP:
				ux_ft_active = 0;
				ux_ft_reset_session();
				ux_ft_rsp_build(r, cmd, seq, UX_FT_ST_OK);
				break;

			case UX_FT_CMD_STATUS:
			{
				u8  ready = 0;
				u32 tick  = 0;

				ux_ft_rsp_build(r, cmd, seq, UX_FT_ST_OK);
				r[UX_FT_UL_OFFS_DATA + UX_FT_STS_OFFS_ACTIVE] =
					ux_ft_active;
				r[UX_FT_UL_OFFS_DATA + UX_FT_STS_OFFS_DUT_LINK] =
					(blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) ? 1 : 0;
			#if (BIBOO_UX_SPP)
				ready = (spp_my_write_handler && spp_my_read_handler) ? 1 : 0;
			#endif
				r[UX_FT_UL_OFFS_DATA + UX_FT_STS_OFFS_SPP_READY] = ready;
			#if (BIBOO_UX_AUTH)
				tick = ux_user_data_get_auth_tick();
			#endif
				r[UX_FT_UL_OFFS_DATA + UX_FT_STS_OFFS_TICK_L] = (u8)(tick & 0xff);
				r[UX_FT_UL_OFFS_DATA + UX_FT_STS_OFFS_TICK_H] = (u8)((tick >> 8) & 0xff);
				break;
			}

			case UX_FT_CMD_DUT_DISCONNECT:
				ux_ft_pend_disconnect = 1;	/*BLE stack call -> ux_factory_test_proc*/
				ux_ft_rsp_build(r, cmd, seq, UX_FT_ST_OK);
				break;

			default:
				break;
		}
		return 1;
	}

	if (cmd == UX_FT_CMD_FWD)
	{
		return ux_ft_fwd_fragment(p, r);
	}

	return 0;	/*not a factory-test command: legacy host link handling*/
}

u8 ux_ft_host_active(void)
{
	return ux_ft_active;
}
#endif /* BIBOO_UX_HOST_LINK */


/*----------------------------------------------------------------------------
 * module entry (dispatched by user_module_manage.c)
 * NOTE: the 0xE1 mouse-report bypass moved into the host_link module
 * (ux_host_link_att_mouse, top-level att_mouse hook like the LED consumer);
 * this module keeps the FT commands, the E2 link event and the E3 SPP bridge.
 *--------------------------------------------------------------------------*/
void ux_factory_test_init(void)
{
	ux_ft_active = 0;
	ux_ft_pend_disconnect = 0;
	ux_ft_reset_session();
	ux_ft_last_link = 0;
	ux_ft_evt_seq = 0;

#if (BIBOO_UX_SPP)
	/*0x77/0x78 stay with the built-in factory auth logic (ux_spp.c);
	 *everything else is bridged to the PC as 0xE3 events while active*/
	ux_spp_rce_data_proc_register(ux_ft_spp_rx_handle);
#endif
	printf("[ux_ft] factory-test module init\r\n");
}

void ux_factory_test_proc(void)
{
	/*deferred DUT disconnect (0x13)*/
	if (ux_ft_pend_disconnect)
	{
		ux_ft_pend_disconnect = 0;
		if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
		{
			/*disconnect: ux_mouse_fwd_disconnect_evt re-enables auto pairing,
			 *so the next DUT unit can be connected without a button*/
			blm_ll_disconnect(BLM_CONN_HANDLE, HCI_ERR_REMOTE_USER_TERM_CONN);
			printf("[ux_ft] DUT disconnected (swap unit)\r\n");
		}
	}

	/*complete PC command -> SPP bridge*/
	if (ux_ft_dut_ready)
	{
	#if (BIBOO_UX_SPP)
		if ((blc_ll_getCurrentState() == BLS_LINK_STATE_CONN)
			&& spp_my_write_handler
			&& (ux_spp_send(ux_ft_dut_buf, ux_ft_dut_len) == SPP_APP_DATA_SEND_SUCCESS))
		{
			ux_ft_rsp_pending = 1;	/*await the DUT reply (see the retry below)*/
			ux_ft_rsp_retried = 0;
			ux_ft_rsp_len = ux_ft_dut_len;	/*snapshot before the assembly counter reset*/
			ux_ft_rsp_tick = clock_time();
			#if (BIBOO_UX_DEBUG)
			printf("[ux_ft] DUT cmd %02X sent, len %d\r\n", ux_ft_dut_buf[0], ux_ft_dut_len);
			#endif
		}
	#endif
		ux_ft_dut_ready = 0;
		ux_ft_dut_len = 0;	/*reset after send: the next command's first fragment
							checks (ux_ft_dut_len + len) <= UX_FT_SPP_SIZE, a stale
							non-zero value makes it trip ST_ERR on the 2nd+ command*/
	}

#if (BIBOO_UX_SPP)
	/*DUT reply timeout: retransmit once, then give up*/
	if (ux_ft_rsp_pending)
	{
		if (blc_ll_getCurrentState() != BLS_LINK_STATE_CONN)
		{
			ux_ft_rsp_pending = 0;
		}
		else if (clock_time_exceed(ux_ft_rsp_tick, UX_FT_RSP_TIMEOUT_US))
		{
			if (!ux_ft_rsp_retried)
			{
				ux_ft_rsp_retried = 1;
				ux_ft_rsp_tick = clock_time();
				ux_spp_send(ux_ft_dut_buf, ux_ft_rsp_len);	/*snapshot len: the
					assembly counter was already reset after the first send*/
				printf("[ux_ft] no DUT reply, retransmit once\r\n");
			}
			else
			{
				ux_ft_rsp_pending = 0;
				printf("[ux_ft] no DUT reply after retransmit, give up\r\n");
			}
		}
	}
#endif

	/*DUT link edge -> 0xE2 event (and session reset on link loss)*/
	{
		u8 link = (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN) ? 1 : 0;

		if (link != ux_ft_last_link)
		{
			ux_ft_last_link = link;
			if (!link)
			{
				ux_ft_reset_session();	/*DUT gone: drop partial/awaiting frames*/
			}
#if (BIBOO_UX_HOST_LINK)
			if (ux_ft_active)
			{
				ux_ft_evt_link(link);
			}
#endif
		}
	}
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST */
