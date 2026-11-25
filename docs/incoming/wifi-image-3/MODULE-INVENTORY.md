# MODULE-INVENTORY - the driver-module candidates and the IMAGE #3 decision

**Lane:** wifi-image-3 (task-196). The artifacts below are LOCAL evidence (in
/tmp and the lanes' trees when this lane ran); no .ko is committed. Identity
hashes are for the exact local files; a rebuild in another directory can change
the 20-byte GNU build-id (the project's documented build-id caveat).

## 1. The lineage (same association tree, three branches)

    FINAL ASSOCIATION module (56 params, 344,372 B, ab9e18a4...)
      = all-patch tree + wpa3-sae + ap-mgmt + sta-ops + data-tx
        + the CORRECTED mgmt-TX carrier (HCC g0 id6; wifi-sibling-gpl patch)
      |
      +-- image #2 branch (RX watermark, T4)
      |     + wifi-rx-watermark/patches/0001 -> 347,388 B d2c608e7..., 57 params
      |       <<< BAKED IN IMAGE #2 AND IN THE wifi-image-update BASE
      |
      +-- 5 GHz branch (vap5g -> beacon5g -> r1-band -> desc_table -> dfs_mode)
      |     vapmsg5g   353,652 B 13e7bf4c...  58
      |     beacon5g   356,600 B bc1cc0ea...  59
      |     r1-band    356,612 B e8b17f72...  59  (the vap_start band self-clobber fix)
      |     desc_table 360,448 B aac6177c...  60  (H2D 0x00F7)
      |     dfs_mode   364,192 B f5408567...  61  (H2D 0x01F7)
      |     F2         364,184 B 12427b86...  61  (+ the field-B 0x00 fix)
      |       <<< F2 FIRE 2026-11-27 23:30: THE 5 GHz RF IS PROGRAMMED
      |
      +-- HE branch (AX-1)
      |     he_caps    356,564 B 475975d3...  59  (58-param vap5g tree + he_caps)
      |       <<< HE AP LIVE (HE capabilities/Operation IEs on air)
      |
      +-- BA branch
            ba_skeleton 359,992 B 78460841...  60  (fail-closed; no peer yet)

## 2. Why no single artifact is "the most complete"

| artifact | 5 GHz chain | RX watermark | HE caps | BA | bench proof |
| --- | --- | --- | --- | --- | --- |
| image #2 module (d2c608e7) | - | YES | - | - | association/RX accepted; no 5 GHz |
| F2 (12427b86) | YES (F2) | - | - | - | **5 GHz RF programmed**; no AX advertisement, no T4 |
| he_caps (475975d3) | - | - | YES | - | HE AP on air; no 5 GHz chain |
| ba_skeleton (78460841) | - | - | - | YES | fail-closed only |
| **IMAGE #3 merged** (a717c86d installed) | **YES** | **YES** | **YES** | **YES** | new combination; each parent proven; fail-closed defaults |

The F2 branch was built from the 56-param association tree (the pre-watermark
base), so it lacks the image #2 RX watermark; the HE and BA branches are
siblings off the 58-param 5 GHz-ready tree, so they lack the desc-table/
dfs-mode/field-B fixes. Choosing any one would regress a bench-proven feature.

## 3. The decision

IMAGE #3 carries the merged module (the F2 tree + the T4 RX-watermark patch +
the HE patch + the BA patch):

    activity  379,092 B  sha256 545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99
    installed 358,544 B  sha256 a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed
    64 params = 61 (F2) + rx_refill_wm + he_caps + ba_enable

Evidence: REPORT.md section 3; build chain:
build-evidence/module-build/source-chain.txt; preservation:
build-evidence/module-build/symbol-preservation.txt; host tests:
build-evidence/module-build/host-tests-*.txt; delta vs the F2 source:
build-evidence/module-build/merged-source-delta.txt.

## 4. Fail-closed surface (why the image is safe with the merged module)

Every feature the merge adds is OFF at load: `he_caps=0`, `ba_enable=0`,
`band5g=0`, `vap5g=0`, `desc_table=0`, `dfs_mode=0`; `rx_refill_wm=75` is
inert unless `rx_enable=1` AND the attended `dp_rx_arm` write. The image
shipped uci/modprobe defaults are 0 for all gates and the loader refuses
`desc_table/dfs_mode/vap5g` unless `band5g=1`. So with no operator action the
merged module behaves like the image #2 module (plus the inert additions).

## 5. Regeneration

The merged module is rebuilt by the recipe in
build-evidence/module-build/source-chain.txt from the committed lane patches
(wifi-rx-watermark, wifi-ba-agg, wifi-ax-he-caps) on the F2 source tree. The
image bake pins only the resulting activity/installed artifacts; if the module
is regenerated with a different build-id, update the pins in
tools/bake-image-3.sh, tools/verify-image-3.sh and HASHES.txt together.
