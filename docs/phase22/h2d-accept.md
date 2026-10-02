# h2d-accept: the harness and batched runner on real hardware, and the device-side H2D HCC accept gate that still holds (phase 22, 2026-10-02)

Task `st_01a0fbea`. Executes the queued experiment in `docs/phase22/NEXT-EXPERIMENT.md` on the router
with the phase-22 harness, from the proven `bothep` boot-2 configuration (claim both PCIe functions,
decode both RCs, drive the ETE rings/release through EP0's BAR, live ISR on irq 209). Module:
`lab/hccaccept/hccaccept.c` (bothep + the batch convention of `docs/phase22/exp-harness.md`). Every
claim here is **[proven]** (a value the device or the harness printed this session).

**Headline.**

- **The harness is proven on real hardware.** `tools/exp.sh` ran a full one-module cycle to
  `EXP RESULT: PASS`; `tools/batch.sh` ran **20 hypotheses in ONE boot** to
  `BATCH RESULT: PASS` with per-entry `result.txt`. The author lane had only dry-run them.
- **Three real harness bugs surfaced on the first boots and were fixed** (list persistence, a
  health-gate race, and a completion-marker false-positive). All three would have blocked the run.
- **BOOT A (batched, safe: H4 glue arm, H2 ETE-interrupt sweep, H3 producer-commit variants): the
  chip's state does not change on any of the 20 entries.** `out[0]` (H2D mask, CA `0x40039010`) is
  never cleared by the device, there is no id-1 reply/payload, `SR ch0 +0x1c` stays at `0x400`, and
  the glue status `0x400392ec & 0x3d8` stays `0`. The series was not stopped (no change to stop on).
- **BOOT B (single, highest risk: the vendor's synchronous arm `out[5]` CA `0x400392f0 <= 8`): in
  the proven dual-RC configuration the write did NOT hang the chip** — it read back `0` immediately
  (self-clearing, like `out[2]`) — **but it still produced no H2D accept**: `out[0]` not cleared, no
  id-1. (The phase-20 hang was `fwaccept` on EP0 only, without the sibling's live interrupt.)
- **The gate still holds. Named next blocker:** the firmware's own H2D dispatcher (file `0x818a8`,
  which acks `0x400392f0`, reads+clears `out[0]` and re-arms `0x400392d4`) is never entered. No
  host-writable register in the exposed windows arms it; the remaining work is device-side
  (firmware trace/JTAG or reproducing the vendor's message-context/ISR binding), not another
  register poke.

---

## 0. Module and build

`lab/hccaccept/hccaccept.c` is `lab/bothep/bothep.c` verbatim (the proven boot-2 bring-up) plus:

- the batch convention: params `batch=<list>`, `batchdir=<dir>` default `/tmp/omo-batch`; parses one
  `label|hyp=..,arg=..` line at a time, resets the post-release base state before each entry,
  writes `<batchdir>/<NN>-<label>/result.txt` with exactly
  `label= params= out0_cleared= sr1c= glue= irq=`, then `batch-summary.txt` and `batch.done`;
- the hypothesis engine (`hyp=`): `none` (control: refill + full-lap SR commit + id-3 doorbell),
  `glue` (H4: CA `0x400392e8 <= 0x20`), `intror` (H2: CA `0x40039508 <= 0x3f201f1f`), `intrbit`
  (H2: `0x3f201818 | (1<<arg)`), `h3noop|h3full|h3zero|h3ctrl|h3ctrl48` (H3 producer-commit
  edge/phase variants on SR ch0), and `out5` (BOOT B);
- `resultpath=` writes a persistent single-mode status file; for `out5` it writes `stage=pre-out5`
  and `stage=arming-out5` **before** the risky write, so a watchdog reset is attributable.

`out0_cleared` is `y` iff `out[0]` transitions nonzero -> zero during the entry window, sampled
after the entry's own id-3 doorbell (so the module's own clears are not miscounted). `irq` is the
larger of the two requested-line counters (the live line is irq 209).

CI (GitHub Actions `build-load-test-module`, new step + `hccaccept-ko` artifact):

| run | module md5 | used by |
| --- | --- | --- |
| `36989832590` | `07d07b0bffeada5291f3926d62d867fe` (82452 B) | BOOT A (#1 and #2) |
| `36990797432` | `b586de977686a190591929f9fc0a94e7` (82940 B) | BOOT B (adds only the `arming-out5` marker) |

`vermagic=5.10.201` (the CI `show vermagic` list covers neither `hccaccept` nor `bothep`, so the
artifact was verified directly). Scope: module sources, the two harness scripts, evidence under
`build/register-dumps/`, this report. CA `0x400392f0` is written **only** in BOOT B's `out5`
hypothesis; the RC misc window `0x10161000` is never read.

## 1. Harness bugs found and fixed

| # | script | bug | fix | proof |
| --- | --- | --- | --- | --- |
| 1 | `tools/batch.sh` | the hypothesis list was staged to `/tmp/omo-batch/list`, but `/tmp` is **tmpfs** and exp.sh reboots into the takeover boot — the list would be gone before the module ran | stage the list at the persistent overlay path `/root/omo-batch-list` (results stay in `/tmp/omo-batch`, pulled before recovery in the same boot) | inspection; fixed before the first real run |
| 2 | `tools/exp.sh` | the recovery script did not remove the new persistent list | added `rm -f /root/omo-batch-list` / `rm -rf /root/omo-batch` to the generated recovery | final health OMO_OFF/STAGED/LOADER = 0 and no `/root/omo-batch-list` |
| 3 | `tools/exp.sh` | the health gate probed **once**, immediately after SSH returned; the recovered vendor stack needs ~20-30 s to load/calibrate, so it read `WIPHY=0` and failed a healthy boot | bounded-retry the health probe (`EXP_HEALTH_TIMEOUT`, default 180 s) until healthy or timeout | BOOT A #1: `BATCH RESULT: FAIL ... reason: post-recovery health check failed / health: wiphy=0/2 iface=0/6` — same boot verified healthy by hand minutes later; BOOT A #2 with the fix: **PASS** |
| 4 | `tools/exp.sh` | default `EXP_DONE_CMD="dmesg \| grep -q 'done ('"` matched the device boot line `S95done (5809): drop_caches: 3`, so step 4 "succeeded" and the harness captured/recovered **before the S99 loader ran** | pattern is now `': done ('`; step 4 also requires the staged module to be in `lsmod` before accepting the marker | BOOT B #1: printed `EXP RESULT: PASS` with an **empty** `module-log.txt` and no module dmesg (false pass); BOOT B #2 with the fix shows the full module log |

The scripts are host-side (`router-openwrt/tools/`, outside the git repo), so these fixes are local;
only the module and CI step are committed (branch `omo/phase22-hccaccept`).

## 2. BOOT A — batched, safe hypotheses (one boot)

Command (host):

```
bash tools/batch.sh --module opensource/build/tmp/phase22/hccaccept-ko/hccaccept.ko \
  --params "domain=0 domain2=1 program=1 outwin=1 devvabase=0x80000000 devvaend=0xffffffff \
            hostcabase=0x80000000 release=1 useirq=1 irq=207 hostirq=207 useirq2=1 irq2=209 \
            hostirq2=209 rings=1 acpoff=0 enable=0 intr=0 srctrl=0 seq=0 svc=0 scanlen=0 \
            hccwin=1200 stopfirst=1" \
  opensource/lab/hccaccept/batch-bootA.list
```

Harness lines (run #2, `20261002-093334`):

```
-- [1/7] arm device-side self-recovery (mandatory)
-- [2/7] stage the module
-- [3/7] reboot and wait for SSH
  device back: uptime=104s
-- [4/7] wait for the completion marker
  completion marker seen
-- [5/7] capture the evidence
  evidence: .../exp/20261002-093334-batch
-- [6/7] recover
-- [7/7] verify health
  wiphy=2/2 iface=6/6 cal_succ=1 omo_off=0 staged=0 loader=0

BATCH RESULT: PASS module=hccaccept ...
BATCH RESULT: PASS hypotheses=20 out0_cleared_yes=0 evidence=.../exp/20261002-093334-batch/
```

Per-entry observables (`batch-summary.txt` / each `result.txt`; `glue` = `0x400392ec & 0x3d8`,
`irq` = live-line cumulative count). **Every entry: `out0_cleared=n`, `sr1c=0x00000400`,
`glue=0x00000000`, `id1=0`, `fetched=0`.**

| # | label | hyp | out0 | sr1c | glue | irq |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | control | none | n | 0x400 | 0 | 100017 |
| 2 | h4-glue | glue (`0x400392e8<=0x20`) | n | 0x400 | 0 | 100034 |
| 3 | h2-intr-or | `0x40039508<=0x3f201f1f` | n | 0x400 | 0 | 100051 |
| 4-13 | h2-intr-bit0/1/2/3/4/8/9/10/11/12 | `0x3f201818\|(1<<n)` | n | 0x400 | 0 | 100068-100220 |
| 14 | h3-noop | `+0x18<=rptr` | n | 0x400 | 0 | 100236 |
| 15 | h3-full | `+0x18<=rptr+depth` | n | 0x400 | 0 | 100253 |
| 16 | h3-zero | `+0x18<=0x000 then 0x400` | n | 0x400 | 0 | 100270 |
| 17 | h3-ctrl0 | `+0x08 low3=0` + commit | n | 0x400 | 0 | 100287 |
| 18 | h3-ctrl7 | `+0x08 low3=7` + commit | n | 0x400 | 0 | 100304 |
| 19 | h3-ctrl48-0 | `+0x48=0` + commit | n | 0x400 | 0 | 100321 |
| 20 | h3-ctrl48-1 | `+0x48=1` + commit | n | 0x400 | 0 | 100338 |

Module log, decisive lines (run #2):

```
[post0 +1580ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 (host wptr=0x00000400) = SR engine read
base recorded: ETE intr 0x40039508=0x3f201818 glue 0x400392e8=0x00000020 sr_ctrl=00000000/00000000/00000000
[entry 1 control]  out0_cleared=n sr1c=0x00000400 glue=0x00000000 irq=100017 id1=0 fetched=0
...
[entry 20 h3-ctrl48-1] out0_cleared=n sr1c=0x00000400 glue=0x00000000 irq=100338 id1=0 fetched=0
done (batch entries=20 stopped=0)
```

**Interpretation.** The proven configuration's fetch is the **pre-release** lap (`SR ch0 0x10 ->
0x400`, as in `bothep` boot 2). Post-release producer commits — no-op edge, full lap, explicit phase
toggle, `+0x08` 0..7, `+0x48` 0/1 — **do not re-trigger the engine**: `SR+0x1c` stays at `0x400`
across all entries, so the H3 edge/phase variants cannot change SR state and the series has no
change to stop on. H4's glue arm and H2's ETE-interrupt OR/bit sweep likewise change nothing
observable (`out[0]`, `sr1c`, `glue` all identical to control). The `[ISR ep0] out[0] 0x08 -> 0x00`
lines in the log are the module's own `base_reset` clearing the previous entry's doorbell before the
next doorbell — the per-entry sample starts **after** its own doorbell, and reports `n`, i.e. the
device never clears the post-doorbell `out[0]`.

## 3. BOOT B — the single highest-risk arm: `out[5] <= 8`

Command (host):

```
EXP_RUN_TIMEOUT=150 bash tools/exp.sh \
  opensource/build/tmp/phase22/hccaccept-ko/hccaccept.ko \
  domain=0 domain2=1 program=1 outwin=1 devvabase=0x80000000 devvaend=0xffffffff \
  hostcabase=0x80000000 release=1 useirq=1 irq=207 hostirq=207 useirq2=1 irq2=209 \
  hostirq2=209 rings=1 acpoff=0 enable=0 intr=0 srctrl=0 seq=0 svc=0 scanlen=0 \
  hccwin=5000 hyp=out5 resultpath=/root/omo-hccaccept-bootB.txt
```

Harness line (run `20261002-094158`):

```
-- [7/7] verify health
  wiphy=2/2 iface=6/6 cal_succ=1 omo_off=0 staged=0 loader=0

EXP RESULT: PASS module=hccaccept ... evidence=.../exp/20261002-094158/
```

Module log, decisive sequence:

```
[post0 +550ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 (host wptr=0x00000400) = SR engine read
single hypothesis hyp=out5 arg=0 window=5000ms
[hyp out5] pre-arm out[5]=0x00000000 sr1c=0x00000400 out0=0x00000008 out1=0x00000004 (pcie_msg_send_irq @0x174a8)
[hyp out5] CA 0x400392f0 <= 0x00000008 readback=0x00000000
single result hyp=out5 out0_cleared=n sr1c=0x00000400 glue=0x00000010 irq=56147 id1=0 fetched=0
done (release=1 rings=1 sr_posted=1 acpoff=0 svc=0 ... irq=207 irq_taken=0 irq_handled=0 ...)
```

Persistent status file pulled after recovery (`/root/omo-hccaccept-bootB.txt`):

```
stage=post hyp=out5 arg=0 out0_cleared=n sr1c=0x00000400 glue=0x00000010 irq=56147 id1=0 fetched=0
```

**Interpretation.**

- **No hang.** `0x400392f0 <= 8` read back `0` immediately and the module ran to completion; the
  board never watchdog-reset, and `/sys/fs/pstore` has **no new record** (blk-0/1/2 mtimes unchanged
  at 10:41/14:37). The phase-20 `fwaccept` hang was an EP0-only takeover without the sibling's live
  interrupt; in the proven dual-RC config (both RCs decoded, ISR on irq 209, engine fetching) the
  same write is consumed without hanging. It is not, however, an accept.
- **No H2D accept.** `out[0]` stays `0x08` after the arm (never observed clearing), there is no
  id-1 bit/payload, `SR+0x1c` stays `0x400`.
- The glue status `0x400392ec & 0x3d8` reads `0x10` at the end; it was **already `0x10` at the
  post-release baseline** in this boot (`[dual post0] ... glue+0x2ec=0x00000010`), so the `out5`
  write did not change it. (The glue word varies across boots — `0` in BOOT A, `0x10` here.)

The `hyp=out5` run therefore **kills** the "out[5] synchronous arm alone is the missing gate"
hypothesis in the dual-RC configuration: the arm is accepted by the register file but does not
cause the firmware dispatcher to run.

## 4. Recovery and health

Device-side self-recovery was armed with `start-stop-daemon -S -b -m` **before staging** in every
harness run (step 1/7), re-armed inside the takeover boot by the self-deleting loader, and cancelled
with the done flag plus a manual recovery; the loader re-arms it before `insmod`, so a chip hang at
init is covered. Four takeover boots (BOOT A #1, BOOT A #2, BOOT B #1 aborted, BOOT B) plus their recovery boots.

Final router state (verified by hand after the last boot):

```
/root/uptime                          ~43s (recovered vendor boot)
lsmod: hi5622v100_plat 323584 3 / hi5622v100_wifi 3387392 1
iw phy | grep -c '^Wiphy'             2
iw dev | grep -c 'Interface'          6   (vap0,1,3,8,9,11)
iwpriv vap0 alg get_2g_power_param    [SUCC]
iwpriv vap8 alg get_5g_power_param    [SUCC]
lib/modules/5.10.201/*.omo-off        0
lib/modules/5.10.201/hccaccept.ko     0
/etc/init.d/omo-* /etc/rc.d/S99omo-*  0
/root/omo-batch-list, /root/recover-exp.sh, /tmp/omo-exp.done   absent
vendor md5 wifi e21629d226ec7de9a860a8955952d311 / plat 23660bc285393e678d5cade1c36c194b = baseline
pstore: no new record
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
calibrating.** (The `.omo-off`/loader/staged checks above are the exp.sh health gate; the 2g/5g
`[SUCC]` and md5 checks were run manually because the gate only probes 2g.)

## 5. Which boot moved what

| boot | module | write under test | chip moved? |
| --- | --- | --- | --- |
| BOOT A #1/#2 | `07d07b0b` | H4 `0x400392e8<=0x20`; H2 `0x40039508` OR + bits 0-4/8-12; H3 `+0x18`/`+0x08`/`+0x48` variants | **nothing**: `out[0]` never cleared, `sr1c` frozen `0x400`, `glue` 0, no id-1. Only the proven pre-release fetch (`0x10 -> 0x400`) happened. |
| BOOT B | `b586de97` | `out[5]` CA `0x400392f0 <= 8` | register accepted (readback 0, no hang), but **nothing else**: `out[0]` never cleared, `sr1c` `0x400`, no id-1; glue `0x10` unchanged from baseline. |

## 6. Named next blocker

**The firmware's H2D dispatcher is never entered, and no host-writable register in the windows the
endpoint exposes arms it.** Concretely: the firmware routine at file `0x818a8` — which would write
`1` to the ack `0x400392f0`, read+clear `out[0]` `0x40039010`, write `8` to the doorbell
`0x400392d4`, and dispatch the lowest pending bit — is present and correct but not reached. This
boot proves the last cheap host-side candidates are dead: the glue channel-resource arm (H4), the
ETE-interrupt per-channel enables (H2), the SR producer-commit edge/phase variants (H3), and the
vendor's synchronous `out[5]` arm (BOOT B) all leave `out[0]` set. The SR engine also did not
re-fetch on any post-release commit, so the descriptor-side state is not what gates the accept.

Next work is **device-side**, not another register poke: either (a) a device-side trace
(JTAG/ROM-monitor) of `pcie_msg_handle`'s caller to see which check gate `0x818a8` sits behind, or
(b) reproduce the vendor's full runtime message-context/ETE/interrupt binding that routes the glue
ISR to the firmware dispatcher (`pcie_msg_init`/`pcie_ete_init`/`hcc_init` and the runtime
`pcie_intr_handle` glue status path), which a raw takeover does not stand up.

## 7. Artifacts

```
build/register-dumps/exp/20261002-092940-batch/   BOOT A #1 (FAIL: health race; 20x result.txt)
build/register-dumps/exp/20261002-093334-batch/   BOOT A #2 (PASS: 20x result.txt, report.txt)
build/register-dumps/exp/20261002-093920/         BOOT B #1 (false PASS: module never ran)
build/register-dumps/exp/20261002-094158/         BOOT B   (PASS; module-log.txt has the out5 run)
build/register-dumps/exp-logs/bootA-*.log         harness stdout (FAIL line + reason)
build/register-dumps/exp-logs/bootA2-*.log        harness stdout (PASS line + report table)
build/register-dumps/exp-logs/bootB-*.log         harness stdout (false PASS)
build/register-dumps/exp-logs/bootB2-*.log        harness stdout (PASS); pulled status file text
opensource/lab/hccaccept/hccaccept.c              module
opensource/lab/hccaccept/batch-bootA.list         the 20 BOOT A hypotheses
```

`/root/omo-hccaccept-bootB.txt` on the device holds the persistent BOOT B result (quoted in section
3); it is left in place (not a recovery leftover — the recovery only removes the harness paths).
