# The 2026-11-27 AP milestone - honest documentation

**Headline (exact).** On **2026-11-27** the clean-room host stack, running against the
**PD4-patched research firmware** on the bench unit, brought up a cfg80211 AP interface:
hostapd reached **`wlan0: AP-ENABLED`** and `iw dev wlan0 info` reported **`type AP, ssid
cudy-ap-test`**. The four-message AP-start sequence was posted by the driver and **consumed by
the vendor firmware in 0 polls each**, with the golden readback pack intact (exception mask 0,
staging 0, no exceptions). This is the first live Wi-Fi AP on the project's clean-room stack.

**It is a bench research result, not a Wi-Fi driver and not an upstream or product feature.**
The authoritative record is the parent-repo ledger entry **2026-11-27 04:30**; the owning lane
is [../wifi-ap-start/](../wifi-ap-start/README.md) (commit `9b180f3`, module v7). This page
exists so that no submission, publication or close-out document over-states it.

## 1. What was actually running (the exact stack)

| layer | what |
| --- | --- |
| board | Cudy WR3000 v2.0 (luofu R116), pair B, bench unit |
| kernel | our 6.18.60 cfg80211 bench image; kernel slice `13e81214...` / slot `1794a531...` flashed 2026-11-25 via the stock-assisted path |
| wireless subsystem | `cfg80211.ko` 2,328,548 B sha256 `949393f9...` (target-tree build) loaded from `/tmp` |
| driver | clean-room provider module **ap-start v7**: 206,708 B sha256 `fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020`, built from the hardware-fired provider base `b42ff4ec...` + AP-start provider patch v3; loaded from `/tmp` |
| firmware | the **PD4-patched research firmware** (local evidence only; patched image sha256 `cfe24bfb...037b1`, recipe + verifier in [../wifi-fw-audit/](../wifi-fw-audit/README.md)); never committed, never shipped |
| hostapd | hostapd-mini v2.12 package (OpenWrt tree build), plus the bench workaround pushing the missing `/usr/share/hostap/*.uc` wifi-scripts files; config = 2.4 GHz ch 6, **OPEN** network, beacon_int 100, DTIM 2 (`hostapd-open.conf`) |
| regulatory | `iw reg set US` before the AP (the world/00 regdomain is NO_IR) |

The driver path that made it work (fixed in v2-v7 of the lane): fail-closed `.connect` so
hostapd's nl80211 capability gate passes; AP iftype change accepted; `.start_ap` posts the
vendor sequence (not the M3 stub); `mgmt_stypes` advertised so hostapd's frame subscriptions
are accepted; `WIPHY_FLAG_REPORTS_OBSS`; `.disconnect`/`.change_beacon`; and the **mode-1
VAP base posted before the AP frames** (v7 - this was the fix that closed it).

## 2. The four AP-start messages (all consumed, 0 polls)

| # | message | carrier | size | posted | firmware |
| --- | --- | --- | --- | --- | --- |
| 1 | `vap_add` (mode-1 VAP object) | g0 id1 | 48 B | 3/8 | consumed in 0 polls |
| 2 | `vap_start` | g0 id1 | 40 B | 4/8 | consumed in 0 polls |
| 3 | `ap_ssid` | g0 id1 | 64 B | 5/8 | consumed in 0 polls |
| 4 | `ap_cfg` (AP config) | g0 id1 | 184 B | 6/8 | consumed in 0 polls |

Driver log chain: `cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=63
tail=9)` -> the four `frame post ... consumed` lines -> `start_ap sequence posted to provider
vapscan-0e4ad45` -> hostapd `UNINITIALIZED->ENABLED` / **AP-ENABLED**.

Read-only readback after the sequence (BAR0/C A conventions from the owning lane):
exception mask CA `0x40008` = **0**; staging BAR0 `0x6F800C` = **0**; boot page **`0x86807FEF`**
golden with state 1; **VAP pointer ctx0+0x190 = `0x01010F18`** (nonzero - the firmware VAP
object); **ctx0+0x18C changed to `0x70094810`** (AP/HT mode bits set by the firmware AP-config
worker). Heartbeats continued; no exceptions.

Earlier fires in the same push (kept for the record): v6 reached `start_ap` and posted
`ap_ssid` but crashed the firmware on `ap_auth` because the mode-1 VAP base was missing; v5
proved the `mgmt_stypes` fix; v4/v3/v2 fixed `set_mode`, `.connect` advertisement,
`.start_ap` and the capability gates. The final v7 sequence is the one that must be cited.

## 3. What this proves ([V], bench only)

* the cfg80211 AP-mode control path works end to end from hostapd to the firmware: interface
  setup, mode/beacon validation, `start_ap`, and the four management-message sequence;
* the reverse-engineered mode-1 VAP create/start + AP-config messages are accepted and processed
  by the firmware (VAP pointer allocated, AP flags updated, zero exceptions);
* the FCC/HCC boot chain + calibration + cfg80211 registration + VAP lifecycle + AP frames is one
  continuous, reproducible stack on the clean-room host (the 2026-11-26 one-command matrix
  already runs the boot/C2/D1/D2/E1 rows; the AP row now rides on top).

## 4. What this does NOT prove ([U]/[X]) - do not claim it

* **No over-the-air verification.** No air capture, monitor capture or second device was used.
  Beacon transmission to a client is **unverified**; the evidence is hostapd/cfg80211/firmware
  state, not a sniffer or a client.
* **No client association, no WPA2.** The fixture is an **OPEN** network. WPA2-PSK/CCMP needs
  the `add_key`/key-install path (M3 stub) and has not run; there is no 4-way handshake evidence.
* **No data-plane TX completion / RX.** The earlier first-TX test published one frame to the MAC
  TX ring but stayed **POST-ONLY** (completion pending a started radio). The radio is now
  started, but the completion/RX test has not been run - the AP result does not move that claim.
* **No scan/BSS integration proof after AP start** (scan results were proven separately on the
  mode-1 provider).
* **No persistence or integration**: modules load from `/tmp`, hostapd is not in the flashed
  image (zero-flash package deploy + ucode workaround), no autostart, no netifd/wifi-scripts
  integration, no reboot test of the AP configuration.
* **No regulatory/DFS/txpower proof**; the AP config is a single 2.4 GHz channel.
* **Not upstreamable, not portable:** requires the PD4-patched firmware, the research provider
  module and host-only tooling; none of it is in the kernel series, target tree or image recipe.

## 5. Why it cannot be upstreamed (the exact blockers)

1. **The vendor firmware is closed and PD4 is required** for the H2D path on this board. PD4 is
   a research patch on a vendor blob (audit in [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md);
   H1 confirmed the stub is consume-only). Shipping or posting a patched vendor firmware is not
   an option; the GPLv2 source request is unanswered (sent 2026-10-01).
2. **No upstreamable Wi-Fi driver exists.** The clean-room stack is a research transport +
   cfg80211 provider, not an in-tree mac80211 driver; the vendor MAC/firmware handles the
   802.11 processing, and there is no mainline binding or driver for the Hi5622 in the series.
3. **No host-clean boot path is proven.** The only host-only candidate (spare inbound iATU
   viewport onto the event-controller block + one-shot event 0x4C raise) is a new device-write
   class, never attempted, needing its own attended experiment. Without it, the unpatched
   vendor blob cannot service host frames.
4. **The AP is not feature-complete** (open-only, no client/data-plane proof, no persistence),
   and the install/image pipeline is not closed (see [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md)).

Until the GPL source arrives **or** a host-clean boot path is proven, the Wi-Fi result stays a
published research/interoperability artefact (protocol report, frames, recipes, hashes) - never a
shipped driver, never a firmware patch in any repo or image, and never an upstream claim.

## 6. Reviewer/publication wording

**Use:** "The reverse-engineered host protocol drives the vendor firmware into an
AP-configured state on the bench (hostapd AP-ENABLED): the mode-1 VAP create/start and AP SSID/
config messages are accepted by the firmware with zero exceptions. This is a research result on
a patched research firmware and is not an upstream or product Wi-Fi driver."

**Do not use:** "Wi-Fi AP support", "working AP", "open Wi-Fi", "first open-source AP", "driver
ready for upstream", "OpenWrt Wi-Fi support", "association/data plane works", or any phrasing
that implies a client can use it.

## 7. Remaining work to a real open Wi-Fi path (for a future maintainer)

1. prove over-the-air beaconing (AP page/beacon D2H events or an air capture) - bench, attended;
2. close the WPA2 security path (`add_key`/key install, M3) - driver research;
3. radio-up data-plane completion/RX on the started VAP (reuse the dp-air merged module and the
   F-AIR ladder) - bench, attended;
4. decide the firmware problem: GPL source, host-clean boot path (event-controller aperture
   experiment), or keep Wi-Fi out of the supported feature set;
5. build a real mac80211/cfg80211 driver with DT binding and package integration only if 1-4
   become supportable;
6. image/install closure: hostapd + wifi-scripts package, kernel+rootfs flash, persistence.

## 8. Evidence

| claim | evidence |
| --- | --- |
| AP-ENABLED, SSID `cudy-ap-test`, 4 messages consumed 0 polls, readback pack | parent-repo ledger **2026-11-27 04:30** |
| module identity v7 `fd8e1439...`, base `b42ff4ec...`, fix history | [../wifi-ap-start/README.md](../wifi-ap-start/README.md), [../wifi-ap-start/HOSTAPD-BRINGUP.md](../wifi-ap-start/HOSTAPD-BRINGUP.md), commit `9b180f3` |
| provider module/vapscan chain, cfg80211 scan/AP wiring | [../wifi-scan-ap/README.md](../wifi-scan-ap/README.md), [../wifi-vap-start/README.md](../wifi-vap-start/README.md) |
| PD4 identity/audit/H1 (consume-only) | [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md); parent-repo ledger 2026-11-25 21:30 |
| hostapd package/deploy gates, open-network fixture | [../wifi-hostapd-deploy/README.md](../wifi-hostapd-deploy/README.md) |
| image/kernel identity + install paths | [TARGET-IMAGE-INSTALL.md](TARGET-IMAGE-INSTALL.md) |
| prior TX POST-ONLY result | parent-repo ledger 2026-11-27 02:30; [../wifi-dp-air/LADDER-F-AIR.md](../wifi-dp-air/LADDER-F-AIR.md) |
| one-command matrix all-PASS rows | parent-repo ledger 2026-11-26 17:00, 15:00; [../wifi-bench-tooling/](../wifi-bench-tooling/) |
