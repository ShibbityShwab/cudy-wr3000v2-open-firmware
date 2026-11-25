// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rx_woc.c - portable model of the vendor-class ("WOC") RX buffer
 * allocation protocol.  Host-testable; no kernel headers, no vendor bytes.
 * See REPORT.md section 3 for the decode this models.
 */
#include "hi5622_rx_woc.h"

#include <string.h>

static rxwoc_u32 rxwoc_le32(const rxwoc_u8 *p)
{
    return (rxwoc_u32)p[0] | ((rxwoc_u32)p[1] << 8) |
           ((rxwoc_u32)p[2] << 16) | ((rxwoc_u32)p[3] << 24);
}

int rxwoc_class_valid(rxwoc_u32 requested, rxwoc_u32 *latched)
{
    if (latched == NULL)
        return RXWOC_EARG;
    if (requested == RXWOC_CLASS_COHERENT) {
        *latched = RXWOC_CLASS_COHERENT;
        return RXWOC_OK;
    }
    if (requested == RXWOC_CLASS_WOC) {
        *latched = RXWOC_CLASS_WOC;
        return RXWOC_OK;
    }
    *latched = RXWOC_CLASS_COHERENT; /* safe value; caller must refuse */
    return RXWOC_EBADCLASS;
}

rxwoc_u32 rxwoc_alloc_len(rxwoc_u32 size)
{
    return size + RXWOC_ALLOC_MARGIN;
}

rxwoc_u32 rxwoc_align_shift(rxwoc_u32 data_addr)
{
    return (RXWOC_ALIGN - (data_addr & (RXWOC_ALIGN - 1u))) &
           (RXWOC_ALIGN - 1u);
}

int rxwoc_window_ok(rxwoc_u32 dma)
{
    if (dma < RXWOC_HOST_DMA_LO || dma > RXWOC_HOST_DMA_HI)
        return RXWOC_ERANGE;
    return RXWOC_OK;
}

int rxwoc_entry(rxwoc_u32 dma, rxwoc_u32 *entry)
{
    if (entry == NULL)
        return RXWOC_EARG;
    if (rxwoc_window_ok(dma) != RXWOC_OK)
        return RXWOC_ERANGE;
    if (dma > 0xffffffffu - RXWOC_HOST_WORD)
        return RXWOC_ERANGE;
    *entry = dma + RXWOC_HOST_WORD;
    return RXWOC_OK;
}

int rxwoc_size_valid(rxwoc_u32 size, int small_ring)
{
    rxwoc_u32 want = small_ring ? RXWOC_SMALL_STRIDE : RXWOC_NORM_STRIDE;

    if (size != want)
        return RXWOC_ERANGE;
    if (size < RXWOC_HOST_WORD + RXWOC_HDR_BYTES)
        return RXWOC_ERANGE;
    return RXWOC_OK;
}

int rxwoc_prepare(rxwoc_u8 *buf, rxwoc_u32 size, rxwoc_u32 *word_out)
{
    rxwoc_u32 w = RXWOC_HOST_WORD_MAGIC;

    if (buf == NULL || word_out == NULL)
        return RXWOC_EARG;
    if (size < RXWOC_HOST_WORD + RXWOC_HDR_BYTES)
        return RXWOC_ERANGE;
    /* Only the host word and the 36-byte device header are pre-written; the
     * device owns everything from buffer+0x28 on. */
    buf[0] = (rxwoc_u8)(w & 0xffu);
    buf[1] = (rxwoc_u8)((w >> 8) & 0xffu);
    buf[2] = (rxwoc_u8)((w >> 16) & 0xffu);
    buf[3] = (rxwoc_u8)((w >> 24) & 0xffu);
    memset(buf + RXWOC_HOST_WORD, 0, RXWOC_HDR_BYTES);
    *word_out = w;
    return RXWOC_OK;
}

int rxwoc_flush_span(rxwoc_u32 size, rxwoc_u32 *span)
{
    if (span == NULL || size == 0u)
        return RXWOC_EARG;
    if (size != RXWOC_NORM_STRIDE && size != RXWOC_SMALL_STRIDE)
        return RXWOC_ERANGE;
    /* The vendor flushes the whole configured buffer span around add_buff (and
     * the 128-byte header area separately); one BIDIRECTIONAL sync over the
     * allocation is the single-call equivalent the kernel patch uses. */
    if (size < RXWOC_HOST_WORD + RXWOC_HDR_BYTES)
        return RXWOC_ERANGE;
    *span = size;
    return RXWOC_OK;
}

int rxwoc_witness(const rxwoc_u8 *buf, rxwoc_u32 size, int *prepared,
                  int *header_nonzero)
{
    rxwoc_u32 i;

    if (buf == NULL || prepared == NULL || header_nonzero == NULL)
        return RXWOC_EARG;
    if (size < RXWOC_HOST_WORD + RXWOC_HDR_BYTES)
        return RXWOC_ERANGE;
    *prepared = rxwoc_le32(buf) == RXWOC_HOST_WORD_MAGIC ? 1 : 0;
    *header_nonzero = 0;
    for (i = 0; i < RXWOC_HDR_BYTES; i++) {
        if (buf[RXWOC_HOST_WORD + i] != 0u) {
            *header_nonzero = 1;
            break;
        }
    }
    return RXWOC_OK;
}
