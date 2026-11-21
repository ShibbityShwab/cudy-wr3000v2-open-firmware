# W22 release integration: patch placement, manifest, validation, images

Status 2026-11-21. Prepared host-side after M1a was proven (proof image 5800c9ab). The two patch
drafts are in this directory; nothing below has been applied to the release series or the device
by this session.

## 0. Validation already done on the drafts (host-only)

- patch(1) apply to the release tree: both drafts apply cleanly (no fuzz, no rejects) to the
  current luofu-switch.c/.h/regs.h and luofu-pie.c of the release-line tree.
- compile: applied to a configured linux-6.18.60 tree (copy of the quickcheck build tree, which
  carries the same driver sources); commands:
      make drivers/net/ethernet/hisilicon/luofu-switch.o
      make drivers/net/ethernet/hisilicon/luofu-pie.o
  both build with RC=0 (the only build warnings are the pre-existing tools/ ABI-header notices).
- checkpatch: 0 errors / 0 warnings / 0 checks on both patches
  (scripts/checkpatch.pl --no-tree --no-signoff --patch < file).
- NOT done: boot/runtime validation of the kernel implementation (the hardware proof used the
  userspace replay of the same sequence), full target build, release-line validator run.

## 1. Stage the patches into the release line

From the repository root (review both files first):

    W=opensource/docs/incoming/lsw-forwarding/w22
    P=opensource/target-skeleton/target/linux/hisilicon/patches-6.18
    cp $W/0001-net-lsw-napt-init.patch            $P/059-net-lsw-napt-init.patch
    cp $W/0002-net-pie-exception-delivery.patch   $P/060-net-pie-exception-delivery.patch
    sha256sum $P/059-net-lsw-napt-init.patch $P/060-net-pie-exception-delivery.patch

059/060 sort after 058 and before the excluded 062-065, so the target build order is correct.
No existing patch numbers change.

## 2. Manifest and release-line bookkeeping

Insert these two lines into opensource/upstream/series-6.18/release-line/manifest.tsv immediately
after the 058 line (tab-separated; the hash is of the patch file content):

    include	B	059-net-lsw-napt-init.patch	cfd7debc3908e0b52785afd5ffac979c8c5e03538b3f98dccbf54738fa98152f	LSW NAPT/flow engine init (W22, native CPU delivery)
    include	B	060-net-pie-exception-delivery.patch	8c748061fe0577cea471534f209e68258bc707d45746c25d4f40409f6eeb1bd3	PIE native exception-class delivery (W22, retire accepterr)

Then update the counts/tables in:
- opensource/upstream/series-6.18/release-line/INCLUDED.txt (add the two names);
- opensource/upstream/series-6.18/05-release-line.md section 1: **L0 62 -> 64 patch files
  (001-060 + 062-065), L1 51 -> 53**; the "added by this update" text and any numbered list of
  included patches;
- the release-line README/patch list rows used by the curated repos if they enumerate patches.

## 3. Command-line (target tree, not a kernel patch)

File: opensource/target-skeleton/target/linux/hisilicon/luofu/config-6.18 line 554
(CONFIG_CMDLINE, CONFIG_CMDLINE_FORCE=y).

Current string contains the mirror-era tokens:
    ... luofu_switch.pfe=1 luofu_switch.ifc=1 luofu_switch.ext=1 luofu_switch.cpu_mirror=1
    luofu_switch.mirror_dst=4 luofu_pie.arm=1 luofu_pie.rxcfg=1 luofu_pie.accepterr=1
    luofu_pie.cpucopy_deliver=1 luofu_pie.cpucopy_port=11 luofu_gmac.macregs=1 oops=panic panic=0

New canonical string (mirror and accepterr/cpucopy tokens removed, naptinit explicit):

    CONFIG_CMDLINE="ubi.mtd=rootfsb ubi.block=0,0 root=/dev/ubiblock0_0 rootfstype=squashfs rootwait console=ttyS0,115200 earlycon noinitrd cma=0 maxcpus=2 nr_cpus=2 additional_cpus=1 luofu_switch.arm=1 luofu_switch.egress=1 luofu_switch.qidmap=1 luofu_switch.eps=1 luofu_switch.qminit=1 luofu_switch.pfe=1 luofu_switch.naptinit=1 luofu_switch.ifc=1 luofu_switch.ext=1 luofu_pie.arm=1 luofu_pie.rxcfg=1 luofu_gmac.macregs=1 oops=panic panic=0"

Fallback command line (keep for a recovery image and document in README/RELEASE-NOTES): the same
string, replaced by naptinit=0 and appended with the mirror tokens:

    ... luofu_switch.pfe=1 luofu_switch.naptinit=0 luofu_switch.ifc=1 luofu_switch.ext=1
    luofu_switch.cpu_mirror=1 luofu_switch.mirror_dst=4 luofu_pie.arm=1 luofu_pie.rxcfg=1
    luofu_pie.cpucopy_deliver=1 luofu_pie.cpucopy_port=11 luofu_gmac.macregs=1 oops=panic panic=0

Truth updates that mention the old tokens (search -n 'cpu_mirror=1|accepterr=1|cpucopy_deliver=1'
under opensource/target-skeleton): README.md (~lines 1327-1335), RELEASE-NOTES.md (~74, 94-95),
docs/port-notes/qm-decode/SUMMARY.md (~27-43, historical - add a native-forwarding note instead of
rewriting the old record), docs/incoming/feature-inventory/REPORT.md (~418). Also update the
repo-root STATUS.md/EVIDENCE-INDEX/ledger per the normal end-of-session process (lead).

## 4. Release-line validator and review checks

1. Release-line validator (host-only, pristine 6.18.60):
       opensource/upstream/series-6.18/tools/check-release-line.sh \
           <linux-6.18.60.tar.xz> build/linux-kbuild/waves/2026-11-21-w22-validate
   Expected: every include patch applies in manifest order with RC=0, no rejects, DTS contract
   checks pass. The new patches are included via the manifest.
2. Re-run checkpatch on the staged files (same commands as section 0).
3. Full target build (OpenWrt, canonical config) and kernel compile with WERROR; verify no new
   warnings in the two touched drivers.
4. Review form (L2 upstream series): W22/W22b are **release-line only** for now. They are
   hardware-proven behaviour but the RE-grade story (vendor decode, carveout contract) is in this
   lane, not in the upstream review doc; do not add to opensource/upstream/series-6.18/series
   until the lead decides the review story and renumbers. If they are added later, they become
   the next review-form numbers (currently 001-026; the release line is separate).

## 5. Image rebuild and hardware validation (attended, lead-run)

1. Build the canonical image from the staged release line + new command line; record the sha256.
2. Cold-boot test: 2 CPUs, 2 online, LEDs/event0, no Oops/BUG/panic; dmesg contains
   "W22: NAPT/flow engine initialized and verified"; luofu_switch_ready() true (GEMAC netdevs
   register); network with **no mirror tokens in the command line**: host ping 0% loss, SSH
   works; UART RXEXC / RXDIAG shows native port=11 deliveries (status class 77 as on the proof
   image).
3. Reboot once and repeat the ping/SSH check (boot-to-boot determinism, W22 idempotent guard not
   needed on a fresh boot but the stage must run on every boot).
4. Fallback image test: build/flash the fallback command-line variant (naptinit=0 + mirror
   tokens) and confirm the old mirror path still works (ping/SSH), so the fallback is real.
5. 15-minute ping/soak + link flap/cable re-plug; verify SSH recovers, native delivery persists
   (M1b will formalise this) and no RX drop/error growth.
6. Keep the previous canonical image hash and the stock pair A untouched for rollback; broker
   install path unchanged.

## 6. Curated sync and rollback

- After the bench pass: commit the staged patches + target config/docs to target-skeleton, update
  the release-line manifest/docs/hashes, then resync the curated repos (cudy-wr3000v2-openwrt and
  the public notebook) per PUBLISH.md; regenerate EVIDENCE-INDEX/STATUS/ledger.
- Rollback plan: the fallback command line and/or the previous canonical image; the stock pair A
  is untouched. The W22 driver stage can be disabled with luofu_switch.naptinit=0 without
  touching the image, which is exactly the fallback path.
