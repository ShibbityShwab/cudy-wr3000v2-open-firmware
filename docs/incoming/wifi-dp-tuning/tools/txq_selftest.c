/* SPDX-License-Identifier: GPL-2.0 */
/*
 * txq_selftest.c - host-only selftest for the wifi-dp-tuning TXQ-table
 * queue-state publish model (shared task task-134).
 *
 * Why: the vendor host module (hi5622v100_wifi.ko 2.4.15) publishes per-TID
 * queue state into the 0x2524-byte host-DRAM TXQ table after every ring push
 * (hmac_tx_skb_pop_tid -> hmac_update_user_tid_txq_table).  The previous
 * clean-room lanes only published the ring producer index, leaving the table
 * zero.  These checks pin the derived layout with LITERAL offsets (not the
 * driver macros) so a macro regression cannot make the test lie, then check
 * the driver helper against the same image.
 *
 * The offsets are decoded from the local vendor artifact (not committed):
 *   table + 0x00 low16           publish clock (jiffies_to_msecs, u16)
 *   table + 0x524 + user*0x40    per-user block
 *     + tid*2                    u16 queued msdu count
 *     + 0x10 + tid*2             u16 start sequence number
 *     + 0x20 + tid*4             u32 queued bytes
 *   table + 0x10 + user          u8  TID non-empty bitmap
 * Users > 0x7f are refused (different vendor small block, out of scope).
 *
 * Build: cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I<staged-src> \
 *        txq_selftest.c <staged-src>/dp_txrx.c
 */
#include <stdio.h>
#include <string.h>

#include "dp_txrx.h"

#define TABLE_BYTES 0x2524u
#define FILL 0xA5u

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

static void fill(unsigned char *t)
{
    memset(t, FILL, TABLE_BYTES);
}

/* every byte outside the published fields must still be FILL */
static int untouched(const unsigned char *t, unsigned long off, unsigned long n)
{
    unsigned long i;

    for (i = 0; i < n; i++)
        if (t[off + i] != FILL)
            return 0;
    return 1;
}

/* zeroed-field variant: the driver stores only the published fields */
static int allzero(const unsigned char *t, unsigned long off, unsigned long n)
{
    unsigned long i;

    for (i = 0; i < n; i++)
        if (t[off + i] != 0u)
            return 0;
    return 1;
}

int main(void)
{
    unsigned char t[TABLE_BYTES];

    /* 1. per-user block arithmetic (literal vendor layout) */
    ck(dp_txrx_txq_user_block(0u) == 0x524u, "user 0 block = 0x524");
    ck(dp_txrx_txq_user_block(1u) == 0x564u, "user 1 block = 0x564");
    ck(dp_txrx_txq_user_block(0x7fu) == 0x24e4u, "user 0x7f block = 0x24e4");
    ck(dp_txrx_txq_user_block(0x80u) == DP_TXRX_TXQ_USER_MAX + 1u,
       "user 0x80 refused (alternate vendor block is out of scope)");

    /* 2. bitmap helper */
    ck(dp_txrx_txq_flags_set(0x00u, 0u, true) == 0x01u, "bitmap set bit0");
    ck(dp_txrx_txq_flags_set(0x00u, 3u, true) == 0x08u, "bitmap set bit3");
    ck(dp_txrx_txq_flags_set(0xffu, 0u, false) == 0xfeu, "bitmap clear bit0");
    ck(dp_txrx_txq_flags_set(0x42u, 8u, true) == 0x42u,
       "bitmap refuses tid 8 (no change)");

    /* 3. one publish: exact fields, nothing else touched.  The vendor
     * helper preserves the existing bitmap bits, so zero the owned fields
     * first to make the expected value exact. */
    fill(t);
    memset(t + 0x10, 0, 1);
    memset(t + 0x524, 0, 0x40);
    dp_txrx_txq_publish(t, 0u, 0u, 1u, 0x12u, true, 0x20u, 0xbeefu);
    ck(le16(t + 0x00u) == 0xbeefu, "publish: u16 clock at +0x00");
    ck(t[0x10] == 0x01u, "publish: user0 bitmap byte at +0x10 = 0x01");
    ck(le16(t + 0x524u + 0x00u) == 1u, "publish: tid0 count at 0x524");
    ck(le16(t + 0x524u + 0x10u) == 0x12u, "publish: tid0 start sn at 0x534");
    ck(le32(t + 0x524u + 0x20u) == 0x20u, "publish: tid0 bytes at 0x544");
    ck(untouched(t, 0x02u, 0x0eu), "publish: gap +0x02..+0x0f untouched");
    ck(untouched(t, 0x11u, 0x513u), "publish: gap +0x11..+0x523 untouched");
    ck(allzero(t, 0x526u, 0x0eu), "publish: count/sn gap zero");
    ck(allzero(t, 0x536u, 0x0eu), "publish: sn/bytes gap zero");
    ck(allzero(t, 0x548u, 0x1cu), "publish: block tail after tid0 zero");

    /* 4. independent TIDs + count clearing: bitmap is per user */
    dp_txrx_txq_publish(t, 0u, 3u, 1u, 9u, true, 40u, 1u);
    ck(t[0x10] == 0x09u, "two TIDs set bitmap 0x09");
    ck(le16(t + 0x524u + 6u) == 1u, "tid3 count at 0x52a");
    ck(le16(t + 0x524u + 0x10u + 6u) == 9u, "tid3 sn at 0x53a");
    ck(le32(t + 0x524u + 0x20u + 12u) == 40u, "tid3 bytes at 0x550");
    dp_txrx_txq_publish(t, 0u, 0u, 0u, 0u, false, 0u, 2u);
    ck(t[0x10] == 0x08u, "tid0 drain clears its bitmap bit only");
    ck(le16(t + 0x524u) == 0u, "drain publishes count 0");
    ck(le16(t + 0x524u + 0x10u) == 0x12u,
       "drain keeps the previous tid0 start sn (has_sn=false)");
    ck(le16(t + 0x00u) == 2u, "drain updates the clock");

    /* 5. last legal user block ends exactly at the table end */
    fill(t);
    memset(t + 0x10u + 0x7fu, 0, 1);
    dp_txrx_txq_publish(t, 0x7fu, 7u, 5u, 6u, true, 7u, 8u);
    ck(le16(t + 0x24e4u + 14u) == 5u, "user 0x7f tid7 count at 0x24f2");
    ck(le16(t + 0x24e4u + 0x10u + 14u) == 6u, "user 0x7f tid7 sn at 0x2502");
    ck(le32(t + 0x24e4u + 0x20u + 28u) == 7u,
       "user 0x7f tid7 bytes at 0x2520 (last 4 bytes of the table)");
    ck(t[0x10u + 0x7fu] == 0x80u, "user 0x7f bitmap byte at +0x8f");

    /* 6. refusals leave the image untouched */
    fill(t);
    dp_txrx_txq_publish(t, 0x80u, 0u, 1u, 1u, true, 1u, 1u);
    ck(untouched(t, 0u, TABLE_BYTES), "refuse user 0x80: table untouched");
    dp_txrx_txq_publish(t, 0u, 8u, 1u, 1u, true, 1u, 1u);
    ck(untouched(t, 0u, TABLE_BYTES), "refuse tid 8: table untouched");
    dp_txrx_txq_publish(NULL, 0u, 0u, 1u, 1u, true, 1u, 1u);
    ck(1, "NULL table refused without fault");

    /* 7. ring-index arithmetic used for the per-TID producer (depth 64) */
    {
        dp_u32 w = 0u;
        unsigned int i;

        for (i = 0; i < 64u; i++)
            w = dp_txrx_idx_next(w, 64u);
        ck(w == 0x8000u, "64 pushes wrap packed wptr to index 0 phase 1");
        w = dp_txrx_idx_next(0x8000u, 64u);
        ck(w == 0x8001u, "post-wrap push advances index with phase 1");
    }

    /* 8. table size as used by the driver */
    ck(DP_TXRX_TXQ_TABLE_BYTES == TABLE_BYTES, "driver table size 0x2524");
    ck(DP_TXRX_TXQ_USER_BLOCKS_OFF == 0x524u, "driver block base 0x524");

    printf("SUMMARY: %d passed, %d failed\n", checks - fails, fails);
    return fails == 0 ? 0 : 1;
}
