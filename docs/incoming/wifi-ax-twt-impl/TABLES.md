# wifi-ax-twt-impl - evidence tables (T1-T10)

Grades: **[V]** = decoded from the pinned artifacts and re-asserted by the
sibling lane's RE tool (`../wifi-ax-advanced/tools/fw_ax_advanced_re.py
--selftest`, 221/221 PASS) or asserted by this lane's host tests; **[V-our]**
= our own host-built protocol bytes (asserted byte-for-byte by
`src/twt_selftest.c` and `tools/twt_frame_xcheck.py`); **[I]** = inference
from the surrounding decode; **[U]** = undetermined; **[H]** = host-side
contract (this lane's code/dmesg/sysfs, not a firmware claim).

Address conventions (sibling lanes): firmware addresses are **CPU addresses**
(CA) of the pinned local image, `CA = file offset + 0x40000`; module
addresses are **link-time .text section offsets** of the pinned local vendor
module.  Reproduce the vendor rows with the sibling lane's tool; this lane
stores **no** vendor byte.

## T1. The decoded TWT-enable path [V]

| step | artifact address | decoded fact |
| --- | --- | --- |
| user path | vendor module .text **0x131634** `wal_hipriv_twt_enable` | parses one integer, accepts only **1** and **3**: `cmp r0,#3; cmpne r0,#1; bhi error` at 0x1316BC |
| event build | .text **0x1316DC** `movw r3,#0x35D`, 0x1316E0 `mov r2,#4` | posts a WAL cfg event carrying key **0x035D**, body = the one value byte |
| host mirror | module .text **0x15E8E8** `hmac_config_twt_enable` | mirrors the two bits into the host object `+0xD2` bits 6/7, posts the same key/body on the g0-id1 carrier |
| dispatcher | firmware CA **0x43D58** | g0-id1 config: field B = `hdr[3]` selects `ctx = CA 0xFE578(0, hdr[3])`; key = `u16 hdr+20`; len = `u16 hdr+22`; body = `hdr+24`; calls `worker(r0=ctx, r1=len, r2=body)` |
| key table | firmware CA **0x10C23C** table A | row 89: key **0x035D**, worker value **0x041E5B** (Thumb bit set) |
| worker | firmware CA **0x41E5A** (0x041E5B & ~1) `hmac_config_twt_enable` | `ctx+0xDA.bit6 = body[0].bit0` (**TWT enable**), `ctx+0xDA.bit7 = body[0].bit1` (**TWT required**); no other write |
| module context | firmware CA **0x00119758 + k*0xB40**, k = 0..18 | the 19 boot-created per-module contexts; **field B is k** |
| agreement (out of scope) | module .text **0x2B6C** `hdpp_d2h_user_twt_syn` | D2H `{u16 uid; u8 flags}`, flags bit0 -> `user+0x1C8.bit0`; needs an associated STA = **RX gate** |

## T2. The g0-id1 frame layout [V] / [V-our]

Total = **28 + body_len**; for TWT body_len = 1 -> **29 bytes**.

| offset | size | value (TWT) | meaning |
| ---: | ---: | --- | --- |
| +0x00 | 1 | `00` | HCC group 0 |
| +0x01 | 1 | `01` | alloc nibble 1 / dst 0 / message id 1 family |
| +0x02 | 1 | `00` | fa (inherited family byte) |
| +0x03 | 1 | **field B** | **the AP-start module context** (this lane derives it; T5) |
| +0x04 | 2 | `1d 00` | total length 29 (LE u16) |
| +0x06 | 2 | `01 00` | message id 1 |
| +0x08 | 1 | `00` | fc |
| +0x09 | 1 | `00` | dst |
| +0x0a | 2 | `5a 5a` | envelope tag 0x5A5A |
| +0x0c | 8 | `00 00 00 00 01 00 00 00` | token (the fired envelope; semantics [U]) |
| +0x14 | 2 | `5d 03` | **key 0x035D** (LE) |
| +0x16 | 2 | `01 00` | body length 1 (LE) |
| +0x18 | 1 | **`01` or `03`** | body[0]: bit0 = TWT enable, bit1 = TWT required |
| +0x19 | 4 | `ff ff ff ff` | inherited 4-byte tail |

## T3. Our frames [V-our] (asserted byte-for-byte)

```
TWT enable (1), field B 0:       000100001d00010000005a5a00000000010000005d03010001ffffffff
TWT enable+required (3), field B 0: 000100001d00010000005a5a00000000010000005d03010003ffffffff
TWT enable (1), field B 1 (proves hdr[3] is not a constant):
                                 000100011d00010000005a5a00000000010000005d03010001ffffffff
```

(twt=1/field B 0 is the fire shape: the 2.4 GHz AP-start module context is
ctx0, so field B = 0 - but the code derives it, T5.)

## T4. Parameter contract and the fail-closed matrix [H]

`twt` (uint, sysfs 0444, default **0**):

| twt | meaning | frame |
| ---: | --- | --- |
| 0 | off (default) | **none** - plan has no twt step; byte-identical to the merged base |
| 1 | TWT enable | 29-B key 0x035D, body `01` |
| 3 | TWT enable + required | 29-B key 0x035D, body `03` |
| 2 | not a vendor-validator value / not a bit combination | refused `-EINVAL` before any frame |
| >3 | out of contract | refused `-EINVAL` before any frame |

Decision matrix (all host-tested; the kernel gate runs **before** the first
AP-start frame, so a bad request never leaves a partial ladder):

| twt | beacon_static | beacon_vap_start | he_caps | band | result |
| ---: | ---: | ---: | ---: | ---: | --- |
| 0 | any | any | any | any | **no-post, 0** (off never refuses) |
| 1 or 3 | 1 | 1 | 1 | 0 | **post**, flags = twt, expected ctx+0xDA bits = 0x40 / 0xC0 |
| 1 or 3 | 0 | - | - | - | **-EINVAL** (not the vendor AP-start ladder) |
| 1 or 3 | 1 | 0 | - | - | **-EINVAL** (no AP start, no TWT enable) |
| 1 or 3 | 1 | 1 | 0 | - | **-EINVAL** (HE AP not advertised) |
| 1 or 3 | 1 | 1 | 1 | 1 | **-EOPNOTSUPP** (5 GHz HE AP not bench-proven) |
| 2 | any | any | any | any | **-EINVAL** |
| >3 | any | any | any | any | **-EINVAL** |
| any | any | any | any | any | NULL out: **-EINVAL** |

Builder refusals (defence in depth): unarmed plan, flags not in {1,3},
field B > 18, inconsistent read-back mask/expectation, NULL wire/out_len/
plan -> `-EINVAL`; cap < 29 -> `-ENOSPC`; `*out_len` is zeroed before any
refusal.

## T5. Field B derivation - the AP-start module context (F2 lesson) [V]/[H]

Context table geometry [V]: ctx k CA = **0x00119758 + k*0xB40**, k = 0..18
(19 boot contexts); the AP-start module context is read with the beacon
lane's recipe (module-table slot 0 CPU **0x171458**, base u16 CPU
**0x172504**, `mctx = slot0 + (u16)base`).

| live `mctx` CA | derivation result |
| --- | --- |
| `0x00000000` | `-EAGAIN` - firmware has not bound a context; **refused, never guessed** |
| `0x00119758` | field B **0** (ctx0 - the fired 2.4 GHz ladder) |
| `0x0011A298` (= +1*0xB40) | field B 1 |
| `0x0011ADD8` (= +2*0xB40) | field B 2 |
| `0x001261D8` (= +18*0xB40) | field B 18 (last boot context) |
| `0x00126D18` (= +19*0xB40) | `-ERANGE` |
| `0x00119759` (misaligned) | `-ERANGE` |
| `0x00119757` (< ctx0) | `-ERANGE` |
| `0x00200000` | `-ERANGE` |

Why: the g0-id1 dispatcher uses `hdr[3]` as the per-module context index; a
frame posted with the wrong index writes the wrong module's `ctx+0xDA`
silently.  The F2 lane fixed exactly this class of bug in the 0x00F7/0x01F7
frames (`field B = 0x00 = the AP-start module context`).

## T6. AP-start ordering contract [H] (host-tested: 12/12)

`hi5622_beacon_plan(enabled, vap_start, desc_table, dfs_mode, twt, out, max)`

| case | plan |
| --- | --- |
| legacy / enabled=0 | (empty) |
| desc=0 dfs=0 twt=0 vap_start=1 | `vap_add ap_chan ap_ssid ap_cfg ap_vap_start` (5) |
| desc=1 dfs=1 twt=0 | `vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg ap_vap_start` (7) |
| **desc=0 dfs=0 twt=1 (fire shape)** | `vap_add ap_chan ap_ssid ap_cfg ap_vap_start **twt**` (6) |
| desc=1 dfs=1 twt=3 | `vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg ap_vap_start twt` (8) |
| twt=2 or 4 | twt step absent (kernel refuses first) |
| vap_start=0, twt=1 | `vap_add ap_chan ap_ssid ap_cfg` (4) - the post needs the AP |
| truncation (max 3) | never writes past max |

Step name: `hi5622_beacon_step_name(HI5622_BEACON_STEP_TWT_ENABLE) =
"twt"`.  The step is LAST because field B comes from the module context the
AP start bound and the worker writes the per-module context.

## T7. Driver read-back and dmesg/sysfs contract [H] (exact strings)

Good rung (twt=1; for twt=3: `flags=3 enable+required`, expected 0xc0):

```
cfg80211: start_ap beacon_static=1 plan (6 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start twt
cfg80211: start_ap beacon_static: 'twt' posted (29 B, g0 id1 key 0x035D, flags=1 enable, field_b=0 = the AP-start module context 0x00119758)
cfg80211: twt post: module ctx=0x00119758 (field B 0) ctx+0xDA=0x40 expected bits 6/7=0x40 after 0 ms (TWT-enable readback MATCHES)
```

The read-back is a **bounded read-only poll** (<= 100 ms, 10 ms steps) of
the firmware target; a timeout logs `readback not yet visible - the host
probe pack decides` (diagnostic, not a retry).

Refusals (each **before any AP frame**, so the ladder is not partial):

```
cfg80211: start_ap REFUSED - twt=2 (invalid) is not usable on this AP start (-22): needs 1 or 3 with beacon_static=1, beacon_vap_start=1, he_caps=1 and the 2.4 GHz band; no frame sent
cfg80211: start_ap REFUSED - twt=1 (enable) is not usable on this AP start (-22): ... (beacon_static=0 / beacon_vap_start=0 / he_caps=0)
cfg80211: start_ap REFUSED - twt=1 (enable) is not usable on this AP start (-95): ... (band 1, -EOPNOTSUPP)
```

Post-ladder failures (already posted AP frames -> the usual DIRTY latch; the
string says `power-cycle`): context unresolved (`-EAGAIN`/`-ERANGE`),
builder refused, frame not posted.

sysfs `cfg80211_status` gains one line:

```
twt: param=1 (enable) he_caps=1 beacon_static=1 last_rc=0 posted=1 refused=0
```

`last_rc` is `-ENODATA` (=-61) until the first attempt; mode names are
`off` / `enable` / `enable+required` / `invalid`.

## T8. Host tests and build identity [V]

| item | value |
| --- | --- |
| model selftest (`src/twt_selftest.c`) | **54/54 PASS** |
| plan-order test (`src/plan_order_test.c` vs the patched `hi5622_scan_logic.c`) | **12/12 PASS** |
| independent Python re-derivation (`tools/twt_frame_xcheck.py`) | **62/62 PASS** |
| merged-base `.text` pin (== IMAGE #3 module) | 160,592 B sha256 `494ccd42100e3e3792738bb0a94419c7e549b86a11812ac505a7b1df2b18f2a1` |
| TWT module `.text` pin | **162,196 B** sha256 `1d7d6ddae70a9044c917381c52a124fed6adf6546489fece2b0ab6de9f5eb8e4` |
| TWT module installed form | **362,540 B** sha256 `ed13145a99a4b5dacc3b043abccddec333d51e8d129410c1c869d94544c01f93` |
| parameters | **65** (merged 64 + `twt`) |
| symbols | 5 added (`hi5622_ax_twt_{build,field_b,mode_name,precheck,status_line}`); **0 global symbols lost** vs the merged base |
| build | 0 warnings, 0 errors; vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; depends=cfg80211; GPL |
| merged-base source pins | `hi5622_scan.c` 8fbc8e1e..., `scan_logic.c` 75b9bdd5..., `scan_logic.h` b83e74cc..., Kbuild b4f98773..., cfg80211.c 1af4d7f3..., dp_txrx.c 76da6d4b..., rx_live.c 4bf35afb..., rx_live.h afa37ecc..., ba.c ae51aa12..., he_caps.c de2d78b5... (full values in BUILD-NOTES.md) |
| TWT source pins | patched `hi5622_scan.c` d6474087..., `scan_logic.c` 73614030..., `scan_logic.h` 955b30c1..., Kbuild 1430f7af..., `hi5622_ax_twt.c` 65e39633..., `.h` 940efe65... |

## T9. The Lead's read-only probe pack (no writes)

| # | read (CPU address) | expected | meaning |
| ---: | --- | --- | --- |
| 1 | u8 **0x119832** (= ctx0+0xDA) | **0x40** (twt=1) / **0xc0** (twt=3), bits 6/7 only | **the decisive TWT-enable bit check**; 0 = the frame never reached the worker |
| 2 | u32 **0x1198E8** (= ctx0+0x190) | nonzero / 'the VAP object' (may read 0 right after a fresh base post - bench R2 race; non-gating) | context is bound (contextual) |
| 3 | ctx table: u32 **0x171458** (slot0), u16 **0x172504** (base) | slot0 = 0x00119758, base = 0x0000 [I] -> the dmesg `module ctx=0x00119758`, field B 0 | proves field B derivation |
| 4 | u32 **0x174B38** (= CBH+0x374, beacon hook) | stock 0x00057639 (context only; unchanged by this lane) | no regression of the beacon hook |
| 5 | page/state/mask sysfs (§fire recipe) | page 0x86807FEF / state 1 / mask 0 | chip health |

All reads are read-only; this lane's frame performs **no direct MMIO write**
- the only write is the 29-B H2D frame through the proven post path.

## T10. Failure modes and next moves

| observation | meaning | next |
| --- | --- | --- |
| plan line has no `twt` step | twt=0, or a bad/unusable twt refused earlier (look for the REFUSED line) | check the parameter/sysfs line; no frame was sent |
| plan line OK, no `'twt' posted` line | the lane never reached the completion path (earlier frame failed) | follow the earlier failure's rule (dirty latch -> power-cycle) |
| `'twt' posted` but no readback MATCH / probe 1 = 0 | the frame was posted but the worker did not consume it (wire/key/table) | capture the 29 B wire bytes from the post line; re-check key row 89; hand back to the firmware-]decode lane - do not re-post blind |
| `TWT-enable module context ... does not resolve` | the module table read did not return a decoded context (unbound/garbage) | probe rows 3; a fresh power-cycle + re-run of the AP ladder; do not hard-code field B |
| probe 1 = 0x40/0xc0 | **host-side TWT enable accepted by the firmware** | the TWT client probe (fire recipe H7); agreements remain RX-gated |
| an exception mask / page change / mask != 0 | chip anomaly | power-cycle, record, no retry |
