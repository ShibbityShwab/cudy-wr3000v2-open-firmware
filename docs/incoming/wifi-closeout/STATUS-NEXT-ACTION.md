# STATUS NEXT-ACTION REFRESH - for the Lead to paste into the parent repo

**Lane:** wifi-closeout (task-131). The parent repo's `STATUS.md` / `mem-entries.md` are the
Lead's write scope; this file delivers the exact content to paste. Written against the
**2026-11-27 12:30** ledger state (flash + AP on the flashed image + combined AP/DP fire +
self-contained final image built host-only).

## 1. What is stale in `STATUS.md` today

- `## 4. Next action` still opens with **"Current phase (2026-11-23): ... B3 cold-boot
  ready-gate test DONE and NEGATIVE - next experiment is the vendor post-release kick of the
  firmware init table"**. That experiment was answered (the kick family was found and the PD4
  route became the working one) and the Wi-Fi line moved far past it through 2026-11-27. As
  written, the section tells a reader to resume a 2026-11-23 experiment that is no longer the
  frontier.
- `## 3k` / `## 3l` stop at 2026-11-26 05:30 (matrix complete, real VAP message). They do not
  mention: D2 TRUE PASS + one-command all-PASS matrix (11-26 15:00/17:00), the real scan
  results/BSS decode (11-26 14:00/16:30), hostapd integration, the first AP + flash (11-27
  04:30/06:30), AP persistence and the new firmware event family (09:00), the combined AP+DP
  fire (10:30), or the self-contained final image (12:30).
- Nothing in `STATUS.md` names the final honest limits for a reader: PD4 research firmware, no
  upstreamable Wi-Fi driver, GPL request unanswered, no on-air beacon/association proof, MAC
  TX completion open.

## 2. Proposed new section (insert after `## 3l`, keep 3b..3l as history)

```markdown
## 3m. Wi-Fi close-out: AP on the flashed image, combined AP+DP module, self-contained final image (2026-11-27)

The Wi-Fi line reached its documented ceiling. Hardware-validated, in order:

- **one-command bring-up matrix ALL PASS** - boot, C2 (six calibration, 54/54 checks), D1
  (cfg80211 VAP add/del, 21/21), D2 (scan with real firmware results), E1 (MAC data-plane
  setup + TXQ address sync, 37/37); `phase_matrix.py --phase all --yes-really --attended
  --reboot-between` (ledger 11-26 17:00; local evidence
  `build/wifi-bench-phase-matrix/20261124T094716Z/`).
- **real firmware scan results decoded into cfg80211 BSS** - mode-1 VAP base -> vap_add/
  vap_start (g0 id1 48/40 B) + scan_req (g0 id0xC 520 B) consumed in 0 polls; g1 id17
  channel results + g1 id15 beacon frames; first successful scan 6 channels/6 beacons
  (11-26 14:00), first full BSS decode 12 entries, 4/13 channels at the 14.7 s host window
  (11-26 16:30; the 60 s manual window package is `wifi-bench-session`).
- **first live Wi-Fi AP on our clean-room stack** - hostapd `wlan0: AP-ENABLED`, SSID
  `cudy-ap-test`; the four AP-start frames vap_add 48 B -> vap_start 40 B -> ap_ssid 64 B ->
  ap_cfg 184 B consumed in 0 polls; VAP pointer ctx0+0x190 = 0x01010F18; page 0x86807FEF
  golden, mask 0 (ledger 11-27 04:30; `wifi-ap-start/HOSTAPD-BRINGUP.md` sec 12).
- **AP persistence + new firmware event family** - the AP stayed live for minutes (hostapd
  processes alive, same SSID/type, no exceptions) and the D2H catalogue gained a boot-absent
  family: g1 id0x23 (35) count=6, 24 B, plus g1 id3 count=20 (466 B). Likely AP/beacon-period
  firmware activity [I] - indirect evidence, not an on-air proof (ledger 11-27 09:00).
- **first MAC data-plane frame published to the TX ring** - POST-ONLY (completion needs a
  started radio; ledger 11-27 02:30).
- **ABI image flashed and AP re-run on it** - container df6a78c3 (kernel slice 13e81214 /
  slot 1794a531 + rootfs UBI 62344e80) via the stock-assisted install; `apk query kernel` =
  6.18.60~6f995278...-r1 (kmod ABI closed); native `modprobe cfg80211` + `hi5622_wifi` from
  `/lib/modules/6.18.60`; hostapd AP-ENABLED + `cudy-ap-test` again on the flashed image
  (ledger 11-27 06:30).
- **combined AP+DP module fired** (242,572 B sha 44c6cc12, ap-start v7 + dp-txrx in one load):
  one boot runs C2 clean -> hostapd AP-ENABLED -> `dp_probe`/`dp_setup`/key-0x0320 TXQ sync
  OK -> `dp_tx_frame` published (wptr 0->1, rptr 0, completion_pending=1) = **still
  POST-ONLY**; `dp_rx_poll` 0 captures; boot/mask clean. Conclusion: the host publishes
  correctly but the MAC consumer does not advance rptr - the vendor TX-engine start/config
  (MAC/PIE/EQM port setup or firmware TX-queue start) is **[U]**, not a module-combination
  problem (ledger 11-27 10:30).
- **self-contained final image built host-only** (not flashed): rootfs-only rebuild, kernel
  slice unchanged (13e81214 / slot 1794a531), container 6,922,559 B sha ae5e28bf, rootfs UBI
  3,538,944 B sha ff9f3e1c, manifest 83 packages - native iw 6.17, hostapd-mini v2.12 +
  hostapd-common, ucode/wifi-scripts, wireless-regdb, cfg80211.ko and the **combined
  dp-air+AP-start v7 driver** (242,572 B sha 44c6cc12, installed stripped 231,132 B sha
  51608fd5). A fresh flash would need **no /tmp deployment**; T0 full-set is no longer needed
  (ledger 11-27 12:30; opensource 78e30e8).

Honest ceiling: the Wi-Fi path needs the **PD4-patched research firmware** (vendor mailbox
dispatch is not host-programmable; the patch is consume-only and never ships); there is **no
upstreamable Wi-Fi driver** and the vendor GPL source request is unanswered; the AP is
host-side AP-ENABLED - no on-air beacon/association/WPA2 proof; the MAC TX frame is POST-ONLY
and TX completion/RX is open. Platform/upstream track remains separate and is the real
submission line.
```

## 3. Proposed replacement for `## 4. Next action`

```markdown
## 4. Next action

**Current phase (2026-11-27): the Wi-Fi research line is at its documented ceiling - AP up,
no upstream driver; the next work is the self-contained image flash/validation, external
observation, and the publication track, not new protocol reversing.**

State: ABI-correct image df6a78c3 flashed on pair B with the AP running; combined AP+DP module
fired (AP-ENABLED + POST-ONLY TX); self-contained final image ae5e28bf built host-only (combined
driver + hostapd, kernel unchanged) and awaiting the attended flash; one-command matrix all PASS; real scan
results decoded into cfg80211 BSS; first air-TX frame POST-ONLY. Details: STATUS.md sec 3m,
ledger 2026-11-27.

Next, in order:

1. **Flash + validate the self-contained final image** (attended, one flash only): rootfs-only
   decision per `opensource/docs/incoming/wifi-image-deploy/DEPLOY.md` (the stock-telnet tool
   writes identical kernel bytes; strict rootfs-only needs `--rootfs-only` or the manual
   sequence). Accept the overlay wipe; rollback = old rootfs UBI 62344e80 or stock pair A.
   Validate with NO /tmp deployment: `modprobe cfg80211` + `modprobe hi5622_wifi` from the
   rootfs -> native iw -> hostapd -B -> AP-ENABLED + `cudy-ap-test`.
2. **AP breadth with an external observer** (needs a second client device): beacon on air,
   association, WPA2 (if enabled), DHCP. This is the difference between "host-side AP" and
   "working AP"; the post-AP firmware event family is only indirect evidence.
3. **MAC TX completion / RX**: the host publish is proven, the device consumer is not. Likely
   needs vendor TX-engine start/config (MAC/PIE/EQM port setup or a firmware TX-queue start,
   both [U]); then re-fire the combined-module ladder and check rptr advance / completion
   magic instead of POST-ONLY.
4. **Publication track** (the real endpoint): generate the v2 review form (30 + DTS-0001; v1
   is stale), land the publication fixes in
   `opensource/docs/incoming/wifi-upstream-final/PUBLICATION-CHECKS.md` (history purge, broken
   links, COPYING/CONTRIBUTING/DCO, Wi-Fi/LAN-switch statement), then the curated repos. The
   Wi-Fi driver stays research-only until the vendor GPL source or a host-clean firmware boot
   path exists.
5. **GPL request follow-up**: the 2026-10-01 request is unanswered; sending the tracked
   follow-up remains the only route to a legally clean Wi-Fi driver.

Do **not** re-flash the df6a78c3 image: it is already on pair B and the receipt matches; plan
one flash for the self-contained final image. Keep one slot stock and the recovery door
physical.
```

## 4. Suggested ledger entry (append with the Lead's timestamp)

```markdown
## 2026-11-27 HH:MM - PROJECT CLOSE-OUT DOCS DELIVERED (wifi-closeout, task-131)

Host-only close-out lane delivered under `opensource/docs/incoming/wifi-closeout/` (opensource
commit <sha>): **LANE-INDEX.md** (every Wi-Fi + platform lane -> task/commit/deliverable/
evidence/status, incl. the upstream-final line and the 06:30-12:30 milestones), **EVIDENCE-
REFRESH.md** (verifies the landed v4 block EV-001..EV-277 gap-free, then the residual rows
**EV-278..EV-281**: combined AP+DP fire, self-contained final image, closeout deliverables,
host-verifier suite; plus the root `EVIDENCE-INDEX.md` rows and the v3/v4 corrections),
**OVERVIEW-README.md** (proposed replacement for `opensource/README.md`: what this is, what
works, how to reproduce host-only and on the bench, honest limits, links),
**TEST-MATRIX.md** (52-entry host-only selftest/verifier sweep with exact counts and the
explained non-PASS rows), **PUBLICATION-NOTES.md** (publish surface still NOT clean: history
purge + 9 fails; cites task-130's PUBLICATION-CHECKS.md), **STATUS-NEXT-ACTION.md** (the sec 3m
+ sec 4 text above). Host-side re-run: `cd opensource/docs/incoming/wifi-closeout &&
./tools/run-host-suite.sh` (results in the lane's `build-evidence/host-suite/`); lane
verifier `./tools/verify-closeout.py`. No device contact, no vendor bytes, no root
STATUS/ledger edits by the lane.
```

## 5. Companion notes

- The root `EVIDENCE-INDEX.md` rows to append (claim/artifact/status/regenerate) are in
  [EVIDENCE-REFRESH.md](EVIDENCE-REFRESH.md) sec 2; the residual report-index rows
  (EV-278..EV-281) are sec 1 (report index owner: re-report4; v4 already landed at EV-277).
- The proposed README replacement is [OVERVIEW-README.md](OVERVIEW-README.md); it links only
  to files that exist in this repository (verified by the lane's link checker).
- Do not delete sections 3b-3l: they are history, and the ledger is append-only.
- Two action items live in [TEST-MATRIX.md](TEST-MATRIX.md): re-pin the bench-session artifact
  hashes before firing, and regenerate the cfg80211 provider-symbols baseline against the
  current tree.
