# appie-wext2 - the vendor WEXT `set_vendor_ie` call sequence (the app-IE slot storage)

**Lane:** appie-wext2, shared task **task-239**, 2026-11-26. **HOST-ONLY**: no bench
contact, no device, no firmware write, no flash. Parent lanes: **wifi-app-ie**
(task-234) and **app-ie-image3** (task-235) - this lane starts where their class
sweep stopped.

## Why this lane

The app-IE H2D frame is proven end to end at the HCC layer: the byte-exact
g0 id 0x20 frame is accepted and consumed, but **the firmware per-VAP class slot
stays empty** for every vendor class tried (4, 3, 9, 1 - the class sweep of
2026-11-27), and the laptop never sees the 0xDD IE. The AP-start posting path is
therefore not the vendor's slot-storage mechanism.

The vendor's userspace sets app IEs through the Hi5622 WEXT private ioctl
`nlwifi_set_app_ie`; this lane decodes that whole call sequence from the local
vendor driver disassembly (`hi5622v100_wifi.ko`, gitignored local evidence) and
replicates it: the WEXT request ABI, the WAL cfg-event RPC envelope, the hmac
chain, and the host-mirror + H2D call order.

**Headline finding:** the vendor WEXT path does **not** post the bare g0 id 0x20
frame. It packs the same `app_ie_req` into a **WAL cfg-event RPC (event 0x0276
SET_VENDOR_IE) sent as a group-2 / id-0 HCC frame** and waits for a response; the
cfg-event dispatcher then runs
`hmac_config_set_vendor_ie -> hmac_config_set_app_ie_to_vap -> mac_vap_save_app_ie
(host mirror) -> hmac_config_h2d_send_app_ie (g0 id 0x20)` - and only on a
**running AP netdev**. The AP-start posting skipped the RPC envelope, the host
mirror and the running-AP context.

## Contents

| file | content |
| --- | --- |
| `REPORT.md` | the decode, the delta to the AP-start posting, the ranked root-cause hypotheses and the plan |
| `TABLES.md` | W1-W8 decode tables (CAs, struct layouts, frame geometry, call order) |
| `src/hi5622_app_ie_wext.{h,c}` | portable clean-room model: WEXT request parser, 0x7f strip, host mirror, cfg-event 0x276 builder, fail-closed gate |
| `src/app_ie_wext_selftest.c` | host KAT (byte-exact builder vector, geometry, ABI, mirror, refusals) |
| `tools/run-host-tests.sh` | host build+run of the model (no kernel, no device) |
| `patches/0002-app-ie-wext2.patch` | kernel integration on top of `../app-ie-image3/patches/0001-app-ie-image3.patch`: new params `app_ie_mode` (direct / WEXT envelope) and `app_ie_when` (pre-start / post-enable delayed), host-mirror call order, cfg-event builder |
| `FIRE-RECIPE.md` | the staged fire: W1 delayed running-AP post, W2 hdr[3] sweep, W3 WEXT envelope |
| `BUILD-NOTES.md` | build identity of the patched module (or the honest failure) |
| `evidence/` | host KAT + build evidence |

Frame/exception constants are **decoded protocol layouts** quoted as hex text; no
vendor binary, object, firmware byte, calibration value, MAC or secret is
reproduced or committed.
