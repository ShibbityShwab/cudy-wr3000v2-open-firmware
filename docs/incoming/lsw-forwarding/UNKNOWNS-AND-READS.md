# Unknowns and the exact attended reads that answer them

Status 2026-11-21. Read-only plan. Every item below is an [U] from the implementation plan; each
has the exact capture command and the decision it feeds. Addresses are physical (window base +
offset). On our image the static devmem tool is /usr/bin/devmem; on the stock vendor shell use
the busybox devmem or the static copy from the bench kit. Reads only - the capture script has no
write form.

## Read helpers (POSIX shell, works on both images)

~~~
rd() { devmem "$1" 32; }          # print one word
dump4() { rd $1; rd $(($1+4)); rd $(($1+8)); rd $(($1+12)); }
~~~

If the shell's devmem prints decimal or 0x-prefixed hex, record it verbatim; the host-side
diff tool normalises both.

## U1 - stock tag action table content (highest value)

The tag action table is the vendor's runtime CPU/VLAN tagging mechanism. Location decoded from
hi_lsw_pfe!tag_write@0x2eb0 (tab128 write at synthetic 0xb054000): physical 0x124D4000,
16-byte entries, up to 64 entries.

~~~
i=0; while [ $i -lt 64 ]; do dump4 $((0x124D4000 + i*16)); i=$((i+1)); done
~~~

Run on stock with traffic flowing. Decision: any non-zero entry whose VID/flags match the LAN
ports is the vendor's CPU-tag/VLAN action and becomes the first replay candidate (N1). If the
whole table is zero on stock, vendor tagging is not used in this boot and Route B must design
its own.

## U2 - IFC tab128 banks

W10 (release line) covers bank0 entries 1..8. Read the full banks on stock with traffic:

~~~
# bank 0 (0x12150000) and bank 1 (0x12160000), entries 0..127 x 4 words
for base in 0x12150000 0x12160000; do
  i=0; while [ $i -lt 128 ]; do dump4 $((base + i*16)); i=$((i+1)); done
done
~~~

Decision: entries beyond the W10 defaults on stock (in either bank) are vendor runtime
classification state. If present they are replay candidates; if both banks are only the 8
defaults, the vendor does not install more IFC entries at runtime in this boot.

## U3 - IFC command-engine descriptors (write-only; not readable)

The 5-word engine descriptors at ifc+0x138..0x148 have no read path in the recovered vendor
code. Do not try to infer them from a read; the probe matrix below treats the engine as a
write-only black box, one entry per attended run.

## U4 - L2 table / MAC learning state with traffic

The L2 window (0x12180000) has no Linux writer; its config (offsets 0x100..0x18c) is identical
on stock and ours. What is unmeasured is whether active learning writes entries somewhere in
the window (or in DDR). Capture the candidate table pages on stock while the host pings:

~~~
for off in 0x0 0x8000 0x10000 0x18000 0x20000 0x40000 0x60000 0x70000; do
  a=$((0x12180000 + off)); i=0
  while [ $i -lt 1024 ]; do rd $((a + i*4)); i=$((i+1)); done
done
~~~

Search the capture for MAC-shaped words: the host NIC MAC (00:e0:4c:63:17:58 in the recorded
bench) and the box bdinfo MAC (parent BENCH-FACTS / bdinfo). A byte-swapped or 16-bit-swapped
match counts. Decision: if hidden L2 entries exist, record offset/value and add them to the N1
replay candidate list; if nothing changes with traffic, hardware learning is either disabled or
not reachable through readable registers (M2 needs the flow path instead).

## U5 - mirror-off native delivery on eth2 (Q1 of the minimal experiment)

Captured by E0-a (cmdline-only image variant). Read on the box after the test:
~~~
dmesg | grep -E 'RXDIAG|RXCPUCOPY|pie-trace|cpu_mirror|W17_MIRROR'
cat /proc/interrupts | grep -i pie
~~~
Decision:
- zero RX completions with the mirror off -> the native punt path is not active on eth2;
- normal completions with a port field -> native delivery exists; the port field is the
  identity candidate (U6);
- only exception completions -> native delivery is the exception path (U7).

## U6 - what the completion port field means for native deliveries

From the same E0-a capture, for every RXDIAG line record: class (status), port, vport, tag
class, len. Compare:
- with the mirror on (canonical image): the port-0 copies;
- with the mirror off: any delivery is native, so its port field is meaningful.
Decision: if native completions report the ingress port, the PIE side needs no tag parsing; if
they report 0/constant, identity must come from the frame tag (U7) or from per-port CPU
destinations (not planned).

## U7 - CPU-delivery tag format

For native deliveries, capture the first 32 bytes of the frame from the RXDIAG path (patch 053
prints the descriptor; the driver can be extended in the probe image to hex-dump the first 32
bytes) and look for:
- an in-frame 802.1Q tag (0x8100 + VID) whose VID tracks the ingress port or interface;
- a 4/8-byte vendor header before the Ethernet header (the CFE stack uses tag actions; the
  tag_write entry shape suggests a VLAN push with VID + TPID class).
Decision: defines luofu_pie.cputag decode mode (plan section 5.3) and the VID/port mapping.

## U8 - per-port native coverage for the far jacks

Requires an image with the probe diagnostics pre-armed (rxdiag/accepterr/native gates) because
after the cable leaves eth2 there is no network path to the box. Procedure per candidate jack:
1. arm the experiment for that switch port (native mask includes it, mirror mask excludes it);
2. move the cable to that jack (human), UART capture running;
3. host pings; capture UART/dmesg;
4. move cable back; confirm eth2 mirror fallback restored ssh.
Decision: whether the native stage covers ports 9/10/12 and whether the identity field tracks
the port (same checks as U6/U7).

## U9 - whether a native stage write survives switch port-up

The W19 finding: OPP mirror registers are reset/ignored when the switch port comes up, and the
driver re-applies them 200 ms after link-up and every 2 s. For every native write, verify the
same: capture target words before and after a link-up (cable move or link flap) and compare.
Decision: the W21 stage must re-apply after link-up like the mirror; if the words survive, the
worker is optional.

## U10 - PVID/VLAN membership / port-based forwarding table location

No vendor module writes the L2 window from Linux, so a per-port PVID table, if it exists, is
either pre-Linux config or written through a table engine we have not identified. The only
non-invasive probe is to compare stock captures with the same traffic on different jacks and
different interface configurations produced by the *vendor* userspace (e.g. LAN versus WAN
active), which the bench can select by booting stock with only the WAN cable. Decision: if a
table word follows the jack/interface, it is the PVID/membership table; otherwise M2 must use
the flow/IFC path.

## U11 - IFC action encoding for "send to CPU + tag"

This is the probe matrix (Route B). Starting point: the eight W10 entries are known vendor
payloads (engine 5 words + tab128 4 words). One experiment per attended session:

| # | change from W10 baseline | expected observation | stop rule |
| --- | --- | --- | --- |
| P0 | none (baseline image) | mirror copies as today; exception completions on eth2 | - |
| P1 | mirror off for port 11 (E0-a) | decides if a punt action already exists | revert cmdline |
| P2 | rewrite IFC entry 8 exactly as W10 (control) | no behavioural change | proceeds |
| P3 | IFC entry 8, engine word4 one bit varied at a time (bits 0..31), rxdiag on | any new native completion on eth2 | revert on regression, one bit per session |
| P4 | same, varying engine word2/word3 key fields one nibble at a time | native completion appears with a port/status signature | as above |
| P5 | add a tag action entry (U1-derived) and reference its index from the action word found in P3/P4 | native completion carries an in-frame tag whose VID tracks the port | as above |
| P6 | per-port entries for 9/10/12 using the working shape | far-jack native delivery (U8) | as above |

Rules: the mirror stays on every port except the one under test; every write is snapshot +
readback + restore; the engine descriptors are write-only so they are acknowledged and reverted
by writing the previous descriptor back (the only revert possible); any ping/SSH loss on eth2
ends the session and restores the image.

## U12 - completion of the identity path (success criterion)

M1 is met when, for all four wired ports, a frame from the bench host (cable on that jack)
produces a native RX completion that the driver maps to the correct netdev, with the mirror off
for that port, and the Linux bridge/router works (ping to the box from each LAN jack; WAN
routing tested with the WAN-side peer available). Record: RXDIAG tuples, netdev counters,
bridge FDB, ping results, UART log.
