# wifi-bss-color - the vendor g0 id36 hmac_scan_config_bss_color (the 8-byte BSS-color bitmap)

**Lane:** wifi-bss-color (shared task-233). HOST-ONLY: no bench contact. Deliverables
committed under `opensource/docs/incoming/wifi-bss-color/` only; no vendor binary,
firmware image or object is committed here (addresses and derived structure only).

## What this lane is

The first stock H2D capture (task-232, commit e977557) decoded a vendor family our
clean-room driver never sends: **group 0 id 36 (0x24) = hmac_scan_config_bss_color**,
builder at vendor host-module .text **0x99C10**, a 20-byte frame whose 8-byte payload is a
**64-bit BSS-color bitmap**. This lane decodes the builder and its caller and its firmware
handler completely, then implements the frame in our 6.18 stack, host-tests it byte-exactly,
builds it on the image #3 module base, and writes the fire recipe.

## Headline decode

* The frame is a **direct group-0 id 0x24** message (NOT a `g0 id1` config key, NO subkey):
  12-byte HCC header + 8-byte payload, total 20 B, `hcc_msg_alloc(8)`. [V]
* The payload is a **little-endian u64 bitmap; bit i = BSS color i (0..63)**. The builder
  ORs `1 << entry[+0x85]` over a list of scan-result entries, then `memcpy_s(8)` and
  `hcc_msg_tx()`. [V]
* The caller is **hmac_scan_proc_scan_comp_event @0x99EAC**, call site 0x9A00C: it is the
  **scan-completion** path (scan object status `[+0xFAC]==2`). [V]
* The colour field provenance is proven: **hmac_scan_proc_scan_result_handle @0x9D870**
  parses the scanned BSS's HE Operation IE with `mac_ie_parse_bss_he_oper` and stores
  `(param_byte & 0x3F)` into `entry[+0x85]` - i.e. the 6-bit BSS color of that scanned
  BSS. So the message is the vendor's **"colors observed in the scan" report**, not an
  AP-color setter. [V]
* Firmware handler `g0[0x24] -> file 0x755E1 (CA 0xB55E1, Thumb)` forwards the two mask
  words to the per-chip callback registered at firmware RAM global+0x3EC, with a per-chip
  one-shot dedupe flag; it never dereferences the payload. Safe to post. [V]

## Integration (ours)

Because the true trigger is scan completion, this lane implements the frame as a portable
clean-room builder and wires it to the only per-AP `he_bss_color` value that cfg80211 hands
a cfg80211-only driver: `NL80211_ATTR_HE_BSS_COLOR` -> `settings->beacon.he_bss_color`
(hostapd `he_bss_color=25`). The posting point is `hi5622_scan_op_start_ap` after the AP
config, behind a fail-closed module knob `bss_color=0|1` (default 0, no frame, no device
write). See REPORT.md sec. 4 for why this is a *differential probe*, not a claim about the
on-air colour (the beacon's HE Operation IE is firmware-built).

## Contents

| path | content |
| --- | --- |
| REPORT.md | full decode (builder, caller, field provenance, firmware handler), integration, limits |
| TABLES.md | T1 frame layout, T2 field definitions, T3 KAT vectors, T4 decode addresses, T5 cfg80211 path |
| BUILD-NOTES.md | the image #3 module base pin, the patch, build + host-test evidence |
| FIRE-RECIPE.md | the attended rung (module load + 2.4 GHz HE AP + hostapd he_bss_color=25 + scan) |
| patches/0001-bss-color.patch | the fail-closed patch against the image #3 module tree |
| src/ | portable builder + policy + host selftest |
| tools/ | host-test runner, patch/build script |
| evidence/ | host transcripts, base pin, artifact hashes |

## Status

DECODE COMPLETE. Implementation/build/fire-recipe: see the section below (updated by the
commits of this lane).
