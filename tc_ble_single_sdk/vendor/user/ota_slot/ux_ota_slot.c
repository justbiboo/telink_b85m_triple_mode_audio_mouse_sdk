/********************************************************************************************************
 * @file    ux_ota_slot.c
 *
 * @brief   BIBOO UX module (dongle side): PC-fed OTA stream pipe + master-OTA
 *          driver (implementation). See ux_ota_slot.h for the protocol, the
 *          overall PC-driven flow and why this replaced the flash-slot design.
 *
 *          Concurrency design (important):
 *            - ux_ota_slot_host_cmd() runs in the EP0 SET_REPORT irq and only
 *              does validation + a 13-byte memcpy into a RAM fifo (or sets a
 *              flag). No flash, no BLE API calls that block.
 *            - ux_ota_slot_proc() runs in the main loop: deferred SLOT_BEGIN
 *              response, rx-idle watchdog, progress log, and the IDLE reset
 *              after the master OTA latches its result.
 *            - The actual radio push lives in blm_ota.c proc_ota step 6,
 *              which pulls 16B blocks from the fifo via ux_ota_stream_get16.
 *
 * @author  BIBOO UX
 *
 ***********************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"				/*clock_time*/
#include "stack/ble/ble.h"			/*blc_ll_getCurrentState / BLS_LINK_STATE_CONN*/

#include "ux_ota_slot.h"

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)

#if (!BLE_MASTER_OTA_ENABLE)
#error "BIBOO_UX_OTA_SLOT requires BLE_MASTER_OTA_ENABLE=1 (vendor/ble_master_kma_dongle/app_config.h)"
#endif

#include "../host_link/ux_host_link.h"		/*ux_host_link_tx (deferred responses)*/
#include "../factory_test/ux_ft_proto.h"	/*UX_FT_ST_* status codes + frame offsets*/


/* ---- blm_ota.c globals (master OTA framework) ---- */
extern int master_ota_cmd;		/*1 = PC asked for a master OTA*/
extern int host_ota_start;		/*master OTA state machine step*/
extern u32 ota_adr;				/*bytes pushed to the mouse so far*/
extern u32 n_firmware;			/*image size for the running push*/
extern int master_ota_result;	/*0 none/running, 1 ok, 2 fail (latched)*/
extern void ota_set_result(int success);	/*latch + reset the state machine*/


/* ================ stream state ================
 * ota_stream_total != 0 selects stream mode in proc_ota (the image size
 * comes from SLOT_BEGIN instead of the flash slot header). */
u32 ota_stream_total = 0;

/*rx fifo: EP0 irq writes, proc_ota drains 16B at a time.
 *Entry = [len][data..]; ux_ota_fifo_roff tracks a partially popped frame.*/
#define UX_OTA_FIFO_FRAMES		24
#define UX_OTA_FIFO_ENTRY		(1 + UX_OTA_DATA_MAX)

/*rx inactivity watchdog: if the PC dies / the cable is pulled mid-upload
 *the stream can never complete, so abort (this also fails a running push).
 *10s without a single frame is far beyond any legit gap (frame-synchronous
 *PC, ~15ms RTT), so it never kills a healthy transfer.*/
#define UX_OTA_IDLE_TIMEOUT_S	10

static u8  ux_ota_state;			/*UX_OTA_ST_*/
static u32 ux_ota_fifo_bytes;		/*bytes queued but not yet pushed*/
static u32 ux_ota_taken;			/*bytes consumed by the BLE push*/
static volatile u8  ux_ota_stream_end;		/*PC sent SLOT_END*/
static volatile u32 ux_ota_last_rx_tick;	/*last SLOT_BEGIN/DATA rx (idle watchdog)*/

static u8  ux_ota_fifo[UX_OTA_FIFO_FRAMES][UX_OTA_FIFO_ENTRY];
static volatile u8 ux_ota_fifo_wptr = 0;
static volatile u8 ux_ota_fifo_rptr = 0;
static u8  ux_ota_fifo_roff = 0;	/*read offset inside the head frame*/

/*Staging block: the next 16B OTA block, filled from the fifo but NOT yet
 *counted as pushed. blm_ota.c commits it only after blc_gatt_pushWriteCommand
 *returned BLE_SUCCESS (see ux_ota_stream_commit).
 *
 *2026-10-21 field fix (why this exists at all): the old ux_ota_stream_get16
 *popped straight out of the fifo and immediately did ux_ota_taken += got, so
 *   (a) when the fifo held a 12B SLOT_DATA frame but 16B were needed it
 *       zero-padded the gap and returned the block as "ready" - and because
 *       the PC feeds 12B/frame while the mouse consumes 16B/PDU, that short
 *       block was not an edge case but the COMMON case;
 *   (b) a failed blc_gatt_pushWriteCommand dropped those bytes (fifo already
 *       popped, ota_adr not advanced) and every later block slid one slot.
 *Either way the mouse received adr_index=0 with data that was not the start
 *of the image, failed its Telink-magic check on fw[6]/fw[8..11] ("KNLT"),
 *and answered OTA_FW_CHECK_ERR(6) within ~30ms of CMD_OTA_START, then
 *rearmed its shutdown flag and dropped the link. Field arithmetic from the
 *21:17 log confirms it: the PC had fed 384B with 276B still queued (=108B
 *actually left the fifo) while ota_adr said 128 - 20 bytes of padding had
 *been injected into the image.*/
static u8  ux_ota_stg[16];
static u8  ux_ota_stg_len = 0;		/*real bytes staged for the NEXT block*/

/*deferred SLOT_BEGIN response (released when proc_ota reaches the push phase)*/
static volatile u8 ux_ota_pend = 0;
static u8  ux_ota_pend_cmd;
static u8  ux_ota_pend_seq;


/*----------------------------------------------------------------------------
 * helpers
 *--------------------------------------------------------------------------*/

/*build one uplink response and push it through the host link tx fifo*/
static void ux_ota_send_rsp(u8 cmd, u8 seq, u8 status, const u8 *data, u8 dlen)
{
	u8 r[UX_FT_HLINK_SIZE];

	memset(r, 0, sizeof(r));
	r[UX_FT_UL_OFFS_CMD]    = cmd | UX_FT_RSP_MASK;
	r[UX_FT_UL_OFFS_SEQ]    = seq;
	r[UX_FT_UL_OFFS_STATUS] = status;
	if (dlen)
	{
		memcpy(&r[UX_FT_UL_OFFS_DATA], data, dlen);
	}
	ux_host_link_tx(UX_FT_HLINK_SIZE, r);
}

static inline u8 ux_ota_fifo_empty(void)
{
	return ux_ota_fifo_rptr == ux_ota_fifo_wptr && ux_ota_fifo_roff == 0;
}

/*EP0 irq: append one frame. 0 = fifo full (PC must retry).*/
static u8 ux_ota_fifo_push(const u8 *data, u8 len)
{
	u8 next_w = (u8)((ux_ota_fifo_wptr + 1) % UX_OTA_FIFO_FRAMES);

	if (next_w == ux_ota_fifo_rptr)
	{
		return 0;
	}
	ux_ota_fifo[ux_ota_fifo_wptr][0] = len;
	memcpy(&ux_ota_fifo[ux_ota_fifo_wptr][1], data, len);
	ux_ota_fifo_wptr = next_w;
	ux_ota_fifo_bytes += len;
	return 1;
}

/*main loop: pop up to max bytes, spanning frame boundaries if needed.
 *0 = fifo empty.*/
static u8 ux_ota_fifo_pop(u8 *dst, u8 max)
{
	u8 n;

	if (ux_ota_fifo_empty())
	{
		return 0;
	}
	n = ux_ota_fifo[ux_ota_fifo_rptr][0] - ux_ota_fifo_roff;
	if (n > max)
	{
		n = max;
	}
	memcpy(dst, &ux_ota_fifo[ux_ota_fifo_rptr][1] + ux_ota_fifo_roff, n);
	ux_ota_fifo_roff += n;
	if (ux_ota_fifo_roff >= ux_ota_fifo[ux_ota_fifo_rptr][0])
	{
		ux_ota_fifo_roff = 0;
		ux_ota_fifo_rptr = (u8)((ux_ota_fifo_rptr + 1) % UX_OTA_FIFO_FRAMES);
	}
	ux_ota_fifo_bytes -= n;
	return n;
}

/*reset all stream state back to "no session" (does NOT touch
 *master_ota_result - the PC must still be able to poll it)*/
static void ux_ota_stream_reset(void)
{
	ux_ota_state       = UX_OTA_ST_IDLE;
	ota_stream_total   = 0;
	ux_ota_fifo_bytes  = 0;
	ux_ota_taken       = 0;
	ux_ota_stg_len     = 0;
	ux_ota_stream_end  = 0;
	ux_ota_fifo_wptr   = 0;
	ux_ota_fifo_rptr   = 0;
	ux_ota_fifo_roff   = 0;
	ux_ota_pend        = 0;
}

/*Abort the current session (idle watchdog / stale BEGIN). Stops a running
 *master OTA first - the PC is gone, so the image can never complete; the
 *mouse would time out on its own, but failing it here frees the dongle
 *state machine and latches a result the PC can poll. ota_set_result()
 *touches GPIO, so only call this from main-loop context (the irq-context
 *stale-BEGIN path can never have a push running: BEGIN is BUSY-rejected
 *in that case, see ux_ota_slot_host_cmd).*/
static void ux_ota_slot_abort(const char *why)
{
	if (master_ota_cmd || host_ota_start)
	{
		ota_set_result(0);
	}
	ux_ota_stream_reset();
	printf("[ux_ota_slot] session aborted: %s\r\n", why);
}


/*----------------------------------------------------------------------------
 * stream API (called from blm_ota.c proc_ota, main-loop context)
 *--------------------------------------------------------------------------*/

/*Build (but do not commit) the next 16B block.
 *Returns 1 with dst[0..15] filled when a COMPLETE block is available - 16
 *real bytes, or the short tail block 0xFF-padded exactly like the PC-direct
 *channel (which is the channel that passes, so the bytes must match it).
 *Returns 0 when the fifo cannot complete the block yet; the partially filled
 *staging buffer is kept and topped up by the next call, nothing is lost.
 *The caller MUST call ux_ota_stream_commit() after the push was accepted.*/
int ux_ota_stream_get16(u8 *dst)
{
	u8 need;

	if (ux_ota_taken >= ota_stream_total)
	{
		return 0;		/*image complete*/
	}

	/*real bytes that belong to this block: 16, or the short tail*/
	need = (ota_stream_total - ux_ota_taken) >= 16
		   ? 16 : (u8)(ota_stream_total - ux_ota_taken);

	while (ux_ota_stg_len < need)
	{
		u8 n = ux_ota_fifo_pop(ux_ota_stg + ux_ota_stg_len,
							   (u8)(need - ux_ota_stg_len));
		if (n == 0)
		{
			/*fifo dry mid-block: WAIT for the next PC frame and keep the
			 *bytes already staged. Never hand out a padded short block -
			 *that was the 2026-10-21 corruption.*/
			return 0;
		}
		ux_ota_stg_len += n;
	}

	memcpy(dst, ux_ota_stg, need);
	if (need < 16)
	{
		/*tail: pad to a full PDU with 0xFF (flash erase state), byte for
		 *byte what OtaCore.BuildDataPdu writes on the BLE-direct path*/
		memset(dst + need, 0xFF, (u16)(16 - need));
	}
	return 1;
}

/*Commit the block handed out by ux_ota_stream_get16. Call ONLY after
 *blc_gatt_pushWriteCommand returned BLE_SUCCESS: a rejected push must leave
 *the staging buffer intact so the identical bytes are re-sent.*/
void ux_ota_stream_commit(void)
{
	ux_ota_taken += ux_ota_stg_len;
	ux_ota_stg_len = 0;
}

u8 ux_ota_stream_eof(void)
{
	return (ux_ota_stream_end && ux_ota_stg_len == 0 &&
			ux_ota_taken >= ota_stream_total) ? 1 : 0;
}

/*1 = a full block can be composed right now (staged + queued bytes cover
 *whatever this block still needs). blm_ota.c step 6 gates on this: the old
 *"pending >= 16" gate deadlocked on the short tail block, where only 4 bytes
 *are left and no further frame will ever arrive.*/
u8 ux_ota_stream_ready(void)
{
	u32 rem;
	u32 avail = (u32)ux_ota_stg_len + ux_ota_fifo_bytes;

	if (ux_ota_taken >= ota_stream_total)
	{
		return 0;
	}
	rem = ota_stream_total - ux_ota_taken;
	if (rem > 16)
	{
		rem = 16;
	}
	return avail >= rem ? 1 : 0;
}

/*bytes queued in the fifo (or staged) that the push engine has not taken
 *yet. blm_ota.c step 6 uses this for its stall watchdog: data waiting here
 *while no PDU goes out means the BLE push path (not the PC feed) is the
 *bottleneck. */
u32 ux_ota_stream_pending(void)
{
	return ux_ota_fifo_bytes + ux_ota_stg_len;
}


/*----------------------------------------------------------------------------
 * host link command entry (EP0 irq - keep it lightweight!)
 *--------------------------------------------------------------------------*/
int ux_ota_slot_host_cmd(const u8 *p, u8 *r)
{
	u8 cmd = p[UX_FT_DL_OFFS_CMD];
	u8 seq = p[UX_FT_DL_OFFS_SEQ];
	const u8 *args = &p[UX_FT_DL_OFFS_ARGS];

	switch (cmd)
	{
		case UX_OTA_CMD_SLOT_BEGIN:
		{
			u32 total = (u32)args[0] | ((u32)args[1] << 8) |
						((u32)args[2] << 16) | ((u32)args[3] << 24);

			if (total == 0 || total > UX_OTA_IMAGE_MAX)
			{
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				break;
			}
			if (master_ota_cmd != 0 || host_ota_start != 0)
			{
				/*previous push still running - starting a new image now
				 *would corrupt it. This is the ONLY BUSY case (stale
				 *sessions are aborted below, they used to deadlock every
				 *retry - see the 19:22 field failure).*/
				u8 d = UX_OTA_BEGIN_BUSY_OTA;
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_BUSY, &d, 1);
				break;
			}
			if (blc_ll_getCurrentState() != BLS_LINK_STATE_CONN)
			{
				/*nothing to push to: the BLE link to the mouse IS the
				 *push path (a PC-direct connection does not count)*/
				u8 d = UX_OTA_BEGIN_DUT_NOT_CONN;
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_ERR, &d, 1);
				break;
			}
			if (ux_ota_state != UX_OTA_ST_IDLE)
			{
				/*stale session from a previous (dead) transfer. The PC
				 *is the only initiator, so a new BEGIN always means the
				 *old transfer is over: abort it instead of blocking the
				 *new upload forever.*/
				ux_ota_slot_abort("stale session on new BEGIN");
			}

			/*arm the stream and kick the master OTA state machine. No
			 *flash work at all: proc_ota updates the connection
			 *parameters, discovers the mouse OTA characteristic and
			 *sends CMD_OTA_START; the deferred OK below is released
			 *once the push engine actually consumes the fifo.*/
			ota_stream_total  = total;
			ux_ota_fifo_bytes = 0;
			ux_ota_taken      = 0;
			ux_ota_stg_len    = 0;
			ux_ota_stream_end = 0;
			ux_ota_fifo_wptr  = 0;
			ux_ota_fifo_rptr  = 0;
			ux_ota_fifo_roff  = 0;
			ux_ota_state      = UX_OTA_ST_STREAM;
			ux_ota_last_rx_tick = clock_time();

			master_ota_result = 0;
			master_ota_cmd    = 1;	/*blm_ota.c proc_ota takes over*/

			ux_ota_pend_cmd = cmd;
			ux_ota_pend_seq = seq;
			ux_ota_pend     = 1;
			/*no response yet: the deferred OK is pushed from
			 *ux_ota_slot_proc once host_ota_start reaches the push phase*/
			printf("[ux_ota_slot] begin stream: %d bytes (arming master OTA)\r\n",
				   (int)total);
			break;
		}

		case UX_OTA_CMD_SLOT_DATA:
		{
			u8 len = args[UX_OTA_DATA_OFFS_LEN];

			if (len < 1 || len > UX_OTA_DATA_MAX)
			{
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				break;
			}
			/*idle watchdog food on EVERY data frame - a BUSY (fifo full)
			 *retry storm also proves the PC is alive and feeding; only the
			 *PC actually going away should abort the session. Previously
			 *only accepted frames refreshed the tick, so a slow master
			 *OTA engine (still in conn-update / discovery) plus a full
			 *fifo could trip the 10s watchdog mid-upload. */
			ux_ota_last_rx_tick = clock_time();
			if (ux_ota_state != UX_OTA_ST_STREAM || ux_ota_stream_end)
			{
				/*not streaming / PC already closed the stream*/
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				break;
			}
			if (ux_ota_fifo_bytes + ux_ota_taken + ux_ota_stg_len + len >
				ota_stream_total)
			{
				/*PC sends more than the announced size: protocol error*/
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				break;
			}
			if (!ux_ota_fifo_push(&args[UX_OTA_DATA_OFFS_DATA], len))
			{
				/*fifo full: the push engine is behind (BLE congested?).
				 *Tell the PC to retry - nothing is lost.*/
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_BUSY, NULL, 0);
				break;
			}

			ux_ota_send_rsp(cmd, seq, UX_FT_ST_OK, NULL, 0);
			break;
		}

		case UX_OTA_CMD_SLOT_END:
		{
			u8 d[4];

			if (ux_ota_state != UX_OTA_ST_STREAM)
			{
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				break;
			}
			/*staged bytes count too: a partial block sits in ux_ota_stg
			 *between two get16 calls (the PC feeds 12B/frame), so
			 *fifo+taken alone would never reach the announced size and
			 *every SLOT_END would be rejected*/
			if (ux_ota_fifo_bytes + ux_ota_taken + ux_ota_stg_len !=
				ota_stream_total)
			{
				/*PC sent fewer/more bytes than announced*/
				ux_ota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				break;
			}
			ux_ota_stream_end = 1;
			ux_ota_state      = UX_OTA_ST_FINISH;

			d[0] = (u8)(ota_stream_total);
			d[1] = (u8)(ota_stream_total >> 8);
			d[2] = (u8)(ota_stream_total >> 16);
			d[3] = (u8)(ota_stream_total >> 24);
			ux_ota_send_rsp(cmd, seq, UX_FT_ST_OK, d, 4);
			printf("[ux_ota_slot] stream complete: %d bytes, pushing tail...\r\n",
				   (int)ota_stream_total);
			break;
		}

		case UX_OTA_CMD_STATUS:
		{
			u8 d[12];
			u32 adr    = ota_adr;
			u32 fwsize = n_firmware;

			d[UX_OTA_STS_OFFS_STATE] = ux_ota_state;
			d[UX_OTA_STS_OFFS_MCMD]  = (u8)master_ota_cmd;
			d[UX_OTA_STS_OFFS_START] = (u8)host_ota_start;
			d[UX_OTA_STS_OFFS_ADR_L + 0] = (u8)(adr);
			d[UX_OTA_STS_OFFS_ADR_L + 1] = (u8)(adr >> 8);
			d[UX_OTA_STS_OFFS_ADR_L + 2] = (u8)(adr >> 16);
			d[UX_OTA_STS_OFFS_ADR_L + 3] = (u8)(adr >> 24);
			d[UX_OTA_STS_OFFS_SIZE_L + 0] = (u8)(fwsize);
			d[UX_OTA_STS_OFFS_SIZE_L + 1] = (u8)(fwsize >> 8);
			d[UX_OTA_STS_OFFS_SIZE_L + 2] = (u8)(fwsize >> 16);
			d[UX_OTA_STS_OFFS_SIZE_L + 3] = (u8)(fwsize >> 24);
			d[UX_OTA_STS_OFFS_RESULT] = (u8)master_ota_result;

			ux_ota_send_rsp(cmd, seq, UX_FT_ST_OK, d, sizeof(d));
			break;
		}

		default:
			return 0;	/*not an OTA pipe command*/
	}

	return 1;
}


/*----------------------------------------------------------------------------
 * module entry (dispatched by user_module_manage.c)
 *--------------------------------------------------------------------------*/
void ux_ota_slot_init(void)
{
	ux_ota_stream_reset();
	ux_ota_last_rx_tick = 0;
	printf("[ux_ota_slot] init (stream pipe mode, no flash)\r\n");
}

u8 ux_ota_slot_busy(void)
{
	return (ux_ota_state == UX_OTA_ST_STREAM || ux_ota_state == UX_OTA_ST_FINISH) ? 1 : 0;
}

void ux_ota_slot_proc(void)
{
	static u32 last_log_tick = 0;

	/*(0) rx inactivity watchdog: the PC went away mid-upload (crash,
	 *cable, closed tool). The stream can never complete, so abort -
	 *this also fails a running push and re-locks nothing (there is no
	 *flash involved any more).*/
	if (ux_ota_last_rx_tick &&
		(ux_ota_state == UX_OTA_ST_STREAM || ux_ota_state == UX_OTA_ST_FINISH) &&
		clock_time_exceed(ux_ota_last_rx_tick, UX_OTA_IDLE_TIMEOUT_S * 1000000))
	{
		ux_ota_slot_abort("rx idle timeout");
	}

	/*(1) deferred SLOT_BEGIN response: release once the push engine is
	 *actually consuming the fifo (host_ota_start>=6), or fail early if
	 *the master OTA died before that (timeout / disconnect / the mouse
	 *has no OTA characteristic).*/
	if (ux_ota_pend)
	{
		if (host_ota_start >= 6)
		{
			ux_ota_send_rsp(ux_ota_pend_cmd, ux_ota_pend_seq, UX_FT_ST_OK, NULL, 0);
			ux_ota_pend = 0;
			printf("[ux_ota_slot] push engine running, PC feeding...\r\n");
		}
		else if (master_ota_result != 0)
		{
			ux_ota_send_rsp(ux_ota_pend_cmd, ux_ota_pend_seq, UX_FT_ST_ERR, NULL, 0);
			ux_ota_pend = 0;
		}
	}

	/*(2) progress heartbeat once per second while streaming: with this the
	 *dongle UART log shows exactly how far the push got before any stall -
	 *the number itself is the diagnosis. "ota step" is the master OTA
	 *state machine (blm_ota.c proc_ota): 1 conn-param update, 2/3 OTA
	 *characteristic discovery, 4 CMD_OTA_START, 5 start settle, 6 pushing,
	 *7/8/9 END + result. A step that never moves is the stall point. */
	if ((ux_ota_state == UX_OTA_ST_STREAM || ux_ota_state == UX_OTA_ST_FINISH) &&
		clock_time_exceed(last_log_tick, 1000000))
	{
		last_log_tick = clock_time();
		printf("[ux_ota_slot] push %d/%d bytes (fifo %d, ota step %d)\r\n",
			   (int)ota_adr, (int)n_firmware, (int)ux_ota_fifo_bytes, host_ota_start);
	}

	/*(3) master OTA finished: the stream session is over, go back to IDLE
	 *so the next SLOT_BEGIN can start. master_ota_result stays latched -
	 *the PC polls it and the next BEGIN clears it. ota_stream_total is
	 *cleared so proc_ota falls back to the legacy flash source if it is
	 *ever started by the button path.*/
	if (master_ota_result != 0 && ux_ota_state != UX_OTA_ST_IDLE)
	{
		printf("[ux_ota_slot] master OTA %s, pushed %d/%d bytes\r\n",
			   master_ota_result == 1 ? "OK" : "FAIL",
			   (int)ota_adr, (int)n_firmware);
		ux_ota_stream_reset();
	}
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
