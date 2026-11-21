# Minimal first experiment: zero-write, cmdline + read-only capture

Status 2026-11-21. **Nothing here writes a register from this directory.** The experiment is
designed to run on the currently verified bench image family (canonical release line + patch
058 config) and answers the two questions that decide the whole native-forwarding plan:

- **Q1:** with the CPU-copy mirror disabled, does an eth2 ingress frame still reach the CPU
  through the *existing* classifier/exception path? (If yes, a native CPU-delivery path already
  exists and only the ingress-port identity is missing.)
- **Q2:** which readable switch table words exist on the stock vendor boot, with traffic, that
  do not exist on our image? (If the vendor's runtime forwarding state is readable, replay it;
  if it is absent, design our own per-port entries.)

Both are answered without a single register write. The second one is pure read-only capture.

## Prerequisites (same as any bench session)

- Human at the bench; recovery door armed (tools/recovery-card.sh); UART capture running
  (tools/uart-capture.sh <absolute-path>); bench link per BENCH-FACTS.md.
- Current known-good image and its sha256 recorded (the canonical 2-CPU image family; the
  release notes name the exact current hash).
- A stock boot medium/pair available for the stock-side capture (the stock selector swap is a
  normal attended step; the stock image is not modified).
- Host-side: this repository at the recorded commit; the provision venv python available.

## E0-a - mirror-off cmdline experiment (Q1)

This needs **one image rebuild**: the attended gates are part of the built-in forced command
line (CONFIG_CMDLINE_FORCE), so the gate set cannot be changed at runtime (module parameters are
0444). Use the canonical release-line build tree (the same one that produced the current image),
change only the command-line string, rebuild, install.

1. Build variant N0-a with the canonical command line but: luofu_switch.cpu_mirror=0 plus
   luofu_pie.rxdiag=1 luofu_pie.accepterr=1 (all other canonical gates unchanged;
   luofu_pie.cpucopy_deliver=1 luofu_pie.cpucopy_port=11 may stay, it is inert without mirror
   copies).
2. Install attended with the existing tool:
   tools/stock-telnet-install.sh --apply --attended --recovery-armed <image> (read-only
   --inspect first; UART capture through the whole install/boot).
3. After boot: verify /proc/cmdline contains the new gate set; verify ssh/ping to the box.
4. Test: from the host, ping -c 20 <box> and check the UART/dmesg for:
   - pie-trace RXDIAG lines: class (status), length, port, vport, tag class;
   - whether ICMP replies still flow with the mirror off;
   - the RXCPUCOPY lines disappear (expected: no mirror copies).
5. Record: host ping loss, RXDIAG tuples, any luofu_pie/luofu_switch warnings.
6. Interpretation:
   - ping works + native completions with port = 11 and/or a tag class -> native CPU delivery
     already exists on eth2: the next task is per-port coverage and identity (Route B).
   - ping works only through exception-status completions (status != 0 with
     luofu_pie.accepterr=1) -> native delivery = exception path; identity must be added.
   - ping fails with no completions -> the mirror is the only CPU path today; proceed with the
     read-only capture (E0-b) and Route B after it.
7. Restore: reinstall the canonical image (or reboot into the stock/known-good pair) and
   re-verify ssh/ping. The mirror is back with the canonical cmdline.

## E0-b - read-only runtime capture, both images (Q2)

Run in one session, with the host pinging continuously so a bridge/flow table would be live:

On **our image** (ssh):
~~~
sh capture-runtime-state.sh --profile core --out /tmp/lswcap-ours > /tmp/lswcap-ours.log
sha256sum /tmp/lswcap-ours/* > /tmp/lswcap-ours.sha256
~~~

On the **stock image** (stock root telnet; boot the stock pair with the attended stock selector
procedure, run from the telnet shell; the script is transferred with the existing stock-telnet
transport or pasted). The full profile is about 10k single-word devmem reads and is slow
over telnet; if the session is tight, run core first and the full profile in the next attended
window:
~~~
sh capture-runtime-state.sh --profile full --out /tmp/lswcap-stock > /tmp/lswcap-stock.log
sha256sum /tmp/lswcap-stock/* > /tmp/lswcap-stock.sha256
# plus the vendor CFE views (stock only; these files do not exist on our image):
ls -R /proc/hsan > /tmp/lswcap-stock/proc-hsan-tree.txt 2>&1
for f in /proc/hsan/cfe/intf/dump_intf /proc/hsan/net/swa/rootif_dump /proc/hsan/net/swa/wan_port; do
    echo "== $f"; cat "$f" 2>&1; done > /tmp/lswcap-stock/proc-hsan-dumps.txt
dmesg > /tmp/lswcap-stock/dmesg.txt
~~~

Copy both capture directories to the host (scp/ssh for ours; the stock side uses the documented
telnet transport; the capture is text, keep the sha256).

Then on the host:
~~~
.provision-venv/bin/python diff-captures.py \
    --ours lswcap-ours --stock lswcap-stock --out lsw-forwarding-diff.md
~~~
The diff prints, per window: words non-zero on stock and zero/different on ours, classified as
**persistent candidate** vs **counter-like** (the counter pages known from W14/W15 are listed in
the script), plus any MAC-shaped words and any tag/VLAN-shaped words in the tagact/IFC/L2
regions.

**Acceptance for the session:** two capture directories with sha256, the diff markdown, and a
short note answering: (a) did the mirror-off boot still deliver eth2 ingress to the CPU (Q1);
(b) which stock-persistent words are absent on ours (Q2; expected first candidates: the
pub+0x54000 tag action table and IFC bank1/entries >8; L2 table entries if learning is on).
No write has happened at this point.

## Explicit "do not do" list for this experiment

- Do not write any switch register yet (E0 is read-only; the devmem capture only issues read
  forms).
- Do not remove the mirror for a port and then move the bench cable away from eth2 unless the
  UART capture is running and the mirror fallback for eth2 is guaranteed by driver policy:
  after a cable move the only observation channel is UART.
- Do not extend the mirror source mask as a multi-port solution - it is proven not to identify
  the ingress port.
- Do not point the NAPT engine at 0x80400000 (see the qm-decode addendum).

## Revision after the E0-a run (2026-11-21, host-side analysis)

(The hardware run record itself is the final section of this file; this section is the host-side
revision that follows from it.) Use `capture-runtime-state.sh --profile punt` for the revised
E0-b ranges (IFC banks, tagact, OPP/PA, L2-with-traffic).

The E0-a result was recorded on hardware and analysed on the host; the full correction is in
[E0A-ANALYSIS.md](E0A-ANALYSIS.md). Summary of the revised reading:

- The 60-byte status=37 frames are the host's ARP retries (Ethernet-minimum ARP), i.e. the
  already-known exception/punt class. With accepterr=1 that class is deliverable and parsed fine
  (proven on 2026-11-20 15:30: the box answered the ARP on the wire). status=37 / err=1 is a
  class marker, not a frame-integrity error: the vendor decode sets summary bit 0 only when
  status == 0, and our driver derives err from status != 0.
- What is missing is not the exception classification: it is unicast-to-the-box-MAC delivery
  (the 98-byte ICMP frames never appear in the ring) and, for multi-port, per-port punt of the
  other jacks. The ARP exception class already carries the correct source port (11), so the
  completion-port identity works for punts.
- The link down/up at 12.9 s / 38.9 s is the normal bench link pattern, not a new fault.
- The adopted hardware baseline is now image c3ef2a50 (supersedes f6685b80; 7421bb7a is the
  original verified baseline). The unit needs a physical power cycle to return to stock/pair A
  before the baseline image can be reinstalled.

### Revised E0-b (read-only, run on the baseline image and on stock)

The target is now the unicast-to-CPU / bridge-MAC state, not a generic classifier dump:

1. Stock runtime CFE state (primary). Boot stock with the host pinging (so its FDB/flows are
   live) and capture, read-only:
   - the /proc/hsan tree and the interface/FDB/flow dumps (the capture script already tries the
     known paths; extend with a directory listing and any dump files found under /proc/hsan/cfe
     and /proc/hsan/net);
   - dmesg;
   - the register set below on stock.
2. Readable switch tables on stock, with traffic:
   - IFC tab128 banks 0/1, all 128 entries (0x12150000 and 0x12160000, 4 words each);
   - tag action table, 64 entries (0x124D4000 + idx*16);
   - L2 window with traffic: 0x12180000+0x0..0x400, +0x40000..0x40400 and the other candidate
     pages (0x8000/0x10000/0x18000/0x20000/0x60000/0x70000), then search for the box MAC
     (bdinfo) and host MAC (00:e0:4c:63:17:58) bytes;
   - OPP 0x12400000+0x0..0x400 (foreground: CPU-copy/forward maps; capture the full first 1 KiB,
     not only the mirror registers);
   - PA 0x12080000+0x0..0x400 (the PA block's default/unknown-unicast and to-CPU settings);
   - PFE_GLB 0x12000000+0x0..0x100 (attribute words).
3. Our baseline image capture, same addresses, with the mirror ON and the host pinging; after the
   ARP exchange, confirm the host's neighbour entry for the box exists (ip neigh on the host) so
   the unicast phase is live during the capture.
4. Diff and decide:
   - if stock has readable persistent state that maps the bridge MAC / unknown unicast to CPU
     port 4 (IFC entry, L2/FDB row, or OPP/PA word), the first W21 write set is a replay of those
     exact words (snapshot, readback, restore-on-mismatch);
   - if nothing unique is readable, go to the controlled probe: candidate IFC/tagact writes via
     the write planner, with the mirror kept for management and rxdiag on, looking for 98-byte
     native completions beyond the mirror copies (M1a signal).

Acceptance: capture directories + diff report; a definite statement whether a readable
unicast-to-CPU state exists on stock; if not, the first probe write set and the exact RXDIAG
signature that would prove M1a.

New exact reads that can run over SSH on the adopted baseline image (read-only; devmem is on the
image) are listed in [UNKNOWNS-AND-READS.md](UNKNOWNS-AND-READS.md) section "Reads executable over
SSH on the baseline image".

## E0-a result (run 2026-11-21 on hardware)

Image 990c5cab (built from the adopted target DTS with cmdline
luofu_switch.cpu_mirror=0 i.e. mirror machinery disabled, plus
luofu_pie.rxdiag=1 luofu_pie.accepterr=1) was installed on the unit (pair b, selector
0x11) and booted.

Result: the unit boots fine (SMP, 2 CPUs, 6 LEDs, event0), but there is NO usable IP path:
both 192.168.1.1 and the stock LAN IP are unreachable from the host; the host link stays up
(1 Gbps). The UART shows:
- luofu-gmac 14480000.ethernet eth2: Link is Down at 12.9 s then Link is Up - 1Gbps/Full
  at 38.9 s (the host side comes up late, as before);
- repeated pie-trace RXDIAG ch=0 idx=0..7+ len=60 status=37 err=1 port=11 vport=0 vlan=0
  desc=04000000 0x8238..c2 00000000 ... - i.e. native-path completions DO arrive on
  port 11 (eth2) without the mirror, but every frame is marked status=37 err=1 and IP is dead.

Interpretation: native CPU delivery exists for the eth2 port, but the switch punts those frames
with an error flag (wrong destination/tag classification - the stock CPU-port/IFC/tagact
configuration is missing in our init), so they are dropped. The mirror workaround remains required
for the verified baseline. Next steps per IMPLEMENTATION-PLAN: E0-b read-only stock-vs-ours runtime
capture (tagact/IFC/classifier) to derive the missing CPU-punt classification, then W21 design.
The unit needs a physical power cycle to clear the selector cell and return to stock (per the
documented power-on semantics) before reinstalling the baseline image.
