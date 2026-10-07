# luofu-pcie

Stage-2 **driver frame** for the Hi5671Y "luofu" PCIe root complex
(HiSilicon luofu / OpenWrt 22.03.6, kernel 5.10.201) - two Synopsys DWC RC
domains, matching the `hisilicon,luofu-pcie` node in
`opensource/docs/soc/luofu-r116.dts`.

It carries the platform-driver shape (`of_match_table` + `probe`/`remove`) and
the register plan from `build/tmp/inta-spec/pcierc.md` (register receipt:
`build/tmp/inta-spec/pciskel.md`; the frame's write-path staging:
`build/tmp/inta-spec/pcidrv.md`), distilled from the vendor `hi_pcie.ko`
(`rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_pcie.ko`, disassembled with
`lab/ko_disasm.py`) against the pinned DTS.

## What it does

Probe maps the three per-`pcierc.md` windows and runs a read-only status
inventory - **zero register writes**:

| window | CA (RC0 / RC1) | what the frame does |
| --- | --- | --- |
| `dbi`  | `0x10160000` / `0x10164000` | mapped + read: the RC's own config header (vendor/device id), DWC Link Control/Status, the port-logic Link Width-Speed Control, and the iATU register file (`DBI+0x900..0x91c`) |
| `cfg`  | `0x50000000` / `0x68000000` | mapped + read: the downstream dev-0 config header (`59e7:0005`, class `0x028000`, subsystem `19e5`) and the endpoint Link Status |
| `misc` | `0x10161000` / `0x10165000` | **mapped, write-only, never read** - the "port-logic/app" block is write-only host-side; ioremap alone performs no bus access, so the frame maps it ready for the staged mode/LTSSM writes |

`readl()` is the only MMIO op in the module. The `misc` window is never read: a
read-only userspace `devmem` of it panicked the box
(`docs/phase20/host-window.md` C.4). The vendor's link/LTSSM reads
(`misc+0x100`/`misc+0x110`) are therefore replaced by the safe DBI/cfg Link
Status DL_ACTIVE bit (bit 13) per `pcierc.md` sec 4b.

## The accessor and the link-state read path

Every register field is fetched through `luofu_pcie_read()`, the **aligned
dword-field accessor**: one 4-byte-aligned `readl()` of the dword that *contains*
the field, then a shift/mask to the field's byte lanes. This window answers only
4-byte-aligned 32-bit accesses, so a direct read of a 2-mod-4 offset (the 16-bit
Link Status at `0x082`) external-aborts whatever the width - the fix is the
aligned read, not a narrower accessor (`readw.md`, `rcfix.md`,
`pciskel-smoke.md`).

The **link-state read path** (`struct luofu_pcie_link_state` +
`luofu_pcie_read_link_status()` / `luofu_pcie_report_link()`) reads the Link
Status DL_ACTIVE bit (bit 13), link-training (bit 4) and negotiated speed
(bits 3:0) from `dbi+0x082` and `cfg+0x082`. This is the safe substitute for the
read-forbidden `misc+0x110` LTSSM word; `luofu_pcie_report_link()` is the
reusable link-up predicate the staged bring-up polls.

## force_probe

The vendor kernel's live DT carries `hsan,pcie`, not this driver's
`hisilicon,luofu-pcie`, so probe never fires on a vendor boot. `force_probe=1`
registers two name-matched platform_devices (RC0/RC1, no `of_node`) that bind
through `platform_match()`'s name compare; probe maps the pinned CAs with
`devm_ioremap()` (the regions are already owned by the vendor `hi_pcie`) and
logs the decoded inventory + link state. It is a no-op the day a luofu DT node
exists.

## Build / smoke

The module **compiles** (and performs no register writes) in the CI cross-build
against the vanilla 5.10.201 arm headers, via
`.github/workflows/lab-module-build.yml` (matrix target `lab/luofu-pcie`,
triggered on `omo/**`) and the `build-load-test-module.yml` lane; the resulting
`luofu-pcie.ko` is uploaded as the `luofu-pcie-ko` artifact.

Smoke on the router (`insmod_rc=0` / `luofu-pcie` / `rmmod_rc=0`): with no DT
match, no probe runs unless `force_probe=1` is passed, so the load proves
vermagic/ABI only.

## Staged next (the write path)

The frame deliberately performs **zero register writes**. The staged bring-up
(`pcierc.md` sec 2, `pcidrv.md`): the `clk_bulk_enable` / 4x
`reset_control_deassert` from `&crg` (the CRG dependency), the `misc` writes
(mode / LTSSM / linkdown irq - write-only, via aligned read-modify-write), the
iATU programming, the endpoint power-up via `pcie-gpios`, and
`pci_scan_root_bus_bridge` registration with the vendor's config-space
`pci_ops` shape.
