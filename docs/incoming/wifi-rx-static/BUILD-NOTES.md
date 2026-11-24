# BUILD-NOTES - wifi-rx-static (task-137)

Host-only lane.  Everything below was executed in this lane; no bench/device
contact, no image build, no vendor bytes anywhere in the tracked files.

## Base tree

The patch is generated against the **final combined clean-room driver source
tree** (the wifi-stability lane's final combined module source, itself
base wifi-dp-air + ap-start v7 + dp-tuning TXQ publish + image-polish defaults +
scan-tuning R4 + stability hardening):

* identity: unstripped combined module 990,760 B sha256 `64ece213...`,
  stripped artifact 269,688 B sha256 `b5dfb883`, opensource HEAD `0bc0e24`;
* local path used by this lane: `/tmp/wifi-stability-build/combined`
  (gitignored bench artifact tree, not published).

The patch changes exactly six files (4 edits + 2 new files) and is verified to
apply with `patch -p1` and to reproduce the lane's `src/` files byte-for-byte.

## Host selftest

```
cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I<src> \
   tools/rx_selftest.c <src>/hi5622_rx_live.c -o /tmp/rx_selftest
/tmp/rx_selftest    # -> 51 checks, 0 failures
```

Covered: literal vendor decode anchors (live-bank CA/offsets, config words,
36-byte header base, payload length/chain offsets), ring pending/consumed
arithmetic incl. phase wrap / one-lap / invalid cases, packed index
advance/publish, header parse bounds and chain refusal, completion-entry pool
classification, status bit decode, and a synthetic 256-published / 32-consumed
/ one-captured-frame end-to-end case.

## Kernel module build (compile verification)

```
export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
export CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -C $KDIR M=<staged tree> modules
```

Result: **RC=0, 0 warnings** (`hi5622_rx_live.o` added to the Kbuild object
list and linked into `hi5622_wifi.ko`).  `modinfo` carries the new parameters
`rx_enable`, `rx_clear_status`, `rx_drain_max`; vermagic
`6.18.60 SMP mod_unload ARMv7 p2v8`.

Release hashes of this lane's build:

| artifact | size | sha256 |
| --- | --- | --- |
| unstripped `hi5622_wifi.ko` | 1,021,976 B | `3649f589e3a428575e6ee4a917a823b7b3ce434add0988ed71bda5f2b7830e12` |
| stripped `hi5622_wifi.ko` (`--strip-debug`) | 280,736 B | `5b43d0a2ca526f84fabd744377213024c9dde15ce22a3c18e826180944d1f7be` |

Local artifact paths for the Lead (gitignored, not published):
`/tmp/wifi-rx-lane-build/hi5622_wifi.ko` (unstripped, build tree) and
`/tmp/wifi-rx-lane-artifact/hi5622_wifi.ko` (stripped, insmod candidate).

## What this lane did NOT do

* no bench fire, no boot, no insmod on the device, no image integration;
* no IRQ enable (the RX path is poll driven; `cfg_intr_mask_host` bank is [U]);
* no sub-MSDU chain consume (refused), no mac80211/msdu delivery;
* no writes to the live bank, none outside MAC band 0, none to the RC misc
  window, none to CA 0x400392f0.
