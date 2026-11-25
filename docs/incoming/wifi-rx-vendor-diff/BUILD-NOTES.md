# BUILD-NOTES - wifi-rx-vendor-diff module

**Host-only.**  The .ko is local evidence only and is NEVER committed.

## Chain

```
FINAL association tree 251d08f0
  + ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
  + ../wifi-rx-watermark/patches/0001-rx-refill-watermark.patch
  + patches/0001-rx-vendor-diff.patch          <- this lane (6 files, 1052 lines)
  = RX VENDOR-DIFF module (59 params)
```

The first two steps reproduce the artifact d2c608e7... (347 388 B, 57 params)
recorded by the rx-watermark lane; the staged tree
/tmp/wifi-rx-watermark-build/rebuild is the base this lane patched.  If that
tree is gone:

```
cd ../wifi-rx-watermark && REBUILD_ASSOC=1 ./tools/build-final-module.sh
# (regenerates assoc + carrier + watermark via the committed chain scripts)
```

## Reproduce

```
# 1) host tests (no kernel): 440 checks, 0 failures
./tools/run-host-tests.sh

# 2) rebuild the module on the staged base tree
ASSOC_TREE=/tmp/wifi-rx-watermark-build/rebuild ./tools/build-rx-diff-module.sh
#    -> /tmp/rx-vendor-diff-artifact/hi5622_wifi.stripped.ko (+ unstripped, logs)
```

The build script stages a private copy (never writes the shared chain tree),
applies this lane's patch with "patch -p1 --dry-run" first, builds against the
project 6.18.60 KDIR, refuses on any warning/error/undefined symbol and checks
the artifact identity (59 params, feature strings).

## Artifact identity (host-built, NOT fired, NOT in any image)

| item | value |
| --- | --- |
| stripped (bake/fire this) | **361 872 B sha256 81ab72c157dd6f1005a4a9012eb8210161a30fde55bc1c2064bde21a7c48031c** |
| unstripped | 1 367 620 B sha256 068c61a92fbb717abced119b77087127dba6a763ba872997967b31f198ba78da |
| .text | 152 176 B; byte-identical stripped/unstripped |
| params | **59** = base 57 + rx_drain_write + rx_status_service |
| new feature strings | "DP rx_parity: read-only vendor-state diff", "DP rx_observe: OBSERVE-ONLY" |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| build warnings/errors | 0 |

Build-path note: Linux module shas change with the build directory through the
20-byte GNU build-id.  The same sources rebuilt by tools/build-rx-diff-module.sh
at /tmp/rx-vendor-diff-build give 361 872 B sha256
9a0d7ddf6be02da412d526d1707e72bef2fcfa2b057b2f73d47763cc6039a26a with an
equal 152 176 B .text (verified: patch round-trip, 6/6 source files identical,
.text identical across three build paths; evidence/identity.txt,
evidence/patch-verify.txt).  Quote the sha of the exact file baked.

## Verification performed here

| assertion | result |
| --- | --- |
| host test 440 checks | PASS (evidence/host-test.txt) |
| patch applies to the base tree (dry-run + real) | OK |
| patched tree reproduces the built source byte-for-byte | 6/6 files |
| kernel build 6.18.60 | 0 warnings / 0 errors (evidence/build.log) |
| strip keeps .text identical | yes |
| fresh-tree rebuild .text identical | yes (152 176 B) |
| params | 59, rx_* present |

## Scope / limits

- No bench contact, no image bake, no flash, nothing fired.
- The patch is additive to the inherited fail-closed RX path: the write-enabled
  drain is one parameter away; the two new write-capable actions are refused by
  default.
- If R2 in FIRE-RECIPE proves capture, the old write path (rx_drain_write=1)
  becomes the consumption rung; if it does not, the five device-owned live-bank
  words are the primary lead.

