# BUILD-NOTES - how the vap5g module is produced and why the chain is pinned

HOST-ONLY: everything below runs on the workstation; nothing contacts the
router. The .ko is local evidence, never committed.

## Chain

    assoc tree (wifi-stability build-association-module.sh)
      + sibling mgmt-TX carrier patch
      + wifi-5ghz-bringup patches/0001-band5g-assoc.patch
      + wifi-5ghz-bringup patches/0002-5ghz-primary-wiphy.patch
    = 5 GHz ready module: 57 params, stripped 351,008 B,
      sha256 1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53
      (the module under bench test; built by build-5ghz-ready.sh)
      + this lane's patches/0001-vap5g-order.patch
    = vap5g module: 58 params, stripped 353,652 B,
      sha256 13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf

`tools/build-vapmsg5g-ready.sh` rebuilds the base when it is absent (calls
`../wifi-5ghz-bringup/tools/build-5ghz-ready.sh`), and otherwise pins the
existing base artifact by sha256 and refuses to continue if the base chain
drifted. It then stages a clean copy of the base tree, applies the patch,
checks the portable helper copy is byte-identical to the patched tree, builds
against linux-6.18.60 (armv7 musl toolchain), refuses any warning/error or
undefined symbol, verifies the debug-strip keeps .text identical, and asserts
the artifact contract:

* 58 modinfo params including `vap5g` (default 0),
* the four vendor-order feature strings,
* 12 cfg80211-provider symvers lines,
* vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`.

## Host tests

    ./tools/run-host-tests.sh

* part 1 (always): `src/vapmsg5g_selftest.c` - the planner decision matrix:
  legacy mode, the one-radio/two-band AP shape, single-band/non-AP/
  unregistered wiphy regression, out-of-range mode refusal, NULL/short
  buffer handling, and a mode-1-vs-mode-0 non-regression sweep over
  (bands 0..3) x (iftype 0..8): 450 checks;
* part 2 (when a patched driver tree exists): `tools/frame_bytes_selftest.c`
  compiled against the driver's own portable builders - g0 id7 = 180 B with
  header[3] = mac chip 0 and uid/MAC/type/RSSI at the vendor offsets, g0 id8
  = 180 B id 8 and the vendor capability-block offsets, VAP add = g0 id1
  key 0x0064 48 B with body[0x12] = band 1 and body[0x13] = chip 0:
  103 checks.

## Rebuilding from a clean /tmp

    rm -rf /tmp/wifi-5ghz-ready-build /tmp/wifi-5ghz-ready-artifact \
           /tmp/wifi-vapmsg5g-build /tmp/wifi-vapmsg5g-artifact
    ./tools/build-vapmsg5g-ready.sh      # rebuilds the base, then the patch
    ./tools/run-host-tests.sh

The script needs the project 6.18.60 kernel tree and OpenWrt staging
toolchain (`/home/shibbityshwab/openwrt-lab-upstream-target/...`; the
selectorfix image tree is the fallback, same as the base lane).
