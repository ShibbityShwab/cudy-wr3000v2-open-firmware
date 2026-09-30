# Hazards found the hard way (all reproduced on this device)

These are operational rules for anyone working on a WR3000 V2.0 with this research toolkit. Every one of
them cost a reboot or a failed write during the original work.

## 1. `rmmod wifi_debug` panics and reboots the router

The vendor ships `/lib/hisilicon/ko/wifi_debug.ko` for its register-dump facility. Unloading it with
`rmmod` produces a kernel panic immediately; the pstore records show the module list with
`wifi_debug(O-)` (the `O-` meaning it is in the unload path) and errors like
`Internal error: Oops: 80000007 [#1] SMP ARM` followed by `Panic#2`.

**Rule:** to remove the module, reboot the router. Do not `rmmod` it.

## 2. Only read mapped windows through `/dev/mem`

Reading the BARs from userspace works, but only at the right offset, and the wrong address can fault the
bus:

- The register I/O window is BAR0's `SHUANGTA_REGION_IO`, which lives at host address `0x403b8000` for
  endpoint 0. So `resource0_offset = 0x3b8000 + (CA - 0x40000000)`.
- Offset `0` of BAR0 is **not** registers; it reads as the chip's firmware ROM (`0xE59FF018`, an ARM
  instruction).
- Confirmed safe and byte-exact against the register dump: `devmem 0x403b8000`, `0x403b8004`,
  `0x403b8008` return `0x101`, `0x110`, `0x2`, matching `dumps/reg_all.txt`.

**Rule:** read only addresses that appear in the register dump, prefer the static ones, and never write.

## 3. Some registers are live even when the rest of their window is static

One word in the register window (`CA 0x40040004`) changed between two dumps minutes apart
(`0x83FD7000` to `0x84A67000`). Treat any address that moved between the two published dumps as
volatile.

## 4. Flashing rules still apply to everything else

The slot and recovery rules from `FLASH-PLAN.md` are unchanged and non-negotiable: full dumps before any
write, one slot always stock, and the slot switch only through the env blocks plus the boot register.
