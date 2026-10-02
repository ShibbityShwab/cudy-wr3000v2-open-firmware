# sr-carrier: the H1 (ETE SR ring as the H2D carrier) experiment on real hardware - all four entries negative, the gate still holds (phase 22, 2026-10-02)

Task `st_01a0fc11`. Re-runs the queued H1 device experiment (`docs/phase22/fw-hostmem.md` section 6,
H1) in ONE takeover boot with the phase-22 batch harness, driven end-to-end by a **detached runner**
so no agent death can strand the router. Module: `lab/hccaccept/hccaccept.c` at
`omo/phase22-hccaccept` head `94dde01` (bothep bring-up + the harness batch convention + the
`h1b`/`h1c`/`h1d` hypotheses). Every claim below is **[proven]** (a value the runner, the device, or
the harness printed this session).

**Headline.**

- The detached runner completed the whole cycle unattended: staging phase (watchdog armed on the
  device **first**, then the vendor modules hidden), module + hypothesis-list staging, takeover boot,
  evidence capture, watchdog cancel, restore and health - `BATCH RESULT: PASS hypotheses=4
  out0_cleared_yes=0`, `wiphy=2/2 iface=6/6 cal_succ=1 omo_off=0 staged=0 loader=0`.
- **All four entries are negative.** `out[0]` (CA `0x40039010`) is never cleared by the device, there
  is no id-1 reply, the ack CA `0x400392f0` reads `0` at every entry, and `SR ch0 +0x1c` stays frozen
  at `0x400` across the whole series. The pre-release lap is the only SR consumption
  (`SR ch0 DEVICE INDEX 0x10 -> 0x400` at +550 ms post-release, reproduced) - the engine never
  consumes again post-release under any H1 candidate write.
- **H1 is dead in all three post-release forms**: (b) the SR ch0 program re-assert + the double-lap
  commit edge, (c) the DR `+0x30/+0x34` re-assert + mailbox ring (and the ack read), and (d) the
  exact 72-byte `shuangta_ete_sr_dscr_fill` node in SR slot 0 committed with the edge. The writes
  land (every read-back matches the value written) and change **nothing**.
- **Named next blocker.** The host side is exhausted: with `h2d-accept.md` (20 register hypotheses
  negative) + `fw-hostmem.md` (the dispatcher reads no host memory) + this run (the SR ring, the one
  shared host-memory structure, is inert), no host register, ring program, node payload, or mailbox
  write reaches the firmware's H2D dispatcher (file `0x818ac`). The remaining work is **device-side**:
  the dispatcher's runtime-installed *caller* (the firmware's internal message-service wake), i.e.
  firmware trace/JTAG or blob patching - not another host poke.
- The router ends **verified restored**: 2 wiphys, 6 interfaces, `[SUCC]` on both bands, 0 `.omo-off`,
  0 experiment init entries, 0 experiment modules loaded.

---

## 0. Module, build provenance, and the run shape

| item | value |
| --- | --- |
| module source | `lab/hccaccept/hccaccept.c` (`omo-bothep` bring-up + batch convention + `h1b`/`h1c`/`h1d`) |
| commit | `94dde014277ecf51023b929e3ef26cd0a55c0ee8` on `omo/phase22-hccaccept` (pushed, = `origin`) |
| CI build | run [`36992716765`](https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36992716765) `build-load-test-module`, `success`, headSha `94dde01` |
| artifact | `build/tmp/phase22/hccaccept-h1/hccaccept.ko`, 86464 B, md5 `0ed53d05e34ffeddd5b581dbe243820a` |
| artifact integrity | re-downloaded from the CI run and compared: **byte-identical** md5; `vermagic=5.10.201` = the device kernel |
| hypothesis list | `lab/hccaccept/batch-h1.list` (4 entries: `h1a-baseline`, `h1b-sr-reassert-edge`, `h1c-dr-reassert-mbox`, `h1d-sr72-edge-iso`) |
| runner | `tools/sr-carrier-detached.sh` -> `tools/run-exp-detached.sh` (staging phase) then `tools/batch.sh` (full cycle) |
| runner log | `build/register-dumps/detached/sr-carrier.log` |
| evidence | `build/register-dumps/exp/20261002-100556-batch/` |

The run shape is the mandated one: **no device cycle inside the agent's step-by-step loop**. The
runner is a single long-lived process; `nohup`'d from a 60 s tool window, it stayed alive past that
window and finished the boot/capture/recover/health sequence on its own (log timestamps
`17:05:54 -> 17:08:22`), and the device-side watchdogs were armed before anything was staged.

The module is staged and run exactly like the harness does it: `batch.sh` arms
`/root/recover-exp.sh --watch` on the device first, then copies the `.ko` to
`/lib/modules/5.10.201/`, installs a one-shot self-deleting `/etc/init.d/omo-hccaccept` +
`/etc/rc.d/S99omo-hccaccept` loader, and hands the module `batch=/root/omo-batch-list
batchdir=/tmp/omo-batch`. The `h1b/h1c/h1d` hypotheses and the extra per-entry observables are
commit `94dde01`; the rest of the module is the proven bothep boot-2 bring-up.

## 1. Runner PASS/FAIL lines (verbatim)

Staging phase (`tools/run-exp-detached.sh`), `build/register-dumps/detached/run-20261002-170554.log`:

```
=== run-exp-detached start 2026-10-02T17:05:54+07:00 ===
module=stage
reachable after 0 polls
WATCH_ARMED
2
=== STAGE COMPLETE - module staging from the caller happens here ===
```

`WATCH_ARMED` = `/root/rd-watch.sh` armed on the device **before** the vendor modules were hidden;
`2` = both `hi5622v100_{wifi,plat}.ko` moved to `.omo-off`. So a runner/agent death at any later
point is recovered by the device itself.

Full cycle (`tools/batch.sh`), `build/register-dumps/detached/sr-carrier.log`:

```
== batch: 4 hypotheses in one boot ==
-- [1/7] arm device-side self-recovery (mandatory)
-- [2/7] stage the module
-- [3/7] reboot and wait for SSH
  device back: uptime=30s
-- [4/7] wait for the completion marker
  completion marker seen
-- [5/7] capture the evidence
  evidence: /c/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/register-dumps/exp/20261002-100556-batch
-- [6/7] recover
-- [7/7] verify health
  wiphy=2/2 iface=6/6 cal_succ=1 omo_off=0 staged=0 loader=0

BATCH RESULT: PASS module=hccaccept params=[batch=/root/omo-batch-list batchdir=/tmp/omo-batch domain=0 domain2=1 program=1 outwin=1 devvabase=0x80000000 devvaend=0xffffffff hostcabase=0x80000000 release=1 useirq=1 irq=207 hostirq=207 useirq2=1 irq2=209 hostirq2=209 rings=1 acpoff=0 enable=0 intr=0 srctrl=0 seq=0 svc=0 scanlen=0 hccwin=1500 stopfirst=0] evidence=.../exp/20261002-100556-batch/
BATCH RESULT: PASS hypotheses=4 out0_cleared_yes=0 evidence=.../exp/20261002-100556-batch/
=== phase 2 exit=0 2026-10-02T17:08:22+07:00 ===
=== sr-carrier detached runner end 2026-10-02T17:08:22+07:00 ===
```

`stopfirst=0` is deliberate: every entry runs and is recorded, so the "stop the series at the first
change" rule is an observation, not a skip. **No entry met a stop condition** (`out0_cleared=y`,
`id1!=0`, or a new fetch), which is why all four ran and all four are reported.

## 2. Winning configuration is live, and the pre-release lap is the only SR consumption

`module-log.txt`, decisive lines (this run):

```
[postdr +...] DR ch3 DEVICE INDEX 0x00000000 -> 0x00000010 (host wptr=0x00000400, delta=16) = pcie_rx_handle saw a completion
[postdr +...] DR ch4 DEVICE INDEX 0x00000000 -> 0x00000010 ...
[postdr +...] DR ch5 DEVICE INDEX 0x00000000 -> 0x00000010 ...
[postdr +...] DR ch6 DEVICE INDEX 0x00000000 -> 0x00000010 ...
[post0 +550ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 (host wptr=0x00000400) = SR engine read
base recorded: ETE intr 0x40039508=0x3f201818 glue 0x400392e8=0x00000020 sr_ctrl=00000000/00000000/00000000
```

So the ep0-primary / both-RC / irq-209 configuration of `docs/phase21/both-eps.md` is reproduced: the
engine reads the SR descriptors **before** the `0x5a5a` release and never again, and the DR
completion path runs.

## 3. Per-entry observables

`hccwin=1500` ms per entry; before each entry the module restores the post-release base state
(`omo_hyp_base_reset()`: ETE interrupt block, glue `chn_res`, `out[0]`/`out[1]` = 0, SR
base/depth/ctrl, a fresh node refill, `wptr := rptr` with no commit, DR re-post) - i.e. the ring is
reset between attempts.

Columns: `SR+0x10` = SR ch0 ring base; `SR+0x18` = SR ch0 producer (final in the entry window);
`SR+0x1c` = **SR ch0 device index**; `DR+0x3c` = DR ch3 device index; `out[0]` = CA `0x40039010` at
the end of the window; `ack` = CA `0x400392f0` read-back; `glue` = status CA `0x400392ec & 0x3d8`;
`irq` = cumulative irq-209 count; `id1` = id-1 bit observed in `out[1]`; `fetched` = did `SR+0x1c`
move during the window.

| # | entry | host action | SR+0x10 | SR+0x18 | SR+0x1c | DR ch3 idx | out[0] | ack 0x400392f0 | glue | irq | id1 | fetched |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `h1a-baseline` | (a) control: refill + full-lap commit + id-3 doorbell, nothing else | `0x83a9e000` | `0x00000000` | `0x00000400` | `0x00000010` | `0x00000008` | `0x00000000` | `0x00000018` | 56586 | 0 | 0 |
| 2 | `h1b-sr-reassert-edge` | (b) SR ch0 `+0x10/+0x14/+0x08` re-assert, then `+0x18 <= 0x410` then `<= 0x000`; mailbox untouched | `0x83a9e000` | `0x00000000` | `0x00000400` | `0x00000010` | `0x00000000` | `0x00000000` | `0x00000018` | 56586 | 0 | 0 |
| 3 | `h1c-dr-reassert-mbox` | (c) DR `+0x30/+0x34` re-assert on all 4 DR channels, then `out[0] <= 0x08`, `out[2] \|= 1`, read ack | `0x83a9e000` | `0x00000400` | `0x00000400` | `0x00000010` | `0x00000008` | `0x00000000` | `0x00000018` | 56586 | 0 | 0 |
| 4 | `h1d-sr72-edge-iso` | (d) 72-byte SR payload in node 0 (`word0` = coherent buffer devva, `word1 = (72<<16)\|0x6d2b`) + commit edge; mailbox untouched | `0x83a9e000` | `0x00000000` | `0x00000400` | `0x00000010` | `0x00000000` | `0x00000000` | `0x00000018` | 56586 | 0 | 0 |

Per-entry raw module obs lines (`tmp/omo-batch/<NN>-<label>/obs.txt`):

```
label=h1a-baseline out0=0x00000008 ack392f0=0x00000000 sr10=0x83a9e000 sr18=0x00000000 sr1c=0x00000400 dr3_devidx=0x00000010 glue=0x00000018 irq=56586 id1=0 fetched=0
label=h1b-sr-reassert-edge out0=0x00000000 ack392f0=0x00000000 sr10=0x83a9e000 sr18=0x00000000 sr1c=0x00000400 dr3_devidx=0x00000010 glue=0x00000018 irq=56586 id1=0 fetched=0
label=h1c-dr-reassert-mbox out0=0x00000008 ack392f0=0x00000000 sr10=0x83a9e000 sr18=0x00000400 sr1c=0x00000400 dr3_devidx=0x00000010 glue=0x00000018 irq=56586 id1=0 fetched=0
label=h1d-sr72-edge-iso out0=0x00000000 ack392f0=0x00000000 sr10=0x83a9e000 sr18=0x00000000 sr1c=0x00000400 dr3_devidx=0x00000010 glue=0x00000018 irq=56586 id1=0 fetched=0
```

### 3.1 Every candidate write landed (read-backs), and nothing answered

```
[hyp h1b] SR ch0 re-assert +0x10<=0x83a9e000 +0x14<=0x0000001f +0x08<=0x00000000 (readback 0x83a9e000/0x0000001f/0x00000000)
[hyp h1b] SR ch0 +0x18 <= 0x00000410 readback=0x00000410
[hyp h1b] SR ch0 +0x18 <= 0x00000000 readback=0x00000000 rptr(+0x1c)=0x00000400 (double-lap edge, no out[0] write)
[hyp h1c] DR ch3 +0x30 0x83a9b000 -> 0x83a9b000 +0x34 0x0000001f -> 0x0000001f (readback 0x83a9b000/0x0000001f, d2h_notify ring)
[hyp h1c] DR ch4 +0x30 0x83a99000 -> 0x83a99000 +0x34 0x0000001f -> 0x0000001f (readback 0x83a99000/0x0000001f, d2h_notify ring)
[hyp h1c] DR ch5 +0x30 0x823f4000 -> 0x823f4000 +0x34 0x0000001f -> 0x0000001f (readback 0x823f4000/0x0000001f, d2h_notify ring)
[hyp h1c] DR ch6 +0x30 0x83a8c000 -> 0x83a8c000 +0x34 0x0000001f -> 0x0000001f (readback 0x83a8c000/0x0000001f, d2h_notify ring)
[hyp h1c] ack 0x400392f0 pre=0x00000000 out[0] pre=0x00000000
[hyp h1c] out[0] <= 0x00000008 doorbell out[2] |= 1; ack 0x400392f0 readback=0x00000000
[hyp h1d] SR ch0 node[0] word0=0x84c28000 word1=0x00486d2b (72-byte id-1 frame, sr_dscr_fill @0x17858)
[hyp h1d] SR ch0 +0x18 <= 0x410 then 0x000 (commit edge, NO out[0] write - ring isolated from the mailbox) readback=0x00000000 rptr=0x00000400
omo-hccaccept: done (batch entries=4 stopped=0)
```

`0x00486d2b` is exactly `(72<<16) | 0x6d2b`, so the node `word1` matches `shuangta_ete_sr_dscr_fill`
@`0x17858` bit-for-bit.

## 4. Confirm / kill verdicts

The H1 confirm set (`fw-hostmem.md` section 6) is `out[0]` transitions nonzero->zero with **no host
write**, or an id-1 bit in `out[1]`, or `0x400392f0` reads nonzero; the kill is `SR+0x1c` advancing
with nothing else changing.

| entry | confirm | kill | verdict |
| --- | --- | --- | --- |
| (a) `h1a-baseline` | none - `out[0]` stays `0x08` (the module's own id-3 doorbell), ack `0` | - | control negative, identical to BOOT A of `h2d-accept.md` |
| (b) `h1b-sr-reassert-edge` | none - `out[0]` never written, ack `0`, no id-1 | `SR+0x1c` does **not** advance (stays `0x400`) | **negative and inert**: the post-release SR program + double-lap edge is written and read back exactly, but cannot re-trigger the engine |
| (c) `h1c-dr-reassert-mbox` | none - `out[0] <= 0x08` stays `0x08`, ack reads `0` | - | **negative**: the DR `+0x30/+0x34` re-assert is a no-op (the registers already held the quoted values) and the mailbox ring is not answered |
| (d) `h1d-sr72-edge-iso` | none - `out[0] = 0`, ack `0`, no id-1 | `SR+0x1c` does **not** advance (stays `0x400`) | **negative**: the vendor-shaped 72-byte SR payload committed with the edge is not consumed |

Two further readings worth recording:

- **`irq` is flat at `56586` across all four 1.5 s windows.** After the pre-release lap the
  completion interrupt stops entirely - the batch produced no interrupt activity at all, matching the
  "fetch and interrupt only in the pre-release lap" picture from `both-eps.md`.
- **`glue` is `0x00000018` (mask `0x3d8`) for every entry**, whereas `h2d-accept.md` BOOT A recorded
  `0x00000000`. This run therefore had the glue status bits 3-4 set for the whole series; they were
  not affected by any entry (constant across control and all three candidates), so it is a
  device-state difference between boots, not an effect of the hypotheses. Reported as observed; no
  interpretation claimed.

## 5. Reproduction

This run reproduces the previous H1 run (`build/register-dumps/exp/20261002-095920-batch`, module
md5 identical) observable-for-observable - same `out0_cleared=n` on all entries, `SR+0x1c` frozen at
`0x400`, `ack=0`, `id1=0`, `glue=0x18`; only the DMA addresses (`sr10 0x83af9000` vs `0x83a9e000`)
and the interrupt counter base (`56366` vs `56586`) differ, as expected for a fresh boot. Both runs
passed the harness.

## 6. Next blocker (named)

With H1 dead, every host-reachable lever named across the phase-22 static lanes is now measured
negative: 20 register hypotheses (`h2d-accept.md`), the "firmware reads the host message context"
story killed by static proof (`fw-hostmem.md` section 4), and now the SR ring / DR ring / node
payload - the only host memory the device reads - driven in every post-release form with zero effect.

**Next blocker: the firmware's internal wake of the H2D dispatcher (file `0x818ac`).** The dispatcher
reads only device registers and its device-RAM handler table, has no static reference anywhere in
the blob, and is installed at runtime (most plausibly by the framework installer at file `0x874b0`,
`fw-hostmem.md` section 3.4); its caller is device-internal. No host-writable register, ring
program, descriptor payload, or mailbox write reaches it. The next step is **device-side**: firmware
tracing/JTAG on the ETE-receive / message-service entry, or a blob patch that calls `0x818ac`
directly - not another host poke.

## 7. Restore and health verification

The runner cancelled the watchdog and restored (`-- [6/7] recover` -> `-- [7/7] verify health
wiphy=2/2 iface=6/6 cal_succ=1 omo_off=0 staged=0 loader=0`), and a separate manual probe on the
recovered boot confirms both bands and a clean tree:

```
WIPHY=2  IFACE=6  OMOOFF=0  2G=[SUCC]  5G=[SUCC]  VENDOR_MODULES=2
lsmod: no hccaccept / bothep entry
/etc/init.d, /etc/rc.d: no experiment loader (only the stock omosshd)
```

`/root/rd-watch.sh` (the staging-phase watchdog file, whose process died at the takeover reboot) was
removed after the health check; `/root/recover-exp.sh`, `/root/omo-batch-list` and the staged module
were removed by the harness recovery itself. **The router ended healthy and unstaged.**

## 8. Artifacts

| artifact | path |
| --- | --- |
| runner log (staging + full cycle + PASS lines) | `build/register-dumps/detached/sr-carrier.log` |
| staging-phase log | `build/register-dumps/detached/run-20261002-170554.log` |
| evidence dir (dmesg, lsmod, interrupts, module-log, report, per-entry results) | `build/register-dumps/exp/20261002-100556-batch/` |
| per-entry observables | `.../tmp/omo-batch/<NN>-<label>/{obs.txt,result.txt}` |
| batch report | `.../report.txt` |
| runner script | `tools/sr-carrier-detached.sh` |
| module + list | `lab/hccaccept/hccaccept.c`, `lab/hccaccept/batch-h1.list` @ `94dde01` |

Scope: the module and the report are in the repo (`opensource`); the runner scripts and the evidence
live outside it (`router-openwrt/tools/`, `router-openwrt/build/register-dumps/`). CA `0x400392f0`
was read, never written; the RC misc window `0x10161000` was never read.
