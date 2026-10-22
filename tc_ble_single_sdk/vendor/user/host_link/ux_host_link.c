/********************************************************************************************************
 * @file    ux_host_link.c
 *
 * @brief   BIBOO UX module (dongle side): USB HID host communication link
 *          (dongle <-> PC host tool). See ux_host_link.h for the transport
 *          and payload protocol. Logic migrated from the bibo_feat_rx_buf
 *          spots in application/usbstd/usb.c (originally wrapped by
 *          "BIBOO_UX_HOST_LINK" at each site).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "ux_host_link.h"


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK)

#include "application/usbstd/usb.h"
#include "drivers.h"
#include "application/app/usbmouse.h"				/*BIBOO UX diag: usbmouse_dbg_* counters (MSTAT 0x03)*/
#include "vendor/user/mouse_fwd/ux_mouse_fwd.h"	/*BIBOO UX diag: uxm_att_rx counter (MSTAT 0x03)*/
#include "stack/ble/controller/ble_controller.h"	/* blc_ll_getCurrentState, BLS_LINK_STATE_CONN (mouse link up test); NOTE: ll.h alone is not self-contained - blt_event_callback_t comes from controller.h, use the ble_controller.h umbrella */
#include "vendor/user/factory_test/ux_factory_test.h"	/*BIBOO UX: factory-test commands 0x10..0x13/0x40 (BIBOO_UX_FACTORY_TEST)*/
#include "vendor/user/factory_test/ux_ft_proto.h"	/*BIBOO UX: FT host link protocol constants (0xE1 evt, frame offsets)*/
#include "vendor/user/ota_slot/ux_ota_slot.h"		/*BIBOO UX: OTA stream pipe commands 0x50..0x52/0x54 (BIBOO_UX_OTA_SLOT)*/
#include "vendor/user/self_ota/ux_self_ota.h"		/*BIBOO UX: dongle self upgrade commands 0x70..0x75 (BIBOO_UX_SELF_OTA)*/

#if (BIBOO_UX_OTA_SLOT)
/*BIBOO UX 2026-10-21: mouse DevInf cache, filled by the BLE master layer
 *(blm_host.c) right after connect + SDP; relayed to the PC by cmd 0x55.*/
extern u8 *ux_devinf_get(void);
extern u8  ux_devinf_ready(void);
#endif /* BIBOO_UX_OTA_SLOT */


/*----------------------------------------------------------------------------
 * BIBOO UX: host link command set (payload[0]).
 * Known commands are dispatched synchronously inside ux_host_link_rx()
 * (EP0 irq context - keep them lightweight); the response is sent as an
 * Input Report through ux_host_link_tx().
 *   0x00       : ECHO - default / fallback for unknown cmds (payload mirrored)
 *   0x01       : IDENT - module presence & version probe
 *   0x02       : TEST beacon - 1s periodic uplink while the mouse link is up
 *   0x03       : MSTAT - mouse-forward pipeline counters (BIBOO UX diag, see
 *                usbmouse_dbg_* / uxm_att_rx; request p[2]=group/0xFF reset)
 *   0x04..0x7f : reserved for business commands (dongle side features)
 *   0x10..0x13 : factory-test control cmds (FT_START/STOP/STATUS/DUT_DISCONNECT,
 *                see vendor/user/factory_test + ux_ft_proto.h)
 *   0x40       : factory-test FT_FWD fragment (bridged to the DUT mouse)
 *   0x50..0x52/0x54 : OTA stream pipe (SLOT_BEGIN/DATA/END/STATUS, see
 *                vendor/user/ota_slot - the image is streamed to the mouse
 *                through the master-OTA push engine, never stored on the
 *                dongle; 0x53 TRIGGER was removed with the flash-slot design)
 *   0x55       : GET_DEV_INFO - relay the mouse DevInf_aaa (read by the
 *                dongle as BLE master after connect, cached in blm_host.c).
 *                args[0]=byte offset; reply data = 12 bytes starting there,
 *                so the 15-byte value comes back in two frames (0 and 12).
 *                status: 0=OK, 1=not ready (mouse not connected / not read
 *                yet), 2=bad offset.
 *   0x70..0x75 : dongle SELF upgrade (SELF_BEGIN/DATA/END/STATUS/INFO/COMMIT,
 *                see vendor/user/self_ota - the dongle's own image is
 *                written into the spare A/B bank of its flash; the running
 *                firmware is only invalidated by an explicit COMMIT)
 *   0x80..     : reserved for debug commands
 * Heavy commands must NOT run in the EP0 irq: their dispatch() case should
 * only set a pending flag, the real work happens in ux_host_link_proc()
 * (main loop) and sends the response via ux_host_link_tx().
 *--------------------------------------------------------------------------*/
#define UX_HLINK_CMD_ECHO    0x00
#define UX_HLINK_CMD_IDENT   0x01
#define UX_HLINK_CMD_BEACON  0x02
#define UX_HLINK_CMD_MSTAT   0x03	/*mouse-forward pipeline counters (BIBOO UX diag)*/
#define UX_HLINK_CMD_DEVINFO 0x55	/*mouse DevInf_aaa relay (version, see blm_host.c ux_devinf_*)*/
#define UX_HLINK_DEVINFO_LEN 15		/*DevInf_aaa size*/
#define UX_HLINK_DEVINFO_CHUNK 12	/*bytes per reply: r[3..14]*/

/*module identification (cmd IDENT response): "BIBOO" magic + module version*/
#define UX_HLINK_MAGIC0      0xB1
#define UX_HLINK_MAGIC1      0xB0
#define UX_HLINK_VERSION     0x01

#define UX_HLINK_FEATURE_PAYLOAD  UX_HLINK_FEATURE_REPORT_PAYLOAD_SIZE
#define UX_HLINK_INPUT_PAYLOAD    UX_HLINK_INPUT_REPORT_PAYLOAD_SIZE
#define UX_HLINK_TX_FIFO_NUM      8

static const u8 ux_hlink_report_desc[UX_HLINK_REPORT_DESC_SIZE] = {
	0x06,0x00,0xff,
	0x09,0x01,
	0xa1,0x01,
	0x85,UX_HLINK_FEATURE_REPORT_ID,
	0x09,0x01,
	0x15,0x00,
	0x26,0xff,0x00,
	0x75,0x08,
	0x95,UX_HLINK_FEATURE_PAYLOAD,
	0xb1,0x02,
	0x85,UX_HLINK_INPUT_REPORT_ID,
	0x09,0x02,
	0x15,0x00,
	0x26,0xff,0x00,
	0x75,0x08,
	0x95,UX_HLINK_INPUT_PAYLOAD,
	0x81,0x02,
	0xc0,
};

static u8 ux_hlink_rx_buf[UX_HLINK_FEATURE_PAYLOAD];
static u8 ux_hlink_tx_fifo[UX_HLINK_TX_FIFO_NUM][UX_HLINK_INPUT_PAYLOAD];
static volatile u8 ux_hlink_tx_wptr = 0;
static volatile u8 ux_hlink_tx_rptr = 0;
static volatile u8 ux_hlink_rx_seq = 0;
/*rolling sequence of the 0xE1 factory-test mouse events (ux_host_link_att_mouse);
 *independent from the E2/E3 event sequence in the factory_test module*/
static u8 ux_hlink_evt_seq = 0;

#if (BIBOO_UX_DEBUG)
volatile u32 dbg_hrx_cnt = 0;
volatile u32 dbg_hcmd_cnt = 0;
volatile u32 dbg_htx_cnt = 0;
volatile u32 dbg_htx_drop_cnt = 0;
#endif


u8 *ux_host_link_get_report_desc(void)
{
	return (u8 *)ux_hlink_report_desc;
}

u16 ux_host_link_get_report_desc_size(void)
{
	return sizeof(ux_hlink_report_desc);
}


#if (UART_PRINT_DEBUG_ENABLE && BIBOO_UX_DEBUG)
/*BIBOO debug: dump one host link frame, e.g.
 * "[HLINK] RX: 01 00 00 00 00 00 00".
 * NOTE: it may be called from the EP0 SET_REPORT irq context. The 0x09
 * channel is a low frequency control channel, so the ~2ms busy-wait GPIO
 * UART print is acceptable there. Keep frames short while this print is
 * enabled.
 * 2026-10-20: muted while an OTA stream upload is running (see
 * UX_HLINK_DUMP_MUTED) - at 66 frames/s the dumps alone cost ~10ms of EP0
 * irq time per frame and starved the BLE stack (mouse link dropped on the
 * supervision timeout, twice, exactly BEGIN+4.00s into the upload).*/
static void ux_hlink_dump(const char *tag, const u8 *p, u8 len)
{
	u8 i;
	printf("%s", tag);
	for (i = 0; i < len; i++)
	{
		printf("%02X ", p[i]);
	}
	printf("\r\n");
}
#endif

/*true while the per-frame UART dumps must stay silent: an OTA stream upload
 *owns the EP0 irq at high frequency, and each dump is a multi-ms busy-wait
 *print inside that irq.*/
#if (BIBOO_UX_OTA_SLOT || BIBOO_UX_SELF_OTA)
#if (BIBOO_UX_OTA_SLOT)
extern u8 ux_ota_slot_busy(void);
#endif
#if (BIBOO_UX_SELF_OTA)
extern u8 ux_self_ota_busy(void);
#endif
#define UX_HLINK_DUMP_MUTED()		\
	((BIBOO_UX_OTA_SLOT   && ux_ota_slot_busy()) ||	\
	 (BIBOO_UX_SELF_OTA   && ux_self_ota_busy()))
#else
#define UX_HLINK_DUMP_MUTED()		0
#endif


/*synchronous command dispatch, called from ux_host_link_rx (EP0 irq).
 * p = 15-byte command payload ([0]=cmd, [1..14]=args);
 * r = 15-byte Input response buffer, already mirroring p (echo) on entry, so
 * unknown commands simply return and keep the echo response.
 * Returns 1 when the command has already queued its own uplink response
 * (the caller must NOT send the echo fallback), 0 when r is filled and the
 * echo fallback should send it.*/
static int ux_host_link_dispatch(const u8 *p, u8 *r)
{
#if (BIBOO_UX_SELF_OTA)
	/*BIBOO UX: dongle self upgrade (0x70..0x75, see vendor/user/self_ota);
	 *returns 1 with r filled when p[0] is one. Every command manages its own
	 *response (immediate ones here in the irq, END/COMMIT deferred from
	 *ux_self_ota_proc), so the echo fallback below must not fire for them.*/
	if (ux_self_ota_host_cmd(p, r))
	{
		return 1;
	}
#endif
#if (BIBOO_UX_OTA_SLOT)
	/*BIBOO UX: OTA stream pipe commands (0x50..0x52/0x54, see
	 *vendor/user/ota_slot); returns 1 with r filled when p[0] is one.
	 *Every OTA path manages its own response: immediate ones through
	 *ux_ota_send_rsp() right here in the irq, BEGIN through the
	 *deferred response pushed from ux_ota_slot_proc(). The echo fallback
	 *below must not fire for them - otherwise each SLOT_DATA frame would
	 *burn two uplink slots (real ack + duplicate), halving the effective
	 *uplink throughput (2026-10-20: frame-synchronous upload hit exactly
	 *that wall after ~250 frames).*/
	if (ux_ota_slot_host_cmd(p, r))
	{
		return 1;
	}
#endif
#if (BIBOO_UX_FACTORY_TEST)
	/*BIBOO UX: factory-test commands (0x10..0x13 / 0x40, see ux_factory_test.c);
	 *returns 1 with r filled when p[0] is a factory-test command. These only
	 *fill r and rely on the echo fallback below to send it.*/
	if (ux_ft_host_cmd(p, r))
	{
		return 0;
	}
#endif
	switch (p[0])
	{
		case UX_HLINK_CMD_IDENT:
			r[0] = UX_HLINK_CMD_IDENT;
			r[1] = UX_HLINK_MAGIC0;
			r[2] = UX_HLINK_MAGIC1;
			r[3] = UX_HLINK_VERSION;
			r[4] = 0;
			r[5] = 0;
			r[6] = 0;
			#if (BIBOO_UX_DEBUG)
			dbg_hcmd_cnt ++;
			#endif
			break;

		case UX_HLINK_CMD_MSTAT:
		{
			/*mouse-forward pipeline counters (locate the broken-trajectory
			 *layer while drawing). Request p[2]: group 0 = att_rx / ring_in /
			 *ring_drop, group 1 = tx_ok / epq / fifo_drop, 0xFF = reset all.
			 *Response: [0]=cmd|0x80 [1]=seq [2]=status [3..14]=3 x u32 LE.
			 *Synchronous here: a few byte copies only, EP0-irq safe.*/
			u8  grp = p[2];
			u32 c0, c1, c2;
			u8  irq_en;

			r[0] = UX_HLINK_CMD_MSTAT | 0x80;
			r[1] = p[1];	/*seq echo so the PC SendCommand matcher accepts it*/

			if (grp == 0xFF)
			{
				irq_en = irq_disable();	/*vs. USB-ISR / BLE-host increments*/
				uxm_att_rx = 0;
				uxm_gap_reset();
				usbmouse_dbg_ring_in = 0;
				usbmouse_dbg_ring_drop = 0;
				usbmouse_dbg_tx_ok = 0;
				usbmouse_dbg_epq = 0;
				usbmouse_dbg_fifo_drop = 0;
				irq_restore(irq_en);
				r[2] = 0;	/*status OK; data bytes stay zero*/
				break;
			}

			if (grp == 0)
			{
				c0 = uxm_att_rx;
				c1 = usbmouse_dbg_ring_in;
				c2 = usbmouse_dbg_ring_drop;
			}
			else if (grp == 1)
			{
				c0 = usbmouse_dbg_tx_ok;
				c1 = usbmouse_dbg_epq;
				c2 = usbmouse_dbg_fifo_drop;
			}
			else if (grp == 2)
			{
				/*dongle-time inter-arrival gaps at the att_mouse hook (immune to
				 *the PC host-link 50ms read batching that quantizes the 0xE1
				 *stream; see ux_mouse_fwd.c uxm_gap_*)*/
				c0 = uxm_gap_cnt;
				c1 = uxm_gap_max_ms;
				c2 = uxm_gap_over20_cnt;
			}
			else if (grp == 3)
			{
				/*gap sum (avg = sum/cnt) + stream breaks; third slot reserved*/
				c0 = uxm_gap_sum_ms;
				c1 = uxm_gap_break_cnt;
				c2 = 0;
			}
			else
			{
				r[2] = 1;	/*status: unknown group*/
				break;
			}

			r[3]  = (u8)(c0);        r[4]  = (u8)(c0 >> 8);
			r[5]  = (u8)(c0 >> 16);  r[6]  = (u8)(c0 >> 24);
			r[7]  = (u8)(c1);        r[8]  = (u8)(c1 >> 8);
			r[9]  = (u8)(c1 >> 16);  r[10] = (u8)(c1 >> 24);
			r[11] = (u8)(c2);        r[12] = (u8)(c2 >> 8);
			r[13] = (u8)(c2 >> 16);  r[14] = (u8)(c2 >> 24);
			r[2] = 0;
			break;
		}

#if (BIBOO_UX_OTA_SLOT)
		case UX_HLINK_CMD_DEVINFO:
		{
			/*Relay the cached mouse DevInf_aaa (15B, read by the dongle as
			 *BLE master - see blm_host.c ux_devinf_*). args[0] = byte offset;
			 *12 bytes per frame, so the PC reads offset 0 then 12.
			 *Synchronous & EP0-irq safe: a 12-byte copy from RAM only.*/
			u8 off = p[2];
			u8 n, k;
			u8 *src;

			r[0] = UX_HLINK_CMD_DEVINFO | 0x80;
			r[1] = p[1];	/*seq echo so the PC SendCommand matcher accepts it*/

			if (!ux_devinf_ready())
			{
				r[2] = 1;	/*mouse not connected, or DevInf not read yet*/
				break;
			}
			if (off >= UX_HLINK_DEVINFO_LEN)
			{
				r[2] = 2;	/*bad offset*/
				break;
			}

			src = ux_devinf_get();
			n = (u8)(UX_HLINK_DEVINFO_LEN - off);
			if (n > UX_HLINK_DEVINFO_CHUNK)
				n = UX_HLINK_DEVINFO_CHUNK;
			for (k = 0; k < n; k++)
				r[3 + k] = src[off + k];
			r[2] = 0;
			break;
		}
#endif /* BIBOO_UX_OTA_SLOT */

	default:
		/*unknown / ECHO command: keep the echo response*/
		break;
	}

	return 0;
}


/*user_init (dispatched from user_module_init): reset the host link state*/
void ux_host_link_init(void)
{
	memset(ux_hlink_rx_buf, 0, sizeof(ux_hlink_rx_buf));
	memset(ux_hlink_tx_fifo, 0, sizeof(ux_hlink_tx_fifo));
	ux_hlink_tx_wptr = 0;
	ux_hlink_tx_rptr = 0;
	ux_hlink_rx_seq = 0;
}


void ux_host_link_rx(u8 len, u8 *p)
{
	u8 i;
	u8 response[UX_HLINK_INPUT_PAYLOAD] = {0};
	if (len > UX_HLINK_FEATURE_PAYLOAD) { len = UX_HLINK_FEATURE_PAYLOAD; }
	for (i = 0; i < len; i++)
	{
		ux_hlink_rx_buf[i] = p[i];
		response[i] = p[i];
	}
	for (; i < UX_HLINK_FEATURE_PAYLOAD; i++)
	{
		ux_hlink_rx_buf[i] = 0;
	}
	/*dispatch returns 1 when the command already queued its own response
	 *(OTA slot commands) - then the echo fallback must stay silent*/
	if (!ux_host_link_dispatch(ux_hlink_rx_buf, response))
	{
		ux_host_link_tx(UX_HLINK_INPUT_PAYLOAD, response);
	}
	ux_hlink_rx_seq ++;
	#if (BIBOO_UX_DEBUG)
	dbg_hrx_cnt ++;
	#endif
	#if (UART_PRINT_DEBUG_ENABLE && BIBOO_UX_DEBUG)
	if (!UX_HLINK_DUMP_MUTED())
		ux_hlink_dump("[HLINK] RX: ", p, len);
	#endif
}


/*---- test beacon (dongle -> host uplink) ----------------------------------
 * While the BLE mouse link is up (link state CONN), push a rolling test
 * Input Report every second. The host receives the frame through its
 * pending Interrupt IN read.
 * Set UX_HLINK_TEST_BEACON to 0 to mute it.--------------------------------*/
#define UX_HLINK_TEST_BEACON   0     /*test: 1s uplink beacon while the mouse link is up*/
#define UX_HLINK_BEACON_MS     1000  /*beacon period, ms*/

#if (UX_HLINK_TEST_BEACON)
static void ux_host_link_beacon_proc(void)
{
	static u32 tick = 0;
	static u8 seq = 0;
	u8 pay[UX_HLINK_INPUT_PAYLOAD];
	u8 i;

	if (blc_ll_getCurrentState() != BLS_LINK_STATE_CONN)
	{
		tick = 0;	/*link down: reset, first frame goes out right after (re)connect*/
		return;
	}
#if (BIBOO_UX_FACTORY_TEST)
	/*BIBOO UX: pause while the factory-test mode is active (its 0xE1/0xE2/0xE3
	 *events replace the beacon; tick keeps rolling so the first beacon after
	 *FT_STOP is one period away)*/
	if (ux_ft_host_active())
	{
		tick = clock_time();
		return;
	}
#endif
	if (!clock_time_exceed(tick, UX_HLINK_BEACON_MS * 1000))
	{
		return;
	}
	tick = clock_time();
	seq ++;
	pay[0] = UX_HLINK_CMD_BEACON;	/*test beacon command id*/
	pay[1] = seq;					/*rolling sequence - host checks liveness with it*/
	for (i = 2; i < UX_HLINK_INPUT_PAYLOAD; i++)
	{
		pay[i] = 0;
	}
	ux_host_link_tx(UX_HLINK_INPUT_PAYLOAD, pay);	/*dumps "[HLINK] TX:" on the debug uart*/
}
#endif


static void ux_host_link_send_input_report(const u8 *report)
{
	reg_usb_ep_ptr(USB_EDP_APP_IN) = 0;
	reg_usb_ep_dat(USB_EDP_APP_IN) = UX_HLINK_INPUT_REPORT_ID;
	foreach(i, UX_HLINK_INPUT_PAYLOAD)
	{
		reg_usb_ep_dat(USB_EDP_APP_IN) = report[i];
	}
	reg_usb_ep_ctrl(USB_EDP_APP_IN) = FLD_EP_DAT_ACK | (edp_toggle[USB_EDP_APP_IN] ? FLD_USB_EP_DAT1 : FLD_USB_EP_DAT0);
	edp_toggle[USB_EDP_APP_IN] ^= 1;
}

static void ux_host_link_tx_proc(void)
{
	u8 report[UX_HLINK_INPUT_PAYLOAD];
	u8 irq_en;

	/*drain the whole fifo per iteration (2026-10-20 fix): sending only one
	 *report per main-loop iteration made the uplink hostage to the main
	 *loop period - with the OTA stream pipe sharing the same loop, a
	 *frame-synchronous protocol (PC sends one frame, waits for its ack)
	 *saw multi-ms RTT jitter and the 8-deep fifo could fill up under
	 *burst conditions, silently dropping acks (ux_host_link_tx() drops
	 *when full). The EP is only written while it is idle, so draining
	 *more per iteration is safe and keeps the RTT close to the USB frame
	 *time even when the main loop is busy.*/
	while (!usbhw_is_ep_busy(USB_EDP_APP_IN))
	{
		irq_en = irq_disable();
		if (ux_hlink_tx_rptr == ux_hlink_tx_wptr)
		{
			irq_restore(irq_en);
			return;
		}
		memcpy(report, ux_hlink_tx_fifo[ux_hlink_tx_rptr], sizeof(report));
		ux_hlink_tx_rptr = (ux_hlink_tx_rptr + 1) & (UX_HLINK_TX_FIFO_NUM - 1);
		irq_restore(irq_en);

		ux_host_link_send_input_report(report);
	}
}

/*main loop (dispatched from user_module_proc): asynchronous command
 *extension point. Commands that need more than a few us (flash access,
 *radio commands, ...) must not run in the EP0 irq: their dispatch() case
 *sets a pending flag, the work runs here and pushes the response with
 *ux_host_link_tx().*/
void ux_host_link_proc(void)
{
	#if (UX_HLINK_TEST_BEACON)
	ux_host_link_beacon_proc();
	#endif
	ux_host_link_tx_proc();
	(void)ux_hlink_rx_seq;
}


/*1 when everything queued for the uplink has really left (fifo drained and
 *the IN endpoint idle). vendor/user/self_ota uses this to hold its reboot
 *back until the COMMIT response is on the wire - a blind sleep would also
 *work, but this keeps the main loop (and the BLE link) alive while waiting.*/
u8 ux_host_link_tx_idle(void)
{
	if (ux_hlink_tx_rptr != ux_hlink_tx_wptr)
	{
		return 0;
	}
	return usbhw_is_ep_busy(USB_EDP_APP_IN) ? 0 : 1;
}


void ux_host_link_tx(u8 len, u8 *p)
{
	u8 report[UX_HLINK_INPUT_PAYLOAD] = {0};
	u8 next_wptr;
	u8 irq_en;

	if (len > UX_HLINK_INPUT_PAYLOAD) { len = UX_HLINK_INPUT_PAYLOAD; }
	memcpy(report, p, len);

	irq_en = irq_disable();
	next_wptr = (ux_hlink_tx_wptr + 1) & (UX_HLINK_TX_FIFO_NUM - 1);
	if (next_wptr == ux_hlink_tx_rptr)
	{
		irq_restore(irq_en);
		#if (BIBOO_UX_DEBUG)
		dbg_htx_drop_cnt ++;
		#endif
		return;
	}
	memcpy(ux_hlink_tx_fifo[ux_hlink_tx_wptr], report, sizeof(report));
	ux_hlink_tx_wptr = next_wptr;
	irq_restore(irq_en);

	#if (BIBOO_UX_DEBUG)
	dbg_htx_cnt ++;
	#endif
	#if (UART_PRINT_DEBUG_ENABLE && BIBOO_UX_DEBUG)
	if (!UX_HLINK_DUMP_MUTED())
		ux_hlink_dump("[HLINK] TX: ", report, UX_HLINK_INPUT_PAYLOAD);
	#endif
}


/*----------------------------------------------------------------------------
 * mouse report mirror hook (att_mouse top level, dispatched through
 * user_module_att_mouse_hlink, user_module_manage.c).
 * Same hook style as the LED consumer (user_module_att_mouse_led): att_mouse()
 * calls this unconditionally and the module itself decides what to do with
 * the report.
 * Factory-test mode active: wrap the raw BLE report into a 0xE1 host link
 * event for the PC and return 1. Normal mode (or BIBOO_UX_FACTORY_TEST
 * off): return 0. Pure mirror - att_mouse() always continues into the USB
 * forwarding, so the PC mouse keeps working during factory test.
 *--------------------------------------------------------------------------*/
int ux_host_link_att_mouse(u8 *p)
{
#if (BIBOO_UX_FACTORY_TEST)
	if (ux_ft_host_active())
	{
		u8 rpt[UX_FT_HLINK_SIZE];
		u8 i;
		/*raw BLE report bytes (btn first; MOUSE_DATA_LEN_AAA layout, the PC
		 *interprets them according to the PING-reported mouse length)*/
		u8 n = MOUSE_DATA_LEN_AAA;

		memset(rpt, 0, sizeof(rpt));
		rpt[UX_FT_EVT_OFFS_ID]  = UX_FT_EVT_MOUSE;
		rpt[UX_FT_EVT_OFFS_SEQ] = ux_hlink_evt_seq++;
		for (i = 0; (i < n) && ((UX_FT_EVT_OFFS_DATA + i) < UX_FT_HLINK_SIZE); i++)
		{
			rpt[UX_FT_EVT_OFFS_DATA + i] = p[i];
		}
		ux_host_link_tx(UX_FT_HLINK_SIZE, rpt);

		return 1;	/*mirrored to the PC as a 0xE1 event*/
	}
#else
	(void)p;
#endif
	return 0;
}


#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK */
