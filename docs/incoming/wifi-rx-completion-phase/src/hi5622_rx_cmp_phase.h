/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rx_cmp_phase.h - portable completion-ring consumer/phase model for
 * the Hi5622V100 clean-room host (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-rx-completion-phase, shared task
 * task-168).  No vendor source/object code, firmware byte, calibration value
 * or per-unit identity is reproduced: every constant below is a derived fact
 * with a [V]/[V*] marker cited in REPORT.md, and every routine is device-free
 * so the host selftest can pin it without a kernel or a bench.
 *
 * Why this file exists (task-168, the completion-consumer candidate): the
 * completion ring consumer is the single host-written word of the RX capture
 * path that the previous lanes never modelled as a *phase* quantity.  The
 * vendor ring library publishes a packed 16-bit pointer = index[14:0] |
 * phase<<15 (hal_ring_set_sw2hw 0x31f1c, type 2 = completion ring, 32-bit
 * store to config +0x40); the consumer is advanced by hal_ring_get_entries
 * 0x32164 (phase toggles on wrap) and the device producer is read from live
 * bank +0x10 by hal_ring_get_hw2sw 0x31e1c.  The initial consumer the vendor
 * publishes is 0x0000, not 0x8000 (stock dump, see REPORT.md).  This header
 * pins the vendor semantics, the vendor-exact initial value, the requested
 * phase-1 experiment and its state-machine gates.
 *
 * Evidence markers: [V] decoded from opensource/docs/incoming/wifi-shim-port/
 * raw/hi5622v100_wifi.dis / committed register dumps; [V*] hardware-observed
 * by an attended lane and quoted from a committed report; [I] inference;
 * [U] unknown.
 */
#ifndef HI5622_RX_CMP_PHASE_H
#define HI5622_RX_CMP_PHASE_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8  rxc_u8;
typedef u16 rxc_u16;
typedef u32 rxc_u32;
typedef s32 rxc_s32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  rxc_u8;
typedef uint16_t rxc_u16;
typedef uint32_t rxc_u32;
typedef int32_t  rxc_s32;
#endif

/* ===================================================================
 * 1. Completion-ring constants [V]
 * =================================================================== */
#define RXC_IDX_MASK   0x7fffu   /* packed pointer index bits [14:0] */
#define RXC_PHASE_BIT  0x8000u   /* packed pointer phase bit 15 */
#define RXC_CMP_DEPTH  4095u     /* completion ring depth, band 0 [V] */
#define RXC_CONFIG_CMP_RPTR 0x40u /* host-interface config word, band 0 */
#define RXC_LIVE_CMP_WPTR 0x10u  /* live bank word: device producer */

/* The vendor-exact initial consumer: 0x0000 (index 0, phase 0) [V].
 * Evidence: shuangta_rx_host_init_dscr_queue (0x37888) zeroes the 0x28-byte
 * completion ring struct and never writes config +0x40; the vendor stock
 * register dump (vendor driver live, AP idle) reads cfg 0x40040040 = 0 and
 * live 0x40050010 = 0.  Do not confuse this with the free-ring producers,
 * which do publish 0x8000 (index 0, phase 1 = ring full) after the 256/512
 * entry fill: cfg +0x38/+0x3c = 0x8000 in the same dump. */
#define RXC_VENDOR_INITIAL_CONSUMER 0x0000u
#define RXC_FREE_RING_FULL_PUBLISH  0x8000u

/* The phase-1 prepublish experiment value: index 0, phase 1 = 0x8000. */
#define RXC_PHASE1_PREPUBLISH 0x8000u

/* Return codes (negative; same shape as the other lane headers). */
#define RXC_OK      0
#define RXC_EARG   (-1)
#define RXC_ERANGE (-2)

/* ===================================================================
 * 2. Vendor ring arithmetic
 * =================================================================== */

/* Packed pointer: index in [14:0], phase in bit 15. */
rxc_u32 rxc_pack(rxc_u32 index, rxc_u32 phase);
void     rxc_unpack(rxc_u32 packed, rxc_u32 *index, rxc_u32 *phase);

/* Next packed pointer after publishing/consuming one entry; phase toggles on
 * wrap (vendor hal_ring_set_entries 0x32354 / hal_ring_get_entries 0x32258:
 * the index goes to 0 and bit 15 is inverted). */
rxc_u32 rxc_next(rxc_u32 packed, rxc_u32 depth);

/* Advance a consumer by n consumed entries (n <= depth). */
rxc_u32 rxc_advance(rxc_u32 packed, rxc_u32 n, rxc_u32 depth);

/* Pending count with the vendor hal_ring_get_entry_count (0x31f80) phase-aware
 * arithmetic: equal packed words -> 0; equal index with different phase ->
 * the ring is one lap apart (full: pending = depth); otherwise pending is the
 * wrapped index distance.  Refuses a bad depth/out-of-range index. */
int rxc_pending(rxc_u32 producer, rxc_u32 consumer, rxc_u32 depth,
                rxc_u32 *count);

/* Ring-state classification used by the experiment prediction: given the
 * device producer and the published consumer, classify empty / partial /
 * full-lap.  full_lap = 1 exactly when the phase-aware count is the depth
 * (equal index, opposite phase). */
int rxc_ring_state(rxc_u32 producer, rxc_u32 consumer, rxc_u32 depth,
                   rxc_u32 *pending, rxc_u32 *full_lap);

/* ===================================================================
 * 3. The prepublish state machine (host-side decisions only)
 *
 * The rung is deliberately a one-shot, pre-arm, opt-in action: it publishes
 * the completion consumer before any traffic so the RX ladder can test
 * whether the device's DMAC requires a non-zero consumer phase before it
 * accepts its first completion write.  Every input below is checked by the
 * kernel glue BEFORE one 32-bit store is issued; the portable layer decides,
 * the glue performs the I/O.
 * =================================================================== */
enum rxc_gate {
    RXC_GATE_ALLOW = 0,
    RXC_GATE_DISABLED,       /* rx_cmp_prepublish module param is 0 */
    RXC_GATE_NO_RX_ENABLE,   /* rx_enable=0 */
    RXC_GATE_NOT_READY,      /* bind/transport/write gate not up */
    RXC_GATE_ARMED,          /* dp_rx_arm already ran: too late */
    RXC_GATE_ALREADY_DONE,   /* one-shot already used in this load */
    RXC_GATE_LANE_FAILED,    /* RX lane or device already closed */
    RXC_GATE_CONSUMER_DIRTY /* cfg +0x40 is not the vendor initial 0 */
};

/* Decide whether the one-shot prepublish may write. */
enum rxc_gate rxc_prepublish_gate(rxc_u32 param_enabled, rxc_u32 rx_enable,
                                  rxc_u32 ready, rxc_u32 armed,
                                  rxc_u32 done, rxc_u32 lane_failed,
                                  rxc_u32 cfg_cmp_rptr);

/* Exact-width store value: replace only the low 16 bits of the 32-bit word
 * (the vendor setters 0x325e4 / hal_ring_set_sw2hw do a 32-bit access; the
 * upper half is 0 in the vendor stock state). */
rxc_u32 rxc_store_value(rxc_u32 old_word, rxc_u32 packed);

/* True when the read-back carries the requested packed value in [15:0]. */
int rxc_readback_ok(rxc_u32 readback, rxc_u32 packed);

/* Status-word reaction check for the probe-and-rollback rung: the device's
 * host-MAC status word bits 7 (rptr overtake wptr) and 10 (rx_ppdu_desc
 * error) are the two phase/descriptor consistency flags [V: status map in
 * wifi-rx-capture-plan REPORT section 5.2 and shuangta_get_rpt_host_intr_
 * rptr_overtake_wptr 0x3a818 = (word >> 7) & 1].  Returns the mask of flags
 * that changed from 0 to 1 (0 = no reaction). */
#define RXC_STATUS_BIT_RPTR_OVERTAKE 0x00000080u
#define RXC_STATUS_BIT_PPDU_DESC_ERR 0x00000400u
rxc_u32 rxc_status_reaction(rxc_u32 before, rxc_u32 after);

/* Predicted outcome of the vendor library's phase-aware count for the
 * phase-1 prepublish state: producer 0 (phase 0) + consumer 0x8000
 * (phase 1) = equal index / opposite phase = one full lap.  Returns 1 when
 * the published value makes the ring look full to a phase-aware consumer
 * (the reason R1 runs WITHOUT traffic before R2). */
int rxc_phase1_looks_full(rxc_u32 producer);

#endif /* HI5622_RX_CMP_PHASE_H */
