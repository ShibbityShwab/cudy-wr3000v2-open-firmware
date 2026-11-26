# FIRE-RECIPE - monitor mode + spectral: the attended ladder

**Lane:** `monitor-mode` (task-241). **HOST-ONLY lane: the Lead fires; this lane delivers the
ladder, the patch and the host evidence.** No device was contacted by this lane.

Read first: [REPORT.md](REPORT.md) sec 0 (verdict), [CFG80211-MONITOR.md](CFG80211-MONITOR.md)
sec 3 (the deltas) and [BUILD-NOTES.md](BUILD-NOTES.md) (the artifact under test).

## Global preconditions (every rung)

* Recovery door **READY** (`tools/recovery-card.sh`), human at the bench, read-only UART
  capture running to an absolute path (`tools/uart-capture.sh`; never write to the tty).
* Bench link PASS (`tools/bench-link.sh`); page/mask baseline read **before** the rung
  (page CA 0x1BF000 = 0x86807FEF/7/2/0xABCD, state = 1, mask = 0) and re-read after.
* The module under test is the prototype from `BUILD-NOTES.md` (base: IMAGE #3 merged tree
  + `patches/0001-monitor-mirror.patch`); load only our `hi5622_wifi.ko`; **never rmmod the
  vendor modules**, never touch CA 0x400392f0, never read 0x10161000.
* Abort rules: ANY page-loss, mask != 0, unexpected oops/hang, or a monitor log line that
  names a firmware write -> power-cycle + recovery, record the UART, stop the ladder.

## R0 - host-only build + tests (no bench, can run today)

| step | action | PASS |
| --- | --- | --- |
| R0.1 | apply the patch to a fresh copy of the base tree; build the module against the pinned 6.18.60 tree | 0 compiler/modpost warnings; `depends=cfg80211`; module size/sha recorded |
| R0.2 | run `tools/monitor_selftest.c` | all checks PASS (policy, channel map, radiotap vectors, bounds) |
| R0.3 | verify the module params readback in the build log | 65 params incl. `monitor`; no other param changed |
| R0.4 | control build of the unpatched tree | identical config, so the delta is only the patch |

R0 is the gate for everything below; it needs no router.

## R1 - cfg80211 monitor rung (attended; **no firmware write**)

Goal: prove the new cfg80211 contract bits (iface + channel op) without touching firmware.
This rung can fire as soon as the module is built; **it cannot capture a frame** (the F1 is
still open).

1. Boot the image, load the module with `monitor=0` (default). Readback: dmesg shows the
   wiphy registration line; `monitor: status: mode=0 (off)` once.
2. `iw dev wlan0 interface add mon0 type monitor` -> PASS1: `iw dev` lists `type monitor`;
   `ip link show mon0` shows `link/ieee802.11/radiotap`; dmesg VAP-added line.
3. `iw dev mon0 set channel 6` -> **expected FAIL by design** at `monitor=0`:
   `-EOPNOTSUPP` / `command failed: Operation not supported`. Record it: this is the honest
   control for the new op.
4. Reload our module with `monitor=1` (rmmod/insmod of OUR module only); recreate `mon0`.
5. `iw dev mon0 set channel 6` -> PASS2: rc=0; dmesg `monitor: channel set 2437 MHz (metadata
   only - no firmware tune)`; `iw dev mon0 info` shows channel 6. Repeat with 36 -> rc=0
   (PASS2b) and with a disabled channel -> `-EINVAL` (PASS2c).
6. Read back page/mask. PASS3: unchanged.
7. `tcpdump -i mon0 -e -c 1` with monitor=1: expected **timeout/no frame** (the F1 is open;
   this is the negative control, not a failure of the patch).

**R1 PASS = PASS1 + PASS2/2b/2c + PASS3 + the negative capture control.** No firmware write
occurs in R1 (the op is documented as metadata-only).

## R2 - mirror-capture rung (after the F1 lands; attended)

Precondition: the F1 (task-236 / `g1 id13` host-bound RX copy) has a PASSed fix in the loaded
image, and the AP can serve a client (hostapd AP-ENABLED).

1. AP up on ch 6 with `monitor=1`, `mon0` up, `tcpdump -i mon0 -e -n`.
2. Trigger real management frames from an external client: a probe scan (laptop/phone scan)
   and an auth attempt.
3. PASS: ≥1 frame captured with a **valid radiotap header** (Wireshark/tshark shows the
   radiotap layer + the 802.11 frame; e.g. probe request / auth), and the monitor counters
   (dmesg rate-limited line) increment; page/mask unchanged.
4. Negative control: same trigger with `monitor=0` -> 0 frames captured.
5. Optional differential: a known client's auth frame appears both in hostapd's log and in
   the `mon0` capture with the same address/sequence.

If the capture is empty **with** the F1 fix: check the dmesg mirror counters (no_dev /
dev_down / busy / bad_len) before suspecting the RX chain; report the counter, do not add
writes.

## R3 - true promiscuous sniffer (BLOCKED - do **not** fire)

Requires the hi5622 firmware monitor switch (TABLES T8 M-1) or a monitor VAP mode (M-2), neither
decoded. The GPL-family cfg id 522 is a **different chip's** message; sending it is an
unvalidated firmware write and is forbidden by the project rules. This rung stays closed
until a firmware decode names the exact message, payload and expected consumption.

## S-rungs - spectral (BLOCKED - do **not** fire)

| rung | needs | state |
| --- | --- | --- |
| S0 | S-1 decode: the PSD bank CA base + the LMI/event register addresses | **BLOCKED** - this lane's next decode |
| S1 | read-only probe of +0x430/+0x434/+0x438 and the LMI regs | **BLOCKED** on S0 |
| S2 | register round-trip (fft_size/count/period, psd_en 1/0) under a fail-closed param | **BLOCKED** on S1 |
| S3 | PSD capture with a known interferer, differential PASS | **BLOCKED** on S2 |
| S4 | documented sysfs/debugfs interface + capture recipe | **BLOCKED** on S3 |

The spectral register map in [SPECTRAL.md](SPECTRAL.md) / TABLES T6 is a static decode; **no
spectral register write is allowed before S0+S1**, and no rung above S0 is fireable today.

## Evidence to keep (absolute paths under `build/`, gitignored)

* `build/uart-logs/<ISO>-monitor-r1.log` (read-only UART), dmesg captures, `iw dev/phy`
  dumps, `tcpdump -w` pcap + a tshark text decode, page/mask readback before/after,
  module sha256 + param readback, and the rung's PASS/FAIL table filled in.

## Rollback

Reload the previous (unpatched) module or reboot to the prior image; the prototype performs
no persistent writes, no flash, no firmware change. The monitor netdev disappears with the
module.
