/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rx_woc.h - portable model of the vendor-class ("WOC") RX buffer
 * allocation protocol for the Hi5622V100 clean-room driver.
 *
 * ORIGINAL clean-room code (wifi-rx-woc-class lane, shared task task-167).
 * This is the host-testable model of the kernel-side implementation in
 * patches/0001-rx-woc-class.patch (hi5622_dp.c / hi5622_dp_txrx.c).  It is
 * plain C with no kernel headers so tools/rx_woc_selftest.c can prove the
 * arithmetic and the fail-closed decisions on the build host.
 *
 * The protocol is a derived fact decoded from the local vendor host stack
 * (hi_kpie.ko + hi5622v100_wifi.ko, not committed); every constant below is
 * cited in REPORT.md section 3.  No vendor code or byte is reproduced.
 */
#ifndef _HI5622_RX_WOC_MODEL_H
#define _HI5622_RX_WOC_MODEL_H

typedef unsigned char  rxwoc_u8;
typedef unsigned short rxwoc_u16;
typedef unsigned int   rxwoc_u32;

/* Buffer classes (must match the kernel module parameter rx_buf_class). */
#define RXWOC_CLASS_COHERENT    0u
#define RXWOC_CLASS_WOC         1u

/* Device buffer geometry (matches dp_rings.h of the fired driver). */
#define RXWOC_HOST_WORD         4u        /* ring entry = buffer+4 (header start) */
#define RXWOC_HDR_BYTES         0x24u     /* 36-byte device-written header */
#define RXWOC_NORM_BUFLEN       0x63cu    /* configured device-writable length */
#define RXWOC_SMALL_BUFLEN      0xb0u
#define RXWOC_NORM_STRIDE       (RXWOC_HOST_WORD + RXWOC_NORM_BUFLEN)   /* 0x640 */
#define RXWOC_SMALL_STRIDE      (RXWOC_HOST_WORD + RXWOC_SMALL_BUFLEN)  /* 0xb4 */

/* Vendor-class allocation rules. */
#define RXWOC_ALIGN             4u        /* skb->data aligned to 4, vendor add_buff */
#define RXWOC_ALLOC_MARGIN      8u        /* our skb head margin for the align shift */
#define RXWOC_HOST_WORD_MAGIC   0x574f4301u /* "WOC" + class tag, host bookkeeping only */

/* Device-visible host window accepted by the driver. */
#define RXWOC_HOST_DMA_LO       0x80000000u
#define RXWOC_HOST_DMA_HI       0x8fffffffu

/* Error codes (<= 0; 0 = OK). */
#define RXWOC_OK        0
#define RXWOC_EARG     (-1)
#define RXWOC_ERANGE   (-2)
#define RXWOC_ENOMEM   (-3)
#define RXWOC_EBADCLASS (-4)

/* Class validation: only the two known classes are accepted; the latched value
 * is what the load uses for every later decision.  Fail-closed. */
int rxwoc_class_valid(rxwoc_u32 requested, rxwoc_u32 *latched);

/* __netdev_alloc_skb length: allocation size + alignment margin. */
rxwoc_u32 rxwoc_alloc_len(rxwoc_u32 size);
/* Bytes to skb_reserve() so skb->data is RXWOC_ALIGN aligned. */
rxwoc_u32 rxwoc_align_shift(rxwoc_u32 data_addr);
/* True if the DMA address is inside the device-visible host window. */
int rxwoc_window_ok(rxwoc_u32 dma);
/* Ring entry for a buffer: device VA of the 36-byte header = dma + 4. */
int rxwoc_entry(rxwoc_u32 dma, rxwoc_u32 *entry);
/* Per-publish preparation: write the host word and zero the 36-byte header.
 * Leaves everything from buffer+0x28 on untouched. */
int rxwoc_prepare(rxwoc_u8 *buf, rxwoc_u32 size, rxwoc_u32 *word_out);
/* Cache-maintenance span for the preparation write (whole allocation),
 * DMA_BIDIRECTIONAL semantics: flush then invalidate. */
int rxwoc_flush_span(rxwoc_u32 size, rxwoc_u32 *span);
/* Read-only witness after preparation: 1 = host word intact, *header_nonzero
 * tells whether the device already wrote the 36-byte header. */
int rxwoc_witness(const rxwoc_u8 *buf, rxwoc_u32 size, int *prepared,
                  int *header_nonzero);
/* True if "size" is a valid allocation size for the class geometry. */
int rxwoc_size_valid(rxwoc_u32 size, int small_ring);

#endif /* _HI5622_RX_WOC_MODEL_H */
