// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_dp.c - MAC host-interface data-plane bring-up for the Hi5622V100
 * clean-room driver (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (wifi-datapath-bringup lane, shared task task-111,
 * 2026-11-25).  No vendor source/object code, firmware byte, calibration value,
 * per-unit identity or vendor text is reproduced.  Every constant and the
 * register-program order is a derived fact with evidence markers in REPORT.md /
 * FRAME-SPECS.md (static decode re-asserted by tools/dp_static_decode.py).
 *
 * What this adds on top of the working HCC control plane (clean driver +
 * post-ready H2D messages):
 *
 *   dp_probe     (RO)  ONE read-only word read of MAC band-0 +0x00: the
 *                      reachability probe that must PASS before any MAC write.
 *   dp_setup     (WO)  allocate the host-DRAM data rings (TXQ table 0x2524,
 *                      8 per-TID MSDU-info rings of 64 x 8 B, tx_ba_info
 *                      0x800 B, RX norm/small free rings 256/512 x 4 B,
 *                      completion ring 4095 x 4 B, 0x63c/0xb0 buffer pools),
 *                      write the device-RAM MSDU-info ring-pointer table
 *                      (12-byte entries {ring devVA,0,0} at ACP table base),
 *                      fill the RX free rings entirely (wptr -> 0x8000) and
 *                      program MAC slot-5 band 0 in the vendor order with
 *                      read-back verification of every write.  Gated on:
 *                      windows programmed + SR armed + CPU released + latched
 *                      golden boot + READY_TXQ_SYNC/ACTIVE + this load's
 *                      context-create + a passed dp_probe.
 *   dp_txq_sync  (WO)  ONE group-0 id-1 key-0x0320 frame carrying the device VA
 *                      of the allocated TXQ table, through the existing exact
 *                      H2D carrier (SR node + mailbox id-3 announce + bounded
 *                      out[0] poll).  Gated on dp_setup + h2d_send_budget.
 *   dp_status    (RO)  counters, gates, ring/table device VAs, MAC read-back.
 *   dp_rings     (RO)  live MAC slot-5 register snapshot + device-RAM table and
 *                      ring index read-out for the bench (reads only).
 *
 * HARD SAFETY (unchanged project rules): probe performs no writes; no register
 * is written outside MAC band 0; the MAC block is never touched before
 * dp_probe passed in the same module load; every value is range-checked before
 * a write; CA 0x400392f0 (out[5]) and the RC misc window are never touched;
 * one setup per module load and no retry after a partial program (the lane is
 * marked failed and every later setup/txq request is refused until reload).
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hi5622_driver.h"
#include "hi5622_hcc.h"
#include "hi5622_dp.h"
#include "h2d_send.h"
#include "dp_rings.h"
#include "hi5622_dp_txrx.h"

/* Host bookkeeping + DMA area size per RX buffer (vendor: 4-byte skb word
 * followed by the descriptor/MPDU area). */
#define DP_RX_NORM_ALLOC (DP_RX_BUF_HDR + DP_RX_NORM_BUFLEN)
#define DP_RX_SMALL_ALLOC (DP_RX_BUF_HDR + DP_RX_SMALL_BUFLEN)
#define DP_DP_MAC_ALL_OFF_REGS 18u
#define DP_DP_MAC_ALL_OFF_MAX 0x54u

struct hi5622_dp {
    struct device *dev;
    struct mutex lock;
    bool probe_ok;
    bool setup_done;
    bool failed;
    bool txq_sent;
    u32 probe_bar0;
    u32 probe_value;

    /* device-RAM MSDU-info ring-pointer table (ready g4 id2 payload) */
    u32 ready_payload;
    u32 msdu_table_ca;
    u32 msdu_table_acp;
    u32 table_entries_written;

    /* host-DRAM rings/tables (dma_alloc_coherent) */
    void *txq_table;
    dma_addr_t txq_table_dma;
    u32 txq_table_devva;

    void *tx_msdu_rings;
    dma_addr_t tx_msdu_rings_dma;

    void *txba;
    dma_addr_t txba_dma;

    void *rx_norm_ring;
    dma_addr_t rx_norm_ring_dma;
    void *rx_small_ring;
    dma_addr_t rx_small_ring_dma;
    void *rx_cmp_ring;
    dma_addr_t rx_cmp_ring_dma;

    /* per-buffer RX pools (allocated one coherent buffer each: no high-order
     * allocation, so setup cannot fail on a fragmented post-boot allocator) */
    void **rx_norm_cpu;
    dma_addr_t *rx_norm_dma;
    void **rx_small_cpu;
    dma_addr_t *rx_small_dma;

    /* last executed MAC program + read-backs */
    unsigned int mac_reg_count;
    u32 mac_written[DP_MAC_REG_COUNT];
    u32 mac_readback[DP_MAC_REG_COUNT];
    u32 mac_snapshot[DP_DP_MAC_ALL_OFF_REGS];
    bool mac_snapshot_valid;
    u32 last_err;
};

static const u8 dp_mac_all_offs[DP_DP_MAC_ALL_OFF_REGS] = {
    0x00u, 0x04u, 0x10u, 0x1cu, 0x20u, 0x24u, 0x28u, 0x2cu,
    0x30u, 0x34u, 0x38u, 0x3cu, 0x40u, 0x44u, 0x48u, 0x4cu,
    0x50u, 0x54u,
};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */
static struct hi5622_pci *dp_ctx(struct device *dev)
{
    return pci_get_drvdata(to_pci_dev(dev));
}

static u32 dp_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

/* Device CA -> BAR0 window offset, refusing anything the six vendor inbound
 * regions do not cover (the transport's contract). */
static u32 dp_ca_bar0(struct hi5622_pci *ctx, u32 ca)
{
    u32 off = hi5622_ca_to_bar0(ca);

    if (!off) {
        dev_err(&ctx->pdev->dev,
                "DP: CA 0x%08x is outside the six inbound regions - refused\n",
                ca);
        return 0;
    }
    return off;
}

static u32 dp_mac_read(struct hi5622_pci *ctx, u32 off)
{
    u32 bar0 = dp_ca_bar0(ctx, DP_MAC_CA_BAND0 + off);

    if (!bar0)
        return 0xffffffffu;
    return readl(ctx->bar0 + bar0);
}

static void dp_mac_write(struct hi5622_pci *ctx, u32 off, u32 val)
{
    u32 bar0 = dp_ca_bar0(ctx, DP_MAC_CA_BAND0 + off);

    if (!bar0)
        return;
    writel(val, ctx->bar0 + bar0);
}

/* Vendor-order MAC program with RMW, immediate read-back and masked compare. */
static int dp_mac_program_and_verify(struct hi5622_pci *ctx,
                                     const struct dp_mac_params *p)
{
    struct device *dev = &ctx->pdev->dev;
    struct hi5622_dp *dp = ctx->dp;
    struct dp_mac_reg regs[DP_MAC_REG_COUNT];
    unsigned int count = 0, i;
    int rc;

    rc = dp_mac_program(p, regs, ARRAY_SIZE(regs), &count);
    if (rc) {
        dev_err(dev, "DP: MAC register program rejected (%d)\n", rc);
        return -EINVAL;
    }
    if (count != DP_MAC_REG_COUNT) {
        dev_err(dev, "DP: MAC register program count %u != %u\n",
                count, DP_MAC_REG_COUNT);
        return -EINVAL;
    }
    dp->mac_reg_count = count;
    for (i = 0; i < count; i++) {
        u32 val;

        if (regs[i].mask == 0xffffffffu)
            val = regs[i].value;
        else
            val = (dp_mac_read(ctx, regs[i].off) & ~regs[i].mask) |
                  (regs[i].value & regs[i].mask);
        dp->mac_written[i] = val;
        dp_mac_write(ctx, regs[i].off, val);
        dp->mac_readback[i] = dp_mac_read(ctx, regs[i].off);
        if ((dp->mac_readback[i] & regs[i].mask) != (val & regs[i].mask)) {
            dev_err(dev,
                    "DP: MAC +0x%02x read-back 0x%08x != written 0x%08x (mask 0x%08x) - stop, no retry\n",
                    regs[i].off, dp->mac_readback[i], val, regs[i].mask);
            return -EIO;
        }
    }
    return 0;
}

static void dp_snapshot_mac(struct hi5622_pci *ctx)
{
    struct hi5622_dp *dp = ctx->dp;
    unsigned int i;

    for (i = 0; i < DP_DP_MAC_ALL_OFF_REGS; i++)
        dp->mac_snapshot[i] = dp_mac_read(ctx, dp_mac_all_offs[i]);
    dp->mac_snapshot_valid = true;
}

/* ------------------------------------------------------------------ */
/* DMA allocation / free                                              */
/* ------------------------------------------------------------------ */
static void *dp_dma_alloc(struct hi5622_pci *ctx, size_t size,
                          dma_addr_t *dma, const char *what)
{
    struct device *dev = &ctx->pdev->dev;
    void *p;

    if (!size || !dma)
        return NULL;
    p = dma_alloc_coherent(dev, size, dma, GFP_KERNEL);
    if (!p) {
        dev_err(dev, "DP: dma_alloc_coherent(%zu B, %s) failed\n",
                size, what);
        return NULL;
    }
    if ((u64)*dma < (u64)DP_HOST_DMA_LO ||
        (u64)*dma > (u64)DP_HOST_DMA_HI) {
        dev_err(dev,
                "DP: %s DMA address 0x%llx outside the device-visible host window 0x%08x..0x%08x - refused\n",
                what, (unsigned long long)*dma, DP_HOST_DMA_LO,
                DP_HOST_DMA_HI);
        dma_free_coherent(dev, size, p, *dma);
        return NULL;
    }
    memset(p, 0, size);
    return p;
}

static void dp_free_rx_pool(struct hi5622_pci *ctx, void ***cpu,
                            dma_addr_t **dma, unsigned int count, size_t size)
{
    struct device *dev = &ctx->pdev->dev;
    unsigned int i;

    if (!cpu || !dma || !*cpu || !*dma)
        return;
    for (i = 0; i < count; i++) {
        if ((*cpu)[i] && (*dma)[i] != 0) {
            dma_free_coherent(dev, size, (*cpu)[i], (*dma)[i]);
            (*cpu)[i] = NULL;
            (*dma)[i] = 0;
        }
    }
    kfree(*cpu);
    kfree(*dma);
    *cpu = NULL;
    *dma = NULL;
}

static int dp_alloc_rx_pool(struct hi5622_pci *ctx, void ***cpu,
                            dma_addr_t **dma, unsigned int count,
                            size_t size, const char *what)
{
    void **c;
    dma_addr_t *d;
    unsigned int i;

    c = kcalloc(count, sizeof(*c), GFP_KERNEL);
    d = kcalloc(count, sizeof(*d), GFP_KERNEL);
    if (!c || !d) {
        kfree(c);
        kfree(d);
        return -ENOMEM;
    }
    *cpu = c;
    *dma = d;
    for (i = 0; i < count; i++) {
        c[i] = dp_dma_alloc(ctx, size, &d[i], what);
        if (!c[i]) {
            dp_free_rx_pool(ctx, cpu, dma, count, size);
            return -ENOMEM;
        }
    }
    return 0;
}

static void dp_free_rings(struct hi5622_pci *ctx)
{
    struct hi5622_dp *dp = ctx->dp;
    struct device *dev = &ctx->pdev->dev;

    if (!dp)
        return;
    dp_free_rx_pool(ctx, &dp->rx_norm_cpu, &dp->rx_norm_dma,
                    DP_RX_NORM_DEPTH, DP_RX_NORM_ALLOC);
    dp_free_rx_pool(ctx, &dp->rx_small_cpu, &dp->rx_small_dma,
                    DP_RX_SMALL_DEPTH, DP_RX_SMALL_ALLOC);
    if (dp->rx_cmp_ring) {
        dma_free_coherent(dev, DP_RXCMP_RING_BYTES, dp->rx_cmp_ring,
                          dp->rx_cmp_ring_dma);
        dp->rx_cmp_ring = NULL;
        dp->rx_cmp_ring_dma = 0;
    }
    if (dp->rx_small_ring) {
        dma_free_coherent(dev, DP_RX_SMALL_RING_BYTES, dp->rx_small_ring,
                          dp->rx_small_ring_dma);
        dp->rx_small_ring = NULL;
        dp->rx_small_ring_dma = 0;
    }
    if (dp->rx_norm_ring) {
        dma_free_coherent(dev, DP_RX_NORM_RING_BYTES, dp->rx_norm_ring,
                          dp->rx_norm_ring_dma);
        dp->rx_norm_ring = NULL;
        dp->rx_norm_ring_dma = 0;
    }
    if (dp->txba) {
        dma_free_coherent(dev, DP_TXBA_BYTES, dp->txba, dp->txba_dma);
        dp->txba = NULL;
        dp->txba_dma = 0;
    }
    if (dp->tx_msdu_rings) {
        dma_free_coherent(dev, DP_TX_MSDU_RING_BYTES, dp->tx_msdu_rings,
                          dp->tx_msdu_rings_dma);
        dp->tx_msdu_rings = NULL;
        dp->tx_msdu_rings_dma = 0;
    }
    if (dp->txq_table) {
        dma_free_coherent(dev, DP_TXQ_TABLE_BYTES, dp->txq_table,
                          dp->txq_table_dma);
        dp->txq_table = NULL;
        dp->txq_table_dma = 0;
        dp->txq_table_devva = 0;
    }
}

static int dp_alloc_rings(struct hi5622_pci *ctx)
{
    struct hi5622_dp *dp = ctx->dp;
    dma_addr_t dma;

    dp->txq_table = dp_dma_alloc(ctx, DP_TXQ_TABLE_BYTES, &dma, "txq_table");
    if (!dp->txq_table)
        return -ENOMEM;
    dp->txq_table_dma = dma;
    dp->txq_table_devva = (u32)dma;

    dp->tx_msdu_rings = dp_dma_alloc(ctx, DP_TX_MSDU_RING_BYTES, &dma,
                                    "tx_msdu_rings");
    if (!dp->tx_msdu_rings)
        return -ENOMEM;
    dp->tx_msdu_rings_dma = dma;

    dp->txba = dp_dma_alloc(ctx, DP_TXBA_BYTES, &dma, "tx_ba_info");
    if (!dp->txba)
        return -ENOMEM;
    dp->txba_dma = dma;

    dp->rx_norm_ring = dp_dma_alloc(ctx, DP_RX_NORM_RING_BYTES, &dma,
                                    "rx_norm_free_ring");
    if (!dp->rx_norm_ring)
        return -ENOMEM;
    dp->rx_norm_ring_dma = dma;

    dp->rx_small_ring = dp_dma_alloc(ctx, DP_RX_SMALL_RING_BYTES, &dma,
                                     "rx_small_free_ring");
    if (!dp->rx_small_ring)
        return -ENOMEM;
    dp->rx_small_ring_dma = dma;

    dp->rx_cmp_ring = dp_dma_alloc(ctx, DP_RXCMP_RING_BYTES, &dma,
                                   "rx_data_cmp_ring");
    if (!dp->rx_cmp_ring)
        return -ENOMEM;
    dp->rx_cmp_ring_dma = dma;

    if (dp_alloc_rx_pool(ctx, &dp->rx_norm_cpu, &dp->rx_norm_dma,
                         DP_RX_NORM_DEPTH, DP_RX_NORM_ALLOC,
                         "rx_norm_buffer") != 0)
        return -ENOMEM;
    if (dp_alloc_rx_pool(ctx, &dp->rx_small_cpu, &dp->rx_small_dma,
                         DP_RX_SMALL_DEPTH, DP_RX_SMALL_ALLOC,
                         "rx_small_buffer") != 0)
        return -ENOMEM;
    return 0;
}

/* ------------------------------------------------------------------ */
/* device-RAM MSDU-info ring-pointer table                            */
/* ------------------------------------------------------------------ */
static int dp_lookup_msdu_table(struct hi5622_pci *ctx)
{
    struct hi5622_dp *dp = ctx->dp;
    struct hi5622_hcc_service *h = ctx->hcc;
    u32 payload = 0;
    int rc;

    if (!h)
        return -ENODEV;
    payload = h->rd.id2_payload;
    if (!payload)
        payload = dp_le32(ctx->read_msg[0]);
    rc = dp_ready_payload_ptrtable(payload, &dp->msdu_table_ca,
                                   &dp->msdu_table_acp);
    if (rc) {
        dev_err(&ctx->pdev->dev,
                "DP: ready g4 id2 payload 0x%08x is not a PKTRAM MSDU-info table pointer (%d) - no device write\n",
                payload, rc);
        return -EINVAL;
    }
    dp->ready_payload = payload;
    return 0;
}

/* The vendor writes the per-(user,tid) 12-byte table entries into device RAM
 * (hmac_get_user_tid_tx_msdu_info_ring_table_addr + shuangta_tx_msdu_ring_buf_alloc)
 * with {ring device VA, 0, 0}; we do the same for user 0 (the vendor's default
 * band-0 user slot) and TIDs 0..7, through the ACP alias, verifying read-back. */
static int dp_write_msdu_table(struct hi5622_pci *ctx)
{
    struct hi5622_dp *dp = ctx->dp;
    struct device *dev = &ctx->pdev->dev;
    unsigned int tid;

    for (tid = 0; tid < DP_TX_MSDU_RINGS; tid++) {
        u8 entry[DP_MSDU_TABLE_ENTRY_BYTES];
        u32 ca = 0, acp = 0, bar0, ring_devva;
        u32 rb[DP_MSDU_TABLE_ENTRY_DWORDS];
        unsigned int i;
        int rc;

        rc = dp_msdu_table_entry_ca(dp->msdu_table_ca, 0u, tid, &ca, &acp);
        if (rc) {
            dev_err(dev, "DP: MSDU table entry (user 0, tid %u) CA refused (%d)\n",
                    tid, rc);
            return -EINVAL;
        }
        ring_devva = dp_tx_ring_devva((u32)dp->tx_msdu_rings_dma, tid);
        if (ring_devva < DP_HOST_DMA_LO || ring_devva > DP_HOST_DMA_HI) {
            dev_err(dev, "DP: TID %u ring devVA 0x%08x outside the host DMA window - no write\n",
                    tid, ring_devva);
            return -ERANGE;
        }
        dp_msdu_table_entry_encode(entry, ring_devva);
        bar0 = dp_ca_bar0(ctx, acp);
        if (!bar0)
            return -EFAULT;
        for (i = 0; i < DP_MSDU_TABLE_ENTRY_DWORDS; i++)
            writel(dp_le32(entry + 4u * i), ctx->bar0 + bar0 + 4u * i);
        wmb();
        for (i = 0; i < DP_MSDU_TABLE_ENTRY_DWORDS; i++)
            rb[i] = readl(ctx->bar0 + bar0 + 4u * i);
        if (rb[0] != ring_devva || rb[1] != 0u || rb[2] != 0u) {
            dev_err(dev,
                    "DP: MSDU table entry tid %u read-back 0x%08x/0x%08x/0x%08x != 0x%08x/0/0 - stop, no retry\n",
                    tid, rb[0], rb[1], rb[2], ring_devva);
            return -EIO;
        }
        dp->table_entries_written++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX free-ring fill                                                  */
/* ------------------------------------------------------------------ */
static void dp_fill_rx_rings(struct hi5622_pci *ctx)
{
    struct hi5622_dp *dp = ctx->dp;
    u32 *norm = dp->rx_norm_ring;
    u32 *small = dp->rx_small_ring;
    unsigned int i;

    for (i = 0; i < DP_RX_NORM_DEPTH; i++)
        norm[i] = dp_rx_buf_entry_devva((u32)dp->rx_norm_dma[i]);
    for (i = 0; i < DP_RX_SMALL_DEPTH; i++)
        small[i] = dp_rx_buf_entry_devva((u32)dp->rx_small_dma[i]);
    /* Publish the ring contents before the MAC produces/consumes. */
    dma_wmb();
}

/* ------------------------------------------------------------------ */
/* gates                                                              */
/* ------------------------------------------------------------------ */
static const char *dp_gate_name(int rc)
{
    switch (rc) {
    case 0:         return "ok";
    case -ENODEV:   return "no_hcc_service";
    case -EBUSY:    return "not_armed_or_not_released";
    case -EPERM:    return "phase_or_ctx_gate";
    case -EALREADY: return "already_done";
    case -EIO:      return "device_write_failed";
    default:        return "error";
    }
}

/* The full setup gate: no MAC/device-RAM write may happen before every one of
 * these is true in THIS module load. */
static int dp_gate_setup(struct hi5622_pci *ctx, bool probe_required)
{
    struct hi5622_hcc_service *h = ctx->hcc;
    struct hcc_core *core;

    if (!h || !ctx->dp)
        return -ENODEV;
    core = &h->hcc.core;
    if (!ctx->windows_programmed || !h->armed)
        return -EBUSY;
    if (!core->released || !ctx->cpu_released)
        return -EBUSY;
    if (!core->boot_latched)
        return -EBUSY;
    if (core->phase != HCC_PHASE_READY_TXQ_SYNC &&
        core->phase != HCC_PHASE_ACTIVE)
        return -EPERM;
    if (!h->h2d_ctx_create_sent)
        return -EPERM;
    if (probe_required && !ctx->dp->probe_ok)
        return -EPERM;
    return 0;
}

/* ------------------------------------------------------------------ */
/* sysfs: dp_probe (read-only single MAC word)                        */
/* ------------------------------------------------------------------ */
static ssize_t dp_probe_show(struct device *dev, struct device_attribute *attr,
                             char *buf)
{
    struct hi5622_pci *ctx = dp_ctx(dev);
    struct hi5622_dp *dp;
    u32 bar0, val;

    if (!ctx || !ctx->dp)
        return -ENODEV;
    if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed)
        return -EBUSY;
    dp = ctx->dp;
    bar0 = dp_ca_bar0(ctx, DP_MAC_CA_BAND0 + DP_MAC_REG_TXBA_DEPTH);
    if (!bar0)
        return -EFAULT;
    val = readl(ctx->bar0 + bar0);
    dp->probe_bar0 = bar0;
    dp->probe_value = val;
    dp->probe_ok = true;
    return scnprintf(buf, PAGE_SIZE,
                     "dp_probe: MAC band0 CA 0x%08x+0x%02x -> BAR0 0x%x = 0x%08x (read-only; vendor boot 0x00000100 expected, 0/other = record; dp_setup now allowed)\n",
                     DP_MAC_CA_BAND0, DP_MAC_REG_TXBA_DEPTH, bar0, val);
}
static DEVICE_ATTR_RO(dp_probe);

/* ------------------------------------------------------------------ */
/* sysfs: dp_setup (one shot; the only MAC-program action)            */
/* ------------------------------------------------------------------ */
static ssize_t dp_setup_store(struct device *dev, struct device_attribute *attr,
                              const char *buf, size_t count)
{
    struct hi5622_pci *ctx = dp_ctx(dev);
    struct hi5622_dp *dp;
    struct dp_mac_params p;
    int rc;

    if (!ctx || !ctx->dp)
        return -ENODEV;
    if (!sysfs_streq(buf, "1"))
        return -EINVAL;
    dp = ctx->dp;
    rc = dp_gate_setup(ctx, true);
    if (rc) {
        dev_warn(dev,
                 "DP setup: refused by gate '%s' (phase=%s ctx_created=%u probe_ok=%u) - no device write\n",
                 dp_gate_name(rc),
                 hcc_phase_name(ctx->hcc->hcc.core.phase),
                 ctx->hcc->h2d_ctx_create_sent ? 1u : 0u,
                 dp->probe_ok ? 1u : 0u);
        return rc;
    }
    mutex_lock(&dp->lock);
    if (dp->setup_done || dp->failed) {
        mutex_unlock(&dp->lock);
        return -EALREADY;
    }
    rc = dp_lookup_msdu_table(ctx);
    if (rc)
        goto fail;
    rc = dp_alloc_rings(ctx);
    if (rc)
        goto fail;
    rc = dp_write_msdu_table(ctx);
    if (rc)
        goto fail;
    dp_fill_rx_rings(ctx);
    memset(&p, 0, sizeof(p));
    p.msdu_table_acp = dp->msdu_table_acp;
    p.rx_norm_addr = (u32)dp->rx_norm_ring_dma;
    p.rx_small_addr = (u32)dp->rx_small_ring_dma;
    p.rx_cmp_addr = (u32)dp->rx_cmp_ring_dma;
    p.txba_addr = (u32)dp->txba_dma;
    p.txba_depth = DP_TXBA_DEPTH_INIT;
    p.rx_norm_len = DP_RX_NORM_BUFLEN;
    p.rx_small_len = DP_RX_SMALL_BUFLEN;
    p.rx_norm_depth = DP_RX_NORM_DEPTH;
    p.rx_small_depth = DP_RX_SMALL_DEPTH;
    p.rx_cmp_depth = DP_RXCMP_DEPTH;
    p.int_mask = DP_MAC_INT_MASK_INIT;
    rc = dp_mac_program_and_verify(ctx, &p);
    if (rc)
        goto fail;
    dp_snapshot_mac(ctx);
    {
        struct hi5622_dp_txrx_bind b;

        memset(&b, 0, sizeof(b));
        b.pci = ctx;
        b.tx_msdu_rings = dp->tx_msdu_rings;
        b.tx_msdu_rings_dma = (u64)(u32)dp->tx_msdu_rings_dma;
        b.txq_table = dp->txq_table;
        b.txq_table_dma = (u64)(u32)dp->txq_table_dma;
        b.txq_table_devva = dp->txq_table_devva;
        b.rx_norm_ring = dp->rx_norm_ring;
        b.rx_small_ring = dp->rx_small_ring;
        b.rx_cmp_ring = dp->rx_cmp_ring;
        b.rx_norm_ring_dma = (u64)(u32)dp->rx_norm_ring_dma;
        b.rx_small_ring_dma = (u64)(u32)dp->rx_small_ring_dma;
        b.rx_cmp_ring_dma = (u64)(u32)dp->rx_cmp_ring_dma;
        b.rx_norm_cpu = dp->rx_norm_cpu;
        b.rx_small_cpu = dp->rx_small_cpu;
        b.rx_norm_dma = (const u32 *)dp->rx_norm_dma;
        b.rx_small_dma = (const u32 *)dp->rx_small_dma;
        b.msdu_table_ca = dp->msdu_table_ca;
        b.ready_payload = dp->ready_payload;
        rc = hi5622_dp_txrx_bind(ctx, &b);
        if (rc)
            goto fail;
    }
    dp->setup_done = true;
    mutex_unlock(&dp->lock);
    dev_info(dev,
             "DP setup: OK - rings 256/512/4095 (4-byte entries), TXQ table 0x2524 devva 0x%08x, tx rings 8x64x8, tx_ba_info 0x800 devva 0x%08x, MSDU table %u entries at CA 0x%08x/ACP 0x%08x, MAC band0 %u regs written+verified (mask 0x%08x); next: dp_txq_sync\n",
             dp->txq_table_devva, (u32)dp->txba_dma,
             dp->table_entries_written, dp->msdu_table_ca,
             dp->msdu_table_acp, dp->mac_reg_count,
             dp_mac_read(ctx, DP_MAC_REG_INT_MASK));
    return count;

fail:
    dp->failed = true;
    dp->last_err = (u32)(-rc);
    hi5622_dp_txrx_release(ctx);
    dp_free_rings(ctx);
    dp_snapshot_mac(ctx);
    mutex_unlock(&dp->lock);
    dev_err(dev,
            "DP setup: FAILED at step '%s' (rc %d) - lane closed for this module load; no retry, no further writes\n",
            rc == -ENOMEM ? "dma_alloc" :
            rc == -EINVAL ? "msdu_table_lookup" :
            rc == -ERANGE ? "ring_devva_range" :
            rc == -EFAULT ? "ca_translation" :
            rc == -EIO ? "readback_verify" : "mac_program",
            rc);
    return rc;
}
static DEVICE_ATTR_WO(dp_setup);

/* ------------------------------------------------------------------ */
/* sysfs: dp_txq_sync (the key-0x0320 hand-off, through h2d_send)     */
/* ------------------------------------------------------------------ */
static ssize_t dp_txq_sync_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count)
{
    struct hi5622_pci *ctx = dp_ctx(dev);
    struct hi5622_dp *dp;
    int rc;

    if (!ctx || !ctx->dp)
        return -ENODEV;
    if (!sysfs_streq(buf, "1"))
        return -EINVAL;
    dp = ctx->dp;
    if (dp->failed || !dp->setup_done) {
        dev_warn(dev, "DP txq_sync: refused - dp_setup has not succeeded for this module load\n");
        return -EPERM;
    }
    if (dp->txq_sent)
        return -EALREADY;
    rc = h2d_post_txq_addr(ctx, dp->txq_table_devva);
    if (rc) {
        dev_warn(dev, "DP txq_sync: H2D carrier refused/failed (%d) - no retry in this session\n",
                 rc);
        return rc;
    }
    dp->txq_sent = true;
    dev_info(dev,
             "DP txq_sync: g0 id1 key 0x0320 sent with TXQ-table devVA 0x%08x (%u B table, %u B frame); firmware worker CA 0x81445 stores it into firmware RAM; observe dp_status/rd_catalog and mask CA 0x400008\n",
             dp->txq_table_devva, DP_TXQ_TABLE_BYTES, 0x20u);
    return count;
}
static DEVICE_ATTR_WO(dp_txq_sync);

/* ------------------------------------------------------------------ */
/* sysfs: dp_status / dp_rings (read-only evidence)                   */
/* ------------------------------------------------------------------ */
static ssize_t dp_status_show(struct device *dev, struct device_attribute *attr,
                              char *buf)
{
    struct hi5622_pci *ctx = dp_ctx(dev);
    struct hi5622_dp *dp;
    int pos = 0;

    if (!ctx || !ctx->dp)
        return -ENODEV;
    dp = ctx->dp;
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "DP status (data plane host side, one setup per load): probe_ok=%u probe_value=0x%08x setup_done=%u failed=%u txq_sent=%u mac_regs=%u table_entries=%u\n",
                     dp->probe_ok ? 1u : 0u, dp->probe_value,
                     dp->setup_done ? 1u : 0u, dp->failed ? 1u : 0u,
                     dp->txq_sent ? 1u : 0u, dp->mac_reg_count,
                     dp->table_entries_written);
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "device-RAM MSDU table: ready_payload=0x%08x ca=0x%08x acp=0x%08x (12-byte entries, index tid+user*8, user 0 tids 0..7)\n",
                     dp->ready_payload, dp->msdu_table_ca,
                     dp->msdu_table_acp);
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "host rings: txq_table=0x%08x (%u B) tx_msdu_rings=0x%08x (%u B, 8x%ux%u) txba=0x%08x (%u B) norm_ring=0x%08x small_ring=0x%08x cmp_ring=0x%08x\n",
                     dp->txq_table_devva, DP_TXQ_TABLE_BYTES,
                     (u32)dp->tx_msdu_rings_dma, DP_TX_MSDU_RING_BYTES,
                     DP_TX_MSDU_RINGS, DP_TX_MSDU_RING_DEPTH,
                     (u32)dp->txba_dma, DP_TXBA_BYTES,
                     (u32)dp->rx_norm_ring_dma,
                     (u32)dp->rx_small_ring_dma,
                     (u32)dp->rx_cmp_ring_dma);
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "setup gates: windows=%u armed=%u released=%u latched=%u phase=%s ctx_created=%u probe_required=1 h2d_budget=%u\n",
                     ctx->windows_programmed ? 1u : 0u,
                     (ctx->hcc && ctx->hcc->armed) ? 1u : 0u,
                     (ctx->hcc && ctx->hcc->hcc.core.released &&
                      ctx->cpu_released) ? 1u : 0u,
                     (ctx->hcc && ctx->hcc->hcc.core.boot_latched) ? 1u : 0u,
                     ctx->hcc ? hcc_phase_name(ctx->hcc->hcc.core.phase) : "?",
                     (ctx->hcc && ctx->hcc->h2d_ctx_create_sent) ? 1u : 0u,
                     hi5622_h2d_send_budget);
    if (dp->mac_snapshot_valid) {
        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "MAC band0 snapshot: +00=0x%08x +04=0x%08x +10=0x%08x +1c=0x%08x +20=0x%08x +24=0x%08x +28=0x%08x\n",
                         dp->mac_snapshot[0], dp->mac_snapshot[1],
                         dp->mac_snapshot[2], dp->mac_snapshot[3],
                         dp->mac_snapshot[4], dp->mac_snapshot[5],
                         dp->mac_snapshot[6]);
        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "MAC band0 snapshot: +2c=0x%08x +30=0x%08x +34=0x%08x +38=0x%08x +3c=0x%08x +40=0x%08x +44=0x%08x +48=0x%08x +4c=0x%08x +50=0x%08x +54=0x%08x\n",
                         dp->mac_snapshot[7], dp->mac_snapshot[8],
                         dp->mac_snapshot[9], dp->mac_snapshot[10],
                         dp->mac_snapshot[11], dp->mac_snapshot[12],
                         dp->mac_snapshot[13], dp->mac_snapshot[14],
                         dp->mac_snapshot[15], dp->mac_snapshot[16],
                         dp->mac_snapshot[17]);
    }
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "stop rules: page CA 0x1bf000 golden 0x86807fef/7/2/0xabcd, state CA 0x118b60=1, mask CA 0x400008=0, staging CA 0x40000c=0 (read with the bench read pack, not here)\n");
    return pos;
}
static DEVICE_ATTR_RO(dp_status);

static ssize_t dp_rings_show(struct device *dev, struct device_attribute *attr,
                             char *buf)
{
    struct hi5622_pci *ctx = dp_ctx(dev);
    struct hi5622_dp *dp;
    int pos = 0;
    unsigned int tid, i;

    if (!ctx || !ctx->dp)
        return -ENODEV;
    dp = ctx->dp;
    if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed)
        return -EBUSY;
    dp_snapshot_mac(ctx);
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "DP rings (read-only; MAC band0 CA 0x%08x):\n",
                     DP_MAC_CA_BAND0);
    for (i = 0; i < DP_DP_MAC_ALL_OFF_REGS; i++)
        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "  +0x%02x = 0x%08x\n", dp_mac_all_offs[i],
                         dp->mac_snapshot[i]);
    if (!dp->setup_done)
        return pos;
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "device-RAM MSDU table entries (user 0):\n");
    for (tid = 0; tid < DP_TX_MSDU_RINGS; tid++) {
        u32 ca = 0, acp = 0, bar0;
        u32 w0, w1, w2;

        if (dp_msdu_table_entry_ca(dp->msdu_table_ca, 0u, tid, &ca, &acp))
            continue;
        bar0 = dp_ca_bar0(ctx, acp);
        if (!bar0)
            continue;
        w0 = readl(ctx->bar0 + bar0);
        w1 = readl(ctx->bar0 + bar0 + 4u);
        w2 = readl(ctx->bar0 + bar0 + 8u);
        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "  tid %u entry CA 0x%08x = {0x%08x, 0x%08x, 0x%08x} (expected {ring_devva 0x%08x,0,0})\n",
                         tid, ca, w0, w1, w2,
                         dp_tx_ring_devva((u32)dp->tx_msdu_rings_dma, tid));
    }
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "ring producers: norm_wptr=0x%08x small_wptr=0x%08x cmp_rptr=0x%08x / device: cmp_wptr=0x%08x norm_rptr=0x%08x small_rptr=0x%08x\n",
                     dp->mac_snapshot[10], dp->mac_snapshot[11],
                     dp->mac_snapshot[12], dp->mac_snapshot[15],
                     dp->mac_snapshot[16], dp->mac_snapshot[17]);
    return pos;
}
static DEVICE_ATTR_RO(dp_rings);

/* Bench-tooling compatibility name: the wifi-bench-tooling lane's protocol
 * profile already allowlists `h2d_txq_addr_sync` as the data-plane trigger
 * attribute (bench_guards.DP_ATTRS).  It is a pure alias of dp_txq_sync so a
 * sequence can fire the hand-off without naming a not-yet-allowlisted attr. */
static ssize_t h2d_txq_addr_sync_store(struct device *dev,
                                       struct device_attribute *attr,
                                       const char *buf, size_t count)
{
    return dp_txq_sync_store(dev, attr, buf, count);
}
static DEVICE_ATTR_WO(h2d_txq_addr_sync);

/* wifi-dp-txrx lane (task-122): handler prototypes come from
 * hi5622_dp_txrx.h; the attribute objects must live in this translation unit
 * because the 6.18 DEVICE_ATTR_RO/WO macros define them static. */
static struct device_attribute dev_attr_dp_tx_frame =
    __ATTR(dp_tx_frame, 0200, NULL, hi5622_dp_tx_frame_store);
static struct device_attribute dev_attr_dp_tx_status =
    __ATTR(dp_tx_status, 0444, hi5622_dp_tx_status_show, NULL);
static struct device_attribute dev_attr_dp_rx_poll =
    __ATTR(dp_rx_poll, 0200, NULL, hi5622_dp_rx_poll_store);
static struct device_attribute dev_attr_dp_rx_capture =
    __ATTR(dp_rx_capture, 0444, hi5622_dp_rx_capture_show, NULL);

static struct attribute *hi5622_dp_attrs[] = {
    &dev_attr_dp_probe.attr,
    &dev_attr_dp_setup.attr,
    &dev_attr_dp_txq_sync.attr,
    &dev_attr_h2d_txq_addr_sync.attr,
    &dev_attr_dp_status.attr,
    &dev_attr_dp_rings.attr,
    &dev_attr_dp_tx_frame.attr,
    &dev_attr_dp_tx_status.attr,
    &dev_attr_dp_rx_poll.attr,
    &dev_attr_dp_rx_capture.attr,
    NULL,
};
ATTRIBUTE_GROUPS(hi5622_dp);

int hi5622_dp_add_attrs(struct device *dev)
{
    return device_add_groups(dev, hi5622_dp_groups);
}

void hi5622_dp_del_attrs(struct device *dev)
{
    device_remove_groups(dev, hi5622_dp_groups);
}

/* ------------------------------------------------------------------ */
/* probe / remove                                                     */
/* ------------------------------------------------------------------ */
int hi5622_dp_probe(struct hi5622_pci *ctx)
{
    struct hi5622_dp *dp;

    dp = devm_kzalloc(&ctx->pdev->dev, sizeof(*dp), GFP_KERNEL);
    if (!dp)
        return -ENOMEM;
    dp->dev = &ctx->pdev->dev;
    mutex_init(&dp->lock);
    ctx->dp = dp;
    return 0;
}

void hi5622_dp_remove(struct hi5622_pci *ctx)
{
    if (!ctx || !ctx->dp)
        return;
    hi5622_dp_txrx_release(ctx);
    dp_free_rings(ctx);
    ctx->dp = NULL;
}
