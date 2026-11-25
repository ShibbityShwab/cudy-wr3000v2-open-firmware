# REPORT - the TWT setup path (the TWT-enable config event)

**Lane:** wifi-ax-twt-impl (shared task **task-199**), 2026-11-28.
**HOST-ONLY**: this lane never contacted the router, never flashed, never
fired.  The Lead fires with the fire recipe.  Every protocol byte below is
*our own host-built frame*; the vendor facts are the committed decode of the
sibling lane [wifi-ax-advanced](../wifi-ax-advanced/README.md) (TABLES T2/T3/
T5, FIRE-RECIPE R2).  No vendor source, object code, firmware byte,
calibration value, MAC or secret is reproduced.

## 1. The question

The merged 64-param module is the IMAGE #3 driver: the F2 5 GHz chain + the
RX watermark + the BA skeleton + the HE (802.11ax) wiphy advertisement,
every new gate fail-closed.  The 2.4 GHz HE AP is live on air (the HE AP with
the HE IEs, BSS color 25).  What is the smallest **host-testable** 802.11ax
feature that can be added without the RX gate (which blocks client
association)?  The sibling ax-advanced lane answered: **TWT enabling** - the
vendor host path posts a config event and the firmware writes two context
bits; no client is needed to post and read it back.

## 2. The decoded path (evidence: TABLES T1-T2)

1. user space: the vendor `wal_hipriv_twt_enable` parses one integer and
   accepts only **1 (TWT enable)** and **3 (enable + required)**; every
   other value is refused.
2. it posts a g0-id1 config event `{u8 value; u16 0x035D; u16 4}` with the
   value byte as the body - the frame is the sibling lane's T12 envelope:
   12-B HCC header + 8-B token + key u16 at hdr+20 + body length u16 at
   hdr+22 + body at hdr+24 + the inherited 4-byte 0xFF tail = **29 B**.
3. the firmware g0-id1 dispatcher (CA 0x43D58) selects the per-module
   context with **field B = hdr[3]** (context table CA 0x00119758 + k*0xB40,
   19 boot contexts), looks the key up in table A and calls the worker
   CA 0x41E5A with `(ctx, len, body)`.
4. the worker writes **`ctx+0xDA.bit6 = body[0].bit0` (TWT enable)** and
   **`ctx+0xDA.bit7 = body[0].bit1` (TWT required)** - no other state.
5. the per-user TWT **agreement** is a different, firmware->host event
   (`hdpp_d2h_user_twt_syn`, `{u16 uid; u8 flags}`, flags bit0 ->
   `user+0x1C8.bit0`) and needs an associated STA: **blocked by the RX
   gate**, out of scope here.

## 3. What was implemented

The change is `patches/0001-twt-enable.patch` on the merged base (6 files:
2 new, 4 edits; 517 insertions, 7 deletions), plus the two lane build-system
patches (the Kbuild merge fix).  Four parts:

### 3.1 The portable model (`src/hi5622_ax_twt.{c,h}`, new)

One source compiles in the kernel module and in the host selftest:

* `hi5622_ax_twt_precheck()` - the fail-closed parameter/precondition gate;
* `hi5622_ax_twt_field_b()` - derive field B from the live AP-start module
  context CA;
* `hi5622_ax_twt_build()` - the byte-exact 29-B frame;
* `hi5622_ax_twt_mode_name()` / `hi5622_ax_twt_status_line()` - dmesg and
  sysfs text.

### 3.2 The fail-closed parameter

```
static unsigned int hi5622_twt;
module_param_named(twt, hi5622_twt, uint, 0444);
```

| twt | behaviour |
| ---: | --- |
| **0** (default) | **no frame at all** - byte-identical to the merged base; no step in the plan |
| **1** | TWT enable: post the 29-B key 0x035D frame with body `01` after the VAP start |
| **3** | TWT enable + required: the same frame with body `03` |
| 2 | refused `-EINVAL` before any frame (not a value the vendor user path accepts / not a bit combination) |
| >3 | refused `-EINVAL` before any frame |

The gate additionally requires, and *refuses* rather than silently drops when
a request cannot be honoured:

* `beacon_static=1` - the TWT post lives in the vendor AP-start ladder;
* `beacon_vap_start=1` - no AP start, no TWT enable;
* `he_caps=1` - the wiphy advertises the AP HE capabilities (a TWT enable
  without the HE advertisement cannot lead to a TWT agreement);
* **band 0 (2.4 GHz)** - the only bench-proven HE AP shape; a 5 GHz AP is
  refused `-EOPNOTSUPP` (the band-1 MAC start is still open; adding TWT
  there would confound two variables).

### 3.3 Field B = the AP-start module context (the F2 lesson)

The F2 5 GHz lane paid for this: the frame header byte 3 is the firmware
**per-module context selector**, and a hard-coded vendor-wire value silently
targets the wrong module/band context.  This lane therefore does not put a
constant in the builder.  At post time the driver reads the AP-start module
context CA with the beacon lane's own recipe (module-table slot 0 CPU
0x171458 + base u16 CPU 0x172504 -> `mctx`), then

```
field_b = (mctx - 0x00119758) / 0xB40      /* 0..18, the 19 boot contexts */
```

* `mctx == 0` -> `-EAGAIN` (unbound; never guess);
* a CA that is not exactly a decoded context address -> `-ERANGE`
  (never guess);
* the builder also refuses a field B above 18.

The builder takes `field_b` as an argument and echoes it at `hdr[3]`; the
host test proves that changing field B changes *only* that byte.  On the
fired 2.4 GHz ladder the module context is ctx0, so field B = 0 - but the
code derives it, it does not assume it.

### 3.4 Placement and read-back

* The TWT post is the **last** AP-start step, inside the `beacon_static`
  branch, after the VAP start has been posted: the worker writes the
  per-module context bits and field B comes from the module context the AP
  start bound, so the post cannot precede the binding.
* The ordering contract was extended: `hi5622_beacon_plan()` gained the
  `twt` argument and the step `HI5622_BEACON_STEP_TWT_ENABLE` (value 8).
  For the fire shape (desc_table=0, dfs_mode=0, twt=1) the dmesg plan is
  `vap_add ap_chan ap_ssid ap_cfg ap_vap_start twt` (6 steps).
* After a successful post the driver performs a **bounded read-only**
  read-back of the firmware target (up to 100 ms, 10 ms steps):`ctx+0xDA &`
  `0xc0` against the expected 0x40 (twt=1) or 0xc0 (twt=3), logged as
  `TWT-enable readback MATCHES` or `readback not yet visible - the host
  probe pack decides`.  The read-back is diagnostic; the fire recipe's
  independent /dev/mem probe is the decisive one.
* A failure *after* the ladder (context unresolved, builder refused, frame
  not posted) is reported and the AP start latches DIRTY (the usual rule:
  power-cycle; no in-place retry).
* `cfg80211_status` gains one read-only line:
  `twt: param=N (mode) he_caps=M beacon_static=B last_rc=R posted=P refused=F`.

## 4. The dmesg contract (exact strings; TABLES T7/T8)

Good rung (twt=1, HE AP):

```
cfg80211: start_ap beacon_static=1 plan (6 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start twt
cfg80211: start_ap beacon_static: 'twt' posted (29 B, g0 id1 key 0x035D, flags=1 enable, field_b=0 = the AP-start module context 0x00119758)
cfg80211: twt post: module ctx=0x00119758 (field B 0) ctx+0xDA=0x40 expected bits 6/7=0x40 after 0 ms (TWT-enable readback MATCHES)
```

Refusal rungs (each before any frame): `twt=2`, `twt=1` with
`beacon_static=0` / `beacon_vap_start=0` / `he_caps=0`, and `twt=1` on
a 5 GHz AP.  The sysfs line is the same evidence after the fact.

## 5. Host tests (the contract; evidence/host-tests.txt)

`tools/run-host-tests.sh` - no kernel, no device:

| test | checks | what |
| --- | ---: | --- |
| `src/twt_selftest.c` | **54/54** | byte-exact 29-B frames for twt 1/3, field-B echo/range, field-B derivation from every context CA, the complete fail-closed matrix (parameters, AP shape, band, builder refusals, buffer, NULL), the status line |
| `src/plan_order_test.c` | **12/12** | the AP-start plan order against the patched `hi5622_scan_logic.c`: twt LAST, only for 1/3, dropped with the VAP start, inert for 2/4, truncation-safe |
| `tools/twt_frame_xcheck.py` | **62/62** | an independent Python re-derivation of the envelope bytes, field-B map and decision table (a second implementation catches transcription errors) |

Byte-exact frames (evidence/frames.txt):

```
twt=1 field_b=0: 000100001d00010000005a5a00000000010000005d03010001ffffffff
twt=3 field_b=0: 000100001d00010000005a5a00000000010000005d03010003ffffffff
twt=1 field_b=1: 000100011d00010000005a5a00000000010000005d03010001ffffffff
```

## 6. The build (evidence/build.txt, BUILD-NOTES.md)

The current base is the merged 64-param module of IMAGE #3.  Its source is
not one preserved tree, so `tools/build-twt-impl.sh` reconstructs it
deterministically from the **committed sibling patches**:

```
F2 5 GHz chain   = r1-band base + desc_table/0001 + dfs_mode/0001
                   + fw-dfs-worker/0001-fieldb-context
merged base      = F2 + rx-watermark/0001 + ba-agg/0001 + ax-he-caps/0001
                   (+ the Kbuild object line the HE patch cannot apply on F2)
TWT module       = merged base + patches/0001-twt-enable.patch
```

Proof that the reconstruction is the IMAGE #3 module: the rebuilt base
`.text` sha256 is exactly `494ccd42100e3e3792738bb0a94419c7e549b86a11812ac
505a7b1df2b18f2a1` - the IMAGE #3 module's `.text`.  The TWT module:

| identity | value |
| --- | --- |
| unstripped (local) | 1,454,516 B sha256 `76b967c6...b49dba` (hash is path-dependent: DWARF comp_dir) |
| `.text` (path-independent pin) | **162,196 B sha256 `1d7d6dda...5eb8e4`** |
| activity strip (`strip --strip-debug`) | 383,376 B sha256 `d271d195...faf9b2` |
| **installed form** (OpenWrt `strip-kmod.sh` NO_RENAME=1) | **362,540 B sha256 `ed13145a99a4b5dacc3b043abccddec333d51e8d129410c1c869d94544c01f93`** |
| parameters | **65** = merged 64 + `twt` |
| new symbols | `hi5622_ax_twt_build`, `_field_b`, `_mode_name`, `_precheck`, `_status_line` (+ the 4 param/status data symbols); **0 global symbols lost** |
| build | 0 warnings, 0 errors, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, depends=cfg80211, GPL |

The build is reproducible: the script verifies every source pin, rebuilds the
base and the TWT tree, and fails if either `.text` or the installed hash
drifts.

## 7. Honest limits (what this lane does NOT claim)

* **Not fired.**  No bench claim.  The Lead's fire decides whether the
  firmware consumes the frame (probe `ctx+0xDA`); this lane only proves the
  host side is byte-exact, ordered and fail-closed.
* **No TWT agreement.**  The agreement path is a firmware->host event that
  needs an associated STA; the RX gate (host frames = 0) blocks association
  today.  The client probe in the fire recipe is therefore conditional and
  cannot produce an on-air TWT claim until the RX gate is open.
* **No on-air TWT advertisement claim.**  The firmware worker writes two
  per-module context bits; whether/how the firmware beacon builder uses them
  is [U] (sibling lane).  The ext-TWT IE key 0x00E9 is decoded but not
  implemented (one variable per fire).
* **2.4 GHz only** by design (bench-proven HE AP); the 5 GHz TWT variant is
  a later rung after the band-1 MAC start.
* The read-back in the driver is diagnostic and bounded; a missing read-back
  is not silently retried - the fire recipe's host probe is the decider.

## 8. Files and evidence map

See README.md for the file table.  Evidence (all generated, no vendor
bytes): `evidence/host-tests.txt` (128 checks), `evidence/build.txt` (the
whole build + pins), `evidence/build-base.log` / `evidence/build-twt.log`
(0 warnings), `evidence/module-identity.txt` (65 params, symbols, .text,
installed hash, symbol preservation), `evidence/frames.txt` (the model's
wire bytes), `evidence/dmesg-strings.txt` (the exact contract strings from
the patched source), `evidence/diff-stat.txt` (the patch shape).
