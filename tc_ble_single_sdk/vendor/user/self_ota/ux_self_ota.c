/********************************************************************************************************
 * @file    ux_self_ota.c
 *
 * @brief   BIBOO UX module (dongle side): dongle SELF firmware upgrade over the
 *          USB HID host link (implementation). See ux_self_ota.h for the
 *          protocol, the A/B bank layout and the safety model.
 *
 *          Concurrency design (same split as vendor/user/ota_slot):
 *            - ux_self_ota_host_cmd() runs in the EP0 SET_REPORT irq. It only
 *              validates and memcpy's DATA bytes into a small RAM fifo (or
 *              sets a flag). No flash access, no reboot.
 *            - ux_self_ota_proc() runs in the main loop: drains the fifo into
 *              flash (8B writes + 20us gaps, the proven pattern from
 *              vendor/user/user_data - a long flash busy time would starve the
 *              BLE stack and could drop the mouse link), runs the END
 *              verification and performs the COMMIT.
 *
 *          Why the running bank is detected from main() and NOT from
 *          ota_program_offset:
 *            ota_program_offset is a .data constant (0x00000000) in THIS
 *            project - liblt_825x.a's cpu_wakeup_init() does not touch it
 *            (verified by disassembly) and nothing in the dongle sources
 *            assigns it. The reference projects (8258_dual_mouse,
 *            827x_three_mode_mouse) interpret it as "current firmware
 *            offset", so reading it as "where to write the new image" would
 *            erase the very bank we are executing from. The 8258 executes in
 *            place (flash is mapped at 0x0, boot.link puts .vectors at 0x0),
 *            so the address of main() IS the flash offset of the running
 *            image - that is the one source of truth that cannot be stale.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"				/*clock_time / flash_* / sleep_us / start_reboot / ota_program_* */
#include "stack/ble/ble.h"			/*crc32_half_cal (via service/ota/ota.h)*/
#include "vendor/common/flash_prot.h"	/*flash_unlock / flash_lock (APP_FLASH_PROTECTION_ENABLE)*/

#include "ux_self_ota.h"

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_SELF_OTA)

#include "../host_link/ux_host_link.h"		/*ux_host_link_tx (deferred responses)*/
#include "../factory_test/ux_ft_proto.h"	/*UX_FT_ST_* status codes + frame offsets*/


/*----------------------------------------------------------------------------
 * Telink 8258 A/B bank layout
 *--------------------------------------------------------------------------*/
#define SOTA_BANK_SIZE       0x20000     /*128K per bank (== ota_program_bootAddr)*/
#define SOTA_SECTOR_SIZE     4096        /*flash erase granularity*/
#define SOTA_RW_CHUNK        8           /*write granularity (see ux_user_data.c)*/
#define SOTA_RW_GAP_US       20          /*gap between writes, keeps BLE alive*/

extern u16 flash_lockBlock_cmd;			/*defined in vendor/ble_master_kma_dongle/app.c*/

#if (BIBOO_UX_DEBUG)
extern volatile u32 dbg_htx_drop_cnt;	/*ux_host_link.c: uplink fifo drops*/
#endif


/*----------------------------------------------------------------------------
 * session state
 *--------------------------------------------------------------------------*/
static u8  sota_state;					/*UX_SOTA_ST_*/
static u8  sota_result;					/*UX_SOTA_RES_*/
static u32 sota_run_bank;				/*0 or SOTA_BANK_SIZE: bank we execute from*/
static u32 sota_ota_bank;				/*the other one: upgrade target*/
static u32 sota_total;					/*announced image size*/
static u32 sota_recv;					/*bytes queued + written so far*/
static u32 sota_crc;					/*CRC32 computed at END*/

/*erase watermark: next sector start offset not yet erased. The running write
 *cursor is implicit - each DATA frame carries its own byte offset and is
 *written idempotently at that offset.*/
static u32 sota_erased;					/*next sector start offset not yet erased*/

/*1 while the flash protection is off for the running session. Reset by
 *sota_session_reset() - otherwise the SECOND session would skip the unlock
 *and silently write into a locked flash (the first session re-locks at the
 *end, so the flag must go back to 0 with it).*/
static u8  sota_unlocked;

/*pre-erase: the whole target span is erased UP FRONT, right after BEGIN, while
 *the PC is blocked in its BEGIN Input-report read (it did a SET_REPORT then
 *blocks on the read - no new SET_REPORT/SETUP is in flight). This is the ONLY
 *window in the whole flow where the 79ms sector-erase irq_disable is safe:
 *on the live DATA path a pending USB SETUP arriving during that blackout
 *wedges the Telink control endpoint and the host re-enumerates the device
 *(see the 2026-10-21 deadlock). Keeping the erase here means the DATA path
 *only ever does fast page writes (<2ms irq_disable each), which the USB
 *stack tolerates. sota_preerasing is armed by BEGIN and cleared by proc once
 *the whole span is erased; the BEGIN ack is deferred until then.*/
static u8  sota_preerasing;
static u32 sota_erase_target;
static u8  sota_begin_seq;

/*rx fifo: EP0 irq writes, ux_self_ota_proc drains into flash.
 *Entry = [off(4)][len(1)][data..]; sota_fifo_roff tracks a partially popped
 *frame (only happens if a pop asks for fewer bytes than the frame holds -
 *callers always pop a full frame, so it is effectively per-frame).
 *2026-10-22: 16 -> 64 frames: the PC now bursts TWO 24-frame DATA windows
 *(48 v2 9B-payload frames in flight) before it reads back STATUS(0x73)
 *once. The writer side (~9B/ms in proc) always outruns the PC burst, so
 *the fifo only absorbs the in-flight burst; overflow is still safe -
 *overflowed frames are simply missing and the PC re-sends from the STATUS
 *frontier (idempotent). 64x14B = 896B .bss; if the image runs out of RAM
 *step back to 48 (and tell the PC to use 1-window recon).*/
#define SOTA_FIFO_FRAMES     64
#define SOTA_FIFO_ENTRY      (4 + 1 + UX_SOTA_DATA_MAX)

static u8  sota_fifo[SOTA_FIFO_FRAMES][SOTA_FIFO_ENTRY];
static volatile u8 sota_fifo_wptr = 0;
static volatile u8 sota_fifo_rptr = 0;
static u8  sota_fifo_roff = 0;
static volatile u32 sota_last_rx_tick;	/*last BEGIN/DATA rx (idle watchdog)*/

/*deferred END / COMMIT response (released from ux_self_ota_proc)*/
static volatile u8 sota_pend = 0;
static u8  sota_pend_cmd;
static u8  sota_pend_seq;

/*COMMIT sequencing: 0 = response not yet flushed, 1 = flushing, 2 = go*/
static u8  sota_commit_step;
static u32 sota_commit_tick;

/*diagnostics (main-loop prints only - the EP0 irq must stay light):
 *  rx_ok/rx_bad : DATA frames accepted / rejected in the irq
 *  erases       : sector erases issued (each one disables IRQs for the
 *                 whole flash busy time and can stall the BLE stack)
 *  stat_tick    : periodic stats print watermark*/
static volatile u16 sota_dbg_rx_ok;
static volatile u16 sota_dbg_rx_bad;
static u16 sota_dbg_erases;
static u32 sota_dbg_stat_tick;


/*----------------------------------------------------------------------------
 * helpers
 *--------------------------------------------------------------------------*/

/*build one uplink response and push it through the host link tx fifo*/
static void sota_send_rsp(u8 cmd, u8 seq, u8 status, const u8 *data, u8 dlen)
{
	u8 r[UX_FT_HLINK_SIZE];

	/*the uplink data area is 12 bytes; clamp so a bad dlen can never
	 *overflow the frame buffer (this runs in the EP0 irq)*/
	if (dlen > (UX_FT_HLINK_SIZE - UX_FT_UL_OFFS_DATA))
	{
		dlen = (UX_FT_HLINK_SIZE - UX_FT_UL_OFFS_DATA);
	}

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

/*write one u32 LE at offset off of a response data buffer*/
static void sota_put32(u8 *d, int off, u32 v)
{
	d[off + 0] = (u8)(v);
	d[off + 1] = (u8)(v >> 8);
	d[off + 2] = (u8)(v >> 16);
	d[off + 3] = (u8)(v >> 24);
}

/*reset all session state back to "no session" (does NOT re-lock flash: the
 *caller decides - see sota_unlock()/sota_lock())*/
static void sota_session_reset(void)
{
	sota_state       = UX_SOTA_ST_IDLE;
	sota_result      = UX_SOTA_RES_NONE;
	sota_total       = 0;
	sota_recv        = 0;
	sota_crc         = 0;
	sota_erased      = 0;
	sota_unlocked    = 0;
	sota_preerasing  = 0;
	sota_erase_target = 0;
	sota_begin_seq   = 0;
	sota_fifo_wptr   = 0;
	sota_fifo_rptr   = 0;
	sota_fifo_roff   = 0;
	sota_pend        = 0;
	sota_commit_step = 0;
	sota_dbg_rx_ok   = 0;
	sota_dbg_rx_bad  = 0;
	sota_dbg_erases  = 0;
	sota_dbg_stat_tick = 0;
}

static inline u8 sota_fifo_empty(void)
{
	return sota_fifo_rptr == sota_fifo_wptr && sota_fifo_roff == 0;
}

/*EP0 irq: append one frame (offset + payload). 0 = fifo full (PC must retry).*/
static u8 sota_fifo_push(u32 off, const u8 *data, u8 len)
{
	u8 next_w = (u8)((sota_fifo_wptr + 1) % SOTA_FIFO_FRAMES);

	if (next_w == sota_fifo_rptr)
	{
		return 0;
	}
	sota_fifo[sota_fifo_wptr][0] = (u8)(off);
	sota_fifo[sota_fifo_wptr][1] = (u8)(off >> 8);
	sota_fifo[sota_fifo_wptr][2] = (u8)(off >> 16);
	sota_fifo[sota_fifo_wptr][3] = (u8)(off >> 24);
	sota_fifo[sota_fifo_wptr][4] = len;
	memcpy(&sota_fifo[sota_fifo_wptr][5], data, len);
	sota_fifo_wptr = next_w;
	return 1;
}

/*main loop: pop up to max bytes from one frame. *off is the frame's byte
 *offset in the image (constant for the whole frame, even on a partial pop).
 *0 = fifo empty.*/
static u8 sota_fifo_pop(u8 *dst, u8 max, u32 *off)
{
	u8 n;

	if (sota_fifo_empty())
	{
		return 0;
	}
	*off = (u32)sota_fifo[sota_fifo_rptr][0] |
		   ((u32)sota_fifo[sota_fifo_rptr][1] << 8) |
		   ((u32)sota_fifo[sota_fifo_rptr][2] << 16) |
		   ((u32)sota_fifo[sota_fifo_rptr][3] << 24);
	n = sota_fifo[sota_fifo_rptr][4] - sota_fifo_roff;
	if (n > max)
	{
		n = max;
	}
	memcpy(dst, &sota_fifo[sota_fifo_rptr][5] + sota_fifo_roff, n);
	sota_fifo_roff += n;
	if (sota_fifo_roff >= sota_fifo[sota_fifo_rptr][4])
	{
		sota_fifo_roff = 0;
		sota_fifo_rptr = (u8)((sota_fifo_rptr + 1) % SOTA_FIFO_FRAMES);
	}
	return n;
}


/*----------------------------------------------------------------------------
 * bank detection - the one source of truth (see the file header)
 *--------------------------------------------------------------------------*/
extern int main(void);		/*.text, lives in flash at the image offset*/

static u32 sota_running_bank(void)
{
	u32 pc = (u32)&main;	/*flash is mapped at 0x0 -> this IS the bank offset*/
	return (pc < SOTA_BANK_SIZE) ? 0u : (u32)SOTA_BANK_SIZE;
}

/*Sanity check on the detected bank: a valid image header must be there.
 *This is what makes a wrong detection fail closed instead of erasing the
 *running firmware.*/
static u8 sota_bank_header_ok(u32 bank)
{
	u8 m[4];

	flash_read_page(bank + 0x08, 4, m);
	return (m[0] == 'K' && m[1] == 'N' && m[2] == 'L' && m[3] == 'T') ? 1 : 0;
}

/*flash capacity code (0x12 = 256K, 0x13 = 512K, ...) - the second bank only
 *exists when the flash is at least 2 x 128K. flash_read_raw_mid() is inside
 *an "#if 0" block in drivers/B85/flash.c, so use flash_read_mid() (same
 *JEDEC read, plus a GD25LD80C special case that does not affect the
 *capacity byte for our purpose).*/
static u8 sota_flash_capacity_ok(void)
{
	unsigned int mid = (unsigned int)flash_read_mid();
	return (((mid & 0x00ff0000) >> 16) >= 0x12) ? 1 : 0;
}


/*----------------------------------------------------------------------------
 * flash access (main loop only)
 *--------------------------------------------------------------------------*/
static void sota_unlock(void)
{
#if (APP_FLASH_PROTECTION_ENABLE)
	/*the spare bank lives inside FLASH_LOCK_FW_LOW_256K (app.c locks
	 *0x00000-0x3FFFF), so the protection has to come off for the whole
	 *session - the reference USB-OTA code does the same*/
	flash_unlock();
#endif
}

static void sota_lock(void)
{
#if (APP_FLASH_PROTECTION_ENABLE)
	flash_lock(flash_lockBlock_cmd);
#endif
}

/*write one frame at the given byte offset. Returns 0 on a flash error
 *(the caller then fails the session - the running firmware is still untouched).*/
static u8 sota_flash_write(const u8 *buf, u8 len, u32 off)
{
	u8  i;
	u8  n;
	u32 addr;

	/*2026-10-22: the old defensive lazy-erase here is GONE. With the official
	 *flash.c order (busy-wait inside irq_disable) a live-path sector erase
	 *would hold IRQs off for the whole ~30-100ms erase - exactly the blackout
	 *that wedges the USB control endpoint when a SET_REPORT lands inside it
	 *(the 2026-10-21 deadlock). The pre-erase loop is now verified
	 *blank-after-erase, so an un-erased sector here can only mean the erase
	 *itself failed - fail the session instead of writing garbage (fail
	 *closed; the running firmware is untouched).*/
	if (sota_erased < off + len)
	{
		printf("[ux_self_ota] sector for off 0x%04X not erased - aborting\r\n",
			   (int)off);
		return 0;
	}

	for (i = 0; i < len; i = (u8)(i + n))
	{
		addr = sota_ota_bank + off + i;
		n = (u8)(len - i);
		if (n > SOTA_RW_CHUNK)
		{
			n = SOTA_RW_CHUNK;
		}
		flash_write_page(addr, n, (u8 *)(buf + i));
		sleep_us(SOTA_RW_GAP_US);
	}
	/*only advance the contiguous-received counter when this frame lands exactly
	 *at the frontier. A gap (frame lost in flight) leaves sota_recv put; the PC
	 *learns the gap via STATUS and resends from there. A retransmit (off <
	 *sota_recv) is rewritten idempotently and does NOT advance the counter.*/
	if (off == sota_recv)
	{
		sota_recv += len;
	}
	return 1;
}


/*----------------------------------------------------------------------------
 * END verification (main loop)
 *--------------------------------------------------------------------------*/
/*Telink bin CRC32: standard nibble-table CRC32 (poly 0xEDB88320, init
 *0xFFFFFFFF, no final xor) over the image minus its last 4 bytes - exactly
 *what tl_check_fw / flash_fw_check() compute, copied so this module does not
 *have to mutate the shared ota_program_offset global.*/
static const unsigned long sota_crc32_half_tbl[16] = {
	0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
	0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
	0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
	0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

#define SOTA_VERIFY_CHUNK    128

/*Verification scratch buffers are STATIC on purpose: the boot linker script
 *asserts _ram_use_end_ < (__SRAM_SIZE - 600), i.e. the whole C stack is only
 *600 bytes. The reference flash_fw_check() puts 256+512 = 768 bytes on the
 *stack and would blow it - it survives in the SDK only because nothing ever
 *calls it. This module does call its equivalent, so the buffers live in .bss.*/
static u8  sota_vbuf[SOTA_VERIFY_CHUNK];
static u8  sota_vhalf[SOTA_VERIFY_CHUNK << 1];

static u8 sota_verify(void)
{
	u32 sz;
	u32 crc = 0xFFFFFFFF;
	u32 off;
	u8  m[4];
	u32 stored;

	if (sota_recv != sota_total)
	{
		return UX_SOTA_RES_SIZE;
	}

	/*Telink magic at 0x08 (== BOOT_MARK_VALUE 0x4B, the 'K' of "KNLT")*/
	flash_read_page(sota_ota_bank + 0x08, 4, m);
	if (m[0] != 'K' || m[1] != 'N' || m[2] != 'L' || m[3] != 'T')
	{
		return UX_SOTA_RES_MAGIC;
	}

	/*size field at 0x18 must equal the announced size*/
	flash_read_page(sota_ota_bank + 0x18, 4, (u8 *)&sz);
	if (sz != sota_total)
	{
		return UX_SOTA_RES_HDR;
	}

	/*CRC32 over [0, total-4)*/
	for (off = 0; off < (sota_total - 4); off += SOTA_VERIFY_CHUNK)
	{
		u32 n = sota_total - 4 - off;
		u32 j;

		if (n > SOTA_VERIFY_CHUNK)
		{
			n = SOTA_VERIFY_CHUNK;
		}
		flash_read_page(sota_ota_bank + off, n, sota_vbuf);
		for (j = 0; j < n; j++)
		{
			sota_vhalf[j * 2]     = sota_vbuf[j] & 0x0f;
			sota_vhalf[j * 2 + 1] = sota_vbuf[j] >> 4;
		}
		crc = crc32_half_cal(crc, sota_vhalf, (unsigned long *)sota_crc32_half_tbl, (int)(n << 1));
	}
	sota_crc = crc;

	/*stored CRC32 in the last 4 bytes of the image*/
	flash_read_page(sota_ota_bank + sota_total - 4, 4, (u8 *)&stored);
	if (stored != crc)
	{
		return UX_SOTA_RES_CRC;
	}
	return UX_SOTA_RES_OK;
}


/*----------------------------------------------------------------------------
 * COMMIT - the point of no return (main loop)
 *--------------------------------------------------------------------------*/
/*COMMIT = copy the verified new bank OVER the running bank, then reboot.
 *
 *Why overwrite instead of "invalidate old bank mark + reboot": the 825x ROM
 *boots ONLY from 0x00000 - there is NO "old bank invalid -> run 0x20000"
 *fallback. Field proof (12:12 run): writing 0x00 over the 0x00008 'KNLT'
 *mark and rebooting brought the dongle back running the OLD 0x00000 image
 *as if nothing had happened. The reference 8258_dual_mouse USB OTA agrees:
 *it erases and rewrites the RUNNING bank in place (usb_ota_write).
 *
 *Why this whole body is RAM code with IRQs off: the copy destination is
 *the very .text we execute from (vectors at 0x0, .text from 0x8000, the
 *whole image inside sota_total). While the copy runs, ANY fetch from the
 *overwritten range - including BLE/USB irq handlers - would get new bytes.
 *So: this function is _attribute_ram_code_sec_noinline_, IRQs stay off for
 *the whole copy, and only RAM code may be called from inside:
 *  flash_erase_sector / flash_page_program  (ram_code since today,
 *      see flash.c)
 *  sleep_us (timer.c, ram_code) / wd_clear (inline register write)
 *  irq_disable/irq_restore (inline register writes)
 *No printf inside the copy (printf + crc lib live in .text); source bytes
 *are read from the flash memory-mapped view (plain CPU deref, no MSPI
 *transaction in flight) into sota_vbuf, and flash_page_program gets an
 *SRAM buffer on purpose - its flash-addr branch would call the .text
 *memcpy. Reuses sota_vbuf: verify is already done by now.
 *
 *Power-cut window: erase of the first destination sector until the last
 *page write lands (~15-25s for a 71k image). Same exposure as Telink's own
 *in-place OTA; the PC prints a do-not-power-off warning for the whole copy.
 *Watchdog: flash_erase_sector feeds it once per sector, the page loop
 *feeds it per 128B chunk (16 page writes, ~30ms) - far inside the period.*/
_attribute_ram_code_sec_noinline_ static void sota_do_commit(void)
{
	u32 src = sota_ota_bank;
	u32 dst = sota_run_bank;
	unsigned char r = irq_disable();
	u32 off;
	u32 j;

	for (off = 0; off < sota_total; off += SOTA_SECTOR_SIZE)
	{
		u32 sec = sota_total - off;

		if (sec > SOTA_SECTOR_SIZE)
		{
			sec = SOTA_SECTOR_SIZE;
		}

		flash_erase_sector(dst + off);		/*feeds the watchdog*/

		for (j = 0; j < sec; j += SOTA_VERIFY_CHUNK)
		{
			u32 n = sec - j;
			u32 k;

			if (n > SOTA_VERIFY_CHUNK)
			{
				n = SOTA_VERIFY_CHUNK;
			}

			for (k = 0; k < n; k++)
			{
				sota_vbuf[k] = *(volatile unsigned char *)(src + off + j + k);
			}
			for (k = 0; k < n; k += SOTA_RW_CHUNK)
			{
				u8 m = (u8)(n - k);

				if (m > SOTA_RW_CHUNK)
				{
					m = SOTA_RW_CHUNK;
				}
				flash_page_program(dst + off + j + k, m, &sota_vbuf[k]);
				sleep_us(SOTA_RW_GAP_US);
			}
			wd_clear();
		}
	}

	/*the copy is complete - flash content is consistent. But from here to
	 *the reset there must be ZERO XIP calls: every .text address baked into
	 *this OLD image points into the NEW image's unrelated bytes (every real
	 *upgrade shifts the layout), so printf/sota_lock/start_reboot would all
	 *jump into garbage - the 12:49 run died exactly there: copy finished,
	 *CPU lost in the tail, no reset, USB dead for an hour. Reboot via the
	 *reboot register directly (same write lib start_reboot does), RAM-only.
	 *Flash protection state is irrelevant: app init re-locks at boot.*/
	irq_restore(r);
	reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
	while(1);
}


/*1 while a self-upgrade session owns the host link (RECV/VERIFY/COMMIT).
 *The host link mutes its per-frame UART dumps while this is set - at ~250
 *frames/s the dumps alone cost ~10ms of EP0 irq time per frame and starve
 *the BLE stack (the same failure the mouse OTA pipe hit on 2026-10-20).*/
u8 ux_self_ota_busy(void)
{
	return (sota_state == UX_SOTA_ST_RECV  || sota_state == UX_SOTA_ST_VERIFY ||
			sota_state == UX_SOTA_ST_COMMIT) ? 1 : 0;
}


/*----------------------------------------------------------------------------
 * host link command entry (EP0 irq)
 *--------------------------------------------------------------------------*/
int ux_self_ota_host_cmd(const u8 *p, u8 *r)
{
	u8  cmd = p[UX_FT_DL_OFFS_CMD];
	u8  seq = p[UX_FT_DL_OFFS_SEQ];
	const u8 *args = &p[UX_FT_DL_OFFS_ARGS];

	switch (cmd)
	{
		case UX_SOTA_CMD_INFO:
		{
			/*pure query, no side effects: report the bank layout the
			 *session WOULD use. Safe to call at any time.
			 *12-byte budget: run / ota / bootAddr. The max image size
			 *does not fit a 4th u32 and is NOT sent (build constant,
			 *mirrored by the PC as SelfImageMax).*/
			u8 d[12];
			u32 run = sota_running_bank();

			sota_put32(d, UX_SOTA_INFO_OFFS_RUN, run);
			sota_put32(d, UX_SOTA_INFO_OFFS_OTA, run ? 0u : ota_program_bootAddr);
			sota_put32(d, UX_SOTA_INFO_OFFS_BOOT, ota_program_bootAddr);

			sota_send_rsp(cmd, seq, UX_FT_ST_OK, d, sizeof(d));
			return 1;
		}

		case UX_SOTA_CMD_STATUS:
		{
			u8 d[12];

			/*a STATUS poll also proves the PC is alive: the ACK-repair loop
			 *(PC times out on a lost DATA ack, then polls STATUS and resends
			 *from the frontier) must not be killed by the 20s idle watchdog
			 *while it runs. Same spirit as feeding the tick on every DATA
			 *frame, accepted or not.*/
			sota_last_rx_tick = clock_time();
			memset(d, 0, sizeof(d));
			d[UX_SOTA_STS_OFFS_STATE]    = sota_state;
			d[UX_SOTA_STS_OFFS_RESULT]   = sota_result;
			sota_put32(d, UX_SOTA_STS_OFFS_RECV, sota_recv);
			sota_put32(d, UX_SOTA_STS_OFFS_TOT,  sota_total);
			/*no CRC here: 2+4+4+4 > 12 bytes. The computed CRC32 is
			 *reported in the deferred END response instead.*/

			sota_send_rsp(cmd, seq, UX_FT_ST_OK, d, sizeof(d));
			return 1;
		}

		case UX_SOTA_CMD_BEGIN:
		{
			u32 total = (u32)args[0] | ((u32)args[1] << 8) |
						((u32)args[2] << 16) | ((u32)args[3] << 24);
			u32 run;
			u8  d[1];

			if (sota_state == UX_SOTA_ST_RECV || sota_state == UX_SOTA_ST_VERIFY ||
				sota_state == UX_SOTA_ST_COMMIT)
			{
				d[0] = UX_SOTA_BEGIN_BUSY_SESS;
				sota_send_rsp(cmd, seq, UX_FT_ST_BUSY, d, 1);
				return 1;
			}
			if (total < 1024 || total > UX_SOTA_IMAGE_MAX)
			{
				d[0] = UX_SOTA_BEGIN_ERR_SIZE;
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, d, 1);
				return 1;
			}
			if (!sota_flash_capacity_ok())
			{
				d[0] = UX_SOTA_BEGIN_ERR_FLASH;
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, d, 1);
				return 1;
			}
			run = sota_running_bank();
			/*fail closed on EVERY one of these - a wrong target bank would
			 *erase the firmware we are executing from:
			 *  - ota_program_bootAddr unset (no second bank configured)
			 *  - the derived spare bank IS the running bank
			 *  - the running bank has no valid image header (detection wrong)*/
			if (ota_program_bootAddr == 0 ||
				(run ? 0u : ota_program_bootAddr) == run ||
				!sota_bank_header_ok(run))
			{
				d[0] = UX_SOTA_BEGIN_ERR_BANK;
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, d, 1);
				printf("[ux_self_ota] begin refused: bank layout unsafe "
					   "(bootAddr 0x%04X, run 0x%04X)\r\n",
					   (int)ota_program_bootAddr, (int)run);
				return 1;
			}

		sota_session_reset();
		sota_run_bank = run;
		sota_ota_bank = run ? 0u : ota_program_bootAddr;
		sota_total    = total;
		/*defer the ack: pre-erase the whole target span now (the only long
		 *irq_disable in the flow), and answer OK only once it is done. While
		 *we erase, the PC is blocked in its BEGIN Input-report read, so no
		 *SET_REPORT/SETUP is in flight - this keeps the 79ms blackout off
		 *the live DATA path where it would wedge the USB endpoint.*/
		sota_begin_seq     = seq;
		sota_erased        = 0;
		sota_erase_target  = (total + SOTA_SECTOR_SIZE - 1) & ~(SOTA_SECTOR_SIZE - 1);
		sota_preerasing    = 1;
		sota_state         = UX_SOTA_ST_RECV;
		sota_last_rx_tick  = clock_time();

		printf("[ux_self_ota] begin: %d bytes, pre-erasing %d sectors (run 0x%04X -> ota 0x%04X)\r\n",
			   (int)total, (int)(sota_erase_target / SOTA_SECTOR_SIZE),
			   (int)sota_run_bank, (int)sota_ota_bank);
		return 1;	/*ack sent from ux_self_ota_proc after pre-erase*/
		}

		case UX_SOTA_CMD_DATA:
		{
			/*v2 frame: u24 LE offset + len + payload (2026-10-22; the
			 *image max is 124K so the old 4th offset byte was wasted
			 *payload space)*/
			u32 off = (u32)args[UX_SOTA_DATA_OFFS_OFF] |
					  ((u32)args[UX_SOTA_DATA_OFFS_OFF + 1] << 8) |
					  ((u32)args[UX_SOTA_DATA_OFFS_OFF + 2] << 16);
			u8  len = args[UX_SOTA_DATA_OFFS_LEN];

			if (sota_preerasing)
			{
				/*PC sent DATA before the BEGIN ack (it should be blocked on
				 *the read, so this is defensive): tell it to back off, the
				 *pre-erase finishes and the ack follows.*/
				sota_send_rsp(cmd, seq, UX_FT_ST_BUSY, NULL, 0);
				return 1;
			}

			if (len < 1 || len > UX_SOTA_DATA_MAX)
			{
				sota_dbg_rx_bad++;
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				return 1;
			}
			/*idle watchdog food on EVERY data frame - a BUSY retry storm
			 *also proves the PC is alive; only the PC going away aborts*/
			sota_last_rx_tick = clock_time();
			if (sota_state != UX_SOTA_ST_RECV)
			{
				sota_dbg_rx_bad++;
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				return 1;
			}
			if (off + len > sota_total)
			{
				/*frame runs past the announced size: protocol error*/
				sota_dbg_rx_bad++;
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				return 1;
			}
			if (!sota_fifo_push(off, &args[UX_SOTA_DATA_OFFS_DATA], len))
			{
				/*fifo full: the flash writer is behind. Tell the PC to
				 *retry this (idempotent) frame - nothing is lost.*/
				sota_send_rsp(cmd, seq, UX_FT_ST_BUSY, NULL, 0);
				return 1;
			}
			sota_dbg_rx_ok++;
			/*NOTE: sota_recv is NOT advanced here. It only advances in
			 *sota_flash_write() when a frame lands exactly at the current
			 *frontier (off == sota_recv). A retransmit (off < sota_recv) is
			 *rewritten idempotently and a gap (off > sota_recv) is caught
			 *by STATUS and repaired by the PC. This makes a lost ACK
			 *non-fatal and keeps the image byte-exact.*/
			sota_send_rsp(cmd, seq, UX_FT_ST_OK, NULL, 0);
			return 1;
		}

		case UX_SOTA_CMD_END:
		{
			if (sota_state != UX_SOTA_ST_RECV)
			{
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				return 1;
			}
			/*defer: the fifo must drain into flash first, then the whole
			 *image is verified. The response is pushed from
			 *ux_self_ota_proc once that is done.*/
			sota_state    = UX_SOTA_ST_VERIFY;
			sota_pend_cmd = cmd;
			sota_pend_seq = seq;
			sota_pend     = 1;
			printf("[ux_self_ota] end: %d/%d bytes, verifying...\r\n",
				   (int)sota_recv, (int)sota_total);
			return 1;
		}

		case UX_SOTA_CMD_COMMIT:
		{
			if (sota_state != UX_SOTA_ST_READY)
			{
				/*only a verified image may be committed*/
				sota_send_rsp(cmd, seq, UX_FT_ST_ERR, NULL, 0);
				return 1;
			}
			sota_state        = UX_SOTA_ST_COMMIT;
			sota_commit_step  = 0;
			sota_pend_cmd     = cmd;
			sota_pend_seq     = seq;
			sota_pend         = 1;
			printf("[ux_self_ota] commit requested, rebooting...\r\n");
			return 1;
		}

		case UX_SOTA_CMD_VERSION:
		{
			/*pure query: report the version of the RUNNING image. Safe at any
			 *time; the 4-byte header read is the same kind the BEGIN branch
			 *already does here in the EP0 irq (flash is idle - no session).*/
			u8 d[8];

			flash_read_page(sota_running_bank() + 0x02, 4, d);
			sota_put32(d, UX_SOTA_VER_OFFS_BUILD, UX_SOTA_SELF_BUILD);
			sota_send_rsp(cmd, seq, UX_FT_ST_OK, d, sizeof(d));
			return 1;
		}

		default:
			return 0;	/*not a self-OTA command*/
	}

	return 1;
}


/*----------------------------------------------------------------------------
 * module entry (dispatched by user_module_manage.c)
 *--------------------------------------------------------------------------*/
void ux_self_ota_init(void)
{
	sota_session_reset();
	sota_run_bank = 0;
	sota_ota_bank = 0;
	sota_last_rx_tick = 0;
	printf("[ux_self_ota] init (dongle self upgrade, A/B bank)\r\n");
}

void ux_self_ota_proc(void)
{
	u8  buf[SOTA_FIFO_ENTRY];

	/*(0a) pre-erase: the only long irq_disable in the whole flow. Runs while
	 *the PC is blocked in its BEGIN Input-report read, so no USB SETUP is in
	 *flight - the safe window for the 79ms sector erases. One sector per main
	 *loop iteration, yielding between them so USB/BLE keep getting serviced.
	 *The BEGIN ack is released only once the whole span is erased.*/
	if (sota_preerasing)
	{
		sota_last_rx_tick = clock_time();	/*food the idle watchdog*/
		if (sota_erased < sota_erase_target)
		{
			u32 sa = sota_ota_bank + sota_erased;
			u32 t0 = clock_time();

			/*2026-10-22: the protection must come OFF before the FIRST erase,
			 *not after the last one. app_flash_protection_operation() locks the
			 *whole low 256K (== the entire flash on this part) at boot, and an
			 *erase against a protected region is silently IGNORED by the chip:
			 *WIP clears almost instantly, so the old "done in 2000 us" prints
			 *and the same-millisecond "NOT blank after erase" were a protected
			 *erase that never happened. flash_unlock() only reads the status
			 *and sends WRSR (microseconds) - safe here, where the PC is blocked
			 *in its BEGIN Input-report read with no SETUP in flight.*/
			if (!sota_unlocked)
			{
				sota_unlock();
				sota_unlocked = 1;
			}

			flash_erase_sector(sa);
			sota_dbg_erases++;
			/*2026-10-22: VERIFY the sector is really blank before moving on.
			 *The 2026-10-21 flash.c patch had moved the busy-wait out of the
			 *irq_disable window; field evidence: one run REBOOTED mid-upload
			 *(PC STATUS poll saw state=IDLE), the next froze the main loop
			 *(stats print stopped forever). With the official order restored,
			 *flash_erase_sector() only returns after the erase really completed
			 *- but verify anyway: a non-blank sector must fail the session now
			 *instead of surfacing as garbage at END CRC. Expect ~30-100ms per
			 *sector now; the old "done in 2000 us" was the bug talking.*/
			{
				u8 chk[16];
				int k, blank = 1;

				flash_read_page(sa, sizeof(chk), chk);
				for (k = 0; k < (int)sizeof(chk); k++)
				{
					if (chk[k] != 0xFF) { blank = 0; break; }
				}
				if (!blank)
				{
					u8 bs = sota_begin_seq;
					u8 rr = UX_SOTA_RES_ERASE;	/*data[0]=6 in the PC log*/

					printf("[ux_self_ota] pre-erase 0x%04X NOT blank after erase (first=0x%02X) - refusing session\r\n",
						   (int)sa, (int)chk[k]);
					sota_session_reset();
					sota_lock();
					sota_send_rsp(UX_SOTA_CMD_BEGIN, bs, UX_FT_ST_ERR, &rr, 1);
					return;
				}
			}
#if (BIBOO_UX_DEBUG)
			printf("[ux_self_ota] pre-erase 0x%04X done in %u us\r\n",
				   (int)sa, (int)(clock_time() - t0));
#endif
			sota_erased += SOTA_SECTOR_SIZE;
		}
		if (sota_erased >= sota_erase_target)
		{
			sota_preerasing = 0;
			if (!sota_unlocked)
			{
				sota_unlock();
				sota_unlocked = 1;
			}
			sota_send_rsp(UX_SOTA_CMD_BEGIN, sota_begin_seq, UX_FT_ST_OK, NULL, 0);
			printf("[ux_self_ota] begin ack: pre-erase done, %d bytes ready\r\n",
				   (int)sota_total);
		}
		return;	/*PC has not sent DATA yet (still waiting for this ack)*/
	}

#if (BIBOO_UX_DEBUG)
	/*periodic stats while a session is live. This is the line that tells
	 *"PC frames never arrived" (rx_ok stays 0) apart from "dongle never
	 *answered" (rx_ok climbs but htx_drop climbs too / fifo stays full) -
	 *one line per second, main-loop context, UART cost negligible.*/
	if ((sota_state == UX_SOTA_ST_RECV || sota_state == UX_SOTA_ST_VERIFY) &&
		clock_time_exceed(sota_dbg_stat_tick, 1000000))
	{
		sota_dbg_stat_tick = clock_time();
		printf("[ux_self_ota] stat rx_ok=%u rx_bad=%u recv=%u/%u fifo=%u erase=%u htxdrop=%u\r\n",
			   (unsigned)sota_dbg_rx_ok, (unsigned)sota_dbg_rx_bad,
			   (unsigned)sota_recv, (unsigned)sota_total,
			   (unsigned)((u8)(sota_fifo_wptr - sota_fifo_rptr) & (SOTA_FIFO_FRAMES - 1)),
			   (unsigned)sota_dbg_erases, (unsigned)dbg_htx_drop_cnt);
	}
#endif

	/*(0) rx inactivity watchdog: the PC went away mid-upload. The image can
	 *never complete, so drop the session. The partial image stays in the
	 *spare bank - the running firmware is untouched and still boots.*/
	if (sota_last_rx_tick &&
		(sota_state == UX_SOTA_ST_RECV) &&
		clock_time_exceed(sota_last_rx_tick, UX_SOTA_IDLE_TIMEOUT_S * 1000000))
	{
		printf("[ux_self_ota] session aborted: rx idle timeout (%d/%d bytes)\r\n",
			   (int)sota_recv, (int)sota_total);
		sota_session_reset();
		sota_lock();
	}

	/*(1) drain the fifo into flash. The first DATA frame also unlocks the
	 *flash (the spare bank is inside the locked low 256K). The drain must
	 *keep running through VERIFY: END only sets the state, the last frames
	 *are still sitting in the fifo and the image is only complete once they
	 *are on flash.*/
	if ((sota_state == UX_SOTA_ST_RECV || sota_state == UX_SOTA_ST_VERIFY) &&
		!sota_fifo_empty())
	{
		u8 len;

		if (!sota_unlocked)
		{
			sota_unlock();
			sota_unlocked = 1;
		}

		/*bounded work per main-loop iteration: one ≤8B page write
		 *(<2ms irq_disable). The sector erases are done up front at BEGIN
		 *(see sota_preerasing), so the live DATA path never holds IRQs for
		 *the ~79ms that would wedge the USB control endpoint.*/
		{
			u32 off;
			len = sota_fifo_pop(buf, UX_SOTA_DATA_MAX, &off);
			if (len)
			{
				/*buf holds the frame payload (fifo entry =
				 *[off(4)][len(1)][data(8)], pop strips the off+len prefix).
				 *sota_flash_write() writes it at the absolute offset and
				 *only advances sota_recv when off == sota_recv. It returns
				 *0 only on a real flash problem (un-erased sector) - fail
				 *the session instead of letting END CRC find garbage.*/
				if (!sota_flash_write(buf, len, off))
				{
					printf("[ux_self_ota] flash write failed at off 0x%04X, session aborted\r\n",
						   (int)off);
					sota_session_reset();
					sota_lock();
					return;
				}
			}
		}
	}

	/*(2) deferred END response: fifo drained, now verify the whole image*/
	if (sota_pend && sota_state == UX_SOTA_ST_VERIFY)
	{
		u8 res;

		if (!sota_fifo_empty())
		{
			return;	/*still draining*/
		}
		res = sota_verify();
		sota_result = res;
		sota_state  = (res == UX_SOTA_RES_OK) ? UX_SOTA_ST_READY : UX_SOTA_ST_FAIL;

		printf("[ux_self_ota] verify %s (crc 0x%08X, %d bytes)\r\n",
			   res == UX_SOTA_RES_OK ? "OK" : "FAIL", (int)sota_crc, (int)sota_total);

		{
			u8 d[4];
			d[0] = (u8)res;
			d[1] = 0;
			d[2] = 0;
			d[3] = 0;
			sota_send_rsp(sota_pend_cmd, sota_pend_seq,
						  res == UX_SOTA_RES_OK ? UX_FT_ST_OK : UX_FT_ST_ERR, d, 4);
		}
		sota_pend = 0;

		/*verification is over either way: re-lock the flash now. The spare
		 *bank keeps its content (it is only read from here on, and COMMIT
		 *writes a single byte to the OLD bank, which needs no unlock
		 *because that byte is inside the same locked range - see below).*/
		sota_lock();
	}

	/*(3) deferred COMMIT: flush the OK response over USB first, then pull
	 *the trigger. A blind sleep would work too, but polling the tx fifo
	 *keeps the main loop (and the BLE link) alive while we wait.*/
	if (sota_pend && sota_state == UX_SOTA_ST_COMMIT)
	{
		if (sota_commit_step == 0)
		{
			u8 d[4];
			d[0] = (u8)UX_SOTA_RES_OK;
			d[1] = 0;
			d[2] = 0;
			d[3] = 0;
			sota_send_rsp(sota_pend_cmd, sota_pend_seq, UX_FT_ST_OK, d, 4);
			/*2026-10-22 THE root cause of every "COMMIT ok but never reboots"
			 *run: clearing sota_pend here killed the OUTER gate above, so
			 *steps 1/2 never ran - the copy never started and the dongle just
			 *sat there alive with USB up (UART stopped right after "commit
			 *requested", four field runs in a row). sota_pend must stay 1 to
			 *keep this block alive until sota_do_commit() resets the chip; no
			 *cleanup is needed after that.*/
			sota_commit_step = 1;
			sota_commit_tick  = clock_time();
		}
		else if (sota_commit_step == 1)
		{
			/*wait until the response really left (or 1s elapsed)*/
			if (ux_host_link_tx_idle() ||
				clock_time_exceed(sota_commit_tick, 1000000))
			{
				sota_commit_step = 2;
			}
		}
		else
		{
#if (APP_FLASH_PROTECTION_ENABLE)
			flash_unlock();
#endif
			printf("[ux_self_ota] commit: overwriting running bank 0x%04X - do not power off (~20s)\r\n",
				   (int)sota_run_bank);
			sota_do_commit();
		}
	}
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_SELF_OTA */
