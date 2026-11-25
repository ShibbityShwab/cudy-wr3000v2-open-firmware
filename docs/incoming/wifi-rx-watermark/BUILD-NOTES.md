# BUILD-NOTES - final association module + RX refill watermark

**Lane:** wifi-rx-watermark (task-156), host-only.  The `.ko` is local evidence
only and is **never committed**; this document is the regeneration recipe.

## 1. Chain

```
final association tree 251d08f0 (all-patch 5b587949 + wpa3-sae + ap-mgmt
  + sta-ops + data-tx)
  + ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
      (corrected device mgmt-TX carrier: raw HCC g0 id 6 control message;
       artifact ab9e18a4, 344,372 B, 56 params)
  + patches/0001-rx-refill-watermark.patch          <- this lane
      (rx_refill_wm; 3 files, disjoint from the mgmt-carrier patch)
  = FINAL ASSOCIATION + WATERMARK module (57 params, this document)
```

The association tree is produced by
`../wifi-stability/tools/build-association-module.sh` (which in turn uses
`build-all-patch-module.sh` when the all-patch tree is not present).  The
mgmt-carrier correction is task-151 (sibling-gpl lane); this lane only consumes
it so the rebuilt module keeps the carrier the Lead already hardware-accepted.

## 2. Reproduce

```sh
cd <repo>/opensource/docs/incoming/wifi-rx-watermark

# 1) host model test (no kernel): 127 checks, 0 failures
./tools/run-host-tests.sh

# 2) rebuild the module from the committed chain
./tools/build-final-module.sh
#    -> ${ART:-/tmp/wifi-rx-watermark-artifact}/hi5622_wifi.stripped.ko
#       + hi5622_wifi.ko.unstripped, build.log, params.txt

# 3) if /tmp was cleaned and the association tree is gone:
REBUILD_ASSOC=1 ./tools/build-final-module.sh
```

Overrides: `ASSOC_TREE` (default `/tmp/wifi-assoc-build/integ`), `WORK`,
`ART`, `KDIR_OVERRIDE`, `STAGING_OVERRIDE`.  The script stages a scratch copy,
applies both patches with `patch -p1` (dry-run first, conflict markers refused),
builds with the project 6.18.60 KDIR, and refuses on any warning/error/undefined
symbol.

Build environment found automatically:
`KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`,
`CROSS=.../staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-`.

## 3. Verification performed (all PASS)

| assertion | result |
| --- | --- |
| patch dry-run on the final association tree (assoc + mgmt carrier) | clean |
| patch dry-run on the pristine association tree (disjoint file sets) | clean |
| fresh staging (assoc tree + both patches) reproduces the built source tree | 0 diffs (83 files; only the generated `hi5622_wifi.mod.c` differs, as expected) |
| build warnings/errors/undefined symbols | 0 |
| param count | 57 (56 + `rx_refill_wm`) |
| required params present | `rx_refill_wm rx_enable rx_clear_status rx_drain_max mgmt_tx mgmt_rx sta_ops eapol_rx data_tx data_tx_peer wpa_keys beacon_static vap_start_first` |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| depends | `cfg80211` |
| strip leaves `.text` byte-identical | yes |
| cfg80211 provider symvers | 12 lines (unchanged); 8 cfg80211 imports incl. `cfg80211_bss_iter`, `cfg80211_mgmt_tx_status_ext`, `cfg80211_rx_mgmt_ext` |
| feature strings | `rx_refill_wm`, `RX watermark:`, `watermark refill`, `DP rx_arm`, `set_default_mgmt_key posted`, `data-tx tx: posts=` |
| corrected mgmt carrier symbols | `himg_build_hcc_mgmt_tx`, `himg_parse_rx_mgmt` present |
| baseline reproduction (assoc+mgmt without watermark) | rebuilt 344,372 B; `.text` byte-identical to the pinned `ab9e18a4` artifact; the only byte difference is the 20-byte GNU build-id (see below) |

Evidence: [evidence/build-chain.txt](evidence/build-chain.txt) (kernel
CC/LD lines), [evidence/identity.txt](evidence/identity.txt),
[evidence/patch-verify.txt](evidence/patch-verify.txt),
[evidence/module-params.txt](evidence/module-params.txt).

## 4. Artifact identity (the .ko that is baked/fired)

| item | value |
| --- | --- |
| stripped | **347,388 B sha256 `d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6`** |
| unstripped | 1,332,996 B sha256 `f11835e8c47839d6ccba368ea64800912bfe5e93bd5ef5581f868b73ce858dc7` |
| params | 57 |
| new param | `rx_refill_wm`: uint, percent of ring depth, default `75` (constant `HIL_RX_WM_PCT_DEF`), range 0..100 (`>100` clamps at use) |
| local path | `/tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko`; gitignored durable copy `<repo>/build/wifi-rx-watermark/hi5622_wifi.stripped.ko` |

The quoted sha is for that exact file.  **Linux module shas are build-path
dependent** through the 20-byte GNU build-id: ld SHA1-hashes the pre-strip
DWARF, whose `comp_dir` is the absolute build directory.  The independent
rebuild at `/tmp/wifi-rx-watermark-build/rebuild` produced a different sha but:

* `.text` byte-identical, `.rodata` byte-identical, `modinfo -p` identical;
* total size 347,388 B identical; only the 20-byte build-id note differs.

So: quote/compare the sha of the exact baked file; compare rebuilds by
`.text` + params (or run `tools/build-final-module.sh` and accept its sha).
The same effect was observed for the baseline: rebuilding assoc+mgmt at
`/tmp/wifi-rx-watermark-build/baseline` reproduced `ab9e18a4`'s code exactly
(`.text` identical) and differed only in the build-id.

## 5. What the patch changes

| file | change |
| --- | --- |
| `hi5622_rx_live.h` | new section 4: watermark constants + `hi5622_rx_wm_target/triggered/plan` declarations |
| `hi5622_rx_live.c` | the three portable rule implementations (range-checked, fail-closed) |
| `hi5622_dp_txrx.c` | `rx_refill_wm` module param; `dptx_live_read_checked`; `dptx_rx_wm_ring`/`dptx_rx_wm_refill`; watermark pass in both drain commits (with-completions and no-completion); watermark telemetry in the dmesg line and the `dp_rx_capture` `RX watermark:` line |

Nothing else is touched: no register constant, no ring layout, no enable
semantics, no mgmt/data path change.

## 6. Fire parameters

Same association recipe as
`../wifi-stability/ASSOCIATION-INTEGRATION.md` section 4 / the corrected
carrier recipe in `../wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md` section T1.6,
plus:

```
rx_refill_wm=75          # default; explicit for the record. control rung = 0
```

The default load (no `rx_enable=1`) keeps the whole RX consumer path inert; see
[TEST-LADDER.md](TEST-LADDER.md) for the rungs and stop rules.

## 7. Image integration (cross-lane record; NOT fired)

The wifi-final-image-2 lane (task-159) baked **this exact artifact** into FINAL
IMAGE #2 and verified it.  Recorded identity (source:
`../wifi-final-image-2/HASHES.txt`, `../wifi-final-image-2/VERIFY.txt`,
`../wifi-final-image-2/build-evidence/`):

| item | value |
| --- | --- |
| image docs / commit | [../wifi-final-image-2/](../wifi-final-image-2/) - opensource commit `58e3abb` |
| container (sysupgrade .bin) | 7,188,799 B sha256 `8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8` |
| rootfs UBI (the only changed flash member) | 3,801,088 B sha256 `31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d` |
| kernel slice | **UNCHANGED** 3,372,638 B sha256 `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f`; slot `1794a531...` (8,650,752 B kernelb/mtd12) is NOT a container member (rootfs-only flash writes mtd14) |
| module artifact baked | `/tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko` - 347,388 B sha256 `d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6` |
| module installed in the rootfs | `/lib/modules/6.18.60/hi5622_wifi.ko` - **328,788 B sha256 `b4c076bf37099b21bcb5055f23678ffee38e86da9b3975e36d5c50025b31065e`** = the same code under the image's rstrip/symtab strip (`.text` 146,448 B identical to the artifact; only symbols/debug removed; 57 params preserved) |
| verification | 84/84 PASS; 3 recorded bakes byte-identical; no device write, nothing fired |
| boot policy | uci `hi5622.main.rx_refill_wm=75` + the full 39-arg insmod policy; association gates (`mgmt_tx/mgmt_rx/sta_ops/eapol_rx/sta_user_del/data_tx`) stay fail-closed 0; bench default `autoload=0` |

This lane independently re-hashed the image lane's local artifact set
(`/home/shibbityshwab/wifi-final-image-2-artifacts/`): container, rootfs UBI,
kernel slice and installed module all match the records above, and the installed
module's 146,448 B `.text` is byte-identical to the baked artifact's `.text` (see
[evidence/image-integration.txt](evidence/image-integration.txt)).

Which sha to quote: `d2c608e7` = the built module artifact (source identity);
`b4c076bf` = the installed rootfs copy (image/rootfs verification).  They are
the same code - compare `.text` if in doubt.  The image's insmod policy already
passes `rx_refill_wm=75`, so the [TEST-LADDER.md](TEST-LADDER.md) rungs can run
from the image without adding the parameter by hand; the RX consumer still arms
only on the explicit attended `dp_rx_arm` write.  Nothing was fired by either
lane.
