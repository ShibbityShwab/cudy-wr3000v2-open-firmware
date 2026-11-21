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

---

## Status update 2026-11-21 (after the E0-a run)

E0-a (mirror off, image 990c5cab) was run on hardware. Results and analysis: `MINIMAL-EXPERIMENT.md`
(appended result) and `E0A-ANALYSIS.md`. What it settles for this list:

- **U5 (does a native CPU path exist without the mirror?) - ANSWERED: yes, for the exception
  class only.** The ring receives the host's 60-byte ARP retries with `status=37 err=1 port=11`
  and no 98-byte unicast completions. With `accepterr=1` the exception class is deliverable
  (proven on 2026-11-20 15:30: the box answered the ARP on the wire). The mirror remains
  mandatory for IP.
- **U6 (completion port identity) - ANSWERED for exception-class frames: `port=11` is correct.**
  No tag parser is needed for that class. Normal mirror copies still report `port=0`.
- **U7 (CPU tag format) - PARTIAL:** the exception-class frame carries `vlan=0` and no in-frame
  tag is needed to parse it; tag work is demoted until/unless the stock capture shows tagged CPU
  delivery for unicast.
- **New U13/U14/U15/U16 below** replace the old "generic classifier" target.

### New unknowns

| # | unknown | why it matters | read/experiment |
| --- | --- | --- | --- |
| U13 | which of the eight W10 default-classification entries classifies ARP/broadcast as the exception class, and what action routes it to the CPU | tells us which IFC entry shape to copy for a unicast punt; the engine descriptors are write-only so behavior is the only probe | P-matrix P3/P4 (rewrite one W10 entry at a time via tab128+engine, restore after; observe ARP/ICMP RXDIAG classes) |
| U14 | where the stock boot keeps the unicast-to-CPU state (bridge MAC / unknown-unicast): L2, IFC, OPP or PA | the missing state for M1a; decides the first W21 write set | E0-b revised capture: stock CFE procfs + IFC banks + L2-with-traffic + OPP/PA; see "Reads executable over SSH" below |
| U15 | what status value a unicast punt carries, and whether our driver must accept classes other than 0x25 | if the value is a packed class the driver must not filter on 0x25 specifically | after a candidate punt: RXDIAG status values for 98-byte frames; extend the accepterr policy if needed |
| U16 | does the hardware learn/drop unknown unicast, and is a separate flood-to-CPU rule needed for LAN-to-LAN (M1b) | M1b: two LAN ports must exchange frames; without hardware learning the CPU needs a copy of both directions | bench with a second host (or a UART-observed cable swap): ping from LAN jack A to a host on LAN jack B with the mirror off; needs hardware not currently at the bench |


## Reads executable over SSH on the adopted baseline image (c3ef2a50)

All read-only, safe while the host pings. The image has a static devmem. These establish the
**ours** side of the E0-b diff and verify the canonical state; they write nothing.

~~~
# identity + gates (confirm the running image's built-in command line)
cat /proc/cmdline
dmesg | grep -E 'W3|W4|W5|W6|W7|W8|W9|W10|W12|W13|W16|W17_MIRROR|RXCPUCOPY' | tail -40
DEV=/usr/bin/devmem; [ -x "$DEV" ] || DEV=$(command -v devmem)

# mirror state (fallback path): expect d8=0x800 dc=0x6400000 c0=0x40 after link-up
$DEV 0x124000d8 32; $DEV 0x124000dc 32; $DEV 0x124000c0 32; $DEV 0x124000b4 32

# PFE global attribute words (stock-difference candidate; read-only here)
$DEV 0x1200002c 32

# IFC default entries 1..8 in bank0, and bank1 entries 0..7 (four words each)
i=1; while [ $i -le 8 ]; do for w in 0 4 8 12; do $DEV $((0x12150000 + i*16 + w)) 32; done; i=$((i+1)); done
i=0; while [ $i -le 7 ]; do for w in 0 4 8 12; do $DEV $((0x12160000 + i*16 + w)) 32; done; i=$((i+1)); done

# count non-zero entries per IFC bank (bank0 expect 8, bank1 expect 0 on ours)
for base in 0x12150000 0x12160000; do
  i=0; nz=0; while [ $i -lt 128 ]; do
    s=0; for w in 0 4 8 12; do v=$($DEV $((base + i*16 + w)) 32); [ "$((v))" -ne 0 ] && s=1; done
    [ "$s" -ne 0 ] && nz=$((nz+1)); i=$((i+1)); done
  echo "$base nonzero-entries=$nz"; done

# tag action table: count non-zero entries (expect 0 on ours)
i=0; nz=0; while [ $i -lt 64 ]; do
  s=0; for w in 0 4 8 12; do v=$($DEV $((0x124d4000 + i*16 + w)) 32); [ "$((v))" -ne 0 ] && s=1; done
  [ "$s" -ne 0 ] && nz=$((nz+1)); i=$((i+1)); done
echo "tagact nonzero-entries=$nz"

# OPP / PA / L2 config non-zero summary (first 0x100 words of each window)
for win in 0x12400000 0x12080000 0x12180000; do
  for off in $(seq 0 4 0xfc); do v=$($DEV $((win + off)) 32);
    [ "$((v))" -ne 0 ] && echo "$(printf 0x%x $win)+$off=$v"; done; done
~~~

How to use the results:

- IFC bank0 non-zero entries should be exactly the 8 W10 defaults; bank1 should be empty. Any
  extra non-zero entry is a surprise that must be explained before any write.
- tagact should be empty on ours. On stock, every non-zero entry is a candidate; the host-side
  `diff-captures.py` decodes the fields (`vid~`, `0x40000`, `mode~`).
- OPP/PA/glb non-zero words: compare with the stock capture. A stock-only OPP/PA word that looks
  like a port map or CPU destination is the highest-value candidate for the missing
  unicast-to-CPU state.
- Run all of it with the host pinging, then repeat on stock with the same traffic (E0-b); the
  stock side additionally captures the CFE procfs dumps.

### Revised E0-b priority (replaces the earlier generic target)

1. Stock CFE procfs (interface/FDB/flow dumps) with traffic - it may state the bridge-MAC/CPU
   entries directly.
2. IFC banks 0/1 on stock with traffic (entries beyond the eight defaults).
3. L2 window on stock with traffic: search for the box MAC (bdinfo) and host MAC
   (00:e0:4c:63:17:58), both byte orders; record offsets/values.
4. OPP/PA with traffic: compare the full first 1 KiB, not only the mirror words.
5. Only then design a write set. Tag-action replay is demoted until a tagged CPU delivery is
   actually observed.

~~~

## U12 - completion of the identity path (success criterion)

M1 is met when, for all four wired ports, a frame from the bench host (cable on that jack)
produces a native RX completion that the driver maps to the correct netdev, with the mirror off
for that port, and the Linux bridge/router works (ping to the box from each LAN jack; WAN
routing tested with the WAN-side peer available). Record: RXDIAG tuples, netdev counters,
bridge FDB, ping results, UART log.
