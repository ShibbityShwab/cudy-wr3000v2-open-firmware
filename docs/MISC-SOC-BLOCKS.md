# Misc SoC blocks: evidence-graded RE spec (efuse/MAC, RNG, LED/PMU PWM, CRG parents/PLL/mux, SYSCTRL/SYSENV)

**Status 2026-11-18: planning document only.** This file contains **no driver code**, and none of
the plans below has run on hardware. It is a new document; it edits no shared file. The files that
would need a one-line update *if* these plans are accepted are listed in section 13.

**Measured update 2026-11-18 (task-45, read-only):** the attended readback this document asked for
in 5.5 has been taken on one stock vendor boot: the CRG gate/reset/mux/PLL words, the live
`clk_summary` rates and the three SYSCTRL cells are now [V] (section 5.6, 6.3;
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) sections 4-5). The PLL recalc equation is
validated by the measured CPU rate; the PLL2 post-divider mapping, the corrector-rate delta and
all write paths remain open. No register write was made.

This is the companion to [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) (blocks A-E: pinctrl,
thermal, PCIe, LSW, GEMAC/PIE). It covers the blocks that page deliberately left as "decide per
block" (TARGET-DESIGN section 7.1, last rows) and that [TARGET-DESIGN.md](TARGET-DESIGN.md) open
question 5 called **[U]** ("where the unit's MACs come from ... not established in the repo").
The parent brief folded the MAC source into the efuse item; it is split out here as block F
because the answer is *not* efuse and changes the porting plan for both blocks (the remaining
five parent blocks are G-K):

| block | in one line |
| --- | --- |
| **F. MAC address source** | resolved: the factory MAC is the encrypted `bdinfo` partition, not efuse and not the U-Boot env; userspace assigns netdev MACs (section 1) |
| **G. efuse / nvmem** | register model decoded from the vendor module; read path is safe, write path is one-way and includes key material (section 2) |
| **H. RNG / hwrng** | there is no hardware RNG block: the vendor node wraps the *software* jitterentropy RNG (section 3) |
| **I. LED PWM / PMU PWM** | LED PWM has no driver and no consumer (out of scope); PMU PWM is the CPU `vdd_logic` regulator (section 4) |
| **J. CRG parents / PLL / mux** | per-gate parents, the two fixed PLLs and the 0x138 muxes are fully described by the vendor DT; patch `004` models none of them yet (section 5) |
| **K. SYSCTRL/SYSENV nvmem** | the selector side exists (patches `010`/`017`); the remaining cells are SMP release, reset record and a board-id cell this board's DT does not carry (section 6) |

Context, so the plans are read at the right altitude:

- The kernel platform patch series exists and host-compiles but has **never booted** (patch state
  per [TARGET-SKELETON.md](TARGET-SKELETON.md), [SKELETON-SMOKE.md](SKELETON-SMOKE.md)).
- There is still **no OpenWrt target, board DTS, image recipe or driver series** in any public
  tree; publication is **NOT READY** ([../README.md](../README.md), [PUBLISH-READINESS.md](PUBLISH-READINESS.md)).
- The bench-reversal line stopped at its documented ceiling (v17; parent `STATUS.md` section 3,
  ledger **2026-11-18 06:50**) and bench fires are stopped. The box is stock and idle. Nothing in
  this document may be tested by a bench fire until a recovery-attended session is planned.
- The 2026-10-01 GPL source request is de-scoped as a project action (ledger **2026-11-18 17:40**);
  vendor GPL source remains an optional accelerator only. Every claim below is from **our own**
  reading of local vendor artifacts.

## 0. Conventions, scope, and the shared-page contract

### 0.1 Evidence markers

The marker set is the one defined in [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 0.1 and
is repeated here so this file stands alone:

| marker | meaning |
| --- | --- |
| **[V]** | verified by a committed artifact in this repository, a reproducible host-side check, or a recorded measurement whose artifact is named. Where the underlying artifact is gitignored local material (a vendor `.ko`, a flash dump), the citation says so: a fresh clone cannot regenerate that check. |
| **[I]** | inferred or planned: an engineering conclusion drawn from [V] evidence, or a design decision that still needs review/bench confirmation. |
| **[U]** | unknown / unmeasured. State it; do not design around it silently. |
| **[X]** | blocked on an external dependency (vendor GPL source, blocked hardware, or an undecided project question). |

Citation convention (identical to [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 0.1): a plain
path such as `soc/luofu-r116-pinned.dts` is in this repository; a `parent:` path is in the private
research repository (not published); "ledger" means the parent `mem-entries.md` (the newest dated
entry wins). Local-only artifacts live under parent `rootfs-2.4.15/`, `rootfs-2.5.24/`, parent
`dumps/` or parent `build/` and are named so they can be re-derived; none is copied into this
repository. Disassembly was done on the host with
`parent:build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump -dr` over the
unmodified vendor modules; no vendor code was executed and no device was touched.

**Redaction rule used by this file.** Factory-per-unit data (MAC address, PIN, serial, `secret`,
UUID values) is **withheld**: this repository is the published half. Where a value matters, the
artifact and offset are named so an owner of the unit can read it, but the value is not printed.

### 0.2 Scope

In scope (the five blocks the parent lane named, plus the MAC source folded into block F):

1. the MAC-address source and how netdev MACs are wired (block F);
2. efuse/nvmem (block G);
3. RNG/hwrng (block H);
4. LED PWM and PMU PWM (block I);
5. the CRG per-gate parent/PLL/mux model (block J);
6. SYSCTRL/SYSENV cells and a possible nvmem exposure (block K).

Out of scope, with the lane that owns it: Wi-Fi (**[X]**, [TARGET-DESIGN.md](TARGET-DESIGN.md)
section 8.3), SMP/platsmp (separate reverse-engineering lane), the LSW switch and the PIE/GEMAC
data path ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) blocks D/E, [PIE-DATAPATH.md](PIE-DATAPATH.md)),
peripheral pinctrl (block A - but note the pin functions this file consumes), thermal (block B),
the bootloader/image recipe ([FLASH-PLAN.md](FLASH-PLAN.md),
[../target-skeleton/docs/TRAILER-FORMAT.md](../target-skeleton/docs/TRAILER-FORMAT.md)) and the
A/B selector protocol itself ([SELECTOR-0x21.md](SELECTOR-0x21.md)). Patch `004` (CRG), `010`
(restart/sysctrl) and `017` (boot selector) appear only as dependencies and as things this
document proposes to extend.

### 0.3 The shared-page contract, applied to these blocks

Patch `004` makes the CRG node a `syscon` + `simple-mfd` system controller and forbids any
new driver from mapping `0x14880000` itself; that rule is inherited here
([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 0.3). Three further pages are shared between
blocks and must be treated the same way [I]:

| page | owners already known | consequence for this document |
| --- | --- | --- |
| `0x14880000` (CRG, 4 KiB) | clock gates `0x14`/`0x20`, resets `0x2c`/`0x30`/`0x34`, watchdog `0x50`/`0x64`/`0x70`/`0x100`, efuse clock-source select `0x138`, PLL words `0x198`/`0x1e0`, reset-record `0x90` | efuse's clock-source cell lives in this page; the efuse **read** path does not need it (section 2.2), the **write** path does |
| `0x14900000` (peripheral control, 4 KiB) | pinctrl mux window `0x14900100..0x1490013b` (patch `011`), efuse control `0x14900140..0x1490015f`, LED PWM `oen` `0x14900110` and `loop` `0x1490011c`, thermal `0x14900500` (patch `012`), PMU PWM `0x14900800` | LED PWM's `oen`/`loop` registers **alias patch `011`'s pinctrl window**; any future LED PWM driver must share or shrink that window (section 4.1) |
| `0x10100000` (SYSCTRL, 4 KiB) | patch `010` (`hisilicon,luofu-sysctrl` syscon + reboot consumer), patch `017` (boot selector, `0xc08`), vendor reset-record cell at `0xc10`, SMP release word `0xc00` | a sysenv nvmem child would be a *third* consumer of the same syscon; selector ABI must not be duplicated (section 6.4) |

## 1. Block F - the MAC-address source (bdinfo, not efuse, not env)

### 1.1 The measured source: the `bdinfo` partition

The unit's factory MAC is stored per-unit in the **encrypted `bdinfo` MTD partition** (mtd6,
`0x4c0000` size `0x40000`; partition table
[soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts) line 864 and
[soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 6). [V]

Layout established from the local dump `parent:dumps/router-20261108/full/mtd6-bdinfo.bin`
(262,144 B, sha256 `49432e0f...fd763`) by host-side inspection only [V]:

| offset | content |
| --- | --- |
| `0x0000` | 4-byte container header `00 00 00 01` |
| `0x0004` | DES-CBC payload of a `key = value` text; zero IV, key derived with the OpenSSL `DES_string_to_key` variant from a literal in the vendor `/usr/lib/libbdinfo.so` (parent `tools/decrypt_bdinfo.py`; ledger **2026-11-08**). The tool's reproducible result is offset 4; the payload length is not a multiple of the DES block size, so its final partial block is alignment, not text |
| `0x0004..0x014f` | plaintext of the encrypted text region: `board`/`model`/`country`/`region`/`deviceid`/`mac`/`sn`/`pin`/`uuid`/`hmac`/`fuuid`/`secret`/`version` lines, 332 B, ending `BDINFO_END\n` (values withheld) |
| `0x0150..0xddff` | NUL terminator at `0x0150`, then `0xff` plaintext padding inside the encrypted region (the raw bytes there are DES output, not `0xff`); a short block-alignment tail at the end of the region does not decrypt to text |
| `0xde00..0xde05` | **plain 6-byte copy of the same MAC**, big-endian, immediately before the `0xff` fill; last non-`0xff` byte of the partition is `0xde05` |
| `0xde06..0x3ffff` | `0xff` fill |

On this unit the decrypted `mac` string and the plain copy at `0xde00` are the same six bytes
[V, value withheld]. That plain copy is exactly what the vendor's own userspace reads when it
does not want to decrypt: `mtd_get_mac_binary bdinfo 0xde00` [V] in
`parent:rootfs-2.5.24/etc/uci-defaults/99_fixwan` line 41 (and the AP-mode branch at line 72
of the 2.4.15 file, `parent:rootfs-2.4.15/etc/uci-defaults/99_fixwan`). The decrypted path is
`bdinfo mac`, a subcommand of the closed `/usr/lib/libbdinfo.so` [V].

Evidence that this is *the* wiring, not a one-off script:

| artifact | claim | marker |
| --- | --- | --- |
| `parent:rootfs-2.4.15/etc/board.d/02_network` lines 25-29 | `lan_mac = macaddr_canonicalize $(bdinfo mac)`; `wan_mac = lan_mac + 1`; both written through `ucidef_set_interface_macaddr` | [V] |
| `parent:rootfs-2.4.15/etc/uci-defaults/99_fixwan` lines 17-52 | LAN base MAC applied as `macaddr` on **every** LAN port device (`eth0/eth1/eth2` on R116), WAN gets base+1; `ori_macaddr` recorded so a clone-MAC edit is detectable | [V] |
| `parent:rootfs-2.5.24/etc/uci-defaults/99_fixwan` lines 41-75 | newer release: LAN interface MAC taken **directly** from `mtd_get_mac_binary bdinfo 0xde00`, WAN still `bdinfo mac`+1 via `macaddr_add` | [V] |
| module set | the three scripts' md5s differ between the two rootfs trees, but every kernel module this document cites is **byte-identical** in 2.4.15 and 2.5.24 (appendix A) | [V] |

Interface mapping for this board (from the same script): **WAN = `eth3`; LAN = `eth2 eth1 eth0`**
(R116). R118/R150 boards have their own lists; only R116 matters here. [V]

### 1.2 What is *not* the MAC source

- **efuse: no MAC cell exists in the vendor DT.** The complete efuse cell census (section 2.1) has
  die-id, oscillator/trim, security-key and counter cells - there is no MAC field, and no local
  module or extracted built-in code carries an efuse-MAC path (section 2.3). [V]
- **U-Boot environment `ethaddr`: not the factory value.** Both env copies carry an
  `ethaddr` (`parent:dumps/router-20261108/enva.bin` and full/mtd4-envb.bin, string offset
  `0x282`), but its first octet has the **locally-administered bit set** and it does **not**
  equal the `bdinfo` MAC (comparison done host-side, values withheld). U-Boot's own string
  table still contains `Warning: %s (eth%d) using random MAC address - %pM` and
  `*** ERROR: 'ethaddr' not set`, i.e. a random fabricate-and-save path exists in this
  loader; the saved value is therefore a loader artifact, not the factory MAC. [V]
- **`fac` partition (mtd5, 2 MiB): not a MAC copy on this unit.** The dump is `0xff` up to
  `0x180000`, then holds a ~100-record vendor config store whose record tags include
  `boardinfo`, `sysinfo` and `sysinfo.uci-*`; a byte search for both MAC encodings
  (colon-ASCII, 12-hex ASCII, 6-byte binary) over the whole partition finds **zero** hits. [V]
  Whether an obfuscated record value duplicates the MAC is [U]; nothing in the vendor userspace
  reads `fac` for it.

### 1.3 How the vendor kernel consumes it (the netdev side)

`hi_kmac.ko` (75,116 B, md5 `e85cca99...`) owns the netdev and phylink. Its probe
(`hi_mac_probe`) fills `dev->dev_addr` from `get_random_bytes()` and then forces the
locally-administered bit - the disassembly at probe offset `0xd0` is
`bl get_random_bytes`, followed by `bic r3, r3, #1` / `orr r3, r3, #2` before writing
the address; the module imports no MAC-read helper (`of_get_mac_address`,
`eth_platform_get_mac_address` and `nvmem_get_mac_address` are all absent from its
undefined-symbol list). [V] The module does import `eth_mac_addr` and
`eth_validate_addr`, i.e. it registers the standard `ndo_set_mac_address`, which is how
netifd later writes the real MAC. [V] No code path was found that programs `dev->dev_addr`
into a hardware MAC-filter register; the only other reads of that field are netdev bookkeeping and
a link-state diagnostic print. [V] for the absence of such code, **[U]** for whether the GEMAC
has a hardware address filter at all.

Consequence [I]: on this board the MAC chain is **userspace-driven** -
`bdinfo` (decrypt or raw `0xde00`) -> /etc/config/network.macaddr -> netifd ->
`ndo_set_mac_address` -> netdev address. The kernel never reads the factory MAC.

### 1.4 Port plan

**Vendor-userspace-on-our-kernel (bench lane) [I].** Nothing extra is needed in the kernel except
that our netdev accept a MAC assignment: the vendor scripts keep doing `bdinfo mac`. Our
current GEMAC draft, patch
[`009-net-gmac-luofu.patch`](../upstream/series-6.18/series/0009-net-gmac-luofu.patch), defines
**no** `ndo_set_mac_address` (grep: 0 matches in the patch) [V], so on our netdev a netifd
`macaddr` would fail. Before the hybrid v18 experiment reaches Ethernet userspace, the GEMAC
driver needs either a real `.ndo_set_mac_address` (store + validate, hardware programming
optional) or the device tree must carry the address (next paragraph).

**Upstream target (v1) [I].** Two clean options, in preference order:

1. **Userspace, no new binding - implemented host-side (2026-11-18).** The target's
   `base-files/lib/functions/luofu-mac.sh` (sourced by `base-files/etc/board.d/02_network`)
   applies the vendor's 2.5.24 recipe: `mtd_get_mac_binary bdinfo 0xde00` (with a direct
   `/dev/mtdN` fallback) for the LAN base MAC and `macaddr_add $lan_mac 1` for WAN, then
   `ucidef_set_interface_macaddr` for both; the factory partition is only read. This uses only
   OpenWrt's standard `/lib/functions/system.sh` helpers, needs no kernel code, works with the
   current Linux `of_get_mac_address` fallback (random + netifd override), and matches what
   the vendor does. It does mean the first boot must have run board.d before the interface is
   configured - standard OpenWrt behaviour. Host-validated by
   `target-skeleton/tools/test-mac-provisioning.sh` (mocked `/proc/mtd`, mtd devices and
   helpers); **it has never read bdinfo on a booted target**, and the addresses become effective
   only when the switch port map supplies the interface device/ports (TARGET-DESIGN.md 8.1).
2. **Kernel-side nvmem cell [I], larger.** The 5.10.201 tree in use *does* support
   `nvmem-cell-names = "mac-address"`: `of_get_mac_address()` falls back to
   `of_get_mac_addr_nvmem()` (`drivers/of/of_net.c:57-79,103-120`) which calls
   `nvmem_get_mac_address()` (`net/ethernet/eth.c:534-560`), requiring an nvmem cell
   **named `mac-address`, exactly 6 bytes, and a valid (non-zero, non-multicast) address**.
   The missing piece is a provider for the plain copy at `bdinfo+0xde00`: there is no
   mainline nvmem provider for an MTD-partition offset and no OpenWrt `mtd-mac-address`
   support in this tree (`grep -r mtd-mac-address` over
   `parent:build/linux-kbuild/linux-5.10.201` returns 0 hits). A tiny target-local
   `nvmem` provider over the `bdinfo` partition would be ~100 lines plus a binding, and it
   would have to read MTD in the provider's read callback. [I]

Sketch for option 2 (spec only, not written):

```dts
/* target-local; only if option 2 is chosen */
bdinfo: nvmem@4c0000 {
	compatible = "hisilicon,luofu-bdinfo-nvmem";
	reg = <0x4c0000 0x40000>;		/* mtd6, vendor partition table */
	#address-cells = <1>;
	#size-cells = <1>;
	mac_address: mac-address@de00 {
		reg = <0xde00 0x06>;		/* plain copy, section 1.1 */
	};
};

&gemac3 {
	nvmem-cells = <&mac_address>;
	nvmem-cell-names = "mac-address";
};
```

Do **not** copy this node from the vendor DTB: the vendor has no such node; it is our design.
The encrypted text region (and the closed `libbdinfo.so`) stays out of the kernel in both
options.

### 1.5 Dependencies, risk, effort

- Depends on: patch `009` (GEMAC, for `.ndo_set_mac_address`/MAC consumption), the target's
  `base-files` board.d (now written host-side: `lib/functions/luofu-mac.sh` +
  `etc/board.d/02_network`), and - for option 2 only - a new nvmem provider.
- Risk [I]: the vendor's increment rule is "last 3 bytes +1, wrap modulo 2^24" with **no**
  multicast/local-bit handling; a factory MAC that ends `...ff:ff:ff` would wrap the WAN MAC
  into the base's prefix. The base MAC's vendor OUI makes the risk small, but a target should
  validate `is_valid_ether_addr` and refuse rather than write an invalid address.
  Local-administered env `ethaddr` must never be mixed with the factory MAC: two netdevs on
  one unit would then disagree about their identity, and the "random MAC saved to env" path in
  this loader can persist a wrong one across resets. The target implementation does exactly
  that: it rejects multicast, locally-administered, all-zero, all-`0xff` and OUI-`00:00:00`
  copies plus the `...:ff:ff:ff` WAN-wrap case, logs the reason and uses a random
  locally-administered pair instead; the bdinfo partition is only ever read.
- Effort: **S** (option 1, userspace) - the userspace half is written and host-validated; the
  GEMAC `.ndo_set_mac_address` consumer is still patch `009`'s TODO. **M** for option 2
  (provider + binding + dt_binding_check). No bench required for either; option 1 needs one
  attended boot to see bdinfo read and the netdev accept the address.

## 2. Block G - efuse / nvmem

### 2.1 What is known: the DT cell map [V]

The vendor base DTB describes **four `hsan,efuse` instances** that share one controller, plus
three `syscon` windows they address (all four nodes carry the same
`efusecon-phandle`/`efusedata-phandle`/`efuseclksrc-phandle`/`clocks`/
`resets` cells). Committed references: [soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts)
lines 483-735 (`efuse` family), 738-754 (the three windows);
the authoritative decode used for every number below is the raw DTB from the dumped kernel, which
the pinned file's own provenance names: `parent:dumps/router-20261108/full/mtd11-kernela.bin` at
`0x418610`, 31,220 B, sha256 `947ec62d7fba2aed624166a585a5001f5385befba962f071f19ed468dfa270e3`
(re-extracted and re-hashed on 2026-11-18; matches the pinned DTS header). [V]

| window | address / size | role |
| --- | --- | --- |
| `efuse_clk_src` | `0x14880138` / 1 byte (inside CRG page) | efuse clock source/mux control; also the `efuse_mux` clock node (`reg-offset = 0x138`, `mask = 0x02`) |
| `efuse_control_reg` | `0x14900140` / `0x20` | command/address/data/status registers (section 2.2) |
| `efuse_data_reg` | `0x14948000` / `0x100` | 256-byte read-back window (64 words) of the fuse array |

Four providers, one per cell namespace; each has `#address-cells = <1>`,
`#size-cells = <1>`, `nvmem-cells`/`nvmem-cell-names`, `clocks = <&efuse_ring_clk
&efuse_clk &osc>` (`ring_clk`, `mux`, `osc_clk`), `resets = <&reset0 0x34 0x0>`, and
`read-only;` on the `ns` instance only. Cell tuples are `reg = <byte-offset length> :: bits =
<start width>` (DT `bits` semantics). The full census [V]:

| cell | reg | bits | namespace | notes |
| --- | --- | --- | --- | --- |
| `basic_dieid` | `0x00`/`0x14` | - | ns | 160-bit die id |
| `ext_diedid` | `0x14`/`0x06` | - | ns | 48-bit extension |
| `osc_ring_svt_freq` | `0x1a`/2 | `0 12` | ns | **CPU OPP grade: the `avs` cell** (see 2.4) |
| `bootrom_forbid` | `0x1b`/1 | `4 1` | ns | boot-ROM disable strap |
| `bootcli_forbid` | `0x1b`/1 | `5 1` | ns | boot-CLI disable strap |
| `osc_ring_lvt_freq` | `0x1c`/2 | `0 12` | ns | ring-oscillator grade |
| `osc_ring_hvt_freq` | `0x1d`/2 | `4 12` | ns | ring-oscillator grade |
| `rgmii0_tune_efuse` | `0x1f`/1 | `0 4` | ns | RGMII0 pad tuning |
| `power_grade` | `0x1f`/1 | `4 2` | ns | power/leakage grade |
| `hw_dbios_en` | `0x1f`/1 | `6 1` | ns | hardware strap; the name's expansion is not decoded [U] |
| `tsensor_trim_offset` | `0x20`/2 | `0 10` | ns | thermal-sensor trim |
| `vsensor_trim_offset` | `0x21`/2 | `2 10` | ns | voltage sensor trim |
| `ldo_trim_sel` | `0x22`/1 | `4 1` | ns | LDO trim select |
| `ldo_trim` | `0x22`/2 | `5 4` | ns | LDO trim |
| `usb2_trim_offset` | `0x23`/1 | `1 5` | ns | USB2 PHY trim |
| `cs_chip` | `0x23`/1 | `6 1` | ns | chip-select strap |
| `kgd_size` | `0x23`/1 | `7 1` | ns | key-generation data size |
| `kgd_type` | `0x24`/1 | `0 2` | ns | key-generation data type |
| `kgd_dieid` | `0x28`/`0x0e` | `0 0x6a` | ns | key-generation die id |
| `ft_crc` | `0x3f`/1 | - | ns | final-test CRC marker |
| `msk` | `0x40`/`0x20` | - | chip_key | **device-unique key material - never expose or burn** |
| `huk` | `0x60`/`0x20` | - | chip_key | hardware-unique key |
| `jak` | `0x80`/`0x20` | - | chip_key | JTAG-authentication key |
| `pubkh` | `0xa0`/`0x20` | - | sec_ctrl | public-key hash |
| `sec_boot_en` | `0xc0`/1 | `0 1` | sec_ctrl | secure-boot enable |
| `jatg_auth` | `0xc0`/1 | `3 2` | sec_ctrl | JTAG auth level |
| `jatg_efuse_forbid` | `0xc0`/1 | `7 1` | sec_ctrl | **permanently disables JTAG efuse access** |
| `product_id` | `0xc1`/4 | - | sec_ctrl | product id |
| `revokedkeymask` | `0xd0`/4 | - | online | revoked key mask |
| `blnvcounter` | `0xd4`/4 | `0 31` | online | boot-loader non-volatile counter |
| `blnvcounter_st` | `0xd7`/1 | `7 1` | online | counter status |
| `osnvcounter` | `0xd8`/`0x1c` | `0 0xe9` | online | OTP-online non-volatile counter |
| `osnvcounter_st` | `0xf3`/1 | `7 1` | online | counter status |

Note on the `notes` column: it is a plain-language gloss of the DT cell name, not a decoded
semantic ([I]/[U] unless a consumer is cited); the authoritative facts are the cell name, the
`reg` tuple and the `bits` tuple. The addresses in the left column are **byte offsets into the
256-byte fuse space**, not register addresses; the provider resolves them through the data window. The map covers offsets
`0x00..0xf3`, which is why the window is sized `0x100`. [V]

### 2.2 The controller register model, from `hi_efuse.ko`

Module: `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_efuse.ko`, 10,144 B, md5
`6a13b84a7a8177269ece1cb955605090`, `depends: ksecurec`, GPL, `description=hsan SDK efuse
driver`. Symbols: `hi_efuse_probe` (0x894), `hi_efuse_read` (0x7a4), `hi_efuse_write`
(0x56c), `hi_efuse_show`/`store` (0x14/0x17c), `hi_efuse_sysfs_create` (0x40c) and an
unsymbolized register-access helper at 0x4e4 (called only from the `write` path). Imports confirm the frameworks: `devm_nvmem_register`,
`devm_nvmem_cell_get`/`put`, `nvmem_cell_read`/`write`, `syscon_regmap_lookup_by_phandle`,
`regmap_read`/`write`/`bulk_read`, `devm_clk_get`, `clk_set_parent`,
`__devm_reset_control_get`/`assert`/`deassert`/`status`, `devm_gpiod_get_optional`,
`arm_delay_ops`. [V]

**Probe** (`hi_efuse_probe`) [V]: resolves the three syscon phandles by name
(`efusecon-phandle`, `efusedata-phandle`, `efuseclksrc-phandle`), gets the clocks by name
`ring_clk`/`mux`/`osc_clk`, gets the reset control (index 0), gets an optional GPIO and can fall back
to it when the control-syscon phandle is missing, then registers an nvmem device with
**size `0x100`** and `reg_read`/`reg_write` = `hi_efuse_read`/`hi_efuse_write`, and finally
creates one sysfs file per **child cell node** (`of_get_next_child`; name = node name;
`hi_efuse_show`/`store`). There is no `read-only` string in the module: the DT
`read-only` property is **not parsed** [V], so the sysfs write path exists even on the
`ns` instance. That is a hazard to remove in any port.

**Read path** (`hi_efuse_read`) [V]: allocates `((len+3)/4)*4` bytes, then **one**
`regmap_bulk_read(efusedata, offset & ~3, buf, words)` and a `memcpy` back - no command
write, no clock/reset/GPIO action. The read path is therefore a safe, side-effect-free window
read [I: as long as the window content is loaded by hardware/boot; the module assumes it is].

**Write path** (`hi_efuse_write`) [V], in order:

1. `regmap_read(efuseclksrc, 0)`, set bit **2**, write back (`0x14880138` bit 2 = 4).
2. `clk_set_parent(mux_clk, osc_clk)` - a **write to the CRG mux word `0x138` mask 0x2**.
3. helper `(0x7fff, 1)` - an address/command pair.
4. For each 32-bit word of the requested range: helper `(0x8000 | word_index, word_value)`;
   the helper is: `regmap_write(efusecon, 0x8, addr)`, `regmap_write(efusecon, 0x4,
   value)`, `regmap_write(efusecon, 0x0, 0xaa55)`, `regmap_write(efusecon, 0x0,
   0x55aa)`, then a delay (`arm_delay_ops.delay` with a large loop argument).
5. helper `(0, 0x806)` - one more config word.
6. Reset pulse loop: `reset_control_deassert`/`assert` then poll
   `reset_control_status` until it reads 0 (bounded by 50 iterations of a fixed delay).
7. `regmap_write(efusecon, 0xc, 1)`, then a 100-iteration delay. That is the ("burn")
   trigger, after which the function returns without any status verification beyond step 6. [V]

The control word at `0x0` is a two-write strobe (`0xaa55` then `0x55aa`) and the
`0x4`/`0x8`/`0xc` registers are the data/address/start interface. The exact meaning of
`0x806` and `0x7fff`, the bit fields of `0x0`, and what `0xc` reads back are **[U]**;
so is whether programming works word-at-a-time or byte-at-a-time (the vendor passes whole 32-bit
words and the provider sets no `word_size`, so the caller's nvmem write is word-granular) [I].
What is **V** is the sequence and the register addresses.

### 2.3 What is missing and what must not be ported

Missing/[U]:

- semantic of `0x806`, `0x7fff`, the `0x0` strobe bits and the `0xc` start bit;
- whether the data window is valid immediately after reset or only after a first command (the read
  path never issues one, but no cold-boot dump of the window exists);
- per-word vs per-bit programming; the vendor only ever writes whole words and never erases;
- the vendor module's `read-only` DT property is ignored, and every instance shares the same
  three windows; in a port, one provider should own the controller and the *cell* read-only flag
  must be enforced in software;
- **which cells are ever written in the field**: none of the local artifacts shows a vendor write
  to `ns`/`sec_ctrl`/`online`; the sysfs shim exists, but no evidence that it is used
  on this product [U].

Must-not-port [I]:

- **no write support in v1.** Every one-way fence is reachable from the same interface:
  `bootrom_forbid` / `bootcli_forbid` (can brick the boot ROM path),
  `jatg_efuse_forbid` (permanently kills JTAG efuse access), `sec_boot_en`, and the
  `msk/huk/jak` key material. A vendor-style sysfs write path on this block is a bricking
  interface and violates [HAZARDS.md](HAZARDS.md) in spirit.
- **never expose the key cells** (`msk`, `huk`, `jak`, `pubkh`) to unprivileged
  userspace, and consider not describing them as nvmem cells in a public binding at all.
- **never enable the write path in a boot chain** (target or lab kernel); the burn sequence above
  is documented so it can be *avoided*, not used.

### 2.4 Consumers already in the vendor tree (why a read-only port has value)

| consumer | evidence | what it reads |
| --- | --- | --- |
| `hi_cpufreq.ko` | undefined symbols `nvmem_cell_get`/`put`/`read`; strings `nvmem avs read fail, return [%d]`, `nvmem-cells`, `hpm-range`, `chip speed is %s` (`slow`/`typical`/`fast`) | the CPU `nvmem-cell-names = "avs"` cell = `ns:osc_ring_svt_freq` (reg `0x1a`/2, bits 0..12); combined with `hpm-range` to pick the OPP grade. [V] |
| `hsan,tvsensor` DT node | `efuse-trim-en; trim = <0 0>;` (pinned DTS lines 1477-1490) | thermal trim cells (`tsensor_trim_offset`/`vsensor_trim_offset`); the module itself does not import the nvmem API, so it either reads a register window or the DT supplies the trim - **[U]** |
| RGMII/USB2 trims | `rgmii0_tune_efuse`, `usb2_trim_offset` cells exist | no local module imports nvmem for them; the consumers are likely in `hi_kphy`/`hi_kmdio` or U-Boot - **[U]** |

### 2.5 Port framework + binding sketch

Framework: the **upstream nvmem provider subsystem** (`drivers/nvmem/`, `nvmem_config` with
`devm_nvmem_register`, child cell nodes with `reg`/`bits` per the generic
`nvmem-provider` binding). One provider per fuse namespace, or one provider with all cells and
`read_only = true`; the vendor uses four because the product trees split the namespaces
(ns = public trims, chip_key = secret keys, sec_ctrl = security, online = counters). [I]

The control window is in the `0x14900000` page, so the binding should take three syscon
phandles exactly like the vendor DT rather than a single `reg`; the data window is the only
range a read-only port needs to map:

```dts
efuse: efuse {
	compatible = "hisilicon,luofu-efuse";
	#address-cells = <1>;
	#size-cells = <1>;

	/* the 256-byte data/read-back window; the only range a read-only port maps */
	reg = <0x14948000 0x100>;
	hisilicon,ctrl = <&efuse_ctl>;		/* syscon @ 0x14900140, 0x20 */
	hisilicon,clk-src = <&efuse_clk_src>;	/* syscon @ 0x14880138, 1 */
	clocks = <&efuse_ring_clk>, <&efuse_clk>, <&osc_clk>;
	clock-names = "ring_clk", "mux", "osc_clk";
	resets = <&crg 0x34 0x00>;
	read-only;

	efuse_ns: ns@0 { /* cells exactly as the census in 2.1 ... */ };
	/* no chip_key / sec_ctrl children in a public DTS: key material is not described */
};
```

Notes [I]: the clock mux (`efuse_clk`) is a CRG child (section 5); the reset pair
`(0x34, 0x00)` is already accepted by patch `004`'s allowed reset words, so no reset-path
change is needed. A target kernel would take `clocks` by name; if the port starts as read-only,
the clock tree can be reduced to the two fixed clocks plus no mux write (the read path needs
neither, section 2.2), which decouples the efuse port from the PLL/mux work.

### 2.6 Risks, offline-vs-attended, effort

- Risks: one-way fences (above); nvmem ABI (cell offsets are a kernel<->DT contract - freeze
  before any userspace consumer appears); key cells leaking to sysfs (nvmem sysfs is root-only but
  the vendor's own per-cell sysfs files are created mode 0644 by `hi_efuse_sysfs_create`
  (`movw r3,#420` = 0644, then `strh` into the attribute) - i.e. world-readable [V]); a wrong
  read of the window is harmless, a wrong write is permanent.
- Offline: everything except a read-back check. Bindings, provider skeleton, cell validation,
  `dt_binding_check` and compile are all host-only.
- Attended: one read of the data window through the real hardware (compare `basic_dieid` against
  any U-Boot/`bdinfo`-visible values), one read of `avs` to correlate with the OPP grade.
  **No write ever.**
- Effort: **M** for a read-only provider + cells + binding (host-only), plus a small attended read
  check. **XL / do-not-do** for write support (needs a blank-fuse policy, per-fuse blast radius, and
  is a brick risk).

## 3. Block H - RNG / hwrng

### 3.1 Finding: there is no hardware RNG block to port [V]

The vendor DT node is only

```dts
jent-rng {
	compatible = "hsan,jent-rng";
};
```

(pinned DTS lines 1039-1042) - **no `reg`, no interrupt**. The module
`parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kjent_rng.ko` (4,624 B, md5
`4f3080e82eb105247ced023278b7dc05`, `depends: rng-core`) is a **software RNG wrapper**:
its probe (`hi_jent_rng_probe`, offset 0x48) calls `crypto_alloc_rng("jitterentropy_rng",
0, 0)`, fills an `struct hwrng` (name/quality/`read`/`init`/`cleanup`) with
`priv = ctx`, sets **quality = 0x200 (512)** at struct offset `+0x12c`, and calls
`devm_hwrng_register`. Its read callback (`hi_jent_rng_read`) fetches the crypto rng's
generate op (`ldr r0,[r0,#0x3c]` = priv, then a function pointer through the crypto tfm) and
returns the requested number of bytes. Strings: `jitterentropy_rng`, `failed to register
hwrng`, `Install jent_rng done ...`. [V]

Cross-checks:

- The 2.4.15 and 2.5.24 module trees are byte-identical for this module (appendix A). [V]
- The vendor rootfs also runs the OpenWrt userspace entropy daemon (`/etc/init.d/urngd`,
  `/sbin/urngd` in `parent:rootfs-2.4.15`) and loads `rng-core.ko` before the wrapper
  (`parent:rootfs-2.4.15/etc/rc.d/luofu` lines 12-14). [V]
- The extracted vendor kernel image
  `parent:dumps/router-20261108/full/mtd11-kernela.bin` carries no built-in RNG/TRNG driver
  string; the only hwrng path in the module set is the wrapper above. [V] (Extraction procedure: the
  uImage payload at `0x40` is a zImage whose XZ stream decompresses to a 12,207,156-byte ARM
  vmlinux; `Linux version 5.10.201 ... #0 SMP Sat Nov 25 18:18:57 2023`.)
- No MMIO window, no interrupt, no clock/reset in the DT node; there is no `hsan,trng` (or
  similar) compatible anywhere in the local vendor trees. [V]

### 3.2 Port decision

**Do not port this block.** [I] Concretely:

- there is no hardware register interface to own, so there is nothing for a Luofu driver to model;
- the functionality is already in mainline as `jitterentropy_rng` (the vendor module is a
  hwrng-shaped facade over exactly that crypto rng) and in OpenWrt as `urngd`;
- a DT node with no `reg` and no resources would be rejected by a binding review, and the
  vendor node cannot even be represented as a platform device without inventing resources.

What would change the answer [U]: a GPL-source drop, a BSP diff or a U-Boot/SoC datasheet showing a
real TRNG block (e.g. a register window with a `RNG_CTRL/DATA` pair). None exists locally.
The 2.4.15 vendor module census (58 `.ko`) has no other RNG consumer.

### 3.3 Effort

**Zero** (drop). If a later document needs an entropy statement for the device page, it can say
"no open hardware RNG is known; the stock system mixes `jitterentropy_rng` via a vendor hwrng
wrapper plus `urngd`" - that is all the local evidence supports.

## 4. Block I - LED PWM and PMU PWM

These are two unrelated blocks that the parent task grouped together; treat them separately.

### 4.1 LED PWM (`hsan,ledpwm`): exists in the DT, has no driver, no consumer

What is known [V]:

| property | value |
| --- | --- |
| node | `ledpwm@14900280`, pinned DTS lines 1054-1066; `#ledpwm-cells = <1>`, `nledpwm = <2>`, `label = "peri0"` |
| registers | `reg-names = "oen","loop","cfg","steptable"` = `0x14900110`/4, `0x1490011c`/4, `0x14900280`/8, `0x14944000`/`0x200` |
| clock | `clocks = <&led_pwm>` = CRG gate `0x14` bit `0x0e` (LUOFU_CLK_LEDPWM, patch `004` gate index 7) |
| reset | `resets = <&reset0 0x2c 0x0e>` |
| pin functions | `led0_1` / `led1_1` pinctrl states in the base DTB (`led0_1_default_state`, `led1_1_default_state`); the board overlay only assigns those pins, no consumer |

Why it is dead on this board [V]: (a) no driver exists - a census over all 58 `.ko` files in both
rootfs trees finds **no** module whose strings contain `ledpwm`, `steptable` or
`hsan,ledpwm`; (b) the extracted vendor vmlinux contains **zero** `ledpwm` strings, so it
is not built in either; (c) no node in the base DTB or in the board-3 overlay consumes it (no
`pwms`, no `leds` reference), while the product LEDs are the overlay's `gpio-leds` block;
(d) its `oen`/`loop` registers (`0x14900110`, `0x1490011c`) lie **inside** the
pinctrl mux window `0x14900100..0x1490013b` that patch
[`011-pinctrl-luofu.patch`](../upstream/series-6.18/series/0011-pinctrl-luofu.patch) describes
as `reg = <0x14900100 0x3c>` - the vendor's own DT has overlapping reg windows for these two
nodes, which works only because no LED PWM driver claims them.

Port plan: **out of scope for v1** [I]. If another luofu board ever uses this block, the framework
is a **PWM chip** (`pwmchip`, 2 channels, the vendor's custom `#ledpwm-cells` replaced by the
standard `#pwm-cells = <3>`), consumed by `leds-pwm`; the page-alias problem then needs the
same treatment as the CRG page (syscon/simple-mfd parent, or shrink the pinctrl window). Bench
work would be needed to learn the `cfg`/`steptable` semantics (the step table is a 512-byte
window at `0x14944000` - presumably per-step duty/gradient values, **[U]**). Effort: **L**
(dead block, no consumer to validate against) - recommend **no**.

### 4.2 PMU PWM (`hsan,pmu-pwm`): the CPU `vdd_logic` regulator

What is known [V]:

- DT (`pwm@1`, pinned DTS lines 1361-1369): single register `reg = <0x14900800 0x4>`,
  `npwm = <1>`, `#pwm-cells = <2>`, `clocks = <&osc>` (20 MHz). A **consumer is in the
  same DTB**: `reg@0 { compatible = "pwm-regulator"; pwms = <&pmupwm 0 10000>;`
  `regulator-min-microvolt = <740000>; regulator-max-microvolt = <1040000>;`
  `pwm-dutycycle-unit = <100>; pwm-dutycycle-range = <100 0>; regulator-name = "vdd_logic"; }`
  (lines 1370-1381). The board overlay adds the `pinctrl-0 = <&pmupwm_default_state>` state
  and the base DTB names the pin function **`avs` / `avs_grp`** - i.e. this is the SoC
  adaptive-voltage output, not a user PWM.
- Module `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kpmupwm.ko` (7,568 B, md5
  `f739c6dead863e1f4fa8c6a37f1d38d4`, symbols `hi_pwm_probe` 0x80, `hi_pwm_apply` 0x300,
  `hi_pwm_request`/`free`/`remove`) imports `pwmchip_add`/`remove`,
  `pinctrl_lookup_state("enable")`+`pinctrl_select_state`, `devm_clk_get`+`clk_prepare`+
  `clk_enable`/`clk_get_rate`, and `devm_ioremap_resource` on the single register. [V]
- Probe parses two properties: `npwm` (**required**; string `npwm property read fail,ret[%d]`)
  and `hwver` (**optional**, default 16; string `hwver`). This board's DTB carries
  `npwm = <1>` and **no `hwver`**, so the driver runs with hwver = 16. [V]
- Register model from `hi_pwm_apply` [V]: one 32-bit word per channel (index = `pwm->hwpwm`,
  only index 0 exists here). Polarity is written to **bit 24**. The period is converted to
  clock-cycle counts from the DT period (ns) and the clock rate, and the driver **rejects a period
  longer than 255 cycles** (`cmp r4,#255; bhi` -> `failed to config pwm: %d`), i.e. with the
  20 MHz oscillator the maximum period is ~12.75 us. Two further 8-bit fields are written from the
  duty/period arithmetic (bits `8..15` and `16..23`), and bits `25`/`26` are
  toggled under an `hwver == 16` / `hwver == 32` condition on enable/disable. The exact
  field arithmetic is [I]; the bit positions and the 255-cycle limit are [V]. The DT consumer
  period `10000` ns at 20 MHz is 200 cycles, which fits the limit - a useful consistency check.
- The vendor boot loads it in the cpufreq group:
  `parent:rootfs-2.4.15/etc/rc.d/luofu` lines 14-22: `hi_efuse` -> `hi_kpmupwm` ->
  `pwm-regulator` -> `hi_clk_corrector` -> `hi_cpufreq` -> userspace governor ->
  `scaling_setspeed = 1200000`. So in the stock system `vdd_logic` is actively driven by
  cpufreq/OPP, not left at a fixed duty. [V]

Missing/[U]: the three-field duty encoding (only observable by a readback); the real `hwver`
value on this silicon (default 16 is a driver fallback, not a DT fact); the `avs` pin numbers
(the pinctrl driver's group data holds them; patch `011` would need the `avs` function added
before a PMU PWM node could select it); the mapping from duty to actual voltage (only the DT
min/max range is evidence - no ADC telemetry exists in the local artifacts); whether the CPU OPP
table's lower voltages are safe without the regulator's active control.

Port framework + binding sketch [I]:

- Framework: **PWM subsystem** (a `drivers/pwm/` driver with `pwmchip`, 1 channel,
  `.apply` from `pwm_state`) + the **standard `pwm-regulator` consumer** (the tree's own
  `Documentation/devicetree/bindings/regulator/pwm-regulator.txt` supports exactly the two
  properties the vendor uses, `pwm-dutycycle-unit` and `pwm-dutycycle-range` - [V] in
  `parent:build/linux-kbuild/linux-5.10.201`). The binding name should be a normal PWM provider
  (`hisilicon,luofu-pmu-pwm`), not a vendor-flavoured "pmu-pwm" with custom properties;
  `hwver` becomes driver data keyed off the compatible, not a DT property.

```dts
pmupwm: pwm@14900800 {
	compatible = "hisilicon,luofu-pmu-pwm";
	reg = <0x14900800 0x4>;
	#pwm-cells = <2>;
	clocks = <&osc_clk>;
	pinctrl-names = "default";
	pinctrl-0 = <&pmupwm_pins>;	/* AVS function, must be added to the pinctrl driver */
	status = "disabled";		/* enable only after bench validation */
};

vdd_logic: regulator-pwm {
	compatible = "pwm-regulator";
	pwms = <&pmupwm 0 10000>;
	regulator-name = "vdd_logic";
	regulator-min-microvolt = <740000>;
	regulator-max-microvolt = <1040000>;
	pwm-dutycycle-unit = <100>;
	pwm-dutycycle-range = <100 0>;	/* duty 100 = min voltage on this board */
};
```

- Dependencies: pinctrl block A (`avs` function/group), `osc_clk` (already in the target
  DTS), and - only if OPP/cpufreq is also ported - the CRG PLL/mux model (section 5) and the efuse
  `avs` cell (section 2.4). Without cpufreq, a fixed regulator at one measured-safe duty
  would run the CPU at whatever voltage the bootloader left; that is the conservative v1 choice.
- Risks: **writing the wrong duty can over- or under-volt the CPU**. The DT's `100 0` range
  says duty 100 maps to the minimum voltage and duty 0 to the maximum, which is the opposite of
  the naive assumption - do not enable the regulator node without a readback plan. There is no
  voltage telemetry in any local artifact. The 8-bit period limit also means the PWM frequency
  can not be lowered below ~78 kHz.
- Offline: driver skeleton + binding + `dt_binding_check` + a mocked regulator test. Attended:
  one probe with the PWM output **disabled**, read the register back (confirms the field layout),
  then a single safe duty step with the CPU idle and a watchdog/recovery plan - this is a
  power-rail experiment, not a networking one.
- Effort: **M** (driver ~150 lines, binding, pinctrl `avs` hook, one attended session).

## 5. Block J - CRG: per-gate parents, PLLs and muxes

### 5.1 What patch `004` already does, and what it deliberately does not

[`0004-arm-clk-reset-luofu-crg.patch`](../upstream/series-6.18/series/0004-arm-clk-reset-luofu-crg.patch)
adds `drivers/clk/hisilicon/crg-luofu.c` + the dt-bindings header
`include/dt-bindings/clock/hisilicon,luofu-crg.h` with 16 gate IDs, and the YAML binding
`Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml`. It models:

- the two gate words `0x14` and `0x20`, one bit per gate (verified below against the vendor DT);
- the reset provider with the two-cell "offset,bit" specifier, range-checked to `0x2c`/`0x30`/`0x34`;
- **all gates parented on `apb_clk`** - quote from the patch header: *"Deliberately not implemented:
  the per-gate parent/PLL/mux model (rates are not modelled yet; every gate is currently parented on
  the APB clock), named reset IDs, and the reboot/soft-reset/mux/PLL words."* [V]

That is the gap this section specifies. The lab driver
[`../lab/luofu-clk/luofu-clk.c`](../lab/luofu-clk/luofu-clk.c) and its binding note
[`../lab/luofu-clk/bindings/hisilicon,luofu-crg.md`](../lab/luofu-clk/bindings/hisilicon,luofu-crg.md)
already transcribe the per-gate parent table (PLL/mux read-only, `set_parent` refused) - it is
the reference transcription for the in-tree driver. [V]

### 5.2 What the vendor DT gives (the model to reproduce)

**Fixed input clocks** [V] (pinned DTS lines 173-192, 427-433, 756-780, 955-961, 1090-1097,
1108-1115; all
`fixed-clock`/`fixed-factor-clock`, no Luofu driver needed):

| clock | rate | DT node | used as parent by |
| --- | --- | --- | --- |
| `osc` | 20 MHz | `fixed_clk0` | both PLLs; PMU PWM; efuse mux input |
| `apb_clk` | 100 MHz | `apb_clk` | the `0x14`-group gates (sfc, gpio0/1, i2c0, led_pwm) and the PCIe gates |
| `ahb_clk` | 200 MHz | `ahb_clk` | the PIE gate |
| `gemacN_clk` | 125 MHz | `gemacN_clk` | the five GEMAC gates and the MDIO gate |
| `lsw_dp_clk` | 166 MHz | `lsw_dp_clk` | the LSW-DP gate |
| `lsw_pfe_clk` | 333 MHz | `lsw_pfe_clk` | the LSW-PFE gate |
| `efuse_ring_clk` | 20 MHz | `fixed_clk1` | the efuse mux input |
| `corrector_clk` | 1 GHz in the pinned DTS, but **measured 1.2 GHz** (2026-11-18, 5.6) | `corrector_fixed_clk` | TWD: `fixed_factor_clk` /4 = **measured 300 MHz** (`twd_clk`); the live rate is not the static DT value and the source of the difference is [U] |

**Gate parent table** [V] (pinned DTS lines 206-365; transcribed in the lab binding note section
"Clock gate table"): the 16 gates, their `reg-offset` (`0x14` or `0x20`), bit, and parent:
`sfc_clk`/`gpio0_clk`/`gpio1_clk`/`i2c0_clk`/`led_pwm` -> `apb_clk`;
`pcie0_clk`/`pcie1_clk` -> `apb_clk`;
`gemac_clk0..4` and `mdio_clk0` -> `gemacN_clk`;
`lsw_dp_clk0` -> `lsw_dp_clk`; `lsw_pfe_clk0` -> `lsw_pfe_clk`;
`pie_clk0` -> `ahb_clk`.

Note on the "provisional" caveat: patch `004` and the lab note flag the `0x20`-group
`reg-offset` values as reconstructed from the vendor node unit-address encoding. The
authoritative raw DTB decode resolves this: each `clk_gate@0020NN` node carries an explicit
`reg-offset = <0x20>` property, and `bit` is the documented value (e.g. `gemac_clk0`:
`reg-offset = 0x20`, `bit = 0x13`; `pie_clk0`: `0x20`/`0x19`) [V]. The
PROVISIONAL wording in patch `004` can therefore be retired - see section 13.

**PLLs and muxes** [V] (pinned DTS lines 366-411):

| node | properties | meaning |
| --- | --- | --- |
| `clk_pll1@0198` | `ctrl-offset = 0x198`, `status-offset = 0x90`, `status-bit = 0x1e` (30), parent `osc`, output `cpu-clk` | CPU PLL |
| `clk_pll2@01e0` | `ctrl-offset = 0x1e0`, `status-offset = 0x90`, `status-bit = 0x1b` (27), parent `osc`, output `lsw-clk` | second PLL |
| `cpu_mux@01388` | `reg-offset = 0x138`, `mask = 0x08`, `table = <0 1>`, parents `cpu_pll`, `lsw_pll` | CPU clock source select |
| `efuse_mux@0138` | `reg-offset = 0x138`, `mask = 0x02`, `table = <0 1>`, parents `efuse_ring_clk`, `osc` | efuse ring clock source select |

The CPU OPP table uses 800/1000/1200 MHz (`opp-800000000`/`opp-1000000000`/`opp-1200000000`,
pinned DTS 1174-1201) with `cpu-supply = <&cpu_supply>` (the PMU-PWM regulator) and
`nvmem-cells = <&osc_ring_svt_freq>` named `avs`. [V] All three PLL rates are integer
multiples of the 20 MHz reference (40/50/60), which matches the driver's integer-multiplier
rounding path (below) [I].

### 5.3 What the vendor driver does (the register model we can borrow)

Modules: `hi_clk.ko` (15,556 B, md5 `3b331b9058e13bc409dd8f0040af3b11`) and `hi_crg.ko`
(6,896 B, md5 `b716d4a429d3362ffb91e535a4fe2e28`), both GPL, both parented from
`hsan,clk` / `hsan,crg` nodes. `hi_crg` is only a register-access library (up to 15
register banks, `hi_crg_read_reg`/`write_reg`, `hi_crg_enable`/`disable`/`get_status`
over `(bank, reg, bit)`, offset/size-checked) [V]. `hi_clk` is the actual clock
provider. Its property strings are exactly the vendor DT properties [V]:

`hsan,clk-gate` -> `clock-output-names`, `reg-offset`, `bit`, plus one optional
0/1-valued cell; `hsan,clk-mux` -> `clock-output-names`, `reg-offset`, `mask`,
`#clock-cells`, `clocks`, `table`; `hsan,clk-pll` -> `clock-output-names`,
`ctrl-offset`, `status-offset`, `status-bit`, `fnpllcfg0-offset`,
`fnpllcfg0-value`, `div-factor`, `mode`, `freq-table` (+ `hwver` on the
top node). Only `ctrl-offset`/`status-*` are present on this board's PLL nodes, so the
generic (non-table) path is the one that runs here. [V]

- **Gate enable/disable/is_enabled** [V]: a one-bit regmap RMW at (CRG base + `reg-offset`),
  `enable` sets the bit, `is_enabled` reads and tests it. `disable` clears it, but the
  disassembly has a per-index polarity quirk (`mvnne` vs `moveq` on a table-indexed value),
  so the disable polarity is [U] for the `0x20`-group until a readback experiment. The
  enable polarity (1 = on) matches patch `004`. [V]/[I]
- **Mux get/set_parent** [V]: `get_parent` reads `0x138`, masks, decodes through `table`;
  `set_parent` does a read-modify-write of `0x138` with `mask` and a shift derived from
  the mask. This is the important divergence from the lab safety stance: the lab binding says
  `0x138` is "out of bounds for writes", but the vendor **does write it** - `hi_efuse` calls
  `clk_set_parent(mux, osc)` at the start of an efuse **write** (section 2.2). [V] So a port
  needs the efuse mux write for efuse programming, and must keep the CPU mux read-only (writing
  bit 3 there switches the CPU clock source and can hang the core).
- **PLL rate/round/set** [V]/[I]: the PLL is programmed through two consecutive words at
  `ctrl-offset` (`0x198`/`0x1e0`) and acknowledged through `0x90` bit 30/27. The
  read (recalc) path decodes a fractional PLL: a 12-bit integer field at word0 bits `23:12`, a
  24-bit fraction at word1 bits `23:0`, a 6-bit feedback divider in word0 byte3, and
  divider-select bits in word0 byte0 (bit 3 set selects the integer-only path, cleared selects
  the fractional path; bits 4..6 and 8..10 are divider-select fields and the `div-factor`
  driver property adds to both). The arithmetic was decoded from the module disassembly
  (`hi_pll_recalc_rate`): `rate = parent * (int + frac/2^24) / fbdiv`, then each non-zero
  divider-select value is applied as an integer division, with the absent `div-factor`
  property contributing 0. The writer (offset `0x4c4`) sets busy bit 7 of the control byte, writes the assembled word(s),
  clears bit 7 and polls the status bit; the `round_rate` non-table path treats the PLL as an
  integer multiplier of the reference clamped to `16..3200` [V]. `mode == 1` selects a
  7-word (28-byte) frequency-table entry path; no `freq-table` exists here. [V] **Measured
  2026-11-18:** the equation is now validated against the live CPU rate; the raw words and the
  decode are in 5.6. PLL1 (CPU) reproduces 1200 MHz exactly. PLL2 decodes to a 2000 MHz raw rate
  whose division down to the LSW 333/166/100 MHz domains is not explained by the decoded
  divider-select fields yet - that residual stays [U] and `set_rate` stays unimplemented.

### 5.4 The gap list (what a follow-up patch must do)

1. **Add the missing fixed clocks to the target DTS** [V gap]: the target DTS
   ([../target-skeleton/dts/luofu-r116.dts](../target-skeleton/dts/luofu-r116.dts) lines 202-238)
   carries only `osc`, `ahb`, `apb`, `corrector`, `twd`. Missing:
   `gemacN_clk` (125 MHz), `lsw_dp_clk` (166 MHz), `lsw_pfe_clk` (333 MHz),
   `efuse_ring_clk` (20 MHz). Without them the parent model cannot be expressed at all. The
   rates are [V] from the vendor DT.
2. **Extend the gate table with a parent field** [I]: today `luofu_crg_gates[]` has
   `{name, offset, bit}` and a single `luofu_crg_gate_parent = "apb_clk"`. The driver
   should grow a `parent` string per gate from the table in 5.2 (or a parent clock index into a
   DT-provided list) and drop the single-parent constant. This is driver data, consistent with
   the shared-page contract ("the in-tree driver's table is authoritative", REMAINING-DRIVERS
   section 0.3).
3. **Add the PLL and mux clocks to the CRG driver** [I]: as additional clock indices in
   `include/dt-bindings/clock/hisilicon,luofu-crg.h` (new IDs, not renumbering the existing
   16), with `recalc_rate` implemented from the read-only field decode and `set_rate`
   **not implemented** until a bench readback confirms the equation. The efuse mux is a special
   case: it must accept `set_parent` only if the efuse write path is ever enabled; the CPU
   mux must never accept `set_parent`.
4. **Keep probe free of writes** [I]: patch `004`'s probe already does no register
   access; the PLL/mux additions must keep that property (read the words only when a consumer
   asks; mark `CLK_IGNORE_UNUSED` so `clk_disable_unused()` cannot gate a bootloader-enabled
   clock, as the lab driver does).
5. **Retire the "provisional" caveat** on the `0x20`-group gates (section 5.2) - that is a
   comment-only change, listed in section 13.

**Status (2026-11-18):** items 1-4 are delivered by patch `019` (fixed clocks plus
DT-provided parents, read-only PLL/mux clock outputs, probe-free-of-writes and
`CLK_IGNORE_UNUSED`); item 5 is applied there as well (the provisional wording is retired in
the driver comment). **The attended PLL rate/control-word readback was taken on the stock boot on
2026-11-18** (5.6): the equation is validated by PLL1, the on-board CPU rate is 1.2 GHz and the
LOCK bits are set. Any PLL/mux **write path** remains open and unimplemented; the PLL2
divider-select residual (5.6) must be settled before a `set_rate`. See the patch header and
`patches-6.18/019-arm-clk-luofu-clock-tree.patch`.

### 5.5 Dependencies, risks, offline-vs-attended, effort

- Dependencies: block G (efuse) needs the `efuse_mux` write for programming only; blocks
  D/E (LSW/GEMAC) need `gemac_clk*`/`lsw_*`/`pie_clk0` parents for their clocks; CPU
  OPP/cpufreq (if ported) needs the CPU PLL/mux model and the PMU PWM regulator (section 4.2).
- Risks: writing `0x138` bit 3 (CPU mux) switches the CPU clock and can hang the core - keep it
  read-only; writing the PLL words with a wrong equation can lock/hang the CPU - implement
  read-only first and validate by readback; the busy/status protocol may need the exact delay from
  the vendor writer; the disable-polarity quirk (5.3) could make a "disabled" clock still run
  (harmless for gates with no consumers, but a port must verify `is_enabled` against the vendor
  value in a boot log or a bench read).
- Offline: everything except the rate readback - fixed clocks, per-gate parents, PLL/mux read-only
  clocks, YAML/`dt_binding_check`, compile. Attended: **DONE 2026-11-18** - one read-only stock
  boot printed `clk_summary` and both PLL control-word pairs, confirming the parent selection
  and the PLL1 equation (5.6). No write experiment was performed; a write path (and the PLL2
  divider question) remains attended work.
- Effort: **M** for fixed clocks + per-gate parents (host-only, low risk); **L** for the PLL/mux
  rate model plus an attended readback (the equation is the risky part).

### 5.6 Measured CRG/PLL/clock state on the stock vendor boot (2026-11-18) [V]

Read-only inventory of the running vendor system (one boot; each word identical in two passes;
evidence `parent:build/bench-inventory/20261118T130512Z-inv1-run/` plus its `followup/clk_summary.txt`;
analysis [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 4). No write was made.

| offset | value | reading |
| --- | --- | --- |
| `0x14` | `0x01360ef1` | gate word 0x14: bit0 (sfc_clk) set, bit14 (led_pwm) clear - matches the live consumers; other set bits are outside the cross-checkable gate nodes, [U] |
| `0x20` | `0x02f8300b` | gate word 0x20: bits 0/1/3/12/13/19-23/25 = lsw_dp, lsw_pfe, mdio, pcie0/1, gemac0-4, pie gates all left enabled |
| `0x2c` | `0x0d300e01` | reset word, raw |
| `0x30` | `0xfe7fe05b` | reset word: bits 0/1 (dp_logic/dp_srst), 3/4 (pfe_logic/pfe_srst), 25 (PIE reset 0x19) set = blocks out of reset |
| `0x34` | `0x000ff309` | reset word: bits 16-19 (PCIe RC resets 0x10-0x13) set = both RCs out of reset |
| `0x38` | `0x00200000` | resume word: CPU resume bits 0x16/0x18/0x17/0x19 all clear on the running 2-CPU boot [I consistent with release]; bit21 [U] |
| `0x90` | `0x6a010008` | PLL status: bits 30 (CPU PLL) and 27 (LSW PLL) set = both locked (mask check pass) |
| `0x100` | `0x00000000` | watchdog ISTATUS, healthy |
| `0x138` | `0x00018042` | mux word: bit1 = efuse mux source (osc), bit3 = CPU mux source (CPU PLL, 0 -> cpu_pll) - matches CPU 1.2 GHz / efuse 20 MHz; bits 6/15/16 [U] |
| `0x198` / `0x19c` | `0x8103c11c` / `0x11000000` | PLL1 control words |
| `0x1e0` / `0x1e4` | `0x81064548` / `0x11000000` | PLL2 control words |

**Live clk tree (debugfs `clk_summary`) measured [V]:** osc 20 MHz; corrector_clk **1.2 GHz**
(enable 1) with fixed_factor_clk **300 MHz**; efuse_ring_clk/efuse-clk-mux 20 MHz (0 users);
lsw-clk 100 MHz (0); cpu-clk and cpu-clk-mux 1.2 GHz (0); lsw_pfe_clk/lsw_pfe_clk0 333 MHz
(1/1); lsw_dp_clk/lsw_dp_clk0 166 MHz (1/1); gemacN_clk 125 MHz (5 users: mdio_clk0,
gemac_clk1-4; gemac_clk0 gate is enabled in the bitmap but has no consumer); ahb_clk/pie_clk0
200 MHz (1/1); apb_clk 100 MHz (9 users). The fixed inputs and per-gate parents of 5.2 are
confirmed for every gate with a consumer. The 1.2 GHz/300 MHz corrector values differ from the
pinned DTS's 1 GHz fixed clock - the vendor corrector path is not the static DT value here, and
the reason stays [U]; do not change our DTS fixed-clock from this single sample.

**PLL decode against the measured CPU rate [V]/[U]:** with the equation of 5.3,

- **PLL1:** int=60, frac=0, fbdiv=1, byte0=0x1c (integer-only bit set; divider-select 1/1) ->
  20 MHz * 60 / 1 = **1200 MHz**, exactly the measured `cpu-clk` 1.2 GHz. The recalc equation is
  **validated by measurement** [V].
- **PLL2:** int=100, frac=0, fbdiv=1, byte0=0x48 (integer-only set; divider-select fields 4 and
  5) -> raw **2000 MHz**, while the LSW domains run at 333/166/100 MHz. Recorded arithmetic: a
  single /4 gives 500 MHz (not observed); 4*5=20 gives exactly 100 MHz (matches `lsw-clk`);
  2000/6 and 2000/12 give the 333/166 MHz PFE/DP rates, but 6 and 12 are not the decoded
  divider-select fields. **How PLL2 reaches the LSW rates is therefore still [U]**; it may
  involve an undecoded intermediate/post divider or the downstream LSW clock cells. `set_rate`
  must not be implemented from this sample; a future run should also read the CRG divider/mux
  cells downstream of the PLLs.

---

## 6. Block K - SYSCTRL / SYSENV: the boot selector and the other cells

### 6.1 What already exists

Patch [`0010-arm-luofu-restart.patch`](../upstream/series-6.18/series/0010-arm-luofu-restart.patch)
adds the `hisilicon,luofu-sysctrl` syscon node for `0x10100000` (binding
`Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml`) plus the
`hisilicon,luofu-reboot` driver, which reads the selector, writes the same value back
(a write-same), then issues a **provisional** CRG reset command. Patch
[`0017-soc-luofu-boot-selector.patch`](../upstream/series-6.18/series/0017-soc-luofu-boot-selector.patch)
adds the separate `hisilicon,luofu-selector` sysfs ABI (`current_pair`/`next_pair`) on the
cell at `0x10100000 + 0xc08`, and the whole selector story (env copies, loader sequence,
measured values, the `0x21` bench value) is reconstructed in
[SELECTOR-0x21.md](SELECTOR-0x21.md) with the vendor module disassembly in its section 5.2. [V]

This document adds only the parts that are *not* in those two patches: the **other SYSCTRL/
SYSENV cells**, and whether an **nvmem** exposure is the right way to model any of them.

### 6.2 The vendor drivers, as decoded (all [V] from host-side disassembly)

`parent:rootfs-2.4.15/lib/hisilicon/ko/hi_sysctrl.ko` (4,880 B, md5
`8356e81d6988ce054870c5af745d9210`): a register-access library, not a provider. Probe maps up
to four `reg` resources with `devm_ioremap_resource` and exports `hi_sysctrl_read_reg`,
`hi_sysctrl_write_reg`, `hi_peri_sysctrl_read`/`write` (offset/size checked against the
mapped resource). On this board the DT has one resource `<0x10100000 0x1000>`. It creates no
sysfs, no nvmem and no clock/reset.

`parent:rootfs-2.4.15/lib/hisilicon/ko/hi_ksysenv.ko` (8,748 B, md5
`e325b525b9f57978ea9801d7af6ea75b`, identical in both rootfs trees): the sysenv shim.
Probe (`hi_sysenv_probe` 0x3ec):

1. `syscon_regmap_lookup_by_phandle(dev->of_node, "syscon")` - the syscon phandle is the
   SYSCTRL node [V] (the pinned DTS renders the phandle as a quoted "6", the dt2dts quirk for
   `<0x36>`; the raw-DTB decode gives `syscon = <&sysctrl>`).
2. `of_find_compatible_node(NULL, NULL, "hsan,sysenv")` then reads two u32 properties from
   it: `bootreg-offset` (required; this board `0xc08`) into ctx+8, and
   `board_id-offset` (optional) into ctx+0xc. **This board's DT does not carry
   `board_id-offset`**, so the second attribute group is never created [V].
3. Creates the sysfs attributes: the first group is created unconditionally and the second
   only when `board_id-offset` exists; the attribute set is `boot_reg` (rw), `boot_done`
   (wo), `rf_slave_type` (ro) and `chip_type` (ro), and `chip_type`/`rf_slave_type` read the
   board-id word, so their group is the conditional one (grouping inference [I]; the
   attribute existence and names are [V]).

Behaviour [V]:

- `boot_reg_show` (0x178): `regmap_read(bootreg-offset)`, print the low byte.
- `boot_reg_store` (0x230): `simple_strtol`, then a low-byte read-modify-write
  `(val & ~0xff) | (input & 0xff)`; accepts any byte (this is the write SELECTOR-0x21
  documents as the source of the bench `0x21`).
- `boot_done_store` (0x320): reads the cell; **if the low byte is `0x10` it writes `0x11`,
  else it writes `0x10`** - records the *running* pair when the loader's marker is the
  opposite pair.
- `chip_type_show` (0x0) and `rf_slave_type_show` (0xbc): read the **board-id word**
  (`board_id-offset`) and extract `(word >> 14) & 3` and `(word >> 3) & 3` respectively
  (`ldrb r1,[sp,#1]; lsr #6` and `ubfx r1,r1,#3,#2` - note the two bit groups live in
  different bytes). [V]

The vendor kernel **also** uses SYSCTRL from built-in machine code: the extracted 2.4.15 vmlinux
(section 3.1) contains strings `hsan,sysctrl`, `hsan,rstinfo`, `record_addr`,
`hisilicon,hsan_smp`, `hsan,crg`, `core-info`, `softrst_val0`, `softrst_val1`,
`resume-offset`, `reboot-offset`, `reboot-base-index` and `System Reboot(%s)` in
one block (file offsets `0x903e08`-`0x9041c4`) - i.e. the reboot path reads the CRG and
SYSCTRL/rstinfo nodes, which is what patch `010` is reconstructing. [V]

### 6.3 The SYSCTRL page cell census (what is known, what is not)

| cell | address | evidence | state |
| --- | --- | --- | --- |
| SMP release word | `0x10100c00` | DT `smp-offset = <0xc00>` on `hsan,sysctrl` (pinned DTS line 1430) | **Measured 2026-11-18 [V]: `0x80609680`** on the stock 2-CPU boot, identical to the SMP-RE decoded `__pa_symbol(secondary_startup)` - the release target is confirmed. Physical consumer/semantics still [U] and the write belongs to the SMP lane ([SMP-RE.md](SMP-RE.md) 3.4) |
| boot selector | `0x10100c08` | DT `bootreg-offset` on `hsan,sysenv` (line 1418); SELECTOR-0x21 | [V] values 0x00/0x10/0x11 (+ bench 0x21); patch `017` owns it. **Measured 2026-11-18: the full cell is `0x00000010`** - low byte 0x10 (pair A) and the upper 24 bits are zero, closing the upper-bits [U] for this boot |
| board-id word | unknown offset (property absent) | `board_id-offset` read by `hi_ksysenv` but **not present in this board's base DTB** | **[U]** offset and contents; the two bit fields the driver decodes are [V] but unreachable on this unit |
| reset record | `0x10100c10` | `rstinfo` node `record_addr = <0x10100c10>` (pinned DTS line 1404) | **Measured 2026-11-18 [V]: `0x82000000`** (first live baseline); layout/writer still **[U]** - the vendor machine code reads it; do not model |
| lab bench crumbs | `0x10100c18` (inert) and the `0x10100c94` word the fire recipe writes | lab machine source `parent:tools/lab-src/arch/arm/mach-luofu/luofu.c` line 30; FLASH-PLAN slot recipe; SELECTOR-0x21 section 5.4 | **bench-only, never model in a target**; `0xc94` meaning is [U] |

The three nodes' DT properties are also a warning list: `hsan,crg` carries `core-info =
<0x16 0x18 0x17 0x19>`, `softrst_val0/1`, `reboot-offset`, `resume-offset` and
`reboot-base-index` (pinned DTS 471-482) - these are the **soft-reset magic words** that the
vendor machine restart writes, and patch `010` calls its own implementation provisional for a
reason. None of them is a nvmem cell; do not add them to this block's nvmem proposal.

### 6.4 Is nvmem the right framework here?

**Short answer: no for the selector; optionally yes for read-only diagnostic cells.** [I]

- The boot selector is a **write-once-per-boot record with side effects** (contradicting the cell
  changes the next boot's A/B decision). The vendor exposes it through a sysfs shim, and this
  project already has a reviewed sysfs ABI for it (`current_pair`/`next_pair`, patch
  `017`). Adding a *writable* nvmem cell over the same word would duplicate the ABI and multiply
  the ways userspace can brick the pair bookkeeping; it would also have to reproduce the
  low-byte-only read-modify-write and the whole-cell read-back, which nvmem's byte-granular cell
  write does not give for free (the provider would have to do it). Keep the selector in
  `drivers/soc/hisilicon/luofu-selector.c`.
- A **read-only** nvmem cell for the SYSCTRL page is defensible for cells with no other consumer:
  e.g. `smp-offset` (for the SMP lane to read the release word) and the reset record (for a
  future `rstinfo` driver). But no consumer exists today, and "do not invent bindings for
  unused blocks" is the current project rule (TARGET-DESIGN section 7.1, last row). So: **defer**
  until a consumer appears.
- The vendor's own drivers never use nvmem for SYSCTRL/SYSENV [V] - the nvmem idea is ours.

If a consumer does appear, the binding shape would be a child of the SYSCTRL syscon (which patch
`010` would have to become `syscon` + `simple-mfd` like the CRG node), e.g.:

```dts
sysctrl: system-controller@10100000 {
	compatible = "hisilicon,luofu-sysctrl", "syscon", "simple-mfd";
	reg = <0x10100000 0x1000>;

	reboot { compatible = "hisilicon,luofu-reboot"; hisilicon,sysctrl = <&sysctrl>; };

	/* only if a consumer appears: read-only, one cell, exactly 4 or 1 bytes */
	sysenv_nvmem: sysenv-nvmem {
		compatible = "hisilicon,luofu-sysenv-nvmem";
		hisilicon,sysctrl = <&sysctrl>;
		#address-cells = <1>;
		#size-cells = <1>;
		smp_release: release@c00 { reg = <0xc00 0x4>; };
		/* selector @0xc08 is deliberately NOT a cell here: patch 017 owns it */
	};
};
```

- Dependencies: patch `010` (sysctrl node shape) and patch `017` (selector ABI) must stay the
  owners; the reboot driver already resolves the syscon by phandle, so changing the node to
  `syscon` + `simple-mfd` is ABI-neutral for it [I]. The SMP lane may want the `0xc00`
  cell; coordinate rather than duplicate.
- Risks: a writable boot-cell nvmem is a pair-bricking interface; the upper 24 bits of the selector
  are unmeasured (patch `017` already preserves them - a new path must too); the reset-record and
  SMP cells are completely unmeasured and must not be written; bench crumbs (`0xc18`, `0xc94`)
  must never become bindings.
- Offline: the binding/skeleton only. Attended: none until a consumer exists.
- Effort: **S** (documentation-level, defer), or **S-M** if the SMP lane needs the `0xc00` read
  path (a 4-byte read; no writes).

## 7. Cross-block dependency map

| consumer | depends on | why |
| --- | --- | --- |
| MAC wiring (F) | GEMAC patch `009` (`.ndo_set_mac_address`), target `base-files` board.d | the netdev must accept a MAC; the factory value is userspace-supplied |
| efuse read provider (G) | CRG page `0x14880138` (clk-src syscon), CRG reset `(0x34,0)`, fixed clocks `efuse_ring_clk`/`osc` | provider needs the syscon windows + reset + clock names |
| efuse **write** (not proposed) | efuse mux `set_parent` (J), clksrc bit 2, reset pulse | the vendor sequence writes the mux and pulses reset |
| CPU OPP/cpufreq (not in v1) | efuse `avs` cell (G), PMU PWM + pwm-regulator (I), CRG PLL/mux (J) | OPP voltage/frequency selection |
| PMU PWM (I) | pinctrl `avs` function (block A), `osc_clk` | the AVS pin must be muxed; the PWM clock is the oscillator |
| GEMAC/LSW clocks (blocks D/E) | CRG per-gate parents (J) - `gemacN_clk`/`lsw_*` | their gates are not on APB (section 5.2) |
| SMP lane | SYSCTRL `0xc00` (K) | release/pen word [U] |
| restart (patch `010`) | CRG `core-info`/`softrst_val*`/`reboot-offset` and SYSCTRL selector (K) | the vendor machine restart reads/writes those |
| target image / sysupgrade | selector ABI (K, patch `017`) | pair bookkeeping and failover |

## 8. Offline work vs attended bench work

"Offline" = host-only, reproducible in this checkout; "attended" = needs a human at the box with
the recovery door armed (`parent:RUNBOOK.md`; BENCH-FACTS). Bench fires are stopped: nothing
below is scheduled.

| block | offline (host-only) | attended (requires recovery plan) |
| --- | --- | --- |
| F. MAC | board.d recipe, MAC increment validation, GEMAC `.ndo_set_mac_address` stub, nvmem-provider skeleton (if option 2) | one boot: netifd applies the MAC, `ip link` shows it, no random-address race |
| G. efuse | provider + cells + binding + `dt_binding_check` + compile | one **read** of `basic_dieid`/`avs`; nothing written, ever |
| H. RNG | none needed (drop) | none |
| I. PMU PWM | driver skeleton + binding + mocked regulator; field layout from disassembly | probe with output disabled, register readback, then a single safe duty step with recovery armed |
| I. LED PWM | none (out of scope) | none (unless a different board needs it) |
| J. CRG parents/PLL | fixed clocks + per-gate parents + read-only PLL/mux driver + `dt_binding_check` + compile (all doable now) | read `clk_summary` and the two PLL control words to confirm parents/rates; no writes |
| K. SYSCTRL/SYSENV | nothing new required (patches `010`/`017` exist); optional read-only nvmem skeleton | none until the SMP lane needs `0xc00` |

## 9. Risks

1. **efuse write = permanent brick risk.** The vendor interface reaches `bootrom_forbid`,
   `bootcli_forbid`, `jatg_efuse_forbid` and `sec_boot_en`. Do not implement write
   support; if it is ever needed, treat it as its own campaign with a blank-fuse policy.
2. **Key material exposure.** `msk`/`huk`/`jak`/`pubkh` are in the same provider
   namespace. A naive public binding that lists every cell publishes the layout of secrets; a
   naive sysfs port exposes their values. Keep them out of the binding and out of the port.
3. **CPU clock and voltage writes.** The mux at `0x138` bit 3 selects the CPU clock source; the
   PLL words at `0x198`/`0x1e0` program the CPU/other PLL; the PMU PWM at `0x14900800`
   sets `vdd_logic`. All three can hang or damage the board. Read-only first; every write
   needs an attended session with the recovery door armed.
4. **MAC identity.** Mixing the local-admin env `ethaddr` with the factory `bdinfo` MAC, or
   wrapping the WAN increment, gives two devices the same address or an invalid one. Validate and
   document the increment rule.
5. **Page aliasing.** LED PWM's `oen`/`loop` registers overlap patch `011`'s pinctrl window on
   `0x14900000`; the efuse clock-source cell is inside the CRG page. Any new driver that
   `devm_ioremap_resource`s a whole page will produce an `-EBUSY` (or, worse, a hidden
   double-map that the recovery workflow does not expect). Use syscon/regmap or exact
   non-overlapping windows.
6. **Selector bookkeeping.** Any new Linux path that writes `0x10100c08` can make the next boot
   take the loader's failover path; it is the loader's record, not ours. The existing
   `current_pair`/`next_pair` ABI must be the only writer, with the low-byte RMW and
   read-back semantics it documents.
7. **Untested code.** Everything written for these blocks is host-only so far. Never claim a new
   code path was "validated" by a compile or `dt_binding_check`; the project's evidence rules
   (REMAINING-DRIVERS section 0.1, EVIDENCE-INDEX) require an artifact.
8. **Publication boundary.** The pinned DTS already publishes the efuse cell map; this file
   repeats only what is needed to port the *safe* subset. Any new doc or binding that adds key
   material, MAC values, PINs or the fleet key literal crosses the publish boundary
   ([PUBLISH-READINESS.md](PUBLISH-READINESS.md), [../NOTICE.md](../NOTICE.md)).

## 10. Recommended order

Rationale: cheapest, lowest-risk, highest-dependency-value first; nothing here is needed for the
first upstream submission except the MAC wiring and (optionally) the CRG parents.

| order | item | why first / why now | offline? | effort |
| --- | --- | --- | --- | --- |
| 1 | **MAC wiring (F)**: GEMAC `.ndo_set_mac_address` + board.d recipe | needed by every target image and by the hybrid bench lane; zero hardware risk; resolves TARGET-DESIGN open question 5 | yes | S |
| 2 | **CRG fixed clocks + per-gate parents (J1)** | needed by blocks D/E and by the efuse port; pure driver-table work; matches the vendor DT exactly | yes | M |
| 3 | **SYSCTRL/SYSENV doc close-out (K)** | the selector half is already written; this only records that the other cells are deferred and why | yes | S |
| 4 | **efuse read-only nvmem (G)** | unblocks the CPU `avs` grade and the thermal/USB trims if those lanes need them; or proves they can stay DT-only | yes (readback attended) | M |
| 5 | **CRG PLL/mux read-only model (J2)** | finish the clock tree; the risky equation needs a readback, so it is a later, attended step | partly | L |
| 6 | **PMU PWM (I2)** | only if CPU OPP/cpufreq is ported; a power-rail experiment | no | M |
| 7 | (do not do) **efuse write**, **LED PWM**, **RNG** | brick risk / dead block / no hardware block | - | - |

## 11. Summary table

| block | what is [V] today | what is missing | framework | dependencies | offline / attended | effort | recommendation |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **F. MAC** | factory MAC in encrypted `bdinfo` (mtd6; plain copy at `0xde00`); vendor userspace wiring; random-initial + `ndo_set_mac_address` in `hi_kmac`; env `ethaddr` is not the factory value | netdev MAC programming in our GEMAC; target board.d | userspace (OpenWrt board.d) or optional nvmem "mac-address" cell | patch `009`; target base-files | offline / one attended boot | S | **do (order 1)** |
| **G. efuse** | full cell census, controller sequence, safe read path | command-word semantics; write is one-way; key cells | upstream nvmem provider | CRG page + reset + fixed clocks | offline / read-only attended | M (read-only) | **do the read-only port (order 4)**; never write |
| **H. RNG** | node has no resources; module wraps software `jitterentropy_rng` as hwrng (quality 512); `urngd` also runs | nothing to port - no hardware block exists locally | none (use mainline `jitterentropy_rng` / `urngd`) | none | - | none | **drop** |
| **I. LED PWM** | node + registers + clock gate + reset + pin functions; no driver in either rootfs or the vmlinux; no consumer; overlaps pinctrl window | driver, semantics, consumer | PWM chip + `leds-pwm` | pinctrl page sharing | - | L | **out of scope** |
| **I. PMU PWM** | single register, clock `osc`, one channel, `vdd_logic` pwm-regulator consumer, `avs` pin function; probe/apply model | field arithmetic; real hwver; safety envelope | PWM + standard `pwm-regulator` | pinctrl `avs`; cpufreq/OPP if used | offline / power-rail attended | M | **later (order 6)** |
| **J. CRG** | all fixed clocks, per-gate parents, PLL nodes (ctrl/status), mux nodes, vendor register model; **measured 2026-11-18: gate/reset/mux/PLL raw words + live clk tree rates** (5.6) | in-tree parent table; fixed clocks in DTS; PLL equation **validated by PLL1**; PLL2 divider-select mapping and mux write policy [U] | common clock framework (extend patch `004`) | efuse mux; GEMAC/LSW clocks | mostly offline; readback **done** 2026-11-18; writes remain | M (parents) / L (PLL) | **do parents (order 2)**, PLL read-only (order 5) |
| **K. SYSCTRL/SYSENV** | syscon + selector ABI (patches `010`/`017`); vendor sysenv driver decoded; cell census; **measured 2026-11-18: `0xc00=0x80609680`, `0xc08=0x10` (upper 24 bits zero), `0xc10=0x82000000`** | `0xc00`/`0xc10` physical semantics; `board_id-offset` absent on this board | keep sysfs selector ABI; optional read-only nvmem child | patch `010`/`017`; SMP lane | offline; read-only baseline **done** 2026-11-18 | S | **document/defer (order 3)** |

## 12. Evidence map (claim -> artifact)

| claim | evidence |
| --- | --- |
| MAC = `bdinfo` mtd6 (encrypted text + plain 6-byte copy at `0xde00`) | local `parent:dumps/router-20261108/full/mtd6-bdinfo.bin` (sha256 `49432e0f...fd763`) inspected host-side; partitioned layout re-derived with `parent:tools/decrypt_bdinfo.py`; vendor scripts `parent:rootfs-2.4.15/etc/board.d/02_network` lines 25-29, `.../etc/uci-defaults/99_fixwan` lines 17-52, `parent:rootfs-2.5.24/etc/uci-defaults/99_fixwan` line 41 |
| env `ethaddr` is not the factory MAC | local `parent:dumps/router-20261108/enva.bin` / full/mtd4-envb.bin string `ethaddr=` at `0x282`; comparison with the `bdinfo` MAC (values withheld); U-Boot random-MAC and `ethaddr` error strings in `parent:dumps/router-20261108/uboota.bin` |
| `fac` has no plaintext MAC copy | byte search over `parent:dumps/router-20261108/full/mtd5-fac.bin` (colon/12-hex/6-byte forms, both MACs) = 0 hits; record tags `boardinfo`/`sysinfo` start at `0x180000` |
| `hi_kmac` random-initial MAC + `ndo_set_mac_address` | `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kmac.ko` (md5 `e85cca99...`): undefined symbols `get_random_bytes`, `eth_mac_addr`, `eth_validate_addr`; probe disassembly offset `0xd0`; no MAC-read imports |
| efuse cell census / windows | raw DTB (sha256 `947ec62d...`) extracted from `parent:dumps/router-20261108/full/mtd11-kernela.bin` at `0x418610`; committed rendering [soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts) lines 483-754 |
| efuse controller sequence | `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_efuse.ko` (md5 `6a13b84a...`): `hi_efuse_read` 0x7a4, `hi_efuse_write` 0x56c, helper 0x4e4, probe 0x894; undefined symbols list; string table |
| no efuse MAC, no `read-only` parse | same module: no MAC strings; property strings are exactly the three phandles + clock/reset names; DT `read-only` is not in the string table |
| `avs` = `ns:osc_ring_svt_freq` | pinned DTS CPU nodes `nvmem-cell-names = "avs"` + `nvmem-cells` phandle; `hi_cpufreq.ko` (md5 `af2e15d9...`) nvmem imports/strings |
| RNG = software wrapper | `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kjent_rng.ko` (md5 `4f3080e8...`): `crypto_alloc_rng("jitterentropy_rng")`, `devm_hwrng_register`, quality 0x200; DT node with no `reg`; extracted vendor vmlinux has no RNG block strings |
| vendor vmlinux extraction | `parent:dumps/router-20261108/full/mtd11-kernela.bin` mtd partition: uImage data at `0x40` is a zImage, XZ stream (magic at payload offset `0x3cf7`) decompresses to 12,207,156 B; `Linux version 5.10.201 ... Sat Nov 25 18:18:57 2023`; host-only, local artifact |
| LED PWM dead | census: no `.ko` in `parent:rootfs-2.4.15/lib/hisilicon/ko` or the 2.5.24 tree matches `ledpwm`/`steptable`; zero `ledpwm` strings in the extracted vmlinux; board overlay has `gpio-leds` only; pinctrl window overlap vs pinned DTS line 1060 and patch `011` |
| PMU PWM = `vdd_logic` regulator | pinned DTS `pwm@1` (1361-1369), `reg@0` (1370-1381), `avs` pinmux (367-370); `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kpmupwm.ko` (md5 `f739c6de...`) probe/apply; `parent:rootfs-2.4.15/etc/rc.d/luofu` lines 14-22; pwm-regulator binding `parent:build/linux-kbuild/linux-5.10.201/Documentation/devicetree/bindings/regulator/pwm-regulator.txt` |
| CRG parents/PLL/mux model | vendor DT (pinned 201-411, 467-482); patch `0004` header (all gates on APB); `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_clk.ko` (md5 `3b331b90...`) gate/mux/PLL functions and property strings; lab transcription [../lab/luofu-clk/bindings/hisilicon,luofu-crg.md](../lab/luofu-clk/bindings/hisilicon,luofu-crg.md) |
| CRG measured gate/reset/mux/PLL words + live clk tree (2026-11-18, stock boot) | `parent:build/bench-inventory/20261118T130512Z-inv1-run/inventory.json` entries group `crg`; `followup/clk_summary.txt`; analysis [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 4 |
| `0x20`-group gate offsets are real | raw DTB decode (sha256 `947ec62d...`): explicit `reg-offset = <0x20>` on every `clk_gate@0020NN` node; resolves the PROVISIONAL note in patch `004` |
| efuse mux is written by the efuse driver | `hi_efuse.ko` imports `clk_set_parent` and calls it at write start (`hi_efuse_write` 0x56c); `hi_clk.ko` `hi_clk_mux_set_parent` 0x80 writes `0x138` |
| SYSCTRL/SYSENV cells | patches `010`/`017`; [SELECTOR-0x21.md](SELECTOR-0x21.md) sections 1, 5.2; `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_ksysenv.ko` (md5 `e325b525...`) probe/show/store offsets; `hi_sysctrl.ko` (md5 `8356e81d...`) exports |
| SYSCTRL measured cells (2026-11-18, stock boot; same boot + crosscheck vs sysfs) | `parent:build/bench-inventory/20261118T130512Z-inv1-run/inventory.json` entries group `sysctrl` + `crosschecks`; analysis [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 5 |
| vendor machine restart/`rstinfo` strings | extracted vendor vmlinux strings at file offsets `0x903e08`-`0x9041c4` (`System Reboot(%s)`, `hsan,crg`, `core-info`, `softrst_val0/1`, `hsan,sysctrl`, `hsan,rstinfo`, `record_addr`, `hisilicon,hsan_smp`) |
| GEMAC patch has no MAC setter | [`0009-net-gmac-luofu.patch`](../upstream/series-6.18/series/0009-net-gmac-luofu.patch): 0 occurrences of `ndo_set_mac_address` |
| nvmem MAC path exists in the 5.10 tree | `parent:build/linux-kbuild/linux-5.10.201/drivers/of/of_net.c` 57-79, 103-120; `net/ethernet/eth.c` 534-560 |
| no `mtd-mac-address` support | `grep -r mtd-mac-address` over `parent:build/linux-kbuild/linux-5.10.201`: 0 hits |
| all cited modules identical in 2.4.15/2.5.24 | appendix A (md5 pairs) |

## 13. Files that would need a one-line update if these plans are accepted (not edited here)

Per the shared-page contract, this document edits nothing. If the plans are accepted:

1. [TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.1 rows "efuse/RNG/LED PWM/PMU PWM/clk-corrector"
   and "sysctrl/sysenv" - replace "decide per block" with the decisions in sections 2-6;
   open question 5 ("MAC address / efuse ... [U]") - mark resolved by section 1.
2. [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 0.2 (out-of-scope list) - point at this
   document for the five blocks it defers.
3. [`../target-skeleton/dts/luofu-r116.dts`](../target-skeleton/dts/luofu-r116.dts) - add the
   four missing fixed clocks (section 5.4 item 1) and, if accepted, the PMU-PWM/regulator nodes.
4. [`../upstream/series-6.18/series/0004-arm-clk-reset-luofu-crg.patch`](../upstream/series-6.18/series/0004-arm-clk-reset-luofu-crg.patch) -
   retire the "provisional 0x20-group offsets" wording (section 5.2) and extend the gate table with
   parents (section 5.4 item 2).
5. [DOCUMENTATION-HUNT.md](DOCUMENTATION-HUNT.md) - the node-census row can cite this document for
   the efuse/rng/ledpwm/pmu-pwm/sysenv/clk-corrector nodes.
6. [`../lab/luofu-clk/bindings/hisilicon,luofu-crg.md`](../lab/luofu-clk/bindings/hisilicon,luofu-crg.md) -
   correct the "0x138 out of bounds for writes" rule to "CPU mux never writable, efuse mux only for
   a future efuse write path" (section 5.3).

## Appendix A - vendor module census used by this document

All modules are `vermagic=5.10.201 SMP mod_unload ARMv7` GPL; sizes and md5 are **identical**
in `parent:rootfs-2.4.15/lib/hisilicon/ko/` and `parent:rootfs-2.5.24/lib/hisilicon/ko/`
(host-side `stat`/`md5sum`, 2026-11-18) [V]:

| module | size (B) | md5 | role in this document |
| --- | --- | --- | --- |
| `hi_efuse.ko` | 10,144 | `6a13b84a7a8177269ece1cb955605090` | block G |
| `hi_kjent_rng.ko` | 4,624 | `4f3080e82eb105247ced023278b7dc05` | block H |
| `hi_kpmupwm.ko` | 7,568 | `f739c6dead863e1f4fa8c6a37f1d38d4` | block I |
| `hi_clk.ko` | 15,556 | `3b331b9058e13bc409dd8f0040af3b11` | block J |
| `hi_crg.ko` | 6,896 | `b716d4a429d3362ffb91e535a4fe2e28` | block J (register library) |
| `hi_ksysenv.ko` | 8,748 | `e325b525b9f57978ea9801d7af6ea75b` | block K |
| `hi_sysctrl.ko` | 4,880 | `8356e81d6988ce054870c5af745d9210` | block K (register library) |
| `hi_kmac.ko` | 75,116 | `e85cca99125fbe6370e8d386ea7d82fa` | block F (netdev/MAC) |
| `hi_cpufreq.ko` | 9,088 | `af2e15d97950c9b2696abed413843c3b` | dependency for `avs`/OPP/PMU PWM |
| `hi_clk_corrector.ko` | 4,896 | `ee5178424aca390885d9ab904e459e95` | TWD corrector (not ported in v1) |

There is **no** `hi_ledpwm.ko` or any module matching `hsan,ledpwm` in either tree, and the
extracted 2.4.15 vendor vmlinux contains zero `ledpwm`, `pmu-pwm`, `hsan,efuse` and `clk-gate`
strings (the only `efuse` substring matches are the word `refuse`) - every block in this
document is module-only. [V]

Other local artifacts cited:

| artifact | size / hash | note |
| --- | --- | --- |
| `parent:dumps/router-20261108/full/mtd11-kernela.bin` | 8,650,752 B, sha256 `6c7c0784...` | vendor kernel slot A (2.4.15); base DTB at `0x418610`; XZ kernel payload -> 12,207,156 B vmlinux |
| base DTB | 31,220 B, sha256 `947ec62d...` | matches the committed pinned DTS provenance |
| `parent:dumps/router-20261108/full/mtd6-bdinfo.bin` | 262,144 B, sha256 `49432e0f...` | block F source |
| `parent:dumps/router-20261108/enva.bin` | 262,144 B, sha256 `d1529faf...` | U-Boot env (non-factory `ethaddr`) |
| board-3 overlay (from the same vendor kernel image) | 4,920 B, sha256 `17fd44ed...` | `gpio-leds`, `gpio-keys-polled`, `ledpwm`/`pmupwm` pinctrl states, gemac0 disabled |

## Appendix B - the vendor DTS properties each block's driver consumes

| driver | properties it parses (string-table order from the module) |
| --- | --- |
| `hi_efuse` | `efusecon-phandle`, `efusedata-phandle`, `efuseclksrc-phandle`; clocks by name `ring_clk`/`mux`/`osc_clk`; reset index 0; optional GPIO |
| `hi_clk` | gate: `clock-output-names`, `reg-offset`, `bit` (+ an optional 0/1 cell); mux: `clock-output-names`, `reg-offset`, `mask`, `#clock-cells`, `clocks`, `table`; PLL: `clock-output-names`, `ctrl-offset`, `status-offset`, `status-bit`, `fnpllcfg0-offset`, `fnpllcfg0-value`, `div-factor`, `mode`, `freq-table`; top node: `hwver` |
| `hi_ksysenv` | `syscon` (phandle), `bootreg-offset` (required), `board_id-offset` (optional) |
| `hi_kpmupwm` | `npwm` (required), `hwver` (optional, default 16); clocks by index; pinctrl state name `enable` |
| `hi_kpmupwm`/`pwm-regulator` consumer | `pwms`, `regulator-min/max-microvolt`, `pwm-dutycycle-unit`, `pwm-dutycycle-range`, `regulator-name` (all standard bindings) |

## Appendix C - quick register reference (all [V] from the vendor DT/modules)

| address | block | access |
| --- | --- | --- |
| `0x14880014`/`0x14880020` | CRG gate words | RMW one bit per gate |
| `0x1488002c`/`0x30`/`0x34` | CRG reset words | RMW one bit per reset |
| `0x14880090` | CRG PLL status | read, bits 30/27 |
| `0x14880198` | PLL1 control pair base (`0x198`) | read-only in v1 |
| `0x148801e0` | PLL2 control pair base | read-only in v1 |
| `0x14880138` | clock muxes: bit 1 = efuse source, bit 3 = CPU source | read; bit 1 write only for efuse programming, bit 3 never |
| `0x14900110`/`0x1490011c` | LED PWM oen/loop (aliases pinctrl window) | no driver |
| `0x14900140` | efuse control: `0x0` strobe, `0x4` data, `0x8` address, `0xc` start | see section 2.2 |
| `0x14900280` | LED PWM cfg (2 channels) | no driver |
| `0x14900800` | PMU PWM (one 32-bit word) | see section 4.2 |
| `0x14948000` | efuse data window (256 B) | read-only in a port |
| `0x10100c00`/`0xc08`/`0xc10` | SYSCTRL: SMP release / boot selector / reset record | selector via patch `017`; others [U] |

**Measured 2026-11-18 [V] (one stock boot, read-only; full analysis
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) sections 4-5):** CRG `0x14=0x01360ef1`,
`0x20=0x02f8300b`, `0x2c=0x0d300e01`, `0x30=0xfe7fe05b`, `0x34=0x000ff309`,
`0x38=0x00200000`, `0x90=0x6a010008`, `0x100=0`, `0x138=0x00018042`,
PLL1 `0x198/0x19c = 0x8103c11c/0x11000000`, PLL2 `0x1e0/0x1e4 = 0x81064548/0x11000000`;
SYSCTRL `0xc00=0x80609680`, `0xc08=0x00000010`, `0xc10=0x82000000`. The PLL1 equation is
validated by the measured 1.2 GHz CPU rate; PLL2's division to the 333/166/100 MHz LSW domains
remains [U]. These are the vendor system's post-boot values, not a write recipe.

## Appendix D - how each [V] check was made (reproducible host-only procedure)

1. **Module census and hashes**: `stat -c%s` / `md5sum` over
   `parent:rootfs-2.4.15/lib/hisilicon/ko/*.ko` and the 2.5.24 tree; `modinfo` for
   vermagic/depends; `arm-none-linux-gnueabihf-nm` for defined/undefined symbols;
   `arm-none-linux-gnueabihf-objdump -dr` for the function offsets named in this document
   (module-relative offsets, not load addresses).
2. **DT decode**: base DTB extracted from `parent:dumps/router-20261108/full/mtd11-kernela.bin`
   at `0x418610` (31,220 B), sha256 compared against the committed pinned DTS provenance,
   then decompiled with host `dtc`; every phandle/`reg-offset`/`bit` value quoted
   here is from that decode, not from the pinned DTS's quoted-cell rendering.
3. **Vendor vmlinux strings**: uImage data at `0x40` of the same mtd11 partition is a zImage;
   its XZ stream (magic scan at payload offset `0x3cf7`, i.e. file `0x3d37`) decompresses
   to 12,207,156 B; `strings`/`grep` over it produce the block-driver and machine
   strings cited in sections 1-3 and 6.
4. **bdinfo layout**: raw partition read; header/`0xff`-run/plain-copy boundaries found by byte
   inspection; decryption performed with `parent:tools/decrypt_bdinfo.py` (its own key
   derivation) purely to confirm that the decrypted `mac` field and the plain copy at
   `0xde00` are the same bytes; values withheld.
5. **MAC searches**: byte-for-byte searches for the colon-ASCII, 12-hex-ASCII and 6-byte-binary
   encodings of both MACs over the `fac`, `bdinfo` and env dumps (results in sections 1.1-1.2).
6. **Scripts**: file contents read directly from `parent:rootfs-2.4.15` and
   `parent:rootfs-2.5.24`; line numbers are from those files as dumped.

No step above touched the device, the UART, TFTP, the recovery path or any fire tool; no vendor
code was executed. [V]
