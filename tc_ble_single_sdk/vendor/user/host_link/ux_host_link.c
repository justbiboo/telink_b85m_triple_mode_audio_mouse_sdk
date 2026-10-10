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

#include "drivers.h"							/* irq_disable / irq_restore */


/*----------------------------------------------------------------------------
 * BIBOO UX: host link command set (payload[0]).
 * Known commands are dispatched synchronously inside ux_host_link_rx()
 * (EP0 irq context - keep them lightweight); the response overwrites the
 * tx buffer and is read back by the host with the next GET_REPORT.
 * Unknown commands keep the default echo response, so the legacy
 * "SET -> GET readback compare" of the host tool keeps passing.
 *   0x00       : ECHO - default / fallback for unknown cmds (payload mirrored)
 *   0x01       : IDENT - module presence & version probe
 *   0x02..0x7f : reserved for business commands (dongle side features)
 *   0x80..     : reserved for debug commands
 * Heavy commands must NOT run in the EP0 irq: their dispatch() case should
 * only set a pending flag, the real work happens in ux_host_link_proc()
 * (main loop) and pushes the response via ux_host_link_tx().
 *--------------------------------------------------------------------------*/
#define UX_HLINK_CMD_ECHO    0x00
#define UX_HLINK_CMD_IDENT   0x01

/*module identification (cmd IDENT response): "BIBOO" magic + module version*/
#define UX_HLINK_MAGIC0      0xB1
#define UX_HLINK_MAGIC1      0xB0
#define UX_HLINK_VERSION     0x01

#define UX_HLINK_PAYLOAD     7      /*report id 0x05 payload size (bytes)*/


/*last SET_REPORT payload (downlink command, [0]=cmd)*/
static u8 ux_hlink_rx_buf[UX_HLINK_PAYLOAD];
/*GET_REPORT readback content (uplink response, [0]=cmd)*/
static u8 ux_hlink_tx_buf[UX_HLINK_PAYLOAD];
/*bumped on every downlink frame; wake marker for ux_host_link_proc()*/
static volatile u8 ux_hlink_rx_seq = 0;

#if (BIBOO_UX_DEBUG)
/*BIBOO debug: host link counters (volatile; hook them into the status
 *print of blm_host.c dbg_ble_status_print when needed)*/
volatile u32 dbg_hrx_cnt = 0;   //SET_REPORT(0x05) frames received (downlink)
volatile u32 dbg_hget_cnt = 0;  //GET_REPORT(0x05) readbacks served (uplink)
volatile u32 dbg_hcmd_cnt = 0;  //known commands dispatched (echo excluded)
volatile u32 dbg_htx_cnt = 0;   //business uplink writes (ux_host_link_tx)
#endif


#if (UART_PRINT_DEBUG_ENABLE && BIBOO_UX_DEBUG)
/*BIBOO debug: dump one host link frame, e.g.
 * "[HLINK] RX: 01 00 00 00 00 00 00".
 * NOTE: also called from the EP0 irq (SET / GET_REPORT context). The 0x05
 * channel is a low frequency control channel, so the ~2ms busy-wait GPIO
 * UART print is acceptable there: the SET payload is already drained from
 * the ctrl ep fifo, and the GET status stage is NAK-ed by hardware until
 * the irq returns. Keep frames short and do not add high frequency polls
 * on the host side while this print is enabled.*/
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


/*synchronous command dispatch, called from ux_host_link_rx (EP0 irq).
 * p = 7-byte command payload ([0]=cmd, [1..6]=args);
 * r = 7-byte response buffer, already mirroring p (echo) on entry, so
 * unknown commands simply return and keep the echo response.*/
static void ux_host_link_dispatch(const u8 *p, u8 *r)
{
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

		default:
			/*unknown / ECHO command: keep the echo response*/
			break;
	}
}


/*user_init (dispatched from user_module_init): reset the host link state*/
void ux_host_link_init(void)
{
	u8 i;
	for (i = 0; i < UX_HLINK_PAYLOAD; i++)
	{
		ux_hlink_rx_buf[i] = 0;
		ux_hlink_tx_buf[i] = 0;
	}
	ux_hlink_rx_seq = 0;
}


/*downlink (EP0 irq, usb.c SET_REPORT Feature 0x05): route a host command
 *payload into the module. The payload is mirrored into the tx buffer first
 *(default echo response), then dispatched: known commands overwrite the
 *response synchronously, so "SET -> immediate GET" already reads it.*/
void ux_host_link_rx(u8 len, u8 *p)
{
	u8 i;
	if (len > UX_HLINK_PAYLOAD) { len = UX_HLINK_PAYLOAD; }
	for (i = 0; i < len; i++)
	{
		ux_hlink_rx_buf[i] = p[i];
		ux_hlink_tx_buf[i] = p[i];
	}
	ux_host_link_dispatch(ux_hlink_rx_buf, ux_hlink_tx_buf);
	ux_hlink_rx_seq ++;		/*wake the main loop (see ux_host_link_proc)*/
	#if (BIBOO_UX_DEBUG)
	dbg_hrx_cnt ++;
	#endif
	#if (UART_PRINT_DEBUG_ENABLE && BIBOO_UX_DEBUG)
	ux_hlink_dump("[HLINK] RX: ", p, len);	/*BIBOO debug: host downlink dump*/
	#endif
}


/*main loop (dispatched from user_module_proc): asynchronous command
 *extension point. Commands that need more than a few us (flash access,
 *radio commands, ...) must not run in the EP0 irq: their dispatch() case
 *sets a pending flag, the work runs here and pushes the response with
 *ux_host_link_tx(); the host reads it with the next GET_REPORT.
 *ux_hlink_rx_seq (bumped by every SET_REPORT) is the wake marker.
 *No async commands yet - nothing to do here.*/
void ux_host_link_proc(void)
{
	(void)ux_hlink_rx_seq;
}


/*uplink readback (EP0 irq, usb.c GET_REPORT Feature 0x05): 7-byte response
 *payload served to the host (echo of the last SET payload by default, or
 *the response of the last dispatched command / business tx)*/
u8 *ux_host_link_tx_buf(void)
{
	#if (BIBOO_UX_DEBUG)
	/*single call per GET_REPORT transfer: usb.c gates the 0x05 GET branch
	 *to the EP0 setup stage only (the SDK default runs every GET case in
	 *both the setup and the data stage, which would double this counter
	 *and the dump below; see the comment at the usb.c call site)*/
	dbg_hget_cnt ++;
	#endif
	#if (UART_PRINT_DEBUG_ENABLE && BIBOO_UX_DEBUG)
	ux_hlink_dump("[HLINK] TXGET: ", ux_hlink_tx_buf, UX_HLINK_PAYLOAD);	/*BIBOO debug: uplink readback dump*/
	#endif
	return ux_hlink_tx_buf;
}


/*business uplink (any context): queue a 7-byte response/data payload for
 *the host to read with its next GET_REPORT(Feature,0x05). Atomic against
 *the EP0 irq (GET readback / SET echo mirror), which runs on the same
 *tx buffer.*/
void ux_host_link_tx(u8 len, u8 *p)
{
	u8 i;
	u8 irq_en;
	if (len > UX_HLINK_PAYLOAD) { len = UX_HLINK_PAYLOAD; }
	irq_en = irq_disable();		/*atomic vs. EP0 irq on ux_hlink_tx_buf*/
	for (i = 0; i < len; i++)
	{
		ux_hlink_tx_buf[i] = p[i];
	}
	irq_restore(irq_en);
	#if (BIBOO_UX_DEBUG)
	dbg_htx_cnt ++;
	#endif
	#if (UART_PRINT_DEBUG_ENABLE && BIBOO_UX_DEBUG)
	ux_hlink_dump("[HLINK] TX: ", p, len);	/*BIBOO debug: business uplink dump*/
	#endif
}


#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK */
