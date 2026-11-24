# wifi-beacon-static — bench ladder (DESIGN ONLY, NOT FIRED)

**Status: design artifact.**  This lane touched no device.  The Lead fires the
ladder attended, with the recovery door armed and UART capture running (project
rules: BENCH-FACTS.md / RUNBOOK.md / fire gate).  Goal: prove whether the
vendor-order host sequence puts the AP beacons on air, and if not, isolate the
missing host step.

## 0. Preconditions (all mandatory, unchanged project rules)

* Fresh **golden boot** of the final combined image + module (page
  `0x86807FEF / 7 / 2 / 0xABCD`, state 1, exception mask 0, staging frame 0).
* Recovery door READY (physical trigger + TFTP recovery.bin), bench link PASS.
* UART capture running to an absolute path; no other writer.
* Module loaded with the standard bench parameters for the rung
  (`allow_frames` budget large enough: AP path uses 4 frames in B2,
  `h2d_send_budget=10`, `frame_gap_ms=100`, `vap_start_first=1`,
  `vap_start_mode=1`, `vap_start_module=0`, `ap_start_mode=0`).
* **External observer required for the on-air gate:** a phone/laptop Wi-Fi scan
  (sees the SSID) or, better, a second monitor-mode radio capturing beacons.
  Our own RX path capture is 0 and the D2H g3 id19/20 events are
  *received*-beacon reports, so they are not proof of our AP beaconing.

## 1. Readback pack (read-only; existing `hi5622_fw_ram_read` recipe)

| name | CPU address | PASS |
| --- | ---: | --- |
| golden page | 0x170E28 (BAR0 0x829E28) | 0x86807FEF, state 1 |
| exception mask | CA 0x40008 | 0 |
| module-table slot 0 | 0x175458 | nonzero after user_add |
| module-table base offset | 0x17a504 | 0 |
| module ctx CA | slot0 + base | — |
| **module channel ctx** | module ctx CA + 0x10 (u32) | channel byte == hostapd channel; band byte == 0; valid byte (bits 24..31) != 0; new dmesg line `beacon_static channel ctx readback` |
| firmware VAP object | module ctx CA + 0x190 | nonzero after the AP start (existing guard logs it) |

## 2. Rungs

### B0 — baseline (control): legacy path, no beacon_static
Load the final combined module **without** `beacon_static` (default 0), start
hostapd, confirm the bench-proven behaviour (AP-ENABLED, TX COMPLETED selftest,
page golden, mask 0).  This rung is the A/B control: same module, new code
compiled in but not executed.

* PASS: identical dmesg/readback to the pinned final validation (no new
  `beacon_static` lines, no exception).

### B1 — channel config only (rung isolation)
`insmod ... beacon_static=1 beacon_vap_start=0`
Then `hostapd` start on the configured channel (e.g. ch6).

* Expect dmesg: `start_ap beacon_static=1 plan (4 steps): vap_add ap_chan ap_ssid ap_cfg`,
  `ap_chan posted (32 B, band=0 channel=6)`,
  `channel ctx readback: module_ctx+0x10 u32=0x...`, then the normal AP sequence,
  AP-ENABLED.
* PASS (host): channel byte readback == 6, band byte == 0, valid byte != 0;
  page golden; mask 0; H2D lane open; no exception.
* On-air: **expected still NO** beacons (VAP start is the legacy pre-config
  one).  This rung proves the channel message is consumed and honoured.

### B2 — full vendor order (the decisive rung)
`insmod ... beacon_static=1 beacon_vap_start=1`
Then hostapd start on the configured channel.

* Expect dmesg: 5-step plan `vap_add ap_chan ap_ssid ap_cfg ap_vap_start`;
  `ap_chan posted`; `ap_vap_start posted after ap_cfg`; AP-ENABLED.
* PASS (host): B1 readbacks + `ap_vap_start` posted without refusal; page
  golden; mask 0; heartbeats.
* **PASS (on air, decisive):** external observer sees the SSID on the
  configured channel and, with a monitor radio, captures beacons at the
  configured interval (~100 TU), BSSID = the AP VAP MAC, with the SSID/rates/
  DS IEs.  If the observer sees no beacon while B2's host readbacks are clean,
  the missing step is *downstream of the VAP start* (firmware-internal
  state/channel/PHY), and the next lane should read the SMAC VAP state and the
  channel worker's PHY program instead of adding more host messages.

### B3 — WPA2 beacon (optional, still no stations)
Same as B2 with the WPA2 hostapd fixture.  On-air PASS: observer sees the
privacy capability bit and the RSN IE in the beacon.  Station association is
still blocked on the M3 `add_key` path (wifi-wpa-static lane), not by this
ladder.

## 3. Stop rules (identical to the project rules)

* exception mask non-zero, golden page/state lost, staging frame non-zero;
* any posted frame not consumed inside the bounded poll, or the H2D lane
  closes;
* a beacon-path VAP object still zero after the bounded wait / a builder
  refusal / a post refusal (the driver latches the sequence DIRTY);
* any dmesg fault.

On a stop: record mask/out[]/dmesg/readback, power-cycle, analyse offline.
Never re-send in the same boot.  Never write CA 0x400392F0.  Never post mailbox
id 5.  Never send group 1/4.  Never rmmod after release.

## 4. Rollback

`beacon_static` defaults to 0 in the compiled module; a rung that misbehaves is
abandoned by reboot + load without the parameter (module reload after release
is refused by design; the reboot is the reset).  The legacy AP/TX/scan paths
are untouched code-wise when the parameter is 0.
