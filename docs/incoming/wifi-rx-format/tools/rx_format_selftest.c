/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rx_format_selftest.c - host test for the portable MAC RX ring entry +
 * 36-byte per-buffer header model (lane wifi-rx-format, task-163).
 *
 * Strict C99, no kernel, no device, no vendor bytes.  Every ring/header
 * offset is asserted against its LITERAL number (not just the macro) so a
 * header/macro regression cannot make this test lie.
 *
 *   gcc -std=c99 -Wall -Wextra -Werror -O2 -o /tmp/rxfmt_selftest \
 *       src/hi5622_rx_format.c tools/rx_format_selftest.c
 *   /tmp/rxfmt_selftest
 */
#include <stdio.h>
#include <string.h>
#include "../src/hi5622_rx_format.h"

static int checks;
static int failures;

#define CK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void put_le16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void put_le32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static unsigned long get_le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int main(void)
{
    unsigned char buf[HIF_RX_NORM_ALLOC];
    unsigned char hdr[HIF_RX_HDR_BYTES];
    struct hif_rx_hdr out;
    hif_u32 v = 0, cookie = 0, fill = 0;
    hif_u8 kind = 0xffu;
    int rc;

    /* ---- 0. geometry literals (band 0 vendor contract) ------------ */
    CK(HIF_RX_ENTRY_BYTES == 4u);
    CK(HIF_RX_HOST_WORD == 4u);
    CK(HIF_RX_NORM_DEPTH == 256u);
    CK(HIF_RX_SMALL_DEPTH == 512u);
    CK(HIF_RX_CMP_DEPTH == 4095u);
    CK(HIF_RX_NORM_BUFLEN == 0x63cu);
    CK(HIF_RX_SMALL_BUFLEN == 0xb0u);
    CK(HIF_RX_NORM_ALLOC == 0x640u);
    CK(HIF_RX_SMALL_ALLOC == 0xb4u);
    CK(HIF_RX_HDR_BYTES == 0x24u);
    CK(HIF_RX_PAYLOAD_OFF == 0x28u);
    CK(HIF_RX_NORM_MPDU_MAX == 0x618u);
    CK(HIF_RX_SMALL_MPDU_MAX == 0x8cu);
    CK(HIF_RX_HDR_LEN == 0x1au);
    CK(HIF_RX_HDR_NEXT == 0x20u);
    CK(HIF_RX_HDR_SUBMSDU_NUM == 0x06u);
    CK(HIF_RX_HDR_STATUS == 0x07u);
    CK(HIF_RX_HDR_PPDU_ADDR == 0x00u);
    CK(HIF_RX_HDR_FCTRL == 0x12u);

    /* ---- 1. entry encode/decode ----------------------------------- */
    rc = hif_rx_entry_encode(0x844c7000u, &v);
    CK(rc == HIF_RX_OK);
    CK(v == 0x844c7004u);                 /* buffer+4, not the base */
    CK(v != 0x844c7000u);
    CK(hif_rx_entry_encode(0u, &v) == HIF_RX_EARG);
    CK(hif_rx_entry_encode(0x844c7001u, &v) == HIF_RX_EFORMAT);
    CK(hif_rx_entry_encode(0x844c7002u, &v) == HIF_RX_EFORMAT);
    CK(hif_rx_entry_encode(0x844c7000u, NULL) == HIF_RX_EARG);
    rc = hif_rx_entry_decode(0x844c7004u, &v);
    CK(rc == HIF_RX_OK && v == 0x844c7000u);
    CK(hif_rx_entry_decode(0x844c7003u, &v) == HIF_RX_EFORMAT);
    CK(hif_rx_entry_decode(4u, &v) == HIF_RX_EFORMAT); /* zero buffer */
    CK(hif_rx_entry_decode(0u, &v) == HIF_RX_EFORMAT);
    CK(hif_rx_entry_decode(0x844c7004u, NULL) == HIF_RX_EARG);

    /* ---- 2. completion-entry classification ----------------------- */
    rc = hif_rx_entry_classify(0x844c7004u, 0x844c7004u, 0x844e6c00u,
                               0x844e8000u, 0x84508000u, &kind);
    CK(rc == HIF_RX_OK && kind == 0u);
    rc = hif_rx_entry_classify(0x844e8004u, 0x844c7004u, 0x844e6c00u,
                               0x844e8000u, 0x84508000u, &kind);
    CK(rc == HIF_RX_OK && kind == 1u);
    CK(hif_rx_entry_classify(0x84480000u, 0x844c7004u, 0x844e6c00u,
                             0x844e8000u, 0x84508000u, &kind) == HIF_RX_ERANGE);
    CK(hif_rx_entry_classify(0x844c7005u, 0x844c7004u, 0x844e6c00u,
                             0x844e8000u, 0x84508000u, &kind) == HIF_RX_EFORMAT);
    CK(hif_rx_entry_classify(0u, 0x844c7004u, 0x844e6c00u, 0x844e8000u,
                             0x84508000u, &kind) == HIF_RX_EFORMAT);
    CK(hif_rx_entry_classify(0x844c7004u, 0, 0, 0, 0, NULL) == HIF_RX_EARG);

    /* ---- 3. host-word cookie -------------------------------------- */
    CK(hif_rx_cookie_pack(0u, 0u, &cookie) == HIF_RX_OK);
    CK((cookie & 0xffff0000u) == HIF_RX_COOKIE_MAGIC);
    CK((cookie & HIF_RX_COOKIE_KIND_BIT) == 0u);
    CK(hif_rx_cookie_pack(0u, 255u, &cookie) == HIF_RX_OK && cookie == 0x525800ffu);
    CK(hif_rx_cookie_pack(1u, 511u, &cookie) == HIF_RX_OK && cookie == 0x525881ffu);
    CK(hif_rx_cookie_pack(0u, 256u, &cookie) == HIF_RX_ERANGE);
    CK(hif_rx_cookie_pack(1u, 512u, &cookie) == HIF_RX_ERANGE);
    CK(hif_rx_cookie_pack(2u, 0u, &cookie) == HIF_RX_EARG);
    CK(hif_rx_cookie_pack(0u, 0u, NULL) == HIF_RX_EARG);
    CK(hif_rx_cookie_check(0x52580007u, 0u, 7u) == HIF_RX_OK);
    CK(hif_rx_cookie_check(0x525881ffu, 1u, 511u) == HIF_RX_OK);
    CK(hif_rx_cookie_check(0x52580007u, 1u, 7u) == HIF_RX_ESTALE);
    CK(hif_rx_cookie_check(0x52588007u, 0u, 7u) == HIF_RX_ESTALE);
    CK(hif_rx_cookie_check(0x00000007u, 0u, 7u) == HIF_RX_ESTALE);
    CK(hif_rx_cookie_check(0x52580008u, 0u, 7u) == HIF_RX_ESTALE);
    CK(hif_rx_cookie_check(0x52580007u, 2u, 7u) == HIF_RX_EFORMAT);

    /* ---- 4. buffer prepare / witness ------------------------------ */
    memset(buf, 0xaa, sizeof(buf));
    CK(hif_rx_buffer_prepare(buf, HIF_RX_NORM_ALLOC, 0u, 17u) == HIF_RX_OK);
    CK(get_le32(buf) == 0x52580011u);            /* cookie in the host word */
    CK(hif_rx_header_is_zero(buf + 4) == 1);
    CK(buf[4 + 0x1a] == 0u && buf[4 + 0x20] == 0u); /* len/next cleared */
    CK(buf[HIF_RX_PAYLOAD_OFF] == 0xaau);        /* payload area untouched */
    CK(buf[HIF_RX_HOST_WORD + HIF_RX_HDR_BYTES - 1] == 0u);
    CK(buf[HIF_RX_HOST_WORD + HIF_RX_HDR_BYTES] == 0xaau);
    CK(hif_rx_buffer_prepare(buf, HIF_RX_SMALL_ALLOC, 0u, 0u) == HIF_RX_EARG);
    CK(hif_rx_buffer_prepare(buf, HIF_RX_NORM_ALLOC, 1u, 0u) == HIF_RX_EARG);
    CK(hif_rx_buffer_prepare(buf, HIF_RX_NORM_ALLOC, 0u, 256u) == HIF_RX_ERANGE);
    CK(hif_rx_buffer_prepare(NULL, HIF_RX_NORM_ALLOC, 0u, 0u) == HIF_RX_EARG);

    CK(hif_rx_buffer_witness(buf, HIF_RX_NORM_ALLOC, 0u, 17u) == HIF_RX_BUF_UNTOUCHED);
    buf[4 + 0x1a] = 0x40u;                        /* "the MAC wrote a length" */
    CK(hif_rx_buffer_witness(buf, HIF_RX_NORM_ALLOC, 0u, 17u) == HIF_RX_BUF_WITNESS);
    buf[4 + 0x1a] = 0u;
    CK(hif_rx_buffer_witness(buf, HIF_RX_NORM_ALLOC, 0u, 17u) == HIF_RX_BUF_UNTOUCHED);
    buf[0] = 0u;                                  /* cookie destroyed */
    CK(hif_rx_buffer_witness(buf, HIF_RX_NORM_ALLOC, 0u, 17u) == HIF_RX_BUF_STALE);
    CK(hif_rx_buffer_witness(buf, HIF_RX_NORM_ALLOC, 0u, 18u) == HIF_RX_BUF_STALE);
    CK(hif_rx_buffer_witness(NULL, HIF_RX_NORM_ALLOC, 0u, 17u) == HIF_RX_BUF_BAD);
    CK(hif_rx_buffer_witness(buf, 0xdeadbeefu, 0u, 17u) == HIF_RX_BUF_BAD);
    CK(hif_rx_header_is_zero(NULL) == 0);

    /* a recycle must wipe a stale sub-MSDU chain pointer and length */
    memset(buf, 0, sizeof(buf));
    CK(hif_rx_buffer_prepare(buf, HIF_RX_NORM_ALLOC, 0u, 3u) == HIF_RX_OK);
    put_le32(buf + 4 + 0x20, 0x844c7124u);
    put_le16(buf + 4 + 0x1a, 1500u);
    CK(hif_rx_header_is_zero(buf + 4) == 0);
    CK(hif_rx_buffer_prepare(buf, HIF_RX_NORM_ALLOC, 0u, 3u) == HIF_RX_OK);
    CK(hif_rx_header_is_zero(buf + 4) == 1);

    /* ---- 5. device header parse (literal offsets) ----------------- */
    memset(hdr, 0, sizeof(hdr));
    put_le16(hdr + 0x1a, 100u);
    put_le32(hdr + 0x00, 0x85123404u);
    hdr[0x06] = 2u;
    hdr[0x07] = 0x10u;     /* status nibble 1 in bits7:4 */
    hdr[0x05] = 0x80u;
    put_le16(hdr + 0x12, 0x0080u);
    rc = hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, &out);
    CK(rc == HIF_RX_OK);
    CK(out.payload_len == 100u);
    CK(out.ppdu_devva == 0x85123404u);
    CK(out.next_devva == 0u);
    CK(out.submsdu_num == 2u);
    CK((out.status >> 4) == 1u);   /* the vendor's get_msdu_rx_status shape */
    CK(out.flags05 == 0x80u);
    CK(out.fctrl == 0x0080u);
    CK(hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, &out) == HIF_RX_OK);

    put_le16(hdr + 0x1a, 0u);
    CK(hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, &out) == HIF_RX_EFORMAT);
    put_le16(hdr + 0x1a, (unsigned)(HIF_RX_NORM_BUFLEN - 0x24u)); /* 0x618 */
    CK(hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, &out) == HIF_RX_OK);
    put_le16(hdr + 0x1a, (unsigned)(HIF_RX_NORM_BUFLEN - 0x24u + 1u));
    CK(hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, &out) == HIF_RX_EFORMAT);
    put_le16(hdr + 0x1a, 100u);
    put_le32(hdr + 0x20, 0x844c7104u);   /* aligned chain pointer is reported */
    rc = hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, &out);
    CK(rc == HIF_RX_OK && out.next_devva == 0x844c7104u);
    put_le32(hdr + 0x20, 0x844c7102u);   /* unaligned devVA: refused */
    CK(hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, &out) == HIF_RX_EFORMAT);
    put_le32(hdr + 0x20, 0u);
    put_le16(hdr + 0x1a, (unsigned)(HIF_RX_SMALL_BUFLEN - 0x24u + 1u));
    CK(hif_rx_hdr_parse(hdr, HIF_RX_SMALL_BUFLEN, &out) == HIF_RX_EFORMAT);
    put_le16(hdr + 0x1a, (unsigned)(HIF_RX_SMALL_BUFLEN - 0x24u)); /* 0x8c */
    CK(hif_rx_hdr_parse(hdr, HIF_RX_SMALL_BUFLEN, &out) == HIF_RX_OK);
    CK(hif_rx_hdr_parse(NULL, HIF_RX_NORM_BUFLEN, &out) == HIF_RX_EARG);
    CK(hif_rx_hdr_parse(hdr, HIF_RX_NORM_BUFLEN, NULL) == HIF_RX_EARG);
    CK(hif_rx_hdr_parse(hdr, 0x24u, &out) == HIF_RX_EARG);

    /* ---- 6. refill budget / reset guard --------------------------- */
    rc = hif_rx_refill_budget(256u, 224u, 32u, &fill);
    CK(rc == HIF_RX_OK && fill == 32u);   /* stock mirror: 256 posted, 32 moved */
    rc = hif_rx_refill_budget(256u, 0u, 256u, &fill);
    CK(rc == HIF_RX_OK && fill == 256u);  /* empty ring: one full lap */
    CK(hif_rx_refill_budget(256u, 224u, 33u, &fill) == HIF_RX_ERESET);
    CK(hif_rx_refill_budget(256u, 224u, 224u, &fill) == HIF_RX_ERESET);
    CK(hif_rx_refill_budget(256u, 0u, 257u, &fill) == HIF_RX_ERESET);
    CK(hif_rx_refill_budget(256u, 257u, 0u, &fill) == HIF_RX_ERANGE);
    CK(hif_rx_refill_budget(0u, 0u, 0u, &fill) == HIF_RX_EARG);
    CK(hif_rx_refill_budget(0x8000u, 0u, 0u, &fill) == HIF_RX_EARG);
    CK(hif_rx_refill_budget(256u, 0u, 0u, NULL) == HIF_RX_EARG);
    /* block-reset shape seen on the bench: prod index 0/phase 1, live rptr
     * jumped 0x20 -> 0; pending(prod, old)=224, delta=224 > 256-224 -> refuse */
    CK(hif_rx_refill_budget(256u, 224u, 224u, &fill) == HIF_RX_ERESET);

    /* ---- 7. vendor stock-mirror config words ---------------------- */
    CK(hif_rx_cfg_expect(0x1cu, &v) == 1 && v == 0x00b0063cu);
    CK(hif_rx_cfg_expect(0x2cu, &v) == 1 && v == 0x00000100u);
    CK(hif_rx_cfg_expect(0x30u, &v) == 1 && v == 0x00000200u);
    CK(hif_rx_cfg_expect(0x34u, &v) == 1 && v == 0x00000fffu);
    CK(hif_rx_cfg_expect(0x38u, &v) == 1 && v == 0x00008000u);
    CK(hif_rx_cfg_expect(0x3cu, &v) == 1 && v == 0x00008000u);
    CK(hif_rx_cfg_expect(0x40u, &v) == 1 && v == 0x00000000u);
    CK(hif_rx_cfg_expect(0x48u, &v) == 1 && v == 0xfffffcc1u);
    CK(HIF_RX_CFG_WORDS == 8u);
    CK(hif_rx_cfg_expect(0x20u, &v) == 0);   /* ring address: not a constant */
    CK(hif_rx_cfg_expect(0x1cu, NULL) == HIF_RX_EARG);

    /* ---- 8. a full synthetic publish/consume/recycle cycle -------- */
    {
        unsigned char pool[4][HIF_RX_NORM_ALLOC];
        unsigned long dma[4] = { 0x844c7000ul, 0x844c8000ul,
                                 0x844c9000ul, 0x844ca000ul };
        unsigned char ring[4 * HIF_RX_ENTRY_BYTES];
        unsigned long prod = 0x8000ul;  /* index 0, phase 1 (full fill) */
        unsigned i;

        memset(pool, 0, sizeof(pool));
        for (i = 0; i < 4u; i++) {
            CK(hif_rx_buffer_prepare(pool[i], HIF_RX_NORM_ALLOC, 0u, i) == HIF_RX_OK);
            CK(hif_rx_entry_encode((hif_u32)dma[i], &v) == HIF_RX_OK);
            put_le32(ring + i * 4u, v);
        }
        CK(get_le32(ring) == 0x844c7004ul);
        CK(get_le32(ring + 12) == 0x844ca004ul);
        /* device receives a frame in slot 2: it writes the header in place */
        put_le16(pool[2] + 4 + 0x1a, 60u);
        put_le32(pool[2] + 4 + 0x00, 0x85123404ul);
        CK(hif_rx_buffer_witness(pool[2], HIF_RX_NORM_ALLOC, 0u, 2u) == HIF_RX_BUF_WITNESS);
        CK(hif_rx_buffer_witness(pool[0], HIF_RX_NORM_ALLOC, 0u, 0u) == HIF_RX_BUF_UNTOUCHED);
        rc = hif_rx_hdr_parse(pool[2] + 4, HIF_RX_NORM_BUFLEN, &out);
        CK(rc == HIF_RX_OK && out.payload_len == 60u);
        /* recycle it: prepare again, entry re-encoded identically */
        CK(hif_rx_buffer_prepare(pool[2], HIF_RX_NORM_ALLOC, 0u, 2u) == HIF_RX_OK);
        CK(hif_rx_buffer_witness(pool[2], HIF_RX_NORM_ALLOC, 0u, 2u) == HIF_RX_BUF_UNTOUCHED);
        CK(hif_rx_entry_encode((hif_u32)dma[2], &v) == HIF_RX_OK && v == get_le32(ring + 8));
        (void)prod;
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
