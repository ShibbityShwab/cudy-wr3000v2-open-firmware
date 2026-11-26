# PROBES - corrected read-only PPDU-list probe pack (P1-P8)

**Lane:** `wifi-ppdu-init` (task-219).  **READ-ONLY**: every item is a
load; no write, no retry, no address outside the host-readable windows.  Run
with the resident read helper (`bar0read`, physical addresses, O_RDONLY +
PROT_READ; it refuses unaligned addresses and the RC misc window).

Conversion **[V\*]** (committed lanes): firmware RAM phys = 0x40000000 + CA +
0x6B8000; device IO phys = CA + 0x3B8000; PKTRAM phys = 0x40000000 + 0x1D8000 +
(CA - 0x01000000); ACP phys = 0x40000000 + 0x4D8000 + (CA - 0x02000000).
Never read the RC misc window phys 0x10160000..0x1016FFFF.

Why this pack exists: `../wifi-rx-f1-thread` PROBES P4 read the resolver's
module-context id 0 (0x119758), **not** the device object the init writes
(0x174BF0+0xE28*band).  Its zero at +0x67C is expected either way.  These P1-P8
are the corrected reads; P1-P8 replace f1-thread P4.

## P1-P8 (one pass)

| # | item | CA | phys | width | expect / meaning |
| ---: | --- | ---: | ---: | ---: | --- |
| P1 | **device-init gate byte** | 0x00180810 | 0x40838810 | 1 | u8[0x17FCA8+0xB68]: **1 = init runs**; 0 = skip silently; >1 = error 0x65 (attach aborts) |
| P2 | **PPDU list name ptr, band 0** | 0x0017526C | 0x4082D26C | 4 | **0x001060F7** after the init ran (the self-check) |
| P3 | list head next, band 0 | 0x0017525C | 0x4082D25C | 4 | 0x0017525C (= object+0x66C) when empty/initialised |
| P4 | list head prev, band 0 | 0x00175260 | 0x4082D260 | 4 | 0x0017525C too |
| P5 | descriptor count, band 0 | 0x00175264 | 0x4082D264 | 4 | count of preallocated 0x1A0-B descriptors |
| P6 | list name ptr, band 1 | 0x00176094 | 0x4082E094 | 4 | 0x001060F7 iff the band-1 device inited |
| P7 | device count, init array | 0x00174BEE | 0x4082CBEE | 1 | 0xD1B68 writes 2; 0 = the array was never inited |
| P8 | the OLD probe object (context id 0) | 0x00119DD4 | 0x407D1DD4 | 4 | informational only - **its value is not a PPDU-list proof** |

All eight are firmware-RAM loads inside the same BAR window the f1-thread lane
already read successfully; none is a device-IO/RMW address.

## Run order

**P1 -> P2 -> P3 -> P4 -> P5 -> P6 -> P7 -> P8**, on the 2.4 GHz AP boot, after
the golden-page/status check (f1-thread P0) passes.
Then the **stock control**: the same P1-P8 on a stock boot, same boot phase
(2.4 GHz AP up) if the stock can be booted without a flash; the committed
`opensource/dumps/reg_all.txt` does **not** cover these firmware-RAM addresses,
so the control is a fresh read, not a dump lookup.

## Decision table (one reading pass)

| observation | conclusion | next |
| --- | --- | --- |
| P2 == 0x001060F7 (and P3/P4 self-pointers) | **the list init DID run on our boot**; F1 is not the descriptor-list init.  Re-read the f1-thread decision table with the corrected object. | f1 residual = firmware RX consumer / first-PPDU path (as f1-thread sec. 6) - not this lane |
| P2 == 0, P1 == 1 | the gate passed but the init did not run (or ran on neither band): H2/H3 (the registered body never reached 0x48314, or the boot registration differed) | target the boot-body path; capture the firmware-RAM state right after boot (read-only) |
| P2 == 0, P1 == 0 | H1: the gate skipped the init | compare P1 on stock (expect 1) and find what writes it (firmware-internal; no host message found yet) |
| P2 == 0, P1 > 1 | the attach aborted with error 0x65 inside 0xD1B68; look for the preceding failure | correlate with driver log/boot stage |
| P6 only matches (band 1 present, band 0 absent) | H4: band-object mix-up; also re-check which band the RX consumer uses | fix the object assumption, not the firmware |
| P7 != 2 | the 0xD1B68 array header was never written -> the init never ran at all | same as P1==0/>1 branches |
| P8 nonzero | irrelevant to the list; do not use it | - |
| page/mask/mask anomaly at any point | firmware fault | power-cycle; recovery door; capture UART (read-only) |

## Safety

Read-only, no retries, no writes.  If a read returns 0xFFFFFFFF or an
external-abort, the window is not mapped in this boot - record and stop, do not
retry another address width.  Re-probe after any reboot in the same batch so the
P1/P2/P6/P7 tuple is coherent.  The EDCA post (f1-thread FIRE-RECIPE) remains a
*write* path and is not authorised or required by this lane.
