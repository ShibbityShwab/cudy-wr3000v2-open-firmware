# luofu-pcie

Stage-3 **evolved driver** for the Hi5671Y "luofu" PCIe root complex
(HiSilicon luofu / OpenWrt 22.03.6, kernel 5.10.201) - two Synopsys DWC RC
domains, matching the `hisilicon,luofu-pcie` node in
`opensource/docs/soc/luofu-r116.dts`.

It carries the platform-driver shape (`of_match_table` + `probe`/`remove`), the
DWC-style dword-aligned register accessors, the LTSSM read-state machine, and a
commented host-bridge/CRG scaffold. The register plan is from
`build/tmp/inta-spec/pcierc.md` (register receipt: `pciskel.md`); this stage is
`build/tmp/inta-spec/drvpcie.md`. Distilled from the vendor `hi_pcie.ko`
(`rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi_pcie.ko`, disassembled with
`lab/ko_disasm.py`) against the pinned DTS.

## What it does

Probe maps the three per-`pcierc.md` windows and runs a read-only status
inventory - **zero register writes** on the default path:

| window | CA (RC0 / RC1) | what the driver does |
| --- | --- | --- |
| `dbi`  | `0x10160000` / `0x10164000` | mapped + read: the RC's own config header (vendor/device id), DWC Link Control/Status, the port-logic Link Width-Speed Control, the iATU register file (`DBI+0x900..0x91c`), and the DWC port-logic DEBUG words (`DBI+0x728/0x72c`) |
| `cfg`  | `0x50000000` / `0x68000000` | mapped + read: the downstream dev-0 config header (`59e7:0005`, class `0x028000`, subsystem `19e5`) and the endpoint Link Status |
| `misc` | `0x10161000` / `0x10165000` | **mapped, write-only, never read** - the "port-logic/app" block is write-only host-side; ioremap alone performs no bus access, so it is mapped ready for the staged mode/LTSSM writes |

`readl()` is the only MMIO op the read path executes. The `misc` window is never
read: a read-only userspace `devmem` of it panicked the box
(`docs/phase20/host-window.md` C.4). The vendor's link/LTSSM reads
(`misc+0x100`/`misc+0x110`) are therefore replaced by safe DBI reads (Link
Status DL_ACTIVE bit 13, plus the DWC port-logic DEBUG words) per `pcierc.md`
sec 4b.

## DWC-style register accessors (dword-aligned)

Every register field is fetched through `luofu_pcie_read()`, the **aligned
dword-field accessor**: one 4-byte-aligned `readl()` of the dword that *contains*
the field, then a shift/mask to the field's byte lanes. This window answers only
4-byte-aligned 32-bit accesses, so a direct read of a 2-mod-4 offset (the 16-bit
Link Status at `0x082`) external-aborts whatever the width - the fix is the
aligned read, not a narrower accessor (`readw.md`, `rcfix.md`,
`pciskel-smoke.md`).

`luofu_pcie_write()` is the write counterpart: read the aligned containing dword,
replace the field's byte lanes, `writel()` the whole dword back at the same
aligned address. It is `__maybe_unused` (staged) - the module stays read-only
until the write path lands.

The alignment rule is **compile-time enforced** by `luofu_pcie_check_aligned()`:
`BUILD_BUG_ON`s on every raw-dword offset (iATU 0x900..0x91c,
PORT_LOGIC_DEBUG0/1, misc offsets) fail the build if one stops being 4-byte
aligned - so the CI cross-build is the alignment gate.

## Link-training / LTSSM read-state machine

The DWC core exposes its **own** LTSSM through the dword-aligned DBI window, so
the raw LTSSM state is readable without touching the read-forbidden SoC `misc`
block:

| register | DBI offset | field |
| --- | --- | --- |
| `PORT_LOGIC_DEBUG0` | `0x728` | LTSSM state `[4:0]` (mask `0x1f`, L0 = `0x11`) |
| `PORT_LOGIC_DEBUG1` | `0x72c` | link-up bit 4, link-in-training bit 29 |

`luofu_pcie_read_ltssm()` reads DEBUG0 `[4:0]` and `luofu_pcie_ltssm_name()`
decodes it through the full 32-entry Synopsys table (DETECT.QUIET .. HOT.RESET).
`luofu_pcie_dwc_link_up()` is the mainline `dw_pcie_link_up` shape: DEBUG1
link-up bit set AND link-in-training bit clear. Both run **live** in the forced
smoke, alongside the DL_ACTIVE path (`dbi+0x082`/`cfg+0x082`) as the second,
independent predicate.

## Host-bridge + reset/clock scaffold (not compiled)

`luofu_pcie_host_register()` is a commented outline of the vendor `hi_pcie_probe`
14-step order mapped onto the modern `pci_host_probe()` API: CRG clk/reset ->
misc RC-mode + iATU writes -> endpoint power -> command/ASPM -> LTSSM poll ->
linkdown irq -> `devm_pci_alloc_host_bridge` + `pci_add_resource(mem/io)` +
`pci_host_probe(bridge)`. The CRG dependency (handshake points) is documented as
constants: the `pcie_clk` gate (`LUOFU_CLK_PCIE0|1`, gate reg `0x20` bit
`0x0c`/`0x0d`) and the four resets deasserted apb->pcs->phy->ctrl (reset reg
`0x34`, bits `0x0c..0x0f` RC0 / `0x10..0x13` RC1). It is a scaffold, not a
working host.

## force_probe

The vendor kernel's live DT carries `hsan,pcie`, not this driver's
`hisilicon,luofu-pcie`, so probe never fires on a vendor boot. `force_probe=1`
registers two name-matched platform_devices (RC0/RC1, no `of_node`) that bind
through `platform_match()`'s name compare; probe maps the pinned CAs with
`devm_ioremap()` (the regions are already owned by the vendor `hi_pcie`) and
logs the decoded inventory + link state + the DWC LTSSM state machine. It is a
no-op the day a luofu DT node exists.

## Build / smoke

The module **compiles** (and performs no register writes) in the CI cross-build
against the vanilla 5.10.201 arm headers, via
`.github/workflows/lab-module-build.yml` (matrix target `lab/luofu-pcie`,
triggered on `omo/**`) and the `build-load-test-module.yml` lane; the resulting
`luofu-pcie.ko` is uploaded as the `luofu-pcie-ko` artifact. The same workflow
runs the **alignment rule** step: fail on any `readb/readw/writeb/writew` (the
panic class) or on a non-dword-aligned literal in a raw `readl`/`writel`.

Smoke on the router (`insmod_rc=0` / `luofu-pcie` / `rmmod_rc=0`): with no DT
match, no probe runs unless `force_probe=1` is passed, so the load proves
vermagic/ABI only; `force_probe=1` additionally runs the read-only inventory.

## Staged next (the write path + host)

The driver deliberately performs **zero register writes**. The staged bring-up
(`pcierc.md` sec 2, `drvpcie.md` sec 3-4): the `clk_bulk_enable` / 4x
`reset_control_deassert` from `&crg`, the `misc` writes (mode / LTSSM / linkdown
irq - write-only full-word stores), the iATU programming, the endpoint power-up
via `pcie-gpios`, the LTSSM poll, and `pci_host_probe` registration with the
vendor's config-space `pci_ops` shape.
