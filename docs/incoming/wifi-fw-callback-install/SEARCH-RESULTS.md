# SEARCH RESULTS - who writes the global callback G+0x370 (CA 0x174B34)?

**Lane:** `wifi-fw-callback-install` (shared task task-186), retry of the
failed task-185. **HOST-ONLY**: nothing here touched the router, the bench
link, UART or the network; every fact was produced offline from the pinned
local artifacts. This file is the **search half** (committed first), the
analysis half is in `ANALYSIS.md`.

## The question

The bench read of 2026-11-27 on our host-only boot shows
`G+0x370 = CA 0x174B34 = NULL` (band-1 MAC-start callback) while
`G+0x374 = CA 0x174B38 = 0x00057639` (beacon-start callback) and the G head
(`CA 0x1747C4`) are non-NULL. VAP start-on-band `CA 0x971D8` calls both
slots. Who INSTALLS `G+0x370`?

## Verdict of the search

**No instruction of the 928,920-byte firmware image installs `G+0x370` -
by any addressing form examined. The other five slots in the same
hook block are installed at firmware boot by one initializer function,
`CA 0x5B424`, called from `CA 0x48442` in the pre-HCC boot init. The
installer runs on our boot as well (the bench-read `G+0x374` value equals its
literal 0x57639), so the NULL slot is not a boot-order difference: the image
simply never populates that one slot.**

Evidence grade: **[V]** for every instruction address/value quoted,
**[I]** for the enclosing-function starts (prologue heuristic, see
`tools/fw_callback_install_re.py`), **[U]** for the intended installer
(analysis half).

## 0. Artifacts and conventions

* firmware (primary): `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`,
  928,920 B, sha256 `7fc87e20...311b` (**matches the pinned desc-pool identity**),
  `evidence/selftest.txt:1-2`.
* `/home/shibbityshwab/pd4-rebuilt.bin` is the **same length but NOT
  byte-identical** (59 differing bytes, e.g. file offsets 0x917A.. and
  0xC8000 ish); this lane uses only the sha-pinned recovery-extract copy.
  [V] local compare.
* CPU = file offset + **0x40000**; BAR0 = CPU + 0x6B8000; bar0read phys =
  0x40000000 + BAR0 (= CPU + 0x46B8000). These are the committed lane
  conventions.
* G = `CA 0x1747C4` is **outside the image** (file offset 0x1347C4 >
  0xE2C98): it is a runtime-RAM global, reachable only through the
  `CA 0x1747C4` pointer literal. [V] `evidence/selftest.txt:3`.
* Tool: `tools/fw_callback_install_re.py` (selftest 20/20 PASS,
  `evidence/selftest.txt:22`). It decodes the whole image with the OpenWrt
  lab `arm-openwrt-linux-objdump` and runs a register/offset taint scan from
  every G pointer-literal load, including one level of direct callee arg
  propagation and conservative invalidation of unknown writes.

## 1. No instruction anywhere forms the address CA 0x174B34 / 0x174B38

* `movw`/`movt` immediates `0x4b34`/`0x4b38`: **0** in the whole image.
* raw little-endian literal `34 4b 17 00` / `38 4b 17 00`: **0**
  occurrences; `evidence/selftest.txt:4-5`.
* The only way to reach G is the `CA 0x1747C4` literal: 75 occurrences, 74
  referenced by an `ldr`, 1 data occurrence at `CA 0x1060A8`;
  `evidence/search.txt` section 2. All 76 load sites were taint-scanned
  (section 3).

## 2. The five installed slots and their exact writer instructions

All in the hook block of the same initializer (enclosing function start
`CA 0x5B424` [I], first instruction `movs r3,#64; stmdb sp!,{r4-r9,sl,fp,lr}`,
called by `bl 0x5b424` at `CA 0x48442`):

| slot | writer instruction CA | stored value | source |
| --- | --- | --- | --- |
| G+0x364 | `0x5D13A  str.w r3,[fp,#868]` | `0x000565C5` | literal `CA 0x5D27C` |
| G+0x368 | `0x5CFC2  str.w r3,[fp,#872]` | `0x00052BE1` | literal `CA 0x5D1E8` |
| G+0x36C | `0x5CF60  str.w r2,[r3,#876]`  | `0x000727D1` | literal `CA 0x5D1C8` |
| **G+0x370** | **none - no store exists** | - | - |
| G+0x374 | `0x5D144  str.w r3,[fp,#884]` | `0x00057639` | literal `CA 0x5D280` |
| G+0x378 | `0x5C3B6  str.w r3,[r4,#888]` | `0x00068C0B` | literal `CA 0x5C540` |

`evidence/selftest.txt:8-13`, `evidence/search.txt` section 11,
`evidence/installer-callsite.txt:38-277`. The same function also installs
many other G callbacks (the taint scan lists ~140 distinct offsets), i.e. it
is the general G callback-table initializer, not a band-specific one.
A second, boot-visible function `CA 0x5967C` **clears** G+0x368 to 0 and is
called from `0x7D1CC/0x7D226`; it installs nothing at G+0x370 either.

## 3. WHEN the installer runs (boot order)

`CA 0x48442: bl 0x5b424` sits in the pre-HCC boot-init body that
* zeroes `CA 0x1471C8` (2 B), `CA 0x118CF0` (96 B, 0xFF), `CA 0x176938`
  (0x1004 B) at `0x48422/0x4842A/0x48438`, then
* calls the G-callback installer `bl 0x5b424` at `0x48442`, then
* builds the descriptor-pool default at `CA 0x485FA..0x4861E` and calls the
  populator `bl 0x1017F8` at `CA 0x48620`.

So the callback table is installed **once per firmware boot, before the
pre-HCC/HCC handover, immediately before the descriptor-pool default**.
`evidence/installer-callsite.txt:1-36`, `evidence/search.txt` section 6.
The desc-pool boot block itself (`CA 0x485FA..0x4861E`) contains **no G
store at all** (`evidence/search.txt` section 6) - it is *not* the callback
installer (candidate eliminated).

## 4. The image-wide "no writer" checks that back the verdict

1. Taint scan from all 76 G-pointer load sites (plus direct callees that
   receive G): **G+0x370 store count = 0**; `evidence/selftest.txt:8`.
2. Every word-store in the image with immediate offset `880 (0x370)` -
   17 sites - has a non-G base (per-link/per-VAP structs loaded from
   `[rX+0x2D8]`, object `CA 0x14A298`, tag-dispatch structs); none is G;
   `evidence/search.txt` section 12.
3. No `strd`/`stm` double-word or block store covering `0x36C..0x378`
   with a G base exists (the parse covers str/strb/strh/strd/stm with
   taint-tracked base and `add/sub` pointer arithmetic).
4. G+0x370 has no producer value either: no function pointer equal to any
   "band-1 MAC-start-like" candidate is stored to it (nothing is stored).

## 5. Direct evidence that the installer DID run on our host-only boot

The bench read `G+0x374 = 0x00057639` **exactly equals** the initializer's
literal `CA 0x5D280`; likewise the G head read is non-NULL. Therefore the
boot-init block above executed on our host-only boot, so the missing
G+0x370 is **not** "our boot skipped the installer" - the same installer
runs and just never writes that slot. This is the key differential result
for the analysis half.

## 6. What the search does NOT answer (analysis half)

* the *intended* callback for G+0x370 (which firmware CA should be there)
  and what the band-0 counterpart pair (`0x36C`/`0x378`) vs the called
  pair (`0x370`/`0x374`) means;
* whether the stock boot (or the vendor platform module `hi5622v100_plat.ko`)
  installs the slot by a non-image path (host BAR0/H2D write) - the host
  modules contain **no** literal of `0x174B34`, `0x174B38`, `0x1747C4`,
  `0x82CB34` or `0x4082CB34` [V] local scan of all `hi_*.ko`, so any host
  path would have to obtain the address dynamically;
* the read-only probes and the fire recipe.

## Reproduce

```
cd opensource/docs/incoming/wifi-fw-callback-install
python3 tools/fw_callback_install_re.py --selftest    # 20 PASS, 0 FAIL
python3 tools/fw_callback_install_re.py --search      # evidence/search.txt
python3 tools/fw_callback_install_re.py --dump 0x483EC 0x4844A
python3 tools/fw_callback_install_re.py --dump 0x5CF30 0x5D160
```

## Publication boundary

No vendor binary, firmware byte, extracted table or credential is committed:
the tool reads the pinned local artifacts at run time and the evidence files
carry only derived instruction addresses and small constants. No device,
bench link, UART or network access was used.
