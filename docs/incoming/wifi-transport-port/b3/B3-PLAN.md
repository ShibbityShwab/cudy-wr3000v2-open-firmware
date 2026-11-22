# B3-PLAN — attended bench run: ETE rings, D2H service, HCC group-4 ready gate (P0-P5)

**Lane:** b3-instrument (task-66, host-only). **Written:** 2026-11-22.
**Status:** PLAN ONLY — this lane never touched the device. The run below is the Lead's
attended session, executed under [../../../../../BENCH-FACTS.md], [../../../../../RUNBOOK.md],
[../../../../../LESSONS.md]. The newest dated ledger entry and STATUS.md win over this document.

**Instrument:** `hi5622_pciet_b3.ko` from this directory.
sha256 `0a25d44849e9216b710b94e6df686d150551be804260f2bafb854a3da580718b`,
vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`. Build line in [BUILD-NOTES.md](BUILD-NOTES.md).
It is a full copy of the clean-room transport skeleton plus the B3 additions, built as its own
module (driver name `hi5622_pciet_b3`) so it can never collide with another lane's
`hi5622_pciet.ko`: **load only this module**; never both.

Contract: `opensource/docs/incoming/wifi-hcc-re/REPORT.md` §4 (ETE), §5.1/5.2/5.4 (mailbox
sequence), §8 (driver contract), §9 (probes P0-P5) + `SUMMARY.md`.

---

## 0. What this run is for

B1/B2 proved: the six-region iATU map, the contiguous 928,920 B upload with diffs=0, the
`0x5a5a` CPU release, the `0xcece` D2H gate, the platform-up mailbox word `out[1]=0x4`, and
SR0/DR0 ring programming + an H2D smoke. What is **not** proven is the actual D2H data path:
whether the device ever deposits on the ETE DR rings, which channel carries the chip-group-4
ready messages, and whether the real ready gate (HCC group-4 ids 1 and 2) arrives. B3 answers
exactly those with reads, the already-proven §5.4 ack/drain/re-arm writes, and the contract
ring-arm writes. **B3 sends no H2D message.** §2.4/§5.5: no host reply is required.

Success for this session = P3 shows `g4_id1_seen=1` **and** `g4_id2_seen=1` (the two vendor
init waits released by the device's own payloads), with P2 pinning the DR channel that carried
them.

---

## 1. Preconditions (all must be true BEFORE step 0; no exceptions)

| # | gate | how | pass criterion |
| --- | --- | --- | --- |
| G1 | recovery door armed | `tools/recovery-card.sh` | READY (physical power-off + RESET-hold + TFTP card, human at bench) |
| G2 | bench link | `tools/bench-link.sh` | PASS |
| G3 | UART capture alive | `tools/uart-capture.sh` to an absolute path | read-only capture; **no** tty writes, no screen/minicom/picocom, no read-write tty open |
| G4 | image + module present | sha256 of the 6.18 image per STATUS.md; `modinfo hi5622_pciet_b3.ko` | vermagic `6.18.60 SMP mod_unload ARMv7 p2v8` |
| G5 | no vendor wifi modules | `lsmod` | `hi5622v100_plat` / `hi5622v100_wifi` absent |
| G6 | firmware blob | `sha256sum /lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20…` (local, never committed) |
| G7 | DTS dma-ranges in the image | image W26a or later (RC inbound window) | P0 enforces it in code: it refuses to arm if ring DMA addresses fall outside `0x80000000..0x8fffffff` |
| G8 | only this module | `lsmod` after step 0 | `hi5622_pciet_b3` present, `hi5622_pciet` absent, **no base poll thread** (`poll_ms=0`) |

---

## 2. Hard rules (restated because this run writes ETE ring registers)

- **NEVER write CA `0x400392f0`** (out[5], H2D ack / "copy A"). The module has build-time
  asserts plus a runtime gate on every writer; P4 only **reads** it.
- **NEVER read the RC misc window `0x10161000`.** The module never touches it.
- **UART is READ-ONLY.**
- Every device write is one explicit sysfs write, logged to dmesg, in the order below. Capture
  dmesg (and UART) between steps. No troubleshooting by extra writes.
- **One idea per session.** Any abort, SError, hang, or unknown device state -> stop, capture,
  power-cycle (G1), record. Power-cycle is the only reset after B1.4.
- Do **not** `rmmod` after step 5 (release); power-cycle instead.
- Do **not** interrupt the blocking `echo 30 > b3_p1_watch` store — let it finish (returns in
  <= 30 s, worst case 40 s).
- Use endpoint `0000:00:00.0` for every attribute below (the same attrs exist on
  `0001:00:00.0`; do not mix the two).

---

## 3. Addresses B3 touches (cross-check the dmesg against this)

| item | device CA | BAR0 offset | B3 access |
| --- | --- | --- | --- |
| out[1] D2H bitmap | `0x40039014` | `0x3f1014` | read + **write 0** (drain, §5.4 [V]) |
| out[3] D2H ack | `0x40101438` | `0x4b9438` | **write 1** (ack, §5.4 [V]) |
| out[4] D2H re-arm | `0x40101414` | `0x4b9414` | **write 1** (re-arm, §5.4 [V]) |
| out[0] H2D pending | `0x40039010` | `0x3f1010` | read only (P4) |
| out[2] H2D doorbell | `0x400392d4` | `0x3f12d4` | read only (P4) |
| out[5] | `0x400392f0` | `0x3f12f0` | **read only — NEVER written** (P4) |
| D2H doorbell | `0x40101434` | `0x4b9434` | read only (P4) |
| ETE block | `0x4003a000` | `0x3f2000` | ring registers (P0), readback (P4) |
| ETE intr word | `0x40039508` | `0x3f1508` | read only (P4) |
| SR0/SR1/SR2 group A | block `+0x400/+0x450/+0x4a0` | — | write base `+0x10`, depth-1 `+0x14`, producer `+0x18`, ctrl `+0x08`; read `+0x1c` |
| DR0..DR3 group B | block `+0x590/+0x5e0/+0x630/+0x680` | — | write base `+0x30`, depth-1 `+0x34`, published free idx `+0x38`; read fill `+0x3c`, enable `+0x00` |

Firmware-owned registers B3 **never writes**: channel enable `+0x00`, DR ctrl `+0x08`,
SR consumer `+0x1c`, DR fill `+0x3c`. All are only read back.

---

## 4. Sysfs surface (`/sys/bus/pci/devices/0000:00:00.0/`)

| attribute | mode | purpose |
| --- | --- | --- |
| `b3_p0_ringinit` | WO | **P0**: arm SR0-2 + DR0-3, pre-post DR buffers. Write `1`. Single-shot per module load (second write -> `-EBUSY`). |
| `b3_p1_watch` | WO | **P1**: blocking watch window. Write `N` (seconds, 1..120) -> drains + logs every `out[1]` for N s, runs the DR fill watchdog. |
| `b3_p1_events` | RO | **P1**: raw `out[1]`, per-bit counters, bit3/bit6/bit2 callouts, last 16 bitmap words. |
| `b3_drain` | WO | one-shot contract service (ack/drain/re-arm + dispatch); write `1`. Used to clear the `0x4` announce. |
| `b3_p2_drsnapshot` | WO | **P2**: read-only snapshot of all four DR blocks (`+0x3c/+0x38/+0x30/+0x34/+0x00`) + up to 8 decoded nodes. Write `1`; no consume, no writes. |
| `b3_p2_result` | RO | **P2**: last snapshot text (auto after every id3/6/7 drain, or manual). |
| `b3_p3_hcc` | RO | **P3**: HCC group-4 id1/id2 capture, payload length check (<=0x63), payload bytes, group counts. |
| `b3_p4_state` | RO | **P4**: out[0]/out[1]/out[2]/out[5] (read-only), doorbell, ETE intr, boot flags, per-channel enable + indices. |
| `b3_p5_nodedump` | WO | **P5**: dump node `word1` + first 16 B per completed buffer. Write `1`. |
| `b3_p5_result` | RO | **P5**: last node dump text. |
| `b3_rings` | RO | arm state; per-channel base/depth/prod/consumer/enable/pub/fill as currently read. |
| `b3_clear` | WO | reset event counters (rings stay armed, fill baselines kept). Write `1`. |

Base (B1/B2) attrs remain available and unchanged: `prepare`, `upload`, `verify`, `release`,
`announce`, `out1`, `ready1`, `ready2`, `status`, `d2h_count`, `fw_loaded`, `cpu_released`,
`gate_announced`, `windows`. **Do not use** the base `poll` / `ringinit` / `send` attrs in a B3
run (they are B1/B2 tools; B3 owns its own rings and the full D2H sequence).

---

## 5. The exact sequence

Run the steps in this order. After each write: `dmesg | tail -40` and, if armed, the UART log.
Only the commands below — nothing else.

### Step 0 — load the instrument (no device writes)

    insmod hi5622_pciet_b3.ko poll_ms=0

Expected dmesg:
- two probe lines `hi5622_pciet_b3: endpoint 0000:00:00.0 BAR0 … no device writes performed`
  and the same for `0001:00:00.0`;
- 3x `ETE ring @ chan 0x400/0x450/0x4a0` + 4x `ETE ring @ chan 0x590/0x5e0/0x630/0x680`
  (host-DRAM allocations only);
- `B3 instrument ready: 3 SR + 4 DR rings allocated in host DRAM (no device writes);
  probes P0-P5 via b3_* sysfs attrs`.

Gate: no abort/SError/hang; `lsmod` shows only `hi5622_pciet_b3`; `dmesg` must **not** show
`D2H poll thread started` (if it does, the module was loaded with `poll_ms` != 0 — unload and
reload with `poll_ms=0`, still safe because the CPU is not released).

### Step 1 — B1.1 prepare (endpoint iATU six viewports)

    echo 1 > prepare

Expected: 6x `iatu vpN: … readback=OK`; `cat windows` -> `1`.
This step is mandatory **before P0**: the endpoint iATU must decode BAR0 region 3 before the
ETE block is reachable for writes.

### Step 2 — B1.2 upload

    echo 1 > upload

Expected: `upload hi_wifi/FIRMWARE.bin: 928920 bytes -> BAR0+0x6f8000 (CA 0x01240000)`,
`download finished: 928920 bytes in 2 chunks`; `cat fw_loaded` -> `1`.

### Step 3 — B1.3 verify

    echo 1 > verify

Expected: `firmware verify target BAR0+0x6f8000: file=928920 bytes diffs=0 match=YES`.
Gate: `diffs=0`. Anything else -> STOP (do not release).

### Step 4 — **P0** arm all rings (before CPU release)

    echo 1 > b3_p0_ringinit

Rationale: the vendor arms ETE at probe, **before** the firmware is uploaded/released; arming
first means the DR rings are already valid when the firmware posts its first group-4 ready
message. Writing ring registers while the CPU is held in reset is inert but safe (BAR0 is
accessible — steps 2/3 just proved it).

Expected dmesg (7 lines + summary; record every line):

    B3 P0: arming SR0-2 (group A) + DR0-3 (group B), depth 32 (depth-1=0x1f), pre-post 32 DR free buffer(s)/channel
    B3 P0: SR0 ch=0x400 base=0x8xxxxxxx depth-1=0x1f prod=0x00000000 ctrl=0x00000000 readback=OK (enable=0x00000000 consumer=0x00000000, read-only)
    B3 P0: SR1 ch=0x450 … readback=OK
    B3 P0: SR2 ch=0x4a0 … readback=OK
    B3 P0: DR0 ch=0x590 base=0x8xxxxxxx depth-1=0x1f pub(free)=0x00000400 fill(read-only)=0x00000000 enable=0x00000000 readback=OK
    B3 P0: DR1 ch=0x5e0 … readback=OK
    B3 P0: DR2 ch=0x630 … readback=OK
    B3 P0: DR3 ch=0x680 … readback=OK
    B3 P0: armed 7 channels; DR published index pre-post: packed (idx&0x3ff)|phase<<10 = 0x00000400 for 32 buffers; firmware-owned enable bits left untouched (P4 reads them)

Checkpoints: `cat b3_rings` -> every SR/DR base in `0x8xxxxxxx`, `depth-1=0x1f`, SR prod=0,
DR `pub(+0x38)=0x00000400`, `fill(+0x3c)=0x00000000`.
Gates (all must hold): 7/7 `readback=OK`; every base in `0x80000000..0x8fffffff`;
`pub=0x00000400` for `prepost=32` (see §11 for the alternate convention).
On failure: any `MISMATCH` or `outside the RC inbound window` -> **STOP, do not release.**
While the CPU is still in reset `rmmod` + re-`insmod` + repeat steps 1-4 is physically safe;
prefer a power cycle and record the failure as the session's finding.

### Step 5 — B1.4 release (0x5a5a)

    echo 1 > release

Expected: `CPU release: 0x5a5a -> CA 0x40000108`, `cpu_released=1`.
**From here on `rmmod` is unsafe; power-cycle is the reset.**

### Step 6 — B1.5 announce gate (0xcece)

    echo 1 > announce

Expected: `D2H announce gate: 0xcece -> CA 0x4000010c`, `gate_announced=1`.
The firmware leaves its spin as soon as this word lands; the UART may show firmware output.
Record the wall-clock time of this write — the ready payloads follow within ~1-20 s.

### Step 7 — **P4a** read-only post-release state (before draining)

    sleep 1
    cat b3_p4_state
    cat status

Expected (record all values; no gate yet):
- `out[0] H2D pending=0x00000000`
- `out[1] D2H bitmap=0x00000000` or `0x00000004` (the platform-up announce)
- `out[2] H2D doorbell=0x00000000`
- `out[5] = 0x… (READ-ONLY; expected 0 or a device-written value — never write it)
- `boot: fw_loaded=1 cpu_released=1 gate_announced=1 windows=1 base_poll_thread=0`
- `rings armed=1 prepost=32`
- per SR/DR `enable(+0x00)` (firmware-owned), DR `pub(+0x38)`, `fill(+0x3c)`

Interpretation: the enable bits are the firmware's. Right after the announce they may still be
0 (firmware has not reached its D2H path) or already 1 (re-asserted). Record them — step 12
compares.

### Step 8 — drain the `0x4` announce (P1 precondition)

    echo 1 > b3_drain

If `out[1]` reads 0: wait 1 s and repeat (maximum 3 attempts total). The announce is posted
soon after the `0xcece` gate; if after 3 attempts `out[1]` is still 0 -> **STOP** and record
(step 7 shows the device state; do not poke further).

Expected on success:

    B3 D2H[manual-drain]: out[1]=0x00000004 (ack out[3]=1, out[1]=0 drain, rearm out[4]=1)
    B3 D2H: id2 = PLATFORM-UP ANNOUNCE (firmware out[1]=0x4, the B1 checkpoint); no vendor handler, drain only; NOT the ready gate (P3 owns that)
    B3 drain: one-shot service done: raw out[1]=0x00000004, 1 bit(s) dispatched, out[1] now=0x00000000

If the ready payloads already arrived in the same word, this step shows more bits (e.g. `0x44`
= bits 2+6, `0x48` = bits 2+3) and performs the DR drain + P3 decode immediately; then read
`cat b3_p2_result` and `cat b3_p3_hcc` right away (steps 10/11 evidence may already be there).

### Step 9 — **P1** watch window (30 s)

    echo 30 > b3_p1_watch      # BLOCKS up to 30 s; do not interrupt

Expected dmesg during the window (one or more of the first three, then the drain lines):

    B3 P1: watch window 30 s started (out[1] drained every 25 ms; bits 3 and 6 are the ETE events)
    B3 D2H[P1-watch]: out[1]=0x00000008 … id3 = ETE transfer done -> draining DR0-3 (P2)
    B3 D2H[P1-watch]: out[1]=0x00000040 … id6 = ETE sending trigger (d2h_notify 6) -> draining DR0-3 (P1 bit6)
    B3 D2H[P1-watch]: out[1]=0x00000080 … id7 = ETE sending trigger variant -> draining DR0-3
    B3 DRx: fill=0x… last=0x… -> N completed node(s) (pub=0x… enable=0x…)
    B3 RX drN[k]: node len=… hdr{g=4 rg=… total=… id=1|2 dst=… tag=0x5a5a} tag=OK
    B3 P1: watch window finished after N passes: events=… out[1]=0x00000000 bit3=… bit6=… bit2=…

Also watch for the fill watchdog line (a finding, not a failure):

    B3 P1: DRx fill(+0x3c) advanced 0x… -> 0x… WITHOUT a mailbox word; draining (deposit-without-notify finding) and returning the buffers

Checkpoint: `cat b3_p1_events` -> `events>=2`, `b2>=1`, and `b3` and/or `b6` non-zero.
Gate: **any bit3 or bit6 event = the ETE D2H path is alive (P1 pass).** If after the full
window only bit2 was ever seen (b3=0, b6=0) and P2 shows `fill=0` on all four DR channels ->
the publish side was not reached: record, go to step 10/12 for the state, **STOP** (finding,
not a license to poke). A second 30 s window is allowed once if the first window caught only
the announce and the UART shows the device was still booting (maximum two windows).

### Step 10 — **P2** DR snapshot (read-only)

    cat b3_p2_result          # auto snapshot from any id3/6/7 drain or fill-watchdog event
    echo 1 > b3_p2_drsnapshot # fresh read-only snapshot (no consume, no writes)
    cat b3_p2_result

Expected content:

    B3-P2 DR snapshot reason=… armed=1 prepost=32 d2h_events=… hcc_rx=… dr_rx=…
    dr0 base=0x… depth-1=0x1f pub(+0x38)=0x… fill(+0x3c)=0x… enable=0x… ctrl=0x.. from=0x… new=N
      [00] w0=0x… w1=0x… len=.. hdr g=4 rg=0 total=.. id=1 dst=0 tag=0x5a5a
      …
    dr1 … dr2 … dr3 …

Decision value: whichever channel shows `new>0` and `hdr g=4 id1/2` pins **which DR channel
carries the chip-group messages** — contract prediction [I]: **DR0** (the only DR channel with
flags word = 1, REPORT §4.1). If all four channels print `fill=0x00000000` -> go to P5 (step 13).
Read-only: the snapshot never consumes nodes and writes nothing.

### Step 11 — **P3** the real ready gate

    cat b3_p3_hcc

Expected:

    P3 ready gate: g4_id1_seen=1 g4_id2_seen=1 (the vendor's 200/2000-jiffy waits)
    g4_id1 device_plat_ready: len=.. total=.. first=..
    g4_id2 host_ready: payload=N B (<=0x63: OK) total=.. len=.. payload_first=..
    no H2D reply is required or sent by B3

Gate: `g4_id1_seen=1` **and** `g4_id2_seen=1` = **B3 success** (the device completed DEVICE READY
on its own; no host message needed). An id2 `payload > 0x63` is a contract violation: record and
dead-stop (send nothing). Base `ready1/ready2` attrs are mailbox-level only (B1 semantics) and
may stay 0 — they are **not** the B3 gate.

### Step 12 — **P4b** re-read the state after the events

    cat b3_p4_state

Expected: `out[0]=0x00000000` (no H2D pending); the DR channel(s) that delivered show
`enable(+0x00)=0x00000001` (firmware re-asserted it, contract P1 [V]); `fill`/`pub` advanced
consistently (host returned the buffers); `out[5]` unchanged by the host (read-only).
If a channel delivered while its `enable` still reads 0, record it (firmware enable timing is
[U]) — not a stop by itself.

### Step 13 — **P5** node dump (only if needed)

Condition: bits 3/6 were seen but P2 shows completed nodes with empty/zero words (`w0/w1=0`,
`len=0`) or no HCC header.

    echo 1 > b3_p5_nodedump
    cat b3_p5_result

Expected: per DR: `fill/pub/enable/from/new` + per node `w0/w1/flag/len buf=<16 hex bytes>`.
Decision: all-zero node words => the deposit/index convention is not what P0 published (record;
do not re-arm in the same session — see §11); valid `w1/len` but garbage header => wrong buffer
window. Either way: finding + stop.

### Step 14 — capture and teardown

- capture dmesg + UART to absolute files under `build/` ;
- save the read-only attrs: `out1 status b3_rings b3_p1_events b3_p2_result b3_p3_hcc b3_p4_state b3_p5_result`;
- stop the UART capture; **power-cycle** (do not `rmmod` after release);
- `tools/bench-teardown.sh --apply`; ledger entry with the results and every gate outcome;
- record: image sha, module sha256
  `0a25d44849e9216b710b94e6df686d150551be804260f2bafb854a3da580718b`, exact commands used.

---

## 6. Expected values at a glance

| checkpoint | expected |
| --- | --- |
| P0 SR/DR bases | `0x80000000..0x8fffffff` (the RC inbound window), depth-1 `0x1f` |
| P0 DR published free index | packed `0x00000400` for 32 pre-posted buffers |
| P0 DR fill at arm | `0x00000000` (baselined if nonzero) |
| announce word | `out[1]=0x00000004` (bit2), drained to 0 |
| P1 ETE events | bit3 `0x8` (ETE done) and/or bit6 `0x40` (d2h_notify) |
| P2 DR channel with payload | prediction DR0; flags word `0xd2b` (word1 low 13 bits) |
| P3 group4 id1 | `device_plat_ready_msg_process`, `hdr[1]>>4 == 0` |
| P3 group4 id2 | `host_ready_msg_process`, payload `<= 0x63` B |
| P4 | `out[0]=0`, used DR channel `+0x00=1` (firmware-owned) |

---

## 7. Stop rules (hard)

1. Any external abort / SError / stall / UART panic -> stop, capture, power-cycle.
2. P0: any `readback=MISMATCH` or `outside the RC inbound window` -> stop before release.
3. Step 3 `diffs != 0` -> stop before release.
4. Step 8: `out[1]` still 0 after 3 drain attempts -> stop (record state).
5. Step 9: no bit3/bit6 after the window(s) -> stop (record P2/P4; a finding).
6. Any `B3 DRx: implausible completion count … - NOT consumed; device state unknown, stop`
   line -> **stop immediately** (the module already refused to touch the ring).
7. P3 id1 without id2 -> handshake incomplete: stop, no writes, record.
8. Never write `0x400392f0`, never read `0x10161000`, never type to UART.
9. Never `rmmod` after release; never load the main transport module or vendor modules in the
   same boot; never run the base `ringinit`/`send`/`poll` attrs in a B3 run.
10. No retries with different register values/writes in the same session. A different idea
    (e.g. `prepost=31`) is a **new attended session**.

---

## 8. What each probe proves

| probe | command | proves |
| --- | --- | --- |
| **P0** | `echo 1 > b3_p0_ringinit` | the ETE ring shape of the vendor: 7 channels at the decoded offsets, depth 32 (depth-1=0x1f), host-DRAM bases in the 0x8xxxxxxx DMA window, packed producer/published indices, and that the host can program the host-owned fields without touching firmware-owned ones. Prerequisite for every D2H payload. |
| **P1** | `echo 30 > b3_p1_watch` + `cat b3_p1_events` | the mailbox D2H path is alive after the announce: the device's own notification ids (bit3 = ETE transfer done, bit6 = d2h_notify after a DR deposit) arrive and are serviced with the contract ack/drain/re-arm order; the per-bit counters are the transition record. Also catches deposits that arrive without a notify (fill watchdog). |
| **P2** | `b3_p2_drsnapshot` / `b3_p2_result` | which ETE DR channel actually carries device->host payloads and in what node/flag shape (pins REPORT §4.1 [I]: DR0, flags 1); reads the device-owned +0x3c fill index and host-owned +0x38 published index side by side. |
| **P3** | `cat b3_p3_hcc` | the real post-DEVICE-READY handshake: HCC chip-group-4 id1 (device_plat_ready) then id2 (host_ready, payload <= 0x63 B) delivered over the DR ring — i.e. the vendor's 200/2000-jiffy init waits are released by the device's own messages and **no host reply is needed**. This is the B3 success gate. |
| **P4** | `cat b3_p4_state` | the post-release precondition set: no H2D pending (out[0]=0), firmware re-asserted the DR channel enable it owns (+0x00=1), out[5] readable but never writeable from the host, and the boot/arm flags are what the script believes. |
| **P5** | `b3_p5_nodedump` | if deposits happen but decode fails: distinguishes "no deposit" from "wrong node indexing/buffer window" by dumping node word1 + the first 16 B of each buffer. |

---

## 9. Fail-path decisions (without extra writes)

| symptom | first check | decision |
| --- | --- | --- |
| P0 MISMATCH / EIO | dmesg lines | stop before release; power-cycle/reload allowed while CPU in reset; record |
| no `0x4` after announce | `b3_p4_state` (out[0], gate_announced, DR enables) | retry `b3_drain` up to 3x; then stop |
| bit2 only, b3=b6=0 | `b3_p2_result` fill on all DR | if fill=0 -> publish side not reached; record, stop |
| fill advanced, no mailbox bit | dmesg (watchdog line) | finding logged; buffers already returned; continue P2 |
| DR fill=0 but bits fire | `b3_p4_state` enables | record; stop (does not prove no deposit until P5) |
| P2 nodes present, no HCC header | P5 dump | record; stop |
| P3 id1 only | `dmesg` + `status` | record; stop (no writes) |

---

## 10. [U] unknowns, and which probe can pin them

| [U] | probe | note |
| --- | --- | --- |
| which ETE channel carries the group-4 ready messages | **P2** | prediction DR0 (REPORT §4.1 [I]) |
| initial DR published-free-index convention the firmware expects | **P0/P2** | we publish packed `0x400` = full ring of 32 [I]; if deposit never starts, §11 is the follow-up knob |
| actual deposit node flags / payload buffer size the firmware uses | **P2/P5** | node `word1` and `len` are logged |
| whether the firmware re-asserts each DR `+0x00` enable | **P4** | read-only |
| whether deposits can arrive without a mailbox notify | **P1** | fill watchdog |
| out[5] / "twin" semantics | P4 read-only | B3 never writes it (project rule) |
| which firmware peer writes `0xcece` | — | not addressable by this run (host write site [U]) |
| twin/out[5] interrupt-send semantics | — | out of scope |

---

## 11. Alternate-idea knob (a separate attended session, only if P2 shows fill=0 while bits fire)

The pre-posted full-ring publish value is an [I]: 32 buffers -> packed index `0x400`. If the
device posts notification bits but never advances any DR `+0x3c` fill index and all enables read
1, the initial published-index convention may be wrong. A **later** session may reload with

    insmod hi5622_pciet_b3.ko poll_ms=0 prepost=31

(publishes packed `0x1f`, leaves one slot unpublished) and repeat steps 1-12. One idea per
session: do not try both conventions in one run. Record the convention in the ledger either way.

---

## 12. Attribution

- Contract and probe definitions: `wifi-hcc-re` lane (task-63), REPORT §4/§5/§8/§9 + SUMMARY.
- Transport skeleton, B1/B2 bench evidence and stop-rule discipline:
  `wifi-transport-port` lane (task-64/65), ../BENCH-PLAN.md + ../REPORT.md.
- Instrument code, build evidence, this plan: `b3-instrument` lane (task-66), 2026-11-22,
  host-only (no device contact).
