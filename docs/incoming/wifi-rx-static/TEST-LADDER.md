# TEST-LADDER - wifi-rx-static (task-137)

Attended bench ladder design only: **the Lead fires**, this lane does not touch
the bench.  Every rung is fail-closed and stops on a page/mask anomaly.  Page
golden stays `0x86807FEF`, host MAC mask (config +0x48) stays `0xfffffcc1`,
staging 0.  Recovery door armed before every boot (project rule).

Module: the patched final combined build (artifact hashes in BUILD-NOTES.md),
stripped 280,736 B / `87c8f2df...`.  Load with `rx_enable=1` only for rungs
L1+; rung L0 proves the fail-closed default.

## L0 - fail-closed default (no new behaviour unless asked)

```
insmod hi5622_wifi.ko            # rx_enable defaults to 0
... boot ladder to DEVICE READY ...
echo 1 > /sys/bus/pci/devices/0001:00:00.0/dp_rx_arm     # expect -EPERM
echo 1 > /sys/bus/pci/devices/0001:00:00.0/dp_rx_drain   # expect -EPERM
dmesg | tail -n 5
```

PASS: both writes refused, dmesg says "REFUSED - rx_enable=0", no register
write, page golden.  STOP on any other behaviour.

## L1 - read-only live-bank proof (THE key rung)

```
insmod hi5622_wifi.ko rx_enable=1
... boot ladder to DEVICE READY, dp_setup, dp_txq_sync (as in the dp ladder) ...
echo 1 > .../dp_rx_arm
dmesg | grep 'DP rx_arm'
```

PASS: the arm line exists and shows
`live bank CA 0x40050000 status=... mask=0xfffffcc1 norm_rptr=... small_rptr=...
cmp_wptr=... pending=0` (or cmp pending if traffic is already flowing).
Expected healthy mirror of the vendor stock dump: `norm_rptr` and
`small_rptr` = `0x20` (the MAC prefetch), `cmp_wptr = 0`.
Bounds: no 0xffffffff reads, mask exactly 0xfffffcc1, 15-bit indices below the
depths.  FAIL-CLOSED: on mismatch arm refuses and prints the values.  RECORD
the live values even if zero - a zero here is itself the next finding (the MAC
did not fetch our published free-ring entries).

## L2 - live telemetry under poll (read-only)

```
echo 1 > .../dp_rx_poll
dmesg | grep 'live bank'
cat .../dp_rx_capture
```

PASS: the `DP rx_poll live bank` line appears; captures may be 0 (idle AP has
no host-forwarded frames).  STOP if the live read is 0xffffffff.

## L3 - idle drain is a no-op (safety of the consumer path)

```
echo 1 > .../dp_rx_drain
dmesg | tail -n 5
```

PASS: "no new completion ... no pointer moved", RX lane not failed, page
golden/mask unchanged.  This proves the drain plumbing before any real frame.

## L4 - first completion capture (needs an RX stimulus)

Prerequisite: AP-ENABLED (wifi-ap-start v7 flow, one boot).  Stimulus options
in order of least bench cost:

* L4a: an external phone/STA in range scans/probes (probe requests may be
  forwarded to the host rings; the firmware may also answer them itself - this
  rung decides which).
* L4b: a client connects (auth/assoc frames); WPA2 needs the key path lane, but
  the open/no-key association attempt still produces management RX frames.

```
# after the stimulus
echo 1 > .../dp_rx_drain
dmesg | grep 'DP rx_drain'
cat .../dp_rx_capture
```

PASS: `drained>=1`, `dp_rx_capture` shows a capture with `desc_ok=1`,
`payload_len(+0x1a)` in 1..0x618 (norm) / 1..0x8c (small), and the printed
mpdu prefix is a plausible 802.11 frame (type/subtype nibble in the first
16-bit word).  Bounds: 0 bad entries, 0 chain refusals, page golden, mask
0xfffffcc1, no exception.  STOP on a refusal: the lane closes itself and the
line names the class (bad entry / header / chain / publish read-back).

If L4a yields no completion after >=60 s: record it.  The honest conclusion
is then "the firmware does not forward this class to host RX rings" and the
next step is L4b with a key-path/STA lane, not a longer soak.

## L5 - short soak after the first capture

```
for i in $(seq 1 60); do echo 1 > .../dp_rx_drain; sleep 1; done
dmesg | grep -c 'DP rx_drain: drained'
cat .../dp_rx_capture | head -n 20
```

PASS: no `rx_failed`, free-ring producers advance (refilled norm/small
counters grow), captures accumulate, page golden / mask stable, no exception.
Record: completions/s, refill rate, any chain refusal (chain traffic means the
sub-MSDU decode is the next lane's work).

## Abort rules (all rungs)

* page != 0x86807FEF, mask != 0xfffffcc1, staging != 0, or an exception:
  power-cycle; the lane module is not reloaded (never rmmod after release).
* any "lane closed"/"REFUSED" line: stop the rung, keep the dmesg, do not
  re-run the same action (the patch intentionally has no retry).
* the RX path never sends firmware messages and never enables an IRQ, so it
  cannot wedge the firmware control plane; the stop rule is the page/mask.
