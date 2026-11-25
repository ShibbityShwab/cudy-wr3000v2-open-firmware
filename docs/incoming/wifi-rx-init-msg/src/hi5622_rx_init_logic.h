/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rx_init_logic.h - portable model of the candidate firmware RX-init
 * config message (Hi5622V100 clean-room driver, lane rx-init-decode/task-170).
 *
 * ORIGINAL clean-room code derived from the committed decode in REPORT.md:
 * a group-0 / id-1 HCC config event with u16 key 0x0004, payload len 4 and a
 * u32 data value.  The firmware's key-table A dispatches it to worker
 * CA 0x811CD, which writes data[0] to ctx[+0x190][+0x52] and needs the module
 * context the ordered ctx_create (key 0x0064) created.
 *
 * Pure C, no IO, no kernel/libc dependency beyond <string.h>; compiles as a
 * userspace unit (tools/rx_init_selftest.c) and inside the kernel module
 * (define __KERNEL__).  Fail-closed by construction: the key allowlist has one
 * entry, every gate defaults to "refuse", and the read-back model distinguishes
 * "consumed" from "timeout" without ever granting a retry.
 */
#ifndef HI5622_RX_INIT_LOGIC_H
#define HI5622_RX_INIT_LOGIC_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

/* Wire constants (REPORT.md sec 5.2; geometry cross-checked against the
 * phase20/38 captured g0 id1 key-0xD8 frame: total = 28 + data_len). */
#define HI5622_RX_INIT_KEY_DEFAULT	0x0004u
#define HI5622_RX_INIT_VAL_DEFAULT	1u
#define HI5622_RX_INIT_LEN		4u
#define HI5622_RX_INIT_TOTAL		0x20u	/* 12 hdr + 8 token + 2 key + 2 len + 4 data */
#define HI5622_RX_INIT_MAX		0x20u
#define HI5622_RX_INIT_HDR_LEN		12u
#define HI5622_RX_INIT_TAG		0x5a5au
#define HI5622_RX_INIT_MODULE		4u	/* hdr[3] = fb = created module */

struct hi5622_rx_init_frame {
	u8	wire[HI5622_RX_INIT_MAX];
	u16	total;
};

/* Live driver state the gate needs (filled by the driver or a test model). */
struct hi5622_rx_init_live {
	u8	armed;
	u8	released;
	u8	boot_latched;
	u8	lane_closed;
	u8	phase_ready;	/* READY_TXQ_SYNC or ACTIVE */
	u8	ctx_created;	/* ordered ctx_create (key 0x64) was accepted */
	u8	already_sent;	/* this load already sent rx_init */
	u8	liveness_used;	/* the safe-probe idea was already used */
	u32	frames_sent;
	u32	budget;
	u32	mbox_inflight;
	u32	out0;
};

enum hi5622_rx_init_refusal {
	HI5622_RX_INIT_OK = 0,
	HI5622_RX_INIT_NOT_ARMED,
	HI5622_RX_INIT_NOT_RELEASED,
	HI5622_RX_INIT_NOT_LATCHED,
	HI5622_RX_INIT_LANE_CLOSED,
	HI5622_RX_INIT_PHASE,
	HI5622_RX_INIT_NO_CTX,
	HI5622_RX_INIT_DUP,
	HI5622_RX_INIT_LIVENESS_USED,
	HI5622_RX_INIT_BUDGET,
	HI5622_RX_INIT_INFLIGHT,
	HI5622_RX_INIT_OUT0,
};

enum hi5622_rx_init_result {
	HI5622_RX_INIT_RESULT_CONSUMED = 0,
	HI5622_RX_INIT_RESULT_TIMEOUT,
	HI5622_RX_INIT_RESULT_LANE_CLOSED,
	HI5622_RX_INIT_RESULT_BAD_ARG,
};

/* Build the candidate frame.  key must be the one allowed key (fail-closed);
 * value is written little-endian as the 4-byte data area.  Returns 0. */
int hi5622_rx_init_build(struct hi5622_rx_init_frame *f, u16 key, u32 value);

/* Validate a built wire image at the transport + allowlist level.  Returns 0,
 * -EINVAL on any structural violation.  (No errno dependency: negative ints.) */
int hi5622_rx_init_validate(const u8 *wire, u32 len);

/* Fail-closed allowlist: only key 0x0004 is a legal RX-init key. */
int hi5622_rx_init_key_allowed(u16 key);

/* Gate evaluation, first refusal wins. */
enum hi5622_rx_init_refusal
hi5622_rx_init_gate(const struct hi5622_rx_init_live *live);

const char *hi5622_rx_init_refusal_name(enum hi5622_rx_init_refusal r);

/* Read-back classification of the bounded out[0] announce watch. */
enum hi5622_rx_init_result
hi5622_rx_init_readback(u32 out0_after_polls, u32 polls_done, u32 poll_max,
			u8 lane_closed);

#endif /* HI5622_RX_INIT_LOGIC_H */
