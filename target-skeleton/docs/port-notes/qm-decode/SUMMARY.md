# incoming/qm-decode - LSW EQM queue-manager decode + W7 init write stage (patch 032)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-19 (decode/implementation), updated
after the first hardware run (image 14791958) with the table-19 readback fix.
**Status:** kernel patch revalidated host-side and **partly executed on hardware** (the W7 gate ran,
the snapshot passed, the strict readback failed on table 19 and the stage refused fail-closed).
Nothing in this lane reflashed the box; the hardware result is the bench lane's console evidence
recorded here. Publication remains NOT READY.

This package is an **incoming** deliverable: it is the post-bench update of the already-integrated
lane record. Apply `shared.patch` from the `opensource/` root (it updates the LSW-SPEC 7.9
verification model, the SKELETON-SMOKE W7 session, and the README 032 row with the bench result
and the fix), then move this SUMMARY to `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/032-net-lsw-eqm-qminit.patch` | fixed fifth attended W7 stage (`luofu_lsw.qminit=1`): EQM queue-manager init tables 19/32/35/17/16/34 for the nine wired queue groups (169 entries / 845 words) with pre-write snapshot/conformance, per-field (vendor-read) readback verification, acknowledge-only table 19, restore of verified entries, and readiness that requires the whole W3-W7 write set | `170b2684bdbab99f5bb631b3de00c346826376d08f43b11be8edcff676caf4cd` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | bench-result/fix update of `docs/LSW-SPEC.md` (7.9 verification model + bench result, 12.3 W7 row), `docs/SKELETON-SMOKE.md` (W7 session: bench console, fix, updated sequence) and `target-skeleton/README.md` (032 row) | `5cc49a9dce4437610e8ad3bce3fbd784cabbde388b9cd3de98aa4dbd8e5cafb6` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy of this record | - |

## 2. First hardware run and the fix (2026-11-19, image 14791958)

Command line of the run: `luofu_switch.arm=1 luofu_switch.egress=1 luofu_switch.qidmap=1`,
`luofu_switch.eps=1 luofu_switch.qminit=1` and `luofu_pie.arm=1`.  W3-W6 passed, W7 reached
the snapshot and the first write:

```
[2.355537] luofu-switch: W7 snapshot: 169 EQM table entries (845 words) read and stable, indirect interface idle (eqm+0x48 0x0)
[2.369773] luofu-switch: EQM readback mismatch at table 19 entry 0 word 0: read 0x3fff, expected 0xffffffff; restoring the pre-write snapshot
[2.384940] luofu-switch: probe with driver luofu-switch failed with error -5
```

The fail-closed design worked as intended: the stage restored and refused, `luofu_switch_ready()` stayed
false and the four GEMAC netdevs refused to register (`-EOPNOTSUPP`), so no half-configured netdev
could egress-drop silently.

**Root cause (re-derived from the pinned fixture, no guess).**  Table 19 is **write-only in the
vendor code**: `hi_queue_init@0x5164..0x5198` is its only user, and the EQM table accessors
(`hi_eqm_write_tab@0x1cdc`, `hi_eqm_read_tab@0x1e18`, `hi_eqm_tab_ready@0x1f5c`) are
**module-local** (`l F` symbols) to `hi_lsw_dp.ko`, so no other module can read it either; a
scan of all EQM read call sites found readers for tables 16/17/32/34/35/37/38/39/80/81 and none
for table 19.  The bench value is exactly the low 14 bits of what was written:
`0xffffffff & GENMASK(13,0) = 0x3fff`.  So the engine's read view of table 19 is not the
written record - either a 14-bit field-width view or an unimplemented read ([I]; the value is
[V]).  Requiring readback equality for it was an evidence error in the first patch: the vendor
has no such contract.

**Fix in patch 032 (sha256 above):**

1. **Table 19 is acknowledge-only.**  The write is still issued exactly as the vendor issues it
   (same 5-word record, index 0, vendor order); verification is the command completion (busy
   cleared, no error bit - already enforced by the decoded indirect engine).  The engine read
   view is logged at info level for the next run instead of being compared.
2. **The other tables are compared only on the fields the vendor's own read paths use:**
   table 32 slot priorities (3-bit fields, `hi_queue_get_sch`), table 35 word 0 bytes 1-2
   (the fields `hi_queue_get_sch` extracts), table 17 rate/cell fields (the fields the EQM
   DFX reader extracts), table 16 the bytes the write sets (RMW helper), table 34 queue-port/
   slot/input/valid (the fields the vendor RMW helper checks).  A difference inside a mask still
   fails closed; a difference outside a mask is logged as a diagnostic because the engine need
   not implement those bits as readable.
3. **Restore skips write-only entries.**  Writing the read view of table 19 back could corrupt the
   record, so the stage reports it instead; the block is powered down on refusal and every W7
   attempt rewrites the full vendor init record, so no stale state stays armed.
4. **Diagnosability:** the mismatch log now prints the vendor-read field mask, the pre-write
   snapshot value and the whole 5-word read view, and the success log separates "verified on
   their vendor-read fields" from "write-only entries acknowledged".

## 3. What the decode recovered (unchanged by the fix; pinned fixture evidence)

Fixture: `rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko`, 67,164 B, sha256
`f0010305caf15c6da71981b7c4b51c7f9c0ce55c1911b0ca031a44892fdcd535` (host-only disassembly; no vendor
binary added to the tree).

- **Indirect engine [V]:** command word at `eqm+0x48` = index[15:0] | table[26:20] | bit30 write |
  bit31 start; poll until bit31 clears; **bit29 error** (patch 025's header had bit5, corrected);
  write data words 0..4 -> `+0x5c/60/64/68/70`, read words 0..4 <- `+0x4c/50/54/58/6c`;
  **max 5 data words per operation**.  Corroborated by the measured idle word `0x41100012` =
  a completed write of table 17 index 18 with `eqm+0x5c = 0xbb8 = 3000` (the vendor's last
  queue-init profile write).
- **Write set [V]:** table 19 idx 0 {0xffffffff, 0xffffc3e8, 0xffffffff, 0xffffffff, 0xffffffff};
  table 32 idx P (P = 4..12): byte0 = P*8, 3-bit slot priorities 7..0 in word 1 bits 0..23;
  table 35 idx qid {0x00070000, 0, 0, 0, 0}; table 17 profile: word0 = rate&0x1fff,
  word1 = 0x10000000, word2 = 0xffff0000|rate, word3 = (sch&0x1ff)<<20 | cells,
  word4 = 0x07f03fff with cells = rate if (eqm+0x38 & 0x3fff) < rate else rate/2; table 16 idx
  qid>>4 byte (qid&15) = 0x80|(profile&0x1f); table 34 idx qid word0 = queue-port (bits 5:0),
  slot (17:14), bit13 = 1, bit18 = valid.
- **Per-group constants [V]** (vendor build-time queue-group table `.data+0xd0`): port 4
  {profile base 0x10, sel 1, sch 0x20, rate 3000}; ports 5..7 {0x10, 2, 0x20, 3000}; ports 8..12
  {0x00, 8, 0x20, 2500}; `profile(port,slot) = base + (sel<=7 ? sel : slot)`,
  `qid(port,slot) = 1 + port_index*8 + slot` over the model patches 021/022 validate.
- **Egress answer:** a PIE TX descriptor carries only the egress **port tag** (`+8` bits 3:0)
  and its `1<<port` bitmap, plus the vport - **no qid and no queue handle** (the vendor's
  per-port handle is zero in this build).  The W7 tables are the init-time queue/deque state the
  vendor programs; a W7 boot is the first experiment that can tell whether the tagged descriptor
  egresses through the queue manager or needs the excluded table-38/VP records.
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
| any write gate without `arm=1` | explicit `-EINVAL` refusal | none |

## 5. Validation (isolated clone under `build/linux-kbuild/waves/2026-11-19-qm-decode/`, no device)

- **Series apply (001-033):** pristine `linux-6.18.60` + all 33 patches in order: **33/33 applied,
  0 failed hunks, 0 .rej/.orig**, drivers byte-identical to the lane work tree
  (`logs/40-series-apply-001-033.log`, sha256
  `501ede9f2de65d4018d31a41f616b7ac690b2633960efb88dfcc0779d1237cd0`).
- **Isolated clone prepare/compile:** reflink copy of the 001-031 OpenWrt lab under the wave
  directory with the fixed 032 and the lane-033 patch added (shared clone and
  `openwrt-lab-selectorfix-img` untouched): `make target/linux/prepare` **RC=0**
  (log sha256 `58359efd2a757329a1bb6579de64fbc9068e3db5200e60f72b8a76adc5599953`) and
  `make target/linux/compile` **RC=0** in 721.6 s (log sha256
  `5b8797857e52c0cc1bf1eddf56a8b95a22a31444b0396fd05a08fe8f47a1fd9b`) with `CONFIG_WERROR=y`
  and **zero `warning:`/`error:` lines in the whole log**; both changed drivers rebuilt
  (`luofu-switch.o` 101,268 B incl. the fix, `luofu-gmac.o` 51,768 B); `vmlinux` 57,103,992 B,
  sha256 `45508577fb204a478be5fe9c167efde4981be2270753d8d00da18e2ae85dfd88`; the prepared tree
  is byte-identical to the lane work tree, i.e. the compile validated the delivered patch.
- **checkpatch:** 0 errors, 0 checks, only the expected "missing commit description" warning for a
  pure diff (`logs/41-checkpatch-032.log`, sha256
  `3da5f81a8c7f75b2191cf0c1eb81c88ebb72e7041659c7c214ac79789f5f9d09`).
- **DTS/binding/config:** patch 032 changes no devicetree, binding or Kconfig/config symbol; the
  previously established board-DTS/dt_binding_check/config-idempotence results stand.

## 6. Proof vs unverified

**Proven on hardware (2026-11-19, image 14791958):** W3-W6 run and verify on the real block; the
EQM indirect interface performs the W7 845-word pre-write snapshot (stable, idle); the engine
acknowledges the W7 writes; the fail-closed path (restore, power-down, readiness refusal, GEMAC
refusal) works.  The table-19 read view is not the written record (0x3fff vs 0xffffffff).

**Proven host-side:** write shapes/values are the vendor's own init writes; table 19 is write-only
in the vendor code; verification masks reflect exactly the vendor-read fields; 001-033 applies and
compiles clean.

**Unverified:** whether a fully configured queue manager lets a tagged PIE TX descriptor egress
(the next measurement); RX delivery; table 38 / VP class; IFC/L2/VLAN; queue/flow DDR layout; the
engine semantics of every verified field.  A successful W7 boot still means "the write set was
accepted/verified", not "forwarding works".

## 7. Risks and mitigations

| risk | severity | mitigation |
| --- | --- | --- |
| the engine silently ignores a write that is only acknowledged (table 19 is ack-only) | low | the record is the vendor's own init write and every attempt rewrites it; it is a static cell-config record, not per-frame state |
| a verified-mask field still reads differently (engine masks more than expected) | med | the mismatch log prints mask/snapshot/full read view, the stage restores and refuses; the next run is diagnosable from the console alone |
| write-only table 19 cannot be restored on failure | low | the block is powered down on refusal and W7 rewrites the full record on every attempt; no netdev is armed |
| fields outside the masks differ and are not verified | low | they are not read by any vendor path; differences are logged for the record |
| a wedge on the attended run | high | attended only, recovery door armed, physical RESET+TFTP is the only recovery; no reflash with this patch |

## 8. Next attended step

Boot the same five-gate chain plus `luofu_pie.arm=1` (recovery card armed) and expect, in order:
W3 snapshot -> W4 verified -> W5 verified -> W6 verified -> W7 snapshot lines (169-entry summary,
table-19 pre-write read view, interface idle) -> W7 verification line ("168 verified on their
vendor-read fields, 1 write-only entry acknowledged") with at most a few diagnostic-only warnings
-> connectivity armed -> GEMAC registration.  Then ping across the wired port while watching the
PIE TX read pointer and the switch/EPS drop counters.  If a verified field still mismatches, the
new log line carries the mask, the snapshot value and the full read view - capture it and power
cycle; the stage will have restored and refused.

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

- The fixed kernel patch is in the skeleton's `patches-6.18/` (032) and applies after 031, before
  033 (file-disjoint).
- `shared.patch` is diffed against the current `opensource/` HEAD that already contains the
  original W7 integration (commit `32f16b7`); it only updates the 7.9 verification model/bench result,
  the SKELETON-SMOKE session and the README 032 row.
- The bench console evidence is the parent repository's gitignored
  `build/uart-logs/20261119T120419Z-uart-w7-run.log`.
- No vendor binary, device artifact or per-unit value is in this package.
