# vendor-dt-notes: the vendor device trees read from the dumped mtd images

Task `st_01a10b7f` (DT-first upstream actions, 2026-10-05). Companion to
`opensource/docs/soc/luofu-r116-pinned.dts` (the pinned base DTB) and
`build/tmp/dt-spec/dt.md` (the mach mapping table). Every value here was parsed
from the dumped images by an FDT walker and re-rendered; raw artifacts are in
`build/tmp/dt-spec/` (`kernela_main.dtb`, `kernela_overlay_board0.dtbo`,
`kernela_overlay_board3.dtbo`).

## 1. What is in the images

Each flash image decompresses to one region that carries **three** device-tree
blobs back-to-back (FDT magic `d00dfeed`, version 17 / last-compatible 16):

| blob | offset in gunzip'd image | size | sha256 (blob) | what it is |
| --- | --- | --- | --- | --- |
| base DTB | `0x418610` (kernela/kernelb), `0x61798` (uboota/ubootb) | 31,220 | `947ec62d7fba2aed624166a585a5001f5385befba962f071f19ed468dfa270e3` | the board DT, 177 nodes |
| overlay board_id=0 | `0x420064` | 4,592 | `fe7df5eaf7a8a6f8d2079896d098cff9de103eea27d9b3d30322b539dfaa2ba7` | U-Boot DT overlay for the reference `ax3000_lite` board |
| overlay board_id=3 | `0x421254` | 4,920 | `17fd44ed9513580a1aea630cd5631fdbfd63d79768035549a0e569eba4cb65cf` | U-Boot DT overlay for **this** board (R116) |

The **base DTB is byte-identical** across `mtd1-uboota`, `mtd2-ubootb`,
`mtd11-kernela`, `mtd12-kernelb`. So there is no separate "U-Boot DT" vs "kernel
DT": U-Boot and the kernel ship the *same* static blob. U-Boot is what mutates
it at runtime (below), the kernel only receives the result. Both overlays are
libfdt-apply-overlay shape (`fragment@N` + `__overlay__` + `__fixups__`).

## 2. bootargs and chosen

`/chosen` (in the base DTB):

```
bootargs = "noinitrd cma=0 console=ttyS0,115200 earlycon maxcpus=2 nr_cpus=2 additional_cpus=1"
linux,stdout-path = "/uart1@0x1010f000"
tick-timer = "/local_timer@10180600"
```

- `ttyS0` = `uart1@0x1010f000` (aliases: `serial0`/`console` -> uart1, `serial1`
  -> uart0). The physical console is `uart1@0x1010f000`.
- `cma=0`, `maxcpus=2 nr_cpus=2 additional_cpus=1` (dual A9).
- `tick-timer` = the TWD local timer, not SP804.

## 3. memory and reserved-memory

```
memory { device_type = "memory"; reg = <0x80500000 0x7b00000>; }        // 128 MB at 0x80500000
reserved-memory { ranges;  flashinfo@0x80800000 { compatible = "hsan,flashinfo_reserved"; reg = ...; atag-offset; }; }
```

128 MB DDR3 at `0x80500000` (the low 5 MB below it are the ACP/SoC view, not
system DRAM). `flashinfo@0x80800000` is the vendor flash-info reserved window.

## 4. interrupt-parent / interrupt-map shape (virq-thread relevance)

- Root: `interrupt-parent = <0x1>` where phandle 1 is the GIC
  (`interrupt-controller`, `compatible = "arm,cortex-a9-gic"`,
  `#interrupt-cells = <3>`, `reg = <0x10181000 0x1000 0x10180100 0x100>`,
  `off-secure-status`). Standard Cortex-A9 GIC (dist + cpu interface).
- **There is no `interrupt-map` / `interrupt-map-mask` anywhere in the base DT.**
  Interrupts are flat `#interrupt-cells=<3>` GIC entries. Notable ones:

| node | interrupts | meaning |
| --- | --- | --- |
| `local_timer@10180600` (TWD) | `<1 13 0x301>` | PPI 13 |
| `timer@10104000` (SP804) | `<1 0x2f 4>` | PPI 47 |
| `watchdog` | `<0 2 4>` | SPI 2 |
| `uart0/uart1` | `<0 0x2d 4>` / `<0 0x2e 4>` | SPI 45/46 |
| `i2c0` | `<0 0x2c 4>` | SPI 44 |
| `tvsensor` | `<0 0 4>` | SPI 0 |
| `gpio0/1 port` | `<0 0x31 4>` / `<0 0x32 4>` (each `interrupt-parent=<1>`, `#interrupt-cells=<2>`) | SPI 49/50 |
| `pcie@0x10160000` | `<0 0x3b 4 0 0x45 4>` (`interrupt-names = "radm" "linkdown"`) | SPI 59/69 |
| `pcie@0x10164000` | `<0 0x3f 4 0 0x46 4>` | SPI 63/70 |
| `pie@0x10a70000` | `<0 0x58 4 0 0x59 4 0 0x5a 4 0 0x5b 4>` | SPI 88-91 |
| `lsw_woe` | `<0 0x54 4 0 0x53 4 0 0x52 4 0 0x51 4 0 0x50 4>` | SPI 84,83,82,81,80 |

- **virq thread note:** the ctrl-rb -> GIC source `0x4C` (SPI 76) that the Wi-Fi
  firmware registers and enables (phase 47) is **not described in this DT**. It
  is a device-internal delivery hop behind the PCIe endpoint, invisible to the
  host PCIe view (phase 32/34); there is no DT interrupt-map/msi-parent to
  express it. The host-side doorbell/ctrl-rb glue (`0x2d4/0x2e4/0x2e8/0x2ec/0x2f0`
  on the endpoint BAR0) is likewise absent from the DT and remains a from-scratch
  driver spec (`phase45/reconcile-luofu.md`, `phase46`,
  `phase47/THE-LAST-LINK.md`).

## 5. PCIe host bridges

Two DWC-based root complexes, `compatible = "hsan,pcie" "hsan,acp-pcie0|1"`:

```
pcie@0x10160000 {                       pcie@0x10164000 {
  linux,pci-domain = <0>;                 linux,pci-domain = <1>;
  channel = <0>;                          channel = <1>;
  bus-range = <0 0xa>;
  reg = <0x10160000 0x1000                reg = <0x10164000 0x1000
         0x10161000 0x3000                      0x10165000 0x3000
         0x50000000 0x1000                      0x68000000 0x1000
         0x40000000 0x2000000                   0x58000000 0x2000000
         0x48000000 0x800000>;                  0x60000000 0x800000>;
  reg-names = "dbi" "misc" "cfg" "mem" "io";
  iatu_rc  = <... 24 cells ...>;          iatu_rc  = <... 24 cells ...>;
  iatu_ep  = <0x2 0 0x80000000 0x30000000 0 0x307fffff 0xab000000 0>;
  resets = <0xf 0x34 0xc 0xf 0x34 0xd 0xf 0x34 0xe 0xf 0x34 0xf>;   // apb/pcs/phy/ctrl
  interrupts = <0 0x3b 4 0 0x45 4>;       interrupts = <0 0x3f 4 0 0x46 4>;
} }
```

- The `misc` window `0x10161000` is the RC misc window the **hard rules forbid
  reading** (a read-only devmem there panicked the box). It is in the DT but must
  stay untouched; measure through the endpoint BAR0/BAR2 only.
- `iatu_rc`/`iatu_ep` are the vendor's inbound/outbound iATU window tables
  (matches `phase18/inbound-map.md` Part A). `pcie-gpios`, `capability`,
  `gpio-delay` are populated by the board overlay (fragments 1/2), not the base.

## 6. NAND (FMC) + partitions

```
fmc@10a20000 {
  compatible = "hsan,fmc";  bus_id = "fmc";  spi_cs = <1>;  enable-quad-mode;
  reg = <0x10a20000 0x1000 0x1c000000 0x100000>;
  clocks = <0x3e>;  clock-names = "sfc_clk";  resets = <0xf 0x2c 0>;  reset-names = "sfc_rst";
  partitions { compatible = "fixed-partitions";  ... };
}
```

A SPI-NAND-style controller (quad mode, chip-select 1) exposing a `fixed-partitions`
A/B table (offsets decoded; sizes in bytes):

| # | label | offset | size | | # | label | offset | size |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | esbc | `0x0` | `0x40000` | | 9 | log | `0x900000` | `0x440000` |
| 1 | uboota | `0x40000` | `0x100000` | | 10 | pstore | `0xd40000` | `0x40000` |
| 2 | ubootb | `0x140000` | `0x100000` | | 11 | kernela | `0xd80000` | `0x840000` |
| 3 | enva | `0x240000` | `0x40000` | | 12 | kernelb | `0x15c0000` | `0x840000` |
| 4 | envb | `0x280000` | `0x40000` | | 13 | rootfsa | `0x1e00000` | `0x1740000` |
| 5 | fac | `0x2c0000` | `0x200000` | | 14 | rootfsb | `0x3540000` | `0x1740000` |
| 6 | bdinfo | `0x4c0000` | `0x40000` | | 15 | rootfs_data | `0x4c80000` | `0x1600000` |
| 7 | cfga | `0x500000` | `0x200000` | | 16 | upgrade | `0x6280000` | `0x1d80000` |
| 8 | cfgb | `0x700000` | `0x200000` | | | | | |

(`fac`/`cfga`/`cfgb` render in the pinned dts as string cells `"," " "`, `"P" " "`,
`"p" " "`; decoded they are `<0x2c0000 0x200000>`, `<0x500000 0x200000>`,
`<0x700000 0x200000>`.) Total NAND = `0x8000000` = 128 MB.

Note: the **newer vendor build** (the one that produced the older
`opensource/docs/soc/luofu-r116.dts`) additionally carries an `sfc@0 { compatible =
"hsan,sfc"; }` node (SPI-flash controller) that is **not** in these dumps and not
in either overlay. That is the one genuine build drift, itemised below.

## 7. Ethernet: gemac + MDIO + internal switch

```
gemac@0x14300000 { compatible = "hsan,mac"; port = <8>;  phy-mode = "rgmii"; phy-handle = <...>; reg = <0x14300000 0x10000>; resets = mac_logic/mac; }
gemac@0x14380000 { port = <9>;  phy-mode = "gmii";  reg = <0x14380000 0x10000>; }
gemac@0x14400000 { port = <10>; phy-mode = "gmii";  reg = <0x14400000 0x10000>; }
gemac@0x14480000 { port = <11>; phy-mode = "gmii";  reg = <0x14480000 0x10000>; }
gemac@0x14500000 { port = <12>; phy-mode = "gmii";  reg = <0x14500000 0x10000>; }
mdio0 { compatible = "hsan,mdio"; reg = <0x14000000 0x1000>;
        5x ethernet-phy-ieee802.3-c22 @ reg 1..5, max-speed = <0x3e8> (1G); }
lsw_dp  { compatible = "hsan,dp";       reg-names = queue/flow/dp/eps/eqm/fam/ips; port_bitmap; }
lsw_pfe { compatible = "hsan,pfe"; }
lsw_woe { compatible = "hsan,lsw_woe"; interrupts = <SPI 80..84>; }   // WoC offload
```

The base DTB enables gemac0 (port 8, RGMII). The **board overlay (board_id 3)
disables gemac0 + its PHY (`status="disabled"`)** and uses the four GMII ports
9-12 for the LAN via the internal LSW; the packet-ingress engine `pie@0x10a70000`
(`hsan,pie`/`hsan,acp-pie`, WoC) is the ring consumer.

## 8. The two U-Boot overlays (board_id gate)

Both overlays patch the same targets; the difference is the board_id/root content.
`__fixups__` resolves the targets against base symbols:

```
gemac0  -> fragment@3  (status = "disabled")
gephy0  -> fragment@4  (status = "disabled")
pcie0   -> fragment@1  (capability=3, gpio-delay="2", pcie-gpios=<gpio 0x14/0x15/0x17|0x1c>)
pcie1   -> fragment@2  (capability=3, gpio-delay="2")
lsw_dp  -> fragment@5  (reg base 0x87b00000 -> 0x80000000)
pie     -> fragment@6  (rx_ring/tx_ring/woc_*_ring)
ledpwm0 / pmupwm / pinctrl_peri -> pinctrl-0/1 selections
```

- **board_id=0** overlay: root adds `board_id=<0>`, `board_name="ax3000_lite"`,
  `vendor_id="hsan"`, an interrupt-driven `keys` node and generic LEDs
  (power/wifi1/2/3). The reference board.
- **board_id=3** overlay (this device): root adds `board_id=<3>`,
  `board_name="ax3000_lite"`, `model="hsan luofu/R116"`, `vendor_id="hsan"`, plus
  `gpio-keys-polled` (reset gpio 0x16, wps gpio 0x17, poll 20 ms) and `gpio-leds`
  (`oem:green:{status,wan,lan,internet,wifi,wifi5g}`). Its `pcie-gpios` uses
  gpio 0x1c for the third entry (vs 0x17 on board_id 0).

U-Boot selects by reading `board_id` from the fac/bdinfo store, applies the
matching overlay, then injects `ubootver-offset="512"` at the root before
handing the tree to Linux. This is the `hsan,sysenv` / `bootreg-offset=0xc08`
A/B + boot-config surface the upstream DT must reproduce.

## 9. compatible-string census (base DTB)

**Already free in mainline** (reuse, no new driver): `arm,cortex-a9`,
`arm,cortex-a9-gic`, `arm,cortex-a9-scu`, `arm,cortex-a9-twd-timer`,
`arm,pl310-cache`, `arm,sp804`, `snps,dw-apb-uart`, `snps,dw-apb-gpio`/
`-port`, `snps,designware-i2c`, `ethernet-phy-ieee802.3-c22`, `fixed-clock`,
`fixed-factor-clock`, `fixed-partitions`, `operating-points-v2`,
`pwm-regulator`, `simple-mfd`, `syscon`.

**Vendor `hsan,*` (no mainline match; new binding / port / from-scratch)**:
`hsan-luofu` (root), `hsan,clk`/`-gate`/`-mux`/`-pll` (CRG clock tree),
`hsan,crg`, `hsan,hsan-reset`, `hsan,luofu-peri-pinctrl`, `hsan,iomux`,
`hsan,fmc` (SPI-NAND), `hsan,mac`, `hsan,mdio`, `hsan,dp`, `hsan,pfe`,
`hsan,lsw_woe`, `hsan,pie`/`acp-pie`, `hsan,pcie`/`acp-pcie0|1`,
`hsan,hsan-watchdog`, `hsan,tvsensor`, `hsan,sysenv`, `hsan,sysctrl`,
`hsan,rstinfo`, `hsan,efuse`, `hsan,flashinfo_reserved`, `hsan,jent-rng`,
`hsan,ledpwm`, `hsan,pmu-pwm`, `hsan,net-cdev`, `hsan,woe_dev`, `hsan,woe_wifi`,
`hsan,hsan-clk-corrector`.

Full per-node detail (registers, resets, clocks, iATU tables, partitions) is in
`opensource/docs/soc/luofu-r116-pinned.dts`. The `hsan,*` -> mainline mapping and
the reuse/port/from-scratch call per peripheral is in `build/tmp/dt-spec/dt.md`.

## 10. Drift vs the older `opensource/docs/soc/luofu-r116.dts`

The older copy is base + overlay(board_id 3) + a **newer vendor build**. Diff of
the pinned base against it (265 diff lines) resolves to exactly three causes:

1. **Overlay application** (expected, and reproduced in the notes above): root
   props `board_id/board_name/model/vendor_id`, `gpio-keys-polled`, `gpio-leds`,
   `gemac@0x14300000` and `ethernet-phy@0` -> `status="disabled"`,
   `pcie@0x10160000` `capability=3` + real `pcie-gpios` + `gpio-timing-quirk`,
   `pie` ring sizes, `lsw_dp` reg base `0x80000000`, `ledpwm/pmupwm/pinctrl`
   pinctrl-0/1 additions.
2. **U-Boot runtime injection**: `ubootver-offset = "512"` at the root.
3. **Build drift** (the only genuine divergence): the older copy has an `sfc@0`
   node (`hsan,sfc`, phandle 0x6e) that is absent from these dumps and both
   overlays, and consequently every phandle from `pcie@0x10160000` onward is
   `+1` relative to the pinned base (`"n"`->`"o"`, `"q"`->`"r"`, ...).

The pinned base DTB in these dumps is the authoritative static artifact; the
older copy remains correct as *runtime* evidence of the effective (overlay-applied,
newer-build) tree.
