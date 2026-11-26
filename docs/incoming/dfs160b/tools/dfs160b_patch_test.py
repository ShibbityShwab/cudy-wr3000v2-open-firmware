#!/usr/bin/env python3
"""dfs160b independent integration/order test (lane dfs160b, task-245).

HOST-ONLY: reads the committed lane files + the lane's own patch text and the
target kernel UAPI header (read-only, when present).  No device, no vendor
artifact, no network.  This is the independent counterpart of the C host
selftest: it verifies the PATCH actually carries what the model and the lane
docs claim, and it re-derives the nl80211_chan_width pins from the real
6.18.60 header instead of trusting the model constants.

Run:  tools/dfs160b_patch_test.py            (exit 0 = PASS)
"""
import hashlib
import os
import re
import sys

LANE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKS = 0
FAILS = 0


def ck(what, cond, detail=""):
    global CHECKS, FAILS
    CHECKS += 1
    if not cond:
        FAILS += 1
        print("FAIL: %s %s" % (what, detail))


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def sha(path):
    return hashlib.sha256(read(path).encode()).hexdigest()


patch = read(os.path.join(LANE, "patches", "0001-dfs160b.patch"))
model_h = read(os.path.join(LANE, "src", "hi5622_dfsb.h"))
model_c = read(os.path.join(LANE, "src", "hi5622_dfsb.c"))
glue_h = read(os.path.join(LANE, "src", "hi5622_dfsb_k.h"))
glue_c = read(os.path.join(LANE, "src", "hi5622_dfsb_k.c"))
selftest = read(os.path.join(LANE, "src", "dfs160b_selftest.c"))
build = read(os.path.join(LANE, "tools", "build-dfs160b.sh"))

# ---- 1. the patch shape -------------------------------------------------
files = re.findall(r"^diff --git a/(\S+) b/\S+$", patch, re.M)
ck("patch carries 14 files", len(files) == 14, files)
for f in ("hi5622_dfsb.h", "hi5622_dfsb.c", "hi5622_dfsb_k.h",
          "hi5622_dfsb_k.c"):
    ck("new file in patch: " + f, f in files)
    ck("new-file mode for " + f, ("new file mode 100644" in patch) and
       re.search(r"diff --git a/%s b/%s\nnew file mode" % (re.escape(f),
                                                          re.escape(f)),
                 patch) is not None)
ck("no binary blobs in the patch", "GIT binary patch" not in patch and
   "Binary files" not in patch)
ck("no firmware image path in the patch",
   "firmware.bin" not in patch.lower() and ".ko" not in files)
ck("no private key / password text", "PRIVATE KEY" not in patch and
   "password" not in patch.lower())

# the four new files are byte-identical to the committed lane sources
blocks = patch.split("diff --git ")[1:]
for f in ("hi5622_dfsb.h", "hi5622_dfsb.c", "hi5622_dfsb_k.h",
          "hi5622_dfsb_k.c"):
    want = read(os.path.join(LANE, "src", f))
    got = None
    for b in blocks:
        first = b.splitlines()[0]
        if first == "a/%s b/%s" % (f, f):
            lines = b.splitlines()[1:]
            got = "\n".join(ln[1:] for ln in lines
                            if ln.startswith("+") and
                            not ln.startswith("+++")) + "\n"
            break
    ck("patch %s == src/%s" % (f, f), got == want,
       "" if got == want else "(reconstructed bytes differ)")

# ---- 2. re-derive the nl80211 width pins from the real 6.18.60 UAPI ----
UAPI_CANDIDATES = [
    "/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/"
    "target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/"
    "include/uapi/linux/nl80211.h",
    "/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/"
    "target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/"
    "include/uapi/linux/nl80211.h",
]
uapi = next((p for p in UAPI_CANDIDATES if os.path.isfile(p)), None)
want = {"20_NOHT": 0, "20": 1, "40": 2, "80": 3, "80P80": 4, "160": 5,
        "5": 6, "10": 7, "1": 8, "2": 9, "4": 10, "8": 11, "16": 12,
        "320": 13}
if uapi:
    text = read(uapi)
    m = re.search(r"enum nl80211_chan_width \{(.*?)\};", text, re.S)
    ck("kernel UAPI enum found", m is not None)
    names = re.findall(r"NL80211_CHAN_WIDTH_(\w+),", m.group(1)) if m else []
    ck("kernel UAPI enum order is the 6.18 order",
       names[:14] == ["20_NOHT", "20", "40", "80", "80P80", "160", "5",
                      "10", "1", "2", "4", "8", "16", "320"], names)
    for name, exp in want.items():
        ck("kernel enum %s == %u" % (name, exp),
           names and names.index(name) == exp)
else:
    print("note: target UAPI header not present - kernel-side pin checks "
          "skipped (the model constants are still checked below)")

# the model header pins (always checked)
defs = dict((k, int(v)) for k, v in re.findall(
    r"#define HI5622_DFSB_WIDTH_(\w+)\s+(\d+)u", model_h))
model_names = {"20NOHT": 0, "20": 1, "40": 2, "80": 3, "80P80": 4, "160": 5,
               "5": 6, "10": 7, "1": 8, "2": 9, "4": 10, "8": 11, "16": 12,
               "320": 13}
for name, exp in model_names.items():
    ck("model pin %s == %u" % (name, exp), defs.get(name) == exp,
       "got %r" % (defs.get(name),))

# ---- 3. the kernel glue pins them with compile-time assertions ---------
for name, val in (("20_NOHT", 0), ("80", 3), ("80P80", 4), ("160", 5),
                  ("320", 13)):
    ck("glue _Static_assert for %s" % name,
       ("NL80211_CHAN_WIDTH_%s ==" % name) in glue_c)
ck("glue has >= 7 static assertions", glue_c.count("_Static_assert(") >= 7)
ck("glue pins the 6.18 160 value comment", "6.18 value is 5" in glue_c)
ck("glue pins the 6.18 80P80 value comment", "6.18 value is 4" in glue_c)

# ---- 4. the portable model decisions -----------------------------------
ck("vendor selector 20/40/80/160", all(
    s in model_h for s in ("HI5622_DFSB_BW_20\t\t0u",
                           "HI5622_DFSB_BW_40\t\t1u",
                           "HI5622_DFSB_BW_80\t\t2u",
                           "HI5622_DFSB_BW_160\t\t3u")))
ck("160 CCFS1 5250/5570/5815", all(s in model_c for s in
                                   ("5250u", "5570u", "5815u")))
ck("80 CCFS1 table present", "5210u" in model_c and "5855u" in model_c)
ck("ch144 = 5720 in the vendor table", "{ 144, 5720 }" in model_c)
ck("TDWR window 5600..5650", "5600u" in model_c and "5650u" in model_c)
ck("vendor CAC times 60000/600000/1800000", all(
    s in model_h for s in ("HI5622_DFSB_CAC_MS\t\t60000u",
                           "HI5622_DFSB_CAC_MS_TDWR\t\t600000u",
                           "HI5622_DFSB_NOL_MS\t\t1800000u")))
ck("vendor tick arithmetic 63000/60000", "HI5622_DFSB_FIRST_TICK_MS\t63000u"
   in model_h and "HI5622_DFSB_TICK_MS\t\t60000u" in model_h)
ck("CAC tick counter semantics", "ticks_done" in model_c and
   "ticks ? st->ticks : 1u" in model_c)
ck("wrap-safe NOL gate", "elapsed = now_ms - st->finish_ms;" in model_c)
ck("0x07E7 key", "0x07e7u" in model_h)
ck("29-byte frame", "HI5622_DFSB_SET_WIRE_LEN\t29u" in model_h)
ck("ARM pattern 0x2D", "0x2du" in model_h)
ck("ladder order ops", all(s in model_h for s in
    ("HI5622_DFSB_OP_VAP_START", "HI5622_DFSB_OP_DFS_SET",
     "HI5622_DFSB_OP_DFS_MODE", "HI5622_DFSB_OP_BAND_START")))
ck("order function", "hi5622_dfsb_order_before" in model_c)
ck("ap plan refuses dfs_cac+dfs_set", "One meaning per load" in model_c)

# ---- 5. the patch actually carries the integration --------------------
ck("0x0124 body[1] = the vendor bandwidth selector",
   "+	body[1] = p->bandwidth;" in patch)
ck("chan_cfg params gained bandwidth", "+	u8	bandwidth;" in patch)
ck("ap params gained the centre freqs", "center_freq1" in patch and
   "center_freq2" in patch)
ck("scan sets cp.bandwidth", "+		cp.bandwidth = dfsb_width_sel;" in patch)
ck("Kbuild adds the model + glue objects",
   "hi5622_dfsb.o hi5622_dfsb_k.o" in patch)
ck("start_radar_detection op registered",
   ".start_radar_detection\t= hi5622_dfsb_k_op_start_radar_detection," in patch)
ck("end_cac op registered",
   ".end_cac\t\t= hi5622_dfsb_k_op_end_cac," in patch)
ck("cfg80211_cac_event in the provider symvers",
   "cfg80211_cac_event\tcfg80211\tEXPORT_SYMBOL" in patch)
ck("ch144 restored in the template channel list",
   "HI5622_CH5G(144, 5720, HI5622_DFS)," in patch)
ck("TDWR dfs_cac_ms = 600000", "HI5622_DFSB_CAC_MS_TDWR" in patch and
   "dfs_cac_ms" in patch)
ck("scan includes the glue header", '#include "hi5622_dfsb_k.h"' in patch)
ck("live dfs_set probe exported", "hi5622_scan_dfs_set_probe" in patch)
ck("start_ap plan call", "hi5622_dfsb_k_ap_plan(" in patch)
ck("dfs_cac/dfs_set mutual exclusion in the ladder gate",
   "mutually exclusive" in patch)
ck("ht_bw/dfs_cac parameter gates", "ht_bw=%u is not a valid" in patch and
   "dfs_cac=%u is not a valid" in patch)
ck("module params defined", "module_param_named(ht_bw" in glue_c and
   "module_param_named(dfs_cac" in glue_c)

# ---- 6. lane hygiene ---------------------------------------------------
lane_files = []
for root, _dirs, names in os.walk(LANE):
    for n in names:
        lane_files.append(os.path.join(root, n))
ck("no binary artifacts in the lane",
   not [f for f in lane_files if f.endswith((".ko", ".o", ".bin",
                                              ".fw", ".patch2"))])
ck("no vendor artifact directory", not os.path.isdir(os.path.join(LANE,
                                                                   "vendor")))

# ---- 7. the build script pins are real ---------------------------------
pins = re.findall(r'expect_sha "\$LANE/(\S+)" ([0-9a-f]{64})', build)
ck("build script pins >= 5 lane files", len(pins) >= 5)
for f, want_sha in pins:
    ck("build pin %s matches" % f, sha(os.path.join(LANE, f)) == want_sha)
ck("build script pins the installed module",
   "006a94cf6bcb6f53887c83aa4ad3a0f057358301d9a350a139ae178acac0a183" in build)
ck("build script pins the .text",
   "5e14008fab4707afdf55ab3ea864129abfc65979c3e4b0b2e572f6593ead5faa" in build)

# ---- 8. host selftest integrity ---------------------------------------
ck("selftest checks the 6.18 160 value", "pin 160=5 (6.18; NOT 4)" in
   selftest)
ck("selftest checks 80P80=4 is refused", "80P80 (4) refused" in selftest)
ck("selftest has the ARM frame KAT", "arm_kat" in selftest and
   "0xe7, 0x07" in selftest)
ck("selftest checks the ch144 gap", "ch144 exists (the gap)" in selftest)
ck("selftest checks the TDWR 10-tick CAC", "ch120 ticks 10" in selftest)
ck("selftest checks the NOL boundary", "ch52 gate NOL boundary" in selftest)

# ---- 9. the docs the Lead fires from exist ----------------------------
for doc in ("README.md", "REPORT.md", "TABLES.md", "FIRE-RECIPE.md",
            "BUILD-NOTES.md"):
    ck("lane doc " + doc, os.path.isfile(os.path.join(LANE, doc)))
fire = read(os.path.join(LANE, "FIRE-RECIPE.md")) if os.path.isfile(
    os.path.join(LANE, "FIRE-RECIPE.md")) else ""
ck("fire recipe names the pinned module",
   "006a94cf6bcb6f53887c83aa4ad3a0f057358301d9a350a139ae178acac0a183" in fire)
ck("fire recipe carries the 5 GHz beacon precondition",
   "beacon" in fire.lower() and "G2" in fire and "D0" in fire)

print("RESULT: %u checks, %u failure(s)" % (CHECKS, FAILS))
sys.exit(1 if FAILS else 0)
