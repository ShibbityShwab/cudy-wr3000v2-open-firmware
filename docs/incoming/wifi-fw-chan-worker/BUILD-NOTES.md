# BUILD-NOTES - lane wifi-fw-chan-worker (task-180)

HOST-ONLY build: the module is compiled against the project 6.18.60 kernel
tree; the artifact is **local evidence only** and is never committed.  This
lane's build is a *superset* of the bench-proven beacon-lane module: it
corrects the channel-activation wait predicate and adds read-only gate reads.

## Chain

1. **Base** = the wifi-5ghz-beacon lane's build tree
   (/tmp/beacon5g-build), built by
   ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh; base artifact
   §hi5622_wifi.ko§ 356,600 B sha256
   §bc1cc0ead2c5b98a35eea875c353dc7e5bcf2e39b61ba9427408b714ea9a1bbb§,
   59 params.  The build script pins the three base sources it patches
   (sha256 in BUILD-NOTES below); a drifted base tree is refused.
2. **This lane's patch** = §patches/0001-chan-gate-fix.patch§ applies to the
   staged tree and
   * adds §hi5622_chan_gate.c§ / §hi5622_chan_gate.h§ (decoded channel
     semantics: the two firmware channel tables, the exact ctx+0x10 store
     word, the corrected readiness predicate, the silent 5 GHz RF-programme
     gate) and the §hi5622_chan_gate.o§ Kbuild object;
   * rewires §hi5622_beacon5g_chan_ctx_ready()§ to the decoded predicate
     (the old body required the index byte +0x13 to be 1 - impossible for
     band 1 ch36, index 0);
   * extends the read-only gate pack in §hi5622_scan.c§ with the
     §module_type / module+0x440 / bandobj+0xEC8 / perlink+0x1C8§ reads and
     the §0x40684 -> 0x101890 RAN/SKIPPED§ classification, and reports the
     expected ctx word when the wait fails.

   No wire byte changes; no register write; the 2.4 GHz path is untouched
   (the wait is band-1 only).

## Pins and artifact (this build)

    base pins (sha256):
      Kbuild             6659213e5621d75bd929820442bc7454eca73ad99895423ac8f3cf1a1077f5e7
      hi5622_scan.c      6a0695a510897a4766587ac93efcebbdb1f7a5f6cf36770196af9593a9b28af4
      hi5622_beacon5g.c  9ab8d1a6a73a88feccc1a7785198dd22184279a13fa9bbf57c996267cf018670
      hi5622_beacon5g.h  e150e0250270198e4d010fb90b8770fa6e9ba6cf280cd6e37a00f11e0d0fee47
    patch sha256:         7f72d0edff50f0d3f580414c4422f1195ee4387d36e50b1fe7bb1de59720dccd
    artifact:             /tmp/chanworker-artifact/hi5622_wifi.ko
                          358,256 B sha256 9a5b048e7e21a4212c83d75fa9542afac9c9de89e93089c25694849cf7775fa6
                          59 params; ap_chan_wait_ms + vap5g present; hi5622_chan_gate_word_ready linked
    kernel tree:          openwrt-lab-upstream-target .../linux-6.18.60

The build log and the full host-test output are in §evidence/build-artifact.txt§,
§evidence/build.log§ and §evidence/host-tests.txt§.

## Reproduce

    # base tree (once; needs the beacon lane's chain, host-only):
    ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh
    # stage + patch + build + contract checks:
    ./tools/build-chan-worker.sh
    # host tests (firmware decode 89 checks + portable C model 38 checks):
    ./tools/run-host-tests.sh

The host C selftest uses the first working compiler of §cc/clang/gcc§ (this
workstation's §cc§ lacks cc1; clang is used automatically).  §provision-venv§
supplies the Python interpreter for the firmware decoder (standard library
only; the decoder does not need capstone).

## What was verified here (and what was not)

* VERIFIED (host): patch applies to the pinned base; §src/§ copies are
  byte-identical to the patched tree; the module builds with 0
  warnings/errors; debug-strip keeps §.text§ identical; 59 params; the
  decoded predicate is linked; the firmware decode selftest is 89/89 and the
  portable C model selftest 38/38.
* NOT verified here: any device behaviour.  This lane did not load, fire,
  flash or contact the router.  The module is *not* bench-validated; the
  benchmark rung remains §FIRE-RECIPE.md§ and the Lead's.
