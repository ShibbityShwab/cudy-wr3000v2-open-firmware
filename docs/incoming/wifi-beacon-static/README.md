# wifi-beacon-static — beacon / AP data path static reversal

**Lane:** wifi-beacon-static (shared task task-140), Cudy WR3000 v2.0 clean-room
Hi5622 Wi-Fi RE project.  **Host-only lane:** every deliverable was produced and
verified on the host; the attended bench ladder is designed and audited here and
is marked NOT FIRED.  No bench-device contact, no image, no vendor bytes or
secrets were committed.

## Status in one paragraph

The vendor AP start is a **five-message ordered sequence**, not four: module
channel config (WAL cfg cmd 0x0124) → set SSID (0x0005) → auth (0x006D) → AP
config (0x0127) → VAP start (0x0065).  The host **never** sends a beacon
template or a bcn ring: the firmware SMAC owns beacon generation and TBTT TX
(TBTT ISRs + beacon-TX suspend/resume handlers in the firmware image; no fixed
beacon template; the host start_ap path carries only a 156-byte parameter body
with the RSN/WMM/HT/VHT/MD fields).  The clean-room bench path differs on the
host side in two ways: it never configures the module channel, and it posts the
mode-1 VAP base (user_add + VAP start) **before** the AP config instead of the
vendor's VAP start **after** it — so the firmware VAP/channel context is not
configured when the AP state machine starts.  This is the static candidate for
"hostapd AP-ENABLED but no on-air proof".  The lane delivers a fail-closed
patch (`beacon_static=1`, default 0 = bench-proven path unchanged) that
executes the vendor order, a pure ordering plan, the 0x0124 channel-frame
builder, host tests, the 6.18.60 compile-verify identities, and a bench ladder
with an external-observer on-air gate (our own RX capture is 0 and g3 id19/20
are *received*-beacon reports, not proof of our AP).

**Bench fire (Lead, 2026-11-24):** the ordered plan executed (`vap_add`
posted) and then the lane's own VAP-object readback guard **false-negatived**
(`module_ctx+0x190 == 0` after 100 ms — the stability lane's successful AP
start shows the same zero readback).  Fixed in the current build: a fresh base
post logs the readback and continues; only a genuinely stale pre-existing latch
still fails closed.  Current identity: stripped 276,140 B sha256
`300cd392c669…`; the pre-fix fired artifact was `e633f847cff8…`.

## Deliverables

| path | what it is |
| --- | --- |
| [REPORT.md](REPORT.md) | full reversal: vendor sequence, firmware workers, beacon-engine evidence, missing host steps, patch, limits |
| [TABLES.md](TABLES.md) | addresses/ids/field maps T1–T9 with evidence markers |
| [LADDER-BEACON.md](LADDER-BEACON.md) | bench ladder B0–B3 (channel-only control, full vendor order, WPA2), readbacks, external-observer on-air gate, stop rules |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact build/verification commands and artifact identities |
| [patches/0001-beacon-static.patch](patches/) | the source delta on top of the wifi-stability final combined tree |
| [src/](src/) | patched portable sources + `beacon_selftest.c` (65 host checks) |
| [tools/build-host.sh](tools/build-host.sh) | strict C99 host build + selftest + fail-closed default static checks |
| [tools/build-module.sh](tools/build-module.sh) | 6.18.60 module compile-verify (zero warnings, strip .text identity) |
| [tools/beacon_re.py](tools/beacon_re.py) | 33 static-RE probes (firmware + 2.5.24/2.4.15 host modules) |
| [evidence/](evidence/) | committed host evidence: selftest, RE probes, module build identity |

## Reproduce (no device)

    cd opensource/docs/incoming/wifi-beacon-static
    ./tools/build-host.sh                     # 65/65 host checks + static gates
    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/beacon_re.py --wifi-ko /tmp/bootkick/hi5622v100_wifi.ko   # 2.5.24: 33/33
    $PY tools/beacon_re.py --wifi-ko /tmp/vmods/hi5622v100_wifi.ko      # 2.4.15: 33/33
    ./tools/build-module.sh                   # heavy: 6.18.60 tree, host-built .ko

The vendor artifacts are local evidence referenced by path only; the `.ko` is
never committed.

## Rules honoured

Host-only (no 192.168.1.1 / 192.168.10.1 / UART / flash); no vendor binary,
object byte, firmware byte, calibration value or secret in tracked text —
only derived addresses, ids, field offsets and original C; the bench ladder is
design-only and the Lead fires it attended.
A separate finding for the AP data path: the vendor host implements the AP-side
auth/assoc management exchange in host software and drives it through
`wal_cfg80211_mgmt_tx`; the clean-room driver's `.mgmt_tx` is still a
fail-closed stub and MAC RX has 0 captures, so a client's assoc/auth is a
separate blocker (WAL/mgmt-tx message family + RX lane) even with beacons on
air.  See REPORT.md §3.4 and TABLES.md T10.
