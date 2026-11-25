# BUILD-NOTES - wifi-5ghz-bringup module chain (host-only)

## Toolchain (same as the sibling lanes)

    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

The scripts auto-detect the same trees under openwrt-lab-selectorfix-img as a
fallback.

## Chain (one command)

    ./tools/build-5ghz-ready.sh

1. FINAL ASSOCIATION tree: opensource/docs/incoming/wifi-stability/tools/build-association-module.sh
   = all-patch 5b587949 (base 44c6cc12 -> polished d145a205 -> dp-tuning
   175a4617 -> scan R4 -> stability -> rx/beacon/wpa/sta static) + wpa3-sae +
   ap-mgmt + sta-ops + data-tx. 56 params.
2. opensource/docs/incoming/wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
   - the corrected device mgmt-TX carrier (HCC g0 id6 control message), i.e.
   the exact bench module under test (ab9e18a4...). The script accepts
   ASSOC_TREE=/tmp/wifi-assoc-build/integ to reuse a built tree.
3. patches/0001-band5g-assoc.patch - the wifi-5ghz lane's band5g profile,
   rebased onto the association tree: new hi5622_band5g.c/.h (vendor band
   tables, freq/channel/index, AP channel gate, H1 channel derivation),
   band5g module parameter, 5 GHz wiphy profile branch, scan-request index fix
   (vendor table position), Kbuild object.
4. patches/0002-5ghz-primary-wiphy.patch - this lane's fix: the one-radio/
   two-band wiphy plan on the primary endpoint, no wiphy on the secondary host
   view, band-aware AP-start/scan band bytes.

Patch order matters: 0001 then 0002. Both apply with patch -p1 to the
association tree + sibling carrier patch; the build refuses conflict markers.

## Artifact (stripped, local only - never committed)

    size      351,008 B
    sha256    1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53
    unstripped 1,342,188 B
    .text sha  b21bbc328e7479410fe4952220972a7b14f49f67a0d6cfc513cd48909c0906b6
    params    57 (56 association + band5g)
    vermagic  6.18.60 SMP mod_unload ARMv7 p2v8
    depends   cfg80211

Two independent builds were byte-identical (no build-id drift with this
toolchain/config); the script re-checks: 0 warnings/errors, debug-strip keeps
.text identical, the 57 params and the feature strings, and the 12-line
cfg80211 provider symvers. The build also refuses a drift between the
host-tested src/hi5622_band5g.* and the patched driver sources.

## Host tests

    ./tools/run-host-tests.sh

* portable selftest: compiles src/band5g_selftest.c + src/hi5622_band5g.c with
  gcc -std=c99 -Wall -Wextra -Werror; 114 checks, 0 failures (98 inherited from
  the wifi-5ghz lane + 16 new wiphy-plan / freq-to-band checks).
* patch static checks: 20 checks over the two patches (integration points,
  Kbuild object, fail-closed gates, no conflict markers, no binary artifacts
  in the lane, ladder passphrase stays a placeholder).

The .ko stays under /tmp/wifi-5ghz-ready-artifact/; the lane commits only text
and the portable sources (project publication rule: never a vendor binary,
firmware, calibration or .ko in tracked text).

## Files in this lane

| path | contents |
| --- | --- |
| REPORT.md | the analysis: gate-scope root cause, the refuted second-ladder hypothesis, the fix |
| TABLES.md | evidence tables (endpoints, wiphy plan, gate, band bytes, artifact, unknowns) |
| FIRE-RECIPE.md | the attended G0-G7 ladder (NOT FIRED) |
| README.md | entry point + file map |
| patches/0001-band5g-assoc.patch | band5g profile rebased on the association tree |
| patches/0002-5ghz-primary-wiphy.patch | one-radio/two-band wiphy fix |
| src/hi5622_band5g.c/.h | portable band/plan helpers (identical to the driver) |
| src/band5g_selftest.c | 114-check host selftest |
| tools/build-5ghz-ready.sh | chain build + artifact contract checks |
| tools/run-host-tests.sh | selftest + patch static checks |
| ladders/ | hostapd 5 GHz open/WPA2 configs + the on-router fire script |
| evidence/ | captured host test/build output (text only) |
