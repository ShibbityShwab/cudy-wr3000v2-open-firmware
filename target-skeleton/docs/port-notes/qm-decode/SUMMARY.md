# incoming/qm-decode - round 4h: W10 correction + W12 extended EPS/DP stage (patch 046)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 13).  Prior record: `032`, `036`,
`038`, `040`, `041`, `042`, `043` (W10), `044` (token fix), `045` (W11 dump).
This package delivers **patch 046**: the W10 tab128 correction from the stock-vs-ours dump plus the
W12 stage for the remaining persistent differences.  No device access in this lane.

Apply `shared.patch` from the `opensource` root (carries W10 + token fix + W11 + W12 docs), then move
this SUMMARY to `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. The dump diff (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt)

| class | ours | stock | action |
| --- | --- | --- | --- |
| IFC tab128 entries | written one slot too low; word0 0x1d00 | entry1 0x0 at 0x50010, entry2 0x100800 at 0x50020, entries3..7 **0x1d01** | **W10 corrected** |
| EPS extended words | 0 | eps+0xb20..0xb3c / 0xba0..0xbbc values | **W12 writes them** |
| dp+0x1c | 0x10000 | 0x18000 | **W12 writes 0x18000** |
| eqm+0x48..0x70 | last W7 op residue | last stock queue-init op residue | documented non-config |

## 2. W10 correction (in patch 046)

- **Offset bug:** the vendor primitive writes entry `idx` at `ifc+0x50000 + idx*16`; our revision wrote
  the 0-based array index, so every entry landed one slot too low (entry1 at 0x50000, entry2 at
  0x50010, ...).  Patch 046 uses the entry index: entry1..8 at 0x50010..0x50080.
- **Allocator id:** our first attempt assumed the vendor table-manager allocation id was 0 (0x1d00);
  the stock hardware holds **0x1d01** (first hash id = 1).  Word0 values now: entry1 0x0, entry2
  0x00100800, entries3..7 0x1d01, entry8 0x00100800 (entry1..7 [V] from the diff; entry8 [I] from
  the emulated init - the W11 dump range now covers 0x50080 so the next run confirms it).

## 3. W12 stage (new gate `luofu_switch.ext=1`)

Requires arm/egress/qidmap/eps/qminit/pfe/ifc.  Writes with snapshot / stable-two-reads / exact
readback / restore-on-mismatch, fail-closed (readiness is now W3-W12):

- **16 extended EPS words** (stock values): eps+0xb20..0xb3c = 0xa0, 0x810a0, 0x820a0, 0x830a0,
  0xb0, 0x810b0, 0x820b0, 0x830b0; eps+0xba0..0xbbc = 0x8004ffff, 0x8105ffff, 0x8106ffff,
  0x8107ffff, then the same four repeated.
- **dp+0x1c = 0x18000.**  Decode: the vendor DP probe writes `BIT(16) | field16` where field16 is
  the upper half of a value from `hi_system_mem_info_get` (that field read 0 on our boot, which is
  why W4 programmed 0x10000); stock runtime additionally carries BIT(15).  Bit15's meaning is [U]
  (a vendor memory/runtime field, not a forwarding enable); the stock-observed value is replayed.
- The eqm+0x48..0x70 differences are the indirect command/data registers holding each side's last
  queue-init op (stock 0x41100012 = table 17 index 18 write); documented, not replayed.

## 4. Expected bench log lines (attended run)

```
luofu_switch.ifc=1: programming the recovered IFC default-classification table (W10)
W10: IFC default-classification table: 8 entries, command-engine pre-state 0x0 (busy 0, error 0), tab128 base 0x50000
W10: IFC default-classification table programmed and verified: 8 entries (5-word command-engine descriptors acknowledged, 4-word tab128 entries read back exactly)
luofu_switch.ext=1: programming the stock-observed extended EPS words and the DP runtime value (W12)
W12: extended EPS/DP runtime words: 16 EPS words + dp+0x1c, snapshotting the current state first
W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000
connectivity path armed: ... the IFC default-classification table and the W12 extended EPS/DP runtime words are programmed and verified; ...
```
Failure signatures (box stays up, path refused): `W12: eps+0xNNN readback mismatch ... restoring the
snapshot` or `W12: dp+0x1c readback mismatch ...` or a stability refusal; the pre/post snapshot values are in the
line.  With `luofu_switch.dump=1` as well, the W11 dump now shows ifc+0x50010..0x50080 so the tab128
correction can be verified against the stock capture.

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/046-net-lsw-ifc-fix-eps-ext.patch` | W10 offset/id correction + W12 extended EPS/DP stage | `6b434e43597946d14a97978caf0c1290bca4088cf4de228c91977e4334d1cb07` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | W10 + token fix + W11 + W12 docs (5 files) | `ff128df37004a4697be8020bd5da87f771d4cfdeaac24be4b708229a76920508` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- **Series apply (current full series + 046 = 47 patches):** 47/47, 0 failed hunks, 0 .rej/.orig;
  all changed drivers byte-identical to the lane tree (`logs/10-series-apply-047b.log`).
- **WERROR compile:** both drivers rebuilt clean under `W=1 -Werror`.
- **checkpatch:** 0 errors, 0 checks, only the expected commit-description warning
  (`logs/11-checkpatch-046.log`).
- Shared docs patch verified against the current `opensource` tree; no devicetree/binding/Kconfig change.
