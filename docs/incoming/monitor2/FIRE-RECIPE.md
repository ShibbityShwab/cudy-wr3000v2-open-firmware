# FIRE-RECIPE - monitor mode + spectral: the corrected attended ladder

**Lane:** monitor2 (task-246). **HOST-ONLY lane - the Lead fires; this lane delivers the
ladder, the patch and the host evidence.** No device was contacted by this lane.

Read first: [REPORT.md](REPORT.md) §0, [CFG80211-MONITOR.md](CFG80211-MONITOR.md) §2
(the corrected 3-arg op + the monitors-only rule), [BUILD-NOTES.md](BUILD-NOTES.md).

## Global preconditions (every rung)

* Recovery door **READY** (`tools/recovery-card.sh`), human at the bench, read-only UART
  capture to an absolute path (`tools/uart-capture.sh`; never write to the tty).
* Bench link PASS (`tools/bench-link.sh`); page/mask baseline read **before** the rung
  (page CA 0x1BF000 = 0x86807FEF/7/2/0xABCD, state = 1, mask = 0) and re-read after.
* Module under test = the prototype build from [BUILD-NOTES.md](BUILD-NOTES.md) (base tree
  + `patches/0001-monitor-mirror.patch`); load only our `hi5622_wifi.ko`; **never rmmod the
  vendor modules**, never touch CA 0x400392f0, never read 0x10161000.
* Abort rules: any page-loss, mask != 0, oops/hang, or a monitor log line naming a firmware
  write -> power-cycle + recovery, keep the UART, stop the ladder.

## R0 - host-only build + tests (DONE by this lane; no bench needed)

| step | action | PASS |
| --- | --- | --- |
| R0.1 | apply the patch to a fresh copy of the base tree; cross-build against the pinned 6.18.60 tree | **DONE**: 0 warnings/errors, 65 params (adds `monitor`), depends=cfg80211, vermagic 6.18.60 |
| R0.2 | run `tools/monitor_selftest.c` | **DONE**: 34/34 checks |
| R0.3 | params readback | **DONE**: patched 65 vs unpatched control 64; every other param unchanged |
| R0.4 | control build of the unpatched tree | **DONE**: 0 warnings/errors |
| R0.5 | patch round-trip on a fresh tree | **DONE**: byte-identical |

## R1 - cfg80211 monitor rung (attended; **no firmware write**)

**Correction vs task-241:** `SET_CHANNEL` for a monitor iface requires
`cfg80211_has_monitors_only()` - the monitor netdev must be RUNNING and no other iface may
be running (`net/wireless/chan.c:1544-1552`). So: create + bring up `mon0` first, set the
channel with the AP iface DOWN, then start the AP. Once the AP runs, a further SET_CHANNEL
returns -EBUSY (expected; not needed - the mirror follows the AP).

1. Boot IMAGE #3; load our module with `monitor=0` (default; do **not** start hostapd yet).
2. `iw dev wlan0 interface add mon0 type monitor` -> PASS1: `iw dev` shows
   `type monitor`; `ip link show mon0` shows `link/ieee802.11/radiotap`; dmesg
   `monitor: monitor iface created (mode=0 off; mirror=disabled...)`.
3. `ip link set mon0 up` (this is what makes the monitors-only counter move).
4. `iw dev mon0 set channel 6` at `monitor=0` -> **expected FAIL by design**:
   -EOPNOTSUPP / "Operation not supported". Record it as the honest control for the new op.
5. Reload **our** module with `monitor=1` (rmmod/insmod of our module only); recreate
   `mon0`; `ip link set mon0 up`.
6. `iw dev mon0 set channel 6` -> PASS2: rc=0; dmesg
   `monitor: channel set 2437 MHz width 0 (metadata only - no firmware tune)`; repeat
   with 36 -> PASS2b; a disabled channel -> -EINVAL (PASS2c).
7. Bring the AP up on its normal channel (hostapd config unchanged). A later
   `iw dev mon0 set channel N` -> -EBUSY is the expected monitors-only behaviour
   (PASS2d, do not treat it as a failure).
8. Read back page/mask. PASS3: unchanged. **No firmware write occurs in R1.**
9. `tcpdump -i mon0 -e -c 1` -> expected timeout/no frame while the F1 is open (negative
   control, not a patch failure).

**R1 PASS = PASS1 + PASS2/2b/2c + PASS3 + the negative capture control.**

## R2 - mirror-capture rung (after the F1 lands; attended)

Precondition: the F1 (`g1 id13` host-bound RX copy) has a PASSed fix in the loaded image,
and the AP can serve a client (hostapd AP-ENABLED).

1. Load with `monitor=1`; create `mon0`; `ip link set mon0 up`; set the channel while
   the AP is down (R1 step 6), then start the AP on ch 6.
2. `tcpdump -i mon0 -e -n`; trigger real management frames from an external client
   (laptop/phone scan + auth attempt).
3. PASS: >= 1 frame captured with a **valid radiotap header** (tshark/Wireshark shows the
   radiotap layer + the 802.11 frame, e.g. probe request or auth), the rate-limited dmesg
   `monitor: mirrored ...` line appears, page/mask unchanged.
4. Negative control: same trigger with `monitor=0` -> 0 frames.
5. Optional differential: the same client auth appears in hostapd's log and in the `mon0`
   capture (same address/sequence).
6. If the capture is empty **with** the F1 fix: read the dmesg mirror counters / drop
   reasons (`no_dev/down/busy/bad_len/nomem`) before suspecting the RX chain; report,
   do not add writes.

**Known caveat:** the data sink mirrors data MPDUs only (EAPOL paths included); the mgmt
sink mirrors group-1 id13 events. Frames the firmware filters out before those sinks do not
appear - this is a mirror, not promiscuous mode.

## R3 - true promiscuous sniffer (BLOCKED - do **not** fire)

Requires a decoded hi5622 firmware monitor switch or monitor VAP mode; neither exists in the
vendor driver (VERIFY §2). The GPL-family cfg id 522 is another chip's message - sending it
is an unvalidated firmware write and is forbidden.

## S-rungs - spectral (still BLOCKED, but S-1 is half-closed)

| rung | needs | state |
| --- | --- | --- |
| S0 | the PSD/LMI bank CA | **half-closed**: pointer chain + mapping helper + defaults decoded [V] ([SPECTRAL-S1.md](SPECTRAL-S1.md)); the last check is the init table's .rodata relocation addend (one host-only `readelf -r` step) |
| S1 | attended read-only probe of bank+0x430/+0x434/+0x438 and +0x528..+0x53c | BLOCKED on S0; only after S0 names the CA |
| S2 | register round-trip under a fail-closed param (defaults: fft_size=2, period=40, count=2, psd_en=0) | BLOCKED on S1 |
| S3 | PSD capture with a known interferer (differential vs floor) | BLOCKED on S2 |
| S4 | sysfs/debugfs surface + capture recipe | BLOCKED on S3 |

**No spectral register write is allowed before S0+S1.** The stock 2.5.24 set path only
exposes fft_size/fft_period/count (the other PSD setters have no runtime caller), so the
clean-room surface is small once the bank CA is known.

## Evidence to keep (absolute paths under `build/`, gitignored)

`build/uart-logs/<ISO>-monitor-r1.log` (read-only UART), dmesg captures, `iw dev/phy`
dumps, `tcpdump -w` pcap + tshark text decode, page/mask readback before/after, module
sha256 + param readback, the rung PASS/FAIL table.

## Rollback

Reload the previous (unpatched) module or reboot to the prior image; the prototype performs
no persistent writes, no flash, no firmware change. The monitor netdev disappears with the
module.
