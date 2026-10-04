# Phase 45 - HiSilicon Hi3881V100 (SDIO) host-side mailbox-notification interrupt-enable logic (2026-10-04)

Task `st_01a1073f` (parent `01a0fc5c`). Scope: **static web research only** - no device access, no
register writes. Question: in the OpenHarmony `device_soc_hisilicon` **hi3881v100** HCC/SDIO host
stack (GPLv2), which register(s) enable the mailbox-notification interrupt, what is the arm
sequence, and how does the host learn that a device notification is pending?

Every code quote below was fetched by this task from
`https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/...`; the URL and the
raw line numbers are given with each quote. Line numbers are the raw-file numbers, i.e. identical to
the GitHub blob view.

Source files cited (all GPLv2; header "Copyright (C) 2021 HiSilicon (Shanghai) Technologies CO.,
LIMITED.", lines 2-16 of every file). Full URLs (all under the `master` branch):

| id | full URL |
|----|----------|
| `oal_sdio.h` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/oal_sdio.h |
| `oal_sdio_host.c` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/oal_sdio_host.c |
| `oal_sdio_host_if.h` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/oal_sdio_host_if.h |
| `oal_sdio_if.h` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/oal_sdio_if.h |
| `oal_sdio_comm.h` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/oal_sdio_comm.h |
| `oal_channel_host_if.h` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/oal_channel_host_if.h |
| `plat_sdio.c` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/plat_sdio.c |
| `plat_pm_wlan.c` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/plat_pm_wlan.c |
| `plat_board_adapt.h` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/oal/plat_board_adapt.h |
| `hcc_host.c` | https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/driver/hcc/hcc_host.c |

## Bottom line

1. **The enable register is `HISDIO_REG_FUNC1_INT_ENABLE` = function-1 register offset `0x09`**, and
   it is armed by writing the source mask **`HISDIO_FUNC1_INT_MASK` = `0x07`**
   (`DREADY|RERROR|MFARM`) - `oal_sdio.h:39-55`, applied at `oal_sdio_host.c:1117-1118`.
2. **There is no per-message-id enable and no separate "doorbell" register.** The host->device
   "doorbell" *is* the message-bitmap write `1 << msg_id` into `HISDIO_REG_FUNC1_WRITE_MSG`
   (`0x24`) - `oal_sdio_host.c:175-176` - and the host is told a device message is pending by the
   single "ARM Msg" interrupt class `HISDIO_FUNC1_INT_MFARM` (`1 << 2`, `0x04`) -
   `oal_sdio.h:52`, `oal_sdio_host.c:821-825`.
3. **The enable is by interrupt *source class*, not by message id**: all 32 D2H ids share the one
   `MFARM` interrupt. Per-id routing is pure software (`oal_sdio_message_register` fills
   `hi_sdio->msg[msg]`, `oal_sdio_host.c:316-325`; the ISR walks the set bits,
   `oal_sdio_host.c:427-438`).
4. **Negative, explicitly checked:** a repo-wide scan of **all 240 `.c`/`.h` files under
   `common/platform/wifi/hi3881v100/`** for `mailbox`, `doorbell`, `int_enable`, `intr_enable`,
   `irq_enable`, `HISDIO_REG_FUNC1_INT` found **no "mailbox" or "doorbell" symbol at all**, and the
   only device mailbox/message-path interrupt-enable register is `HISDIO_REG_FUNC1_INT_ENABLE`
   (`0x09`). The only other `*_irq_enable` hits are the RTOS-side (LiteOS) CPU-HWI enabler
   `hi_irq_enable` in `driver/oal/hi_isr.c:125` and `driver/include/hi_isr.h:134`, which are not part
   of the SDIO host client path. Checked paths and method are listed in section 7.

## 1. The register map (quoted)

`oal_sdio.h:38-55` (URL `.../driver/oal/oal_sdio.h`):

```c
#define HISDIO_REG_FUNC1_FIFO                      0x00        /* Read Write FIFO */
#define HISDIO_REG_FUNC1_INT_STATUS                0x08        /* interrupt mask and clear reg */
#define HISDIO_REG_FUNC1_INT_ENABLE                0x09        /* interrupt */
#define HISDIO_REG_FUNC1_XFER_COUNT                0x0c        /* notify number of bytes to be read */
#define HISDIO_REG_FUNC1_WRITE_MSG                 0x24        /* write msg to device */
#define HISDIO_REG_FUNC1_MSG_FROM_DEV              0x28        /* notify Host that device has got the msg */
#define HISDIO_REG_FUNC1_MSG_HIGH_FROM_DEV         0x2b        /* Host receive the msg ack */

/* sdio extend function, add 64B register for hcc */
#define HISDIO_FUNC1_EXTEND_REG_BASE        0x3c
#define HISDIO_FUNC1_EXTEND_REG_LEN         64

#define HISDIO_FUNC1_INT_DREADY             (1 << 0)           /* data ready interrupt */
#define HISDIO_FUNC1_INT_RERROR             (1 << 1)           /* data read error interrupt */
#define HISDIO_FUNC1_INT_MFARM              (1 << 2)           /* ARM Msg interrupt */
#define HISDIO_FUNC1_INT_ACK                (1 << 3)           /* ACK interrupt */

#define HISDIO_FUNC1_INT_MASK (HISDIO_FUNC1_INT_DREADY | HISDIO_FUNC1_INT_RERROR | HISDIO_FUNC1_INT_MFARM)
```

So the enable mask is `0x01 | 0x02 | 0x04 = 0x07`; the `ACK` bit (`1 << 3`) is **not** in the mask.

These offsets are the *device's* function-1 I/O register file: the host reaches them with
`bus->ops.writeData(bus, <offset>, <len>, ...)`, declared through the portable bus-ops layer
(`oal_sdio_if.h:89-96`, `.../driver/oal/oal_sdio_if.h`):

```c
static inline hi_s32 oal_sdio_memcpy_fromio(struct BusDev *bus, hi_void *dst, hi_u32 addr, hi_s32 count)
...
        ret = bus->ops.readData(bus, addr, count, (hi_u8 *)dst);
```

## 2. The arm sequence (quoted)

The canonical arm is `oal_sdio_dev_init` (`oal_sdio_host.c:1079-1127`, URL
`.../driver/oal/oal_sdio_host.c`):

```c
hi_s32 oal_sdio_dev_init(struct BusDev *bus)                 /* 1079 */
{
    hi_s32 ret;
    hi_u32 data;
    ...
        data = HISDIO_FUNC1_INT_MASK;                        /* 1099 */
        ret = bus->ops.writeData(bus, HISDIO_REG_FUNC1_INT_STATUS, ONE_BYTE, (hi_u8 *)&data);   /* 1100 */
        if (ret) {
            printk("failed to clear sdio interrupt! ret=%d\n", ret);
            goto failed_clear_func1_int;
        }
    ...
    /*
     * enable four interrupt sources in function 1:
     * data ready for host to read
     * read data error
     * message from arm is available
     * device has receive message from host
     *  */                                                     /* 1110-1116 */
    data = HISDIO_FUNC1_INT_MASK;                            /* 1117 */
    ret = bus->ops.writeData(bus, HISDIO_REG_FUNC1_INT_ENABLE, ONE_BYTE, (hi_u8 *)&data);  /* 1118 */
    if (ret < 0) {
        printk("failed to enable sdio interrupt! ret=%d\n", ret);
        goto failed_enable_func1;
    }

    oal_enable_sdio_state(bus, OAL_SDIO_ALL);                /* 1124 */
```

(Quote abridged with `...` only where noted; every line number is exact. The comment at 1110-1116
says "four interrupt sources" but the mask it writes is the three-bit `0x07` - the ACK bit is not
enabled.)

The sequence is therefore:

1. **claim host** (`oal_sdio_claim_host(bus)`, `oal_sdio_host.c:1088`);
2. **clear any stale status first** - write the mask to the *status/clear* register `0x08`
   (`oal_sdio_host.c:1099-1100`);
3. **enable the sources** - write the mask to the *enable* register `0x09`
   (`oal_sdio_host.c:1117-1118`);
4. **open the tx/rx state** - `oal_enable_sdio_state(bus, OAL_SDIO_ALL)` (`oal_sdio_host.c:1124`);
5. **register the host IRQ handler** - outside `oal_sdio_dev_init`:
   `oal_register_sdio_intr` calls `bus->ops.claimIrq(bus, (IrqHandler *)oal_sdio_isr, NULL)` with
   the comment `/* use sdio bus line data1 for sdio data interrupt */` (`oal_sdio_host.c:909-922`).

De-init writes `0` to the same enable register (`oal_sdio_host.c:1142-1147`):

```c
static hi_void oal_sdio_dev_deinit(struct BusDev *bus)       /* 1142 */
{
    bus->ops.claimHost(bus);
    hi_u32 data = 0;
    (void)bus->ops.writeData(bus, HISDIO_REG_FUNC1_INT_ENABLE, ONE_BYTE, (uint8_t *)&data);  /* 1146 */
```

Two re-arm variants exist:

- Linux `sdio_dev_init` writes the enable register **without** the preceding status clear
  (`oal_sdio_host.c:2356-2369`): `/* before enable sdio function 1, clear its interrupt flag, no
  matter it exist or not */` then `data = HISDIO_FUNC1_INT_MASK;` (2365) and the `0x09` write (2366).
- `oal_sdio_transfer_prepare` re-enables the host IRQ: `oal_enable_sdio_state(bus, OAL_SDIO_ALL)`
  then, in the non-GPIO build, `oal_register_sdio_intr(bus)`, else
  `oal_wlan_gpio_intr_enable(bus, HI_TRUE)` (`oal_sdio_host.c:2282-2294`).

There is a **software shadow mask** `hi_sdio->func1_int_mask`, initialised to the same value at
module init (`oal_sdio_host.c:2139`):

```c
    hi_sdio->func1_int_mask = HISDIO_FUNC1_INT_MASK;         /* 2139 */
```

and toggled by the inlines `oal_sdio_func1_int_mask` / `oal_sdio_func1_int_unmask`
(`oal_sdio_host_if.h:289-309`, URL `.../driver/oal/oal_sdio_host_if.h`):

```c
static inline hi_void oal_sdio_func1_int_mask(struct BusDev *bus, hi_u32 func1_int_mask)   /* 289 */
{
    ...
    hi_sdio->func1_int_mask &= ~func1_int_mask;              /* 296 */
    ...
}

static inline hi_void oal_sdio_func1_int_unmask(struct BusDev *bus, hi_u32 func1_int_mask) /* 300 */
{
    ...
    hi_sdio->func1_int_mask |= func1_int_mask;               /* 307 */
```

This shadow is applied to the *status* read, not to the hardware enable register (see section 3).

## 3. How the host knows a device notification is pending (quoted)

The pending signal arrives as the function-1 interrupt; the ISR then tests the **MFARM** bit and
routes it to the message handler. `oal_sdio_do_isr` (`oal_sdio_host.c:761-833`):

```c
hi_s32 oal_sdio_do_isr(struct BusDev *bus)                   /* 761 */
{
    hi_u8 int_mask;
    ...
    ret = oal_sdio_get_func1_int_status(bus, &int_mask);     /* 799 */
    ...
    if (oal_unlikely(0 == (int_mask & HISDIO_FUNC1_INT_MASK))) {
        hi_sdio->func1_stat.func1_no_int_count++;
        return HI_SUCCESS;
    }

    /* clear interrupt mask */
    ret = oal_sdio_clear_int_status(bus, int_mask);          /* 810 */
    ...
    /* message interrupt, flow control */
    if (int_mask & HISDIO_FUNC1_INT_MFARM) {                 /* 821 */
        hi_sdio->func1_stat.func1_msg_int_count++;
        if (oal_sdio_msg_irq(bus) != HI_SUCCESS) {           /* 823 */
            return -OAL_EFAIL;
        }
    }

    if (int_mask & HISDIO_FUNC1_INT_DREADY) {                /* 828 */
        hi_sdio->func1_stat.func1_data_int_count++;
        return oal_sdio_data_sg_irq(bus);
    }
```

Two was to read the status word. With the extend function (`CONFIG_SDIO_FUNC_EXTEND`) the status is
taken from a memory copy of the device's extend register block; otherwise it is read from register
`0x08` and masked by the software shadow. `oal_sdio_get_func1_int_status`
(`oal_sdio_host.c:720-738`):

```c
static hi_s32 oal_sdio_get_func1_int_status(struct BusDev *bus, hi_u8 *int_stat)   /* 720 */
{
    ...
    if (g_sdio_extend_func) {
        hi_sdio->sdio_extend->int_stat &= hi_sdio->func1_int_mask;   /* 725 */
        *int_stat = (hi_sdio->sdio_extend->int_stat & 0xF);          /* 726 */
        return HI_SUCCESS;
    } else {
        /* read interrupt indicator register */
        ret = bus->ops.readData(bus, HISDIO_REG_FUNC1_INT_STATUS, ONE_BYTE, int_stat);  /* 730 */
        ...
        *int_stat = (*int_stat) & hi_sdio->func1_int_mask;           /* 735 */
    }
```

and the clear (`oal_sdio_host.c:740-753`) writes the observed bits back to `0x08`:

```c
static hi_s32 oal_sdio_clear_int_status(struct BusDev *bus, hi_u8 int_stat)   /* 740 */
{
    ...
    ret = bus->ops.writeData(bus, HISDIO_REG_FUNC1_INT_STATUS, ONE_BYTE, &int_stat);   /* 747 */
```

The message handler reads the *message bitmap* and dispatches per set bit. `oal_sdio_msg_irq`
(`oal_sdio_host.c:393-443`):

```c
hi_s32 oal_sdio_msg_irq(struct BusDev *bus)                  /* 393 */
{
    ...
    /* reading interrupt form ARM Gerneral Purpose Register(0x28)  */   /* 400 */
    ret = oal_sdio_msg_stat(bus, &msg);                      /* 401 */
    ...
    if (!msg) {
        return HI_SUCCESS;
    }
    ...
    oal_bit_atomic_for_each_set(bit, (const unsigned long *)&msg_tmp, D2H_MSG_COUNT) {   /* 427 */
        ...
        hi_sdio->msg[bit].count++;
        hi_sdio->last_msg = bit;
        ...
        if (hi_sdio->msg[bit].msg_rx) {
            hi_sdio->msg[bit].msg_rx(hi_sdio->msg[bit].data);   /* 436 */
        }
    }
```

`oal_sdio_msg_stat` (`oal_sdio_host.c:344-381`) shows the two possible sources of that bitmap - the
extend block's `msg_stat` (the normal path) or the legacy register `0x28`:

```c
static hi_s32 oal_sdio_msg_stat(struct BusDev *bus, hi_u32 *msg)   /* 344 */
{
    ...
#else
    if (g_sdio_extend_func) {
        *msg = hi_sdio->sdio_extend->msg_stat;               /* 363 */
    }

    if (*msg == 0) {
        /* no sdio message! */
        return HI_SUCCESS;
    }
#ifdef CONFIG_SDIO_D2H_MSG_ACK
    ...
    ret = bus->ops.readData(bus, HISDIO_REG_FUNC1_MSG_HIGH_FROM_DEV, ONE_BYTE, &buf);  /* 374 */
```

The extend block is a single 64-byte read of device function register offset `0x30`; its layout is
struct `hisdio_extend_func` (`oal_sdio_host_if.h:49-52, 86-99`):

```c
/* 0x30~0x38, 0x3c~7B */
#define HISDIO_EXTEND_BASE_ADDR     0x30                     /* 50 */
#define HISDIO_EXTEND_CREDIT_ADDR   0x3c                     /* 51 */
#define HISDIO_EXTEND_REG_COUNT     64                       /* 52 */
...
typedef struct {                                             /* 93 */
    hi_u32 int_stat;
    hi_u32 msg_stat;
    hi_u32 xfer_count;
    hi_u32 credit_info;
    hi_u8 comm_reg[HISDIO_EXTEND_REG_COUNT];
} hisdio_extend_func;
```

filled by `oal_sdio_extend_buf_get` (`oal_sdio_host.c:502-528`):

```c
static hi_s32 oal_sdio_extend_buf_get(struct BusDev *bus)    /* 502 */
{
    ...
    if (g_sdio_extend_func) {
        ret = oal_sdio_memcpy_fromio(bus, (hi_void *)hi_sdio->sdio_extend,
                                     HISDIO_EXTEND_BASE_ADDR, sizeof(hisdio_extend_func));   /* 507-508 */
```

So the pending-notification word and the interrupt-status word are read **together** in one 64-byte
function-register read at offset `0x30`, then the set bits select the message callbacks.

(The same detection, done by polling instead of by an ISR edge, is in
`plat_sdio.c:96-150`: it spins on `readData(... HISDIO_REG_FUNC1_INT_STATUS ...)` until
`int_mask & HISDIO_FUNC1_INT_MASK`, clears it via a `0x08` write (`plat_sdio.c:118`), then reads the
byte count from `HISDIO_REG_FUNC1_XFER_COUNT` (`0x0c`, `plat_sdio.c:146`) and bulk-reads
`HISDIO_REG_FUNC1_FIFO` (`0x00`, `plat_sdio.c:167`).)

## 4. The host->device direction ("doorbell" analog) (quoted)

`oal_sdio_send_msg` (`oal_sdio_host.c:147-183`) writes the message id as a **bitmap bit** into
`HISDIO_REG_FUNC1_WRITE_MSG` (`0x24`), four bytes wide:

```c
hi_s32 oal_sdio_send_msg(struct BusDev *bus, unsigned long val)   /* 147 */
{
    ...
    if (val >= H2D_MSG_COUNT) {
        oam_error_log1(0, OAM_SF_ANY, "[Error]invalid param[%lu]!\n", val);
        return -OAL_EINVAL;
    }
    ...
    unsigned long data = 1 << val;                           /* 175 */
    ret = bus->ops.writeData(bus, HISDIO_REG_FUNC1_WRITE_MSG, FOUR_BYTE, (hi_u8 *)&data);  /* 176 */
```

The HCC layer reaches this through two thin macros - `oal_bus_message_register` and
`oal_bus_send_msg` (`oal_channel_host_if.h:42,51`, URL `.../driver/oal/oal_channel_host_if.h`):

```c
#define oal_bus_message_register(bus, msg, cb, data)  oal_sdio_message_register(bus, msg, cb, data)  /* 42 */
#define oal_bus_message_unregister(bus, msg)    oal_sdio_message_unregister(bus, msg)                 /* 43 */
...
#define oal_bus_send_msg(bus, val)     oal_sdio_send_msg(bus, val)                                     /* 51 */
```

`hcc_message_register` is a one-line forwarder (`hcc_host.c:1329-1332`), and the HCC init registers
per-id callbacks - e.g. `hcc_message_register(hcc_handler, D2H_MSG_FLOWCTRL_ON, hcc_flow_on_callback,
hcc_handler)` (`hcc_host.c:1821`); the host triggers device commands with
`oal_bus_send_msg(g_hcc_host_handler->bus, H2D_MSG_DEVICE_MEM_INFO)` (`hcc_host.c:147`) and
`H2D_MSG_TEST` (`hcc_host.c:157`). The id namespace (all sharing the one MFARM interrupt) is
`oal_sdio_comm.h:49-75` (D2H, `D2H_MSG_COUNT = 32`) and `oal_sdio_comm.h:78-98` (H2D).

Registration is by id into a 32-entry table (`oal_sdio_host.c:316-325`):

```c
hi_s32 oal_sdio_message_register(struct BusDev *bus, hi_u8 msg, sdio_msg_rx cb, hi_void *data)  /* 316 */
{
    ...
    hi_sdio->msg[msg].msg_rx = cb;                           /* 322 */
    hi_sdio->msg[msg].data = data;                           /* 323 */
```

Example consumers: `oal_bus_message_register(bus, D2H_MSG_WLAN_READY, plat_set_device_ready, ...)`
(`plat_pm_wlan.c:418`) and the PM ids `D2H_MSG_WAKEUP_SUCC`/`D2H_MSG_ALLOW_SLEEP`/
`D2H_MSG_DISALLOW_SLEEP`/`D2H_MSG_HOST_SLEEP_ACK`/`D2H_MSG_BEFORE_DEV_SLEEP`/`D2H_MSG_DEV_WKUP`
(`plat_pm_wlan.c:1319-1327`).

## 5. Which interrupt line, and the second (GPIO) mode

The driver has two host-side interrupt modes (`oal_sdio_host.c:100-105`):

```c
/* 0 -sdio 1-gpio */
#ifdef _PRE_FEATURE_NO_GPIO
hi_s32 g_hisdio_intr_mode = 0;
#else
hi_s32 g_hisdio_intr_mode = 1;
#endif
```

- **SDIO mode** uses the SDIO bus data1 line as the interrupt (`oal_sdio_host.c:914-915`,
  `... /* use sdio bus line data1 for sdio data interrupt */`); registration is
  `bus->ops.claimIrq(bus, (IrqHandler *)oal_sdio_isr, NULL)` (`oal_sdio_host.c:915`).
- **GPIO mode** uses a dedicated board GPIO: `oal_register_gpio_intr(bus)`
  (`oal_sdio_host.c:1231`), enabled/disabled at the host with `oal_wlan_gpio_intr_enable`
  (`oal_sdio_host.c:890-907`):

```c
hi_void oal_wlan_gpio_intr_enable(struct BusDev *bus, hi_u32  ul_en)   /* 890 */
{
    oal_channel_stru *hi_sdio = (oal_channel_stru *)bus->priData.data;
#ifndef _PRE_FEATURE_NO_GPIO
    unsigned long flags;

    oal_spin_lock_irq_save(&hi_sdio->st_irq_lock, &flags);
    if (ul_en) {
        oal_enable_irq(hi_sdio->ul_wlan_irq);                /* 898 */
    } else {
        oal_disable_irq_nosync(hi_sdio->ul_wlan_irq);        /* 900 */
    }
```

The GPIO mux that carries the WiFi data interrupt is named `REG_MUXCTRL_WIFI_DATA_INTR_GPIO_MAP`
(`plat_board_adapt.h:40`, URL `.../driver/oal/plat_board_adapt.h`), alongside the host-wake/device-
wake lines `REG_MUXCTRL_HOST_WAK_DEV_GPIO_MAP` and `REG_MUXCTRL_DEV_WAK_HOST_GPIO_MAP`
(`plat_board_adapt.h:41-42`). Both interrupt modes **still read the pending state from the same
function-1 register file** (section 3) - the "line" only tells the host *that* something happened.

## 6. What this gives the luofu port, and what it does not

The hi3881 answer to "which register enables the mailbox doorbell -> interrupt forwarding" is:
**a single device register, `FUNC1_INT_ENABLE` (`0x09`), armed with the source-class mask `0x07`**;
the doorbell itself is the bitmap write to `WRITE_MSG` (`0x24`). The enable is **not** keyed to a
message id - all 32 ids are delivered under the one `MFARM` ("ARM Msg") interrupt and routed in
software.

Transferable structure (stated as a **resemblance, not an identity** - hi3881 is SDIO with a
function-register file; luofu is PCIe with a memory-mapped message window):

- The luofu block the gate ledger (phases 31-35, 42-44) calls the "message block"
  (`CA 0x40039000-0x40039600`, host-visible as BAR0 `0x3f1000-0x3f1600`) is the structural
  counterpart of the hi3881 function-1 register file: it holds the pending/status words
  (`0x40039010`/`0x40039014`, the out0/out1 pending words), the doorbell (`0x400392d4`) and the ack
  (`0x400392f0`). In the hi3881 file those same roles sit next to an **adjacent status/enable byte
  pair** - `INT_STATUS` `0x08` and `INT_ENABLE` `0x09`.
- The only registers the luofu vendor modules actually reference inside that block are
  `0x40039000` (state, normal-op `0x10B`), `0x40039010`/`0x40039014` (out0/out1), `0x40039224`
  (state halfword) and `0x40039508` (ETE intr mask) - none of them is documented as an
  interrupt-enable. That is exactly the shape the hi3881 comparison predicts is *missing*: the
  enable is a separate register, distinct from the pending/doorbell words.
- **Candidate for a live test (inference, not a quoted fact):** if the luofu block mirrors the SDIO
  function-register layout, the mailbox -> line-`0x4C` forwarding enable sits at the hi3881-analog
  **status/enable byte pair** inside the same host-visible block - i.e. around `CA 0x40039008` /
  `0x40039009` (BAR0 `0x3f1008`/`0x3f1009`) - or in the block's control word `0x40039000`. A
  read-only first step is simply to read `0x40039000..0x4003901c` and `0x40039200..0x40039228`
  (all inside the host-visible window) and look for an `0x08`/`0x09`-shaped status-then-enable pair;
  a later live step would be a read-modify-write of that enable bit (log the pre-value), ring the
  doorbell, and watch for the dispatcher's ack signature `0x400392f0` and line `0x4C`.
- The one *structural* lesson that is safe regardless of the address: **expect a source-class
  enable bit, not a per-message-id enable**, and expect the enable to be a different register from
  the doorbell/ack.

This does **not** supply the luofu register address: the hi3881 tree has no `0x4003xxxx` addresses,
no PCIe mailbox, and no register map for `hi5622v100`. It supplies the *role* of the register to
look for and the arm order (clear-status -> write-enable -> open-state -> open-line).

## 7. Sources, checked paths, and negatives

**Fetched and quoted (all 200 OK; full URLs are the ones in the table at the top; equivalent paths
under `https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/common/platform/wifi/hi3881v100/`):**

| path | lines quoted |
| --- | --- |
| `driver/oal/oal_sdio.h` | 38-55 |
| `driver/oal/oal_sdio_host.c` | 147-183, 316-325, 344-381, 393-443, 502-528, 720-753, 761-833, 890-907, 909-929, 1079-1127, 1142-1147, 1223-1248, 2139, 2282-2294, 2344-2375 |
| `driver/oal/oal_sdio_host_if.h` | 49-52, 86-99, 289-309 |
| `driver/oal/oal_sdio_if.h` | 89-96 |
| `driver/oal/oal_sdio_comm.h` | 42, 49-75, 78-98 |
| `driver/oal/oal_channel_host_if.h` | 42-51 |
| `driver/oal/plat_sdio.c` | 96-150, 167 |
| `driver/oal/plat_pm_wlan.c` | 418, 1319-1327 |
| `driver/oal/plat_board_adapt.h` | 40-46 |
| `driver/hcc/hcc_host.c` | 147, 157, 1329-1332, 1821 |

**Checked and negative:**

- Repo tree: `https://api.github.com/repos/openharmony/device_soc_hisilicon/git/trees/master?recursive=1`
  (200; 18,837 entries, not truncated). Under `common/platform/wifi/hi3881v100/` there are **240
  `.c`/`.h` files**; **all 240 were downloaded** and grepped.
- Grep of all 240 for `mailbox`, `doorbell`, `int_enable`, `intr_enable`, `irq_enable`,
  `sdio_int`, `HISDIO_REG_FUNC1_INT`, `msg_en`: **zero "mailbox"/"doorbell" hits**; the only
  message-path interrupt-enable register is `HISDIO_REG_FUNC1_INT_ENABLE` (`0x09`) as quoted; the
  only other `*irq_enable*` hits are the LiteOS CPU-HWI helper `hi_irq_enable`
  (`driver/oal/hi_isr.c:125`, `driver/include/hi_isr.h:134`) and the DMA error code
  `HI_ERR_DMA_CH_IRQ_ENABLE_FAIL` (`driver/include/hi_errno.h:307`) - neither is a mailbox/message
  interrupt enable.
- No file in the tree contains a `0x4003xxxx` device register address (grep for `0x4003` over all
  240 files: 0 hits) or a PCIe register map; the device-side register names are absent by
  construction (the hi3881 driver is an SDIO client).

**Method:** the repo tree was listed via the GitHub trees API; each cited file was fetched with
`curl`-equivalent `fetch` from `raw.githubusercontent.com` and stored locally; line numbers and text
were read from those fetched bytes (no paraphrasing in the quote blocks).
