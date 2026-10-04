# hi1105 mailbox doorbell -> interrupt: the arm register and the enabling value (phase 45, 2026-10-04)

Task `st_01a1073e`. Scope: **static web research only** - no device access, no register writes.
Deliverable: name the register that arms the H2D mailbox-doorbell interrupt on the HiSilicon
Hi1105 PCIe family, its value, and the mailbox register-block layout the vendor driver assumes -
each claim quoted from source I fetched.

## 0. Sources, and how they were fetched

All quotes are from the GPLv2-era vendor kernel tree
[`5-Super-Rookie-5/hongmengkernel`](https://github.com/5-Super-Rookie-5/hongmengkernel), the Hi1105
PCIe sibling already surveyed in `phase38/web-sdk-research.md` (B2/B3/B4). I fetched the tree two
ways and cross-checked them: a shallow sparse clone (grep/line numbers) and direct
`raw.githubusercontent.com` GETs (each quoted line below re-fetched and string-matched).

- pinned commit: **`297b33b07b56adb3fa53baf618fdbc16ad9fd447`** (`main`, repo default branch)
- blob URL prefix: `https://github.com/5-Super-Rookie-5/hongmengkernel/blob/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/`
- raw URL prefix: `https://raw.githubusercontent.com/5-Super-Rookie-5/hongmengkernel/297b33b07b56adb3fa53baf618fdbc16ad9fd447/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/`
- the file the task named, `oal/pcie/pcie_firmware_msg.c` (the H2D/D2H message ring), fetched 200,
  20,365 B; its mailbox/irq neighbours below are the ones that carry the register details.

## 1. Bottom line

**The H2D (host->device) doorbell interrupt is armed by `PCIE_CTRL_RB_HOST_INTR_MASK_OFF` = offset
`0x2E8` in the PCIe control/message block, bit 0 `host2device_tx_intr_mask`, by writing the bit to
`0` (0 = unmask, 1 = mask).** The doorbell itself is a separate register,
`PCIE_CTRL_RB_HOST2DEVICE_INTR_SET_OFF` = offset `0x2D4`, bit 0 `host2device_tx_intr_set`, written
`1` to ring; bit 3 `device2host_rx_intr_set` is the device->host ring (the host's own ISR source).

So for this family the doorbell->interrupt path is a three-register trip inside one 4 KB page, all
offsets relative to the PCIe control block base:

| offset | register | role |
| --- | --- | --- |
| `0x2D4` | `PCIE_CTRL_RB_HOST2DEVICE_INTR_SET` | **ring the doorbell** (bit0 H2D, bit1 H2D-rx, bit2 D2H-tx, bit3 D2H-rx) |
| `0x2D8` / `0x2DC` | `PCIE_CTRL_RB_HOST_DEVICE_REG0/1` | the two 32-bit inter-core ("mailbox") word registers |
| `0x2E4` | `PCIE_CTRL_RB_HOST_INTR_RAW_STATUS` | pre-mask raw status (bit0 H2D tx, bit1 H2D rx, ...) |
| **`0x2E8`** | **`PCIE_CTRL_RB_HOST_INTR_MASK`** | **the arm register** (bit0 `host2device_tx_intr_mask`; 0 = enable) |
| `0x2EC` | `PCIE_CTRL_RB_HOST_INTR_STATUS` | post-mask status the ISR reads |
| `0x2F0` | `PCIE_CTRL_RB_HOST_INTR_CLR` | write-1-to-clear |

The doorbell (`0x2D4`) sits immediately **below** the two mailbox word registers; the interrupt
mask that arms it (`0x2E8`) sits two words **after** the second mailbox word. The block base itself
is `PCIE_CTRL_RB_BASE = 0x04980000` (`platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h` L12) on the mp17c/hi1105 revision; the driver maps it
through `chip_info.addr_info.pcie_ctrl` (`pcie_chip_mp17c.c` L27, L575) into `pst_pci_ctrl_base`
(`pcie_host.c` L515-L526).

## 2. The register names and offsets (verbatim)

`platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h` (the mp17c register-block header, "generated automatically"):

- block base, L12-L13:
```
#define PCIE_CTRL_RB_BASE    (0x04980000)
```
- doorbell / interrupt-set register, L323-L336 - note the comment "0x2D4":
```
/* OBFF DEC配置寄存器 0x2D4 */
typedef union {
    struct {
        unsigned int host2device_tx_intr_set : 1; /* 0 */
        unsigned int host2device_rx_intr_set : 1; /* 1 */
        unsigned int device2host_tx_intr_set : 1; /* 2 */
        unsigned int device2host_rx_intr_set : 1; /* 3 */
        unsigned int reserved0 : 28; /* 4:31 */
    } bits;
    unsigned int as_dword;
} hreg_host2device_intr_set;
#define PCIE_CTRL_RB_HOST2DEVICE_INTR_SET_OFF    0x2D4
```
- the two inter-core word registers, L338-L358:
```
/* 核间通信寄存器 0x2D8 */
...
        unsigned int host2device_reg0 : 32; /* 0:31 */
...
#define PCIE_CTRL_RB_HOST_DEVICE_REG0_OFF    0x2D8
...
/* 核间通信寄存器 0x2DC */
...
        unsigned int host2device_reg1 : 32; /* 0:31 */
...
#define PCIE_CTRL_RB_HOST_DEVICE_REG1_OFF    0x2DC
```
  (`核间通信寄存器` = "inter-core communication register" - this driver's term for the mailbox word.)
- raw status, L360-L381:
```
/* PCIE屏蔽前中断上报 0x2E4 */   /* "PCIe interrupt report, before masking" */
        unsigned int host2device_tx_intr_raw_status : 1; /* 0 */
        unsigned int host2device_rx_intr_raw_status : 1; /* 1 */
        unsigned int device2host_tx_intr_raw_status : 1; /* 2 */
        unsigned int device2host_rx_intr_raw_status : 1; /* 3 */
        unsigned int device2host_intr_raw_status : 1; /* 4 */
...
#define PCIE_CTRL_RB_HOST_INTR_RAW_STATUS_OFF    0x2E4
```
- **the mask (arm) register, L383-L404** - the bitfield comment explicitly names bit 0 as the H2D tx mask:
```
/* PCIE中断上报 0x2E8 */
typedef union {
    struct {
        unsigned int host2device_tx_intr_mask : 1; /* 0 */
        unsigned int host2device_rx_intr_mask : 1; /* 1 */
        unsigned int device2host_tx_intr_mask : 1; /* 2 */
        unsigned int device2host_rx_intr_mask : 1; /* 3 */
        unsigned int device2host_intr_mask : 1; /* 4 */
        unsigned int reserved0 : 1; /* 5 */
        unsigned int mac_n2_intr_mask : 1; /* 6 */
        unsigned int mac_n1_intr_mask : 1; /* 7 */
        unsigned int phy_n2_intr_mask : 1; /* 8 */
        unsigned int phy_n1_intr_mask : 1; /* 9 */
        unsigned int pcie_slv_rresp_intr_mask : 1; /* 10 */
        unsigned int mac_n3_intr_mask : 1; /* 11 */
        unsigned int reserved1 : 20; /* 12:31 */
    } bits;
    unsigned int as_dword;
} hreg_host_intr_mask;
#define PCIE_CTRL_RB_HOST_INTR_MASK_OFF    0x2E8
```
- post-mask status, L406-L427:
```
/* PCIE屏蔽后中断上报 0x2EC */  /* "PCIe interrupt report, after masking" */
        unsigned int host2device_tx_intr_status : 1; /* 0 */
        unsigned int host2device_rx_intr_status : 1; /* 1 */
        ...
#define PCIE_CTRL_RB_HOST_INTR_STATUS_OFF    0x2EC
```
- clear register, L429-L444:
```
/* PCIE中断上报 0x2F0 */
        unsigned int host2device_tx_intr_clr : 1; /* 0 */
        unsigned int host2device_rx_intr_clr : 1; /* 1 */
        unsigned int device2host_tx_intr_clr : 1; /* 2 */
        unsigned int device2host_rx_intr_clr : 1; /* 3 */
...
#define PCIE_CTRL_RB_HOST_INTR_CLR_OFF    0x2F0
```

**Mask semantics are stated in code**: `pcie_chip_mp17c.c` L261, inside `oal_ete_intr_init`:
```
    /* mask:1 for mask, 0 for unmask */
```
so `0` = enabled/unmasked, `1` = masked.

## 3. The arm sequence (function + code + line numbers)

### 3.1 Ringing the H2D doorbell

`oal_pcie_h2d_int()` - `platform/oal/pcie/ete/ete_host.c` L1202-L1217:
```
int32_t oal_pcie_h2d_int(oal_pcie_res *pst_pci_res)
{
    void *pst_pci_ctrl_h2d_int_base = NULL;
    hreg_host2device_intr_set hreg;

    if (oal_unlikely(pst_pci_res->link_state <= PCI_WLAN_LINK_DOWN)) {
        oal_print_mpxx_log(MPXX_LOG_WARN, "pcie is linkdown");
        return -OAL_ENODEV;
    }
    hreg.as_dword = 0x0;
    pst_pci_ctrl_h2d_int_base = pst_pci_res->ete_info.reg.h2d_intr_addr;
    hreg.bits.host2device_tx_intr_set = 1; /* 1 for int */

    hreg_set_val(hreg, pst_pci_ctrl_h2d_int_base);
    return OAL_SUCC;
}
```
Its sibling `oal_pcie_d2h_int()` (L1219-L1235) is the mirror image: it sets
`hreg.bits.device2host_rx_intr_set = 1` at the **same** address - i.e. bits 0 and 3 of `0x2D4` are
the two doorbell directions. `h2d_intr_addr` is assigned in `oal_ete_res_init()` -
`platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c` L298:
```
    reg->h2d_intr_addr = pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST2DEVICE_INTR_SET_OFF;
```
`oal_pcie_firmware_tx_memcpy_trigger()` calls it (`platform/oal/pcie/pcie_firmware_msg.c` L221-L224):
```
void oal_pcie_firmware_tx_memcpy_trigger(oal_pcie_res *pst_pci_res)
{
    oal_pcie_h2d_int(pst_pci_res);
}
```

### 3.2 ARMING the doorbell interrupt - read-modify-write of 0x2E8, bit 0 = 0

`oal_frw_msg_int_unmask_mp17c()` - `platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c` L466-L483. This is the enable (the firmware-download
message path calls it; see 3.3):
```
OAL_STATIC void oal_frw_msg_int_unmask_mp17c(oal_pcie_res *pst_pci_res)
{
    hreg_host_intr_mask intr_mask;

    if (pst_pci_res->pst_pci_ctrl_base == NULL) {
        pci_print_log(PCI_LOG_ERR, "pst_pci_ctrl_base is null");
        return;
    }

    hreg_get_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);

    intr_mask.bits.host2device_tx_intr_mask = 0;
    intr_mask.bits.device2host_rx_intr_mask = 0;

    hreg_set_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);
    hreg_get_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);
    oal_print_mpxx_log(MPXX_LOG_DBG, "pcie host intr mask=0x%x", intr_mask.as_dword);
}
```
The reverse (mask) is `oal_frw_msg_int_mask_mp17c()` - L447-L464 - which sets those two bits to `1`.
The mp12 revision is identical (`platform/oal/pcie/chip/mp12/pcie_chip_mp12.c`
L361-L378); the mask/unmask pair is wired into the chip callback table at L555-L556 (mp17c) as
`cb->frw_msg_int_mask` / `cb->frw_msg_int_unmask`.

`oal_ete_intr_init()` - `platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c` L230-L276 - performs the same arm as part of ETE init, and is the
clearest statement that the H2D tx bit is *unmasked* (line 245) while `phy_n1/n2` stay masked:
```
    hreg_get_val(intr_mask, pst_pci_res->pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF);

    intr_mask.bits.host2device_tx_intr_mask = 0;
    intr_mask.bits.device2host_tx_intr_mask = 0;
    intr_mask.bits.device2host_rx_intr_mask = 0;
    intr_mask.bits.device2host_intr_mask = 0;

    /* WiFi中断需要在WiFi回调注册后Unmask */
    intr_mask.bits.mac_n1_intr_mask = 0;
    intr_mask.bits.mac_n2_intr_mask = 0;
    intr_mask.bits.phy_n1_intr_mask = 1;
    intr_mask.bits.phy_n2_intr_mask = 1;
    intr_mask.bits.mac_n3_intr_mask = 0;
```
and it records that the H2D tx event is deliberately *not* handled by the host (L272-L273):
```
    /* 检测到中断事件，但是host侧不处理 */
    pst_pci_res->ete_info.int_event_ignore_mask = (1 << HOST2DEVICE_TX_INTR_MASK);
```
`HOST2DEVICE_TX_INTR_MASK` is enum value 0 - `platform/inc/oal/ete/ete_comm.h` L42-L60:
```
typedef enum _PCIE_HOST_CTL_INTR_ {
    HOST2DEVICE_TX_INTR_MASK,
    HOST2DEVICE_RX_INTR_MASK,
    DEVICE2HOST_TX_INTR_MASK,
    DEVICE2HOST_RX_INTR_MASK,
    DEVICE2HOST_INTR_MASK,
    PCIE_MSG_IRQ_MASK,
    MAC_5G_INTR_MASK,
    MAC_2G_INTR_MASK,
    PHY_5G_INTR_MASK,
    PHY_2G_INTR_MASK,
    PCIE_SLV_RRESP_INTR,
    MAC_COMMON_INTR_MASK,
    ...
} pcie_host_ctl_intr;
```
- i.e. the enum index and the `0x2E8` bit position agree (both 0 for H2D tx).

### 3.3 The call chain that arms it

`oal_firmware_msg_download_pre()` - `platform/oal/pcie/pcie_firmware_msg.c` L486-L515 - the firmware-download entry; it unmasks the
doorbell interrupt before publishing the shared-memory descriptor:
```
    oal_pcie_firmware_msg_set_ch(pcie_res);
    oal_pcie_firmware_msg_int_unmask(pcie_res);
    ret = oal_pcie_firmware_msg_sharemem_update(pcie_res);
```
`oal_pcie_firmware_msg_int_unmask()` - `platform/oal/pcie/pcie_firmware_msg.c` L442-L447 - dispatches to the chip callback:
```
void oal_pcie_firmware_msg_int_unmask(oal_pcie_res *pst_pci_res)
{
    if (pst_pci_res->chip_info.cb.frw_msg_int_unmask != NULL) {
        pst_pci_res->chip_info.cb.frw_msg_int_unmask(pst_pci_res);
    }
}
```
`oal_firmware_msg_download_pre` is registered as `frw_download_pre` in the ETE linux ops table
(`platform/oal/pcie/ete/ete_linux.c` L835). The mirror `oal_firmware_msg_download_post()` (L517-L533) masks it again and
disables the IRQ. (The full IRQ enable that follows is `oal_enable_pcie_irq_with_request()`, L510 /
`oal_enable_pcie_irq()`, L512.)

### 3.4 Who consumes what

- The **device->host** direction is what the Linux host ISR handles. The firmware-message D2H
  callback is registered to enum `DEVICE2HOST_RX_INTR_MASK` - `platform/oal/pcie/ete/ete_host.c` L1665-L1675:
```
int32_t oal_pcie_frw_int_func_register(pcie_frw_cb_group_stru *pst_func)
{
    if (pst_func == NULL) {
        oal_print_mpxx_log(MPXX_LOG_ERR, "ptr NULL");
        return -OAL_ENODEV;
    }

    g_pcie_ete_intx_callback[DEVICE2HOST_RX_INTR_MASK] = pst_func->pcie_d2h_rx_isr_cb;

    return OAL_SUCC;
}
```
  registered from `oal_pcie_firmware_msg_int_func_register()` (`platform/oal/pcie/pcie_firmware_msg.c` L393-L401), whose ISR is
  `oal_pcie_firmware_msg_d2h_rx_isr()` (L386-L391, `oal_complete(&pci_res->frw_res.dev_done)`).
- The **host->device** direction is *not* consumed on the host. `oal_pcie_process_ete_host_intr()`
  skips the ignore mask (`platform/oal/pcie/ete/ete_host.c` L416-L421, comment at L416: *"host2device_tx_intr host不会单独上报host,
  其他中断响应时不清楚,等待device响应"* = "the host does not separately report host2device_tx_intr to
  itself; on other interrupts it is unclear; wait for the device to respond"), and the callback
  registered for bit 0 is a no-op:
```
void oal_ete_host2device_tx_intr_cb(void *fun)
{
    oal_print_mpxx_log(MPXX_LOG_INFO, "host2device intr, ignore by host");
}
```
  (`platform/oal/pcie/ete/ete_host.c` L2113-L2116; registered at L2139 via `oal_ete_intr_register(HOST2DEVICE_TX_INTR_MASK, ...)`).
  So `host2device_tx_intr_mask` (bit 0 of `0x2E8`) is the enable for the interrupt the **device**
  receives when the host writes the doorbell - exactly the event the luofu port wants on its
  firmware line.
- Power-down masks everything: `oal_ete_ip_exit()` writes `0xffffffff` to
  `reg.host_intr_mask_addr` (`platform/oal/pcie/ete/ete_host.c` L1347), and `ete_linux.c` L269 writes `0xffffffff` to
  `pst_pci_ctrl_base + PCIE_CTRL_RB_HOST_INTR_MASK_OFF`.

Bounded wake-path mask used for message transfer (`pcie_intr` sample masks the status register with
`0xc` in the eDMA path) - `pcie_edma_host.c` L1235-L1270 - is a *different*, older-IP path; the ETE
path above is the one the mp17c/hi1105 uses.

## 4. Mailbox register-block layout the hi1105 driver assumes

The hi1105 driver assumes one PCIe-control page (`PCIE_CTRL_RB_BASE = 0x04980000` on mp17c) accessed
through `pst_pci_ctrl_base`, plus a separate ETE page (`HOST_CTRL_RB_BASE = 0x04985000`,
`chip/mp17c/pcie_chip_mp17c.c` L29 / `inc/oal/mp17c/host_ctrl_rb_regs.h` L12) reached via
`pst_ete_base` (`pcie_host.c` L549-L559). The **message ring itself is not a device register file**:
it is a shared-memory structure `frw_share_mem` (`pcie_firmware_msg.c` L403-L433, L535-L575) whose
`h2d_ctl.data_daddr` / `d2h_ctl.data_daddr` are mapped with `oal_pcie_inbound_ca_to_va()`; the
registers the driver touches are the doorbell/irq words listed in section 1. The eDMA-revision
header shows the same offsets with an older bit numbering and a different base
(`platform/inc/oal/pcie/pcie_edma.h` L16, L62-L67, L82-L86):
```
#define PCIE_CONFIG_BASE_ADDRESS 0x40102000 /* pcie config base address,4KB, 4.70a */
...
#define PCIE_MSG_INTR_OFF                   0x2A8
...
#define PCIE_HOST_DEVICE_REG0               0x2E0
#define PCIE_HOST_DEVICE_REG1               0x2E4
#define PCIE_HOST_INTR_MASK_OFF             0x2E8
#define PCIE_HOST_INTR_STATUS_OFF           0x2EC
#define PCIE_HOST_INTR_CLR                  0x2F0
...
#define PCIE_H2D_DOORBELL_OFF            0x2D4
#define PCIE_D2H_DOORBELL_OFF            0x2D4
#define PCIE_H2D_TRIGGER_VALUE          (1 << 4)
#define PCIE_H2D_DOORBELL_TRIGGER_VALUE (1 << 5)
#define PCIE_D2H_DOORBELL_TRIGGER_VALUE (1 << 6)
```
**Caveat (flagged, not hidden):** the `pcie_edma.h` doorbell *values* (`1<<4 / 1<<5 / 1<<6`) and its
`host_edma_intr_mask` bit numbering (bits 8/9 for the doorbell masks, L285-L300) do **not** match the
mp17c `pcie_ctrl_rb_regs.h` (`host2device_tx_intr_set` = bit 0 of `0x2D4`;
`host2device_tx_intr_mask` = bit 0 of `0x2E8`). `pcie_edma.h` is the generic mp13/5.00a revision
(its `PCIE_CTRL_BASE_ADDR 0x40007000`), while `pcie_ctrl_rb_regs.h` is the mp17c IP the hi1105 code
selects. Where they disagree on bit positions, the mp17c header is the one the hi1105 `pcie_chip_mp17c.c`
code actually writes - both use the offset `0x2D4`/`0x2E8`, so the *offsets* are agreed; the *bit
numbering of the doorbell* differs by revision. The arm instruction in the hi1105 path is the mp17c
one: bit 0.

## 5. Relation to the luofu (hi5622v100) message block - the port candidate

The luofu message block is CA `0x40039000-0x40039600` (host BAR0 `0x3f1000-0x3f1600`), and the
record's known registers map onto this hi1105 layout **offset-for-offset at `0x2d4`/`0x2e4`/`0x2e8`/
`0x2ec`/`0x2f0`** - the same block, a different base:

| hi1105 name (offset) | luofu CA (base `0x40039000`) | record's name |
| --- | --- | --- |
| `PCIE_CTRL_RB_HOST2DEVICE_INTR_SET` `0x2D4` | `0x400392d4` | the doorbell (values `1`/`8` observed = bits 0/3 here) |
| `PCIE_CTRL_RB_HOST_INTR_RAW_STATUS` `0x2E4` | `0x400392e4` | (raw status) |
| **`PCIE_CTRL_RB_HOST_INTR_MASK` `0x2E8`** | **`0x400392e8`** | what the port calls "glue chn_res" |
| `PCIE_CTRL_RB_HOST_INTR_STATUS` `0x2EC` | `0x400392ec` | "glue status" (the port polls mask `0x3d8`) |
| `PCIE_CTRL_RB_HOST_INTR_CLR` `0x2F0` | `0x400392f0` | the forbidden ack register (never written) |

The port's observed doorbell values `1` and `8` are exactly bits 0 and 3 of `0x2D4`
(`host2device_tx_intr_set` / `device2host_rx_intr_set`) - a strong independent cross-check that the
luofu block is this register file.

**What the hi1105 evidence says to try (a candidate, not a re-derivation of the gate):** arm the H2D
doorbell interrupt by clearing bit 0 (`host2device_tx_intr_mask`) of **CA `0x400392e8`**, i.e.

```
en = omo_rd(omo_msg, 0x2e8);
omo_wr(omo_msg, 0x2e8, en & ~1u, "H2D doorbell intr unmask (bit0)");   /* 0 = unmask */
```

But note the port *already* writes this register. `opensource/lab/wifidrv1/wifidrv1.c` L170 defines
`OMO_CHN_RES 0x2e8`, L357 `OMO_GLUE_CHN_RES_MASK 0xfffffc20`, and L562-L564 does
`omo_wr(omo_msg, OMO_CHN_RES, v & OMO_GLUE_CHN_RES_MASK, "glue chn_res 0x400392e8")`; `0xfffffc20`
clears bits 0-4, so by the hi1105 semantics that RMW already writes 0 to bit 0 - **the mask register
may therefore already be armed in the port's run, and the mask bit may not be the missing gate.**
The value the hi1105 itself boots to is built in `oal_ete_int_mask_init()` (`platform/oal/pcie/ete/ete_host.c` L1677-L1715):
`device2host_tx=1, device2host=1, mac_n2/n1=1, phy_n2/n1=1, mac_n3=1`, and `device2host_rx=1` only
when `dma_trans` is false - i.e. its resting value leaves bit 0 (H2D tx) at **0/unmasked** as well.

So this source names the arm register and value with certainty, and it also *predicts* the port's
existing `0x400392e8` write already unmasks the doorbell - which is a falsifiable statement the
record can test: read CA `0x400392e8` bit 0 in a normal-op vendor boot. If it reads `0`, the doorbell
interrupt is armed in the vendor's own steady state and the gate is further inside (the interrupt
routing to line `0x4C`), not the mask.

## 6. Fetch log (this task)

| # | URL (pinned `297b33b07b56adb3fa53baf618fdbc16ad9fd447`) | status | used for |
| --- | --- | --- | --- |
| 1 | `.../platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h` | 200 | register names/offsets/bitfields (§2) |
| 2 | `.../platform/oal/pcie/ete/ete_host.c` | 200 | `oal_pcie_h2d_int`, ISR register/dispatch (§3) |
| 3 | `.../platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c` | 200 | arm/mask functions, `int_event_ignore_mask` (§3) |
| 4 | `.../platform/oal/pcie/pcie_firmware_msg.c` | 200, 20,365 B | call chain, D2H ISR (§3.3-3.4) |
| 5 | `.../platform/inc/oal/ete/ete_comm.h` | 200 | intr enum, bit order (§3.2) |
| 6 | `.../platform/inc/oal/pcie/pcie_edma.h` | 200 | older-revision doorbell values, caveat (§4) |
| 7 | `.../platform/oal/pcie/ete/ete_linux.c` | 200 | mask-all on power-off (§3.4) |
| 8 | `.../platform/inc/oal/mp17c/host_ctrl_rb_regs.h`, `.../oal/pcie/pcie_host.c` | 200 | ETE/ctrl base mapping (§4) |

Every quoted line above was fetched by this task and string-matched against the pinned commit
(§0 method); all matched. No device access, no register writes.
