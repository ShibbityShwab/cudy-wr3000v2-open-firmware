#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""mkpatch.py - generate src/hcc-d2h-direct.patch (wifi-d2h-direct lane).

The direct-read D2H service is a new translation unit + header; it is wired
into the existing wifi-hcc-service module (the only host module that binds the
endpoint) with a small, reviewable patch.  This script reads the committed
wifi-hcc-service sources READ-ONLY, applies the exact hunks below in a scratch
copy, and writes a unified diff with a/ b/ prefixes so the lane build driver
can apply it with 'patch -p1' from the module source root.

No vendor byte is read or written here.  Usage:

    python3 tools/mkpatch.py            # rewrite ../src/hcc-d2h-direct.patch
    python3 tools/mkpatch.py --check    # apply + diff, exit 1 if it changed
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
REPO = os.path.abspath(os.path.join(LANE, "..", "..", "..", ".."))
HCC = os.path.join(REPO, "opensource", "docs", "incoming", "wifi-hcc-service",
                   "src")
OUT = os.path.join(LANE, "src", "hcc-d2h-direct.patch")

# Hunks: (file, old, new).  Each 'old' must appear exactly once.
EDITS = [
    # (A) include the direct-read contract header
    ("pciet_hcc_service.c",
     '#include "hi5622_pciet.h"\n#include "hi5622_hcc_service.h"\n',
     '#include "hi5622_pciet.h"\n#include "hi5622_hcc_service.h"\n'
     '#include "hi5622_d2h_direct.h"\n'),

    # (B) embed the direct-read state in the service state (by value)
    ("pciet_hcc_service.c",
     "\tstruct hcc_ring\t\tsr[HI5622_HCC_SR_CH_COUNT];\n"
     "\tstruct hcc_ring\t\tdr[HI5622_HCC_DR_CH_COUNT];\n",
     "\tstruct hcc_ring\t\tsr[HI5622_HCC_SR_CH_COUNT];\n"
     "\tstruct hcc_ring\t\tdr[HI5622_HCC_DR_CH_COUNT];\n"
     "\t/* direct-read device group-A D2H receive (engine-independent) */\n"
     "\tstruct hi5622_d2h_direct\trd;\n"),

    # (C) accessor: the hcc state is private to this file
    ("pciet_hcc_service.c",
     "static struct hi5622_pci *hcc_ctx(struct device *dev)\n"
     "{\n"
     "\treturn pci_get_drvdata(to_pci_dev(dev));\n"
     "}\n",
     "static struct hi5622_pci *hcc_ctx(struct device *dev)\n"
     "{\n"
     "\treturn pci_get_drvdata(to_pci_dev(dev));\n"
     "}\n"
     "\n"
     "/* Direct-read D2H accessor for pciet_d2h_direct.c (the hcc service\n"
     " * state is private to this file, so the direct path cannot dereference\n"
     " * ctx->hcc itself). */\n"
     "struct hi5622_d2h_direct *pciet_hcc_rd(struct hi5622_pci *ctx)\n"
     "{\n"
     "\treturn ctx && ctx->hcc ? &ctx->hcc->rd : NULL;\n"
     "}\n"),

    # (D) poll the device rings from the vendor-order service pass
    ("pciet_hcc_service.c",
     "\thcc_boot_watch(ctx);\n"
     "\thcc_cece_policy(ctx, bitmap);\n",
     "\thcc_boot_watch(ctx);\n"
     "\n"
     "\t/* Direct-read device group-A DR rings: the firmware stages HCC\n"
     "\t * frames in PKTRAM before (and without) the ETE engine copy, so the\n"
     "\t * host receive path reads them directly.  Read-only here. */\n"
     "\tpciet_d2h_poll(ctx, &h->rd, false);\n"
     "\n"
     "\thcc_cece_policy(ctx, bitmap);\n"),

    # (E) init the direct path in probe + create its attributes
    ("pciet_hcc_service.c",
     "\tmutex_init(&h->lock);\n"
     "\tctx->hcc = h;\n",
     "\tmutex_init(&h->lock);\n"
     "\tctx->hcc = h;\n"
     "\tpciet_d2h_init(ctx, &h->rd);\n"),

    ("pciet_hcc_service.c",
     "\tret = device_add_groups(dev, hcc_groups);\n"
     "\tif (ret) {\n"
     "\t\tdev_err(dev, \"HCC: device_add_groups: %d\\n\", ret);\n"
     "\t\tgoto err_free;\n"
     "\t}\n",
     "\tret = device_add_groups(dev, hcc_groups);\n"
     "\tif (ret) {\n"
     "\t\tdev_err(dev, \"HCC: device_add_groups: %d\\n\", ret);\n"
     "\t\tgoto err_free;\n"
     "\t}\n"
     "\n"
     "\t/* Direct-read D2H attributes (rd_poll/rd_advance/rd_status/\n"
     "\t * rd_decode/rd_catalog).  Best-effort: a failure leaves the\n"
     "\t * vendor-order service fully usable. */\n"
     "\tret = pciet_d2h_add_attrs(dev);\n"
     "\tif (ret)\n"
     "\t\tdev_warn(dev, \"D2H DIRECT: rd_* attributes unavailable (%d)\\n\",\n"
     "\t\t\t ret);\n"),

    # (F) remove the attributes
    ("pciet_hcc_service.c",
     "\tdevice_remove_groups(&ctx->pdev->dev, hcc_groups);\n"
     "\thcc_rings_free(ctx, h);\n",
     "\tpciet_d2h_del_attrs(&ctx->pdev->dev);\n"
     "\tdevice_remove_groups(&ctx->pdev->dev, hcc_groups);\n"
     "\thcc_rings_free(ctx, h);\n"),

    # (G) surface the direct path in hcc_ready (one extra block)
    ("pciet_hcc_service.c",
     "\tif (h->state == HCC_RS_READY)\n"
     "\t\thcc_printf(&o, \"VERDICT: READY - the vendor host would resume "
     "multi_chip_loading and open the H2D lane.",
     "\t{\n"
     "\t\tchar direct[256];\n"
     "\n"
     "\t\tif (pciet_d2h_ready_line(ctx, &h->rd, direct,\n"
     "\t\t\t\t\t  sizeof(direct)))\n"
     "\t\t\thcc_printf(&o, \"%s\", direct);\n"
     "\t}\n"
     "\tif (h->state == HCC_RS_READY)\n"
     "\t\thcc_printf(&o, \"VERDICT: READY - the vendor host would resume "
     "multi_chip_loading and open the H2D lane."),

    # (H) build the new translation unit into the same module
    ("Kbuild",
     "hi5622_pciet_hcc-y := pciet_main.o pciet_ete.o pciet_hcc_service.o\n",
     "hi5622_pciet_hcc-y := pciet_main.o pciet_ete.o pciet_hcc_service.o \\\n"
     "\t\t     pciet_d2h_direct.o\n"),
]


def apply_edits(tree):
    for name, old, new in EDITS:
        path = os.path.join(tree, name)
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        n = text.count(old)
        if n != 1:
            sys.exit("mkpatch: hunk anchor appears %d times in %s: %r" %
                     (n, name, old[:60]))
        with open(path, "w", encoding="utf-8") as f:
            f.write(text.replace(old, new, 1))


def main():
    check = "--check" in sys.argv
    if not os.path.isdir(HCC):
        sys.exit("mkpatch: wifi-hcc-service src not found: %s" % HCC)
    with tempfile.TemporaryDirectory() as tmp:
        base = os.path.join(tmp, "base")
        work = os.path.join(tmp, "work")
        subprocess.run(["cp", "-a", HCC, base], check=True)
        subprocess.run(["cp", "-a", HCC, work], check=True)
        apply_edits(work)
        # diff(1) has no label-prefix option; rewrite the paths so the patch
        # carries a/ b/ prefixes for 'patch -p1'.
        diff = subprocess.run(["diff", "-ruN", "base", "work"], cwd=tmp,
                              capture_output=True, text=True)
        if diff.returncode not in (0, 1):
            sys.exit("mkpatch: diff failed (%d): %s" %
                     (diff.returncode, diff.stderr));
        out = []
        for line in diff.stdout.splitlines(keepends=True):
            # strip both the base/work prefix and the volatile mtime so the
            # patch is deterministic across runs
            if line.startswith("--- base/"):
                out.append("--- a/" + line[len("--- base/"):].split("\t")[0] +
                           "\n")
            elif line.startswith("+++ work/"):
                out.append("+++ b/" + line[len("+++ work/"):].split("\t")[0] +
                           "\n")
            elif line.startswith("diff -ruN "):
                continue
            else:
                out.append(line)
        patch = "".join(out)
    if not patch:
        sys.exit("mkpatch: no changes produced (already patched?)")
    if check:
        with open(OUT, "r", encoding="utf-8") as f:
            if f.read() != patch:
                sys.exit("mkpatch --check: src/hcc-d2h-direct.patch is stale")
        print("mkpatch --check: patch up to date")
        return
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(patch)
    print("wrote %s (%d bytes, %d file(s))" %
          (OUT, len(patch), patch.count("--- a/")))


if __name__ == "__main__":
    main()