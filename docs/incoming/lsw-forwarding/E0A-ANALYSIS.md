# E0-a analysis: the mirror-off boot, status=37, and what is actually missing

Status 2026-11-21, host-side analysis only. This note corrects the first reading of the E0-a
result and re-aims the work. Evidence: the run record appended to
MINIMAL-EXPERIMENT.md and the ledger entry 2026-11-21 18:10; UART excerpt
build/evidence-bench-gates/e0a-mirroroff/e0a-boot-excerpt.txt (sha256 90cde170...); image
990c5cab, cmdline luofu_switch.cpu_mirror=0 luofu_pie.rxdiag=1 luofu_pie.accepterr=1.

## 1. What the run actually shows

The excerpt contains eight RXDIAG completions over ~17 s, exactly one per second, all:

    ch=0 len=60 status=37 err=1 port=11 vport=0 vlan=0
    desc = 04000000 8238??c2 00000000 x7

and the known link pattern (eth2 up 8.3 s, down 12.9 s, up 38.9 s).

Three facts matter:

1. len = 60 is the Ethernet minimum frame (42-byte ARP + padding): these are the host's ARP
   request retries while IP is unresolved. They are the same class that the earlier 2026-11-20
   runs saw, both with and without the mirror.
2. There are no 98-byte completions in the excerpt. The host's IPv4/ICMP frames (98 B) are never
   delivered to the PIE ring. That matches the 2026-11-20 15:30 and 18:30 ledger entries:
   broadcast/ARP-class frames are punted to the CPU, unicast-to-the-box frames are not.
3. The UART excerpt contains no RXERR ACCEPTED lines, only RXDIAG. This is most likely the
   excerpt's grep filter (the ledger's earlier run with the same gate printed RXERR ACCEPTED and
   the frame was then delivered: 2026-11-20 15:30, "pie-trace RX port=11 len=60 status=37
   (delivered to eth2's netdev)" followed by an ARP reply on the wire). It is flagged here so the
   next capture greps the whole console, not just RXDIAG.

## 2. What status=37 / err=1 is (and is not)

The vendor decode is hi_pie_rx_get_desc@0xf14 (v250): status = descriptor byte +8, bits 6:0; the
summary byte +7 bit 0 is set to 1 exactly when status == 0, and our driver derives
compl.err = (status != 0). So err=1 is not an independent hardware bit: it is the vendor's
"not the no-error class" marker. status = 37 = 0x25 = 0b0100101.

What is proven about the 0x25 class:

- The frames are valid Ethernet frames. With accepterr=1 the same class was delivered to eth2 and
  the kernel answered the ARP request on the wire (first-traffic run, 2026-11-20 15:30: RXERR
  ACCEPTED status=37 port=11 len=60 -> netdev -> ARP reply d4:0d:ab:... -> host). A corrupt frame
  cannot produce that.
- The payload is not hidden behind a tag we would need to strip: vlan=0 on the completion, and
  the frame parsed as a plain Ethernet ARP. The vendor's CPU-tag/IFC action is therefore not
  inserting an in-frame tag on this class (or the tag class bits are both zero).
- The source-port identity is already correct on this class: completion port = 11 = eth2, decoded
  from descriptor +9 bits 7:4. For exception-class frames the identity problem is solved by the
  existing PIE completion field.
- The vendor's own receive path treats the field as a class, not a frame error: the descriptor
  class selects between the vendor's normal and exception delivery paths; our driver calls the
  second one "error" and (without accepterr) drops it, which is a policy choice, not a hardware
  verdict. (The exact intent of the vendor's status!=0 branch in hi_net_rx_proc is only partially
  decoded - patch 053 and PIE-DATAPATH section 11 documented it as the drop/exception path, while
  the latest disassembly shows a hook-then-deliver shape. The empirical ARP-reply result is the
  stronger evidence and is what the analysis relies on.)

Hypotheses for the 0x25 value itself (none confirmed; the reason-code table is in the switch
hardware and no readable field exposes it):

| # | hypothesis | prediction / discriminating read |
| --- | --- | --- |
| H1 | a punt/trap reason code: bit 0x20 = "exception/trap class", low bits 5 = reason (e.g. broadcast/ARP default rule) | a different CPU-directed class (unicast punt) should show a different low reason, or a different bit5-style flag; the mirror/normal class shows 0x00 |
| H2 | a packed class: low 2 bits flag class (from the +13/+10 2-bit classes), bits 2..6 a cause | varies with frame class; collect status values for ARP, broadcast, unknown-unicast, DHCP if a candidate punt rule is installed |
| H3 | a PFE/PA counter-like reason code from the ingress parse (e.g. "to CPU" reason) | same value for all CPU-punted frames of a kind, independent of path; decode needs a stock-side observation of a native unicast punt |

None of these changes the plan: 0x25 is an exception-class marker that our driver can already
deliver, and the E0-a boot failed because the unicast frame class never reaches the CPU, not
because the exception copies are corrupt.

What err=1 cannot be: a tag/destination error on the ARP frames themselves (they parse and answer
correctly), and it cannot be caused by the missing mirror (the same class appears with the mirror
off; the mirror class is status 0 / port 0).

## 3. Where the "missing CPU-punt classification" really is

The stock boot must deliver to the CPU, for the wired bridge/router:

1. broadcast and ARP-class frames (we already get these as exception punts);
2. unicast frames whose destination is the box's bridge MAC (ICMP, SSH, TCP) - these are
   completely absent from our PIE ring;
3. frames between two LAN ports (the Linux bridge needs to see traffic that the hardware would
   otherwise forward directly); in the vendor stack this is handled either by hardware flows
   (CFE offload) or by the bridge MAC entries/flood rules.

Only (1) works in our image. (2)/(3) require a switching state that the vendor's boot has and ours
does not: most plausibly an L2/FDB or flow entry mapping the bridge MAC (and/or "unknown
unicast") to the CPU port, or an equivalent per-port "copy to CPU" rule. That state is not in the
eight W10 default-classification entries (they cover the exception classes we see), and it is not
in the readable L2 config words (identical on stock and ours, pre-Linux configuration).

This is why the stock capture is still the right first move - but its target changes from
"capture the classifier" to "capture the specific unicast-to-CPU / bridge-MAC state" (see the
revised E0-b in MINIMAL-EXPERIMENT.md and the reads in UNKNOWNS-AND-READS.md).

## 4. Consequence: milestone split (M1a / M1b)

- M1a - unicast-to-CPU on eth2, mirror off. The smallest decisive milestone: with the mirror
  disabled, a host ICMP echo request to the box MAC arrives as a natively delivered frame on
  eth2, the kernel answers, and ping works. This needs only the rule that gives the bridge MAC a
  CPU destination (or a catch-all punt); it does not need multi-port.
  This is testable over SSH on the adopted baseline image (mirror kept on for management): a
  candidate rule can be added read-write via devmem, and once ICMP 98-byte frames appear as
  additional completions (besides the mirror's port-0 copies) the duplicate delivery is visible
  in RXDIAG without losing the box. A follow-up boot with cpu_mirror=0 proves it cleanly.
- M1b - multi-port. Once unicast-to-CPU works per port, the existing per-port netdevs + PIE port
  mapping give the software bridge all four ports; then the mirror can be removed. The same
  mechanism must punt LAN-to-LAN frames (unknown/broadcast always; known unicast if the hardware
  does not learn).

The completion-port field is already a usable identity for CPU-punted frames (port 11 observed).
So no new tag parser is required to reach M1a; the tag action work becomes relevant only if the
stock capture shows the vendor's CPU delivery uses a tag we must reproduce (or if per-port
destinations are used instead of per-frame port fields).

## 5. What E0-a proves (and what it does not)

Proven on hardware [V]:
- native exception punts exist without the mirror, with the correct source port (11);
- the exception class covers only the ARP/broadcast-size traffic in this configuration (no 98-byte
  unicast completions in 8 samples over 17 s with the host retrying ARP once per second);
- the boot, SMP, LEDs, event0 and link cycle are unaffected by disabling the mirror (the 12.9 s
  down / 38.9 s up pattern is the same bench link behaviour recorded before, not a new fault);
- disabling the mirror removes the only working IP path on this image: no unicast CPU delivery
  means no IP. The mirror workaround remains mandatory for the baseline.

Not proven / not assumed [U]:
- the meaning of the 0x25 reason bits (H1-H3 above);
- whether the missing unicast state is an L2/FDB row, an IFC entry, a port-map/OPP setting, or a
  combination (E0-b target);
- whether a catch-all punt rule is enough for LAN-to-LAN forwarding, or whether hardware FDB
  entries are needed for M1b.
