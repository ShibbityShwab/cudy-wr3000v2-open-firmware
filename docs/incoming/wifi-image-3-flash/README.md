# wifi-image-3-flash (task-201) - the IMAGE #3 flash runbook + preflight tooling

**Lane:** wifi-image-3-flash (shared task task-201), 2026-11-28. **HOST-ONLY**: this
lane never contacted the router, the bench link, the UART, TFTP or the network, and
nothing was flashed, written or rebooted. **The Lead fires.**

This lane delivers the attended **IMAGE #3 flash runbook** (the rootfs-only path
**and** the full sysupgrade-container path), the recovery-door requirements, the
post-boot receipts, the rollback and the STOP conditions - plus the **preflight
verification script** the Lead runs on the host before any write.

## Deliverables

| file | what |
| --- | --- |
| [RUNBOOK.md](RUNBOOK.md) | the complete attended flash procedure: preconditions, stock pair-A switch, both install paths, handover, post-boot receipts, rollback, STOP conditions, evidence/teardown |
| [PREFLIGHT.md](PREFLIGHT.md) | what the preflight verifies, how to produce the on-box witness files, exit codes and the path decision rule |
| tools/preflight-image-3.sh | host-only preflight over the built artifacts + optional on-box witness (/proc/mtd, stock pair-A reads); exit 0 PASS / 1 FAIL / 2 usage |
| tools/selftest.sh | artifact-free tests for the preflight (parsers, error paths, doc pins); exit 0/1 |
| build-evidence/ | committed text evidence of the preflight and selftest runs |

## The flash unit - pinned IMAGE #3 identities

The image is **built + verified host-side** by the wifi-image-3 lane
(verify-image-3.sh **129/129**, selftest **102/102**, three identical bakes) and is
**not flashed, not booted**. All values below are re-derived by the preflight from
the shipped bytes; the authoritative record is
[../wifi-image-3/HASHES.txt](../wifi-image-3/HASHES.txt) and
[../wifi-image-3/VERIFY.txt](../wifi-image-3/VERIFY.txt).

| item | size | sha256 |
| --- | --- | --- |
| **sysupgrade container** (the flash unit) | **7,843,840 B** | `4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53` |
| kernel slice (container member `kernel`; **UNCHANGED**) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` |
| kernelb slot (mtd12; slice padded with 0xFF to 8,650,752 B) | 8,650,752 B | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` |
| rootfs UBI (container member `root`) | 4,456,448 B | `90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3` |
| rootfs squashfs (UBI vol0) | 4,009,984 B | `ecbba4f97300607cd41614ab09c9aa6378bcea215e8de37f4c3097b860a0bfaf` |
| driver module, installed form (/lib/modules/6.18.60/hi5622_wifi.ko, **64 params**) | 358,544 B | `a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed` |
| driver module, activity form (strip --strip-debug) | 379,092 B | `545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99` |
| cfg80211.ko (regdomain fix: unsigned regdb accepted) | 2,317,952 B | `bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91` |
| hostapd (v2.12, CONFIG_DRIVER_11AX_SUPPORT) | 1,618,388 B | `fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6` |
| kernel package pin in the rootfs apk db | - | `6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1` |

Container: 6 tar members (`sysupgrade-cudy_wr3000-v2/` dir, `CONTROL`,
`INSTALL-STATUS.txt`, `fip-manifest.txt`, `kernel`, `root`); CONTROL
`BOARD=cudy_wr3000-v2` / `TARGET=hisilicon/luofu`; rootfs 524 files, 83 packages;
the fixed service timestamps + UBI image sequence number make the container
byte-reproducible (3/3 identical bakes,
[../wifi-image-3/build-evidence/build-runs.txt](../wifi-image-3/build-evidence/build-runs.txt)).

Local artifacts (never committed): /home/shibbityshwab/wifi-image-3-artifacts/
(container, kernel-slice, rootfs.ubi, root.squashfs, cfg80211.ko, hostapd, module
installed, SHA256SUMS) and /home/shibbityshwab/wifi-image-3-module-artifacts/
(activity + installed + unstripped module).

## What changes vs the verified image #2 (and why a rootfs-only write works)

The kernel slice in IMAGE #3 is **byte-identical** to the image #2/ABI-image slice
already in kernelb: everything new is in the **rootfs** - the bench-proven 5 GHz
regdomain cfg80211.ko, the 802.11ax-capable hostapd, and the merged 64-param module
(F2 5 GHz chain + image #2 RX watermark + he_caps + fail-closed BA skeleton) plus
the image #3 text policy payload. The container is 655,041 B larger than image #2
and the whole difference is the larger rootfs UBI
([../wifi-image-3/FLASH-FEASIBILITY.md](../wifi-image-3/FLASH-FEASIBILITY.md) section 1).

| geometry | value | consequence |
| --- | --- | --- |
| kernelb/mtd12 **size** | 8,650,752 B (0x840000) | the padded kernel slot **fills it exactly** |
| kernelb current content (expected, from the earlier installs) | `1794a531...` | a rootfs-only write leaves it untouched and verified |
| rootfsb/mtd14 **size** | 24,379,392 B (0x1740000) | - |
| IMAGE #3 rootfs UBI | 4,456,448 B (34 PEBs) | 18.3 % used; **19,922,944 B free** |
| container total | 7,843,840 B | the two write units are the kernel member (slot 8,650,752 B) and the root member (UBI 4,456,448 B) |

Geometry sources: BENCH-FACTS.md section 5 (mtd11/12 8,650,752 B; mtd13/14
0x1740000), dumps/router-20261108/ubi-readpath-report.md:42, and the
[image-3 feasibility note](../wifi-image-3/FLASH-FEASIBILITY.md) section 2.

## The two install paths (the Lead picks; A is the minimal write)

* **Path A - rootfs-only (recommended).** Write **mtd14 only** with the container's
  `root` member (ubiformat /dev/mtd14 -y -f rootfs.ubi), leave kernelb
  untouched, then hand pair B over (env bootflag=b + selector cell 0x11, read
  back, reboot). Requires the on-box kernelb to already hash `1794a531...`.
* **Path B - full sysupgrade container.** Write **both** pair-b members from the
  container (kernelb = the same validated slice, then rootfsb) with
  tools/stock-telnet-install.sh --inspect / --apply --attended --recovery-armed,
  which verifies the uploads, both read-backs, the untouched active pair, the
  handover and the booted identity. Use it if kernelb's content cannot be
  confirmed, or as the reconstruction path. The extra kernel erase/write is
  provably redundant but safe (same bytes).

**Not an install path:** the vendor stock sysupgrade/web-flash UI. The container's
own `INSTALL-STATUS.txt` says **NOT FLASHABLE-UNTIL-INSTALLED**; its
platform_do_upgrade has never run on a device, it has no factory image, and the
baked command line pins `ubi.mtd=rootfsb` (pair B only). The bench installs
are the manual pair-B writes in RUNBOOK.md.

## Preflight (host, before anything else)

    cd opensource/docs/incoming/wifi-image-3-flash
    ./tools/preflight-image-3.sh              # 23 passed, 0 failed on the bench host
    ./tools/preflight-image-3.sh --strict \
        --stock-witness /tmp/image3-stock-witness.txt \
        --proc-mtd /tmp/image3-proc-mtd.txt   # the fire gate (pair-A readiness + geometry)
    ./tools/selftest.sh                       # artifact-free tests of the tooling

The host-artifact run over the real baked artifacts is committed as
[build-evidence/preflight-run.txt](build-evidence/preflight-run.txt)
(23 PASS / 0 FAIL; container, members, kernel slot, rootfs UBI, geometry,
sibling artifacts and module identities). PREFLIGHT.md has the check table and
the exact on-box commands that produce the witness files (read-only; no write).

## Evidence this runbook is built on

* the image: [../wifi-image-3/REPORT.md](../wifi-image-3/REPORT.md),
  [HASHES.txt](../wifi-image-3/HASHES.txt), [VERIFY.txt](../wifi-image-3/VERIFY.txt),
  [FLASH-FEASIBILITY.md](../wifi-image-3/FLASH-FEASIBILITY.md),
  [BUILD-NOTES.md](../wifi-image-3/BUILD-NOTES.md), build-evidence/
* the base regdomain/ax userspace: [../wifi-image-update/README.md](../wifi-image-update/README.md)
  (cfg80211 fix: [../wifi-regdom-fix/README.md](../wifi-regdom-fix/README.md));
  the HE AP fire recipe: [../wifi-ax-he-caps/FIRE-RECIPE.md](../wifi-ax-he-caps/FIRE-RECIPE.md)
* the verified image #2 base and the rootfs-only feasibility:
  [../wifi-final-image-2/](../wifi-final-image-2/) (README, HASHES.txt, FLASH-FEASIBILITY.md)
* the validated stock-assisted transport (stock pair-A shell, split, read-backs,
  handover, boot verification): [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md)
  and root tools/stock-telnet-install.sh / tools/recovery-card.sh / tools/bench-link.sh
* physical invariants: root BENCH-FACTS.md (slots/selectors, UART read-only,
  recovery), RUNBOOK.md, LESSONS.md; the consolidated session shape:
  [../BENCH-NEXT-SESSION/README.md](../BENCH-NEXT-SESSION/README.md) section 5 (image #2
  install + receipts; this runbook is the IMAGE #3 successor)

## Limits (not claimed by this lane)

* **Not flashed, not booted**: IMAGE #3 is a host-built artifact; the fire is the
  Lead's attended decision.
* **The exact 64-param module combination is not bench-fired as a combination**
  (its three parents are); every new feature ships fail-closed (0) and is inert
  until a rung enables it.
* The 5 GHz **beacon** remains gated on the firmware TBTT arm (5G-4, fw-tbtt-arm
  lane); the RF programming itself is proven (F2 fire 2026-11-27 23:30).
* The association/RX gate is still open (the RX H2D trace decode); this image
  carries the association-lineage module, not a fix for it.
* Recovery is physical (power-off + RESET held through the counter + TFTP); an
  agent cannot recover the box. No human at the bench = no fire.
* The preflight is a **host artifact + witness** gate, not a substitute for the
  attended procedure or for the UART/recovery checks.

## Policy

Text only in git: the container/module/UBI/squashfs and the vendor-derived
firmware artifacts are local (gitignored) and never committed. No vendor binary,
firmware byte, calibration value or secret is stored here; the committed evidence
is sizes, sha256 digests, register/field values and partition numbers.
