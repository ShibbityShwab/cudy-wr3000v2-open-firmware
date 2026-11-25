# FIRE-RECIPE - CRX delivery-gate ladder (attended; the Lead fires)

**Status:** designed, host-verified, **NOT fired**. **Base:** FINAL IMAGE #2 chain
(association tree + mgmt carrier + rx-watermark) **+** this lane's
`patches/0001-crx-delivery-gate.patch` (4 files; adds the read-only `crx_gate`
attribute; no device writes). One module load per boot; no rmmod; power-cycle between
fires.

**Contract check on every rung:** handshake page `0x86807FEF/7/2/0xABCD`, exception mask
CA 0x400008 == 0, staging CA 0x40000C == 0, no `excp_announce`, no dmesg fault, and
every posted frame consumed inside the provider's bounded poll. Any deviation ->
ABORT, power-cycle, analyse offline.

---

## R0 - crash control (no RX writes at all)

The Lead's exception appeared after extended `dp_rx_drain` loops. R0 keeps the whole
proven AP sequence but disables every RX-side host write, so a clean 15+ minute run
bounds the crash cause (REPORT section 5.4):

```
insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<block> \
    allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 \
    vap_start_mode=1 vap_start_module=0 hcc_poll_ms=5 hcc_trace=1 stop_ap_mode=0 \
    rx_enable=0 rx_refill_wm=0 rx_clear_status=0 rd_advance=node rd_ab_write=0 \
    beacon_static=1 beacon_vap_start=1 wpa_keys=1 sta_join=0 band_vendor=0 \
    scan_band_cfg=0 mgmt_tx=1 mgmt_rx=1
```

Order: page/mask/staging receipt -> C2 (ctx-create/TXQ sync) -> AP 5-message order ->
AP-ENABLED -> **no `dp_rx_arm`, no `dp_rx_drain` ever** -> observe 15 min.

| outcome | decision |
| --- | --- |
| page/mask/staging clean for 15+ min, no exception | the crash is associated with RX pointer writes; keep the RX lane read-only until the MAC state is fixed |
| exception still appears | the crash is NOT an RX-write artefact; next suspects are the H2D payload pointers and the firmware's own TX/beacon path (report the first fault frame) |
| any contract deviation before 15 min | ABORT (existing stop rules) |

---

## R1 - delivery-gate read-out (still no client traffic)

Same load as R0. After AP-ENABLED:

```
cat /sys/class/net/<wifi-dev>/device/crx_gate
cat /sys/class/net/<wifi-dev>/device/rd_catalog
```

(`crx_gate` is the new read-only attribute; it performs no write and can be read any
number of times. `rd_catalog` is the direct D2H reader catalogue.)

Expected on the current bench state:

| field | expected | meaning |
| --- | --- | --- |
| verdict | `MAC-RX-IDLE-UPSTREAM` | firmware D2H alive, MAC RX idle, no CRX |
| MAC status | 0x00000182 (RX bit2 clear) | no MAC RX activity |
| norm/small rptr | 0x20 / 0x20 | prefetch-only |
| completion wptr | 0x0 | no frames written to host rings |
| mask | 0xfffffcc1 | vendor-exact RX mask |
| firmware hcc instance | 0x0010C0F4 | firmware D2H object live |
| DR trigger counters | > 0 | D2H machinery has run (ready pair/heartbeats) |
| CRX catalogue | 0 | **the CRX event never arrived** |

| outcome | decision |
| --- | --- |
| `MAC-RX-IDLE-UPSTREAM` | proceed to R2; delivery layer is proven healthy, the missing input is upstream |
| `FW-UNREADABLE` / `MAC-UNREADABLE` | ABORT: record the raw fields printed; the probe's assumptions are [V\*] for this boot (do not re-read in a loop) |
| `FW-D2H-IDLE` | unexpected (ready frames were seen); record raw fields, hand to the d2h lane |
| `INCONSISTENT` | stop; capture raw fields + `rd_catalog` + dmesg |

---

## R2 - input test (the laptop sends auth frames)

Same load. Have the client send authentication frames to the AP BSSID (the proven
stimulus from the assoc ladder). Read `crx_gate` and `rd_catalog` again.

| outcome | decision |
| --- | --- |
| verdict becomes `CRX-DELIVERED` (or `CRX-AND-DATA`) | **host delivery path proven end-to-end**; the CRX side of the association chain is alive; continue to R3 |
| verdict stays `MAC-RX-IDLE-UPSTREAM` | the MAC/DMAC RX never delivered the frame to the firmware: the bug is in the MAC RX hardware configuration, not in CRX gating. Record and hand to the wifi-rx-vendor-diff lane; do not reload in this boot |
| `FW-D2H-IDLE` appears only now | delivery channel wedged under the first traffic: capture `rd_status` before any further action |

**Note:** an id-13 CRX is expected for the auth frame itself; do not expect host-ring
completions (EAPOL/data) until association completes - that is R3 and needs a
successful 4-way.

---

## R3 - data (only after R2 shows CRX)

Associate the client (WPA2), then the proven RX consumer actions:

```
echo 1 > /sys/.../device/dp_rx_arm     # read-only validation of the live bank
echo 1 > /sys/.../device/dp_rx_drain   # bounded; only after a successful arm
cat /sys/.../device/dp_rx_capture
```

PASS: completion entries appear (EAPOL msg2/4, ARP, DHCP) and `crx_gate` reports
`CRX-AND-DATA`. STOP if a drain refuses/closes the lane (fail-closed by design).

---

## Fail-closed rules carried by this patch

* `crx_gate` refuses (-EBUSY) until `windows_programmed` + hcc armed; it reads only the
  six mapped inbound regions and treats any 0xffffffff read as unreadable, never as data.
* The probe reads no CA in 0x4016xxxx (not host-visible).
* The strict CRX decoder refuses any frame that is not the firmware's exact wire form.
* No device write, no allocation, no lock held across a read; reading the file cannot
  change the device state and therefore cannot be the crash trigger.
