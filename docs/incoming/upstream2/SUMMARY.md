# upstream2 - one-page summary

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Task:** task-243, "the upstream OpenWrt target/DTS/image recipe for the luofu
v2 board". **Host-only:** no bench contact, no flash, no boot.
**Lane date:** 2026-11-27. **Status:** CLOSED with evidence; the target remains
NOT upstream-accepted and the image NOT flashable.

## Result

The upstream target for the Cudy WR3000 v2.0 / HiSilicon luofu R116 already
exists as single sources in [`../../../target-skeleton/`](../../../target-skeleton).
This lane (a) re-verified that the assembler installs exactly those sources
(85 files; regenerated manifest byte-identical to the previous lane's),
(b) re-ran the board-DTS compile + 24-check contract on the current 715-line
DTS (24/24 PASS, DTB 12,848 B sha `1c200daf...`), and (c) **built the device
image end-to-end** in an OpenWrt main tree (revision `27663a0`, kernel
`linux-6.18.60`, OpenWrt GCC 14.4.0), including a clean target rebuild
(`make target/linux/clean` + `make -j16 world`, RC=0).

Final image: **6,267,199 B**, sha256
`b991f4c4c4b8aefacfd8abf6de075887ee16d04a4c24fc041c1028ada1438c48`
(clean rebuild == first build, byte-for-byte). The image carries a validated
kernel slice (FIP gate magic `0xd7b7ab1e`, load/entry `0x80608000`, 3,370,590 B
of the 8,650,752 B slot), a two-volume UBI rootfs (static `squashfs` + empty
dynamic `rootfs_data`, all volume CRCs re-parsed) and a byte-consistent
sysupgrade container; the image-built board DTB is the identical 12,848 B
`1c200daf...` DTB from the host compile. It remains marked
"NOT FLASHABLE-UNTIL-INSTALLED".

## The four requested pieces

| # | piece | where |
| --- | --- | --- |
| 1 | target directory | `target-skeleton/target/linux/hisilicon/` (66 patches + metadata + image recipe + base-files + DTS + tools) |
| 2 | board DTS (CPU/memory/NAND/UART) | `target-skeleton/dts/luofu-r116.dts` (+ 90-line loader-match overlay); extracted vendor base DT stays local/gitignored |
| 3 | image recipe | `target-skeleton/target/linux/hisilicon/image/luofu.mk` + tools; uImage -> FIP slice (gate `0xd7b7ab1e`) -> two-volume UBI -> sysupgrade tar |
| 4 | assembled build | this lane: full image build + structural validation logs in [validation/](validation/) |

## What this does not prove

No boot of this exact image (the v4 active-DTS change is un-boot-tested); the
install/sysupgrade path has never run on a device (no factory image on
purpose); Wi-Fi is vendor-only/GPL-blocked; native L2/VLAN switch forwarding is
a separate project; no upstream review/PR. The full PR gap list:
[PR-READINESS.md](PR-READINESS.md); the definitive checklist:
[`../../upstream/SUBMISSION-STATUS.md`](../../upstream/SUBMISSION-STATUS.md).

## Evidence index

| file | content |
| --- | --- |
| [validation/assembly/](validation/assembly/) | assembler check/manifest/verify (manifest identical to task-238) |
| [validation/dts/](validation/dts/) | cpp+dtc compile evidence, 24/24 board contract, warning logs |
| [validation/image-build/](validation/image-build/) | build logs, artifact hashes, container/slice validation, capture script |
| [REPORT.md](REPORT.md) | the detailed report |
| [PR-READINESS.md](PR-READINESS.md) | the target+DTS PR gates that remain |