# wifi-ap-mgmt - WAL management-frame TX family + D2H management-frame RX decode

**Lane:** wifi-ap-mgmt (shared task task-143), **host-only**, 2026-11-24.
**Status:** static reversal complete (vendor host modules 2.4.15 + 2.5.24 and
the PD4 firmware table A), fail-closed patch host-built and compile-verified
on the final all-patch tree (stability commit `008aca5`), host tests pass.
**NOT FIRED by this lane** - the attended ladder is in
[LADDER-MGMT.md](LADDER-MGMT.md); the Lead fires it.

This lane closes the last decoded gap between "AP is enabled / GTK installed"
and "a client can finish auth/assoc": the vendor host's cfg80211
`.mgmt_tx` path (WAL cfg-event cmd **0x01F2**, 12-byte descriptor) and the
device->host management-frame event (HCC **group 1 id 13**, 60-byte prefix,
48-byte RX info, 802.11 frame).  Both are wired into the clean-room driver
behind compiled-default-**off** module params `mgmt_tx=0` / `mgmt_rx=0`.

## Files

| path | purpose |
| --- | --- |
| [REPORT.md](REPORT.md) | full reversal report: vendor path decode, RX layout, design, verification, limits |
| [TABLES.md](TABLES.md) | decoded tables: op family, wire envelopes, descriptor, RX info, firmware table A, patch inventory |
| [LADDER-MGMT.md](LADDER-MGMT.md) | attended bench ladder (design only, not fired) |
| [ladders/ap-mgmt.json](ladders/ap-mgmt.json) | machine-readable ladder |
| [src/hi5622_mgmt_logic.c](src/hi5622_mgmt_logic.c) `.h` | portable TX/RX wire logic (C99, host-tested, device-free) |
| [src/hi5622_mgmt.c](src/hi5622_mgmt.c) `.h` | kernel glue: cfg80211 op, DMA frame buffer, D2H delivery |
| [patches/0001-mgmt-tx-rx.patch](patches/0001-mgmt-tx-rx.patch) | 14-file additive patch on the all-patch tree |
| [tools/mgmt_selftest.c](tools/mgmt_selftest.c) | 60-check host selftest (KATs + refusals) |
| [tools/mgmt_re.py](tools/mgmt_re.py) | 69 static-RE probes against the local vendor modules + firmware |
| [tools/build-host.sh](tools/build-host.sh) | strict C99 selftest + static fail-closed gates |
| [tools/build-module.sh](tools/build-module.sh) | stages the all-patch tree, applies the patch, builds, reports identity |
| [evidence/](evidence/) | captured host-test / RE-probe / module-build output |

## The two wire facts

    TX (host -> device): one WAL cfg-event, HCC group2 header id 0,
      envelope {cookie, id=1, data_len, seq} + TLV {cmd 0x01F2, len 12,
      body {u32 channel, u8 cookie_idx, u8 pad, u16 frame_len,
            u32 frame_devaddr}}
    RX (device -> host): HCC group1 header id 13,
      frame data at payload+48 after the 12-byte HCC header,
      RX info: +7 channel, +17 u16 frame_len, +39 s8 RSSI;
      length = total - 60, frequency from the vendor channel formula

The TX descriptor references the frame by **device-visible host-DRAM
address**; the vendor passes the cfg80211 buffer pointer because its kernel is
VMSPLIT 2G/2G (pointer in the device window 0x80000000..0x8FFFFFFF).  This
kernel is VMSPLIT 3G, so the driver copies the frame into its own
`dma_alloc_coherent()` buffer and passes that DMA address (window-checked).

## Reproduce (host only)

    cd opensource/docs/incoming/wifi-ap-mgmt
    ./tools/build-host.sh                      # selftest + fail-closed gates
    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/mgmt_re.py         --firmware /tmp/hi5622-patched/FIRMWARE.PD4.bin         --wifi-ko /tmp/vmods/hi5622v100_wifi.ko         --wifi-ko-new /tmp/bootkick/hi5622v100_wifi.ko
    ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-module.sh

## Bench gates (Lead only, see LADDER-MGMT.md)

1. **M0** control: mgmt_tx=0/mgmt_rx=0 - byte-identical host path, new params
   visible, `.mgmt_tx` still -EOPNOTSUPP.
2. **M1** `mgmt_rx=1` only: D2H group1 id13 decode + `cfg80211_rx_mgmt` line
   when a client sends a management frame; counts are the PASS evidence.
3. **M2** `mgmt_tx=1 mgmt_rx=1`: hostapd auth/assoc exchange; PASS =
   "frame post 'mgmt_tx' ... consumed" plus an **external observer** seeing
   the auth/assoc response on air (our own RX path is not proof).
4. **M3** WPA2 (cross-lane): association still needs station ops + EAPOL data
   delivery - documented as the next blocker, not this lane.

Stop rules and recovery invariants: LADDER-MGMT.md.

## Policy

Host-only.  No vendor firmware/object byte, calibration value, per-unit
identity or secret is committed; only derived addresses/offsets/ids/field maps
and original C.  The vendor artifacts stay local/gitignored
(`docs/incoming/wifi-shim-port/raw/` is gitignored).  The .ko is local
evidence only.  This lane never contacted the bench router.
