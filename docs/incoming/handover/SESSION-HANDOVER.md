# SESSION-HANDOVER - Cudy WR3000 v2.0 (snapshot 2026-11-28 04:30 +07)

**This is a mirror/snapshot of the superproject-root `SESSION-HANDOVER.md` (task-203, lane
docs-refresh), committed in the published half for the lane register. All paths in
backticks are superproject-root-relative. The canonical, live pages are the superproject
`STATUS.md` (sections 3n/4) and `BENCH-FACTS.md`; the newest ledger entry wins.

---

**What this is.** A one-page, dated snapshot for the next session or agent: the verified state,
the open items, the exact next commands and the lane inventory. It is **not** the live page:
`STATUS.md` wins (sections 3n/4 are the current state and the next action), the
physical invariants are in `BENCH-FACTS.md`, the procedure is
`RUNBOOK.md`, the rules that cost fires are `LESSONS.md`, and the
newest dated entry of `mem-entries.md` wins over any older claim. Refresh this file instead of
appending to it.

## 1. State (verified; newest ledger entry 2026-11-28 04:30)

- **Bench box:** pair B = our image #1 (self-contained `ae5e28bf`, flashed + validated
  2026-11-27; per-fire modules are loaded by hand and the PD4 research firmware is pushed by
  hand, never packaged); pair A = stock 2.5.24. Page golden when healthy; a power cycle is
  needed after a firmware exception. Recovery is physical (RESET-hold + TFTP); the TFTP door is
  armed only while a man is at the bench. The internet is the USB-C tether; the box link is
  `enp103s0u1u3` (192.168.1.88 + 192.168.10.44).
- **2.4 GHz + 802.11ax are LIVE on air** on our clean-room 6.18 Wi-Fi stack: the HE AP
  `cudy-ap-test-he` beacons with the HE Capabilities + HE Operation IEs (BSS Color 25).
- **5 GHz: 13 links proven** (wiphy/regdomain, order, channel worker, sync wait, VAP object,
  gates, vap_start band fix, 0xF7 descriptor table, P/O/R chain, 0x01F7 field-B dispatch, **the
  RF programmed** - F2 mode 3 + the exact ch36-64 image R+0x3A8=0x01800060/R+0x3AC=0x03E6138A,
  and the beacon state present). **The 5 GHz beacon is NOT on air.**
- **The last 5 GHz gap = the band-1 MAC start (CA 0x4AF0C).** The stock arms it by direct calls,
  not through the NULL G+0x370 hook; the missing host message is **g0-id1 key 0x0242
  "set_green_ap"** posted on a fresh band-1-bound module context. Earlier R3 probe was
  mis-addressed - the correct phys for CA 0x4010C220 is **0x404C4220**.
- **RX (live-client blocker) is open:** the MAC receives but host frames=0, so no client
  associates. The stock H2D trace succeeded: **586 vendor messages / 18 families / 210.1 s**,
  our driver 108 matched / 0 drift; the unmatched families (the ASCII HEARTBEAT g4 id4 x83
  included) are the RX candidates.
- **Decoded, fail-closed:** BA A-MPDU (`ba_enable=0`, bench-safe), rate control (`rate_mode`
  auto = a 39-B g0 id2 frame; corrected-C2 retry in flight), AX advanced (TWT is the one
  host-testable path), 5 GHz 160 MHz/DFS (decode in flight).
- **Images:** #1 `ae5e28bf` flashed+validated; #2 `8ba1ad22` host-verified (84/84), not
  flashed; **#3 `4cd85b5d`, 7,843,840 B** host-verified (129/129, 3x reproducible, kernel
  unchanged, rootfs-only install), **not flashed**.

## 2. Open items (ranked)

1. **band1-start-send (task-206):** implement the key 0x0242 send, then the Lead fires the
   R3b-extension ladder. PASS = CA 0x4010C220 gains 0x1029 and a 5200 MHz scan sees the
   beacon; page golden / mask 0.
2. **fw-f1-capture (task-198):** decode the unmatched captured H2D families into the missing
   RX-delivery/config message; then the RX fire (client auth -> host frames>0, page golden).
3. **Image #3 flash (Lead, attended; runbook = image-flash-prep task-201):** rootfs-only write
   of the image's root UBI; the kernel slot is untouched.
4. **Host-only in flight:** rc-impl (task-200), ax-twt-impl (task-199), dfs-160 (task-205),
   upstream-v2-folds (task-202).
5. **Not possible today:** on-air client association (blocked by 2), a 5 GHz AP in service
   (blocked by 1), external-observer air tests, and any upstream Wi-Fi driver (the PD4 research
   firmware is bench-only and never ships; the vendor GPL source request is unanswered).

## 3. Exact next commands

**Every fire is the Lead's, with a human at the bench, the recovery card READY and a read-only
UART capture running. Host-only work needs none of that.**

### A. Session start (every time)

```sh
tools/session-brief.sh          # one-screen git/doc/bench state + warnings
tools/bench-link.sh             # box link PASS (unmanaged, both addresses, carrier)
tools/recovery-card.sh          # recovery door READY (TFTP + recovery.bin md5)
tools/uart-capture.sh start --label <label> --timeout 900   # read-only console
tools/check-repo.sh             # static docs/tools drift
```

### B. The band-1 start fire (after band1-start-send delivers)

Follow `opensource/docs/incoming/wifi-band1-start/FIRE-RECIPE.md`: the **P1 read-only probe
pack** first (P1.4 must be **0x404C4220**, not the old mis-addressed 0x404C8220; the 2.4 GHz AP
must be beaconing as the reference), then one ladder: F1 = key 0x0064 (fresh band-1 ctx bind,
`body[4]`=N, `body[0x12]`=1), F2 = key 0x0242 field B=N body `{01,00,01,00,00}` (fallback
F3 body `{02,00,00,00,00}`, F4 key 0x0243), then the P1 re-read. Abort rules are in the recipe.

### C. Image #3 rootfs-only flash (Lead, attended)

```sh
# host: extract the root UBI from the container and verify it
tar -xOf <image-3-sysupgrade.bin> sysupgrade-cudy_wr3000-v2/root > /tmp/i3-r.ubi
sha256sum /tmp/i3-r.ubi              # 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3 (4,456,448 B)
# stock pair-A root shell (recovery armed, pair A active, UART capture read-only):
dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # 1794a531... (kernel unchanged)
wget -q -O /tmp/i3-r.ubi http://192.168.10.44:8000/rootfs.ubi  # or: tftp -g -l /tmp/i3-r.ubi -r rootfs.ubi 192.168.10.44
sha256sum /tmp/i3-r.ubi              # 90ae340c...
ubiformat /dev/mtd14 -y -f /tmp/i3-r.ubi ; echo UBI=$?        # expect UBI=0 (rootfsb only)
ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg; sync; reboot
```

Post-boot receipts: `uname -a`; `/proc/cmdline` contains `ubi.mtd=rootfsb`; `iw reg get` =
country US + 5 GHz IR; `hostapd -v` (ax build); the image loads nothing (`autoload=0`) - run
`hi5622-load` with the rung's gate set; re-push the PD4 firmware + `wifi_block.bin` by hand;
read the page/gates. Rollback: pair A stays bootable, and the previous rootfs UBI can be
rewritten the same way. Details: `opensource/docs/incoming/wifi-image-3/FLASH-FEASIBILITY.md`
sections 3-5; the method is the Lead's, this file only records the plan.

### D. The RX trace (only if a fresh capture is needed; the 2026-11-28 capture succeeded)

Build + host-test the v2 mmap sampler and run the retry exactly as
`opensource/docs/incoming/wifi-rx-h2d-window/RUNBOOK-DELTA.md` says. Mandatory pre-capture gate
on the stock root shell: `/usr/bin/omo_h2dsampler -p -w 0x583f2000` must print
`access=mmap`, a non-zero SR0 base, `depth=0x1f`, `ring=YES`, and
`pread_control=FAIL errno=14` (the measured v1 defect, expected). Capture args
`-w 0x583f2000 -W 180 -t 240`; decode with the lane's `decode_h2d.py`
(`opensource/docs/incoming/wifi-rx-h2d-trace/tools/decode_h2d.py`); do not rerun the v1 binary.
Access rules: BENCH-FACTS section 7.

### E. Close-out rules (carried from AGENTS/BENCH-FACTS)

UART is READ-ONLY (never transmit). Recovery is physical; an agent cannot power-cycle or hold
RESET. No fire without preflight PASS + armed recovery + human + read-only capture. Never write
CA `0x400392F0`, never read `0x10161000`, never `rmmod` the vendor modules, never replace
`/etc/rc.d/luofu` wholesale. No vendor binaries/firmware bytes/secrets in tracked text. Bench
gates are not upstream progress.

## 4. Lane inventory (the 2026-11-27/28 Wi-Fi wave)

Every lane commits its deliverables under `opensource/docs/incoming/<lane>/`; the shared task
board is the live tracker (task ids below). `DONE` = committed and reported; `IN FLIGHT` =
running now, no deliverable committed yet.

| task | lane | state | deliverable / scope |
| --- | --- | --- | --- |
| 191 | fw-dfs-worker | DONE | `wifi-fw-dfs-worker/` - the 0x01F7 field-B dispatch root cause (the F2 fix) |
| 192 | fw-beacon-tx | DONE | `wifi-fw-beacon-tx/` - the beacon-TX chain + the R0 verdict (C1/C2/C3 ruled out) |
| 193 | fw-tbtt-arm | DONE | `wifi-fw-tbtt-arm/` - the per-band MAC start bits, the R3 differential, the probe pack |
| 194 | rx-h2d-window | DONE | `wifi-rx-h2d-window/` - the mmap-only root cause, the v2 sampler, EP1 window, RUNBOOK delta |
| 195 | ax-advanced | DONE | `wifi-ax-advanced/` - the ten AX config keys, the ALG text plane, the TWT host path |
| 196 | image-3 | DONE | `wifi-image-3/` - IMAGE #3 baked + 129/129 verified (not flashed) |
| 197 | fw-band1-start | DONE | `wifi-band1-start/` - the direct 0x4AF0C path + the key 0x0242 gap + the fire recipe |
| 206 | band1-start-send | IN FLIGHT | the key 0x0242 send implementation + the fire |
| 198 | fw-f1-capture | IN FLIGHT | the unmatched captured H2D families -> the missing RX gate |
| 199 | ax-twt-impl | IN FLIGHT | the TWT setup + AX event sends |
| 200 | rc-impl | IN FLIGHT | the rate_mode auto SET, corrected C2 |
| 201 | image-flash-prep | IN FLIGHT | the image #3 flash runbook + preflight verification |
| 202 | upstream-v2-folds | IN FLIGHT | the upstream v2 review form (host-only) |
| 205 | dfs-160 | IN FLIGHT | the 5 GHz 160 MHz + DFS decode (dependent on the beacon) |
| 203 | docs-refresh | THIS FILE | STATUS/README/BENCH-FACTS refresh + this handover |

Earlier lanes (transport, HCC, driver, HE caps, BA, rate-control decode, the image chain) are
committed under the same directory tree; `mem-entries-INDEX.md` indexes the ledger and
`EVIDENCE-INDEX.md` maps claims to artifacts. The publications policy is
`PUBLISH.md`; the upstream target/series lives in
`opensource/docs/` and `opensource/upstream/`.
