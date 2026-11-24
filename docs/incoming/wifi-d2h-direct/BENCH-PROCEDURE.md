# BENCH-PROCEDURE - direct-read D2H receive on the golden boot (attended only)

**Lane:** wifi-d2h-direct (task-89).  **Status:** PLAN - this lane is host-only
and never contacted the device.  The run below is the Lead's attended session.
It builds on the project hard rules (BENCH-FACTS.md, RUNBOOK.md, LESSONS.md);
the newest dated ledger entry and STATUS.md win over this document.

**Instrument:** `hi5622_pciet_hcc.ko` from this lane (see BUILD-NOTES.md)
- the wifi-hcc-service module with the direct-read D2H service patched in.
It is its own module (driver `hi5622_pciet_hcc`); load ONLY it - one driver
can bind the endpoint, so do not load the base transport or the B3/b3-era
instruments in the same boot.

**Goal of the run:** take the read-only D2H receive pass on the golden boot:
decode the frames the firmware staged in the device group-A DR rings, deliver
the ready pair + EXCP_INFO + the rest into the catalogue, and settle the one
open decode (is **g4 id2 DEVICE READY** among the four 16-B DR0 frames?).
**No device writes.**  The +0x38 republish and the experimental +0x1C consumer
are separate, optional, one-idea sessions (section 6).

---

## 0. Preconditions (all mandatory before step 1)

| gate | how | pass criterion |
| --- | --- | --- |
| G1 recovery door armed | tools/recovery-card.sh | READY, human at the bench |
| G2 bench link | tools/bench-link.sh | PASS |
| G3 UART capture | tools/uart-capture.sh (read-only, absolute path) | alive; **no tty writes, no screen/minicom/picocom** |
| G4 image + module | sha256 of the flashed image per STATUS.md; modinfo | vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; module sha256 `60810cf2...` (262,352 B) recorded |
| G5 no vendor wifi modules | lsmod | hi5622v100_plat / hi5622v100_wifi absent |
| G6 firmware blob | sha256sum /lib/firmware/hi_wifi/FIRMWARE.bin | 928,920 B (local evidence only; never committed) |
| G7 DTS dma-ranges image | STATUS.md | RC inbound window 0x80000000..0x8fffffff; hcc_arm enforces it in code |
| G8 only this module | lsmod after step 1 | hi5622_pciet_hcc present; hi5622_pciet / _b3 absent |

## 1. Hard rules (restated - breaking one has cost a recovery cycle)

- **NEVER write CA 0x400392F0 (out[5])**.  The hcc service refuses it at build
  time and at runtime; this procedure only reads it.
- **NEVER write 0xCECE before the handshake page +8 reads 2.**  The module
  refuses it by policy; the legacy announce attr is dead in this build.  This
  is the GP8 bug.
- **No H2D frame** in this session (the boot path is device->host only; the
  ready pair needs no reply).
- **No device write at all in the read-only pass.**  The direct receive path
  performs none; verify that dmesg shows no "republished host free index" and
  no "EXPERIMENTAL" line.
- **Never write the PKTRAM control word** at node-array base + 0x100 (the ETE
  channel handshake); **never write firmware RAM**.
- **UART is READ-ONLY.**  **Never read the RC misc window 0x10161000.**
- Do not rmmod after the release; power-cycle is the reset.
- One idea per session; every optional write is one explicit sysfs store,
  logged to dmesg.
- Use endpoint 0000:00:00.0 for every attribute:
  `/sys/bus/pci/devices/0000:00:00.0/$attr`.

## 2. Stages

### B0 - load (no device writes)

    insmod hi5622_pciet_hcc.ko

Expected dmesg: two probe lines ("HCC service ready: 3 SR + 4 DR ring mirrors
... no device writes", then "D2H DIRECT receive service ready (read-only) ..."),
the D2H DIRECT attribute line, no base poll thread, no oops.  Record the module
sha256.

Gate: no abort/SError/hang; only this module.

### B1 - iATU windows + firmware upload + verify (unchanged transport steps)

    echo 1 > prepare      # 6x iatu vpN readback=OK; windows=1
    echo 1 > upload       # 928920 bytes, 2 chunks
    echo 1 > verify       # diffs=0 match=YES

Gate: 6/6 readback OK and diffs=0, else STOP before release.

### B2 - arm the ETE rings (vendor-exact, optional for the direct path)

    echo 1 > hcc_arm

Expected: ETE intr RMW &= 0xFFE0F8F8; SR0-2 group A and DR0-3 group B armed
with readback OK; DR pub(+0x38)=0x00000400; firmware-owned enable still 0.
Not required by the direct read (it never touches the host rings) - run it
anyway so the boot is vendor-exact and comparable with the 12:20 session.
Gate: 7/7 readback OK; MISMATCH -> STOP (do not release).

### B3 - start the service before the release

    echo 1 > hcc_loop

Rationale: the loop keeps the firmware out[1] flush window open and starts the
direct poll; a later manual `echo 1 > rd_poll` also works because the boot
batch stays in the device ring until read.

### B4 - release (0x5A5A ONLY)

    echo 1 > release

Expected: "CPU release: 0x5a5a -> CA 0x40000108".  From here rmmod is unsafe.
Gate: no abort/SError/hang.  **Do not** write CECE.

### B5 - golden boot check (0.2-1 s)

    cat hcc_status

Expected (GP8 signature): handshake page 0x86807fef / 0x00000007 / 0x00000002
/ 0x0000abcd (+8==2), state CA 0x118b60 = 1, mask CA 0x40008 = 0.
Gate: if the page is not golden, STOP (the direct path gates itself on +8==2
and would report `boot_ok=0` anyway).

### B6 - the direct-read pass (THE MEASUREMENT)

    cat rd_status
    cat rd_decode
    cat rd_catalog
    cat hcc_ready

`rd_status` expected (the load-bearing lines):

    D2H DIRECT status: init=1 boot_ok=1 windows=1 polls>=1 ...
    dr0 rec=0x10f2b8 layout=OK src=shadow base=0x01060440 depth=32 enable=0x1
        prod_reg(+0x18)=0x00000000 prod_shadow(rec+0xc)=0x00000004 used=shadow
        cons(+0x1c)=0x00000000 pub(+0x38)=0x00000400 fill(+0x3c)=0x00000010
        last=0x00000004 consumed=4 ...
    dr1 ... prod_shadow=0x00000000 ...
    dr2 ... prod_shadow=0x00000000 ...
    dr3 rec=0x10fcb4 layout=OK src=shadow base=0x01060110 depth=32 enable=0x1
        prod_shadow=0x0000000c last=0x0000000c consumed=12 ...

- `layout=OK` proves the firmware record self-check passed (channel CA /
  send CA / cfg pointer match this image).
- `src=shadow` is the authoritative producer (the register +0x18 is only the
  init copy and may read 0 - record it, both are printed).
- `prod_shadow` = 0x4/0xc/0/0 and `consumed` = 4/12/0/0 means the boot batch
  was read completely; the producer is the boot-time deposit count.
- `cons(+0x1c)=0` is expected: only the idle engine would advance it.
- `fill(+0x3c)=0x10` is expected and is **not** a frame count (see REPORT
  section 6).

`rd_decode` expected: 16 frames (newest last), starting with

    [00] ... dr3 node=0x02048c48 len=12 g=4 id=1 total=0xc tag=OK lenchk=OK g4_id1_plat_ready payload[0]=
    [01] ... dr3 node=0x02042de8 len=73 g=4 id=3 total=0x49 tag=OK lenchk=OK g4_id3_excp_info payload[...]="DEVICE EXCP_INFO..."

then the ten remaining DR3 frames (ids printed: this is where the previously
undecoded 592/536/500/41/74/70/128/70/99-B messages get classified), then the
four DR0 16-B frames in staging order.

`rd_catalog` expected:

    ... id1_seen=1 count>=1 | id2_seen=<0 or 1> count=<...> payload=<...> B dword0=0x........
    ... excp_info>=1 | frames=16 ...
    g=4  id=0x01 (...) count=1 ... g4_id1_plat_ready
    g=4  id=0x03 (...) count=1 ... g4_id3_excp_info
    g=3  id=0x12 (...) count=<1..4> ... g3_id18_temp_state
    <the re-read ids of the remaining DR3 frames>

`hcc_ready` now ends its engine-path section with a DIRECT line:

    DIRECT(device group-A): polls=... nodes=16 frames=16 id1_seen=1 id2_seen=<0|1> overruns=0 layout=OK - this is the path that works with the ETE engine idle

Gate / decision (section 5): record everything; **no writes**, stop after the
declared observation window unless a stop rule fires.

### B7 - record and power-cycle

Capture dmesg + UART + all `rd_*`/`hcc_*` attrs to the evidence path
(absolute paths under `build/`; /tmp is not evidence).  Power-cycle (no
rmmod after release).  Write the ledger entry.

---

## 3. Expected observables (one table)

| observable | expected on success | if different |
| --- | --- | --- |
| handshake page | 0x86807fef / 7 / 2 / 0xabcd; state 1; mask 0 | STOP, boot protocol problem (release-only?) |
| rd_status layout | OK on DR0..3 | the firmware record layout did not match: record `layout=MISMATCH`, do not trust the producer; keep the run read-only and report |
| rd_status producer | DR3 0x0c, DR0 0x04, DR1/DR2 0x00 | if all zero: the boot batch was not staged - record, check hcc_status state/bit6; if a channel > 0 and no frames decoded: node validation problem, capture `rd_decode` |
| rd_decode | 16 frames, first two g4 id1 / g4 id3 | a short count: overrun counter > 0 (host too slow - unlikely at 16 nodes) |
| rd_catalog id1 | seen=1 | direct path did not get the ready announce - capture everything, do not write |
| rd_catalog id2 | **the decisive open decode** | if 0: check the four DR0 16-B frames in `rd_decode`; all g3 id18 => the id-2 phase gate is firmware-side (see REPORT 5.3) |
| dmesg write audit | no "republished host free index", no "EXPERIMENTAL" | any of those means a write happened - it should not have; capture and report |
| overruns | 0 | >0 means the host fell >32 deposits behind; record and keep read-only |

---

## 4. Stop rules (all hard)

- any abort/SError/hang/reset -> capture UART+dmesg, stop, power-cycle;
- handshake page not golden -> stop before judging the direct path;
- `layout=MISMATCH` on any channel -> stop the decode interpretation (the
  read-only poll is still safe), report the self-check values;
- implausible `prod_shadow` (out of the 0..2*depth packed range) -> the
  service falls back to the register and logs; do not hand-edit anything;
- never write out[5]; never write CECE before page +8==2; never write device/
  firmware RAM; never write the PKTRAM control word;
- no H2D frame in this session; no speculative H2D payload;
- do not reload modules after the release; power-cycle instead.

---

## 5. Decision matrix after the read-only pass

| outcome | meaning | next action (separate session) |
| --- | --- | --- |
| 16 frames decoded, id1+id2 seen | the ready pair is complete and host-readable without the ETE engine. The host receive contract is solved. | next lane: post-ready H2D / link layer (the vendor's first post-ready frame is the g0 id1 TXQ sync; do not synthesize it); keep the direct read as the receive path |
| id1 seen, id2 absent (DR0 = g3 id18 only) | the device reached "plat ready" but not "DEVICE READY"; the id-2 phase gate `[sp+0x24]==2` / `[0x1ADB80+0x110]==0` is firmware-internal | firmware-side static analysis of the boot state machine's id-2 phase; the direct read is proven, nothing host-side remains |
| fewer frames than staged | ring overrun or decode stall; check `overruns`/`bad_node`/`stall` | record; if the host was the bottleneck (it is not at 5 ms), retry timing; no writes |
| layout=MISMATCH / producer zero / enable=0 | not the golden post-boot state, or a different image beat the self-check | capture `rd_status` + `hcc_status` and stop; no writes |

---

## 6. Optional experiments (each a SEPARATE session, one idea each)

Only after the read-only pass has been recorded, and only if its result makes
the experiment answer a question.  Neither is part of the default run.

**(a) Host-owned DR +0x38 republish (vendor analog, safe).**
Question: does a post-boot destination publish kick the ETE copy?
`echo pub > rd_advance` (or the existing `echo 1 > hcc_dr_republish`).
Expected: one write per channel with consumed nodes, readback logged; then
re-read `rd_status`, the ETE status CA 0x4003A874, the send words
0x4003AC00+4i and fills +0x3C (all read-only).  If status/send/fill stay 0, the
engine cannot be woken by the destination side - record and close the idea.

**(b) EXPERIMENTAL device group-A consumer +0x1C write.**  Only if a future run
finds the producer stalled at depth 32 with the host having consumed every
node (never seen in the planned pass).  Needs `insmod ... rd_ab_write=1` and
`echo ab > rd_advance`; full golden-boot gate + once per channel + readback;
this is an unverified write class - treat a negative as informative and a
positive as requiring a second confirmation in a fresh session.