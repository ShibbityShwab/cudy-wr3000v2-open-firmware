/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hcc_core.h - host-side HCC protocol core for the Hi5622V100 clean-room
 * PCIe/ETE transport, on the CORRECTED boot protocol (GP8, release-only).
 *
 * ORIGINAL code by the wifi-hcc-core lane (shared task task-92, 2026-11-24),
 * written from the project's decoded contract.  No vendor source, object
 * code, firmware byte, calibration value or secret is reproduced here; every
 * constant is a derived address/value with an evidence marker.
 *
 * What this is
 * ------------
 * The portable (no kernel dependency) protocol state machine that the
 * hi5622_pciet_hcc module should embed.  It owns the post-release vendor
 * sequence end-to-end:
 *
 *   note_release() -> BOOT (handshake page CA 0x1bf000 +8 == 2, LATCHED once)
 *     -> WAIT_PLAT_READY   (HCC chip-group-4 id1, <= 2000 ms)
 *     -> WAIT_DEVICE_READY (HCC chip-group-4 id2, <= 20000 ms, payload copy)
 *     -> READY_TXQ_SYNC    (first H2D: group-0 id-1 TXQ config sync)
 *     -> ACTIVE            (PM state messages, heartbeats, DR/data service)
 *
 * plus the pieces the sequence needs:
 *   - the vendor-order D2H mailbox service (ack -> read -> drain -> re-arm ->
 *     dispatch lowest-id-first; handler table ids 1..7, ids 0/8/9/10 dropped,
 *     id > 10 stops the word);
 *   - the H2D mailbox coalescing state machine (pending/in-flight bitmaps,
 *     out[0] publish polled, out[2] doorbell RMW; out[5] NEVER written);
 *   - the SR commit path (packed producer index + node word1 encoding, then
 *     one mailbox id-3 announce per coalesced batch);
 *   - the frame contract (build/validate the 12-byte HCC header + 0x5a5a tag)
 *     for both the device->host ready pair and the host->device frames;
 *   - the corrected CECE policy: never before the boot latch, then one
 *     evidence-driven 32-bit write per observed handler entry;
 *   - heartbeat monitoring (10 s one-shot, restarted by chip-group-4 id4) and
 *     the PM/state machine (id4/id5 + the PM-gated H2D id-1 sleep request).
 *
 * Contract sources (committed lane reports under opensource/docs/incoming/):
 *   wifi-host-replica/REPORT.md sec 2 (vendor post-release order), sec 2.1
 *     (complete host->device write list), sec 3.1-3.2 (mailbox ids 1/3/5,
 *     pcie_msg_send / pcie_msg_send_irq ordering), sec 4.2 (pcie_msg_handle
 *     ack -> read -> drain -> re-arm -> dispatch), sec 4.3 (registered D2H
 *     handler table {1,2,3,4,5,6,7}; ids 0/8/9/10 dropped; register rejects
 *     id > 10), sec 6.3 (ready state), sec 6.6 (PM + 10 s heartbeat), sec 7
 *     (200/2000 jiffy waits, HZ=100) and SPEC.md sec 1-4;
 *   wifi-hcc-service/REPORT.md sec 3-6 (the service so far, CECE policy,
 *     ready observables) + src/hi5622_hcc_service.h (constants);
 *   wifi-d2h-direct/REPORT.md sec 0/2/5 (DIRECT device group-A receive path;
 *     ready pair readable without the ETE engine) and the resolution to feed
 *     its decoded g4 id1/id2 frames into the ready state machine;
 *   wifi-h2d-catalog/REPORT.md sec 4-6 (H2D dispatch tables: group 0 + group
 *     2 only; group 4 is device->host; mailbox id 5 is a firmware BUG trap;
 *     group-0 id-1 = post-DEVICE-READY TXQ config sync with a dynamic
 *     payload - NEVER synthesize it);
 *   wifi-ete-frames/ (12-byte header wire form, tag 0x5a5a, node word1),
 *     wifi-ete-init/, wifi-ete-engine/ (host ETE surface is exhausted),
 *     wifi-rom-dump/ (corrected boot protocol: release 0x5a5a only; the ROM
 *     stage owns 0xCEED until page+8==2, the image handlers spin for 0xCECE),
 *     wifi-hcc-link/ (the vendor sends nothing between release and ready).
 *
 * Address conventions (project-wide, unchanged, and implemented here as
 * hcc_bar0_io()/hcc_bar0_fw()):
 *   device IO CA   -> host BAR0 = 0x3b8000 + (CA - 0x40000000)   [region 3]
 *   firmware CPU   -> host BAR0 = 0x6b8000 + CPU                  [region 5]
 *
 * NOTE (fix list, see INTEGRATION.md): the inherited transport macro
 * HI5622_BAR0_IO() masks the CA with 0xfffff, which is only correct for CAs
 * below 0x40100000; the D2H ack/rearm registers (out[3]/out[4] at CA
 * 0x40101438/0x40101414) map to BAR0 +0x4b9438/+0x4b9414 (21-bit region
 * offset), not +0x3b9438/+0x3b9414.  This core uses the region formula and
 * range-checks every access.
 *
 * Build: portable C99 (host) and kernel (__KERNEL__) - see tools/build.sh.
 */
#ifndef HCC_CORE_H
#define HCC_CORE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#endif

/* ---------------------------------------------------------------------
 * 0. Portable basic types (same layout in host and kernel builds)
 * ------------------------------------------------------------------- */
typedef unsigned char      hcc_u8;
typedef unsigned short     hcc_u16;
typedef unsigned int       hcc_u32;
typedef unsigned long long hcc_u64;
typedef signed int         hcc_s32;

/* ---------------------------------------------------------------------
 * 1. Address map (see the header comment; every access is range-checked)
 * ------------------------------------------------------------------- */
#define HCC_REGION_IO_CA_LO	0x40000000u	/* region 3 device-CA window [V] */
#define HCC_REGION_IO_CA_HI	0x4011ffffu	/* region 3 end [V] */
#define HCC_REGION_IO_BAR0	0x003b8000u	/* region 3 BAR0 base [V] */

#define HCC_FW_RAM_BAR0		0x006b8000u	/* region 5 BAR0 base [V] */
#define HCC_FW_CPU_LO		0x00000000u	/* image RAM window [V] */
#define HCC_FW_CPU_HI		0x00217fffu	/* region 5 covers CPU 0..0x217fff [V] */

/* ---------------------------------------------------------------------
 * 2. Mailbox registers (the vendor's six "out" words)
 *    [V] host-replica REPORT sec 4.1 / transport REPORT address table.
 *    out[5] (CA 0x400392f0) is NEVER written by this core (hard rule).
 * ------------------------------------------------------------------- */
#define HCC_CA_H2D_PENDING	0x40039010u	/* out[0]: H2D pending mask (host write) [V] */
#define HCC_CA_D2H_BITMAP	0x40039014u	/* out[1]: D2H pending bitmap (device write) [V] */
#define HCC_CA_H2D_DOORBELL	0x400392d4u	/* out[2]: H2D doorbell, host ORs bit0 [V] */
#define HCC_CA_D2H_ACK		0x40101438u	/* out[3]: D2H ack, host writes 1 [V] */
#define HCC_CA_D2H_REARM	0x40101414u	/* out[4]: D2H re-arm, host writes 1 [V] */
#define HCC_CA_NEVER_WRITE	0x400392f0u	/* out[5]: NEVER write [V] hard rule */
#define HCC_CA_D2H_DOORBELL	0x40101434u	/* device->host doorbell, host read-only [V] */

/* ---------------------------------------------------------------------
 * 3. Boot protocol (corrected, GP8 2026-11-24 10:40 [V*]; rom-dump [V])
 *
 *   release 0x5a5a -> CA 0x40000108 is the TRANSPORT's write; this core only
 *   records it (hcc_core_note_release) and never writes that CA.
 *   CA 0x4000010c is dual-use: ROM boot stage needs 0xCEED to survive until
 *   CPU1's S1 completes (handshake page +8 == 2); the image service/exception
 *   handler CPU 0xC6E14 zeroes it and spins for 32-bit 0x0000CECE.
 * ------------------------------------------------------------------- */
#define HCC_CA_CPU_RELEASE	0x40000108u	/* transport-owned; core never writes [V] */
#define HCC_CA_GATE		0x4000010cu	/* dual-use boot/image gate [V] */
#define HCC_GATE_CECE		0x0000ceceu	/* image handler spin value [V] */
#define HCC_GATE_CEED		0x0000ceedu	/* ROM S1 gate value [V] */

#define HCC_CPU_HANDSHAKE_PAGE	0x001bf000u	/* CPU page; host = FW_RAM_BAR0 + CPU [V] */
#define HCC_HS_W0_GOLDEN	0x86807fefu	/* GP8 golden word 0 [V*] */
#define HCC_HS_W1_GOLDEN	0x00000007u	/* GP8 golden word 1 [V*] */
#define HCC_HS_W3_GOLDEN	0x0000abcdu	/* GP8 golden word 3 [V*] */
#define HCC_HS_ACK_GOLDEN	2u		/* +0x8 == 2 is the S1 completion gate [V] */

#define HCC_CPU_BOOT_STATE	0x00118b60u	/* boot-task state; 1 = ran [V*] */
#define HCC_CPU_EXCP_MASK	0x00040008u	/* halfword exception-seen bitmap [V] */
#define HCC_CPU_VECTOR_FRAME	0x0004000cu	/* 17-word vector context frame [V] */
#define HCC_VECTOR_WORDS	17

/* ---------------------------------------------------------------------
 * 4. Frame contract (12-byte header; ete-frames REPORT sec 1.3/3.2 [V])
 * ------------------------------------------------------------------- */
#define HCC_FRAME_HDR		12u
#define HCC_FRAME_TAG		0x5a5au		/* transport tag at +0xa [V] */
#define HCC_FRAME_TAG_OFF	0x0au
#define HCC_FRAME_LEN_OFF	0x04u		/* u16 total = payload + 12 [V] */
#define HCC_FRAME_ID_OFF	0x06u		/* u16 message id [V] */
#define HCC_FRAME_DST_OFF	0x09u		/* dst core [V] */
#define HCC_FRAME_MAX_PAYLOAD	0x674u		/* hcc_msg_alloc bound [V] */
#define HCC_FRAME_MAX_TOTAL	(HCC_FRAME_MAX_PAYLOAD + HCC_FRAME_HDR)

/* HCC groups (h2d-catalog sec 4 / hcc-re sec 5.5 [V]) */
#define HCC_GROUP_PLAT		4u		/* device->host boot/ready group [V] */
#define HCC_GROUP_H2D_CFG	0u		/* H2D receive table exists [V] */
#define HCC_GROUP_H2D_ALT	2u		/* H2D receive table exists [V] */

/* device->host chip-group-4 ids (hcc-re sec 5.5 / d2h-direct sec 5.1 [V]) */
#define HCC_G4_ID_OAM_LOG	0u		/* oam_rx_post_action device log [V] */
#define HCC_G4_ID_PLAT_READY	1u		/* device_plat_ready_msg_process [V] */
#define HCC_G4_ID_DEV_READY	2u		/* host_ready_msg_process ("DEVICE READY") [V] */
#define HCC_G4_ID_EXCP_INFO	3u		/* exception_info_msg_process [V] */
#define HCC_G4_ID_HEARTBEAT	4u		/* heartbeat_msg_process [V] */

/* ready-pair wire constants (ete-frames sec 2 / host-replica sec 4.3 [V]) */
#define HCC_G4_ID1_TOTAL	0x0cu		/* g4 id1 = 12-byte header only [V] */
#define HCC_G4_ID2_TOTAL_MAX	111u		/* vendor check (total-13) <= 98 [V] */
#define HCC_G4_ID2_TOTAL_MIN	13u		/* u16 underflow guard [I] */
#define HCC_READ_MSG_LEN	100u		/* per-core vendor read_msg record [V] */
#define HCC_READ_MSG_CORES	2u		/* core 0 only on this board [V*] */

/* H2D mailbox ids (host-replica sec 3.1 [V]; h2d-catalog sec 3-4 [V]) */
#define HCC_H2D_ID_PM_SLEEP	1u		/* wlan_pm_sleep_work -> bal_msg_send(0,1) [V] */
#define HCC_H2D_ID_ETE_ANNOUNCE	3u		/* after every SR commit [V] */
#define HCC_H2D_ID_BUGON	5u		/* firmware BUG() trap: NEVER post [V] */
#define HCC_H2D_TXQ_SYNC_GROUP	HCC_GROUP_H2D_CFG
#define HCC_H2D_TXQ_SYNC_ID	1u		/* first post-ready H2D (TXQ table sync) [V] */

/* D2H dispatch (host-replica sec 4.2/4.3 [V]) */
#define HCC_D2H_MAX_ID		10u		/* register rejects id > 10; loop stops > 10 [V] */
#define HCC_D2H_DR_CHANNELS	4u
#define HCC_SR_CHANNELS		3u
#define HCC_DR_CHANNELS		4u
#define HCC_RING_DEPTH_DEFAULT	32u		/* channel depth-1 = 0x1f [V] */
#define HCC_IDX_LOW_MASK	0x3ffu		/* packed index: low 10 bits [V] */
#define HCC_IDX_PHASE_BIT	0x400u		/* packed index: phase bit [V] */
#define HCC_SR_NODE_FLAGS	0x6d2bu		/* SR node word1 low 16 [V] */
#define HCC_DMA_WIN_LO		0x80000000u	/* ETE host-DRAM identity window [V*] */
#define HCC_DMA_WIN_HI		0x8fffffffu

/* ---------------------------------------------------------------------
 * 5. Timings (vendor literals; HZ=100 in both trees [V])
 * ------------------------------------------------------------------- */
#define HCC_PLAT_READY_MS	2000u		/* 200 jiffies [V] */
#define HCC_DEV_READY_MS	20000u		/* 2000 jiffies [V] */
#define HCC_PM_SLEEP_ACK_MS	500u		/* 50 jiffies [V] */
#define HCC_HEARTBEAT_MS	10000u		/* ktime 0x2540be400 ns [V] */
#define HCC_MBOX_CLEAR_POLL_MAX	0xfffeu	/* pcie_msg_wait_for_clr bound [V] */
#define HCC_BOOT_TIMEOUT_MS	5000u		/* GP8: page valid at 0.2 s [V*] */

/* ---------------------------------------------------------------------
 * 6. Small pure helpers (exposed for the selftest and for integration)
 * ------------------------------------------------------------------- */
/* device IO CA -> host BAR0 offset; 0 if the CA is outside region 3. */
hcc_u32 hcc_bar0_io(hcc_u32 ca);
/* firmware CPU address -> host BAR0 offset; 0 if outside region 5. */
hcc_u32 hcc_bar0_fw(hcc_u32 cpu);
/* packed ring index arithmetic (vendor pcie_ete_ring_ptr_plus/get_rls_num) */
hcc_u32 hcc_idx_plus(hcc_u32 idx, hcc_u32 depth);
hcc_u32 hcc_idx_count(hcc_u32 from, hcc_u32 to, hcc_u32 depth);
hcc_u32 hcc_idx_slot(hcc_u32 idx);
/* SR node word1 = (total << 16) | 0x6d2b [V] */
hcc_u32 hcc_sr_node_word1(hcc_u32 total);
/* whether a CA is in the write allowlist of this core (tests/integration) */
bool hcc_ca_writable(hcc_u32 ca);

/* ---------------------------------------------------------------------
 * 7. Types
 * ------------------------------------------------------------------- */
enum hcc_phase {
	HCC_PHASE_IDLE = 0,		/* before note_release()			*/
	HCC_PHASE_BOOT,			/* released; waiting for page+8 == 2		*/
	HCC_PHASE_WAIT_PLAT_READY,	/* g4 id1 wait, <= 2.0 s			*/
	HCC_PHASE_WAIT_DEVICE_READY,	/* g4 id2 wait, <= 20.0 s			*/
	HCC_PHASE_READY_TXQ_SYNC,	/* ready; first H2D group-0 id-1 pending	*/
	HCC_PHASE_ACTIVE,		/* TXQ sync committed; steady-state service	*/
	HCC_PHASE_FAILED,		/* timeout; mailbox service continues		*/
};

enum hcc_fail_reason {
	HCC_FAIL_NONE = 0,
	HCC_FAIL_BOOT_TIMEOUT,
	HCC_FAIL_PLAT_READY_TIMEOUT,
	HCC_FAIL_DEVICE_READY_TIMEOUT,
};

/* frame origin: the ready pair may arrive through two independent paths */
enum hcc_src {
	HCC_SRC_ENGINE = 0,	/* engine-copied host group-B DR path (legacy)	*/
	HCC_SRC_DIRECT = 1,	/* direct device group-A ring reader (works)	*/
};

enum hcc_cece_mode {
	HCC_CECE_MODE_EVIDENCE = 0,	/* evidence-driven post-boot writes (default) */
	HCC_CECE_MODE_OFF = 1,		/* negative control: never write		*/
	HCC_CECE_MODE_KICK = 2,		/* manual bounded kick enabled (<= 3/load)	*/
};

enum hcc_event {
	HCC_EVENT_NONE = 0,
	HCC_EVENT_BOOT_LATCHED,		/* data = 1 if all golden words matched	*/
	HCC_EVENT_BOOT_REPAIRED,	/* data = 1 (CEED re-asserted once)	*/
	HCC_EVENT_TIMEOUT,		/* data = enum hcc_fail_reason		*/
	HCC_EVENT_D2H_WORD,		/* data = serviced bitmap			*/
	HCC_EVENT_D2H_DISPATCH,		/* data = id					*/
	HCC_EVENT_D2H_DROPPED,		/* data = id with no handler			*/
	HCC_EVENT_VECTOR_CAPTURE,	/* data = exception-seen mask			*/
	HCC_EVENT_CECE_WRITE,		/* data = 0x0000cece				*/
	HCC_EVENT_CECE_REFUSED,		/* data = 1 boot, 2 mode, 3 kick limit		*/
	HCC_EVENT_PLAT_READY,		/* data = source enum				*/
	HCC_EVENT_DEVICE_READY,		/* data = source enum				*/
	HCC_EVENT_TXQ_SYNC,		/* data = 1 (READY -> ACTIVE transition)	*/
	HCC_EVENT_HEARTBEAT,		/* data = heartbeat count			*/
	HCC_EVENT_HEARTBEAT_TIMEOUT,	/* data = timeout count				*/
	HCC_EVENT_PM_STATE,		/* data = 4 (disallow) / 5 (wake ok)		*/
	HCC_EVENT_PM_SLEEP_TIMEOUT,	/* data = timeout count				*/
	HCC_EVENT_H2D_SENT,		/* data = pending mask published to out[0]	*/
	HCC_EVENT_H2D_COMPLETE,		/* data = mask consumed by the device		*/
	HCC_EVENT_H2D_TIMEOUT,		/* data = stuck mask				*/
	HCC_EVENT_H2D_REFUSED,		/* data = refused mailbox id			*/
	HCC_EVENT_FRAME_INVALID,	/* data = reason code				*/
	HCC_EVENT_FRAME_UNKNOWN,	/* data = (group << 8) | id			*/
};

typedef void (*hcc_event_cb)(void *priv, enum hcc_event ev, hcc_u32 data);

/* Byte-level IO: offsets are BAR0-relative; the core maps CAs itself. */
struct hcc_io_ops {
	void *priv;
	hcc_u32 (*read32)(void *priv, hcc_u32 bar0_off);
	void  (*write32)(void *priv, hcc_u32 bar0_off, hcc_u32 val);
};

/* Frame decode result (little-endian fields resolved). */
struct hcc_frame {
	hcc_u8	group;		/* buf[0] & 0x0f				*/
	hcc_u8	core;		/* buf[1] >> 4 (chip/core selector)		*/
	hcc_u8	alloc;		/* buf[1] & 0x0f (alloc nibble, vendor 0x1)	*/
	hcc_u8	field_a;	/* buf[2] (vap)					*/
	hcc_u8	field_b;	/* buf[3] (chip id)				*/
	hcc_u8	field_c;	/* buf[8]					*/
	hcc_u8	dst;		/* buf[9] (dst core)				*/
	hcc_u16	total;		/* u16 at +4 (payload + 12)			*/
	hcc_u16	id;		/* u16 at +6					*/
	hcc_u16	tag;		/* u16 at +0xa (must be 0x5a5a)			*/
};

struct hcc_core_config {
	/* timings (ms); 0 selects the default */
	hcc_u32 plat_ready_ms;
	hcc_u32 dev_ready_ms;
	hcc_u32 boot_timeout_ms;
	hcc_u32 pm_sleep_ack_ms;
	hcc_u32 heartbeat_ms;
	hcc_u32 mbox_clear_timeout_ms;	/* polled substitute for the vendor spin	*/
	hcc_u32 mbox_clear_poll_max;	/* vendor bound 0xfffe				*/

	/* policy switches */
	bool	pm_enabled;		/* enable H2D id-1 PM sleep requests		*/
	bool	heartbeat_armed;	/* vendor boot path leaves this OFF [V]		*/
	bool	allow_ceed_repair;	/* optional one-shot CEED re-assert [I]		*/
	bool	txq_sync_required;	/* INFO only: keep tracking the first H2D	*/
};

/* maximum payload kept from the g4 id2 ("DEVICE READY") frame */
#define READ_PAYLOAD_MAX	99u

/* One SR channel mirror (the transport owns the node array and producer
 * register write; the core owns the protocol encoding/arithmetic). */
struct hcc_sr_ring {
	hcc_u32	producer;	/* packed producer index (host mirror)		*/
	hcc_u32	depth;		/* channel depth (32)				*/
	hcc_u32	nodes;		/* nodes committed while armed			*/
	hcc_u32	last_slot;	/* node array slot of the last commit		*/
	hcc_u32	last_devva;	/* last node word0				*/
	hcc_u32	last_word1;	/* last node word1 = (total<<16)|0x6d2b		*/
	hcc_u32	last_group;
	hcc_u32	last_id;
	hcc_u32	last_total;
};

struct hcc_core {
	struct hcc_io_ops	io;
	struct hcc_core_config	cfg;
	hcc_event_cb		on_event;
	void			*on_event_priv;

	hcc_u64	now_ms;
	hcc_u64	ticks;

	/* boot protocol */
	bool	released;
	bool	boot_latched;		/* LATCHED: page+8==2 seen once [fix]	*/
	hcc_u64	boot_latched_ms;
	hcc_u64	boot_deadline_ms;
	hcc_u32	handshake[4];
	bool	handshake_golden;
	bool	handshake_seen_valid;	/* any page+8==2 observation		*/
	hcc_u32	page_lost_after_latch;	/* page overwritten post-latch [fix]	*/
	hcc_u32	ceed_repairs;
	hcc_u32	boot_checks;
	bool	exception_mask_valid;
	hcc_u32	exception_mask;
	hcc_u32	exception_mask_changes;

	/* ready state machine */
	enum hcc_phase	 phase;
	enum hcc_fail_reason fail_reason;
	hcc_u64	deadline_ms;

	bool	plat_ready;
	bool	dev_ready;
	hcc_u32	ready_map;		/* vendor ready_map bit per core		*/
	hcc_u32	plat_ready_count;
	hcc_u32	dev_ready_count;
	hcc_u32	id1_bad_core;
	hcc_u32	id1_bad_total;
	hcc_u32	id2_bad_core;
	hcc_u32	id2_bad_len;
	hcc_u16	id2_plen;
	hcc_u8	id2_payload[READ_PAYLOAD_MAX];
	hcc_u8	read_msg[HCC_READ_MSG_CORES][HCC_READ_MSG_LEN];
	bool	txq_sync_submitted;
	hcc_u32	txq_sync_count;

	/* D2H mailbox */
	hcc_u64	d2h_words;
	hcc_u32	d2h_last_bitmap;
	hcc_u32	d2h_coalesced_bits;
	hcc_u32	d2h_id_count[HCC_D2H_MAX_ID + 1];
	hcc_u32	d2h_dropped;
	hcc_u32	d2h_stop_gt10;
	hcc_u32	d2h_need_dr_drain;	/* id 3/6/7 observed			*/
	hcc_u32	d2h_wake_requests;	/* id 6/7: wake the TX/service thread	*/
	hcc_u32	d2h_hist[16];
	hcc_u32	d2h_hist_pos;
	bool	excp_announce;		/* out[1] bit2 observed (CECE evidence)	*/
	hcc_u32	vector_mask;
	hcc_u32	vector_frame[HCC_VECTOR_WORDS];
	hcc_u32	vector_capture_count;

	/* CECE gate policy */
	enum hcc_cece_mode cece_mode;
	bool	cece_pending;
	hcc_u32	cece_writes;
	hcc_u32	cece_blocked_boot;
	hcc_u32	cece_blocked_mode;
	hcc_u32	cece_evidence_events;
	hcc_u32	cece_kicks;
	bool	ev_bit2;
	bool	ev_handler;
	bool	ev_mask;

	/* H2D mailbox coalescing */
	hcc_u32	mbox_pending;	/* accumulated bits not yet published		*/
	hcc_u32	mbox_inflight;	/* mask published to out[0], awaiting clear	*/
	hcc_u64	mbox_publish_ms;
	hcc_u32	mbox_publishes;
	hcc_u32	mbox_completions;
	hcc_u32	mbox_coalesced_batches;
	hcc_u32	mbox_timeouts;
	bool	h2d_lane_closed;
	hcc_u32	h2d_announces;
	hcc_u32	h2d_pm_requests;
	hcc_u32	h2d_refused;
	hcc_u32	last_out0;		/* last out[0] read (telemetry)		*/
	hcc_u32	poll_reads;

	/* heartbeat monitor */
	bool	hb_armed;
	hcc_u64	hb_deadline_ms;
	hcc_u32	hb_count;
	hcc_u32	hb_timeouts;

	/* PM state machine */
	bool	pm_sleep_disallow;
	bool	pm_wake_ok;
	bool	pm_sleep_pending;
	hcc_u64	pm_sleep_deadline_ms;
	hcc_u32	pm_sleep_timeouts;
	hcc_u32	pm_state_changes;
	hcc_u32	pm_sleep_aborts;

	/* SR protocol mirror */
	struct hcc_sr_ring sr[HCC_SR_CHANNELS];

	/* frame/telemetry */
	hcc_u32	frames_seen;
	hcc_u32	frames_invalid;
	hcc_u32	frames_unknown;
	hcc_u32	g4_excp_info;		/* chip-group-4 id3 seen (record only) */
	hcc_u32	g4_oam_log;		/* chip-group-4 id0 device log frames */
	hcc_u32	g3_frames;		/* chip-group-3 data/event frames */
	hcc_u32	g3_temp_state;		/* chip-group-3 id 0x12 temp-state frames */
	struct hcc_frame last_frame;

	/* write safety census */
	hcc_u64	writes;
	hcc_u32	bad_writes;		/* refused writes (must stay 0)		*/
	hcc_u32	bad_reads;		/* out-of-range read requests		*/
	hcc_u32	last_write_ca;
	hcc_u32	last_write_val;
};

/* ---------------------------------------------------------------------
 * 8. API
 * ------------------------------------------------------------------- */
void hcc_core_config_default(struct hcc_core_config *cfg);

void hcc_core_init(struct hcc_core *c, const struct hcc_io_ops *io,
		   const struct hcc_core_config *cfg,
		   hcc_event_cb on_event, void *on_event_priv);

/* Record that the transport performed the 0x5a5a release (core never writes
 * CA 0x40000108).  Arms the boot watch and the boot timeout. */
void hcc_core_note_release(struct hcc_core *c, hcc_u64 now_ms);

/* One periodic service pass (5 ms cadence recommended): boot latch + exception
 * watch + D2H mailbox + ready deadlines + H2D mailbox + heartbeat + PM +
 * evidence-driven CECE.  Returns the number of serviced events. */
int hcc_core_tick(struct hcc_core *c, hcc_u64 now_ms);

/* Individual stages (the integration may also call them directly). */
int hcc_core_boot_check(struct hcc_core *c, hcc_u64 now_ms);
int hcc_core_exception_watch(struct hcc_core *c);
int hcc_core_d2h_poll(struct hcc_core *c, hcc_u64 now_ms);
int hcc_core_mbox_tick(struct hcc_core *c, hcc_u64 now_ms);
int hcc_core_cece_service(struct hcc_core *c);

/* Deliver one decoded device->host frame (engine path OR direct-read path). */
int hcc_core_deliver_hdr(struct hcc_core *c, enum hcc_src src,
			 const hcc_u8 *hdr, const hcc_u8 *payload,
			 hcc_u32 payload_avail, hcc_u32 wire_len,
			 hcc_u64 now_ms);
int hcc_core_deliver_frame(struct hcc_core *c, enum hcc_src src,
			   const hcc_u8 *frame, hcc_u32 len, hcc_u64 now_ms);

/* Frame contract. */
int hcc_core_frame_build(struct hcc_core *c, hcc_u8 *buf, hcc_u32 cap,
			 hcc_u8 group, hcc_u16 id, hcc_u8 dst,
			 const hcc_u8 *payload, hcc_u32 plen, hcc_u32 *total);
int hcc_core_frame_decode(const hcc_u8 *buf, hcc_u32 len, struct hcc_frame *f);

/* H2D: SR commit + mailbox announce/uncoalescing + TXQ-sync transition. */
int hcc_core_sr_commit(struct hcc_core *c, hcc_u8 chan, hcc_u32 devva,
		       hcc_u8 group, hcc_u16 id, hcc_u32 total, hcc_u64 now_ms);
int hcc_core_mbox_send(struct hcc_core *c, hcc_u8 id, hcc_u64 now_ms);

/* The first post-ready H2D (group-0 id-1 TXQ config sync): the vendor's
 * payload is dynamic and NOT reconstructed - the caller supplies it. */
int hcc_core_txq_sync_build(struct hcc_core *c, hcc_u8 *buf, hcc_u32 cap,
			    const hcc_u8 *payload, hcc_u32 plen,
			    hcc_u32 *total);

/* Corrected CECE policy: evidence-driven, post-boot only, bounded kick. */
int hcc_core_cece_kick(struct hcc_core *c);

/* Heartbeat (10 s one-shot; g4 id4 restarts it while armed). */
int hcc_core_heartbeat_arm(struct hcc_core *c, hcc_u64 now_ms);
void hcc_core_heartbeat_disarm(struct hcc_core *c);

/* PM: host-initiated sleep request is H2D mailbox id 1 (vendor: 50-jiffy
 * sleep-ack wait).  Disabled by default (vendor PM layer is not on the boot
 * path; wifi-host-replica sec 6.6). */
int hcc_core_pm_request_sleep(struct hcc_core *c, hcc_u64 now_ms);

/* Human-readable one-line status for sysfs / dmesg. */
int hcc_core_status(const struct hcc_core *c, char *buf, hcc_u32 len);

const char *hcc_phase_name(enum hcc_phase p);
const char *hcc_event_name(enum hcc_event e);

#endif /* HCC_CORE_H */
