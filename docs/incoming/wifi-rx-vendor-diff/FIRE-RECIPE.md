# FIRE-RECIPE - RX vendor-diff ladder (read-only first)

**Who:** the Lead fires; this lane is host-only.  **One load per boot**, one
action per rung, page/mask/staging after EVERY rung.

**Module:** the artifact of [BUILD-NOTES.md](BUILD-NOTES.md) (361 872 B stripped
sha256 81ab72c1..., 59 params), built on the FINAL association + corrected
mgmt-TX carrier + RX watermark tree + this patch.  NOT fired, NOT in any image.

**Crash containment:** with the shipped defaults (rx_drain_write=0,
rx_status_service=0) none of the read-only rungs can write an RX register or
ring pointer.  The only RX writes of the whole run are the ones dp_setup
already performs (proven safe in the E1/AP boots).

**Stop rules (any rung):** page CA 0x1bf000 must stay golden
0x86807FEF/7/2/0xABCD, state CA 0x118b60 = 1, mask CA 0x400008 = 0, staging
CA 0x40000c = 0.  Any nonzero mask / exception / "REFUSED" line ends the run
with a power-cycle - do not retry in the same load.  Never let hostapd restart
cycles accumulate (the E2 stale-VAP crash class).

## Load parameters (association recipe + the two new knobs explicit)

```
insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<block> \
    allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 \
    vap_start_mode=1 vap_start_module=0 hcc_poll_ms=5 hcc_trace=1 stop_ap_mode=0 \
    rx_enable=1 rx_drain_write=0 rx_status_service=0 rx_refill_wm=75 \
    beacon_static=1 beacon_vap_start=1 wpa_keys=1 mgmt_tx=1 mgmt_rx=1 \
    sta_join=0 band_vendor=0 scan_band_cfg=0
```

rx_drain_write=0 and rx_status_service=0 are the compiled defaults; pass them
explicitly for the record.  If R3 is to be included in the same load, set
rx_status_service=1 at load but do NOT write dp_rx_status_clear until R3.

## Rung R0 - parity dump (read-only, no RX flow needed)

After the proven boot ladder + C2 + AP 5-message order + dp_probe + dp_setup +
dp_txq_addr_sync:

```
cat /sys/class/hi5622*/hi5622*/dp_rx_parity    # exact path per the running image
```

Expected (PASS): every "stock=0x... MATCH" line for the fixed config words
(+0x00, +0x1c, +0x2c, +0x30, +0x34, +0x48); ring/table addresses RANGE_OK;
"status=... DYNAMIC"; "parity summary: ... read_fail=0".

The decisive datum to CAPTURE AND REPORT:

```
parity live 40050000+0x14 actual=0x???????? stock=0x00200020 ...
parity live 40050000+0x24 actual=0x???????? stock=0x00800000 ...
... +0x28/+0x2c/+0x30
```

- DEVICE_OK on all five = the device-side RX/MAC state matches the vendor
  stock shape; the capture gap is elsewhere (RF/firmware mgmt forwarding).
- DEVICE_ZERO on any = **the device RX state never reached the vendor shape**
  - the diff's missing link; stop and report (highest-value result of the run).
- read_fail > 0 = transport mapping not up; record and stop.

## Rung R1 - arm (read-only)

```
echo 1 > /sys/.../dp_rx_arm
```

Expected: the rx-static arm line plus the two new lines:

```
DP rx_arm: status decode active=0x102 bits=1,8 (rx=0 fwd=0x102) ...
DP rx_arm: live-bank device-owned words 0x14=... 0x24=... 0x28=... 0x2c=... 0x30=...
```

PASS = no REFUSED, mask still 0xfffffcc1, and the status decode matches the
bench-known "status=0x182 mask=0xfffffcc1 -> active=0x102, rx=0".
Compare the five words with R0; they must not change between R0 and R1.

## Rung R2 - observe-only capture (the decisive rung; ZERO writes)

Keep the client (laptop) attempting auth to the AP's BSSID - 3 attempts are
enough per the client's own log.  During/after each attempt:

```
echo 1 > /sys/.../dp_rx_drain
cat /sys/.../dp_rx_capture
```

Every drain is read-only in this configuration.  Three outcomes:

1. **"DP rx_observe: ... frames=N" (N >= 1)** - THE RX CAPTURE IS PROVEN
   WITHOUT ANY WRITE.  Record the per-frame lines (len/flags/status/next) and
   the dp_rx_capture slice.  PASS.
2. **"no completion pending" but cmp_wptr advanced** (compare the line values
   between drains) - the device produced a completion between the live read
   and the ring read; drain again; PASS when frames appear.
3. **"no completion pending", cmp_wptr = 0x0000 after all attempts** - RX is
   still silent even read-only.  Then and only then go to R3/R4.  (This
   outcome with an exception appearing supports candidate (a) in REPORT.md
   section 5.2: the received frame faults the device path before a completion
   is written.)

## Rung R3 - status service (the one new write; optional, only if R2 silent)

Only if R2 showed rx=0 toggling or an RX-pending bit:

```
echo 1 > /sys/.../dp_rx_status_clear     # requires rx_status_service=1 at load
```

Expected: either "RX completion bit not observable, no write performed" (no
side effect) or one line "W1C 0x4 to config +0x44 ..." followed by a fresh
dp_rx_drain observe attempt.  This is exactly the vendor's post-service clear;
PASS = no exception and a subsequent observe either captures or reports
cleanly.

## Rung R4 - write-enabled drain (ONLY after R2 captured frames)

Power-cycle first (clear any device state), load with rx_drain_write=1 (and
rx_status_service=1 only if R3 was clean).  Then:

```
echo 1 > /sys/.../dp_rx_arm
echo 1 > /sys/.../dp_rx_drain      # consumes completions, refills, publishes
cat /sys/.../dp_rx_capture
```

PASS = drained>0, free-ring producers/consumer published with read-back, no
exception.  This is the old rx-static/watermark path, now explicitly opted in.

## Crash sampling (every rung)

Record after each rung: page CA 0x1bf000 (+0/4/8), state CA 0x118b60, mask
CA 0x400008, staging CA 0x40000c, the hcc event counters (excp_announce,
vector_mask, d2h words), and the dmesg tail.  If mask becomes 0x04000010
(vector 4 = data abort): capture the 17-word vector frame CA 0x40000C..0x400044
and stop - report which rung it appeared in (R0-R2 are write-free, so an
exception there is NOT caused by this patch's RX actions).

## Expected evidence bundle

- R0: dp_rx_parity full output (the five device-owned words!).
- R1: arm output incl. status decode + device-owned words.
- R2: dp_rx_observe lines and dp_rx_capture (the capture proof, if any).
- R3: dp_rx_status_clear line + follow-up observe.
- every rung: page/mask/staging/hcc snapshot and the dmesg tail.

