# Install path - delivery-track status (the objective's clause)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** host-only reconciliation, 2026-11-27 (goal round 61). Two distinct
install paths exist in this project and must not be confused:

## Path A - the bench image #3 (DONE: flashed + accepted)

The image #3 sysupgrade container (7,843,840 B sha256
`4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53`) was
written by the **rootfs-only path** (a single `ubiformat /dev/mtd14` of the
4,456,448 B rootfs UBI `90ae340c...`, then the pair-b handover) and booted:
the 6.18.60 kernel, the fixed cfg80211, the ax hostapd, the merged module, the
US regdomain and the WPA3-SAE + Wi-Fi 6 HE on air. The rollback (the stock
pair A) stayed intact throughout. Evidence: `wifi-image-3-flash/` + the ledger
2026-11-26 18:00.

**This is a bench install, not a consumer install:** it requires the pair-A
stock boot + the UART + the TFTP recovery door + a human, and it writes one
slot. That is the honest scope of what has been executed.

## Path B - the upstream target's sysupgrade (NEVER RUN)

The target built from the (now PR-gate-cleaned) skeleton produces a
6,267,199 B sysupgrade container (sha256 `b991f4c4...`), the kernel slice
(3,370,590 B, gate `0xd7b7ab1e`) and the two-volume UBI rootfs. **No device has
booted an image built from the upstream target.** That is the PR-READINESS's
item 3.1 and SUBMISSION-STATUS's open item: the target recipe is host-build-
verified only.

The `DEVICE_PACKAGES`/factory-stanza status in `image/luofu.mk` is deliberate:
the install path is an open question, and a factory/recovery stanza must not be
added until one path has been executed end to end on a recovered unit (see the
in-file TODO).

## What a first consumer install path would need (the honest gap list)

1. **The A/B selector model** - the device uses the pair selector + the env
   `bootflag`; an OpenWrt sysupgrade must read/write that model, and the bench
   line today does it by hand (the pair switch + the `ubiformat`).
2. **The kernel-slot write** - the upstream sysupgrade carries a kernel slice;
   the executed bench install wrote only the rootfs (the kernelb slot stayed
   pinned). A real install must write the kernel slot safely with a rollback.
3. **One attended boot of a target-built image** - the bootstrap of Path B on
   a recovered unit (the UART + the TFTP door + a flash-readback verification).
4. **The recovery door** - the physical RESET+TFTP path is the mandatory
   fallback and is already documented (BENCH-FACTS / RUNBOOK).

## The verdict

Path A: executed and accepted. Path B: host-built, never executed. The
install-path clause of the objective is therefore **partially satisfied**: the
delivery mechanism is proven at the bench (Path A), but the consumer-grade
install path (Path B + the A/B selector + the kernel-slot write) remains the
open item - and it is bench-attended work, not host work.
