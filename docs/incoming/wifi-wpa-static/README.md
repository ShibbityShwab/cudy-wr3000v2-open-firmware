# wifi-wpa-static - WPA2 key path static RE + cfg80211 M3 ops

**Lane:** wifi-wpa-static (shared task task-139), **host-only**, 2026-11-28.
Cudy WR3000 v2.0 clean-room Hi5622V100 Wi-Fi RE project.

## Status in one paragraph

The WPA2 key path was the project's M3 blocker: hostapd reached AP-ENABLED but
cfg80211 had no `.add_key/.set_default_key` ops, so key installation failed
with -EOPNOTSUPP.  This lane reversed the vendor key path from the two shipped
vendor host modules (2.4.15 + 2.5.24, identical key-op code) and the pinned
firmware image, and delivered: the exact firmware messages (g0 id-1 config
event cmds **0x0078 / 0x0079 / 0x007A**, workers **CA 0x0A5825 / 0x0A5C9D /
0x0A4271**), the full **0x44-byte key body layout**, the **cipher-class
mapping and index rules**, the hostapd WPA2 startup sequence, a cfg80211
`.add_key/.del_key/.set_default_key` implementation with the three portable
message builders behind a **wpa_keys=0 fail-closed** parameter, host
selftests, a compile gate that reproduces the pinned fired final-combined
identity before patching, and a design-only attended bench ladder (R0-R4).
Nothing was fired; no vendor binary/firmware/calibration byte is committed.
The remaining WPA2 gate is the RX path (association/EAPOL/4-way handshake),
which this lane does not touch.

## Deliverables

| path | what it is |
| --- | --- |
| [REPORT.md](REPORT.md) | full lane report: method, vendor sequence, firmware workers, implementation, verification, limits |
| [TABLES.md](TABLES.md) | derived addresses/offsets/layouts/cipher classes/hostapd sequence |
| [LADDER.md](LADDER.md) | attended bench ladder design (R0-R4), PASS/abort criteria |
| [ladders/wpa2-ap.json](ladders/wpa2-ap.json) | machine-readable ladder |
| [patches/0001-wpa-static.patch](patches/0001-wpa-static.patch) | source delta on the final combined hardened tree |
| [src/](src/) | the new portable builder + kernel layer files (review copy) |
| [tools/build-wpa-static-module.sh](tools/build-wpa-static-module.sh) | compile gate: chain identity assert + patched build + artifact checks |
| [tools/key_selftest.c](tools/key_selftest.c) | host selftest for cipher/body/frame/bounds |
| [tools/run-host-check.sh](tools/run-host-check.sh) | portable host gate (selftest + patch/doc static checks) |
| [evidence/](evidence/) | committed text evidence: build identity + host check output |
| [refs/artifact-identity.txt](refs/artifact-identity.txt) | pinned artifact/chain identities |

## Reproduce (host only)

    cd opensource/docs/incoming/wifi-wpa-static
    ./tools/run-host-check.sh           # fast: portable selftest + static checks
    ./tools/build-wpa-static-module.sh  # heavy: chain identity + patched build

The .ko is local evidence only and is never committed.

## What works / what does not

* **Works (static + host):** key frame decode, builders, cfg80211 op wiring,
  firmware-bound validation, fail-closed gate, compile-verified module.
* **Hardware-unknown until fired:** the firmware accepting our g0 id1 key
  frames (rung R2).  The carrier family itself is bench-proven for AP start /
  TXQ sync.
* **Not implemented on purpose:** `get_key` reply parsing (not needed by
  WPA2-PSK), `set_default_mgmt_key` (PMF), the 4-way handshake (needs RX),
  STA `.connect` (join payload still [U]).
