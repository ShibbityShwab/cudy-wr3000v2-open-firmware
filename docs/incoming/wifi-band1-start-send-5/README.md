# wifi-band1-start-send-5 - the R5 order: the AP config before the 0x0065 VAP start and the 0x0242 band start

**Lane:** wifi-band1-start-send-5, shared task **task-213**, 2026-11-26.
**HOST-ONLY**: no device contact, no fire.  **The Lead fires.**

## Why

The 2026-11-26 fire armed band 1: the corrected gates (G9'/G11' == 1), the R4b
order, the 0x0065 VAP start (G12 pass) and the 0x0242 band start (state[1]=2,
state[4]=0x00) all succeeded, the RF image was programmed (R+0x3A8/0x3AC =
0x01800060/0x03E6138A) and the AP was ENABLED - but the 5200 MHz observer scan
saw **no beacon**.  The committed beacon-TX decode explains the residual: the
0x0127 AP config only **stages** the beacon interval/DTIM/template in the VAP,
and the 0x0065 VAP start is what **creates** the per-link beacon state (and
then calls the band start).  The R4b ladder posted the AP config **after** the
0x0065/0x0242, so the beacon state was created and the band armed with no
beacon template to transmit.  The stock order is channel config -> AP config ->
start_vap -> green_ap; R5 restores it.

## What changed

    R4b: vap_add -> desc_table -> dfs_mode -> ap_chan -> vap_start_g0 -> band_start -> ap_ssid -> ap_cfg -> ap_vap_start
    R5 : vap_add -> desc_table -> dfs_mode -> ap_chan -> ap_ssid -> ap_cfg -> vap_start_g0 -> band_start -> ap_vap_start

Order only: every gate (G8 after the ap_chan, G9'/G11' == 1, G12 after the
0x0065) and every frame byte is unchanged; 65 module parameters (no new knob).
The start_ap post sites are split into an AP-config phase before the 0x0065 and
a carrier-owned trailing VAP start after the 0x0242.

## Deliverables

| path | what |
| --- | --- |
| [patches/0001-r5-order.patch](patches/0001-r5-order.patch) | the R5 order on the gate-inverted base (the deployed 9bf1d846 tree): plan model, enum, start_ap post sites, contract texts |
| [patches/0002-r5-selftest-update.patch](patches/0002-r5-selftest-update.patch) | the send-4 C selftest copy updated for the R5 plan/enum order (apply after the gates 0002 test patch) |
| [tools/build-r5-order.sh](tools/build-r5-order.sh) | full chain: R4b tree -> gates 0001 -> R5 patch -> module build -> artifact contract -> host tests |
| [tools/r5_order_frame_test.py](tools/r5_order_frame_test.py) | independent Python re-derivation of the 40-B 0x0065 frame + R5 source order/contract test |
| [tools/run-host-tests.sh](tools/run-host-tests.sh) | host-test rerun against an already-built /tmp/r5-build |
| [REPORT.md](REPORT.md) | the fire evidence, the root cause, the patch, the gates, the residual risks |
| [TABLES.md](TABLES.md) | order tables, plan steps, post sites, beacon-data flow, probe pins, artifact pins |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact chain, pins, contract checks, reproduce, apply-on-other-base |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the Lead's fire: same load + ch40 AP + verification probes + verdict/abort rules |
| evidence/ | build + test + base-chain transcripts (host-only, no device) |

## Status

Host-built module (local evidence only): 383,240 B stripped, sha256
**24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c**, 65
params, 0 build warnings.  Host tests: **405 checks, 0 failures** (295 C +
110 Python).  **Nothing fired, no device contacted.**

## One-line reproduce

    opensource/docs/incoming/wifi-band1-start-send-5/tools/build-r5-order.sh

## Related lanes

../wifi-band1-start-send-4 (the R4b order and the fire lineage),
../wifi-vap-start-gates (the deployed gate-inverted module 9bf1d846 and the
G9'/G11' polarity), ../wifi-fw-beacon-tx (the beacon TX chain: 0x0127 stages
the VAP fields, 0x0065 creates the per-link beacon state),
../wifi-fw-tbtt-arm (the per-band beacon-state readback),
../wifi-band1-start-send-3 and ../wifi-band1-start-send (the 0x0065/0x0242
frames and gates).
