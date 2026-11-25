#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""make-patch.py - regenerate patches/0001-rate-ctl.patch from the committed
source trees (host-only).

    tools/make-patch.py            # write the patch (deterministic, no
                                   # timestamps) and print its sha256
    tools/make-patch.py --verify   # additionally apply it to a fresh staged
                                   # copy of the base tree and report

The patch is derived, not hand-written: it copies a source-only snapshot of
../wifi-h2d-send/src, applies the wifi-rate-ctl edits (and adds the four new
source files from this lane's src/), then diffs the two trees.  The base tree
files are pinned in ../BUILD-NOTES.md; if the base drifts the patch apply in
tools/build-rate-ctl.sh fails loudly.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LANE = os.path.dirname(HERE)
BASE = os.path.normpath(os.path.join(LANE, "..", "wifi-h2d-send", "src"))
OUT = os.path.join(LANE, "patches", "0001-rate-ctl.patch")
NEW_FILES = ("hi5622_rate_ctl_logic.h", "hi5622_rate_ctl_logic.c",
             "hi5622_rate_ctl.h", "hi5622_rate_ctl.c")


def copy_base(dst):
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(BASE):
        if name.endswith((".c", ".h", ".inc")) or name == "Kbuild":
            src = os.path.join(BASE, name)
            if os.path.isfile(src):
                shutil.copy2(src, dst)


def edit(path, replacements):
    with open(path) as fh:
        txt = fh.read()
    for old, new in replacements:
        if old not in txt:
            raise SystemExit("anchor not found in %s:\n%s" % (path, old[:200]))
        txt = txt.replace(old, new, 1)
    with open(path, "w") as fh:
        fh.write(txt)


def apply_edits(src):
    edit(os.path.join(src, "h2d_send_logic.h"), [
        ("\tH2D_MSG_CFG_CAL,\t/* g0 id2 key 0x0101, 270-byte calibration blob */\n\tH2D_MSG_COUNT,",
         "\tH2D_MSG_CFG_CAL,\t/* g0 id2 key 0x0101, 270-byte calibration blob */\n"
         "\tH2D_MSG_RATE_CFG,\t/* g0 id2 key 0x0101, decoded rate-control alg_cfg */\n"
         "\tH2D_MSG_COUNT,"),
        ("/* Extended build: cfg_id == 0 selects H2D_CAL_CFG_PROBE and fb == 0xff the",
         "/* The rate-control alg_cfg config event (wifi-rate-ctl lane): the same\n"
         " * proven HCC group 0 / id 2 / key 0x0101 carrier as h2d_build_cfg_cal, but\n"
         " * with the blob length = 10-byte alg_cfg header + value (the vendor LUT's\n"
         " * per-command length) so any decoded rate-control command fits.  cfg_type\n"
         " * 0 = SET / 1 = GET; pkt_type 0..13 (13 = no leading packet-type token,\n"
         " * the vendor host encoder's value); value_len <= H2D_ALG_MAX_VALUE.  The\n"
         " * value bytes are the decoded layout; this builder never guesses.  EXACT. */\n"
         "#define H2D_ALG_HDR_LEN\t10u\n"
         "#define H2D_ALG_MAX_VALUE\t260u\n"
         "int h2d_build_alg_cfg(struct h2d_frame *f, u8 dst, u8 fb, u16 cfg_id,\n"
         "\t\t      u8 cfg_type, u8 pkt_type, const u8 *value, u16 value_len,\n"
         "\t\t      enum h2d_err *err);\n\n"
         "/* Extended build: cfg_id == 0 selects H2D_CAL_CFG_PROBE and fb == 0xff the"),
    ])
    edit(os.path.join(src, "h2d_send_logic.c"), [
        ("\tcase H2D_MSG_CFG_CAL:\t\treturn \"cfg_cal\";",
         "\tcase H2D_MSG_CFG_CAL:\t\treturn \"cfg_cal\";\n"
         "\tcase H2D_MSG_RATE_CFG:\t\treturn \"rate_cfg\";"),
        ("int h2d_msg_build(enum h2d_msg msg, u8 dst, u16 user_id, const u8 *addr,",
         "/* Generalised alg_cfg config event: one decoded vendor command on the\n"
         " * proven g0 id2 / key 0x0101 carrier.  See h2d_send_logic.h. */\n"
         "int h2d_build_alg_cfg(struct h2d_frame *f, u8 dst, u8 fb, u16 cfg_id,\n"
         "\t\t      u8 cfg_type, u8 pkt_type, const u8 *value, u16 value_len,\n"
         "\t\t      enum h2d_err *err)\n"
         "{\n"
         "\tu8 blob[H2D_ALG_HDR_LEN + H2D_ALG_MAX_VALUE];\n"
         "\tu16 blob_len;\n\n"
         "\tif (!f || cfg_id == 0u || cfg_type > 1u || pkt_type > 13u ||\n"
         "\t    value_len > H2D_ALG_MAX_VALUE || (value_len && !value)) {\n"
         "\t\tif (err)\n"
         "\t\t\t*err = H2D_EARG;\n"
         "\t\treturn -1;\n"
         "\t}\n"
         "\tif (fb > H2D_CTX_MAX_MODULE) {\n"
         "\t\tif (err)\n"
         "\t\t\t*err = H2D_EBOUNDS;\n"
         "\t\treturn -1;\n"
         "\t}\n"
         "\tblob_len = (u16)(H2D_ALG_HDR_LEN + value_len);\n"
         "\tmemset(blob, 0, sizeof(blob));\n"
         "\tblob[0] = (u8)(cfg_id & 0xffu);\n"
         "\tblob[1] = (u8)((cfg_id >> 8) & 0xffu);\n"
         "\tblob[2] = cfg_type;\t/* 0 = SET, 1 = GET */\n"
         "\tblob[3] = pkt_type;\t/* 13 = no leading packet-type token */\n"
         "\tblob[8] = 1u;\t\t/* need_w4_dev_return: the device answers */\n"
         "\tif (value_len)\n"
         "\t\tmemcpy(blob + H2D_ALG_HDR_LEN, value, value_len);\n"
         "\treturn h2d_build_g0_cfg_event(f, 2u, dst, 0u, fb, 0u, h2d_cal_token,\n"
         "\t\t\t\t      H2D_CAL_KEY, blob, blob_len,\n"
         "\t\t\t\t      h2d_cal_tail, err);\n"
         "}\n\n"
         "int h2d_msg_build(enum h2d_msg msg, u8 dst, u16 user_id, const u8 *addr,"),
    ])
    edit(os.path.join(src, "h2d_send.h"), [
        ("int  hi5622_h2d_add_attrs(struct device *dev);",
         "/* Rate-control alg_cfg config event (wifi-rate-ctl lane): ONE decoded\n"
         " * vendor rate/alg command on the proven g0 id2 / key 0x0101 carrier.\n"
         " * Caller (hi5622_rate_ctl.c) has already applied its fail-closed policy;\n"
         " * this entry point applies the same gates/budget/one-idea rules as the\n"
         " * other post-ready messages.  fb = header field B (module context). */\n"
         "int  h2d_post_rate_cfg(struct hi5622_pci *ctx, u16 cfg_id, u8 cfg_type,\n"
         "\t\t       u8 pkt_type, const u8 *value, u16 value_len, u8 fb);\n\n"
         "int  hi5622_h2d_add_attrs(struct device *dev);"),
    ])
    edit(os.path.join(src, "h2d_send.c"), [
        ("static int h2d_post(struct hi5622_pci *ctx, enum h2d_msg msg, u8 dst,\n"
         "\t\t    u16 user_id, const u8 *addr, u16 cfg_id, u8 fb)\n{",
         "/* Parameter block for the rate-control alg_cfg event (wifi-rate-ctl). */\n"
         "struct h2d_alg_cfg {\n"
         "\tu16 cfg_id;\n"
         "\tu8  cfg_type;\n"
         "\tu8  pkt_type;\n"
         "\tu16 value_len;\n"
         "\tconst u8 *value;\n"
         "};\n\n"
         "static int h2d_post(struct hi5622_pci *ctx, enum h2d_msg msg, u8 dst,\n"
         "\t\t    u16 user_id, const u8 *addr, u16 cfg_id, u8 fb,\n"
         "\t\t    const struct h2d_alg_cfg *alg)\n{"),
        ("\t/* 1. Exact frame build + full policy validation (no device access). */\n"
         "\tif (h2d_msg_build_ext(msg, dst, user_id, addr, cfg_id, fb, f, wire,\n"
         "\t\t\t  HI5622_HCC_FRAME_MAX_TOTAL, &total, &herr) != 0) {\n"
         "\t\th2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);\n"
         "\t\tdev_warn(dev, \"H2D %s: frame refused by the exact builder/validator (%s); no device write performed\\n\",\n"
         "\t\t\t h2d_msg_name(msg), h2d_strerror(herr));\n"
         "\t\tret = -EPERM;\n"
         "\t\tgoto out;\n"
         "\t}",
         "\t/* 1. Exact frame build + full policy validation (no device access).\n"
         "\t * The rate-control alg_cfg event (wifi-rate-ctl lane) carries decoded\n"
         "\t * command bytes, so it uses its own exact builder (h2d_build_alg_cfg)\n"
         "\t * and the same encode-side validation; every other message goes\n"
         "\t * through the canned/extended builder table. */\n"
         "\tif (msg == H2D_MSG_RATE_CFG) {\n"
         "\t\tsize_t wire_len = 0;\n\n"
         "\t\tif (!alg) {\n"
         "\t\t\therr = H2D_EARG;\n"
         "\t\t\tret = -EPERM;\n"
         "\t\t\tgoto out;\n"
         "\t\t}\n"
         "\t\tif (h2d_build_alg_cfg(f, dst, fb, alg->cfg_id, alg->cfg_type,\n"
         "\t\t\t\t      alg->pkt_type, alg->value,\n"
         "\t\t\t\t      alg->value_len, &herr) != 0 ||\n"
         "\t\t    h2d_frame_encode(f, wire, HI5622_HCC_FRAME_MAX_TOTAL,\n"
         "\t\t\t\t     &wire_len, &herr) != 0 || wire_len > 0xffffu) {\n"
         "\t\t\th2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);\n"
         "\t\t\tdev_warn(dev, \"H2D %s: frame refused by the alg_cfg builder/validator (%s); no device write performed\\n\",\n"
         "\t\t\t\t h2d_msg_name(msg), h2d_strerror(herr));\n"
         "\t\t\tret = -EPERM;\n"
         "\t\t\tgoto out;\n"
         "\t\t}\n"
         "\t\ttotal = (u32)wire_len;\n"
         "\t} else if (h2d_msg_build_ext(msg, dst, user_id, addr, cfg_id, fb, f,\n"
         "\t\t\t\t     wire, HI5622_HCC_FRAME_MAX_TOTAL, &total,\n"
         "\t\t\t\t     &herr) != 0) {\n"
         "\t\th2d_note_refused(h, msg, H2D_GATE_FRAME_INVALID);\n"
         "\t\tdev_warn(dev, \"H2D %s: frame refused by the exact builder/validator (%s); no device write performed\\n\",\n"
         "\t\t\t h2d_msg_name(msg), h2d_strerror(herr));\n"
         "\t\tret = -EPERM;\n"
         "\t\tgoto out;\n"
         "\t}"),
        ("\tret = h2d_post(ctx, H2D_MSG_TXQ_SYNC, 0u, 0u, NULL, 0u,\n\t\t\tH2D_CAL_FB_DEFAULT_UNSET);",
         "\tret = h2d_post(ctx, H2D_MSG_TXQ_SYNC, 0u, 0u, NULL, 0u,\n\t\t\tH2D_CAL_FB_DEFAULT_UNSET, NULL);"),
        ("\tret = h2d_post(ctx, H2D_MSG_TXQ_SYNC72, 0u, 0u, NULL, 0u,\n\t\t\tH2D_CAL_FB_DEFAULT_UNSET);",
         "\tret = h2d_post(ctx, H2D_MSG_TXQ_SYNC72, 0u, 0u, NULL, 0u,\n\t\t\tH2D_CAL_FB_DEFAULT_UNSET, NULL);"),
        ("\tret = h2d_post(ctx, msg, 0u, 0u, NULL, 0u,\n\t\t\tH2D_CAL_FB_DEFAULT_UNSET);",
         "\tret = h2d_post(ctx, msg, 0u, 0u, NULL, 0u,\n\t\t\tH2D_CAL_FB_DEFAULT_UNSET, NULL);"),
        ("\tret = h2d_post(ctx, H2D_MSG_CFG_CAL, 0u, 0u, NULL, cfg_id, fb);",
         "\tret = h2d_post(ctx, H2D_MSG_CFG_CAL, 0u, 0u, NULL, cfg_id, fb, NULL);"),
        ("\tret = h2d_post(ctx, req.msg, req.dst, req.user_id,\n"
         "\t\t       req.has_addr ? req.addr : NULL,\n"
         "\t\t       req.has_cfg ? req.cfg_id : 0u,\n"
         "\t\t       req.has_fb ? req.fb : H2D_CAL_FB_DEFAULT_UNSET);",
         "\tret = h2d_post(ctx, req.msg, req.dst, req.user_id,\n"
         "\t\t       req.has_addr ? req.addr : NULL,\n"
         "\t\t       req.has_cfg ? req.cfg_id : 0u,\n"
         "\t\t       req.has_fb ? req.fb : H2D_CAL_FB_DEFAULT_UNSET, NULL);"),
        ("static ssize_t h2d_send_store(struct device *dev, struct device_attribute *attr,",
         "/* Rate-control alg_cfg config event: one decoded vendor rate/alg command on\n"
         " * the proven g0 id2 / key 0x0101 carrier.  Used ONLY by\n"
         " * hi5622_rate_ctl.c after its fail-closed policy check. */\n"
         "int h2d_post_rate_cfg(struct hi5622_pci *ctx, u16 cfg_id, u8 cfg_type,\n"
         "\t\t      u8 pkt_type, const u8 *value, u16 value_len, u8 fb)\n"
         "{\n"
         "\tstruct h2d_alg_cfg alg;\n\n"
         "\tif (!ctx || !ctx->hcc)\n"
         "\t\treturn -ENODEV;\n"
         "\talg.cfg_id = cfg_id;\n"
         "\talg.cfg_type = cfg_type;\n"
         "\talg.pkt_type = pkt_type;\n"
         "\talg.value = value;\n"
         "\talg.value_len = value_len;\n"
         "\treturn h2d_post(ctx, H2D_MSG_RATE_CFG, 0u, 0u, NULL, 0u, fb, &alg);\n"
         "}\n\n"
         "static ssize_t h2d_send_store(struct device *dev, struct device_attribute *attr,"),
    ])
    edit(os.path.join(src, "main.c"), [
        ('#include "hi5622_hcc.h"\n#include "h2d_send.h"',
         '#include "hi5622_hcc.h"\n#include "h2d_send.h"\n#include "hi5622_rate_ctl.h"'),
        ('\t{ "d2h", hi5622_d2h_add_attrs, hi5622_d2h_del_attrs },',
         '\t{ "d2h", hi5622_d2h_add_attrs, hi5622_d2h_del_attrs },\n'
         '\t{ "rate_ctl", hi5622_rate_ctl_add_attrs, hi5622_rate_ctl_del_attrs },'),
    ])
    edit(os.path.join(src, "hcc.c"), [
        ('#include "hi5622_hcc.h"',
         '#include "hi5622_hcc.h"\n#include "hi5622_rate_ctl.h"'),
        ("\tif (!ctx->hcc)\n\t\treturn -ENODEV;\n"
         "\treturn hcc_core_deliver_hdr(&ctx->hcc->hcc.core, src, hdr, payload,",
         "\tif (!ctx->hcc)\n\t\treturn -ENODEV;\n"
         "\t/* wifi-rate-ctl: decode the g1 id3 key-0x0101 config-sync answers into\n"
         "\t * the read-only rate_ctl status/log (the rate-selection observable). */\n"
         "\thi5622_rate_ctl_rx(ctx, hdr, payload, payload_avail);\n"
         "\treturn hcc_core_deliver_hdr(&ctx->hcc->hcc.core, src, hdr, payload,"),
    ])
    edit(os.path.join(src, "Kbuild"), [
        ("\t\t h2d_frames.o h2d_send_logic.o h2d_send.o",
         "\t\t h2d_frames.o h2d_send_logic.o h2d_send.o \\\n"
         "\t\t hi5622_rate_ctl.o hi5622_rate_ctl_logic.o"),
    ])


def main(argv):
    verify = "--verify" in argv
    if not os.path.isdir(BASE):
        print("base tree missing: %s (run in the opensource submodule)" % BASE,
              file=sys.stderr)
        return 2
    work = tempfile.mkdtemp(prefix="wifi-rate-ctl-patch-")
    try:
        a = os.path.join(work, "a", "src")
        b = os.path.join(work, "b", "src")
        copy_base(a)
        copy_base(b)
        for name in NEW_FILES:
            shutil.copy2(os.path.join(LANE, "src", name), b)
        apply_edits(b)
        raw = subprocess.run(["diff", "-ruN", "a", "b"], cwd=work,
                             capture_output=True, text=True).stdout
        # deterministic: drop the file timestamps from the diff headers
        lines = []
        for line in raw.splitlines(True):
            if line.startswith(("--- ", "+++ ")) and "\t" in line:
                line = line.split("\t")[0] + "\n"
            lines.append(line)
        patch = "".join(lines)
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, "w") as fh:
            fh.write(patch)
        sha = hashlib.sha256(patch.encode()).hexdigest()
        touched = [ln[6:].strip() for ln in patch.splitlines()
                   if ln.startswith("+++ b/")]
        print("wrote %s (%d B, sha256 %s)" % (OUT, len(patch), sha))
        print("files touched: %d" % len(touched))
        if verify:
            # apply the patch to a pristine source-only copy of the base tree
            probe = os.path.join(work, "probe")
            os.makedirs(os.path.join(probe, "src"))
            copy_base(os.path.join(probe, "src"))
            rc = subprocess.run(["patch", "-p1", "--batch", "--forward", "-s",
                                 "-d", probe, "-i", OUT])
            if rc.returncode != 0:
                print("VERIFY FAILED: patch does not apply to the current base tree",
                      file=sys.stderr)
                return 1
            print("verify: patch applies cleanly to a pristine base copy")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))