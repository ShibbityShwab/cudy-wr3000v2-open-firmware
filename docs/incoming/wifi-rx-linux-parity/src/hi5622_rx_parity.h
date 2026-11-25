/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rx_parity.h - portable Linux-driver RX-enable parity rules for the
 * Hi5622V100 clean-room host (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-rx-linux-parity, shared task task-166).
 * No vendor source/object code, no vendor text, no firmware byte and no
 * per-unit identity is reproduced.  The file exists so the host selftest can
 * pin the rules that the kernel glue applies to the MAC RX path; every rule is
 * a derived fact with a Linux-driver reference (an architectural analogue, not
 * copied code) and/or a committed project evidence marker in REPORT.md.
 *
 * Why this file exists: the hi5622 RX ring program (the 14 MAC writes) matches
 * the vendor's ring init, yet the MAC never writes a received frame.  The
 * driver reproduces only part of the canonical Linux RX-enable order - the
 * ring program, the free-buffer fill and the producer publish - and does not
 * have the steps Linux drivers also perform before they expect RX traffic:
 * a completion-entry ownership/commit decision, an exact-width pointer
 * publish, a served-bits-only W1C, a bounded refill window and a stall
 * classification.  This file models that missing half so it can be tested on
 * the host and applied fail-closed on the bench.
 *
 * Evidence markers used in the comments:
 *   [V]  committed project decode of a vendor/Linux artifact
 *   [L]  measured on the bench by an earlier attended lane (quoted)
 *   [I]  inference
 *   [U]  unknown / not pinned
 */
#ifndef HI5622_RX_PARITY_H
#define HI5622_RX_PARITY_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8  rxp_u8;
typedef u16 rxp_u16;
typedef u32 rxp_u32;
typedef s32 rxp_s32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  rxp_u8;
typedef uint16_t rxp_u16;
typedef uint32_t rxp_u32;
typedef int32_t  rxp_s32;
#endif

/* ===================================================================
 * 1. The canonical RX-enable step catalogue (Linux reference -> us)
 *
 * The comparison table in REPORT.md cites the exact function and file for
 * every row in a local Linux 5.10.201 tree (mt76, iwlwifi, ath10k, ath11k,
 * brcmfmac).  The catalogue here only names the steps so the driver telemetry
 * and the host test share one vocabulary.
 * =================================================================== */
enum rxp_step {
    RXP_STEP_RING_ALLOC = 0,   /* ring memory + DMA-coherent allocation   */
    RXP_STEP_RING_PROGRAM,     /* base/size/id/depth registers            */
    RXP_STEP_BUF_FILL,         /* per-buffer DMA mapping + entry write    */
    RXP_STEP_DMA_ENABLE,       /* explicit engine/ring enable word        */
    RXP_STEP_IRQ_NAPI,         /* interrupt unmask + NAPI                 */
    RXP_STEP_RX_FILTER,        /* MAC address / RX filter word            */
    RXP_STEP_PRODUCER_PUBLISH, /* host producer index store               */
    RXP_STEP_KICK,             /* second store / doorbell after publish   */
    RXP_STEP_COMPLETION_CONSUME,
    RXP_STEP_CONSUMER_PUBLISH,
    RXP_STEP_REFILL,
    RXP_STEP_STALL_WATCH,
    RXP_STEP_TEARDOWN,
    RXP_STEP_COUNT
};

struct rxp_step_info {
    const char *name;
    const char *linux_analogue;  /* architectural reference, not quoted code */
    const char *our_state;       /* done / partial / missing / n/a           */
};

/* Static catalogue (never NULL for a valid step). */
const struct rxp_step_info *rxp_step_info(enum rxp_step step);
const char *rxp_step_name(enum rxp_step step);

/* ===================================================================
 * 2. Exact-width pointer publication (vendor hal_ring_set_sw2hw analogue)
 *
 * Linux drivers publish a host producer/consumer index as a plain integer in
 * a ring register (mt76 cpu_idx, ath10k CE ring write index, iwlwifi RBD
 * write pointer); the luofu vendor's hal_ring_set_sw2hw stores a
 * zero-extended u16 packed {index[14:0], phase<<15} [V wifi-rx-static REPORT
 * sec. 4].  The inherited driver used a 32-bit read-modify-write that
 * preserved the upper half - a write width the vendor never uses.  The
 * parity rule is: validate the packed value, write exactly the zero-extended
 * u16, and refuse when the register reads back with unexpected upper bits
 * (that is a state the vendor path never produces).
 * =================================================================== */
#define RXP_IDX_MASK    0x7fffu
#define RXP_PHASE_BIT   0x8000u
#define RXP_PTR_MAX     0xffffu

/* Pack an (index, phase) pair the way every luofu ring pointer is packed.
 * Returns 0 on success; refuses depth 0/too deep, index >= depth, phase > 1. */
int rxp_publish_value(rxp_u32 index, unsigned int phase, rxp_u32 depth,
                      rxp_u32 *value);

/* ===================================================================
 * 3. Completion-entry commit decision (Linux ownership/DONE-bit analogue)
 *
 * Linux completion consumers never consume an entry until the device has
 * committed it: mt76 descriptors carry MT_DMA_CTL_DMA_DONE, ath11k descriptors
 * carry a loop count, iwlwifi reads the uCode's READ index after the write
 * pointer.  The luofu completion ring has no explicit owner bit [V/U], so the
 * driver can only bound the producer-visible-before-entry-committed window
 * (the Hi1105 sibling HAL spins until the entry is non-zero, <=1000
 * iterations [V sibling REPORT sec. 4.2]).  Rule: read the entry, if zero
 * re-read it up to rx_entry_wait times, and only consume a value that is
 * non-zero and stable across the two reads; a still-zero entry is a
 * spurious producer advance (not corruption) and is not consumed.
 * =================================================================== */
#define RXP_ENTRY_WAIT_DEF 32u /* bounded re-reads of a still-zero entry      */
#define RXP_SPURIOUS_MAX 64u   /* consecutive spurious advances before close */

/* Settled = the last read is non-zero AND (the first read was zero - it
 * committed between the two reads - or both reads agree). */
int rxp_commit_settled(rxp_u32 first, rxp_u32 last, int *settled);

/* Bounded-wait decision: retry = 1 means "read again"; retry = 0 means the
 * caller must decide (settled or spurious).  waits_done is the number of
 * re-reads already performed. */
int rxp_commit_retry(rxp_u32 last, unsigned int waits_done,
                     unsigned int wait_max, int *retry);

/* Close the lane only once the consecutive-spurious budget is exceeded. */
int rxp_spurious_limit(rxp_u32 spurious, rxp_u32 cap, int *close_lane);

/* ===================================================================
 * 4. Served-bits-only W1C (vendor ISR analogue)
 *
 * The vendor host-MAC ISR computes status &= ~mask and W1Cs exactly the
 * served bits to config +0x44 [V wifi-rx-static REPORT sec. 5].  The
 * inherited driver W1C'd the raw live status word.  Rule: only the bits the
 * mask leaves enabled may be acknowledged; the masked-off bits are not the
 * host's to clear.
 * =================================================================== */
int rxp_w1c_served(rxp_u32 status, rxp_u32 mask, rxp_u32 *value);

/* ===================================================================
 * 5. Bounded free-ring refill window (Linux refill/restock analogue)
 *
 * Linux RX queues refill only slots the device has returned and never
 * overrun the posted producer (iwlwifi restock moves the WRITE index to the
 * READ index; ath11k replenishes into requested entries; mt76 fills to
 * ndesc - 1).  The inherited drain refilled by the raw consumer delta but did
 * not bound the plan by the posted/unposted room.  Rule: plan =
 * min(device-consumed delta, returned buffers queued, depth - posted, cap)
 * and derive the new producer from it; refuse any input that cannot describe
 * a valid window instead of clamping.
 * =================================================================== */
int rxp_refill_plan(rxp_u32 old_consumer, rxp_u32 fresh_consumer,
                    rxp_u32 producer, rxp_u32 depth, rxp_u32 queued,
                    rxp_u32 cap, rxp_u32 *count, rxp_u32 *new_producer);

/* ===================================================================
 * 6. Opt-in RX-control candidate word (the missing explicit enable)
 *
 * Linux drivers write an explicit RX DMA/ring enable word before the ring can
 * produce completions: mt76 sets MT_WPDMA_GLO_CFG_RX_DMA_EN after the ring
 * base/size/index program and before the RX ring interrupts are enabled
 * (mt7615_dma_start), iwlwifi writes FH_MEM_RCSR_CHNL0_CONFIG_REG with the
 * channel EN bit / RFH_RXF_DMA_CFG with RFH_DMA_EN_ENABLE_VAL, ath11k sets
 * HAL_REO1_RING_MISC_SRNG_ENABLE in the ring MISC register.  Our chip has no
 * decoded host-writable equivalent yet [U]: the 14-write program writes every
 * vendor-stock non-zero word of the MAC config block except +0x5c/+0x6c
 * (which have no identified writer and are covered by the existing
 * mac_tail_writes experiment).  The only words left in that block that are
 * zero in the vendor stock capture and are NOT part of the ring program are
 * the four candidates below.  This lane never claims one of them is the
 * enable: it provides the read-only parity probe plus a one-shot, explicitly
 * enabled, allowlisted candidate write for an attended fire.
 * =================================================================== */
#define RXP_CTL_OFF_CANDIDATE_MAX 4u

/* 1 = a candidate offset the one-shot dp_rx_ctl action may write.  The list is
 * deliberately tiny and fixed: 0x08, 0x0c, 0x14, 0x18 (zero in the vendor
 * stock dump, outside the 14-write ring program, inside the 0xb0-byte block).
 * Everything else - in particular every ring-program/status word - is
 * refused. */
int rxp_ctl_offset_allowed(rxp_u32 off);

/* Compose the one-shot RMW value.  Refuses set & clr (a bit cannot be both),
 * refuses set | clr == 0 (nothing to do) and requires the offset to be an
 * allowlisted candidate. */
int rxp_ctl_value(rxp_u32 off, rxp_u32 old, rxp_u32 set, rxp_u32 clr,
                  rxp_u32 *value);

/* ===================================================================
 * 7. Stall classification (iwlwifi RX_STALLED / sibling P3 analogue)
 *
 * iwlwifi documents RX_STALLED: when no free buffers are available the READ
 * index is not advanced and the stall flag is set, because the free-buffer
 * ring is the RX credit.  The sibling Hi1105 HAL has a dedicated
 * free-ring-empty interrupt.  Our chip exposes the live bank only, so the
 * driver classifies a drain into: IDLE (nothing moved), PREFETCH_ONLY (the
 * device consumer moved, no completion), PROGRESS (completion producer moved)
 * or SPURIOUS (producer visible without a settled entry).  Log-only; no
 * pointer is moved by the classification.
 * =================================================================== */
enum rxp_stall {
    RXP_STALL_IDLE = 0,
    RXP_STALL_PREFETCH_ONLY,
    RXP_STALL_PROGRESS,
    RXP_STALL_SPURIOUS,
    RXP_STALL_ERROR
};

int rxp_stall_class(rxp_u32 arm_status, rxp_u32 arm_norm_rptr,
                    rxp_u32 arm_small_rptr, rxp_u32 arm_cmp_wptr,
                    rxp_u32 now_status, rxp_u32 now_norm_rptr,
                    rxp_u32 now_small_rptr, rxp_u32 now_cmp_wptr,
                    rxp_u32 spurious, int *class_out);
const char *rxp_stall_name(enum rxp_stall stall);

/* ===================================================================
 * 8. Read-only parity probe classification
 *
 * The probe dumps the MAC config block, the live bank and the first words of
 * the other eight machw blocks; these classification helpers tell the host
 * tool which words are part of the reproduced 14-write program, which are
 * vendor-stock non-zero words the driver never writes (the candidate missing
 * configuration) and whether a word currently matches the vendor stock dump.
 * =================================================================== */
#define RXP_MAC_CFG_SPAN 0xb0u  /* vendor-dumper block length [V] */

/* 1 = off is one of the 14 MAC words the driver's dp_setup programs. */
int rxp_is_program_off(rxp_u32 off);

/* Vendor stock value of a MAC config block word, from the committed
 * opensource/dumps/reg_all.txt capture [V*]; unknown/never-dumped offsets read
 * as 0 (the whole block is 0 in the capture except the words in the table). */
rxp_u32 rxp_mac_cfg_stock(rxp_u32 off);

/* Classify one word: 0 = absent from the probe set, 1 = programmed word
 * matching stock, 2 = programmed word differing from stock, 3 = unprogrammed
 * word that is non-zero in stock (the "vendor has state we never write" set),
 * 4 = unprogrammed word, zero in stock. */
int rxp_mac_cfg_class(rxp_u32 off, rxp_u32 value, int *stock_nonzero,
                      int *matches_stock);

#endif /* HI5622_RX_PARITY_H */
