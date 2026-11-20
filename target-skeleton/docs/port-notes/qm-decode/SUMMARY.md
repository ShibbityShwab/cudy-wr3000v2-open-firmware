# incoming/qm-decode - round 4e: IFC default-classification table (patch 043)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 10).  Prior record: `032`, `036`,
`038`, `040`, `041`, `042`.  This package delivers **patch 043 (W10)**: the recovered IFC
default-classification table.  No device access in this lane.

Apply `shared.patch` from the `opensource/` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. What the macconf run proved (image f58c516e)

Six gates + PFE, W9 inert, heartbeat liveness detector, macconf=1: the box stayed stable through
the traffic test and both eth2 link-ups logged the vendor MAC sequence
(`before 0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0` -> `after 0x1c=0x1@BT@, 0x40 already
correct) - the MAC configuration is complete, but traffic is still dead, so the classifier is the
next layer.

## 2. Decode: hi_pfe_ifc_init (pinned hi_lsw_pfe.ko, 52,412 B, sha256 12ea9fcb...)

Eight default entries, each written through **two** interfaces:

1. **IFC command/data engine** (ifc window): data words 0x138/0x13c/0x140/0x144/0x148 (5 per
   entry), then command `0x120 = 0xc0000000 | (index & 0x7f)`; poll @BT@0x120@BT@ until bit31
   clears (<=1001 iterations), bit29 = error.  Same access protocol as the EQM indirect engine.
   No read path exists in the recovered code -> these writes are acknowledged, not verified.
2. **tab128 default entry**: four words at `ifc+0x50000 + index*16` via
   `hi_lsw_write_tab128`; this region is directly readable and is verified STRICTLY.

The payload pairs were extracted by emulating the function from the fixture (byte-addressed
register/memory model) and cross-checked by hand for entries 1-5:

| idx | engine words | tab128 words |
| --- | --- | --- |
| 1 | 00000000 00000000 00000000 00100000 000f3001 | 0 0 0 0 |
| 2 | 00000002 00000000 00000022 ffff0000 000f2002 | 00100800 0 0 0 |
| 3 | 00000001 00000000 00000028 ffff0000 000f1003 | 00001d00 0 0 0 |
| 4 | 00000002 00000000 00000028 ffff0000 000f1004 | 00001d00 0 0 0 |
| 5 | e0000000 00000000 1c00000c ffff0000 000f1005 | 00001d00 0 0 0 |
| 6 | 00000000 ff000000 00001100 ffff0018 000f1006 | 00001d00 0 0 0 |
| 7 | 00000001 00000000 00000025 ffff0c00 000f1007 | 00001d00 0 0 0 |
| 8 | 00000000 00000000 00000000 ffff0000 000f0008 | 00100800 0 0 0 |

Only tab128 word0 of entries 3..7 depends on runtime state: it is built from the id returned by
the vendor `hi_tab_mgr_alloc_hash` (fresh manager -> 0, giving `0x1d00`).

## 3. Patch 043 (W10) implementation

- New gate `luofu_lsw.ifc=1`, requires `arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`.
- Command engine: pre-check idle (bit31/bit29 clear) before the first write; writes the five data
  words, then the command, then polls up to 1001 iterations; bit29 -> error, fail closed.
- tab128: snapshot all 32 words, require each stable across two reads, write the payloads, then
  read every word back; on any mismatch restore the whole tab128 snapshot and fail.
- Engine entries cannot be restored (no read path) - stated in the log and here; on a later-stage
  failure the tab128 region is restored and the block refuses to arm.
- `luofu_switch_ready()` now requires W3-W10; the GEMAC front end only registers with the
  classifier programmed.
- Payloads are the vendor's own init values [V write shape; U field semantics].

## 4. Bench verification for the next run

Expect: W10 start line with the command-engine pre-state and tab128 base, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registrations, link, heartbeat.  After the traffic test look for a
host ARP request followed by a box ARP reply on the wire (or PIE RX trace lines) and no watchdog
hang.

Failure signatures (box stays up, data path refused):
`W10: IFC entry N command failed (cmd=..., bit29 set)` - engine protocol mismatch;
`W10: IFC entry N command never completed ... after 1001 polls` - engine stuck;
`W10: tab128 readback mismatch ... restoring the snapshot` - wrong payload/word order.

## 5. W9 corrected single-port write: recommendation

Yes, run it - but as a **second boot of the same 001-044 image**, not before the IFC run:
the cmdline-only difference is `luofu_gmac.w9=1 hwinitwrite=1` (write mode, corrected 1518 limit,
bench port 11) with otherwise identical gates.  The heartbeat detector makes it a cheap, isolated
test of the corrected MAC frame-limit path; it is independent of the classifier work and needs no
rebuild.  Order: (1) 001-044 full chain with `ifc=1`, no W9 - the decisive classifier test;
(2) same image + W9 write mode - validates the frame-limit correction on top.

## 6. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/043-net-lsw-ifc-default-table.patch` | W10: IFC command-engine + tab128 default table, snapshot/readback/restore, readiness W3-W10 | `655ae1f4b50821bb75e7c145b7516f5728e734c02dfef6d1400bf2ec9e4d186e` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | LSW-SPEC 7.11 round-4e/W10 section, SKELETON-SMOKE session, README 043 row | see handover |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |
| wave dir `ifc-init-emu.py` | payload-extraction tooling (read-only over the fixture) | - |

## 7. Validation

- **Series apply (current full series + 043 = 44 patches):** 44/44, 0 failed hunks, 0 .rej/.orig;
  all three changed drivers byte-identical to the lane tree (`logs/10-series-apply-044b.log`).
- **WERROR compile:** luofu-switch.o / luofu-gmac.o rebuilt clean under `W=1 -Werror`.
- **checkpatch:** 0 errors, 0 checks, only the expected commit-description warning
  (`logs/11-checkpatch-043.log`).
- No devicetree/binding/Kconfig change; no vendor binary or per-unit value added.
