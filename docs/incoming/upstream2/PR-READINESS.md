# PR readiness - upstream OpenWrt target (Cudy WR3000 v2.0 / luofu R116)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** HOST-BUILD-VERIFIED, NOT POSTABLE. **Date:** 2026-11-27.
The definitive per-item checklist remains
[`../../upstream/SUBMISSION-STATUS.md`](../../upstream/SUBMISSION-STATUS.md)
(27 items: 5 DONE / 17 PARTIAL / 5 BLOCKED; section 8 proposes the target PR
split). This page reduces it to the gates specific to the **target + DTS + image
recipe** work of this lane, so the next lane does not have to re-derive them.
Everything below was checked against the files at
[`../../target-skeleton/`](../../target-skeleton) on 2026-11-27; nothing here is
invented.

## 1. What PR-1 ("add HiSilicon luofu target + board DTS") would carry

- `target/linux/hisilicon/Makefile`, `luofu/target.mk`, `luofu/config-6.18`;
- `image/Makefile`, `image/luofu.mk`, `image/luofu-ubinize.cfg`;
- `base-files/` trimmed to real board support (see 2.1);
- `dts/luofu-r116.dts` + `dts/luofu-r116-overlay.dtso` (the same content as
  kernel-side DTS-0001);
- kernel patch subset for a userspace-only boot: 001 (mach), 002/003
  (TEXT_OFFSET), 004 (+014 folded, CRG), 005 (watchdog), 010 (restart), 011
  (pinctrl), 018 (SMP), 019 (clock tree) - per SUBMISSION-STATUS 8.1;
- the three host tools `scripts/{fip-slice,luofu-image,pad-dtb}.py`
  (reviewer question: an OpenWrt PR adding scripts/ needs a justification that
  they are target-build tools, not local lab helpers).

## 2. Mechanical gates (host-doable; no hardware needed)

1. **Trim bench-only target files.** `base-files/etc/init.d/heartbeat` and
   `base-files/etc/init.d/luofu_record_pair` are bench instruments; the
   recorder only belongs with the A/B selector support (patch 017), which is
   not in PR-1. `luofu-mac.sh`/`luofu-netdev.sh` depend on the MAC-provisioning
   decision (SUBMISSION-STATUS item 8).
2. **Delete the `Kconfig` review stub.** Its own header says it is
   "REVIEW STUB, NOT USED BY THE BUILD" and recommends deletion; the platform
   symbols belong in the kernel patch (patch 001).
3. **Finalize the package lists.** `luofu/target.mk` marks
   `DEFAULT_PACKAGES += ubifs` TODO(packages) and `image/luofu.mk` leaves
   `DEVICE_PACKAGES` commented out. Both must be decided before posting.
4. **Re-derive `luofu/config-6.18`.** The file's own header marks it a BENCH
   config (cmdline/DEVMEM/gates); an upstream target config must drop the
   bench diagnostics and document every non-default symbol (SUBMISSION-STATUS
   item 7). `luofu/config-5.10` is a reference only and should not be posted.
5. **Fix stale comments.** `image/luofu.mk` still says `cudy,wr3000-v2`
   "becomes valid once the machine descriptor's dt_compat list carries it -
   TODO"; patch 001's `luofu_dt_compat[]` already carries
   `cudy,wr3000-v2`, `hisilicon,luofu-r116`, `hisilicon,luofu`. Comment only,
   but a reviewer will notice.
6. **Bindings + dtbs_check.** All `hisilicon,luofu-*` YAML bindings are
   drafts/RFCs and are still inside driver patches; split them and run
   `dtbs_check` against submission-time mainline (the `wifi-pcie-hog` child
   needs the post-v6.18 `snps,dw-apb-gpio` hog pattern; expected warning if
   checked against the v6.18 tag). See DTS review notes in
   [`../../upstream-dts/DTS-COVERAGE.md`](../../upstream-dts/DTS-COVERAGE.md)
   section 6.
7. **MAINTAINERS + DCO.** The target needs a MAINTAINERS entry and real
   `Signed-off-by` identities; both are blocked on the owner decisions
   (SUBMISSION-STATUS items 24 and 10.1) - not an editing task.
8. **PCIe review items** if the PCIe node/driver rides along: `acp-gate` is a
   documented bring-up property that should become an ACP power/clock domain;
   no MSI/INTx; static CFG0 viewport. Per SUBMISSION-STATUS item 20.

## 3. Hardware / boot gates (not this lane; attended bench work)

1. **One attended boot of the current active-DTS image as an OpenWrt target
   build.** The 715-line DTS passed the 24-check host contract (this lane) but
   the v4 `reset-gpios` fold is an active-DTS change and no image built from it
   has booted. Earlier target-recipe boots (2026-11-19/21/24) predate the fold;
   the bench record's DTS regression rule applies (an active-DTS change needs
   its own boot test).
2. **Install/sysupgrade path never run end to end** - the only verified install
   transport is the manual stock-telnet path; `platform_do_upgrade` has never
   executed on a device (SUBMISSION-STATUS item 6, BLOCKED). This is why the
   image recipe deliberately has no factory stanza.
3. **Wi-Fi stays out of scope.** The only working radio stack is the vendor
   5.10 binary; the GPL source request is unanswered; the clean-room transport
   is readiness evidence, not a driver. The board page must say
   "[X] not supported" (SUBMISSION-STATUS item 22).
4. **Native L2/VLAN switch forwarding** is a separate LSW/PIE project; do not
   present the CPU-delivery path as a working switch (SUBMISSION-STATUS item 21).

## 4. Ordering

Post the target+DTS PR only after the minimal kernel-side series has a home
(RFC for direction first: patches 0001-0003); the DTS entry is posted with the
SMP RFC (patch 018 + DTS-0001). Driver PRs (storage, network, thermal, PCIe)
follow one subsystem at a time, after their kernel series is accepted or at
least ACKed. Wi-Fi has no PR until a mainline driver exists. Full sequence:
SUBMISSION-STATUS section 8 and its dependency graph in 8.3.

## 5. Evidence this lane contributes

- [validation/image-build/](validation/image-build/) - the full OpenWrt image build from
  the assembled target (the item SUBMISSION-STATUS section 10.5 says was still
  missing: "build the target image from the skeleton tree inside OpenWrt");
  host-only, NOT flashed.
- [validation/dts/](validation/dts/) - 24/24 board contract on the current
  compiled DTB, byte-identical to the v4 DTS-0001 content.
- [validation/assembly/](validation/assembly/) - assembly reproducibility from
  the single sources (manifest byte-identical to task-238).
