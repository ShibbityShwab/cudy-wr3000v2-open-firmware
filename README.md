# WR3000 V2.0 open firmware toolkit

Tools, build recipes, and a black-box study for the **Cudy WR3000 V2.0** router
(`WR3000V2-R116`, SoC HiSilicon **Hi5671Y**, platform `hsan luofu`, Wi-Fi **Hi5622V100** over PCIe,
vendor kernel 5.10.201, OpenWrt 22.03.6 based vendor userland).

This repository contains **only our own work**: scripts, documentation and analysis. It contains **no
vendor binaries**, no firmware images, no keys and no credentials. See `NOTICE.md`.

## What is in here

| Path | What it is |
| --- | --- |
| `docs/FLASH-PLAN.md` | The verified build, flash, A/B slot switch and recovery procedure. |
| `docs/DRIVER-BLACKBOX.md` | Black-box study of the vendor Wi-Fi stack: PCIe identity, driver symbol map, firmware load map, power/regulatory tables, the `iwpriv alg` dispatch chain, and the calibration surface. |
| `docs/phase2/` | Detailed decodes: power and regulatory tables, the `alg` dispatch map, and their verification report. |
| `docs/phase3/` | Full `alg` command table, kernel-trace wire capture, firmware-blob forensics (added by the analysis lanes; see the files for their current state). |
| `docs/systime-rce-writeup.md` | The command-injection finding in the vendor `systime` page (also published as a gist). |
| `docs/phase49/gic-view.md` | The GIC delivery study: the device reads its own GIC across the post-unmask ring tests, ADDENDUM 9 (2026-10-05) records the dual-sided INTA test (ROW 2 RING-PENDING-NOT-TAKEN plus VIRQ 207 owned), and ADDENDUM 13 records the max-parallel sweep: the deliberate storm with the bound tripped, the ladder ranking, the 209 witness source, and the folded CRG node with the `lab/luofu-clk/` skeleton. ADDENDUM 17 (2026-10-05) records the take probe: the IRQ-vector canary proves the device CPU DOES enter the firmware ISR (`V_CNT=4`, a valid entry CPSR, the handler's live window ran), but HPPIR names id `0x1D`/`0x40` and never `0x4c`, so the take is real and the `0x4c` service is a priority/sequencing matter, not a stuck mask. ADDENDUM 21 (2026-10-06) records the bracket (take5): the three-instant instrument (E5 the ring, I5 the ISR post-EOI, F5 the gate fall-through) was built and verified (instrument verdict CONFIRMED, the emitted-bytes check PASS and still rejects take3), but the boot died at the completion marker with no `omo-drv1` output and no evidence dir, so `vrun18` labels it NO-SAMPLE, a harness/run failure, and no branch row closes. ADDENDUM 22 (2026-10-06) records the stuck-active (take6): the gate 21a named is a PRIORITY-0 SOURCE HELD ACTIVE (`E5_RPR=0x00` at the ring, `0xFF` at the ISR's own post-EOI, `F_HPP=0x4C` the same boot that read `E_HPP=0x3FF`), the SGI bank and the group enable are REFUTED, and the sampler that would name the source stalled at the same completion marker the take5 bracket did. ADDENDUM 23 (2026-10-06) records the take6f capstone: the read-only fast sampler (`GICC_RPR` x16 plus the sticky bit and the word-0 active bank) and the 66-B ranked `GICC_EOIR` force, built and verified (`vtool19` CONFIRMED, 10/10 emitted), but the boot was NO-SAMPLE BY CONSTRUCTION - the vendor stack took the endpoint and our blob was never read into the chip. ADDENDUM 24 (2026-10-06) records the take6f RE-RUN: the mitigation held, the takeover bound the endpoint, every active pad deposited its sentinel, and the sampler NAMED the stuck id as SGI 2 (`STK_ACT=0x00000004`, sticky `0x00`), and the ranked EOIR force DROPPED the running priority (`STK_RPR1=0xFF` against `STK_15=0x00`, the capping proof's first half, with `F_HPP=0x4C` one site over); the cycle boot came up on the wrong image slot (`mtd13 "rootfsa"`), so the gate refused the cells as GATE-REFUSED on `INSTRUMENT_GATE=NOT_HELD_WRONG_IMAGE_SLOT` - the first complete take6/take6f reading of the arc, measured but uncertified. ADDENDUM 30 to 32 (2026-10-07) continue the arc to the capstone: the take7c re-run proved both gates passed while the store still did not land, the ADDENDUM 31 RCA named the write's `[12:10]` source-PE field as the residue and shipped the one-byte `take7d` fix, and the take7d cycle then RETIRED the priority-0 epoch (`TG_RPR 0x00 -> 0xFF`, `TG_ACT0` cleared). ADDENDUM 33 (2026-10-07) records the six-lane CODE sprint that ran alongside it: the expanded mach DT, the CRG controller, the pinctrl mapping tables, the Wi-Fi service skeleton, the DWC PCIe frame and the kernel-config lane, five of them CI-green and four smoked live on 2.5.24 (three clean load/unload pairs, one kernel refusal on the vendor's `luofu-pinctrl` driver name). ADDENDUM 34 (2026-10-07) records the FIRST OWN-KERNEL WINDOW, whose honest headline is that it was never a flash: the prep named the missing build items (`mach-luofu`, our DTB, three config groups) and the GO/NO-GO gate passed five of its seven checks while failing the shape one, because the CI candidate `uImage + DTB = 10,153,079 B` is `1,502,327 B` OVER the 8,650,752-byte `kernelb` partition (`mtd12`), so the runner FAILED CLOSED with zero device mutation, the backup verified before any (non-)write, the rollback stayed ready and the box ended healthy on 2.5.24 slot B with `boot_id` and the `kernelb` sha256 unchanged. |
| `docs/UPSTREAM-PORT-PLAN.md` | The honest inventory and staged roadmap for our own fully-current OpenWrt (our kernel plus our drivers), with the `docs/soc/luofu-r116.dts` stage-1 skeleton, the folded `crg:` clock+reset node, the `lab/luofu-clk/` driver skeleton, the arm-A CRG write-path status block (stage-1 no-op FAILED on gate 4 `led_pwm` `0x14` b`0x0e`, so the stage-2 `pcie0` flip stays queued), and the arm-B PCIe-RC status (the aligned-dword read that panicked two kos now returns on BOTH RCs live, so what remains is the from-scratch host controller); the six-lane 2026-10-07 code sprint is recorded in the status block at the file's end, followed by the FIRST OWN-KERNEL WINDOW's status: the flash phase is QUEUED and this window ABORTED before any write, because the CI candidate (10,153,079 B) is over the 8,650,752-byte `kernelb` partition, so nothing was staged, erased, written or booted and the box stayed healthy on 2.5.24 slot B. ADDENDUM 35 (2026-10-07) records the SLOT RESTORE that put the box back on the stock pair: the vendor sysupgrade wrote STOCK 2.5.24 into the inactive slot (mtd14/rootfsb) and rebooted onto it, so slot A holds stock 2.4.15 again (the pre-slot this boot left) while slot B's own factory `kernelb` was never touched, the custom rootfs is gone, and the box ends healthy (4 AP vaps ENABLED, `alg:[SUCC]` x14, 6 netdevs, no `.omo-off`, live on 80/443). The restore is CONFIRMED by `build/register-dumps/diffs/20261007T0822Z-vrestore/verdict.txt` with one honest residual: the restored slot does not run ssh, so the MANDATED NEW-`boot_id` gate could not be read and the reboot rests on the release change plus the slot switch plus a single fresh boot log. |
| `tools/make_custom_tar_v3.py` | Builds a custom rootfs tar from a stock rootfs tar you extracted from **your own** device. |
| `tools/wifi-cal-snapshot.sh` / `wifi-cal-restore.sh` | Snapshot and restore the Wi-Fi calibration state, dry-run by default. |
| `tools/web-login.sh` / `rce-probe.sh` | Log into the vendor web UI and probe the systime page (for testing your own fix). |
| `tools/wifireg.sh` | Read a Wi-Fi chip register from userspace by its config address, on either radio (endpoint 0 or 1), using the verified BAR0 offset. See `docs/HAZARDS.md` before using it. |
| `tools/proxy.js` | Tiny CONNECT/HTTP proxy, used to give the router internet access through a PC over SSH when it has no WAN. |

## The short version of the build

1. Get root on **your own** device (the systime finding in `docs/systime-rce-writeup.md` is one path on
   the tested firmware; the UART header is the documented recovery path in `docs/FLASH-PLAN.md`).
2. Dump every partition, and extract the vendor rootfs from your device's own firmware image.
3. Run `tools/make_custom_tar_v3.py` (edit the paths at the top) to produce a rootfs tar with our layer:
   a dropbear init script, your `authorized_keys`, a firmware marker, a README, and the package-manager
   configuration; optionally a quote-stripping wrapper for the systime page as defence in depth.
4. Pack it with `tar2sqfs`, `ubinize` it on the device, and flash the **inactive** slot with
   `ubiformat`. Keep the other slot stock, always.
5. Switch slots with the env blocks plus the `boot_reg` register exactly as `docs/FLASH-PLAN.md`
   describes, then verify from inside the booted system.

## Safety

- Never flash without a full dump of every partition and a documented recovery path.
- Keep one slot stock at all times; the slot switch recipe in `docs/FLASH-PLAN.md` is the way back.
- The calibration toolkit is dry-run by default on purpose: read `wifi-cal-restore.sh` before `--apply`.

## Legal

Our scripts and documentation are MIT licensed (see `LICENSE`). Vendor components are not included and
remain the vendor's. Note that the vendor Wi-Fi kernel module declares `license=GPL` in its metadata
while shipping as a binary; if you need its source, ask the vendor for it under GPLv2 section 3.
Everyone using this toolkit is responsible for the radio regulations of their own country.
