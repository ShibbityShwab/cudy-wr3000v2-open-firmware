# Cover-email draft (v3 RFC form) - DO NOT SEND FROM THIS NOTEBOOK

- **Status:** draft for the owner to review and send manually if they choose.
  No account, list subscription or email transport is part of this package.
- **Form:** v3 submission-form revision (2026-11-21).  Nothing below is invented;
  the numbers are the ones recorded in `../04-validation.md` and
  `../05-release-line.md`.
- **No Tested-by/Reviewed-by/Acked-by is claimed** (see `TESTED-BY-POLICY.md`).

**Subject:** `[RFC PATCH 00/26] ARM: hisilicon: add Hi5671Y "luofu" (Cudy WR3000 v2) platform and driver support`

**To:** linux-arm-kernel@lists.infradead.org, devicetree@vger.kernel.org,
netdev@vger.kernel.org

**Cc:** the maintainers listed in `../02-maintainers-proposal.md` (plus the
devicetree and netdev maintainers for their subsystems).

---

Hi,

This is an **RFC, not a merge request**.  It is the first public review round
for a 26-patch kernel series adding support for the HiSilicon Hi5671Y "luofu"
SoC and the Cudy WR3000 v2.0 (board R116): ARM machine/Kconfig/early boot, CRG
clock-reset, watchdog, FMC SPI-NAND, MDIO + four PHYs, five GMACs, pinctrl,
PCIe host, thermal sensor, PIE packet engine, internal LSW switch, restart and
A/B boot selector, plus the SMP bring-up patch.

The series is deliberately split by readiness in patch 00/26: core platform
(first direction review), ready-but-draft drivers, and RFC/do-not-merge drafts
(PIE, LSW, GEMAC front end, restart, PCIe).  Individual patches are also marked
RFC or do-not-merge in the series descriptions.  Please treat the RFC group as
a request for direction, not as code ready to merge.

**DTS dependency.**  SMP correctness depends on a devicetree contract the stock
bootloader consumes before Linux runs (`/aliases/console` and
`/reserved-memory/flashinfo@80600000`).  The kernel form is kernel-only, so
the board devicetree ships as the separate DTS-series entry **DTS-0001**
(`../dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`,
sha256 `886f3ad0...`, 694 lines); it must be posted together with kernel patch 0018.
0018 without the DTS contract is inert.  The base DTB totalsize needs 8-byte
alignment for the stock bootfip gate-word load; the target build pads it with
`pad-dtb.py` (12,678 -> 12,680 B; see `../06-dts-series-dependency.md`).

**What has been validated (host-only):** sequential application of the 26
patches to pristine 6.18.60 with `patch(1)` and `git am` (identical trees,
3 offset hunks, 0 fuzz); checkpatch 0 errors / 58 explained warnings; the
release-line validator (51 patches) applies with 0 fuzz and strict
`git apply` is clean; the board DTB compiles with the loader contract present
and DTS-0001 is byte-identical to the single source.  There is **no** upstream
Tested-by: the only hardware evidence is target-image bench evidence for the
target's own build (DTS-purpose image 6e9c54d0: stock loader staging PASS,
2 CPUs, `GIC-0 29 Level twd`, cmdline ubi.mtd=rootfsb + project marker;
previous DTS-purpose baselines c3ef2a50, f6685b80, 7421bb7a).  Host checks are
necessary but not sufficient.

**ZERO remaining blockers before merge intent:** all documented blockers are
closed by individual hardware boot-tests (TWD f6685b80; MAC/switch/compatible
c3ef2a50; flashinfo 6e9c54d0).  The last one was the
`hsan,flashinfo_reserved` loader binding: the adopted DTS uses a documented
`hisilicon,luofu-flashinfo` compatible first with the vendor string as
fallback, and the stock loader accepted it.  The drafted reserved-memory
binding remains maintainer-facing documentation.  Pinctrl consumer states,
consumer clocks, the A/B selector protocol beyond the measured cell, LED
description, Wi-Fi and thermal support are out of scope for this wired-first
RFC.

I would welcome comments on the split and on the RFC group; I am not asking for
a merge in this round and I will not add a Tested-by that no independent party
gave.

Thanks,
[owner name/email]
