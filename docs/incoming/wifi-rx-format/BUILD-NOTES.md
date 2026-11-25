# BUILD-NOTES - RX-format-hardened module (task-163)

**Lane:** wifi-rx-format, host-only. The `.ko` is local evidence only and is
**never committed**; this document is the regeneration recipe and identity.

## 1. Chain

    final association tree 251d08f0
      + ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch  (ab9e18a4)
      + ../wifi-rx-watermark/patches/0001-rx-refill-watermark.patch      (d2c608e7 module)
      = base tree /tmp/wifi-rx-watermark-build/rebuild
      + patches/0001-rx-format-hardening.patch                            <- this lane
      = RX-FORMAT-HARDENED module (57 params, this document)

The base tree is exactly the tree of the module baked into FINAL IMAGE #2
(`d2c608e7c8f5...`, 347,388 B): regenerating it here reproduces that module's
.text byte-for-byte (`7403aaff764b620b35883e502135e1730fbae967ddace6c9ea8bda930a515af0`,
[evidence/chain.txt](evidence/chain.txt)).  The patch is therefore a drop-in
delta on the fired stack, not on a stale base.

## 2. Reproduce

    cd <repo>/opensource/docs/incoming/wifi-rx-format

    # 1) host model test (no kernel): 132 checks, 0 failures
    ./tools/run-host-tests.sh

    # 2) rebuild base tree + apply patch + build + verify
    ./tools/build-patched-module.sh
    # if /tmp was cleaned (regenerates the chain first):
    REBUILD_CHAIN=1 ./tools/build-patched-module.sh
    #    -> ${ART:-/tmp/rxfmt-artifact}/hi5622_wifi.stripped.ko
    #       + hi5622_wifi.ko.unstripped, build.log, params.txt

Overrides: `BASE_TREE`, `WORK`, `ART`, `KDIR_OVERRIDE`, `STAGING_OVERRIDE`.
The script refuses on any warning/error/undefined symbol, on a param count
other than 57, on a missing feature string/symbol.

Build environment found automatically:
`KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`,
`CROSS=.../staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-`.

## 3. Verification performed (all PASS)

| assertion | result |
| --- | --- |
| patch dry-run + apply on the base tree | clean, 0 conflict markers |
| build warnings / errors / undefined symbols | 0 |
| param count | 57 (unchanged; the patch adds no module parameter) |
| required symbols | `hif_rx_entry_encode`, `hif_rx_hdr_parse`, `hif_rx_buffer_witness`, `hif_rx_refill_budget`, `hi5622_dp_rx_probe_show`, `dptx_rx_publish_one` |
| feature strings | `dp_rx_probe`, `DEVICE-WROTE-FRAMES`, `MAC-WROTE-NOTHING`, `RX format self-check FAILED`, `rx_refill_wm` |
| strip | `--strip-debug` only; data/params intact (modinfo reads 57) |
| .text | `136965bbc9709d3ed0e0dd40138367bd21333a9f74554f30b7062a2c255132e8` (baseline `7403aaff...`, expected to differ - the patch adds code) |

Evidence: [evidence/build.txt](evidence/build.txt),
[evidence/module-identity.txt](evidence/module-identity.txt),
[evidence/patch-verify.txt](evidence/patch-verify.txt),
[evidence/applied-tree-build.log](evidence/applied-tree-build.log).

## 4. Artifact identity (the .ko this lane built)

| item | value |
| --- | --- |
| stripped | **356,424 B** sha256 `0eadba355ac914c3ecf09dbe50b31d023ff0c7dbd56c2ba7a977f2cc79d5aacd` |
| unstripped | 1,355,436 B sha256 `9fa4794c98310b40b579e38731af988ca06a847ce9adb842b309b329b7dc9d08` |
| params | 57; vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; depends `cfg80211` |
| local path | `/tmp/rxfmt-artifact/hi5622_wifi.stripped.ko` (gitignored; never committed) |
| build warnings | 0; .text `136965bb...` |

The sha is for that exact file: a Linux module's build-id (20-byte GNU note,
ld hashes the pre-strip DWARF `comp_dir`) depends on the build directory.
Rebuilds elsewhere are code/param identical but carry a different sha - quote
the sha of the exact baked file and compare rebuilds by .text + params.

## 5. What the patch changes

See [REPORT.md](REPORT.md) section 5.1: two new portable-format files, bind
self-check + the two fail-closed refill fixes + the read-only `dp_rx_probe`;
`dp_setup` fails closed if the ring/config words do not mirror the vendor
stock dump.  No module parameter, register constant or enable semantic is
changed; every added check only refuses.

## 6. Fire parameters

Unchanged from the final association + watermark module (the patch adds no
parameter).  Probe rungs: [FIRE-RECIPE.md](FIRE-RECIPE.md).
