# incoming/qm-decode - LSW EQM queue-manager decode + W7 init write stage (patch 032)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-19 (decode/implementation); updated after the
second hardware round with the tiered-verification fix (patch 032 revision 3).
**Status:** kernel patch revalidated host-side (001-034) and **executed on hardware three times** on
2026-11-19. Every hardware round refused fail-closed (GEMAC unregistered) - by design - and each
round narrowed the engine's readback contract: the write path is correct, the engine simply
implements only part of each EQM record as readable. The next attended boot is the first one that
can run the complete write set with bench-proven verification and then test traffic. Nothing in
this lane flashed or wrote the device; the hardware evidence is the bench lane's console capture.
Publication remains NOT READY.

This package is an **incoming** deliverable (post-bench revision of the integrated lane record).
Apply `shared.patch` from the `opensource/` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/032-net-lsw-eqm-qminit.patch` | fifth attended W7 stage (`luofu_lsw.qminit=1`), revision 3: EQM queue-manager init tables 19/32/35/17/16/34 for the nine wired queue groups (169 entries / 845 words), pre-write snapshot/conformance, **tiered verification** (STRICT only on bench-proven fields, DIAG for tables 16/34, ACK for write-only table 19), restore of bench-proven entries only, readiness requiring the whole W3-W7 set | `719c824c1f173b8c98b1c48181cf8177a9f1aad70c71ca2a216b1f35e7d50ffd` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | round-2 update of `docs/LSW-SPEC.md` (rounds section, tier model, 12.3 W7 row), `docs/SKELETON-SMOKE.md` (bench results rounds 1-2, tiered policy, revised expected console output) and `target-skeleton/README.md` (032 row) | `517fe475c4fa51efb1bc4516006002c307834318e8d5afd0e14ca1692c73982a` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy of this record | - |

## 2. Hardware rounds and the verification fix

All rounds used the same command line: `luofu_switch.arm=1 luofu_switch.egress=1`,
`luofu_switch.qidmap=1 luofu_switch.eps=1 luofu_switch.qminit=1` and `luofu_pie.arm=1`.  W3-W6 passed
in every round; W7 reached the snapshot and the writes each time.

### Round 1 (image 14791958): table 19 is write-only in the vendor code

```
[2.355537] W7 snapshot: 169 EQM table entries (845 words) read and stable, indirect interface idle (eqm+0x48 0x0)
[2.369773] EQM readback mismatch at table 19 entry 0 word 0: read 0x3fff, expected 0xffffffff; restoring the pre-write snapshot
[2.384940] probe with driver luofu-switch failed with error -5
```

Decode: table 19 is never read by any vendor code path - `hi_queue_init@0x5164..0x5198` is its
only user and the EQM table accessors are **module-local** to `hi_lsw_dp.ko` - so no readback
contract exists for it.  Fix (rev 2): acknowledge-only table 19; compare only vendor-read fields.

### Round 2 (image 5d2c5476): the engine implements only part of each record as readable

That fix let the run continue: every table-32 and table-35 entry verified, and the table-19 read
view (logged) became the decisive diagnostic:

```
[2.702915] W7 snapshot: write-only table 19 entry 0 pre-write read view 0x0 0x0 0x0 0x0 0x0 (not compared)
[2.730787] W7: table 19 entry 0 is write-only in the vendor code: command acknowledged, read view 0x3fff 0x3e8 0x3fff 0x1ffffff 0x0 (not compared)
[2.747459] EQM readback mismatch at table 17 entry 17 word 0: read 0x0, expected 0xbb8 (vendor-read field mask 0x1fff), snapshot 0x0, read view 0x0 0x10000000 0xffff0bb8 0x2000bb8 0x0; restoring the verified entries
[2.786938] probe with driver luofu-switch failed with error -5
```

Three facts come out of this (the third was the open question from round 1):

1. **The table-19 write takes effect.**  The fresh block reads 0x0 0x0 0x0 0x0 0x0; after writing
   {0xffffffff, 0xffffc3e8, 0xffffffff, 0xffffffff, 0xffffffff} the read view is
   **0x3fff 0x3e8 0x3fff 0x1ffffff 0x0** - the written words truncated to per-word field widths
   (14/14/14/25/0 bits).  The read view is a field-masked view, not the record.
2. **The write/read data-register mapping is correct, not shifted.**  Table 17 was written
   {0xbb8, 0x10000000, 0xffff0bb8, 0x2000bb8, 0x07f03fff} and read back
   {0x0, 0x10000000, 0xffff0bb8, 0x2000bb8, 0x0}: words 1..3 come back **exactly at their own
   indices**.  A one-word shift or a wrong +0x5c..0x70 / +0x4c..0x6c pairing would not preserve
   that alignment, and the same mapping verified tables 32 (9 entries) and 35 (72 entries).
3. **The engine does not implement every word as readable.**  Table 17 words 0 and 4 read 0
   although written; table 19 word 4 likewise.  Field/word readability is per table and can only
   be established from the read view, not from the vendor write code.

### Fix (patch 032 revision 3): tiered verification

- **STRICT (fail closed)** - only fields proven readable on the bench: table 32 slot priorities
  (3-bit fields, word 1), table 35 word 0 bytes 1-2 (the fields `hi_queue_get_sch` reads),
  table 17 words 1..3 (read back exactly).
- **DIAG (acknowledged + logged)** - tables 16 and 34 were not reached before the table-17
  refusal: their writes are command-acknowledged and their read views are logged against the
  vendor-read mask, up to six diagnostic lines, so the next bench capture contains the data to
  promote them to STRICT.
- **ACK** - write-only table 19: command completion only, read view logged (not compared).
- **Restore** - a STRICT mismatch restores the **bench-proven** entries from the pre-write
  snapshot.  Table 19 and tables 16/34 are deliberately left as written: their read views cannot
  be trusted to restore a record, so writing them back could corrupt it.  The failure path powers
  the switch block down and refuses readiness, so nothing stays armed; every W7 attempt rewrites
  the full vendor init record.  The restore log states the restored/skipped counts.
- **Diagnosability** - the mismatch log prints the bench-proven field mask, the pre-write snapshot
  value and the whole 5-word read view; the success log separates bench-proven verified entries,
  diagnostic entries and the write-only acknowledgement.

## 3. What the decode recovered (unchanged; pinned fixture evidence)

Fixture: `rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko`, 67,164 B, sha256
`f0010305caf15c6da71981b7c4b51c7f9c0ce55c1911b0ca031a44892fdcd535` (host-only disassembly; no vendor
binary added to the tree).

- **Indirect engine [V]:** command word at `eqm+0x48` = index[15:0] | table[26:20] | bit30 write |
  bit31 start; poll until bit31 clears; **bit29 error**; write data words 0..4 ->
  `+0x5c/60/64/68/70`, read words 0..4 <- `+0x4c/50/54/58/6c`; **max 5 data words per
  operation**.  Corroborated by the measured idle word `0x41100012` = completed write of table 17
  index 18 with `eqm+0x5c = 0xbb8 = 3000`.
- **Write set [V]:** table 19 idx 0 {0xffffffff, 0xffffc3e8, 0xffffffff, 0xffffffff, 0xffffffff};
  table 32 idx P (P = 4..12): byte0 = P*8, 3-bit slot priorities 7..0 in word 1 bits 0..23;
  table 35 idx qid {0x00070000, 0, 0, 0, 0}; table 17 profile: word0 = rate&0x1fff,
  word1 = 0x10000000, word2 = 0xffff0000|rate, word3 = (sch&0x1ff)<<20 | cells,
  word4 = 0x07f03fff with cells = rate if (eqm+0x38 & 0x3fff) < rate else rate/2; table 16 idx
  qid>>4 byte (qid&15) = 0x80|(profile&0x1f); table 34 idx qid word0 = queue-port (bits 5:0),
  slot (17:14), bit13 = 1, bit18 = valid.
- **Per-group constants [V]** (vendor build-time queue-group table `.data+0xd0`): port 4
  {0x10, 1, 0x20, 3000}; ports 5..7 {0x10, 2, 0x20, 3000}; ports 8..12 {0x00, 8, 0x20, 2500};
  `profile(port,slot) = base + (sel<=7 ? sel : slot)`, `qid(port,slot) = 1 + port_index*8 + slot`
  over the model patches 021/022 validate.
- **Egress answer:** a PIE TX descriptor carries only the egress **port tag** (`+8` bits 3:0)
  and its `1<<port` bitmap, plus the vport - **no qid and no queue handle**.  The W7 tables are
  the init-time queue/deque state the vendor programs; the next attended boot is the first that
  can tell whether a tagged descriptor egresses through the queue manager or needs the excluded
  table-38/VP records.
- **Excluded by design:** table 38 (vendor table-manager id) and the ports 40..43 VP class.

## 4. Gate model (fail closed; default boot unchanged)

| boot parameters | switch driver | GEMAC netdevs |
| --- | --- | --- |
| none | read-only baseline/probe/refusal; no write | none |
| `arm=1` | W3 power + snapshot; refuse; no LSW register write | none |
| `arm=1 egress=1` | W3+W4 verified; qid map absent -> restore, power down, refuse | none |
| `arm=1 egress=1 qidmap=1` | W3+W4+W5 verified; eps absent -> restore, refuse | none |
| `arm=1 egress=1 qidmap=1 eps=1` | W3-W6 verified; qminit absent -> restore, refuse | none |
| `arm=1 ... eps=1 qminit=1` | W3-W7 accepted/verified -> `luofu_switch_ready()` true | requires `luofu_pie.arm=1` too |

## 5. Validation (isolated clone under `build/linux-kbuild/waves/2026-11-19-qm-decode/`, no device)

- **Series apply (001-034):** pristine `linux-6.18.60` + all 34 patches in order: **34/34 applied,
  0 failed hunks, 0 .rej/.orig**, drivers byte-identical to the lane work tree
  (`logs/60-series-apply-001-034.log`, sha256
  `b6acf4dd00811b3bf179336e8507edf2a6cffca4525a584d3b31bee214fae616`).
- **Isolated clone prepare/compile:** reflink copy of the OpenWrt lab under the wave directory
  with patch 032 rev 3 and the lane-033/034 patches added (shared clone and
  `openwrt-lab-selectorfix-img` untouched): `make target/linux/prepare` **RC=0** (log sha256
  `f8c194a8040c6137c1fd9a0c4f2f61aa96ccdd7fb1ef4a6867dd252cd7206dc5`) and
  `make target/linux/compile` **RC=0** in 666.3 s (log sha256
  `233f35ea56394a123263417bd60ede1f51e961c9fe78967978e410f650b615dc`) with `CONFIG_WERROR=y`
  and **zero `warning:`/`error:` lines in the whole log**; both changed drivers rebuilt
  (`luofu-switch.o` 102,448 B, `luofu-gmac.o` 51,768 B); `vmlinux` 57,108,392 B, sha256
  `8ad07abea9c703530cd7862a20ae3f259b28af126cd2b29bc4b14b1f057b4819`; the prepared tree is
  byte-identical to the lane work tree, i.e. the compile validated the delivered patch.
- **checkpatch:** 0 errors, 0 checks, only the expected "missing commit description" warning
  (`logs/61-checkpatch-032.log`, sha256
  `0589b889cfc793c32a61570c9281e3b89f8d00dece83569f93c1ff3a3f3e9f8b`).
- **DTS/binding/config:** patch 032 changes no devicetree, binding or Kconfig/config symbol; the
  established board-DTS/dt_binding_check/config-idempotence results stand.

## 6. Proof vs unverified

**Proven on hardware (2026-11-19, images 14791958 / 5d2c5476):** W3-W6 run and verify on silicon;
the W7 845-word pre-write snapshot is stable with the indirect interface idle; the engine
acknowledges every W7 write; the table-32 and table-35 vendor-read fields verify; table 17 words
1..3 read back exactly at their indices (write/read data-register mapping correct, not shifted);
table 17 words 0/4 and table 19 word 4 are not readable (read 0 although written); the table-19
read view is a per-word field-width mask of the written record; the fail-closed path (restore of
bench-proven entries, table-19/16/34 left as written and reported, power-down, readiness refusal,
GEMAC refusal) works as designed.

**Unverified:** whether a fully configured queue manager lets a tagged PIE TX descriptor egress
(the next measurement); readability of tables 16/34 fields (diagnostics will show it); RX
delivery; table 38 / VP class; IFC/L2/VLAN; queue/flow DDR layout.  A successful W7 boot still
means "the write set was accepted/verified on the bench-proven fields", not "forwarding works".

## 7. Risks and mitigations

| risk | severity | mitigation |
| --- | --- | --- |
| a STRICT field still mismatches a third time | med | only bench-proven fields are STRICT now (32/35/17), and their exact readback is on record; logs give mask/snapshot/read view |
| tables 16/34 writes are accepted but their fields differ (not fail-closed yet) | low | command acknowledgement + logged diagnostics; they are the vendor's init values; readiness claim documented as bench-proven-only |
| the engine silently ignores a write that is only acknowledged | low | table 19's write was proven effective by its read-view change in round 2; the write set is the vendor's own init record and is rewritten on every attempt |
| table 19 / tables 16/34 cannot be restored on failure | low | documented; the block is powered down on refusal and no netdev is armed; restore log reports restored/skipped |
| a wedge on the attended run | high | attended only, recovery door armed, physical RESET+TFTP is the only recovery; no reflash with this patch |

## 8. Next attended step

Boot the same five-gate chain plus `luofu_pie.arm=1` (recovery card armed).  Expected sequence:
W3 snapshot -> W4 verified -> W5 verified -> W6 verified -> W7 snapshot lines (169-entry summary,
table-19 pre-write read view, interface idle) -> W7 line "... programmed: 91 verified on
bench-proven vendor-read fields (tables 32/35/17), 77 acknowledged with diagnostics (tables 16/34;
N differed on a vendor-read field), 1 write-only acknowledged (table 19) ..." -> connectivity armed
-> GEMAC registration.  Then ping across the wired port while watching the PIE TX read pointer and
the switch/EPS drop counters.  Capture the diagnostic read views for tables 16/34 either way - they
are the data needed to promote those tables to fail-closed verification.  If a STRICT table
mismatches, the stage restores the bench-proven entries, leaves table 19 and tables 16/34 as
written, powers the block down and refuses readiness.

## 9. Reproduction

```
# fixture (read-only; not in the tree)
sha256sum rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko
# -> f0010305caf15c6da71981b7c4b51c7f9c0ce55c1911b0ca031a44892fdcd535

# addend-resolving annotated disassembly (gitignored wave dir)
.provision-venv/bin/python build/linux-kbuild/waves/2026-11-19-qm-decode/ann.py \\
  rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko \\
  build/linux-kbuild/waves/2026-11-19-qm-decode/hi_lsw_dp.addr.dis

# vendor queue-group table + EQM data-address table dump
.provision-venv/bin/python build/linux-kbuild/waves/2026-11-19-qm-decode/dump-tables.py
```

## 10. Integration notes

- The kernel patch is in the skeleton's `patches-6.18/` (032 revision 3); it applies after 031
  and before 033/034 (file-disjoint).
- `shared.patch` is diffed against the current `opensource/` HEAD that already contains the
  rev-2 W7 integration (commit `4c5e57b`); it updates the LSW-SPEC rounds/tier model, the
  SKELETON-SMOKE bench results/policy and the README 032 row.
- Bench console evidence is the parent repository's gitignored
  `build/uart-logs/20261119T135316Z-uart-w7smp-final.log` (round 2) and
  `build/uart-logs/20261119T120419Z-uart-w7-run.log` (round 1).
- No vendor binary, device artifact or per-unit value is in this package.
