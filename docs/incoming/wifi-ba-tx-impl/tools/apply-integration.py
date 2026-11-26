#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""tools/apply-integration.py - lane wifi-ba-tx-impl (task-217)

Applies the BA TX-direction integration to a COPY of the pinned driver base
tree (the F2/R5 lineage; default /tmp/r5-build).  Every edit is an exact-match
replacement that fails loudly on a drifted base (no fuzzy application).

The committed deliverable is patches/0001-ba-tx-impl.patch; this script is the
regeneration path (build-ba-tx.sh --regen-patch) and the reviewable record of
the six touched files.  HOST-ONLY: it edits source files, never a device.
"""
import argparse
import shutil
import sys
from pathlib import Path

LANE = Path(__file__).resolve().parent.parent
SRC = LANE / "src"
NEW_FILES = ["hi5622_ba.h", "hi5622_ba.c", "hi5622_ba_glue.h", "hi5622_ba_glue.c"]

# (file, exact old text, replacement text)
EDITS = [
    ("Kbuild",
     "\t\t hi5622_desc_table.o hi5622_band_start.o hi5622_dfs_mode.o \\\n\t\t hi5622_vap_start_g0.o \\\n",
     "\t\t hi5622_desc_table.o hi5622_band_start.o hi5622_dfs_mode.o \\\n\t\t hi5622_vap_start_g0.o \\\n\t\t hi5622_ba.o hi5622_ba_glue.o \\\n"),

    ("main.c",
     '#include "h2d_send.h"\n',
     '#include "h2d_send.h"\n#include "hi5622_ba_glue.h"\n'),

    ("main.c",
     "\t/* MAC data plane (merged wifi-dp-air lane): host state only at probe\n"
     "\t * (no rings, no device access); dp_setup is the one MAC/ring action. */\n"
     "\tret = hi5622_dp_probe(ctx);\n",
     "\t/* lane wifi-ba-tx-impl (task-217): the fail-closed TX-direction BA core\n"
     "\t * (ba_enable=0 default; inert unless the attended fire sets it).  No\n"
     "\t * device access at init; the wire posts go through hi5622_frame_post(). */\n"
     "\tret = hi5622_ba_glue_init(ctx);\n"
     "\tif (ret) {\n"
     "\t\thi5622_cfg80211_remove(ctx);\n"
     "\t\thi5622_hcc_remove(ctx);\n"
     "\t\treturn dev_err_probe(dev, ret, \"ba glue init\\n\");\n"
     "\t}\n"
     "\n"
     "\t/* MAC data plane (merged wifi-dp-air lane): host state only at probe\n"
     "\t * (no rings, no device access); dp_setup is the one MAC/ring action. */\n"
     "\tret = hi5622_dp_probe(ctx);\n"),

    ("main.c",
     "\t/* Reverse of probe order (stability hardening): cfg80211 teardown cancels\n",
     "\t/* lane wifi-ba-tx-impl: cancel the BA workqueue before the subsystems it\n"
     "\t * posts through are freed. */\n"
     "\thi5622_ba_glue_remove(ctx);\n"
     "\t/* Reverse of probe order (stability hardening): cfg80211 teardown cancels\n"),

    ("hcc.c",
     '#include "hi5622_mgmt.h"\n',
     '#include "hi5622_mgmt.h"\n#include "hi5622_ba_glue.h"\n'),

    ("hcc.c",
     "\t/* wifi-ap-mgmt lane: D2H management-frame events (group1 id13) decode\n"
     "\t * to cfg80211_rx_mgmt behind mgmt_rx=1; default counts only. */\n"
     "\thi5622_mgmt_deliver(ctx, hdr, payload, payload_avail, wire_len);\n",
     "\t/* wifi-ap-mgmt lane: D2H management-frame events (group1 id13) decode\n"
     "\t * to cfg80211_rx_mgmt behind mgmt_rx=1; default counts only. */\n"
     "\thi5622_mgmt_deliver(ctx, hdr, payload, payload_avail, wire_len);\n"
     "\t/* lane wifi-ba-tx-impl (task-217): the D2H BA events (ids 2/3/0x0F/\n"
     "\t * 0x1D on the event group) are filtered cheaply here and handled in\n"
     "\t * the BA workqueue; inert unless ba_enable=1. */\n"
     "\thi5622_ba_glue_d2h(ctx, hdr, payload, payload_avail, wire_len);\n"),

    ("hi5622_mgmt.c",
     '#include "hi5622_sta_ops_logic.h"\n',
     '#include "hi5622_sta_ops_logic.h"\n#include "hi5622_ba_glue.h"\n'),

    ("hi5622_mgmt.c",
     "\tmgmt_state.rx_events++;\n"
     "\tif (!hi5622_mgmt_rx)\n"
     "\t\treturn;\t\t/* count only (fail-closed default) */\n",
     "\tmgmt_state.rx_events++;\n"
     "\t/* lane wifi-ba-tx-impl (task-217): block-ack action frames are handed to\n"
     "\t * the BA core regardless of the cfg80211 delivery gate; the BA core has\n"
     "\t * its own fail-closed gate (ba_enable) and defers the work. */\n"
     "\thi5622_ba_glue_mgmt_rx(ctx, rx.frame, rx.frame_len);\n"
     "\tif (!hi5622_mgmt_rx)\n"
     "\t\treturn;\t\t/* count only (fail-closed default) */\n"),

    ("hi5622_data_tx.c",
     '#include "dp_txrx.h"\n',
     '#include "dp_txrx.h"\n#include "hi5622_ba_glue.h"\n'),

    ("hi5622_data_tx.c",
     "\tspin_unlock_irqrestore(&d->lock, flags);\n"
     "\tif (rc)\n"
     "\t\treturn rc;\n"
     "\tqueue_delayed_work(system_wq, &d->poll,\n",
     "\tspin_unlock_irqrestore(&d->lock, flags);\n"
     "\tif (rc)\n"
     "\t\treturn rc;\n"
     "\t/* lane wifi-ba-tx-impl (task-217): the in-kernel ADDBA originator.\n"
     "\t * xmit-context safe (spinlock + schedule_work); inert unless\n"
     "\t * ba_enable=1 and the peer is an associated station. */\n"
     "\thi5622_ba_glue_tx_data(c->ctx, eth.da, (u8)p.tid, (u16)seq);\n"
     "\tqueue_delayed_work(system_wq, &d->poll,\n"),

    ("hi5622_scan.c",
     '#include "hi5622_vap_start_g0.h"\n',
     '#include "hi5622_vap_start_g0.h"\n#include "hi5622_ba_glue.h"\n'),

    ("hi5622_scan.c",
     "\tc->ctx->ap_started = true;\n"
     "\tc->ctx->ap_seq_frames = (u8)posted;\n"
     "\tposted = 0;\t/* committed: not a dirty abort */\n",
     "\tc->ctx->ap_started = true;\n"
     "\tc->ctx->ap_seq_frames = (u8)posted;\n"
     "\tposted = 0;\t/* committed: not a dirty abort */\n"
     "\t/* lane wifi-ba-tx-impl (task-217): record the committed AP so the BA\n"
     "\t * action-frame carrier can address it (channel/band/MAC); inert unless\n"
     "\t * ba_enable=1, no frame is sent here. */\n"
     "\thi5622_ba_glue_ap_up(c->ctx, p->band, p->channel, dev->dev_addr);\n"),

    ("hi5622_scan.c",
     "\t\tc->ctx->ap_stopped_hostside = true;\n",
     "\t\tc->ctx->ap_stopped_hostside = true;\n"
     "\t\thi5622_ba_glue_ap_down(c->ctx);\n"),

    ("hi5622_scan.c",
     "\t\t\t \"cfg80211: stop_ap handed to provider '%s' (%zu B frame)\\n\",\n"
     "\t\t\t ops->name, len);\n"
     "out:\n",
     "\t\t\t \"cfg80211: stop_ap handed to provider '%s' (%zu B frame)\\n\",\n"
     "\t\t\t ops->name, len);\n"
     "\t/* lane wifi-ba-tx-impl (task-217): the AP is down, the BA sessions are\n"
     "\t * no longer addressable - clear them (no frame sent). */\n"
     "\thi5622_ba_glue_ap_down(c->ctx);\n"
     "out:\n"),

    ("hi5622_scan.c",
     "\tif (pos >= len)\n"
     "\t\treturn (int)(len - 1);\n"
     "\tmutex_lock(&s->lock);\n"
     "\tret = hi5622_scan_status_line(&s->model, buf + pos, len - pos);\n",
     "\tif (pos >= len)\n"
     "\t\treturn (int)(len - 1);\n"
     "\t/* lane wifi-ba-tx-impl (task-217): the BA parameter/session/outcome\n"
     "\t * (read-only; frames are posted from the BA workqueue, never here). */\n"
     "\tret = hi5622_ba_glue_status_line(c->ctx, buf + pos, len - pos);\n"
     "\tif (ret > 0)\n"
     "\t\tpos += (size_t)ret;\n"
     "\tif (pos >= len)\n"
     "\t\treturn (int)(len - 1);\n"
     "\tmutex_lock(&s->lock);\n"
     "\tret = hi5622_scan_status_line(&s->model, buf + pos, len - pos);\n"),
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("tree", help="driver tree copy to integrate into")
    ap.add_argument("--check", action="store_true",
                    help="verify every edit matches, change nothing")
    args = ap.parse_args()
    tree = Path(args.tree)
    if not (tree / "hi5622_scan.c").is_file():
        print(f"apply-integration: {tree} is not the driver base tree", file=sys.stderr)
        return 2
    # 1. new files
    for name in NEW_FILES:
        src = SRC / name
        if not src.is_file():
            print(f"apply-integration: missing {src}", file=sys.stderr)
            return 2
        if not args.check:
            shutil.copyfile(src, tree / name)
    # 2. exact edits
    for fname, old, new in EDITS:
        path = tree / fname
        text = path.read_text()
        n = text.count(old)
        if n != 1:
            print(f"apply-integration: {fname}: expected exactly 1 match, got {n}",
                  file=sys.stderr)
            return 3
        if not args.check:
            path.write_text(text.replace(old, new))
    print(f"apply-integration: {'OK (check only)' if args.check else 'OK'} "
          f"({len(EDITS)} edits, {len(NEW_FILES)} new files) on {tree}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
