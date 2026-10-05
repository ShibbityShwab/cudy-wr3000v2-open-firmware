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
| `docs/phase49/gic-view.md` | The GIC delivery study: the device reads its own GIC across the post-unmask ring tests, ADDENDUM 9 (2026-10-05) records the dual-sided INTA test (ROW 2 RING-PENDING-NOT-TAKEN plus VIRQ 207 owned), and ADDENDUM 13 records the max-parallel sweep: the deliberate storm with the bound tripped, the ladder ranking, the 209 witness source, and the folded CRG node with the `lab/luofu-clk/` skeleton. |
| `docs/UPSTREAM-PORT-PLAN.md` | The honest inventory and staged roadmap for our own fully-current OpenWrt (our kernel plus our drivers), with the `docs/soc/luofu-r116.dts` stage-1 skeleton, the folded `crg:` clock+reset node, and the `lab/luofu-clk/` driver skeleton. |
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
