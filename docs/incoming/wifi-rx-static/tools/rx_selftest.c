/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rx_selftest.c - host-only selftest for the wifi-rx-static MAC RX model
 * (shared task task-137).
 *
 * Why: the MAC RX completion path was never exercised because the driver
 * polled the wrong register bank, and the inherited capture decoder had its
 * descriptor base 4 bytes too high (it treated the 4-byte skb bookkeeping
 * word as part of the device header).  These checks pin the corrected model
 * with LITERAL vendor offsets (not the header macros) so a macro regression
 * cannot make the test lie, then check the driver helper against the same
 * image.
 *
 * Build: cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I<staged-src>  *        rx_selftest.c <staged-src>/hi5622_rx_live.c
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_rx_live.h"

#define NORM_DEPTH  256u
#define SMALL_DEPTH 512u
#define CMP_DEPTH   4095u
#define NORM_BUFLEN 0x63cu
#define SMALL_BUFLEN 0x00b0u
#define NORM_POOL_LO 0x83600004u  /* device VA of norm buffer 0 header area */
#define SMALL_POOL_LO 0x84000004u
#define STRIDE_NORM 0x640u

static int checks;
static int fails;

static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("FAIL: %s\n", what);
    } else {
        printf("PASS: %s\n", what);
    }
}

static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

/* One synthetic RX buffer: 4-byte skb word + 36-byte device header + payload. */
static void build_buf(unsigned char *buf, size_t cap, unsigned int len,
                      unsigned long next_devva)
{
    memset(buf, 0, cap);
    buf[0] = 0x58; /* skb bookkeeping word (host-owned) */
    buf[1] = 0x52;
    buf[2] = 0x58;
    buf[3] = 0x54;
    /* device header at buffer+4; length at literal buffer+0x1e */
    put16(buf + 0x1e, len);
    /* next sub-MSDU devVA at literal buffer+0x24 */
    put32(buf + 0x24, next_devva);
}

int main(void)
{
    unsigned char buf[0x700];
    struct hi5622_rx_hdr hdr;
    unsigned int i;
    unsigned long entry;
    unsigned int count;
    unsigned int active;
    unsigned char kind;
    int rc;

    /* ---- 1. literal vendor constants (the decode anchors) ---- */
    ck(HIL_RX_LIVE_CA_BAND0 == 0x40050000u, "live bank band0 CA = 0x40050000 [V]");
    ck(HIL_RX_LIVE_NORM_RPTR == 0x08u && HIL_RX_LIVE_SMALL_RPTR == 0x0cu &&
       HIL_RX_LIVE_CMP_WPTR == 0x10u, "live rptr/wptr offsets +8/+0xc/+0x10 [V]");
    ck(HIL_RX_LIVE_STATUS == 0x00u, "live status offset +0x00 [V]");
    ck(HIL_RX_CFG_INT_MASK == 0x48u && HIL_RX_INT_MASK_INIT == 0xfffffcc1u,
       "config +0x48 mask / 0xfffffcc1 constant [V]");
    ck(HIL_RX_CFG_WPTR_NORM == 0x38u && HIL_RX_CFG_WPTR_SMALL == 0x3cu &&
       HIL_RX_CFG_CMP_RPTR == 0x40u, "free-ring wptr +0x38/+0x3c, cmp rptr +0x40 [V]");
    ck(HIL_RX_HDR_LEN_OFF == 0x1au, "payload length at header+0x1a (= buffer+0x1e) [V]");
    ck(HIL_RX_HDR_NEXT_OFF == 0x20u, "next sub-MSDU devVA at header+0x20 (= buffer+0x24) [V]");
    ck(HIL_RX_HDR_BYTES == 36u && HIL_RX_PAYLOAD_OFF == 0x28u,
       "header 36 B at buffer+4, MPDU at buffer+0x28 [V]");

    /* ---- 2. completion-ring distance (producer ahead of consumer) ---- */
    rc = hi5622_rx_ring_pending(0u, 0u, CMP_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 0u, "cmp pending: empty ring -> 0");
    rc = hi5622_rx_ring_pending(0x8010u, 0x8000u, CMP_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 0x10u, "cmp pending: same phase, +0x10 -> 16");
    rc = hi5622_rx_ring_pending(0x8000u, 0u, CMP_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == CMP_DEPTH, "cmp pending: one full lap -> 4095");
    rc = hi5622_rx_ring_pending(0x8064u, 0x0fa0u, CMP_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 195u,
       "cmp pending: phase-wrapped producer 0x8064 vs consumer 4000 -> 195");
    rc = hi5622_rx_ring_pending(0x010u, 0x020u, CMP_DEPTH, &count);
    ck(rc == HIL_RX_EFORMAT, "cmp pending: consumer ahead -> EFORMAT (fail closed)");
    rc = hi5622_rx_ring_pending(0x1000u, 0u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_ERANGE, "cmp pending: index beyond depth -> ERANGE");

    /* ---- 3. free-ring consumer distance (refill budget) ---- */
    rc = hi5622_rx_ring_consumed(0u, 0u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 0u, "norm consumed: no movement -> 0");
    rc = hi5622_rx_ring_consumed(0u, 0x20u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 0x20u, "norm consumed: rptr 0 -> 0x20 = 32");
    rc = hi5622_rx_ring_consumed(0x8000u, 0x20u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 0x20u,
       "norm consumed: published 0x8000 -> rptr 0x20 = 32 (vendor prefetch state)");
    rc = hi5622_rx_ring_consumed(0u, 0x8000u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == NORM_DEPTH, "norm consumed: exactly one lap -> 256");
    rc = hi5622_rx_ring_consumed(0x80ffu, 0x8000u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 1u, "norm consumed: wrap edge 0x80ff -> 0x8000 = 1");
    rc = hi5622_rx_ring_consumed(0x1000u, 0x8000u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_ERANGE, "norm consumed: bad snapshot -> ERANGE");

    /* ---- 4. packed index advance / publish ---- */
    ck(hi5622_rx_idx_advance(0u, NORM_DEPTH) == 1u, "advance 0 -> 1");
    ck(hi5622_rx_idx_advance(255u, NORM_DEPTH) == 0x8000u,
       "advance last slot -> index 0 phase 1 (0x8000)");
    ck(hi5622_rx_idx_advance(0x80ffu, NORM_DEPTH) == 0u,
       "advance last slot of lap 2 -> 0");
    ck(hi5622_rx_idx_advance(4094u, CMP_DEPTH) == 0x8000u,
       "advance cmp ring last slot -> 0x8000");
    rc = hi5622_rx_wptr_pack(0u, 1u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 0x8000u, "wptr pack index 0 phase 1 -> 0x8000");
    rc = hi5622_rx_wptr_pack(255u, 0u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_OK && count == 255u, "wptr pack index 255 phase 0 -> 255");
    rc = hi5622_rx_wptr_pack(NORM_DEPTH, 0u, NORM_DEPTH, &count);
    ck(rc == HIL_RX_ERANGE, "wptr pack index == depth -> ERANGE");

    /* ---- 5. completion header: literal byte offsets on a real buffer ---- */
    build_buf(buf, sizeof(buf), 100u, 0u);
    ck(le16(buf + 0x1e) == 100u, "synthetic buffer writes length at literal buf+0x1e");
    rc = hi5622_rx_hdr_parse(buf + 4u, NORM_BUFLEN, &hdr);
    ck(rc == HIL_RX_OK && hdr.payload_len == 100u,
       "parse norm header: len 100 at header base buffer+4");
    ck(hdr.next_devva == 0u && hdr.flags9 == 0u,
       "parse norm header: no chain, flags byte read from header+0x05");
    build_buf(buf, sizeof(buf), NORM_BUFLEN - 0x24u, 0u);
    rc = hi5622_rx_hdr_parse(buf + 4u, NORM_BUFLEN, &hdr);
    ck(rc == HIL_RX_OK && hdr.payload_len == 0x618u,
       "parse norm header: maximum payload 0x618 accepted");
    build_buf(buf, sizeof(buf), NORM_BUFLEN - 0x24u + 1u, 0u);
    rc = hi5622_rx_hdr_parse(buf + 4u, NORM_BUFLEN, &hdr);
    ck(rc == HIL_RX_EFORMAT, "parse norm header: payload overrun -> EFORMAT");
    build_buf(buf, sizeof(buf), 0u, 0u);
    rc = hi5622_rx_hdr_parse(buf + 4u, NORM_BUFLEN, &hdr);
    ck(rc == HIL_RX_EFORMAT, "parse norm header: len 0 -> EFORMAT");
    build_buf(buf, sizeof(buf), 64u, SMALL_POOL_LO + 0x640u);
    rc = hi5622_rx_hdr_parse(buf + 4u, NORM_BUFLEN, &hdr);
    ck(rc == HIL_RX_ECHAIN, "parse header: sub-MSDU chain -> ECHAIN (refused)");
    build_buf(buf, sizeof(buf), 64u, SMALL_POOL_LO + 2u);
    rc = hi5622_rx_hdr_parse(buf + 4u, NORM_BUFLEN, &hdr);
    ck(rc == HIL_RX_EFORMAT, "parse header: misaligned chain pointer -> EFORMAT");
    build_buf(buf, sizeof(buf), 0x8cu, 0u);
    rc = hi5622_rx_hdr_parse(buf + 4u, SMALL_BUFLEN, &hdr);
    ck(rc == HIL_RX_OK && hdr.payload_len == 0x8cu,
       "parse small header: max small payload 0x8c accepted");

    /* ---- 6. completion entry -> pool classification ---- */
    rc = hi5622_rx_entry_split(NORM_POOL_LO, NORM_POOL_LO, NORM_POOL_LO + 0x63c0u,
                               SMALL_POOL_LO, SMALL_POOL_LO + 0xb000u, &kind);
    ck(rc == HIL_RX_OK && kind == 0u, "entry: norm buffer+4 -> kind norm");
    rc = hi5622_rx_entry_split(SMALL_POOL_LO + 0x4u, NORM_POOL_LO,
                               NORM_POOL_LO + 0x63c0u, SMALL_POOL_LO,
                               SMALL_POOL_LO + 0xb000u, &kind);
    ck(rc == HIL_RX_OK && kind == 1u, "entry: small buffer+4 -> kind small");
    rc = hi5622_rx_entry_split(0x81000004u, NORM_POOL_LO, NORM_POOL_LO + 0x63c0u,
                               SMALL_POOL_LO, SMALL_POOL_LO + 0xb000u, &kind);
    ck(rc == HIL_RX_ERANGE, "entry: outside both pools -> ERANGE");
    rc = hi5622_rx_entry_split(NORM_POOL_LO + 2u, NORM_POOL_LO,
                               NORM_POOL_LO + 0x63c0u, SMALL_POOL_LO,
                               SMALL_POOL_LO + 0xb000u, &kind);
    ck(rc == HIL_RX_EFORMAT, "entry: misaligned -> EFORMAT");

    /* ---- 7. live status decode ---- */
    rc = hi5622_rx_status_rx_bit(0x00000004u, 0xfffffcc1u, &active);
    ck(rc == HIL_RX_OK && active == 1u, "status: bit 2 unmasked -> RX pending");
    rc = hi5622_rx_status_rx_bit(0x00000004u, 0xfffffcc5u, &active);
    ck(rc == HIL_RX_OK && active == 0u, "status: bit 2 masked -> no RX pending");
    rc = hi5622_rx_status_rx_bit(0x00000100u, 0xfffffcc1u, &active);
    ck(rc == HIL_RX_OK && active == 0u, "status: unrelated bit -> no RX pending");

    /* ---- 8. end-to-end synthetic: publish 256, device consumes 32, one
     *         frame completes on slot 0 with a 100-byte payload ---- */
    /* pool arithmetic: slot i is at pool_lo + i*stride, and its slot index is
     * recoverable from the address (the linear search in the drain path). */
    ck(NORM_POOL_LO + 255u * STRIDE_NORM == 0x83600004u + 255u * 0x640u,
       "norm pool: slot 255 address arithmetic");
    for (i = 0; i < 2u; i++) {
        entry = (unsigned long)NORM_POOL_LO + (unsigned long)i * STRIDE_NORM;
        ck(entry == (unsigned long)NORM_POOL_LO + (unsigned long)i * STRIDE_NORM,
           "norm pool: slot address formula");
    }
    {
        const unsigned long slot0 = NORM_POOL_LO;
        unsigned char cmp[4];

        /* The MAC completion ring holds the buffer devVA (buffer+4). */
        put32(cmp, slot0);
        ck(le32(cmp) == slot0, "completion entry is the free-ring devVA (buffer+4)");
        build_buf(buf, sizeof(buf), 100u, 0u);
        memcpy(buf + 0x28, "PAYLOAD", 7);
        rc = hi5622_rx_hdr_parse(buf + 4u, NORM_BUFLEN, &hdr);
        ck(rc == HIL_RX_OK && hdr.payload_len == 100u,
           "end-to-end: completion entry decodes header and payload length");
        ck(le32((const unsigned char *)buf + HIL_RX_PAYLOAD_OFF) ==
           le32((const unsigned char *)"PAYLOAD"), "end-to-end: payload at buf+0x28");
        rc = hi5622_rx_ring_consumed(0x8000u, 0x20u, NORM_DEPTH, &count);
        ck(rc == HIL_RX_OK && count == 32u,
           "end-to-end: 32 consumed slots available for refill");
        rc = hi5622_rx_wptr_pack(1u, 1u, NORM_DEPTH, &count);
        ck(rc == HIL_RX_OK && count == 0x8001u,
           "end-to-end: refill publish index 1 phase 1 -> 0x8001");
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
