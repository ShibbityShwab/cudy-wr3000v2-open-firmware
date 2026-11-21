# E0-b decode and the reviewed M1a write proposal (host-side, 2026-11-21)

Status: host-side analysis and proposal only. No device write has been made by the author of this
file; the write scripts in m1a/ are prepared and self-tested on the host, to be run only at an
attended bench session by the lead/human, with the recovery door armed.

Evidence (gitignored):
- ours: build/evidence-bench-gates/e0b-ours/lswcap-20261117T202155/ (image 6e9c54d0, 6.18.60,
  2 CPUs, selector 0x11, bootflag b, 9,488 reads / 0 failures; unique words 8,432);
- stock: build/evidence-bench-gates/e0b-stock/lswcap-stock/ (vendor 5.10.201, pair a, bootflag a,
  boot_reg 0x10; regs.txt sha256 f1b627674e208bf4d4d843864f33bf7fdb1d39cb4aa4e0621575808a81744a85;
  capture.tar.gz sha256 2e2475dbd23eaad7a6ece7bf097146fdb497d2849651152fe2ea6b25e72fa14a);
- diff outputs /tmp/e0b-diff-out and /tmp/e0b-diff.json (30 candidate rows, 72 counter rows, 1 note);
- lead summary: E0B-RESULTS.md; measurement run: capture-runtime-state.sh --profile punt.

## 1. What the 107 differing words are

The diff classified the common captured words as: 21 new-on-stock + 9 diff-on-stock candidates,
72 counter-like rows, 5 ours-only rows (the reported "30 candidates" excludes the ours-only set).
Breakdown by window:

| window | differing words | what they are (decoded below) |
| --- | --- | --- |
| l2 (page l2+0x40000) | 72 (counter-classified) | L2 DFX counters, not config - never replay |
| napt | 11 | flow engine config + DDR carveout pointers - forbidden |
| eqm | 10 | indirect engine command/data residue - not config |
| pie | 8 | our PIE driver's ring config vs vendor driver - not LSW forwarding |
| pub (tagact) | 1 | stock tag action entry 1 - candidate A |
| pfe_glb | 1 | up_port/flow-age attribute - candidate B (flow, not L2 CPU) |
| opp/mcc | 5 (ours-only) | our mirror workaround and MCC init - expected |

No differences exist in the ifc, opp/pa config (other than our mirror), dp, eps, fam, mcc
config or pfe_glb low16 - so the missing unicast-to-CPU state is NOT an extra readable IFC entry.

## 2. Candidate A: tag action entry 1 at 0x124d4010

### 2.1 What wrote it (writer fully decoded: hi_lsw_pfe!tag_write@0x2eb0)

tag_write(index, descriptor) builds a 16-byte hardware entry and writes it with
hi_lsw_write_tab128(synth 0xb054000 + index*16, entry). The recovered construction is:

- the entry is zeroed; entry byte 5 (word 1 bits 8..15) is preset to 0x80;
- descriptor byte 8 carries two 2-bit type fields: add_type = desc[8] & 3,
  del_type = (desc[8] >> 2) & 3;
- selector = add_type when del_type == 0, else del_type + 2; entry byte 5 becomes
  0x80 | (selector << 4);
- if add_type != 0 the function also fills the add-tag fields:
  word0 bits 0..11 = svlan (desc[0..1] & 0xfff), bit 18 = 0x40000 (add-tag flag),
  bits 12..14 = (desc[1] >> 5) (priority/class), bits 16..17 = tpid index + 1 of desc[2..3];
  word1 bit 0 = 1 for the single-tag path;
- for the double-tag variant (desc[8] & 3 == 2) word0 bits 20..31 = cvlan (desc[4..5] & 0xfff),
  word1 byte 0 bits 2..3 = tpid2 index + 1, byte 1 bits 0/2 set (0x05) plus the selector;
- tag_dump then prints exactly these fields: add/del type, stpid, svlan, spri, scfi,
  ctpid, cvlan, cpri, ccfi - i.e. one 802.1Q S-tag/C-tag add or delete action.

For the stock captured content (w0=0, w1=0x0000c000, w2=0, w3=0):

- word1 byte 1 = 0xc0 = 0x80 | (selector << 4) -> selector = 4 -> del_type = 2, add_type = 0;
- word0 = 0 -> no svlan/priority/TPID, no add tag; word1 bit 0 clear -> not the add-tag path;
- all other fields zero.

So **tag action entry 1 is a TAG DELETE action of type 2 with no VLAN/TPID parameters** (the
descriptor CFE wrote was desc[8] = 0x08 and the rest zero). The exact meaning of delete-type 2
(S-tag, C-tag or both) is [U]: the kernel only prints the numeric type; the vendor userspace enum
is not in the recovered strings. Hypothesis [I]: the types are 1 = outer/S-tag, 2 = inner/C-tag,
3 = both - so type 2 is most likely "delete the inner/C tag".

### 2.2 Is it the CPU-punt / exception-class action? No, not by itself

A tag action is not a classifier/punt action. It is referenced by an action id (tagact_id) from
other tables. The recovered vendor code prints tagact_id in:

- l3intf_dump: ",tagact_id=%u" - the L3 interface table references a tag action;
- pfe_mcc_dump_user_ll: ",egr=%u,tagact_id=%u,next_id=%u" - multicast user-link entries do too.

The IFC dump has no tagact_id (its action fields are id, igr_bitmap, label, map_pri, eof, pq,
pq_en, car_id, car_en, statis_idx), and the captured IFC table is identical on both sides, so no
readable classifier entry references it on stock either. The reference almost certainly lives in
the vendor CFE runtime state that our kernel does not recreate (L3-interface entries and/or the
CFE flow table in the DDR carveout, which was not and cannot be safely captured/replayed).

Consequence: replaying the entry is a hypothesis test, not a known enabler. Hypothesis H-T
(unproven): the vendor's rootif/L3-interface entries reference tag action 1 on the CPU/routed
path (e.g. to strip an internal tag before the CPU sees the frame); with our image lacking those
entries, the entry alone should be inert. If H-T is somehow wired in hardware defaults, the
observable would be a change in the exception-class (status=37) CPU deliveries - status, tag
class/vlan or a new duplicate delivery - not a silent no-op.

## 3. Candidate B: pfe_glb+0x2c = 0x10000034 (stock) vs 0x34 (ours)

Writer decoded: hi_lsw_pfe!write_glb_attr(attr, data):
- attr == 0: read pfe_glb+0x2c, bfi(data, bits 16..31), write back -> high16 = data;
- attr != 0: read napt+0x54, bfi(data bit 0, bit 8), write back -> napt+0x54 bit 8 = data.

Reader decoded: hi_lsw_pfe!dump_glb_attr reads pfe_glb+0x2c and napt+0x54 and prints
"up_port=0x%x,flow_age_en=%u" with up_port = high16 and flow_age_en = napt+0x54 bit 8.

So the stock word is: up_port = 0x1000 (bit 12 = WAN port 12, the flow-engine uplink port bitmap),
flow_age_en = 1 (napt+0x54 = 0x100; the NAPT window is refused by the write planner).

This is a **flow/NAPT engine attribute, not an L2/CPU-delivery control**. Replaying the high16 is
exact and revertible, but it cannot make unicast frames reach the CPU; it only tells the flow
engine which port is the uplink if/when flows exist. It is included as a control probe that
removes the only other stock-only config delta in the allowed windows.

## 4. The L2 0x414/0x415 words are DFX counters, not port maps or MAC entries

Vendor hi_lsw_pfe!hi_pfe_dfx_dump_cnt reads the L2 counter block at synth 0x01040000
(physical 0x121c0000 = l2+0x40000) and prints each value with its name. Resolving the string
relocations of that function gives the exact offset map for the first counter group:

| l2+0x40000 offset | counter name |
| --- | --- |
| +0x00 | rx_total |
| +0x04 | tx_total |
| +0x08 | rx_drop |
| +0x0c | port_move |
| +0x10 | age_drop |
| +0x14 | sport_clash |
| +0x18 | search_ok_mc |
| +0x1c | search_fail_mc |
| +0x3c | tx_drop |

The observed differences are at exactly such counter words in that page: 0x121c0000/+4 etc.,
stock 0x00000414/0x00000415 (1044/1045) versus ours 0x00000148 (328); the page contains 0x40-byte
replicated groups of the same counter set and the remaining differing words (+0x23c, +0x30c...
+0x3c/word) are further counter/status state in the same block, not table rows.

Conclusion: the page is runtime statistics. It is not a forwarding table, contains no port maps or
MAC entries, and must never be replayed (writing counters is undefined and the readback contract
would be meaningless). The wait for "large L2 differences" is therefore answered: **there is no
stock-only L2 config delta in the captured set**; the readable L2 config words (l2+0x100..0x18c)
are identical on both sides.

## 5. Excluded differences (and why)

| window | words | classification | why excluded |
| --- | --- | --- | --- |
| eqm 0x11200048..0x70 | 10 | indirect engine command/data registers | the recovered EQM write path uses eqm+0x48 (command) and eqm+0x4c/+0x5c/+0x60.. (data); the captured values are the residue of the last table operation, not configuration. Replaying them would issue a stray table command (dangerous) and they cannot be verified by readback (write-only engine interface). |
| napt 0x12280050..0x170 | 11 | flow engine config + DDR carveout pointers | the table base words are 0x80400000/0x80408000/0x80448000/0x80450000, i.e. the vendor's DDR flow area; on our kernel that range is Linux memory, so enabling the flow engine would DMA into live kernel memory. flow_age_en (napt+0x54 bit 8) is in the same window. The write planner hard-refuses NAPT by name. |
| pie 0x10a73500..0x3570 | 8 | PIE RX per-channel ring size/config | these are our own PIE driver's ring registers (driver: LUOFU_PIE_RX_SIZE_OFF = 0x3504 + ch*0x10; stock vendor PIE driver configures the same block with its own ring layout). They are not LSW forwarding state and replaying stock values would break our rings. |
| l2 l2+0x40000 page | 72 | L2 DFX counters | decoded counter names above; runtime statistics, never written. |
| opp 0xc0/0xd8/0xdc, mcc 0xc, eqm 0x4c | 5 | ours-only | our CPU-copy mirror workaround (W19/W20) and our MCC init; expected and required for the working baseline. |

Within the 30 candidate rows (21 new-on-stock + 9 diff-on-stock) the split is: 8 pie rows,
10 eqm rows, 11 napt rows, 1 pub tagact row, 1 pfe_glb row - i.e. exactly two candidate words
remain after the decode: the tag action entry and the up_port attribute. No IFC, OPP, PA, EPS,
DP, FAM, MCC-config or PFE-GLB low16 differences exist.

## 6. The reviewed M1a write set

Minimal set (two independent probes, 5 words total, all in allowed windows, all with rollback):

| probe | words | content | justification | expected |
| --- | --- | --- | --- | --- |
| A: tagact entry 1 | 0x124d4010=0x00000000, 0x124d4014=0x0000c000, 0x124d4018=0x00000000, 0x124d401c=0x00000000 | exact stock content | only stock-only config-like table entry; writer decoded (tag_write, desc[8]=0x08 -> tag delete type 2); harmless if unreferenced; tests H-T | most likely null; if referenced, a change in CPU-delivered frames (status/tag class/duplicate delivery) |
| B: pfe_glb+0x2c | 0x1200002c=0x10000034 | exact stock value (RMW of high16 = up_port 0x1000) | decoded writer/reader; removes the only other allowed-window stock-only delta; flow-engine control | null for L2/CPU delivery; flow attribute only |

Explicitly NOT in the set: L2 counter page, NAPT window (including flow_age_en), EQM command/data
registers, PIE ring words, and the DDR flow table. The generated scripts use the fail-closed
plan-writes machinery: read-only plan preview, snapshot of the complete rollback, preflight check
that every target still reads its captured rollback value, per-word readback verification,
automatic partial rollback on mismatch, and a --revert mode that restores the captured rollback
file. The planner refuses NAPT and L2 windows by name and the whole DDR/CRG range by allow-list.

Artifacts (all host-side prepared, none executed against a device):

- m1a/probe-a-tagact.plan, m1a/apply-probe-a-tagact.sh (4 writes);
- m1a/probe-b-glbattr.plan, m1a/apply-probe-b-glbattr.sh (1 write);
- m1a/observe-m1a.sh (read-only snapshot: eth2/br-lan counters, decoded L2 counters, tagact,
  pfe_glb+0x2c, mirror registers, dmesg);
- m1a/SESSION.md (the exact attended runbook, including a pre-armed 90 s auto-revert).

Host validation already done: both generated scripts pass shell syntax; a fake-devmem harness
verified preflight, write, readback, revert, preflight-mismatch abort (exit 8) and the
missing-flags refusal (exit 5); the delayed-revert path was tested after a two-second gap.

## 7. Expected observable and how it is measured

Positive M1a signal (with the mirror still on): for a fixed controlled ping (e.g. 500 ICMP
requests), the box's eth2 receives the mirror copy of every request. If a probe additionally makes
the switch punt the unicast request natively, the box sees two deliveries per request and
eth2 rx_packets increases by about 2x the sent count (plus a small ARP baseline). The observer
script records eth2 rx_packets/rx_bytes/rx_dropped/rx_errors, so a before/after delta is enough -
no dmesg trace gate is needed (the canonical baseline command line has no rxdiag gate and the
boot-time traces are exhausted).

Secondary observations: l2 counter deltas (read-only; they show whether the traffic reached the
L2 block differently), dmesg Oops/BUG/panic count, ping loss to the box, mirror register values
(re-applied by the driver every 2 s) and SSH liveness. If a positive signal appears, the proof
run is a boot of the same release-line image with cpu_mirror=0 (the E0-a build recipe), attended,
with UART capture: ping/ICMP unicast must then work without the mirror.

Null result interpretation: neither word is referenced/enabling in our state; the stock-only
runtime state that actually makes unicast reach the CPU is not in the captured readable tables
(most plausibly the vendor CFE flow table in DDR). Then no further register writes are attempted
until the reference/encoding is decoded host-side and the stock procfs decoded dumps are captured.

## 8. Risks and mitigations

| risk | severity | mitigation |
| --- | --- | --- |
| tag action is referenced by a hardware default and misconfigures CPU-port tagging | medium | script readback + auto-revert after 90 s even if SSH dies; mirror stays on so management cannot be lost by tagact alone in the expected (unreferenced) case; abort on any ping/SSH loss; recovery door armed |
| up_port write changes flow-engine direction handling | low | no flows/NAPT are active on the image; one word, readback, revert; the flow_age_en half (NAPT window) is not written |
| a write silently perturbs the L2/mirror path | low | full snapshot/rollback for every word; mirror registers are re-applied by the driver; observer snapshots before/after |
| claim inflation: calling the probe an "M1a enabler" | medium | this document labels both probes as hypothesis tests; a null result is the expected outcome and is recorded as a negative |
| unreviewed build/attended requirements | - | every script requires LSW_WRITE_ATTENDED=1 and LSW_RECOVERY_ARMED=1, prints the plan with no arguments, and is copied/run only in the attended session |

## 9. Exact commands (short form; full runbook in m1a/SESSION.md)

On the host, from the repository root (attended, UART capture and recovery armed first):

    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh 'mkdir -p /tmp/m1a'
    tools/rscp.sh m1a/observe-m1a.sh m1a/apply-probe-a-tagact.sh m1a/apply-probe-b-glbattr.sh root@192.168.1.1:/tmp/m1a/

Baseline (read-only): run observe-m1a.sh, ping -i 0.02 -c 500 192.168.1.1, run observe-m1a.sh
again, diff the two snapshots (eth2_rx_packets delta and l2 counter deltas).

Probe A (after reviewing the plan print):

    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "nohup sh -c 'sleep 90; cd /tmp/m1a && LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-probe-a-tagact.sh --revert' >/tmp/m1a/auto-revert-a.log 2>&1 &"
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "cd /tmp/m1a && LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-probe-a-tagact.sh --apply"
    ping -i 0.02 -c 500 192.168.1.1
    ROUTER_HOST=root@192.168.1.1 tools/rsh.sh "cd /tmp/m1a && LSW_WRITE_ATTENDED=1 LSW_RECOVERY_ARMED=1 sh apply-probe-a-tagact.sh --revert"

Probe B: same sequence with apply-probe-b-glbattr.sh (one write).

## 10. What remains untested / unknown after this session

- [U] whether any hardware path references tag action 1 on our image (no readable reference; the
  vendor reference is expected in L3-interface or multicast entries, absent here);
- [U] the exact delete-type enum (2 = inner/C-tag is a hypothesis);
- [U] the real unicast-to-CPU state: the only remaining candidate is the vendor CFE flow/L2
  program in the DDR carveout, which cannot be read or replayed safely;
- [U] the IFC/L3 action encoding needed to program our own catch-all CPU punt (host-side decode
  still to do; ifc_dump action fields are decoded by name only);
- untested on hardware: everything in section 8/9; no write was executed by the analyst.

Next read-only step (cheap, on stock, no writes): dump the vendor decoded procfs tables that our
capture did not cover - /proc/hsan/lsw/table/tagact, l3intf, ifc, glb_attr, flow_cnt, and
/proc/hsan/lsw/pfe/tocpu_cnt, drop_cnt, plus /proc/hsan/cfe/intf/br_dump and the cfe flow/mac
tables. Those decoded dumps should name the tagact reference and the CPU-delivery configuration
directly, avoiding further blind writes.

## 11. M1a probe results (both NULL) and the decoded CPU-punt path

The reviewed probes from section 6 were run on hardware (2026-11-21, adopted baseline image,
mirror on, attended):

- probe A (tagact entry 1, 4 words): the write at 0x124d4014 was **silently ignored** - wrote
  0xc000, read back 0x0. The script reverted the other three words. This confirms that the pub
  tag action table has no usable CPU write path through the mapped window (contrast: the IFC
  tab128 writes were readback-verified in W10 through their own path).
- probe B (pfe_glb+0x2c = 0x10000034): write accepted and read back, but eth2 rx delta stayed 531
  = mirror copies only. As decoded, up_port is a flow-engine attribute and not a delivery enabler.
- Box healthy, all words reverted.

The write experiments are closed. The subsequent consistent-stock read-only procfs capture
(hsan-stock/hsan2/, 137 files) provides the decisive counter decomposition:

- ifc rx_total=992, ifc_hit=992, **ifc_tocpu=104**, **ifc_napt=170**;
- napt tx_total=170, **up_tocpu=129**, sip_chk_tocpu=4, mc_fail_tocpu=37; pa cpu_spec_send=718;
- dp port4 queue in/out=274/274; pie ch0 rx=191, ch1 rx=83 (sum 274 = eth2 rx 274 = uc130/mc41/bc103).

Reading: **broadcast/ARP-class frames are punted to CPU by the IFC default rules** (104 ~ the 103
broadcasts), and **unicast frames are routed by IFC to the NAPT/flow engine, whose "up" direction
punts them to CPU** (129 ~ the 130 unicasts). Our image has the same IFC defaults (W10, verified)
but the NAPT engine is uninitialized (all config words zero), so the unicast class is dropped
inside the flow engine - exactly the E0-a/E0-b observation of "ARP arrives, 98-byte unicast never
does". The tag action entry is not on the critical path (it belongs to the CFE rootif/L3 program).

The missing initialization is therefore **hi_pfe_napt_init@0x4314** (vendor PFE module): a
register-only sequence that programs the flow table bases inside the switch's reserved DDR
carveout (0x80400000/1 MiB; the Linux memory node starts at 0x80500000, so no Linux page is
reachable) and issues two table-clear commands through napt+0x60. This is decoded and proposed as
the next reviewable step in m1a/napt-init/ (NAPT-INIT-PROPOSAL.md, apply-napt-init.sh,
observe-napt-init.sh, check-napt-path.sh). The write planner's default NAPT refusal is kept; the
prototype uses its own explicit 13-address allow-list with the same fail-closed rules.

## 12. Evidence map

| claim | artifact |
| --- | --- |
| tag action entry 1 stock content | E0-b regs (0x124d4014 = 0x0000c000), /tmp/e0b-diff-out note |
| tag action writer/descriptor layout | fixture hi_lsw_pfe!tag_write@0x2eb0, tag_dump@0x300c; annotate.py lane 2026-11-18-lsw |
| tagact_id references | fixture l3intf_dump, pfe_mcc_dump_user_ll (string xrefs) |
| pfe_glb+0x2c = up_port + flow_age_en | fixture hi_lsw_pfe!write_glb_attr@0x2210, dump_glb_attr@0x218c |
| L2 page = DFX counters and names | fixture hi_pfe_dfx_dump_cnt, resolved string relocations |
| EQM registers are indirect engine residue | LSW-SPEC section 6/7; hi_eqm_write_tab/hi_eqm_read_tab fixture |
| PIE 0x3500 block is our ring config | luofu-pie.c LUOFU_PIE_RX_SIZE_OFF; PIE-DATAPATH |
| NAPT DDR pointers / refusal | LSW-SPEC section 8/NAPT; plan-writes.sh refusal test |
| write scripts fail-closed and tested | m1a/apply-*.sh; host fake-devmem apply/revert/mismatch/refusal tests |
