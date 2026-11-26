# app-ie-image3 - the H2D g0 id 0x20 app/vendor IE injection REBASED onto the IMAGE #3 merged module

**Lane:** `app-ie-rebase` (shared task **task-235**), 2026-11-26. **HOST-ONLY:**
no bench contact, no device write, no flash. Deliverables live under
`opensource/docs/incoming/app-ie-image3/`; the built `.ko` is local evidence
only (`/tmp/app-ie-image3/hi5622_wifi.ko`) and is never committed.

**The gap this closes.** The wifi-app-ie lane (task-234, commits `99665ae` /
`32462c0`) decoded and integrated the vendor's H2D **g0 id 0x20
`hmac_config_h2d_send_app_ie`** frame (builder CA 0x600cc, firmware handler
CA 0x85631), but built and tested it on the **R5-order base tree**
(`/tmp/r5-build`, 65 params, the band-start/VAP-start chain). The BENCH image
#3 runs the **merged module** whose source tree is `/tmp/wifi-image-3-module`
(F2 5 GHz chain + rx-watermark + BA + HE, installed form **a717c86d**, 64
params). This lane rebases the same integration onto that tree, rebuilds,
re-runs the KAT and writes the fire recipe for the IMAGE #3 lineage.

**Result in one paragraph.** The rebase was **not mechanical** (the IMAGE #3
lineage has no `vap_start_g0` / `0x0242 band-start` steps, so the app-IE
step is placed after the single `ap_vap_start` step, the `vap_start_g0=1`
gate becomes the composite "a VAP start is posted" condition, and the VAP
index/read-back wait/plan signature are re-mapped - delta table in
`REPORT.md`). The portable app-IE model is **byte-identical** to task-234, so
the KAT is the same suite: **55/55 host checks**. The rebased module builds
with **0 warnings / 0 errors**, has **66 params** (64 IMAGE #3 + `app_ie` +
`app_ie_type`), installed form **364,516 B sha256 `2a2fff35...`**, `.text`
**`c1ad1379...`**; the KAT (`tools/kat-image3.sh`) passes **17/17**. The
IMAGE #3 base tree itself was proven by a control build that reproduces the
shipped module exactly (installed **a717c86d**, `.text` `494ccd42...`, 64
params - `evidence/BASE-CONTROL-BUILD.txt`).

## Deliverables

| file | content |
| --- | --- |
| `REPORT.md` | the rebase: base identity proof, the non-mechanical delta table, invariant grades and limits |
| `BUILD-NOTES.md` | source/artifact pins, build command, control build, KAT |
| `FIRE-RECIPE.md` | the updated attended fire: IMAGE #3-lineage module + `app_ie=1 app_ie_type=4` + hostapd `vendor_elements=dd0a00112233617070696521` on the 2.4 GHz AP + laptop scan |
| `patches/0001-app-ie-image3.patch` | the rebase patch against the IMAGE #3 module tree (`patch -p1`) |
| `src/hi5622_app_ie.{c,h}` | the portable model, byte-identical to task-234 |
| `tools/build-app-ie-image3.sh` | reproducible host build + artifact pins + host KAT |
| `tools/kat-image3.sh` | read-only KAT on the delivered module |
| `tools/run-host-tests.sh` | re-runs the task-234 host suite |
| `evidence/` | base-tree manifest + control build, build summary, KAT, host tests, patch round-trip |

## Pins (see BUILD-NOTES.md)

    IMAGE #3 base   /tmp/wifi-image-3-module  (a717c86d source; 97-file manifest)
    rebased module  /tmp/app-ie-image3/hi5622_wifi.ko
                    364,516 B  sha256 2a2fff3508bdbffad4a23d04c31c17ef91f7ce46ae190fdae090119bcb6263e2
                    .text 163,052 B sha256 c1ad137974b6866bbc7cdc1971288ade203bf9dfcca6421b73343ef3d50b9417
                    66 params (64 base + app_ie + app_ie_type), vermagic 6.18.60
    patch           sha256 09523f988b7a0256565278017a4e82687d7dd6363abbccc5722de595550adb7d

**Read order:** REPORT.md -> BUILD-NOTES.md -> FIRE-RECIPE.md.
