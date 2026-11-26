# wifi-image-3-final - IMAGE #3 preflight verification + final GO/NO-GO

**Lane:** image-flash-verify (shared task **task-218**).  **HOST-ONLY**: this lane
never contacted the router, the bench link, the UART, TFTP or the network, and
nothing was written, flashed or rebooted.  **The Lead fires, attended.**

**Verified against the real baked artifacts:**
`/home/shibbityshwab/wifi-image-3-artifacts/` and
`/home/shibbityshwab/wifi-image-3-module-artifacts/` (local, never committed;
text-only receipts are in [build-evidence/](build-evidence/)).

## Verdict

**GO - conditional.**  The host side is verified end to end: every pinned hash and
size of the IMAGE #3 flash unit re-derives, the preflight passes 23/0 on the real
artifacts, the selftest passes 37/0, the strict fire gate (rehearsed with the
expected stock pair-A witness) passes 30/0 for **both** install paths and fails
closed on all three negative cases, and the RUNBOOK's R1-R8 receipts are
consistent with the current bench facts (two stale/wording fixes and one
pre-flash staging action are called out in [GO-NO-GO.md](GO-NO-GO.md)).

The GO is **conditional** on the fire-time gates that only the bench can satisfy:
the strict gate must PASS with the **live** stock pair-A witness
(`--stock-witness` / `--proc-mtd`), the recovery door must be READY, a human must
own the power/RESET button, the read-only UART capture must be running, and the
vendor-firmware staging gap (C4, below) must be closed **before** the write wipes
the overlay.

## The conditions (full text: GO-NO-GO.md section 4)

| id | condition | why |
| --- | --- | --- |
| C1 | the on-bench **strict fire gate** PASSes with the live witness (rootfs-only if kernelb == `1794a531...`; otherwise Path B) | the rehearsal proves the tool, the live witness proves the box |
| C2 | recovery door READY (`recovery.bin` 22,024,351 B md5 `bc405809...`), bench link PASS, read-only UART capture, human at the bench | the only recovery is physical |
| C3 | artifact freeze + re-run of the host preflight immediately before the write | the fire unit must be the verified build |
| C4 | **rescue the vendor firmware pair** (`FIRMWARE.PD4.bin` + `wifi_block.bin`) off the current box (or rebuild) before the flash wipes the overlay; the host `/tmp` copies are gone | R6-R8/rungs need them; the image never packages them |
| C5 | accept the known losses: overlay/rootfs_data wipe, the current `/tmp` per-fire modules and `/root` fire scripts, first boot of a rootfs that has never booted | by design; pair A remains the rollback |
| C6 | after boot run R1-R4 first; R5 before any load; R6-R8 only as deliberate attended rungs (one per fresh boot, no rmmod) | the image is fail-closed; the 64-param merged module has never been fired as a combination |

## What was proven here (and what was not)

**Proven (host side, from the shipped bytes):** the container `4cd85b5d`
7,843,840 B with exactly 6 tar members; CONTROL/fip-manifest/INSTALL-STATUS; the
UNCHANGED kernel slice `13e81214` and the derived kernelb slot
`1794a531` (8,650,752 B); the rootfs UBI `90ae340c` 4,456,448 B (34 PEBs);
the module `a717c86d` 358,544 B / 64 params / `.text` identical to the
`545192df` activity form; `cfg80211` `bc4d9c73` (0 pkcs7 refs) and hostapd
`fef44f3b` (v2.12, 11AX build); the mtd14 arithmetic **19,922,944 B free**;
the rollback artifacts; the R5 PD4 rebuild byte-identical (`cfe24bfb`).

**Not proven here:** the live strict gate (needs the stock pair-A shell), the
image rootfs has never been booted (this flash is its first boot), the exact
64-parameter module combination has never been fired, the 5 GHz beacon is still
gated on the firmware-internal SMAC/TBTT timer, and the RX association gate is
still open.  The R8 rung proves the RF programme, **not** a beacon.

## Files

| file | what |
| --- | --- |
| [GO-NO-GO.md](GO-NO-GO.md) | the final report: verdict, artifact-verification table, preflight/selftest/gate-rehearsal results, the R1-R8 review vs the current bench facts, conditions, NO-GO triggers, residual risks |
| [FLASH-SEQUENCE.md](FLASH-SEQUENCE.md) | the exact copy-paste command sequences for the Lead: preconditions, stock pair-A switch, the strict gate, **Path A rootfs-only** and **Path B full container**, first-boot checks, evidence/teardown |
| [ROLLBACK.md](ROLLBACK.md) | pair switch back to stock A, restore a previous rootfs to mtd14, the physical RESET+TFTP last resort |
| [ACCEPTANCE.md](ACCEPTANCE.md) | the post-flash acceptance checklist (R1-R8 with the verified pins, the corrected HE/TBTT receipts, and the un-installed/rung rules) |
| build-evidence/ | text-only run receipts: real preflight, print-pins, unit-test, selftest, gate rehearsal (positive + negative), independent artifact verification, R1-R8 cross-check, preconditions |

Every value in these files is a size, sha256, mtd/partition number or a quoted
text line; no vendor binary, firmware byte or secret is carried.
