# wifi-wpa3-sae - WPA3-SAE / WPA2+WPA3 / WPA2-hardened hostapd profiles + SAE host-side bits

Lane for shared task **task-144**, host-only (2026-11-28).  Base: the pinned
FINAL all-patch module (opensource `008aca5`).  Nothing here was fired; no
device contact happened in this lane.

## What is here

| path | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | full report: what was built, the exact functional state, static RE, limits |
| [TABLES.md](TABLES.md) | derived constants/contracts/firmware decode/state tables |
| [LADDER.md](LADDER.md), [ladders/wpa3-ap.json](ladders/wpa3-ap.json) | attended bench ladder (design only) |
| `configs/` | hostapd WPA3-SAE, WPA2/WPA3 transition and WPA2-hardened fixtures |
| `patches/0001-wpa3-sae.patch` | kernel patch on the pinned final all-patch chain (9 files) |
| `src/` | the patched kernel sources, mirrored for review |
| `tools/` | hostapd validator build + config gate, portable selftest, host check, module build |
| `evidence/` | config gate, module/patch identity, firmware decode summary, selftest |

## Fast checks (host-only, no device)

    ./tools/run-host-check.sh --evidence evidence/host-check.txt   # 23 checks + 44 selftest checks + config gate
    ./tools/build-wpa3-sae-module.sh                               # base-chain identity + patch + 6.18.60 build

## One-line state

The three profiles are config-valid, and the patch closes the PMF AP-start
gate (GTK + IGTK + default management key) so a WPA3-SAE AP can reach the same
key-setup milestone the WPA2 GTK rung proved - but a *usable* WPA3 AP (SAE
authentication, association, 4-way PTK, data) still needs the AP management
TX/RX path (wifi-ap-mgmt lane), `.add_station`/`.del_station` and the MAC RX
data path.  See REPORT.md section 0 and TABLES T8/T9.

## Policy

No vendor binary/firmware/calibration byte, no credential and no secret is in
this tree: only derived addresses/offsets/masks/constants and original code.
The built `.ko` and vendor artifacts stay local and gitignored.
