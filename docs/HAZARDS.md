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

## 5. Never replace /etc/rc.d/luofu wholesale (2026-11-15)

A shadow that replaced the vendor script with `exit 0` took the box off the network: the
script's first line is `insmod hi_kpinctrl`, and without the pinmux there is no MDIO, no
Ethernet, and no Wi-Fi. Any shadow must be a filtered copy of the vendor script that
keeps the pinctrl and the switch and drops only the modules our drivers own. Run
`tools/check-init-shadow.sh` before every fire.

## 6. The initramfs /vendor mount can be read-only (2026-11-15)

On some strap states `/dev/mtdblock15` opens read-only, so the init's `wfile()` writes
to `/vendor/upper` are silent. The same partition is writable from userspace through
`/overlay`. Mount jffs2 with the `rw` option and check the result; remount `rw` if the
superblock is still read-only. Tag the mount errno into a cell before writing.

## 7. Vendor modules need .plt sections and near allocation (2026-11-14)

Every vendor module lacks `.plt` and `.init.plt` because the vendor kernel had
`CONFIG_ARM_MODULE_PLTS=n`. Add zero-size sections with `objcopy` and keep
`module_alloc` in the near region. Do not patch the loader to tolerate the missing
sections; that broke the boot 5/5 while the controls booted 3/3.

## 8. An unowned sysctrl cell is not safe (2026-11-01)

"No driver writes it" says nothing about what the address does in hardware. Three
marks on unowned cells stranded the box. Read only cells whose last writer is known.

## 9. The forbidden register list

Never write CA `0x400392f0`. Never read the RC misc window `0x10161000`; a read-only
`devmem` there panicked the box. Never `rmmod` the vendor modules, including
`wifi_debug.ko`.
