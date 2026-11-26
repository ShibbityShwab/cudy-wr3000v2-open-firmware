# REPORT - rebasing the H2D g0 id 0x20 app/vendor IE injection onto the IMAGE #3 merged module

**Lane:** app-ie-rebase (task-235), 2026-11-26. **HOST-ONLY** (no bench, no
device, no flash). Parent lane: **wifi-app-ie** (task-234, commits `99665ae`
/ `32462c0`, `../wifi-app-ie/`). Base taken from: **wifi-image-3** module
build (`../wifi-image-3/build-evidence/module-build/`).

## 1. What was rebased and why

The task-234 integration (the cfg80211 vendor-IE collector -> the H2D **g0 id
0x20 `hmac_config_h2d_send_app_ie`** frame -> the firmware per-VAP IE slot
table) was authored, built and KAT-tested on the **R5-order base tree**
(`/tmp/r5-build`, the `wifi-band1-start-send-5` chain; base module 65
params, rebuilt form `24f7221b...` 383,240 B). The bench IMAGE #3 runs the
**merged module** `a717c86d...` (358,544 B, 64 params: the F2 5 GHz chain +
rx-refill watermark + BA skeleton + HE caps). Its source tree is
`/tmp/wifi-image-3-module`. Rebasing onto that tree gives a module that is a
**one-integration delta** from the module the bench actually runs, instead of a
different driver lineage.

## 2. The base is the shipped IMAGE #3 module (proof)

A control build of `/tmp/wifi-image-3-module` (no patch) reproduces the shipped
artifact exactly:

| artefact | control build | shipped IMAGE #3 pin | verdict |
| --- | --- | --- | --- |
| installed | 358,544 B `a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed` | same | **MATCH** |
| `.text` | 160,592 B `494ccd42100e3e3792738bb0a94419c7e549b86a11812ac505a7b1df2b18f2a1` | same | **MATCH** |
| params | 64 | 64 | **MATCH** |

Both the 97-file source manifest (`evidence/base-tree.sha256`) and this control
build are recorded in `evidence/BASE-CONTROL-BUILD.txt`. The rebase is
therefore on the real bench module lineage, not a look-alike.

## 3. The rebase was NOT mechanical - the delta

The task-234 patch applies cleanly to the R5 tree only. The IMAGE #3 lineage
descends from the F2 5 GHz chain and does **not** contain the R5 band-start /
VAP-start-g0 ladder, so the patch's integration points do not exist. The
rebased patch keeps the portable model and re-implements the integration at
the IMAGE #3 ladder's equivalent points:

| # | item | R5-order base (task-234) | IMAGE #3 merged base (this lane) | rebased treatment |
| --- | --- | --- | --- | --- |
| 1 | tree lineage | band1-start-send-5 chain, 65 params | F2 + T4 + BA + HE, 64 params | base = `/tmp/wifi-image-3-module` (control-built to `a717c86d`) |
| 2 | band-start chain | `hi5622_band_start.c/.h`, `hi5622_vap_start_g0.c/.h` present | **absent** (no `0x0242` band start, no `0x0065` g0 step) | app-IE step attaches to the lineage's **single VAP start** (`ap_vap_start`) |
| 3 | plan steps | VAP_ADD 1 .. VAP_START_G0 7, BAND_START 8, VAP_START 9, MAX 10 | VAP_ADD 1 .. VAP_START 7, MAX 8 | `HI5622_BEACON_STEP_APP_IE = 8`, MAX 9 |
| 4 | plan signature | `hi5622_beacon_plan(enabled, vap_start, vap_start_g0, desc_table, dfs_mode, band_start, out, max)` | `(enabled, vap_start, desc_table, dfs_mode, out, max)` | `(enabled, vap_start, desc_table, dfs_mode, app_ie, out, max)` |
| 5 | placement | between the `vap_start_g0` step and the `0x0242` band start | after the last step (`ap_vap_start`), before `start_ap()` commits | documented deviation (below) |
| 6 | gate | `vap_start_g0 == 1` (a started VAP exists) | no such parameter | composite `hi5622_app_ie_vap_start_ok()` = `beacon_vap_start \|\| ap_start_mode != 0` (either the explicit `ap_vap_start` post, or the WAL carrier sequence owns it) |
| 7 | VAP index | `hi5622_vap_add_index` | absent; the lineage uses `hi5622_vap_start_module` | `vap_start_module` -> `hdr[3]` |
| 8 | read-back wait | `HI5622_VAP_START_G0_WAIT_MS` (300 ms) | absent | local `HI5622_APP_IE_WAIT_MS = 300u` (same value, diagnostic only) |
| 9 | Kbuild | object after `hi5622_vap_start_g0.o` | no such object | `hi5622_app_ie.o` after `hi5622_he_caps.o` |
| 10 | status line | `vap_start_g0=%u` from the parameter | same portable model | the model is **byte-identical**; its `vap_start_g0` field is fed the composite condition (commented at the call site) |

**Deliberate deviation, #5 (placement).** On the R5 tree the frame sits after
`0x0065 VAP start (vap_start_g0)` and before `0x0242 band start`. The IMAGE
#3 lineage has no `0x0242` step and only one VAP-start step (`ap_vap_start`,
posted after the AP config), so the frame is posted **after** that step, still
before `start_ap()` commits. Rationale: (a) the step's contract is "a VAP
exists and its IE table is installed at the end of the vendor AP-start order";
(b) the vendor's own caller `hmac_config_set_app_ie_to_vap` (CA 0x60298) posts
this message from its VAP-configuration path, and the vendor's WEXT
`nlwifi_set_app_ie` updates vendor IEs on a **running** AP - the firmware frame
builders read the slot table per frame, so later beacons/probe responses still
carry it; (c) no `0x0242` arming exists on 2.4 GHz, which is the rung this fire
targets. This is the one place where the rebased integration is a design
decision rather than a textual re-application; it is called out for review.

**Invariants preserved.** `src/hi5622_app_ie.c/.h` are byte-identical to
task-234 (`19c9cbf7...` / `e0f3b39f...`), so the wire format, the collector,
the fail-closed gate and the byte-exact stock-frame vector are unchanged; the
host KAT is the task-234 suite and still passes **55/55**. The failure
semantics are the same: `app_ie>1`, `app_ie_type>19`, a missing beacon_static
ladder, a missing VAP start, a malformed cfg80211 IE list, a builder/post
failure -> refuse **before / abort the sequence**, and `app_ie=0` posts
nothing.

## 4. Grades and limits

* **[V] inherited** - the 0x20 frame geometry, the handler `CA 0x85631`
  bounds (616/608), the VAP lookup by `hdr[3]`, the slot table
  `vap+0x24c+20*type`, the stock-frame byte equality: from the task-234
  decode (`../wifi-app-ie/TABLES.md` T1-T5), unchanged.
* **[h] host-built** - the rebased integration: 0 warnings / 0 errors, 66
  params, plan/step witnesses inside the module, KAT 17/17, patch round-trip.
* **[I]** - the per-VAP IE class -> frame mapping (inherited T6), and the
  **placement effect** on-air (after the VAP start vs before). These need the
  bench fire (`FIRE-RECIPE.md`) and the laptop observer.
* **NOT verified here:** no module was fired, no firmware byte was written, no
  device was contacted. The `.ko` is local evidence only.

## 5. Evidence map

| claim | artefact |
| --- | --- |
| base tree = IMAGE #3 module source | `evidence/base-tree.sha256` (97 files), `evidence/BASE-CONTROL-BUILD.txt` |
| patch reproduces the built tree | `evidence/PATCH-ROUNDTRIP.txt`, `patches/0001-app-ie-image3.patch` |
| build identity | `evidence/BUILD.txt` (66 params, `2a2fff35...`, `.text` `c1ad1379...`) |
| host KAT 55/55 | `evidence/HOST-TESTS.txt` |
| module KAT 17/17 | `evidence/KAT.txt` |
| source pins | `evidence/source-pins.txt` |

Reproduce: `tools/build-app-ie-image3.sh` (needs the local IMAGE #3 scratch
tree + the OpenWrt 6.18.60 toolchain); verify: `tools/kat-image3.sh`.
