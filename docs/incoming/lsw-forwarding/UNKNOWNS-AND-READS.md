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

## Status update 2026-11-21 (after E0-b: stock-vs-ours captured and decoded)

E0-b is done (ours image 6e9c54d0 vs vendor stock; 9,488 words each, 0 failures). Decode and the
reviewed M1a probe: E0B-ANALYSIS.md and m1a/. What it settles for this list:

- **U4 (L2 table / MAC learning state) - ANSWERED for the readable block:** the differing
  l2+0x40000 page words are L2 DFX counters, not a table. The vendor hi_pfe_dfx_dump_cnt string
  map decodes them as rx_total (+0), tx_total (+4), rx_drop (+8), tx_drop (+0x3c), port_move
  (+0xc), age_drop (+0x10), sport_clash (+0x14), search_ok_mc (+0x18), search_fail_mc (+0x1c).
  Stock 0x414/0x415 vs ours 0x148 are counter values, not port maps or MAC entries. The readable
  L2 config words (l2+0x100..0x18c) are identical on both sides; no L2 writes are proposed.
- **U14 (where the stock unicast-to-CPU state lives) - PARTIAL:** it is not in the captured
  readable set. The IFC table is identical to ours (no stock-only entries), the L2 page is
  counters, OPP/PA/EPS/DP/FAM/MCC config match, and the only stock-only config words are tag
  action entry 1 and pfe_glb+0x2c. The remaining candidate is the vendor CFE flow/L2 program in
  the DDR carveout (0x80400000 area), which cannot be read or replayed safely on our image.
- **U13/U11 (which action/Cpu-punt encoding to use) - PARTIAL:** tag action entry 1 is decoded as
  a tag DELETE action (add_type 0, del_type 2, no VLAN); tagact_id is referenced by L3-interface
  and multicast entries in the vendor code, not by readable IFC entries. The IFC/L3 action
  encoding needed for our own catch-all punt is still [U] (host-side decode still to do).
- **New U17-U19 below** cover the tagact reference, the stock procfs decoded dumps that should
  name it, and the DDR flow table as the remaining possible enabler.

### New unknowns after E0-b

| # | unknown | why it matters | read/experiment |
| --- | --- | --- | --- |
| U17 | what references tag action index 1 (L3-interface entry, multicast user-link, or a hardware default) | decides whether replaying entry 1 can have any effect at all | stock procfs decoded dumps: /proc/hsan/lsw/table/l3intf, /proc/hsan/lsw/table/tagact, /proc/hsan/lsw/table/ifc; host-side decode of l3intf_write/l3intf_dump field extraction |
| U18 | the decoded stock procfs tables (tagact, l3intf, ifc, glb_attr, flow_cnt, tocpu_cnt, drop_cnt, br_dump, cfe flow/mac) | cheapest way to see the vendor's CPU-delivery configuration with the vendor's own field decoders | stock boot, read-only, exact commands below; no register writes |
| U19 | the CFE flow/L2 program in the DDR carveout | the last remaining candidate for the real unicast-to-CPU state; cannot be replayed on our kernel (DDR is live Linux memory) | do not read/write DDR from our image; treat as out of scope unless a safe read-only method is agreed (e.g. stock-side capture only) |

### Next read-only stock capture (decoded procfs, no register writes)

The E0-b register capture did not include the vendor's decoded procfs dumps. On the stock boot,
with the host pinging (so the state is live), capture these (read-only, via the stock telnet
shell; add to capture-runtime-state.sh or paste manually):

    /proc/hsan/lsw/table/tagact          # decoded tag action entries (add/del fields, types)
    /proc/hsan/lsw/table/l3intf          # L3 interface entries incl. tagact_id (the reference!)
    /proc/hsan/lsw/table/ifc             # decoded IFC classifier rows (key + action fields)
    /proc/hsan/lsw/table/glb_attr        # up_port / flow_age_en decoded
    /proc/hsan/lsw/table/flow_cnt        # flow table counters
    /proc/hsan/lsw/pfe/tocpu_cnt         # "to CPU" counters
    /proc/hsan/lsw/pfe/drop_cnt          # drop counters
    /proc/hsan/cfe/intf/br_dump          # bridge/rootif state
    /proc/hsan/cfe/table/flow            # software flow entries
    /proc/hsan/cfe/table/mac_stats       # software FDB/MAC stats
    /proc/hsan/net/swa/                  # rootif/vlan filter views (already partly captured)

These decoded texts should state the tagact reference and the vendor's CPU-delivery configuration
directly; they are the natural input to designing our own IFC/L3 entry (the host-side decode of
ifc_write/l3intf_write is the companion task).

## Status update 2026-11-21 (after the M1a nulls and the stock decoded procfs capture)

The two M1a probes were run on hardware and both were NULL: the tagact word write was ignored
(wrote 0xc000 at 0x124d4014, read 0x0), and pfe_glb+0x2c was accepted/read back but eth2 rx delta
stayed 531 = mirror copies only. Words reverted, box healthy; the write experiments are closed.
A consistent-stock read-only procfs capture (hsan-stock/hsan2/, 137 files) followed. It resolves
the CPU-punt mechanism:

- **U14 (where the unicast-to-CPU state is) - ANSWERED: it is the NAPT/flow engine path.**
  Stock counters: ifc rx_total/hit=992/992, ifc_tocpu=104 (broadcast class ~ 103), ifc_napt=170;
  napt tx_total=170, up_tocpu=129 (unicast class ~ 130); dp port4 queue 274/274 = all CPU frames
  (uc130+mc41+bc103); pie ch0/ch1 rx = 191/83. So IFC defaults punt broadcast to CPU and route
  unicast to NAPT, whose up-direction punt sends it to CPU. Our image has the same IFC defaults
  but NAPT uninitialized (config words zero), which is exactly why the unicast class disappears
  before PIE (no completion) while ARP arrives.
- **U15 (completion status classes) - deferred:** the NAPT punt class is not observed yet; once
  the init replay works, RXDIAG/accepterr handling will show the class.
- **U13/U11 (IFC action encoding) - deferred:** not needed if the IFC defaults already route
  unicast to NAPT; the read-only counter test below confirms/refutes that first.
- **U17 (tagact reference) - no longer on the M1a critical path:** the entry is a tag-delete
  action referenced by the CFE rootif/L3 program (l3intf/ multicast entries in vendor code); it
  does not gate unicast CPU delivery. It remains a D2 (VLAN/offload) question.
- **New U20:** does the vendor NAPT init (register-only replay with the reserved switch flow
  window bases) alone make the up_tocpu punt work, or is more of hi_pfe_pub_init/flow-table setup
  needed? The m1a/napt-init prototype answers this.
- **New U21:** the two napt+0x60 clear commands are engine-managed table initializations; after
  the replay, do the flow tables stay valid across link events (like the mirror registers did
  not), or must the sequence be re-applied? Check after M1a proof.

### Exact read-only stock procfs invocations (and what they can/cannot reveal)

All of these are plain reads (cat) or, where marked, a proc command write that only triggers a
diagnostic dump (no register/config change). The captured hsan2 tree already contains the
decoded outputs of the first list; the command-based ones are the only way to get live traces.

Read-only cat list (the values that mattered here):
    cat /proc/hsan/lsw/pfe/cnt        # the pa/ifc/l2/napt/mcc/opp/pe counter decomposition
    cat /proc/hsan/lsw/pfe/tocpu_cnt  # ifc_point0/tocpu sub-counters
    cat /proc/hsan/lsw/pfe/drop_cnt
    cat /proc/hsan/lsw/dp/cnt         # dp rx/tx per port/queue and pie channel split
    cat /proc/hsan/lsw/table/ifc /l3intf /tagact /flow_cnt /ipv4_flow /ipv6_flow /glb_attr /cnt
    cat /proc/hsan/lsw/dp/vport /proc/hsan/lsw/queue/cnt /queue/dev_port
    cat /proc/hsan/net/eth2/cnt /proc/hsan/net/pie_cnt /proc/hsan/net/pie_chl_cnt
    cat /proc/hsan/net/pie_config /proc/hsan/net/swa/rootif_dump /proc/hsan/net/eth2/attr1
    cat /proc/hsan/cfe/hook/cnt /proc/hsan/cfe/intf/dump_intf /proc/hsan/cfe/table/mac_stats
    cat /proc/hsan/cfe/lrn/dump_lib

Command-based dumps (write a command line to the same node, then cat it; these are diagnostic
commands, not register writes):
    echo "0xffff 0xffff" > /proc/hsan/lsw/dp/diag ; cat /proc/hsan/lsw/dp/diag
        # usage decoded from hi_lsw_dp: "echo igr_bitmap egr_bitmap > diag" (hex masks)
        # prints the per-stage ingress/egress counters (ips/eqm/eps classes)
    hdb cnt get                        # vendor helper: cats all counter nodes (read-only)
    hdb info dump <tftp-ip>            # vendor helper: cats all /proc/hsan files into a log
    hdb flow trace -igr eth2 -dip <host-ip> -detail
        # writes "flow trace ..." to /proc/hsan/diagnose/flow/flow_trace; diagnostic only,
        # prints where the packet is seen (cfe/IFC/NAPT/LSW positions); note the helper also
        # runs "hdb cnt clr" and clears dmesg first - capture counters before using it
    hdb flow learn -igr eth2 -dmac <mac>   # learning-path diagnostic only
    hdb flow cnt -igr eth0 -egr eth2       # flow-counter diagnostic only

Important negative result: **there is no proc node that dumps the hardware IFC/L3/flow entries.**
The table files (ifc/l3intf/ipv4_flow/...) print only the software table-manager shadow, and on
stock that shadow is empty (alloc=0) while the counters prove the hardware classified 992 frames;
the classifier entries are direct hardware defaults (the eight W10 defaults) plus CFE flow state
in the DDR carveout, neither of which is exposed by these dumps. The decoded counter groups above
are the correct read-only way to see which class goes where; chasing table-dump command syntax is
not useful.

### Read-only diagnosis on our image (do this first, no writes)

m1a/napt-init/check-napt-path.sh prints the eth2 counters, the IFC counter block, the NAPT
counters and the NAPT config words. Run it, ping the box from the host, run it again:
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sh /tmp/m1a/napt-init/check-napt-path.sh'
    ping -i 0.02 -c 200 192.168.1.1
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'sh /tmp/m1a/napt-init/check-napt-path.sh'
Expected if the NAPT diagnosis is right: the IFC block's ifc_napt-class word increments by about
200, napt rx_napt/up_tocpu stay 0 (engine uninitialized), eth2 rx_packets rises only by the mirror
copies, and no new native delivery appears. That confirms the frames reach NAPT and die there,
which is what the m1a/napt-init replay is designed to fix.

## Status update 2026-11-21 (M1a PROVEN - the NAPT init replay delivered native CPU delivery)

The reviewed NAPT replay was run from a probe-only boot script on proof image 5800c9ab (canonical
adopted tree + command-line change only: cpu_mirror=0 luofu_napt_init=1 luofu_pie.rxdiag=1):

- boot 6.18.60, 2 CPUs, no Oops/BUG/panic; dmesg "luofu-napt-init: applied
  (up_port=0x10000034 bases=0x80400000/0x80408000)";
- with the mirror OFF: host ping 200/200 0% loss AND working SSH; UART RXDIAG native port=11
  deliveries with status=77 (the E0-a status=37 class is gone);
- NAPT registers read back the stock values.

So **U20 is answered: yes, the decoded hi_pfe_napt_init replay alone is sufficient** for the
eth2 unicast CPU delivery; the earlier "NAPT init alone will not produce native forwarding"
caution is superseded by the measured result (the replay includes the flow-table bases and the
v4/v6 clear commands, which is what the engine needed).

Implications for the remaining list:
- **U21 (persistence/re-apply)**: the proof image kept working through its boot (link came up
  after the replay). Whether the NAPT config survives link flap/reboot must be checked in M1b
  (m1b/M1B-PLAN.md, M1b-4). The mirror's post-link reset is the precedent; W22 has an idempotent
  guard so a re-apply can be added cheaply if needed.
- **U22 (completion status classes)**: the native punt class is status 77 on the proof boot
  (status 37 was the older IFC/ARP exception class under the mirror-era configuration). W22b
  delivers every non-zero status class with a valid decoded port by default (no accepterr token);
  the first deliveries are logged as RXEXC. If M1b sees other status values, the same rule covers
  them and the log records them - open only as a catalogue to collect.
- **U23 (LAN-to-LAN class coverage)**: does the punt deliver unknown-unicast/broadcast/LAN-to-LAN
  frames to the CPU, or only traffic to the box? This decides software-bridge sufficiency vs the
  hardware bridge; M1b-2/M1b-3 answer it.
- **U24 (W22 as kernel code)**: the proof used a userspace init script; the driver implementation
  (w22/ patches) compiles/checkpatch-clean host-side but has not run on hardware yet. The first
  W22 image boot is the remaining validation.
- **U25 (mirror retirement)**: mirror patches 054-057 stay in the release line default-off as a
  fallback. Retire the code only after a W22-only soak; the fallback command line is in
  w22/RELEASE-INTEGRATION.md.

## Status update 2026-11-21 (full switch/CFE reversal phase opened)

A new read-only reversal phase started with two new enablers: (a) the switch DDR carveout is
readable via /dev/mem on both kernels, (b) the vendor rootfs is extracted at
/home/shibbityshwab/recovery-extract (the hi_* modules there are byte-identical to the pinned
fixtures, so all earlier disassembly lanes apply), plus U-Boot dumps, recovery.bin and the hsan2
procfs capture. The goal is the full CFE flow/L3/tag/MCC/queue model and an optional M2 offload
design (not needed for the release line).

New items:
- **U26 (flow table DDR layout) - GEOMETRY ANSWERED, entry layout open**: the our-image W22 dump
  (OUR-SIDE-VALIDATION.md) proves the exact 1 MiB partition: v4 buckets 32 KiB (8192x4B) at
  napt+0x130, v4 entry arena 256 KiB (8192x32B) at +0x138, v6 buckets 32 KiB at +0x134, v6 entry
  arena 704 KiB (8192x88B) at +0x13c; W22 clear writes 0x017be000 into the bucket arrays only.
  The field layout inside an entry slot is still open - it needs a controlled single-flow insert
  probe (stock is inaccessible: vendor /dev/mem returns Bad address, no bootargs env, no debug
  kernel). v4/v6 manager sizes 8224/8208 = 8192 + reserved.
- **U27 (l3intf hardware encoding)**: the pub+0x5b000/0x5c000 banks are readable; the dump
  validates the static field map and the tagact_id reference in stock entries.
- **U28 (tagact references)**: which l3intf entry references tagact id 1 (the stock tagact table
  has exactly one entry) and what action it describes.
- **U29 (MCC/userll/ext + multicast CPU-copy wiring)**: mcc+0x44000 userll words and the
  userll_ext table location; multicast class coverage for M1b/M2.
- **U30 (queue window layout)**: W7 decoded the EQM table ids/formulas; the dump gives the
  in-window layout and the CPU port-4 queue under punt traffic.
- **U31 (status-class catalogue)**: status 0 / 0x25 / 0x4d observed; whether other classes exist
  for multicast/IPv6/WoC is a traffic-matrix question for the same dump.
- **U32 (CFE userspace side)**: recovery-extract has the vendor rootfs but no obvious CFE
  userspace binary with the flow/tag strings; the kernel modules remain the primary source. If
  the lead locates the CFE daemon binary, its netlink attribute names would confirm the ADP API.
- **U33 (stock read access)**: the vendor 5.10 kernel has no usable /dev/mem (Bad address for
  carveout/windows/control page) and no editable boot environment; the stock runtime CFE tables
  are inaccessible unless a vendor debug kernel or another privileged read path appears.
- **U34 (M2 verdict)**: after the our-side dump M2 is deferred: M2-A flow offload needs a
  controlled insert probe to close the 32B/88B entry-slot layout; M2-B bridge/VLAN needs a bench
  probe matrix for the write-only IFC action engine; M2-C needs the GPL CFE userspace. Nothing in
  M2 blocks the release line.

## U12 - completion of the identity path (success criterion)

M1 is met when, for all four wired ports, a frame from the bench host (cable on that jack)
produces a native RX completion that the driver maps to the correct netdev, with the mirror off
for that port, and the Linux bridge/router works (ping to the box from each LAN jack; WAN
routing tested with the WAN-side peer available). Record: RXDIAG tuples, netdev counters,
bridge FDB, ping results, UART log.
