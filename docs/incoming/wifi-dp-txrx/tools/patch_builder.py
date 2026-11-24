#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
patch_builder.py - reproduce the wifi-dp-txrx kernel integration patch
(lane task-122) against the wifi-datapath-bringup source tree.

The lane does not edit the bringup lane in place.  This tool copies nothing by
itself; it edits a scratch COPY of that tree (--work DIR must contain src/) and
can emit the exact unified diff for review:

    python3 tools/patch_builder.py --work /tmp/wifi-dp-txrx-build \
        --emit-patch patches/0001-hi5622-dp-txrx.patch

The patch adds only: the hi5622_dp_txrx include, the bind call at the end of a
successful dp_setup, the release calls, four device_attribute objects registered
in the existing dp group, and the two new Kbuild objects.  All TX/RX logic lives
in this lane's src/ files.
"""
import argparse
import difflib
import os
import sys


def edit(work, name, edits, orig):
    path = os.path.join(work, "src", name)
    before = orig.get(name)
    if before is None:
        before = open(path).read()
    s = before
    for old, new in edits:
        if s.count(old) != 1:
            raise SystemExit("FATAL: anchor not unique in %s:" % name)
        s = s.replace(old, new, 1)
    open(path, "w").write(s)
    return before, s


def emit_patch(work, orig, out_path):
    chunks = []
    for name in ("hi5622_dp.c", "Kbuild"):
        a = orig[name].splitlines(True)
        b = open(os.path.join(work, "src", name)).read().splitlines(True)
        diff = difflib.unified_diff(a, b,
                                    fromfile="a/src/" + name,
                                    tofile="b/src/" + name)
        chunks.extend(diff)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    open(out_path, "w").writelines(chunks)
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", required=True,
                    help="scratch tree with src/ copied from wifi-datapath-bringup")
    ap.add_argument("--emit-patch", default=None,
                    help="write the unified diff to this file")
    args = ap.parse_args()
    work = args.work
    src = os.path.join(work, "src")
    if not os.path.isdir(src):
        raise SystemExit("FATAL: %s/src missing" % work)
    orig = {}
    for name in ("hi5622_dp.c", "Kbuild"):
        orig[name] = open(os.path.join(src, name)).read()

    edit(work, "hi5622_dp.c", [
        ('#include "dp_rings.h"\n',
         '#include "dp_rings.h"\n#include "hi5622_dp_txrx.h"\n'),
        ('''    dp_snapshot_mac(ctx);
    dp->setup_done = true;
    mutex_unlock(&dp->lock);''',
         '''    dp_snapshot_mac(ctx);
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
    mutex_unlock(&dp->lock);'''),
        ('fail:\n    dp->failed = true;\n    dp->last_err = (u32)(-rc);\n    dp_free_rings(ctx);',
         'fail:\n    dp->failed = true;\n    dp->last_err = (u32)(-rc);\n    hi5622_dp_txrx_release(ctx);\n    dp_free_rings(ctx);'),
        ('    if (!ctx || !ctx->dp)\n        return;\n    dp_free_rings(ctx);\n    ctx->dp = NULL;',
         '    if (!ctx || !ctx->dp)\n        return;\n    hi5622_dp_txrx_release(ctx);\n    dp_free_rings(ctx);\n    ctx->dp = NULL;'),
        ('static struct attribute *hi5622_dp_attrs[] = {\n    &dev_attr_dp_probe.attr,',
         '''/* wifi-dp-txrx lane (task-122): handler prototypes come from
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
    &dev_attr_dp_probe.attr,'''),
        ('    &dev_attr_dp_rings.attr,\n    NULL,\n};',
         '''    &dev_attr_dp_rings.attr,
    &dev_attr_dp_tx_frame.attr,
    &dev_attr_dp_tx_status.attr,
    &dev_attr_dp_rx_poll.attr,
    &dev_attr_dp_rx_capture.attr,
    NULL,
};'''),
    ], orig)

    edit(work, "Kbuild", [
        ("\t\t dp_rings.o hi5622_dp.o\n",
         "\t\t dp_rings.o hi5622_dp.o \\\n\t\t dp_txrx.o hi5622_dp_txrx.o\n"),
    ], orig)
    print("patch_builder: edits applied to %s" % src)

    if args.emit_patch:
        out = emit_patch(work, orig, args.emit_patch)
        print("patch_builder: wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
