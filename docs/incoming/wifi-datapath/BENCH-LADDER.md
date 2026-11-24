# BENCH-LADDER - data-plane bring-up for an attended session

Companion to REPORT.md. Every stage below is **attended**; preconditions are the standard
ones: recovery door armed/READY, bench link PASS (tools/bench-link.sh), UART capture
read-only to an absolute path, image/module hashes recorded, no vendor Wi-Fi modules bound,
one clean driver, never rmmod after release. Hard rules unchanged: never write
CA 0x400392f0, never read RC misc 0x10161000, never post mailbox id 5, no CECE during boot,
one frame one idea, no retry after a timeout. Image used for the working loop: PD4-patched
firmware sha256 cfe24bfb... on our kernel, module hi5622_wifi.ko 400,732 B sha256 774c4f1f...
(the same module that ran the validated create+sync lap).

## B0. Control p2p (read-only, 1 min)

Confirm the state the last lap ended in and that no exception is pending:

| # | read | location | expect |
| ---: | --- | --- | --- |
| 1 | golden page | handshake page CA 0x1BF000 (BAR0 = CA+0x6B8000) | 0x86807FEF / 7 / 2 / 0xABCD |
| 2 | boot state | CA 0x118B60 | 1 |
| 3 | exception mask | CA 0x400008 | 0 |
| 4 | PD4 cave | CA 0x108240 | stub bytes present |
| 5 | exception staging | CA 0x40000C | zero (no frame) |
| 6 | ready pair | direct reader rd_catalog | id1/id2 seen (id2 payload 0x01060850) |

Stop if any row differs; do not proceed.

## B1. TXQ-table address sync (one frame; the vendor's own init message)

Purpose: hand the TXQ table device VA to the firmware (key 0x0320, worker CA 0x81445).
Builder: dp_build_txq_addr_sync(). Requires a host-allocated zeroed 0x2524-byte non-cached
buffer; its device VA is the payload. If the driver does not own such a table yet, build the
frame with a **dummy value only to prove acceptance**, or skip B1 until the table allocation
lands (a firmware that dereferences the VA could fault; if it faults, the exceptions are
contained and the boot page stays golden - the stage is still diagnostic).

Pre-flight reads: mask CA 0x400008 = 0; out[0] (CA 0x40039010) = 0; staging CA 0x40000C
zero. Fire exactly once (48-byte? no - this frame is 32 bytes; node length = header total
0x20). Observables (read-only afterwards):

| observation | meaning | next |
| --- | --- | --- |
| mask stays 0, staging unchanged, out[0] 0x8->0 | accepted, no fault | capture; proceed B2 later |
| mask becomes 0x12/other, staging frame appears | handler faulted - the dummy/VA is unsafe | stop; power-cycle; record; never retry |
| out[0] stuck 0x8 | mailbox not consumed (should not happen with PD4) | stop; record |
| new D2H frames (rd_catalog) | firmware processed something | decode; capture |

Stop rule: one frame only. Do not clear out[0] speculatively.

## B2. MAC slot-5 window probe (read-only, single access) and ring programming

Purpose: resolve the reachability question and then program the RX rings. Two sub-stages
with a hard stop between them.

B2a - read-only window probe (1 read): read CA 0x40040000 (BAR0 0x402a000) once. Expected
vendor-boot value 0x00000100 (tx_ba_info depth). If this **bus-errors / hangs**, stop: the
declared region-3 window does not map the MAC block, the data path needs a new (approved)
viewport design, and no host write may be attempted. If it returns 0 or a plausible value,
record it and continue to B2b in the next session (single idea).

B2b - program one band's RX rings (host register writes only):
preconditions: B2a read worked; all values prepared from REPORT.md 2.1; every write is a
read-modify-write with immediate read-back; start with **band 0 only**; do not touch band 1
in the same session; do not add a iATU viewport (a previous session rebooted the box doing
that).

Order (one session, one band):
1. +0x10 = ACP(0x01060850) (ready id2 payload through memmap conversion)
2. +0x1c = 0x063c | 0x00b0<<16
3. +0x20/+0x24/+0x28 = devVA(allocated free rings + cmp ring)
4. +0x2c/+0x30/+0x34 = 256/512/4095
5. +0x00/+0x04 = 256 / devVA(tx_ba_info buffer)
6. +0x48 = 0xfffffcc1
7. after write-back verification: fill the free ring nodes with buffers and advance
   +0x38 (norm wptr) only if the ring node format is confirmed by the static follow-up
   ([U] today: free-ring node format needs the same treatment as the ETE nodes - do not
   guess in this session; step 7 is deferred until a static spec exists)

Expected negative/positive observables: read-backs equal the writes (window works), the
device does not assert an error (no box reboot), and (positive) the completion-ring wptr
(+0x4c) starts advancing on its own once RX is activated by the firmware; if it stays 0,
the firmware has not enabled the MAC data path - stop, do not poke more registers.

## B3..B5. Later stages (need struct RE first - do not improvise payloads)

* B3 user/VAP add: g0 id7 user-add (0xa8) / g2 WAL VAP start; payload structs [U]. Blocked on
  a struct-RE lane or a vendor kprobe capture.
* B4 scan: g0 id0xC (0x1fc); results g1 15/16/17 + g2 2. Blocked on identically.
* B5 data smoke: after B2b steps 1-6 verify and B3 exist. TX: one ARP-sized Ethernet frame ->
  8-byte MSDU entry -> ring post; RX: observe completion ring and read back the 0x28-byte
  descriptor from the first completed buffer (this also settles REPORT.md 7.2's [I] bits).

## Decision matrix / stop rules (all stages)

| signal | stop? | action |
| --- | --- | --- |
| handshake page changes from golden | YES | stop all writes; power-cycle; capture |
| exception mask CA 0x400008 != 0 | YES | stop; record staging frame; power-cycle |
| out[0] stuck / staging shows a new exception frame | YES | lane closed for the boot |
| MAC block read/write bus error or box reboot | YES | no retry in the same boot; report exposure |
| read-back != written value | YES | the write did not land; do not proceed |
| new D2H frames appear | no | capture, decode, then decide next session |

Safety: one register/frame idea per session; the recovery door armed for every session that
writes a register; no speculative clears; no extra iATU viewports; UART read-only.
