# Port plan: what "latest OpenWrt with fully open drivers and firmware" would take

Written 2026-10-01. Nothing in this file is done yet; it is the honest map, with what is already in
place and what each path costs.

## The three facts that shape everything

1. OpenWrt 25.12.5 runs kernel 6.12 on mainline drivers. This SoC (HiSilicon Hi5671Y, `hsan luofu`,
   board R116) has **no mainline support at all**: clock, pinctrl, PCIe, NAND, Ethernet MAC and the
   internal switch would all need new drivers plus a device tree.
2. The Wi-Fi chip (Hi5622V100) is two PCIe endpoints at `59e7:0005` with 16 MB + 16 KB + 8 MB BARs each.
   There is **no open driver and no public register documentation**, and the radio firmware runs on the
   chip's own processor. "Fully open drivers and firmware" therefore means writing both.
3. Linux has no stable module ABI, so the vendor's kernel-5.10 binaries cannot be loaded into a 6.12
   kernel. Any hybrid arrangement needs the vendor's source to recompile and port.

## Path A - vendor GPL sources (shortest, external dependency)

Trigger: Cudy answers the GPL request sent 2026-10-01 (kernel + U-Boot + build config; the Wi-Fi module
itself declares `license=GPL`, which is the strongest lever for getting its source too).

1. Build the vendor SDK for `luofu` / `hi5671y` from the received sources; compare the produced kernel
   and modules against our dumps (we hold every partition) and against the shipped `vermagic`.
2. Produce a from-source firmware, write it to slot B, verify exactly like the 0.3 release (hash, boot,
   services, recovery drill).
3. Diff vendor sources against mainline: the usual upstream candidates are clocks, pinctrl, PCIe host,
   NAND and the Ethernet MAC/switch.
4. If the Wi-Fi driver source is included, port it to a current kernel and evaluate which parts are
   upstreamable (the host-side MAC layer is large and clearly written in C - see the black-box dossier).

Ceiling: a 22.03-era SDK userland unless it is replaced; a modern userland on a vendor kernel is
possible in principle but is its own large project.

## Path B - clean-room (no vendor cooperation)

1. Register map: the driver's disassembly plus the 16 MB/8 MB BARs give the address ranges; MMIO reads
   and writes can be traced with kprobes on the driver's accessors.
2. Firmware loader format: `FIRMWARE.bin` is uncompressed Thumb code plus tables (see
   `docs/phase3/firmware-forensics.md`) and the INI documents the ITCM/DTCM split.
3. Wire protocol: dynamic kprobes already capture the driver<->firmware boundary (see
   `docs/phase3/wire-capture.md`); the remaining work is decoding the message structures.
4. Then a new driver, and - for "fully open firmware" - new firmware for the radio cores.

Scope: years of specialist work. b43 and ath9k are the reference points. This is a funding decision,
not a weekend project.

## Path C - what runs today (and stays the fallback)

The vendor base (currently 2.5.24, their newest release) plus our layer: root SSH, package manager,
hardened systime page, calibration toolkit, verified A/B slot switching and recovery. Kept current with
each Cudy release.

## What is already in place (de-risks Path A)

- Full partition dumps, the verified slot-switch recipe, and a recovery procedure tested end to end.
- The complete `alg` command table, the calibration snapshot/restore toolkit, and a demonstrated
  driver<->firmware capture method.
- A public record of the whole study so any future contributor starts from evidence, not zero.
