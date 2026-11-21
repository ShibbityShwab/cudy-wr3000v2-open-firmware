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
