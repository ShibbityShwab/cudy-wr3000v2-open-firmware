# TEST-LADDER - wifi-rx-watermark (task-156)

Host-only lane.  **The Lead fires the bench; this lane does not.**  Every bench
rung is fail-closed, one load per boot, attended with the recovery door armed.
Page golden `0x86807FEF`, host MAC mask (config +0x48) `0xfffffcc1`, staging 0
on every rung (project rules; see `BENCH-FACTS.md` / existing RX ladders).

Module under test: final association + watermark module,
stripped **347,388 B sha256 `d2c608e7...`**, 57 params (identity in
[BUILD-NOTES.md](BUILD-NOTES.md)); new param `rx_refill_wm` (percent of ring
depth, default 75, 0 = off, >100 clamps to 100).

## 1. Host ladder (automated, this lane; all PASS)

Run `./tools/run-host-tests.sh` - **127 checks, 0 failures** (log:
[evidence/host-test.txt](evidence/host-test.txt)).

| rung | what is proven |
| --- | --- |
| H0 rules | target rounding (256/75=192, 512/75=384), 0/disabled, >100 refused, trigger boundary, plan bounds, free-ring packed-phase one-lap cases |
| H1 state machine | the ring model keeps a strict buffer partition (posted / in-flight / queued, no buffer twice) across every simulated action |
| H2 burst | 200-entry burst: posted 56 -> 120 (window refill) -> 184 -> 248; CREDIT_LOW only while below target |
| H3 control | `rx_refill_wm=0` reproduces the old leak exactly (120 posted / 136 queued, stuck); pass is a no-op |
| H4 repair | enabling the watermark at runtime repairs the leaked ring with **no new completions** (184 -> 248, capped 64/action) |
| H5 sustained | 400 steps with <=64 completions/action: ring returns to full after every action, no CREDIT_LOW, refills <= cap |
| H6 one lap | 256-entry (phase-flip) lap recovered to the 3/4 target: 192 posted, 128 watermark refills + 64 window refills |
| H7 small ring | 512-deep ring, 500-entry burst: 12 -> 396 posted (target 384), 116 above-watermark buffers left queued (floor-not-full policy) |
| H8 plan grid | 2 depths x 4 watermarks x 3 caps x posted x avail: refill never exceeds `min(avail, depth-posted, cap)` and is nonzero only with a trigger |

## 2. Bench ladder (design only - the Lead fires)

These rungs are **inside the association run** from
`../wifi-stability/ASSOCIATION-INTEGRATION.md` section 4 and the corrected
carrier rung in
`../wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md` section T1.6.  The point of T4
is EAPOL msg2/msg4 + client data as MAC-ring completions, so a rung can only
PASS once a client associates; without association the ring stays empty and the
honest result is "no data traffic to drain", not a watermark failure.

### Load line (association + watermark)

```
insmod /lib/modules/$(uname -r)/hi5622_wifi.ko \
    fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
    allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
    vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
    hcc_poll_ms=5 hcc_trace=1 stop_ap_mode=0 mgmt_diag=0 \
    rx_enable=1 beacon_static=1 beacon_vap_start=1 \
    wpa_keys=1 wpa_key_module=0 \
    sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 \
    mgmt_tx=1 mgmt_rx=1 sta_ops=1 eapol_rx=1 sta_user_del=0 \
    data_tx=1 data_tx_peer=1 data_tx_protect=0 data_tx_timeout_ms=200 \
    rx_refill_wm=75
```

Order (unchanged from the association ladder): boot ladder -> iface `__ap` ->
hostapd AP-ENABLED with the 5-message order (vap_add -> ap_chan 0x0124 ->
ap_ssid -> ap_cfg -> ap_vap_start) -> wire the dp chain once
(`cat dp_probe`, `echo 1 > dp_setup`, `echo 1 > h2d_txq_addr_sync`) ->
`echo 1 > dp_rx_arm` -> client association / EAPOL attempt -> drain writes.

### T4-R0 - fail-closed default (safety rung, no RX pointer may move)

Same load **without** `rx_enable=1` (default 0; keep `rx_refill_wm` at its
default 75), boot ladder to DEVICE READY, then:

```
echo 1 > .../dp_rx_arm      # expect -EPERM
echo 1 > .../dp_rx_drain    # expect -EPERM
cat .../dp_rx_capture | grep 'RX watermark'
```

PASS: both writes refused, dmesg says "REFUSED - rx_enable=0", the
`RX watermark:` line reports `rx_refill_wm=75%` with all counters 0, no register
write performed, page/mask/staging golden.  This proves the new default cannot
move a ring pointer on a plain boot/autoload.

### T4-R1 - control: old behaviour (`rx_refill_wm=0`)

Load with `rx_enable=1 ... rx_refill_wm=0`, arm, run the association/EAPOL
attempt, then:

```
echo 1 > .../dp_rx_drain
dmesg | grep 'DP rx_drain' | tail -n 2
cat .../dp_rx_capture | grep 'RX watermark'
```

PASS: behaviour identical to the verified `ab9e18a4` RX path - the
`RX watermark:` line shows `rx_refill_wm=0%`, trips=0, credit_low=0,
refilled{norm=0 small=0}; the drain line is the old window-refill line (the wm
counters are present but zero); no `rx_failed`; page/mask golden.
This is the A/B control that separates the new pass from the proven path.

### T4-R2 - primary: `rx_refill_wm=75` during association / EAPOL

Same as T4-R1 with `rx_refill_wm=75`.  After hostapd reaches AP-ENABLED and the
client auth/assoc/EAPOL attempt, write `dp_rx_drain` a few times (one write per
observation is the attended model; e.g. 5-10 writes, ~0.5-1 s apart):

```
for i in 1 2 3 4 5; do echo 1 > .../dp_rx_drain; sleep 1; done
dmesg | grep 'DP rx_drain'
cat .../dp_rx_capture | grep 'RX watermark'
```

PASS (T4 rung):
* `drained>=1` on at least one action (frames actually reached the host ring:
  record the capture: `desc_ok=1`, plausible MPDU prefix; EAPOL ethertype
  `0x888e` in the payload for msg2/msg4 once the 4-way begins);
* the watermark line shows `posted >= target` (`norm=192`, `small=384`) after
  the first actions once `refilled`/trips advance, `credit_low` not growing
  across actions, no `rx_failed`, free-ring producers advanced
  (`refilled{norm,small}` totals grow);
* page `0x86807FEF/7/2/0xABCD`, mask 0, staging 0 on every check.
RECORD: completions drained per action, refilled counts, posted/target,
credit_low count, any bad_entry/chain_refused, hostapd 4-way progress, frame
type of each capture.

### T4-R3 - starvation diagnostic (only if R2 shows credit_low growing)

```
for i in $(seq 1 20); do echo 1 > .../dp_rx_drain; sleep 0.5; done
dmesg | grep 'DP rx_drain' | tail -n 5
```

PASS: `credit_low` stops growing, `posted` returns to >= target, `refilled`
advances, no `rx_failed`.  Interpretation: the device produced more completions
between two drain writes than one 64-entry action could return; the answer is
more frequent drains (or a raised `rx_drain_max`, still <= 64), **not** a lower
watermark.  If `posted` sits at target while buffers stay queued, that is the
designed floor - only "below target after publishing everything" is credit-low.

### T4-R4 - soak (only after R2 passes)

`hcc_trace=0` load (quiet), same params; keep a client doing DHCP/ping and
write `dp_rx_drain` every second for ~60 s:
```
for i in $(seq 1 60); do echo 1 > .../dp_rx_drain; sleep 1; done
dmesg | grep -c 'DP rx_drain: drained'
cat .../dp_rx_capture | head -n 20
```

PASS: no `rx_failed`, no lane "REFUSED", captures accumulate, `posted` never
stays below target with the queue non-empty, page/mask stable, no exception.

## 3. Abort / stop rules (all rungs)

* page != `0x86807FEF`, mask != `0xfffffcc1`, staging != 0, or an exception:
  power-cycle; do not retry in the same load.
* any "DP rx_drain: REFUSED" / "lane closed" line: stop the rung, keep dmesg;
  the patch intentionally has no retry (power-cycle if a reload is needed).
* never `rmmod` after release; one load per boot; one post per load where the
  frame-post budget applies.
* the RX path sends no firmware message and enables no IRQ; the stop rule is
  the page/mask/staging triad plus the RX lane's own fail-closed line.
