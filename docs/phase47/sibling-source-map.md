# sibling-source-map: the hi1105 tree's interrupt sources, the ctrl-rb host2device source, and its enable (phase 47, 2026-10-04)

Task `st_01a10764`. Scope: **static + web read-only** - no device access, nothing written outside this
file. Question put to this report: *which interrupt-controller source id is the ctrl-rb's
host2device line, and did the firmware's init enable it - i.e. is the last link an UNREGISTERED
SOURCE or a wiring/e-delivery issue?*

Tree mined: [`5-Super-Rookie-5/hongmengkernel`](https://github.com/5-Super-Rookie-5/hongmengkernel),
pinned at **`297b33b07b56adb3fa53baf618fdbc16ad9fd447`** (`main`, the repo's only branch - the same
pin the phase-45 sibling report used). Method: a `--filter=blob:none --depth 1 --sparse` clone with
`kernel/linux-5.10-lts/drivers/connectivity/hi11xx` checked out (1,275 files, 35 MB) for greps plus
`raw.githubusercontent.com` GETs at the same commit for each quote. Every file quoted below was
re-fetched raw at the pin and compared to the clone **byte-for-byte - all 10 identical** (§9). Every
quote carries the `blob` URL at the pin with its line range; the line numbers are the fetched ones.

## 0. The three answers

| # | asked for | answer |
| --- | --- | --- |
| 1 | the source-id table | **It does not exist in this tree.** The tree is the *host* (Linux/PCIe-RC) driver plus the shared platform layer; it contains no device-firmware image, no RTOS/device OAL, and no register anywhere that maps a ctrl-rb or ETE event to a controller INTID. What it *does* carry is the family's source numbering, and that numbering is a **bit index inside each register block**, not a controller id (§2). Checked-path ledger: §6. |
| 2 | the source id of the ctrl-rb `host2device_tx` line | In the family's own numbering: **bit 0 / source index 0**. Register side: `host2device_tx_intr_raw_status` = bit 0 of the ctrl-rb `HOST_INTR_RAW_STATUS` (`0x2E4`), `host2device_tx_intr_mask` = bit 0 of `HOST_INTR_MASK` (`0x2E8`), `host2device_tx_intr_status` = bit 0 of `HOST_INTR_STATUS` (`0x2EC`), `host2device_tx_intr_clr` = bit 0 of `HOST_INTR_CLR` (`0x2F0`); driver side: `HOST2DEVICE_TX_INTR_MASK` is the **first** enumerator of `pcie_host_ctl_intr`, i.e. index **0** of the `g_pcie_ete_intx_callback[]` array (§2.1/§2.3). No INTID appears anywhere. |
| 3 | the enable register/value for that source | The ctrl-rb **`HOST_INTR_MASK`, block offset `0x2E8`, bit 0**, written **`0` (0 = unmask / 1 = mask)**, by read-modify-write. Polarity is stated in code at `pcie_chip_mp17c.c` L261: `/* mask:1 for mask, 0 for unmask */`. Two writers on mp17c: `oal_ete_intr_init()` (file the tree itself labels `/* device intx/msi init */`, L229) sets it to 0 at probe; `oal_frw_msg_int_unmask_mp17c()` sets it to 0 again when the firmware-download message service starts, and its mirror `..._mask_mp17c()` sets it back to 1 (§3). `HOST2DEVICE_TX_INTR_MASK`'s own host-side callback is a deliberate no-op - `"host2device intr, ignore by host"` - and the ISR masks the bit out before dispatch, with the comment "the host does not separately report host2device_tx; wait for the device to respond" (§4). |

**Where that leaves the gate question** (this report's reading, clearly separated from the quotes):
the sibling's evidence argues *against* "unregistered source" and *for* the wiring/delivery reading -
in this family the H2D-tx event is a **source the design deliberately accounts for and routes to the
device**, and the only enable in the tree that governs it (`0x2E8` bit 0) is *unmasked* in the
vendor's own steady state. But the sibling cannot prove that the luofu wires that event to the id the
firmware registered (`0x4C`), because the mapping from block bit to controller INTID is not in the
tree at all (§6, §8). Full reasoning and its limits: §8.

---

## 1. How the tree is organised, and why the table is missing by construction

Everything connectivity-related lives under one source tree that is compiled **per transport and per
OS**:

- `.../hi1105/platform/inc/oal/` - the OS-abstraction headers. In this vendored copy the OS dirs are
  `linux/` only (no `liteos/`, no device RTOS); the *SoC/platform* dirs are `mp12/ mp16/ mp16c/
  mp17c/` plus `ete/`, `sdio/`, `oam/`.
- `.../hi1105/platform/inc/oal/mp17c/` - the three register-block headers for the mp17c SoC:
  `pcie_ctrl_rb_regs.h` (64.5 KB), `host_ctrl_rb_regs.h` (159.7 KB), `pcie_pcs_rb_regs.h`; both big
  headers open with `/* This file is generated automatically. Do not modify it */`.
- `.../hi1105/platform/oal/pcie/` - the host driver: `chip/mp17c/` (mp17c revision logic),
  `ete/` (the ring engine), `pcie_linux.c` (Linux IRQ/PCI glue), `pcie_firmware_msg.c` (the H2D/D2H
  message ring), `pcie_rc_porting/{socv100..socv500}` (the *root-complex* porting layers for
  Kirin/Kunpeng/Hertz/WLAN-export/RK - i.e. the host SoC side).
- `.../hi1105/platform/frw/`, `.../hi1105/platform/main/`, `.../hi1105/platform/firmware/` -
  framework/startup/`firmware` in the sense of *firmware download*, not device firmware source.

The device side of the chip (the CPU that runs the firmware image and owns the GIC) is not part of
this tree, so a "firmware-side interrupt-controller source list" cannot be here. What the tree *does*
give, and what the rest of this file quotes, is the family's complete register-level source list for
the two blocks, the enable semantics for each source, and the host half of the delivery path.

---

## 2. The source list, as the tree actually has it

Two register blocks matter to the record: the **PCIe ctrl-rb** (the record's `0x40039000` block, the
one carrying the doorbell) and the **host_ctrl_rb** (the record's `0x4003a000` block, the ETE/ring
block). Both use the sibling's internal offsets over their own base
(`PCIE_CTRL_RB_BASE = 0x04980000`, `HOST_CTRL_RB_BASE = 0x04985000` on mp17c).

### 2.1 The ctrl-rb group - 12 named sources, host2device tx = bit 0

Register block base and the doorbell (write) register:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h#L12>
```
L12  #define PCIE_CTRL_RB_BASE    (0x04980000)
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h#L323-L334>
```
L323  /* OBFF DEC配置寄存器 0x2D4 */
L326          unsigned int host2device_tx_intr_set : 1; /* 0 */
L327          unsigned int host2device_rx_intr_set : 1; /* 1 */
L328          unsigned int device2host_tx_intr_set : 1; /* 2 */
L329          unsigned int device2host_rx_intr_set : 1; /* 3 */
L334  #define PCIE_CTRL_RB_HOST2DEVICE_INTR_SET_OFF    0x2D4
```

The "source list" proper - the pre-mask, mask, post-mask and clear registers, all bit-aligned, with
`host2device_tx` at bit 0 in every one of them:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h#L360-L381>
```
L360  /* PCIE屏蔽前中断上报 0x2E4 */          /* "PCIe interrupt report, before masking" */
L363          unsigned int host2device_tx_intr_raw_status : 1; /* 0 */
L364          unsigned int host2device_rx_intr_raw_status : 1; /* 1 */
L365          unsigned int device2host_tx_intr_raw_status : 1; /* 2 */
L366          unsigned int device2host_rx_intr_raw_status : 1; /* 3 */
L367          unsigned int device2host_intr_raw_status : 1; /* 4 */
L368          unsigned int reserved0 : 1; /* 5 */
L369          unsigned int mac_n2_intr_raw_status : 1; /* 6 */
L370          unsigned int mac_n1_intr_raw_status : 1; /* 7 */
L371          unsigned int phy_n2_intr_raw_status : 1; /* 8 */
L372          unsigned int phy_n1_intr_raw_status : 1; /* 9 */
L373          unsigned int pcie_slv_rresp_intr_raw_status : 1; /* 10 */
L374          unsigned int mac_n3_intr_raw_status : 1; /* 11 */
L379  #define PCIE_CTRL_RB_HOST_INTR_RAW_STATUS_OFF    0x2E4
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h#L383-L402>
```
L383  /* PCIE中断上报 0x2E8 */
L386          unsigned int host2device_tx_intr_mask : 1; /* 0 */
L387          unsigned int host2device_rx_intr_mask : 1; /* 1 */
L388          unsigned int device2host_tx_intr_mask : 1; /* 2 */
L389          unsigned int device2host_rx_intr_mask : 1; /* 3 */
L390          unsigned int device2host_intr_mask : 1; /* 4 */
L391          unsigned int reserved0 : 1; /* 5 */
L392          unsigned int mac_n2_intr_mask : 1; /* 6 */
L393          unsigned int mac_n1_intr_mask : 1; /* 7 */
L394          unsigned int phy_n2_intr_mask : 1; /* 8 */
L395          unsigned int phy_n1_intr_mask : 1; /* 9 */
L396          unsigned int pcie_slv_rresp_intr_mask : 1; /* 10 */
L397          unsigned int mac_n3_intr_mask : 1; /* 11 */
L402  #define PCIE_CTRL_RB_HOST_INTR_MASK_OFF    0x2E8
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h#L406-L425>
```
L406  /* PCIE屏蔽后中断上报 0x2EC */          /* "PCIe interrupt report, after masking" */
L409          unsigned int host2device_tx_intr_status : 1; /* 0 */
L410          unsigned int host2device_rx_intr_status : 1; /* 1 */
L411          unsigned int device2host_tx_intr_status : 1; /* 2 */
L412          unsigned int device2host_rx_intr_status : 1; /* 3 */
L425  #define PCIE_CTRL_RB_HOST_INTR_STATUS_OFF    0x2EC
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h#L429-L442>
```
L429  /* PCIE中断上报 0x2F0 */
L432          unsigned int host2device_tx_intr_clr : 1; /* 0 */
L433          unsigned int host2device_rx_intr_clr : 1; /* 1 */
L434          unsigned int device2host_tx_intr_clr : 1; /* 2 */
L435          unsigned int device2host_rx_intr_clr : 1; /* 3 */
L437          unsigned int pcie_slv_rresp_intr_clr : 1; /* 10 */
L442  #define PCIE_CTRL_RB_HOST_INTR_CLR_OFF    0x2F0
```

The same block also carries a *second*, one-bit interrupt group (`PCIE_MSG`) - the link-down/turn-off
group, not the mailbox one - so the block has two independent interrupt groups, not one:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h#L258-L266>
```
L258  /* PCIE MSG中断屏蔽前上报寄存器 0x2A8 */
L266  #define PCIE_CTRL_RB_PCIE_MSG_INTR_OFF    0x2A8
```
(`PCIE_MSG_INTR_STATUS` `0x2AC`, `PCIE_MSG_INTR_CLR` `0x2B0`, `PCIE_MSG_INTR_MASK` `0x2B4`; named
bits `soc_radm_msg_turnoff` and `link_down_irq`.)

### 2.2 The host_ctrl_rb group - the ETE block's own sources

Two ETE interrupt groups live in the second block: the device-side-relevant `ETE_INTR_*` group at
`0x86C..0x888`, and the host-facing `PCIE_CTL_ETE_INTR_*` group at `0xF00..0xF2C`:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/host_ctrl_rb_regs.h#L12>
```
L12  #define HOST_CTRL_RB_BASE    (0x04985000)
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/host_ctrl_rb_regs.h#L2920-L2934>
```
L2921          unsigned int tx_err_intr_mask : 5; /* 0:4 */
L2923          unsigned int tx_done_intr_mask : 5; /* 8:12 */
L2925          unsigned int rx_err_intr_mask : 6; /* 16:21 */
L2927          unsigned int rx_done_intr_mask : 6; /* 24:29 */
L2932  #define HOST_CTRL_RB_ETE_INTR_MASK_OFF    0x86C
```
with the companion offsets `HOST_CTRL_RB_ETE_INTR_CLR_OFF 0x870` (L2950), `..._STS_OFF 0x874`
(L2968), `..._RAW_STS_OFF 0x878` (L2986), `..._CH_DR_EMPTY_INTR_MASK_OFF 0x87C` (L2999) and the
`PCIE_CTL_ETE_INTR` group at `0xF00/0xF04/0xF08/0xF10` (L4330/L4348/L4366/L4384) with
`PCIE_CTRL_ETE_CH_DR_EMPTY_INTR_{MASK,CLR,STS,RAW}` at `0xF20/0xF24/0xF28/0xF2C`.
The same block also carries a **host-triggers-device-interrupt** group - defined but never used by
any `.c` in this tree: `HOST_CTRL_RB_HOST_TRIGGER_DEVIE_REG_0/1` (`0xEA8/0xEAC`),
`..._INTR_MASK` (`0xEB0`), `..._INTR_CLR` (`0xEB4`), `..._INTR_STS` (`0xEB8`), each a single
`host_trigger_device_intr_*` bit
(<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/mp17c/host_ctrl_rb_regs.h#L4280-L4312>).

### 2.3 The driver's source-index list and its per-source handler slots

The host driver's source list is the enum `pcie_host_ctl_intr`; the entries are used *as array
indices* into the callback table, with `HOST2DEVICE_TX_INTR_MASK` first (index 0) and the ETE-block
sources parked at 20..23:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/ete/ete_comm.h#L42-L60>
```
L42  typedef enum _PCIE_HOST_CTL_INTR_ {
L43      HOST2DEVICE_TX_INTR_MASK,
L44      HOST2DEVICE_RX_INTR_MASK,
L45      DEVICE2HOST_TX_INTR_MASK,
L46      DEVICE2HOST_RX_INTR_MASK,
L47      DEVICE2HOST_INTR_MASK,
L48      PCIE_MSG_IRQ_MASK,
L49      MAC_5G_INTR_MASK,
L50      MAC_2G_INTR_MASK,
L51      PHY_5G_INTR_MASK,
L52      PHY_2G_INTR_MASK,
L53      PCIE_SLV_RRESP_INTR,
L54      MAC_COMMON_INTR_MASK,
L55      HOST_ETE_RX_INTR_MASK = 20,
L56      HOST_ETE_TX_INTR_MASK,
L57      SYNC_RX_CH_DR_EMPTY_INTR_MASK,
L58      SYNC_TX_CH_DR_EMPTY_INTR_MASK,
L59      PCIE_HOST_CTL_INTR_BUTT /* max support msg count */
L60  } pcie_host_ctl_intr;
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/ete/ete_host.c#L36>
```
L36  OAL_STATIC pcie_callback_stru g_pcie_ete_intx_callback[PCIE_HOST_CTL_INTR_BUTT];
```
The array is *the* source table: index = source, `.pf_func` = its handler, dispatching iterates
`for (bit = 0; bit < sizeof(status) * 8; bit++)` and warns `"unregister host intr:%u"` for any set
bit with no callback
(<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/ete/ete_host.c#L163-L189>).
Note the index/bit agreement for the H2D tx source: enum value 0, ctrl-rb bit 0, and the enable
bit 0.

### 2.4 Counterpart table against the record's four registered ids

The record (phases 32/46) established that the firmware registers exactly four controller ids -
`0x2D`, `0x2E`, `0x4C`, `0x4E`. Those numbers are **not** in the sibling tree (see §6), and the
sibling's numbering is index-based, so no id mapping can be lifted from it. What the sibling *does*
supply is the block structure, and one cross-reference is exact: the record's own quote for the
`0x2D`/`0x2E` handlers reads the status word of a register block at CA `0x4003A86C` with status at
`+8` and clear at `+4`, i.e. `0x4003A874` / `0x4003A870`. Those are the sibling's
`HOST_CTRL_RB_ETE_INTR_MASK_OFF 0x86C` / `..._STS_OFF 0x874` / `..._CLR_OFF 0x870` over a base of
`0x4003a000` - the block the record calls "the ETE block" and this tree calls `HOST_CTRL_RB`.

| sibling block (offsets) | interrupt groups it contains | record's firmware ids |
| --- | --- | --- |
| `pcie_ctrl_rb` (`0x2E4/0x2E8/0x2EC/0x2F0`) | the 12-source host-intr group (H2D tx = bit 0) + the `PCIE_MSG` group (`0x2A8..0x2B4`) | `0x4C` = the H2D dispatcher, `0x4E` = the D2H kick (**structural fit, not a derivation** - see §8) |
| `host_ctrl_rb` ETE group (`0x86C..0x888`) | tx_err/tx_done/rx_err/rx_done per channel + ch_dr_empty | `0x2D`, `0x2E` (the two per-channel demuxers; the record's own quoted addresses are these offsets) |
| `host_ctrl_rb` PCIE_CTL group (`0xF00..0xF2C`) | the same four classes, host-facing, plus ch_dr_empty | not registered by the firmware in the record's analysis |

**This table is a correspondence of *blocks*, not of ids.** The sibling is consistent with "one
registered id per interrupt group", and the record's four ids sort into two pairs over the two
blocks - but the actual number assignment (why `0x4C` and not `0x4D`/`0x50`) is a silicon/boot fact
that is nowhere in the tree.

### 2.5 The way *this* family's host raises a device interrupt without the doorbell

The ETE descriptor control word has a per-descriptor interrupt bit for each direction, and the host
sets the device one on the H2D transmit path:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/ete/ete_comm.h#L62-L71>
```
L62  typedef union {
L63      struct {
L64          uint32_t reserve : 13; /* 0~12 */
L65          uint32_t dst_intr : 1; /* 13 , send int to device when set 1 */
L66          uint32_t src_intr : 1; /* 14 , send int to host when set 1 */
L67          uint32_t gather : 1;   /* 15 */
L68          uint32_t nbytes : 16;  /* 16~31 */
L69      } bits;
L70      uint32_t dword;
L71  } ete_h2d_sr_ctrl;
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/ete/ete_host.c#L1008-L1020>
```
L1008        oal_ete_init_h2d_sr_descr(&st_tx_sr_item, dst_addr, len);
...
L1014            st_tx_sr_item.ctrl.bits.src_intr = 1;
L1015            st_tx_sr_item.ctrl.bits.dst_intr = 1;
...
L1020        /* 硬件需要更新head_ptr才启动传输,可以先写ringbuf */
```
So in the sibling, every H2D SR descriptor that carries data is marked to raise **both** interrupts
(host and device): the device-side interrupt here is generated by the ETE engine from the
descriptor, *not* by the mailbox doorbell. (This is the family's data path. Whether the luofu port posts descriptors with this bit set is a
record-side question this report did not examine.)

---

## 3. The enable for the ctrl-rb host2device source

### 3.1 Register, bit, value, polarity

Register: `PCIE_CTRL_RB_HOST_INTR_MASK`, offset `0x2E8`, bit 0 `host2device_tx_intr_mask`
(§2.1). Value: **`0` = unmasked/enabled, `1` = masked**. The polarity is stated in the code, not
only in the header:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c#L255-L261>
```
L255      intr_mask.bits.mac_n3_intr_mask = 0;
L256
L257      hreg_set_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);
L258      hreg_get_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);
L259      oal_print_mpxx_log(MPXX_LOG_INFO, "pcie intr mask=0x%x", intr_mask.as_dword);
L260
L261      /* mask:1 for mask, 0 for unmask */
```

### 3.2 The two mp17c writers, and where in the life-cycle they run

The tree labels the whole function **`/* device intx/msi init */`** - i.e. this is the *device's*
interrupt-generation init as the host driver performs it - and it clears the H2D-tx mask bit:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c#L229-L275>
```
L229  /* device intx/msi init */
L230  static int32_t oal_ete_intr_init(oal_pcie_res *pst_pci_res)
...
L243      hreg_get_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);
L244
L245      intr_mask.bits.host2device_tx_intr_mask = 0;
L246      intr_mask.bits.device2host_tx_intr_mask = 0;
L247      intr_mask.bits.device2host_rx_intr_mask = 0;
L248      intr_mask.bits.device2host_intr_mask = 0;
L249
L250      /* WiFi中断需要在WiFi回调注册后Unmask */
L251      intr_mask.bits.mac_n1_intr_mask = 0;
L252      intr_mask.bits.mac_n2_intr_mask = 0;
L253      intr_mask.bits.phy_n1_intr_mask = 1;
L254      intr_mask.bits.phy_n2_intr_mask = 1;
L255      intr_mask.bits.mac_n3_intr_mask = 0;
...
L269      /* unmask all ete host int */
L270      oal_writel(0x0, pst_pci_res->ete_info.reg.ete_intr_mask_addr);
L271
L272      /* 检测到中断事件，但是host侧不处理 */
L273      pst_pci_res->ete_info.int_event_ignore_mask = (1 << HOST2DEVICE_TX_INTR_MASK);
```
Line 250 is the family's ordering rule - *unmask only after the callback is registered* - and line
273 is the statement that the H2D-tx source is *deliberately excluded from host handling* while still
being an armed source.

The second writer is the message-service unmask, reached from the firmware-download path:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c#L466-L483>
```
L466  OAL_STATIC void oal_frw_msg_int_unmask_mp17c(oal_pcie_res *pst_pci_res)
...
L475      hreg_get_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);
L476
L477      intr_mask.bits.host2device_tx_intr_mask = 0;
L478      intr_mask.bits.device2host_rx_intr_mask = 0;
L479
L480      hreg_set_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);
```
Its mirror `oal_frw_msg_int_mask_mp17c()` (L447-L464) sets the same two bits back to `1`
(`L458  intr_mask.bits.host2device_tx_intr_mask = 1; L459  intr_mask.bits.device2host_rx_intr_mask = 1;`).
The unmask is dispatched from
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/pcie_firmware_msg.c#L486-L502>
```
L486  int32_t oal_firmware_msg_download_pre(hcc_bus *bus)
...
L501      oal_pcie_firmware_msg_set_ch(pcie_res);
L502      oal_pcie_firmware_msg_int_unmask(pcie_res);
```
which is the "unmask when the message service starts" pattern the phase-45 report recorded.

### 3.3 What the sibling leaves the mask at while idle

`oal_ete_int_mask_init()` (called from `oal_ete_chan_init()`, ete_host.c L2137) builds the *resting*
value explicitly: it sets `device2host_tx = 1`, `device2host = 1`, `mac_n2 = mac_n1 = 1`,
`phy_n2 = phy_n1 = 1`, `mac_n3 = 1`, and `device2host_rx` only when `dma_trans` is false - leaving
bit 0 (H2D tx) **at 0**:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/ete/ete_host.c#L1683-L1701>
```
L1683      host_intr_mask.as_dword = 0x0;
...
L1687      host_intr_mask.bits.device2host_tx_intr_mask = 1;
L1688      /* dma_trans，须通过device2host_rx_int完成消息交互，不能mask; 非dma_trans，可mask */
L1689      if (pst_pci_res->chip_info.boot_cap.bits.dma_trans == OAL_FALSE) {
L1690          host_intr_mask.bits.device2host_rx_intr_mask = 1;
L1691      }
L1692      host_intr_mask.bits.device2host_intr_mask = 1;
L1693      host_intr_mask.bits.mac_n2_intr_mask = 1;
L1694      host_intr_mask.bits.mac_n1_intr_mask = 1;
L1695      host_intr_mask.bits.phy_n2_intr_mask = 1;
L1696      host_intr_mask.bits.phy_n1_intr_mask = 1;
L1697      /* mp17c新增，其他项目该字段为reserved */
L1698      host_intr_mask.bits.mac_n3_intr_mask = 1;
```
(`host2device_tx_intr_mask` is simply never set here, so it stays 0 from the `= 0x0` at L1683 - the
same "unmasked at rest" state the record's live dump shows for the luofu counterpart, `0x20`.)

---

## 4. Which side consumes the H2D-tx event - the sibling's own statement

The host ISR reads the ctrl-rb status, strips the ignored bit, clears the block, then dispatches the
remaining bits through the source table:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/ete/ete_host.c#L411-L421>
```
L411      hreg_get_val(intr_status, pst_pci_res->ete_info.reg.host_intr_sts_addr);
L412      hreg_get_val(pcie_ctl_ete_intr_sts, pst_pci_res->ete_info.reg.ete_intr_sts_addr);
L413      hreg_get_val(dr_empty_sts, pst_pci_res->ete_info.reg.ete_dr_empty_sts_addr);
L414
L415      /* 先清中断 */
L416      /* host2device_tx_intr host不会单独上报host，其他中断响应时不清楚，等待device响应 */
L417      intr_status.as_dword = intr_status.as_dword & (~(pst_pci_res->ete_info.int_event_ignore_mask));
```
(L416 = "the host does not separately report host2device_tx to itself; on other interrupts it is
unclear; wait for the **device** to respond".)

And the slot for that source is filled with a deliberate no-op:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/ete/ete_host.c#L2108-L2116>
```
L2108  static void oal_ete_intr_register(int bit, void (*fun_callback)(void *para))
L2109  {
L2110      g_pcie_ete_intx_callback[bit].pf_func = fun_callback;
L2111  }
L2112
L2113  void oal_ete_host2device_tx_intr_cb(void *fun)
L2114  {
L2115      oal_print_mpxx_log(MPXX_LOG_INFO, "host2device intr, ignore by host");
L2116  }
```
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/ete/ete_host.c#L2135-L2143>
```
L2135      (void)memset_s((void *)g_pcie_ete_intx_callback, sizeof(g_pcie_ete_intx_callback),
L2136                     0, sizeof(g_pcie_ete_intx_callback));
L2137      oal_ete_int_mask_init(pst_pci_res);
L2138
L2139      oal_ete_intr_register(HOST2DEVICE_TX_INTR_MASK, oal_ete_host2device_tx_intr_cb);
L2140      oal_ete_intr_register(HOST_ETE_RX_INTR_MASK, oal_ete_default_intr_cb);
L2141      oal_ete_intr_register(HOST_ETE_TX_INTR_MASK, oal_ete_default_intr_cb);
L2142      oal_ete_intr_register(SYNC_RX_CH_DR_EMPTY_INTR_MASK, oal_ete_default_intr_cb);
L2143      oal_ete_intr_register(SYNC_TX_CH_DR_EMPTY_INTR_MASK, oal_ete_default_intr_cb);
```

**So: in this family the H2D-tx source is not an unregistered source, and not a host-handled one -
it is a *registered-but-ignored* source whose consumer is the device.** That is the closest the
sibling comes to answering the gate question: it says the event exists, is enabled
(§3), and is expected to be consumed by the device side.

---

## 5. The delivery path, both halves

### 5.1 Host half (quoted)

1. The host driver arms the *device's* interrupt generation: `oal_ete_intr_init()`
   (`/* device intx/msi init */`, §3.2) writes the ctrl-rb mask and clears the ETE host mask
   (`oal_writel(0x0, ete_intr_mask_addr)`, L270).
2. The host registers its own IRQ with Linux. The IRQ number is the **PCI device's IRQ** from the
   PCI core (comment: `/* Read from DTS later */`), or an MSI vector = base + index; no device
   INTID is involved:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/pcie_linux.c#L506-L527>
```
L506      if (pst_pci_lres->pst_pci_res->chip_info.ete_support == OAL_TRUE) {
L507          ret = request_irq(pst_pci_dev->irq, oal_pcie_intx_ete_isr, IRQF_SHARED,
L508                            "hisi_ete_intx", (void *)pst_pci_lres);
L509      }
...
L524      oal_io_print("raw irq: %d\n", pst_pci_dev->irq);
L525
L526      /* Read from DTS later */
L527      pst_pci_lres->st_msi.is_msi_support = g_hipci_msi_enable;
```
(MSI variant: `pci_enable_msi()` then one `request_irq(pst_pci_dev->irq + i, ...)` per non-NULL
`st_msi.func[i]` handler; the handler array is the `oal_pcie_msi_stru` - `int32_t is_msi_support;
oal_irq_handler_t *func; /* msi interrupt map */ int32_t msi_num;`
<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/inc/oal/pcie_host_mpxx.h#L152-L156>.)
3. The endpoint raises the host interrupt; the host ISR reads the two status words plus the DR-empty
   word, clears what it read, and dispatches per bit (§4).
4. The *device* asserting the endpoint INTx is also visible in the PM code, which has to stop the
   device's INTx from pulling `xfer_pending`:

<https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/platform/oal/pcie/chip/mp17c/pcie_pm_mp17c.c#L157-L170>
```
L157      val = oal_readl(pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_PCIE_CFG_OFF);
L158      if (is_forbid == OAL_TRUE) {
L159          /* 禁止dev侧intx中断拉高xfer_pending */
L160          oal_writel((val & (~PCIE_INTX_XFER_PENDING_SEL)), ... + PCIE_CTRL_RB_PCIE_CFG_OFF);
...
L169          /* 允许dev侧intx中断拉高xfer_pending */
L170          oal_writel((val | PCIE_INTX_XFER_PENDING_SEL), ... + PCIE_CTRL_RB_PCIE_CFG_OFF);
```
(`PCIE_INTX_XFER_PENDING_SEL` = `0x4` = bit 2 of `PCIE_CFG` `0x2D0` =
`soc_app_xfer_pending_sel`, pcie_ctrl_rb_regs.h L314/L319.)

### 5.2 Device half (NOT in this tree)

There is no device-side file in the tree at all: the OAL OS directories are `linux/` only, there is
no interrupt-controller driver, no vector table, no GIC/distributor reference, and the string
`0x4016` (the record's device GIC window at CA `0x40160000`/`0x40161000`) does not occur anywhere in
the 1,275-file tree. The device firmware is therefore only visible to the record, not to the
sibling - see §6 and §8. Consistently, the checked-out tree holds no binary of any kind (no
firmware image, no `.bin`/`.img`/`.ko`; the only non-`.c`/`.h` files are three `*_defconfig`, one
`mpxx.mk` and the `Kconfig`/`Makefile` pair).

---

## 6. Absence ledger - the paths checked before declaring the table absent

| # | check | result |
| --- | --- | --- |
| 1 | `git ls-tree -r --name-only HEAD` over the whole repo (36,506 paths), filtered for `intr\|_irq\|gic\|intc` | Under the entire `hi11xx` tree the only interrupt-named files are the *host* WiFi IRQ files: `wifi/dpe/hal/host_hal_irq.{c,h}` and `wifi/dpe/hal/product/{mp12,mp16,mp16c_gf61,mp17c/mpw,mp17c/pilot}/host_irq_*`. No device-side interrupt file exists. |
| 2 | Full checkout of the whole hi11xx tree (1,275 files) + `find -iname '*intr*' -o -iname '*irq*' -o -iname '*gic*'` | Same 12 files as #1 (`host_hal_irq.*`, `host_irq_mp12/16/16c/mp17c*.{c,h}`). |
| 3 | `grep -rn -w` over the tree for `intr_source`, `int_num`, `intr_id`, `src_id`, `int_src`, `irqno`, `hwirq`, `virq`, `INT_ID`, `INTID`, `interrupt_id` | **0 hits** each. `irq_id` has 2 hits, both SDIO CPU-affinity (`irq_set_affinity_hint`) in `platform/oal/oal_sdio_host.c` L297/L300. `irq_num` has 1 hit, `plat_gpio.c` L387, a *host* GPIO IRQ log line. |
| 4 | `grep -rn -E "0x4016\|4016010\|4016110"` over the tree | **0 hits.** The GIC/distributor address space of the record's firmware appears nowhere. (Same via the GitHub code-search API on the repo: `"0x40161100"`, `"0x4016010c"`, `"0x40161000"` → 0 results.) |
| 5 | `grep -rn -E "#define +[A-Za-z0-9_]*(IRQ\|INTR)[A-Za-z0-9_]* +\(?0x[0-9a-fA-F]+\)?"` | Only the SSI/PMU register offsets (`IRQ0_MASK_REG 0x5003`, `SCP0/OCP0/OCP1/IRQ0/OCP2_IRQ_REG 0x6000-0x6004`, `IRQ0_NP_EVENT_REG 0x7003`, the HI6516 `0x6005..0x7006` set) and GPIO IRQ attributes. None is a controller source id. |
| 6 | Complete interrupt-register inventory of both mp17c blocks (`grep -E "^#define .*INTR.*_OFF "`) | `pcie_ctrl_rb`: `PCIE_MSG_INTR` `0x2A8/0x2AC/0x2B0/0x2B4`, `HOST2DEVICE_INTR_SET` `0x2D4`, `HOST_INTR_RAW_STATUS/MASK/STATUS/CLR` `0x2E4/0x2E8/0x2EC/0x2F0`, `ETE_INTR_STATUS` `0x648`. `host_ctrl_rb`: `ETE_INTR_{MASK,CLR,STS,RAW_STS}` `0x86C-0x878`, `ETE_CH_DR_EMPTY_INTR_*` `0x87C-0x888`, `HOST_TRIGGER_DEVIE_*` `0xEB0-0xEB8`, `PCIE_CTL_ETE_INTR_*` `0xF00-0xF10`, `PCIE_CTRL_ETE_CH_DR_EMPTY_INTR_*` `0xF20-0xF2C`. **No id/vector/source-number register in either block.** |
| 7 | Every numeric IRQ in the tree inspected for "device id" semantics | They are all *host* Linux IRQs: `request_irq(pst_pci_dev->irq, ...)` (pcie_linux.c L507), MSI `pst_pci_dev->irq + i`, and the single hardcoded one - `#define OAL_IRQ_NUM 5` (`platform/inc/oal/oal_hardware.h` L16) used for the firmware-message IPC at `platform/frw/frw_event_deploy.c` L286/L326 (`st_irq_dev.irq = OAL_IRQ_NUM;`) - which is a host-side IPC IRQ, not a device source. |
| 8 | Cross-tree check of other vendored copies of the same driver (GitHub code search: `host2device_tx_intr_mask`, `filename:pcie_ctrl_rb_regs.h` → `wutaijieing/android_kernel_LIO_kirin990_103`, `hisi-oss/android_kernel_huawei_kirin970`, `HK416AAA/HarmonyOS6_Hi36C0_Kernel`, `Hooo1941/kernel_huawei_hepburn...`) | Same file set. The one extra OS variant, `platform/inc/oal/liteos/arch/oal_interrupt.h`, is a Linux-API shim (`#include <linux/interrupt.h>`, `oal_request_irq` → `request_irq`, plus a `GPIO_TO_IRQ` helper off `OS_USER_HWI_MAX`) - still no device source table. |

For contrast, the tree's own notion of a *device-interrupt* register write is the doorbell itself
(`oal_pcie_h2d_int()`, ete_host.c L1202-L1217: `hreg.bits.host2device_tx_intr_set = 1; /* 1 for
int */`) - a *bit write*, not an id.

---

## 7. Cross-checks

- **Independent pin check.** The phase-45 report `docs/phase45/hi1105-mailbox-irq.md` quotes the same
  files and line ranges (`pcie_ctrl_rb_regs.h` L12/L323-L336/L360-L381/L383-L404/L406-L427/L429-L444,
  `pcie_chip_mp17c.c` L230-L276/L261/L447-L483/L466-L483, `ete_host.c` L1202-L1217/L2113-L2116/L2139,
  `ete_comm.h` L42-L60). Every one of those line numbers is confirmed here against the fetched raw
  files at the same commit - the earlier report's citations are accurate.
- **Counterpart bit naming across revisions.** In `mp16`/`mp16c`/`mp12` the same header names bit 5 of
  `HOST_INTR_MASK` `pcie_msg_irq_mask` (mp16 L823, mp16c L822, mp12 L877) whereas mp17c leaves it
  `reserved0` (mp17c L391). The enum keeps `PCIE_MSG_IRQ_MASK` at index 5 regardless. So mp17c has
  **no** ctrl-rb source at index 5, which is worth knowing when reading the record's 12-bit source
  list against the enum.
- **Byte-identity.** All ten fetched files matched the local pinned clone byte-for-byte (§9), so the
  greps in §6 and the quotes in §2-§5 describe the same bytes.

---

## 8. What this settles, and what it does not

**Settles (sibling-sourced):**

1. The family's ctrl-rb `host2device_tx` source is **bit 0 / index 0**, in a 12-source group whose
   pre-mask, mask, post-mask and clear registers are bit-aligned (§2.1, §2.3).
2. Its enable is **`HOST_INTR_MASK` bit 0 = 0** (0 = unmask), with `1 = mask` stated in code, written
   by both `oal_ete_intr_init()` ("device intx/msi init") and the message-service unmask, and left at
   0 in the resting mask value (§3).
3. The source is **registered and explicitly ignored by the host** - `"host2device intr, ignore by
   host"`, masked out of dispatch by `int_event_ignore_mask`, with the comment "wait for the device to
   respond" (§4). In this family the H2D-tx event is a *device-consumed* source by design.
4. The host-side delivery path never involves a device INTID: it is a Linux PCI IRQ (INTx `irq` or
   MSI base+index) whose ISR demuxes the block status bit by bit (§5.1). A device INTID only enters
   the picture on the *device* side, which this tree does not contain (§5.2).
5. The family has a **second, descriptor-level** device-interrupt mechanism: the host sets
   `dst_intr` = "send int to device" on H2D ETE SR descriptors (§2.5) - independent of the mailbox
   doorbell.

**Does not settle:** whether the luofu wires the ctrl-rb's H2D-tx bit to the controller id the
firmware registers (`0x4C`). That assignment is silicon/boot-level and is in no file of this tree
(§6, ledger row 4). The deliverable can therefore name the source *within the block's numbering* and
its enable, but not an INTID.

**Consequence for the gate question, flagged as this report's inference:** the sibling removes the
"unregistered source" reading of the last link as far as the ctrl-rb itself goes - the block's own
source mask is unmasked at rest in the vendor's design, the source has a defined handler slot, and
its intended consumer is stated in code to be the device. Combined with the record's own phase-46
result (the raw and post-mask status both latch on the doorbell, so the ctrl-rb *does* generate the
event) and phase-32 §4.4 (the firmware registers **and** enables an id for the mailbox path), the
remaining suspect is the delivery between the ctrl-rb's event and the firmware's registered line -
i.e. **wiring/delivery**, not a missing registration. The sibling's structural table (§2.4) says the
same thing from the other side: the family assigns interrupt groups per block, so a mailbox block
with an armed, registered source pair (`0x4C`/`0x4E` for H2D/D2H) is the *expected* shape - and the
numbers themselves are simply not derivable from this tree.

Two sibling facts are directly usable by the gate work in either case:

- the resting/arming values and polarity for the block mask are `0x2E8` bit0 = 0 (`0x20` on the
  record's live dump matches this family state exactly), so re-asserting that mask cannot be the
  gate;
- if the mailbox path stays dead, the family's *other* device-interrupt route is the ETE descriptor
  bit `dst_intr` on a posted H2D SR descriptor (§2.5) - a path that does not pass through the
  doorbell at all.

---

## 9. Fetch log

Remote `https://github.com/5-Super-Rookie-5/hongmengkernel`, commit
`297b33b07b56adb3fa53baf618fdbc16ad9fd447` (verified as the clone's `HEAD`; `main` is the repo's only
branch - `git ls-remote --heads origin` returns exactly one ref). Raw prefix
`https://raw.githubusercontent.com/5-Super-Rookie-5/hongmengkernel/297b33b07b56adb3fa53baf618fdbc16ad9fd447/`.

| file (under `kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/`) | HTTP | bytes / lines | identical to pinned clone |
| --- | --- | --- | --- |
| `platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h` | 200 | 63,273 / 1,884 | yes |
| `platform/inc/oal/mp17c/host_ctrl_rb_regs.h` | 200 | 156,889 / 4,677 | yes |
| `platform/inc/oal/ete/ete_comm.h` | 200 | 9,665 / 240 | yes |
| `platform/inc/oal/oal_hardware.h` | 200 | 1,099 / 39 | yes |
| `platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c` | 200 | 25,178 / 647 | yes |
| `platform/oal/pcie/ete/ete_host.c` | 200 | 85,671 / 2,326 | yes |
| `platform/oal/pcie/pcie_linux.c` | 200 | 128,972 / 3,925 | yes |
| `platform/oal/pcie/pcie_firmware_msg.c` | 200 | 20,365 / 583 | yes |
| `platform/frw/frw_event_deploy.c` | 200 | 20,260 / 622 | yes |
| `platform/inc/oal/mp16/pcie_ctrl_rb_regs.h` | 200 | 78,697 / 2,266 | yes |
| `platform/oal/pcie/chip/mp17c/pcie_pm_mp17c.c` | 200 | 12,922 / 312 | yes |
| `platform/inc/oal/pcie_host_mpxx.h` | 200 | 36,203 / 962 | yes |
| cross-tree only: `wutaijieing/android_kernel_LIO_kirin990_103` `.../platform/inc/oal/liteos/arch/oal_interrupt.h` | 200 | 1,728 | n/a (different repo) |
