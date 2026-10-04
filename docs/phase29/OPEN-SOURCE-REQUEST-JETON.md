# Open-source request: Jeton AX3000 Core (second obligor for the same luofu_V200 SDK)

Ready-to-send. This is a REQUEST, not a result. The evidence that Jeton distributes the same
HiSilicon `luofu_V200` SDK binaries as the Cudy WR3000 v2.0 is in
`docs/phase29/jeton-shared-sdk-proven.md` (firmware downloaded, unpacked, and diffed).

## Why Jeton is a valid obligor

Jeton publishes and distributes firmware for the AX3000 Core
(`https://www.jetontechno.com/product/ax3000-core/`), which contains:

- a **Linux 5.10.201 kernel** (`Linux-5.10.201`, ARM, for the `hisilicon/luofu` target),
- **U-Boot**, **BusyBox**, **opkg** and the other GPL packages of the OpenWrt 22.03.6 base,
- identified by the device's own `/etc/hi_version` as `chip name: luofu_V200`, product
  `wrt_hg5013_4g_h2`, product version `ChenTang_1.2.16.linux0`.

All of these are distributed in binary form in the published image
`Jeton-AX3000-Core_V3.1.00-260924_JETONTECHver.zip`. Under **GPLv2 sections 3(a)/3(b)**, the
complete corresponding source for those GPL-licensed components must be made available.

## Send to

**`support@jeton-tech.ru`** (listed on the product page).

## Subject line

`GPL source code request - AX3000 Core V3.1.00-260924 (Linux 5.10.201, hisilicon/luofu, luofu_V200)`

## Body

```
Hello,

I own a Jeton AX3000 Core running your firmware
Jeton-AX3000-Core_V3.1.00-260924_JETONTECHver (OpenWrt 22.03.6-r23439, target
hisilicon/luofu, chip luofu_V200, product wrt_hg5013_4g_h2, product version
ChenTang_1.2.16.linux0).

That firmware distributes GPL-licensed software in binary form: the Linux kernel 5.10.201,
U-Boot, BusyBox, opkg and the other GPL packages of the OpenWrt base.

Under GPLv2 sections 3(a)/3(b), I request the complete corresponding source code for those
GPL-licensed components as you distribute them, including:

1. the Linux kernel sources with the luofu/hi5671y/hsan platform modifications and the build
   configuration (defconfig, device trees);
2. the U-Boot sources for this board;
3. the source or build recipes for the other GPL packages in the image.

The vendor SDK flow identifiers for this platform are hisi_trunk / opal22, chip luofu_V200.
Please provide a download link, or a written offer valid for at least three years.

Thank you.
```

## What to do with the reply

- **A tarball/link** -> extract it, and diff the kernel tree and driver sources against
  `dumps/` and the module md5s on record. This unblocks the from-source firmware path (the
  project's G004 goal) and the reversal's device-side questions.
- **No reply within ~3 weeks** -> escalate: a formal follow-up, then the seller/importer (the
  obligation attaches to whoever distributed the device to the buyer), then a GPL compliance
  organisation with the documented unanswered requests.
