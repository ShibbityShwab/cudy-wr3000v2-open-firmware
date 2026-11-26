# CHAIN - the R5 module chain, rebuilt from the committed build scripts

**Lane:** `wifi-r5-rebuild` (shared task **task-225**) - reboot of the
`wifi-band1-start-send-5` (task-213) deliverable after the host `/tmp` chain
was cleaned. **HOST-ONLY:** the local 6.18.60 kernel tree and the committed
patches are read in place; nothing is fired, flashed or read from the device;
the resulting `.ko` is LOCAL EVIDENCE ONLY and is never committed (this repo
stores no binary artifacts).

## Result: byte-exact reproduction of the installed form

Running the chain below on 2026-11-26 (host-only) reproduced **every** pinned
stage artifact and the final module **byte-for-byte**:

| form | size | sha256 |
| --- | ---: | --- |
| stripped `/tmp/r5-rebuilt/hi5622_wifi.ko` | **383,240 B** | **24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c** |
| unstripped (local, not a deliverable) | 1,424,680 B | 233df986cfcd2c6a5936a5cab532e9d1986e473b40ca194144f02a2a036a0cfa |
| `.text` section | 157,076 B | a727c339714b8826ea069194a50d376622175ec5adf8e7b537442edc7731d11d |
| params | **65** | (unchanged: no new knob) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` | |

The intermediate gate-inverted module the bench actually deployed - the R4b
tree + the G9/G11 inversion, **9bf1d846...** 382,376 B / 65 params - was also
reproduced exactly (stage 9). **There is no drift**: the chain scripts' pins,
the lanes' recorded artifact pins and the rebuilt artifacts all agree, down to
the whole-file sha, because the canonical `/tmp` work paths were used (the
whole-file sha is path-sensitive through build metadata; see *Limits*).

## The exact chain, in order (scripts + the one patch-only step)

Each stage consumes the previous stage's **tree** (or the pinned artifact where
the script checks one) and applies its committed patch. "Pinned artifact" is
what the lane recorded and what this rebuild reproduced (size / params /
sha256). Scripts are relative to `opensource/docs/incoming/`.

| # | task list step | lane / script | consumes | applies | produces (repository path) | pinned artifact (size / params / sha256) | run (s) |
| ---: | --- | --- | --- | --- | --- | --- | ---: |
| 0 | (base tree of step 1) | `wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh` | `/tmp/wifi-5ghz-ready-build` (present; its own script pins that base's artifact `13366151...`) | `wifi-5ghz-vap-msg/patches/0001-vap5g-order.patch` | `/tmp/wifi-vapmsg5g-build` | 353,652 B / 58 params / `13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf` | 12 |
| 1 | (1) the base | `wifi-5ghz-beacon/tools/build-beacon5g-ready.sh` | `/tmp/wifi-vapmsg5g-build` | `0001-beacon5g-gate.patch` | `/tmp/beacon5g-build` | 356,600 B / 59 / `bc1cc0ead2c5b98a35eea875c353dc7e5bcf2e39b61ba9427408b714ea9a1bbb` | 11 |
| 2 | (2) | `wifi-r1-band-diff/tools/build-r1-band.sh` | `/tmp/beacon5g-build` | `0001-vap-start-band.patch` | `/tmp/r1-band-build` | 356,612 B / 59 / `e8b17f7238afce459db61a9ca65b9a0a1336ce232c933c65990d444350e4abd4` | 12 |
| 3 | (3) | `wifi-desc-table-send/tools/build-desc-table-send.sh` | `/tmp/r1-band-build` | `0001-desc-table-send.patch` | `/tmp/desctable-build` | 360,448 B / 60 / `aac6177ce84c972dafff67061951c2fe28e31a330f636c7a185c1511dfe800e3` | 11 |
| 4 | (4) | `wifi-dfs-mode-send/tools/build-dfs-mode-send.sh` | `/tmp/r1-band-build` + the sibling desc-table patch | `0001-dfs-mode-send.patch` | `/tmp/dfsmode-build` | 364,192 B / 61 / `f540856776e77c00ff1a526582466763bac9a3e63834264099faf3a3a10266ac` | 12 |
| 4b | (F2) | **patch only** - `wifi-fw-dfs-worker/patches/0001-fieldb-context.patch` | `/tmp/dfsmode-build` | field B 0x04 -> 0x00 in the 0x00F7 + 0x01F7 builders | the **F2 tree** in place at `/tmp/dfsmode-build` | F2 tree pins (6 shas, below); the F2 fire artifact 364,184 B / 61 / `12427b86...` is pinned by the next script but was not present locally | 0 |
| 5 | (5) the 0x0242 | `wifi-band1-start-send/tools/build-band-start-send.sh` | the F2 tree | `0001-band-start-send.patch` | `/tmp/bandstart-build` | 371,212 B / 63 / `e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028` | 11 |
| 6 | (between 5 and 6; required) | `wifi-band1-start-send-2/tools/build-vap-bound.sh` | `/tmp/bandstart-build` | `0001-vap-add-index.patch` | `/tmp/vapbound-build` | 373,056 B / 64 / `b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71` | 12 |
| 7 | (6) the 0x0065 | `wifi-band1-start-send-3/tools/build-vap-start-g0.sh` | `/tmp/bandstart-build` | send-2 patch + `0001-vap-start-g0.patch` | `/tmp/g0start-build` | 382,072 B / 65 / `bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711` | 12 |
| 8 | (7) the order fix | `wifi-band1-start-send-4/tools/build-r4b-order.sh` | `/tmp/bandstart-build` | send-2 + send-3 patches + `0001-r4b-order.patch` | `/tmp/r4border-build` | 382,188 B / 65 / `b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102` | 11 |
| 9 | (8) the gates | `wifi-vap-start-gates/tools/build-gates.sh` | `/tmp/r4border-build` | `0001-g9-g11-rf-enable-invert.patch` | `/tmp/vsg-build` | **382,376 B / 65 / `9bf1d846e730ce57a5adfef9a2fed9ab589fcba159b6cd08165339917673cf82`** | 12 |
| 10 | (9) the R5 order | `wifi-band1-start-send-5/tools/build-r5-order.sh` | `/tmp/r4border-build` + the gates patch (it applies it itself) | `0001-r5-order.patch` (tests: `0002-r5-selftest-update.patch`) | `/tmp/r5-build` | **383,240 B / 65 / `24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c`** | 13 |

Run start **2026-11-26T04:53:21Z**, end **2026-11-26T04:55:30Z**, total
**129 s** (recorded in `evidence/chain-run.log`; per-stage logs in
`evidence/stage-*.log`). A prior independent run of the same chain (before
`tools/reproduce.sh` existed) took 118 s and produced the same hashes - the
chain is deterministic at the canonical paths.

Two ordering subtleties the chain scripts implement and this rebuild had to
reproduce explicitly:

* **Stage 4b is not a lane script.** `build-band-start-send.sh` applies the F2
  field-B patch **in place** to `/tmp/dfsmode-build`, and only when that base
  tree is *absent* (it then rebuilds the dfs tree itself). Re-running stage 4
  first makes the tree the fresh 0x01F7 tree, so the F2 step must be applied
  between stages 4 and 5 - `tools/reproduce.sh` does it as stage 04b with the
  patch sha pinned and the six F2 tree shas asserted.
* **Stage 6 is required but is not in the task's step list.** The committed
  order is send (0x0242) -> **send-2** (explicit VAP index, 64 params) ->
  send-3 (0x0065 = R4) -> R4b -> gates -> R5; the 65-param R4/R4b trees carry
  `vap_add_index` from send-2, so leaving send-2 out is not an option.

The R5 script applies the gates patch itself (with the same result as stage 9)
when building from `/tmp/r4border-build`; stage 9 exists to reproduce the
deployed `9bf1d846` module independently, and the R5 script compares against
its artifact (`/tmp/vsg-artifact/hi5622_wifi.ko`) when present.

## Patch pins in force (all committed in the repo)

| patch | size | sha256 |
| --- | ---: | --- |
| `wifi-5ghz-beacon/patches/0001-beacon5g-gate.patch` | 20,569 B | `675fcbb031c44021f8a1361e826d2f2443298f1befc3ab1dac1173f212d601ad` |
| `wifi-r1-band-diff/patches/0001-vap-start-band.patch` | 2,536 B | `573e0f6ceb1dcb180c0f8990845e8b49389036b3427205d89f2528282a7b83d4` |
| `wifi-desc-table-send/patches/0001-desc-table-send.patch` | 25,152 B | `9c42076f4f5faecf352f6aecf5d6ba8b88edb73a454e26872ec543233cdfce2f` |
| `wifi-dfs-mode-send/patches/0001-dfs-mode-send.patch` | 26,066 B | `75959d122c57afe4af4e34dc4f6736f56260f9bb1bafeaa82b5a0ea7f58e5e3e` |
| `wifi-fw-dfs-worker/patches/0001-fieldb-context.patch` (F2) | 1,221 B | `0530ca45528e0601af5a6f8b8523e3ed8e43cd2237c2361f6cfd03aa3504f926` |
| `wifi-band1-start-send/patches/0001-band-start-send.patch` (0x0242) | 39,829 B | `3ddda8524b782063837fd23a4b2fab1c666f4826b8fa76da394275e6a9de308b` |
| `wifi-band1-start-send-2/patches/0001-vap-add-index.patch` | 8,294 B | `968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb` |
| `wifi-band1-start-send-3/patches/0001-vap-start-g0.patch` (0x0065) | 47,945 B | `0a8a1c1bda3ebaaf05933b670d996a2962f2679ecf0075237bf6d4adef30fc4c` |
| `wifi-band1-start-send-4/patches/0001-r4b-order.patch` | 34,330 B | `3628899843628984b1373f9c1e2ccd6539aff8920ac3188401a24a83b1c21775` |
| `wifi-vap-start-gates/patches/0001-g9-g11-rf-enable-invert.patch` | 8,173 B | `c806bc128b67b5ab92c558d3f3c691e0f9a74aa02993b95a193bd645bd3e1f98` |
| `wifi-vap-start-gates/patches/0002-gate-selftest-update.patch` | 1,573 B | `766209ebe1a972fd937212af36d782e1bad227d0c76a965bcc6bcdb2a8d2d2b6` |
| `wifi-band1-start-send-5/patches/0001-r5-order.patch` | 25,415 B | `e464f4cf0038859705be492967771e7c3294ef7ba84275c96bb4b473a6b86cf7` |
| `wifi-band1-start-send-5/patches/0002-r5-selftest-update.patch` | 4,857 B | `f35d34e4370da5469d24b0defd078aec8dacd4895b2e1226d8135c72d32cba4e` |

The F2 tree pins enforced by `build-band-start-send.sh` (and by stage 04b of
`tools/reproduce.sh`) are:

    hi5622_scan.c       8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7
    hi5622_scan_logic.c 75b9bdd5fd34bcd3862a7e185fb48bc98f6012c217b04fa5030ced899502f07c
    hi5622_scan_logic.h b83e74cc5bd834eb981e0dc07f197e6c4c3cdc9bd4eb60107b35b1c973e0766e
    Kbuild              900fb204ad817e787a0cde9201a02c6dc34d69d893ef0e4f06d140630fcb0759
    hi5622_dfs_mode.h   cd3b76aa75c5153654ca1e981d0f767fc100e7cb8e83406c55a43d0b4ce3c70b
    hi5622_desc_table.c 5af935f29aa9a329aba365da1753c5edd896b74cfe1c0c50665de13b39797810

## Final-stage verification (what proves the deliverable)

* every stage script's own contract checks passed (0 warnings / 0 errors /
  0 undefined symbols, debug-strip keeps `.text` identical, cfg80211
  `Module.symvers` 12 lines, param counts 58 -> 65 as designed);
* the five R5 source/ordering strings are present in the final module
  ("AP config posted BEFORE the 0x0065 VAP start / 0x0242 band start (R5)",
  the beacon-template line, both reordered post lines, and the corrected
  G9/G11 verdict strings);
* the R5 lane's host suites ran inside stage 10: **295 C selftest checks + 110
  Python order/contract checks, 0 failures** (the same 405 checks).
* independent re-verification of the copied deliverable is in
  `evidence/dev-verify.txt`: sha256, size, params, vermagic, `.text` sha,
  stripped-vs-unstripped `.text` identity, symvers, warning count.

## Reproduce

    opensource/docs/incoming/wifi-r5-rebuild/tools/reproduce.sh
    opensource/docs/incoming/wifi-r5-rebuild/tools/reproduce.sh --with-e2-rebuild

The second form also rebuilds the stage-1 base tree (stage 0). Output:
`/tmp/r5-rebuilt/hi5622_wifi.ko` (+ `.unstripped`), logs in
`/tmp/r5-rechain-logs/`. The script asserts the pinned size/sha/params of
**every** stage artifact and fails closed on any drift.

## Environment

| item | value |
| --- | --- |
| kernel tree | `/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60` |
| staging / toolchain | same lab root, `staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi` (OpenWrt GCC 14.4.0) |
| host | Linux 7.3.1-1-cachyos x86_64; host gcc 16.2.1; GNU patch 2.8.1; kmod `modinfo` 34.3 |
| canonical paths | the scripts' default WORK/ART paths (`/tmp/<lane>-build`, `/tmp/<lane>-artifact`) - never overridden |

## Limits / honesty

* **Host-only.** Nothing here was fired, flashed or read from the device; the
  bench and the recovery door were not contacted. The fire belongs to the Lead
  and follows `../wifi-band1-start-send-5/FIRE-RECIPE.md` (unchanged: the
  rebuilt module is byte-identical, so the recipe needs no delta).
* The `.ko` is local evidence only and is **never committed** (no vendor
  binaries / no artifacts in tracked text). Deliverable on this host:
  `/tmp/r5-rebuilt/hi5622_wifi.ko`.
* The whole-file sha is reproducible only at the canonical `/tmp` work paths:
  an out-of-tree `WORK` path produces the same `.text` and the same 65 params
  but a few bytes of out-of-tree build metadata differ (R5 lane BUILD-NOTES).
  `tools/reproduce.sh` asserts the pinned whole-file sha at the defaults and
  fails closed otherwise.
* Not re-verified here: the F2 bench-fire artifact itself (`/tmp/f2-module.ko`,
  364,184 B / 61 params / `12427b86...`) was absent, so only the six F2 tree
  shas are enforced; and the pre-beacon lineage below stage 1 (the association
  tree that the E2 stage consumes) is guarded by each sibling script's own
  pinned base artifact rather than rebuilt by this lane.

## Files

| file | what |
| --- | --- |
| `CHAIN.md` | this document: the exact order, the pins, the timing, the limits |
| `README.md` | the lane at a glance |
| `tools/reproduce.sh` | reproduces the whole chain and asserts every pin |
| `evidence/chain-run.log` | the committed reproduction run (timestamps, stage pins, final identity) |
| `evidence/stage-*.log` | each builder's own output, 00 .. 10 |
| `evidence/dev-verify.txt` | independent verification of the rebuilt deliverable |
| `evidence/env.txt` | kernel tree, toolchain, host tools, canonical paths |
