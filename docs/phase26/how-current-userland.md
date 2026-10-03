# How current can the userland go? A live answer (phase 26a, 2026-10-03)

The objective asks for "a flashable custom OpenWrt build ... as current as the hardware allows". The
blocking question was what "as current as the hardware allows" *means* for a board with no upstream
target. This is a live measurement of it.

## The finding that reframes the question

The vendor firmware is **not a bespoke Linux** - it is **OpenWrt 22.03.6**, read live from the device:

```
DISTRIB_ID='OpenWrt'            DISTRIB_TARGET='hisilicon/luofu'
DISTRIB_RELEASE='22.03.6'       DISTRIB_ARCH='arm_cortex-a9'
DISTRIB_REVISION='2.5.24'       DISTRIB_DESCRIPTION='OpenWrt 22.03.6 r20265-f85a79bcb4'
/lib/ld-musl-arm.so.1 -> libc.so        kernel 5.10.201 armv7l
```

and its package manager points at **upstream OpenWrt feeds for the standard architecture**:

```
src/gz openwrt_base https://downloads.openwrt.org/releases/22.03.6/packages/arm_cortex-a9/base
src/gz openwrt_packages .../22.03.6/packages/arm_cortex-a9/packages      (433 packages installed)
```

So the vendor built a **private OpenWrt target** (`hisilicon/luofu`) on top of stock OpenWrt 22.03.6, and
their userland is **stock upstream `arm_cortex-a9` musl** packages. The board is not exotic to the
userland; it is exotic only to the *kernel*.

## The live test: does a NEWER OpenWrt userland run on the vendor kernel?

**Yes - 24.10.5 binaries run, verified end to end.**

A `libc`-only package (`aggregate 1.6-r3`) was fetched from
`downloads.openwrt.org/releases/24.10.5/packages/arm_cortex-a9/packages/`, extracted, and put on the
device. Its ELF matches the board exactly:

```
ELF 32-bit LSB executable, ARM, EABI5, dynamically linked
interpreter /lib/ld-musl-arm.so.1        <- identical to the device's
NEEDED libgcc_s.so.1, libc.so            <- both present on the device
Flags 0x5000200, Version5 EABI, soft-float ABI  <- the vendor's ABI is soft-float too
```

Transferred (md5 `4dbf9fd7da02f70d85d16e940b942e03` **verified identical** on both ends) and executed:

```
$ printf "10.0.0.0/24\n10.0.1.0/24\n192.168.1.0/24\n" | /tmp/aggregate-2410 -q
10.0.0.0/23
192.168.1.0/24
exit=0
```

**Correct output, on the vendor's 5.10.201 kernel, from a 24.10.5 userland binary.** So the userland is
**not** pinned to 22.03.6 by the kernel.

## What this means for the deliverable

| question | answer, with evidence |
| --- | --- |
| is there upstream target support? | **no** - re-verified 2026-10-03: no OpenWrt target/subtarget/profile for Hi5671, and no `luofu`/`hsan` anywhere; no public Hi5622 driver |
| what does the vendor run? | **OpenWrt 22.03.6**, private target `hisilicon/luofu`, stock `arm_cortex-a9` musl userland |
| how current can the **userland** go? | **24.10.5 demonstrated running** on the vendor kernel; 25.12.5 speaks a different package container (apk v3, custom - no standard compression inside) but is the same armv7 musl ABI |
| how current can the **kernel** go? | unknown and **blocked**: the kernel is the vendor's 5.10.201 and the target patches are not public (the GPL request) |
| can we build a flashable image today? | **yes, the rootfs half** - the vendor kernel stays, the userland is replaceable; that is what the deployed `omo-minimal-0.3` does, but built from a patched vendor rootfs rather than assembled from upstream packages |

So the honest shape of "a custom, current OpenWrt firmware for this board" is:

> **Upstream userland + the vendor's kernel and Wi-Fi modules, flashed as a rootfs.**

with a **24.10 userland demonstrated** and the flashing path already de-risked
(`docs/FLASH-PLAN.md`: A/B slots, one slot always stock, `ubiformat` to the inactive slot, verify the
volume hash before switching).

## Corrections this makes to the existing plan

`CUSTOM-FIRMWARE-PLAN.md` section 3 says "the only *self-built* images possible today are (a) a custom
rootfs on the vendor kernel ... or (b) modules built against a vanilla 5.10.201". **That understates (a).**
It is not merely "a rootfs"; it is the whole userland, and it can be **newer than the vendor's** - measured,
not assumed.

The same file's section 2 conclusion ("no fork to join; the machine has to be brought up") stands for the
**kernel**, and is now independently re-confirmed, but it does **not** apply to the userland.

## Method note

The transfer path had to be rebuilt: `/usr/bin/scp` on the device is a **symlink to dropbear**, and its
scp emulation **acknowledges a file and exits 0 without writing it** - which is why three scp attempts
"succeeded" and left nothing. The device also has **no `base64`**; `openssl base64 -d -A` is the
working decoder. Transfers were then done as base64 chunks over the ssh command lane with an md5 check on
both ends. **Use md5 verification on every transfer to this box** - a silently-dropped file looks exactly
like a successful one.

