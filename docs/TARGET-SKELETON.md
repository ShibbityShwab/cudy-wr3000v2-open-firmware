# Target skeleton (schematic, untested) - index

**Status 2026-11-18: UNTESTED / SCHEMATIC / NOT A WORKING TARGET.**
The concrete files live in [`../target-skeleton/`](../target-skeleton/README.md); this page is
only an index into them. The source of truth for the design remains
[TARGET-DESIGN.md](TARGET-DESIGN.md) sections 4-6 - if this page and TARGET-DESIGN.md
disagree, TARGET-DESIGN.md wins and this page is stale.

**What it is.** A first-pass, reviewable shape of the OpenWrt target files the design
describes: `target/linux/hisilicon` (target Makefile, Kconfig stub, `luofu` subtarget with
`target.mk`, `config-5.10`, `image/Makefile`, device profile, board.d/02_network skeleton),
a draft upstreamable board DTS derived from the lab DTS, and a fail-closed FIP-slice tool.
Every file is marked SCHEMATIC/TODO where it is not real.

**What it is not.** Not buildable, not booted, not accepted upstream, not a submission.
No OpenWrt target, image recipe, driver series, binding, sysupgrade `platform.sh`, install
path or device page exists (TARGET-DESIGN.md section 9 "Explicitly not met today"). Nothing
in the skeleton has been built inside an OpenWrt or kernel tree. The only executable is
[`target-skeleton/tools/fip-slice.py`](../target-skeleton/tools/fip-slice.py), and its safe
path is the read-only `check` mode: it validates a slice against the measured loader
constraints (uImage + fixed 40,732-byte trailer, gate magic `0xd7b7ab1e` at
`0x40 + ih_size + base-DTB totalsize`, 8,650,752-byte slot budget) and refuses to generate
a trailer or skip the gate check.

**Why it exists now.** TARGET-DESIGN.md section 10 stage S0 is host-side work that does not
depend on the box: one DTS source with the stock partition map, binding/YAML drafts, and an
offline FIP-trailer checker that fails closed. This skeleton is the host-side artifact for
S0; it closes no bench or upstream gate and is not evidence of hardware progress.

**The facts it encodes (all from TARGET-DESIGN.md, which cites the bench ledger):**

- kernel slot = legacy uImage + fixed **40,732-byte** FIP trailer, `<= 8,650,752` B;
- gate magic `0xd7b7ab1e` at `0x40 + ih_size + base-DTB totalsize` (phase42-decode);
- rootfs slot `<= 24,313,856` B, squashfs in a static UBI volume `squashfs`;
- `rootfs_data` stays **JFFS2** for the first submission;
- A/B sysupgrade writes only the inactive pair and switches the measured
  selector/environment mechanism; container metadata schema is proposed, not frozen;
- uImage load/entry must be derived from the loader staging base and the kernel's
  `TEXT_OFFSET`, not copied blind;
- stock partition map (17 partitions), no lab `lab` partition.

**What is still [U]/TODO** (full list in TARGET-DESIGN.md section 12 and in the skeleton
README): trailer regeneration and the overlay match rule; descriptor words +8..+28;
bootargs/root-selector path; install path; selector protocol for both pairs; MAC/efuse
source; switch DSA-vs-switchdev; SMP; Wi-Fi scope; board compatible vs OpenWrt device name;
kernel-config re-derivation for the shipping release.

**Publication.** This is still part of the `opensource/` half whose publication is
**NOT READY**: the history purge in [PUBLISH-READINESS.md](PUBLISH-READINESS.md) is pending.
The skeleton itself contains no vendor binaries, blobs, dumps or credentials.

**Reading order:** [TARGET-DESIGN.md](TARGET-DESIGN.md) -> [ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md)
-> [`../target-skeleton/README.md`](../target-skeleton/README.md) -> the files.
