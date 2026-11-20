# incoming/qm-decode - LSW data path: W7 armed, PFE base init (patch 036)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 4).  Prior record: patch `032`
revisions 1-3 (EQM queue-manager init + tiered bench-proven verification), already integrated.
**Status:** round 3 on hardware (image 75868fe0) armed the full data path for the first time -
readiness true, GEMAC netdevs registered, eth2 link up 1 Gbps, br-lan forwarding - but **no frame
crossed the switch**: zero PIE RX polls, no box frames on the wire, PIE TX descriptors consumed.
This package delivers patch `036`: the recovered **PFE forwarding-engine base init** (W8) plus
the W7 tier-counter fix, revalidated host-side over the 001-036 series.  No device access in this
lane; publication remains NOT READY.

Apply `shared.patch` from the `opensource/` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/036-net-lsw-pfe-init.patch` | sixth attended gate `luofu_lsw.pfe=1`: the recovered PFE base-init writes (pe+0x400=2, pfe_glb+0x2c = read-modify keep bits 15..0, pfe_glb+0x204=1, opp+0x110=0) with snapshot / stability / computed value / readback / restore-on-mismatch, readiness requiring W3-W8; plus the W7 tier-counter fix | `274f09ae45c73955bd44ef6a8c7beb1a42f6621dbd5647a5575336162f1daff9` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | `docs/LSW-SPEC.md` (new 7.10 W8 section + 12.3 W8/W9 rows), `docs/SKELETON-SMOKE.md` (round-3 evidence, W8 stage, remaining gaps, attended sequence), `target-skeleton/README.md` (036 row) | `531e192dc6a8795ba2e4a6676d6727b5fad564ca19f8e430f16ea54102fed636` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy of this record | - |

## 2. Round-3 hardware result and the two findings

Image 75868fe0, command line `... luofu_switch.arm=1 egress=1 qidmap=1 eps=1 qminit=1` plus
`luofu_pie.arm=1`: the revision-3 tiered W7 verification passed on silicon and the driver
published readiness for the first time (log line from the pre-fix build):

```
[2.577673] W7: 169 EQM table entries (845 words) programmed: 0 verified on bench-proven vendor-read fields (tables 32/35/17), 168 acknowledged with diagnostics (tables 16/34; 0 differed on a vendor-read field), 1 write-only acknowledged (table 19); queue ports 9, qids 72, scheduler profiles 10
[2.607002] connectivity path armed: ... the GEMAC netdev front end may register
... luofu-gmac: eth0..eth3 registered (PIE TX armed, LSW egress map ready); eth2 link up gmii 1000Mbps full-duplex
... br-lan: port 1(eth2) entered forwarding state
```

But **no frame moved**: the host AF_PACKET sniffer saw only its own ARP requests, the PIE RX
poll/interrupt trace never fired (zero RX poll lines), and the PIE TX descriptors were consumed
(doorbells accepted, TX read pointer advancing) without a wire frame.

Findings:

1. **Tier-count bug (reporting only).**  The line said "0 verified ... 168 acknowledged" although
   all 91 STRICT entries (9 table-32 + 72 table-35 + 10 table-17) had passed their bench-proven
   masks (0 differed).  Revision 3 incremented the diagnostic counter for every masked entry.
   Patch 036 counts STRICT-passing entries as verified (91) and only tables 16/34 as diagnostic
   (77); the comparison logic itself was correct.
2. **The data path still needs the PFE engine init.**  The DP/EQM/EPS init makes the software stack
   believe the path is armed, but the vendor probes **hi_lsw_pfe** after the DP module and programs
   the forwarding-engine globals before its CFE/rootif layer is usable.  That layer had never been
   written by our driver.

## 3. Decode: the PFE base init (pinned hi_lsw_pfe.ko)

Fixture: `rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_pfe.ko`, 52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a` (host-only disassembly; no vendor
binary added).

`hi_pfe_probe@0x23a4` registers the ten PFE windows (id map 0=ifc 1=l2 3=napt 5=mcc 6=opp 7=pa
8=pe 9=pfe_glb 0xa=prbs 0xb=pub), enables its clock, pulses its resets, then runs
`hi_pfe_init@0x2278` -> `hi_pfe_pub_init@0x3400` -> `hi_pfe_opp_init@0x271c` -> mcc -> napt ->
`hi_pfe_ifc_init@0x794`.  The whole **direct-register** part of that base init is four words:

| write | value | evidence |
| --- | --- | --- |
| pe+0x400 | 2 | hi_pfe_init@0x229c |
| pfe_glb+0x2c | read, write back (value & 0xffff) | hi_pfe_init@0x22a8..0x22bc; write_glb_attr@0x2210 writes bits 31:16 |
| pfe_glb+0x204 | 1 | hi_pfe_init@0x22cc |
| opp+0x110 | 0 | hi_pfe_opp_init@0x2750 (after the port qid-map walk) |

The rest creates software table managers.  `hi_pfe_napt_init`/`hi_pfe_mcc_init` program NAPT/MCC
tables (not needed for plain L2).  `hi_pfe_ifc_init` writes the **IFC default-classification
table** through `hi_lsw_write_tab128`: 16-byte entries at `ifc+0x50000 + index*16` (index 1, 2,
3, ...), built from 12-byte descriptors plus a table-manager allocation.  That table is decoded at
the call-shape level only; its entry fields are [U], so it is **not** in the W8 write set yet - it
needs its own attended verification like every other table.

## 4. Patch 036 (W8) implementation

- New module parameter `luofu_lsw.pfe=1`, required to be combined with
  `arm=1 egress=1 qidmap=1 eps=1 qminit=1` (a gate without `arm=1` is an explicit -EINVAL refusal).
- The four words are snapshotted, each required stable across two reads, the expected value is
  computed (two vendor constants, one vendor read-modify-write keeping bits 15..0, one zero), every
  word is written and read back, and the snapshot is restored on the first mismatch (also from
  `remove()`).  The direct registers behave like the W4 words (exact readback); the write shape is
  [V] from the fixture and the field semantics are [U].
- `luofu_switch_ready()` now requires W3+W4+W5+W6+W7+W8: the GEMAC front end only registers with the
  PFE base init in place.
- Patch 036 also carries the W7 tier-counter fix described above.

## 5. Remaining gap layers (ordered; the next patches)

1. **IFC default-classification table** (`ifc+0x50000`, 16-byte entries from
   `hi_pfe_ifc_init`) - the most likely reason a frame has no forwarding decision at all.
2. **L2/VLAN tables** (`l2`, id 1) and the CFE/rootif contract (`hi_kcfe_pub`, `hi_kcara`,
   `hi_swa`) - how the vendor software bridge learns and what it programs per flow.
3. **Per-port MAC configuration** - our GEMAC driver writes only the measured 1 Gbps link words;
   the vendor `hi_mac_hw_config` full write set (MTU, pause, RX/TX enable) has not been compared
   against it yet, and either wire direction could still be disabled there.
4. NAPT/MCC init words (not needed for plain L2), table 38 / VP class (already documented as
   excluded in the 032 record), and the PIE per-port queue handle (zero in the vendor build; its
   hardware effect is still [U]).

## 6. Validation (isolated clone under build/linux-kbuild/waves/2026-11-20-r4-armed/, no device)

- **Series apply (001-036):** pristine `linux-6.18.60` + all 36 patches in order: **36/36 applied,
  0 failed hunks, 0 .rej/.orig**, drivers byte-identical to the lane work tree
  (`logs/10-series-apply-001-036.log`).
- **Isolated clone prepare/compile:** reflink clone with patches 035+036 added (shared clone and
  `openwrt-lab-selectorfix-img` untouched): `make target/linux/prepare` **RC=0** and
  `make target/linux/compile` **RC=0** with `CONFIG_WERROR=y` and **zero `warning:`/`error:` lines**
  (`logs/20-…`, `logs/21-…`); log hashes in `EVIDENCE-SHA256SUMS.txt`.
- **checkpatch:** 0 errors, 0 checks, only the expected "missing commit description" warning
  (`logs/11-checkpatch-036.log`, sha256
  `8632cd2b45ea77883aec8d6c63882b29d0426ff26cf3c44c75a4fcd8697a2d6e`).
- **DTS/binding/config:** patch 036 changes no devicetree, binding or Kconfig/config symbol.

## 7. Proof vs unverified

**Proven on hardware (round 3):** the W3-W7 write set arms the software data path (readiness, GEMAC
registration, 1 Gbps link, br-lan forwarding); the revision-3 tiered verification passes on silicon
(91 STRICT entries, 0 differed); the fail-closed design reports its counters honestly once fixed.
**Unverified:** whether the PFE base init reopens the wire (next run); everything in section 5;
the hardware effect of the PIE per-port queue handle; RX delivery.  A W8 boot still means "the
vendor's PFE base init was accepted and verified", not "traffic flows".

## 8. Next attended step

Recovery-armed boot with the full chain including `luofu_lsw.pfe=1`: expect W3 -> W4 -> W5 -> W6 ->
W7 (with corrected counters: "91 verified ... 77 acknowledged with diagnostics ... 1 write-only
acknowledged") -> W8 "PFE forwarding-engine base init programmed and verified" -> connectivity armed
-> GEMAC registration -> eth2 up -> br-lan forwarding.  Then ping from the host while watching the
PIE RX poll trace and the switch/EPS drop counters: RX poll lines prove the wire->CPU direction
opened, a wire frame (or non-zero egress counters) proves CPU->wire.  If nothing moves, work through
the section-5 gap list in order (IFC table first).

## 9. Reproduction

```
# fixture (read-only; not in the tree)
sha256sum rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_pfe.ko
# -> 12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a
# annotated disassembly + LSW access inventory (gitignored wave dir)
.provision-venv/bin/python build/linux-kbuild/waves/2026-11-19-qm-decode/ann.py \
  rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_pfe.ko \
  build/linux-kbuild/waves/2026-11-20-r4-armed/hi_lsw_pfe.addr.dis
.provision-venv/bin/python build/linux-kbuild/waves/2026-11-20-r4-armed/scan-lsw.py
```

## 10. Integration notes

- The kernel patch is in the skeleton's `patches-6.18/` (036); it applies after 035 and is
  file-disjoint from the 033-035 SMP lane.
- `shared.patch` is diffed against the current `opensource/` HEAD (which contains the revision-3
  W7 integration, commit 18186f0) and applies cleanly.
- Bench console evidence: `build/evidence-bench-gates/2026-11-19/uart-r3-armed.log`.
- No vendor binary, device artifact or per-unit value is in this package.
