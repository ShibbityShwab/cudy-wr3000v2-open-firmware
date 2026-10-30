# The vendor Wi-Fi driver, decompiled — bring-up chain

**Source:** `hi5622v100_wifi.ko` (3,581,748 B), Ghidra 12.2 headless with `tools/ghidra/DumpFuncs.java`.
**Full dump:** 4,838 functions, 8.1 MB — generated into `C:/tmp/wifi.txt` (regenerate with `tools/ghidra/README.md`).
**The module is NOT stripped:** 4,247 named functions (`hal_*`, `hdpp_*`, `shuangta_*`, `hmac_*`, `wal_*`, `oal_*`),
so the decompilation is readable and the bring-up sequence can be followed by name rather than by address.

## Why this matters for `luofu-wifi`

`opensource/lab/luofu-wifi` is a deliberate first rung: it probes, identifies the chip, and reads the MAC
cfg-power table — read-only, no power sequencing, no firmware download, no data path. This dump is the
spec for every rung above that, and it is named, not guessed.

## The bring-up chain

```
init_module
  -> hal_main_init            (single call: hal_board_init)
       -> hal_board_init      memset g_pst_hal_board; oal_bus_get_chip_num(); for each chip:
            -> hal_chip_init  get_chip_type() -> hal_device_init() -> shuangta_host_chip_irq_init()
                 -> hal_device_init      -> hal_device_init_mac_and_phy
                      -> shuangta_host_initialize_machw   MAC registers (cfg_power0_ref table)
                      -> shuangta_host_initialize_phy      PHY register base + init
            -> hal_ring_init  TX/RX descriptor rings
```

## Named API families (counts from the symbol table)

| prefix | meaning |
|---|---|
| `shuangta_*` (147) | the MAC/PHY hardware layer: machw/phy init, interrupts, TX/RX rings, spectral |
| `hal_*` | chip/device/board/ring init above the MAC layer |
| `hdpp_*` | device power/protection/platform init |
| `hmac_*` | high MAC: VAP, rate, BA, TXBF, RRM |
| `wal_*` | wireless abstraction layer (config, station, scan) |
| `oal_*` | OS abstraction: bus, memory, timers, locks |
| `oam_*` | logging |

## Key functions, decompiled

### `hal_main_init`

```c
int hal_main_init(void)

{
  int iVar1;
  
  iVar1 = hal_board_init();
  if (iVar1 != 0) {
    printk(&_LC0,iVar1);
  }
  return iVar1;
}
```

### `hal_board_init`

```c
int hal_board_init(void)

{
  uint uVar1;
  int iVar2;
  
  memset_s(g_pst_hal_board,0x374,0);
  uVar1 = oal_bus_get_chip_num();
  if (1 < uVar1) {
    oam_error_log1(0,0x75,3,0x3b,"ul_chip_max_num failed: =%d.",uVar1);
    return 0x65;
  }
  oam_info_log1(0,0x75,3,0x3f," Support max chip num: %d.\n");
  if (uVar1 != 0) {
    iVar2 = hal_chip_init(g_pst_hal_board + 8,0);
    if (iVar2 != 0) {
      oam_error_log2(0,0x75,3,0x43,"hal_chip_init failed: uc_chip=%d, l_ret=%d.",0,iVar2);
      return iVar2;
    }
    oal_bit_set_bit_one_byte(g_pst_hal_board,0);
  }
  return 0;
}
```

### `hal_chip_init`

```c
int hal_chip_init(undefined2 *param_1,uint param_2)

{
  int iVar1;
  char *pcVar2;
  undefined4 uVar3;
  byte bVar4;
  byte bVar5;
  undefined1 local_1d;
  int local_1c;
  
  local_1d = 0;
  local_1c = *DAT_0004c10c;
  if (param_1 == (undefined2 *)0x0 || 1 < param_2) {
    pcVar2 = "hal_chip_init::null param or chip_max_num failed: =%d.";
    uVar3 = 0x5b;
  }
  else {
    iVar1 = get_chip_type(param_2,&local_1d,0);
    if (iVar1 == 0) {
      *(undefined1 *)((int)param_1 + 1) = local_1d;
      *(undefined1 *)(param_1 + 3) = 2;
      *(char *)param_1 = (char)param_2;
      *(undefined1 *)(param_1 + 1) = 1;
      bVar5 = 0;
      do {
        iVar1 = hal_device_init(param_1,bVar5);
        if (iVar1 != 0) {
          oam_error_log2(0,0x75,0x5c0,0x6f,"hal_device_init failed: uc_device=%d, l_ret=%d.",bVar5,
                         iVar1);
          goto LAB_0004c08c;
        }
        bVar4 = bVar5 + 1;
        oal_bit_set_bit_one_byte((int)param_1 + 7,bVar5);
        bVar5 = bVar4;
      } while (bVar4 < *(byte *)(param_1 + 3));
      param_1[0x1b4] = *param_1;
      shuangta_host_chip_irq_init();
      goto LAB_0004c08c;
    }
    pcVar2 = "hal_chip_init::get_chip_type failed: =%d.";
    uVar3 = 0x60;
  }
  iVar1 = 0x65;
  oam_error_log1(0,0x75,0x5c0,uVar3,pcVar2,param_2);
LAB_0004c08c:
  if (local_1c != *DAT_0004c10c) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return iVar1;
}
```

### `hal_device_init`

```c
undefined1 * hal_device_init(undefined2 *param_1,uint param_2)

{
  char cVar1;
  undefined1 *puVar2;
  int iVar3;
  byte bVar4;
  undefined2 *puVar5;
  
  if (1 < param_2) {
    oam_error_log1(0,0x75,0x5df,0xd2,"hal_device_init failed.uc_dev_id=%d.",param_2);
    return (undefined1 *)0x65;
  }
  cVar1 = *(char *)((int)param_1 + 1);
  puVar5 = param_1 + param_2 * 0xd8 + 4;
  param_1[param_2 * 0xd8 + 4] = *param_1;
  *(byte *)(param_1 + param_2 * 0xd8 + 5) = (byte)param_2;
  if (puVar5 == (undefined2 *)0x0) {
    oam_error_log0(0,0x75,0x5df,0xba,"hal_device_band_type_init: param is null.");
    bVar4 = 2;
  }
  else {
    bVar4 = ((byte)param_2 ^ 1) & 1;
    if (cVar1 != '\0') {
      bVar4 = 1;
    }
    bVar4 = bVar4 ^ 1;
  }
  *(byte *)(param_1 + param_2 * 0xd8 + 0x4a) = bVar4;
  memset_s(param_1 + param_2 * 0xd8 + 0xae,0xc,0);
  memset_s(param_1 + param_2 * 0xd8 + 0xd0,0x10,0);
  *(undefined1 *)((int)param_1 + param_2 * 0x1b0 + 0x95) = 1;
  *(undefined1 *)(param_1 + param_2 * 0xd8 + 0xce) = 0;
  puVar2 = (undefined1 *)hal_device_init_mac_and_phy(puVar5);
  if (puVar2 == (undefined1 *)0x0) {
    *(undefined1 *)(param_1 + param_2 * 0xd8 + 0xc6) = 0;
    *(undefined4 *)(param_1 + param_2 * 0xd8 + 0xc4) = 0;
    *(undefined4 *)(param_1 + param_2 * 0xd8 + 0xba) = 0;
    *(undefined4 *)(param_1 + param_2 * 0xd8 + 0xb8) = 0;
    *(undefined4 *)(param_1 + param_2 * 0xd8 + 0xc2) = 0;
    *(undefined4 *)(param_1 + param_2 * 0xd8 + 0xc0) = 0;
    *(undefined2 **)(param_1 + param_2 * 0xd8 + 0xb4) = param_1 + param_2 * 0xd8 + 0xb4;
    *(unde
```

### `hal_device_init_mac_and_phy`

```c
int hal_device_init_mac_and_phy(undefined4 param_1)

{
  int iVar1;
  
  iVar1 = shuangta_host_initialize_machw();
  if (iVar1 != 0) {
    oam_error_log1(0,0x75,0x5df,0x95,"{hal_device_init_mac_and_phy:: return code[%d].}",iVar1);
    return iVar1;
  }
  iVar1 = shuangta_host_initialize_phy(param_1);
  if (iVar1 == 0) {
    return 0;
  }
  oam_error_log1(0,0x75,0x5df,0x9b,
                 "hal_device_init_mac_and_phy:: hal_host_initialize_phy failed.l_ret=%d.",iVar1);
  return iVar1;
}
```

### `hal_ring_init`

```c
undefined1 * hal_ring_init(int param_1)

{
  if ((param_1 == 0) || (*(int *)(param_1 + 0xc) == 0)) {
    return &DAT_00008b2d;
  }
  if (*(int **)(param_1 + 0x1c) != (int *)0x0) {
    **(int **)(param_1 + 0x1c) = *(int *)(param_1 + 0xc) + -0x3fa00000;
  }
  if (*(uint **)(param_1 + 0x18) == (uint *)0x0) {
    return (undefined1 *)0x0;
  }
  **(uint **)(param_1 + 0x18) = (uint)*(ushort *)(param_1 + 4);
  return (undefined1 *)0x0;
}
```

### `shuangta_host_chip_irq_init`

```c
void shuangta_host_chip_irq_init(byte *param_1)

{
  int *piVar1;
  int iVar2;
  int iVar3;
  uint uVar4;
  int iVar5;
  
  if (param_1 == (byte *)0x0) {
    printk(&_LC52);
    return;
  }
  if (*param_1 != 0) {
    printk(&_LC53,*param_1);
    return;
  }
  memset_s(&_LANCHOR0,0x10,0);
  iVar5 = (uint)*param_1 * 0x36c;
  _LANCHOR0 = shuangta_irq_host_all;
  DAT_0028098c = shuangta_irq_host_all;
  DAT_00280988 = g_pst_hal_board + iVar5 + 0x10;
  DAT_00280990 = g_pst_hal_board + iVar5 + 0x1c0;
  iVar5 = g_pst_hal_board + iVar5;
  pcie_wlan_func_register(*param_1,&_LANCHOR0);
  if (*(char *)(iVar5 + 0xe) != '\0') {
    uVar4 = 0;
    iVar2 = iVar5;
    do {
      piVar1 = (int *)(iVar2 + 0x15c);
      uVar4 = uVar4 + 1;
      if (*piVar1 == 0) {
LAB_0004a7c0:
        printk(&_LC55);
        return;
      }
      iVar3 = *(int *)(iVar2 + 0x160);
      iVar2 = iVar2 + 0x1b0;
      if (iVar3 == 0) goto LAB_0004a7c0;
      if ((*(int *)(*piVar1 + 0x14) == 0) || (*(int *)(iVar3 + 0x1c) == 0)) {
        printk(&_LC56);
        return;
      }
      hal_set_host_intr_mask_cfg_host_intr_mask(0xfffffcc1);
      hal_set_cfg_intr_mask_host_cfg_intr_mask_host(0x1f,*(undefined4 *)(iVar3 + 0x1c));
    } while (uVar4 < *(byte *)(iVar5 + 0xe));
  }
  printk(&_LC54,*param_1);
  return;
}
```

### `shuangta_host_initialize_machw`

```c
int shuangta_host_initialize_machw(undefined1 *param_1)

{
  char cVar1;
  int iVar2;
  undefined4 local_74;
  undefined4 uStack_70;
  undefined4 uStack_6c;
  undefined4 uStack_68;
  undefined4 local_64;
  undefined4 uStack_60;
  undefined4 uStack_5c;
  undefined4 uStack_58;
  undefined4 local_54;
  undefined4 uStack_50;
  undefined4 local_4c;
  undefined4 uStack_48;
  undefined4 uStack_44;
  undefined4 uStack_40;
  undefined4 local_3c;
  undefined4 uStack_38;
  undefined4 uStack_34;
  undefined4 uStack_30;
  undefined4 local_2c;
  undefined4 uStack_28;
  int local_24;
  
  local_24 = *DAT_00044ac8;
  if (param_1 == (undefined1 *)0x0) {
    iVar2 = -0xe;
    oam_error_log0(0,0x75,0x6b3,0x1c6,"{host_initialize_machw:: NULL PTR.}");
  }
  else {
    iVar2 = oal_mem_alloc(0x6b3,0x1c9,3,0x28,1);
    *(int *)(param_1 + 0x14c) = iVar2;
    if (iVar2 == 0) {
      iVar2 = -0xc;
      oam_error_log0(0,0x75,0x6b3,0x1cc,"{host_initialize_machw:: NO MEM!.}");
    }
    else {
      cVar1 = param_1[2];
      local_74 = *DAT_00044acc;
      uStack_70 = DAT_00044acc[1];
      uStack_6c = DAT_00044acc[2];
      uStack_68 = DAT_00044acc[3];
      local_64 = DAT_00044acc[4];
      uStack_60 = DAT_00044acc[5];
      uStack_5c = DAT_00044acc[6];
      uStack_58 = DAT_00044acc[7];
      local_54 = DAT_00044acc[8];
      uStack_50 = DAT_00044acc[9];
      local_4c = DAT_00044acc[10];
      uStack_48 = DAT_00044acc[0xb];
      uStack_44 = DAT_00044acc[0xc];
      uStack_40 = DAT_00044acc[0xd];
      local_3c = DAT_00044acc[0xe];
      uStac
```

### `shuangta_host_initialize_phy`

```c
int shuangta_host_initialize_phy(undefined1 *param_1)

{
  int iVar1;
  
  if (param_1 == (undefined1 *)0x0) {
    oam_error_log0(0,0x75,0x6b6,0xe7,"{host_initialize_phy:: pst_hal_device NULL.}");
    return -0xe;
  }
  iVar1 = oal_mem_alloc(0x6b6,0xea,3,0x40,1);
  *(int *)(param_1 + 0x150) = iVar1;
  if (iVar1 == 0) {
    iVar1 = -0xc;
    oam_error_log0(0,0x75,0x6b6,0xed,"{host_initialize_phy:: phy_reg_base NULL.}");
  }
  else {
    iVar1 = FUN_0004aa58(iVar1,*param_1,param_1[2]);
    if (iVar1 != 0) {
      printk(&_LC13,iVar1);
      oam_error_log1(0,0x75,0x6b6,0xf6,"{host_initialize_phy:: return code[%d].}",iVar1);
      oal_mem_free(0x6b6,0xf7,*(undefined4 *)(param_1 + 0x150),1);
      *(undefined4 *)(param_1 + 0x150) = 0;
    }
  }
  return iVar1;
}
```

### `multi_chip_loading`

```c
/* WARNING: Control flow encountered bad instruction data */

void multi_chip_loading(void)

{
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}
```
