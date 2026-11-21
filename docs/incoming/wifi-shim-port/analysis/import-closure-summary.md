# M1 import closure - machine summary

- plat.ko GLOBAL UND: 126   wifi.ko GLOBAL UND: 308   distinct: 367
- 6.18 vmlinux ksymtab export pool (parsed from __ksymtab/__ksymtab_gpl): 7539 names
- vendor module pool: 256 .ko files
- vendor 5.10 wireless surface (cfg80211+mac80211 ksymtab): 260

## Combined classification (both modules)

- A_KERNEL: 148
- A_KERNEL_RISK: 26
- B_VENDOR_MODULE: 177
- C_WIRELESS_SHIM: 0
- D_VENDOR_STUB: 1
- E_OTHER_SHIM: 15

## plat.ko

- A_KERNEL: 96
- A_KERNEL_RISK: 8
- B_VENDOR_MODULE: 9
- C_WIRELESS_SHIM: 0
- D_VENDOR_STUB: 1
- E_OTHER_SHIM: 12

## wifi.ko

- A_KERNEL: 100
- A_KERNEL_RISK: 24
- B_VENDOR_MODULE: 175
- C_WIRELESS_SHIM: 0
- D_VENDOR_STUB: 0
- E_OTHER_SHIM: 9

## vendor stack modules' own UNDs vs 6.18

- A_KERNEL: 301
- A_KERNEL_RISK: 72
- B_VENDOR_MODULE: 96
- C_WIRELESS_SHIM: 0
- D_VENDOR_STUB: 0
- E_OTHER_SHIM: 99

## B_VENDOR_MODULE provider breakdown (both modules)

- hi5622v100_plat.ko: 138  (plat 0 / wifi 138)
- cfg80211.ko: 26  (plat 0 / wifi 26)
- ksecurec.ko: 11  (plat 8 / wifi 9)
- hi_kpie.ko: 2  (plat 1 / wifi 2)

## closure (depth <= 3)

- depth 0: hi5622v100_plat.ko
- depth 0: hi5622v100_wifi.ko
- depth 1: cfg80211.ko
- depth 1: hi_kpie.ko
- depth 1: ksecurec.ko
- depth 2: compat.ko
- depth 2: hi_basic.ko

## closure edges (dependent -> provider, symbol counts)

- cfg80211.ko -> compat.ko: 1 symbols
- hi5622v100_plat.ko -> hi_kpie.ko: 1 symbols
- hi5622v100_plat.ko -> ksecurec.ko: 8 symbols
- hi5622v100_wifi.ko -> cfg80211.ko: 26 symbols
- hi5622v100_wifi.ko -> hi5622v100_plat.ko: 138 symbols
- hi5622v100_wifi.ko -> hi_kpie.ko: 2 symbols
- hi5622v100_wifi.ko -> ksecurec.ko: 9 symbols
- hi_basic.ko -> ksecurec.ko: 4 symbols
- hi_kpie.ko -> hi_basic.ko: 3 symbols
- hi_kpie.ko -> ksecurec.ko: 4 symbols
