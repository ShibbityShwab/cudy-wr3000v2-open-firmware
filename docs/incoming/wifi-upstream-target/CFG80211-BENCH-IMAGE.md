# cfg80211 bench kernel - rebuild / deploy / flash steps

Handoff from the wifi-cfg80211 lane (task-104) executed by the upstream-target lane
(task-107), 2026-11-24, host-only. Purpose: put cfg80211 into the running bench image
so the clean-room hi5622_wifi.ko (depends=cfg80211) can be bench-loaded.

Status of this change: **bench-only kernel-config addition. Nothing has been flashed
or booted by this lane.** The kernel build and the image build are host-verified; the
bench steps below are for the Lead to run attended with the recovery door armed.

## 1. What changed

- target/linux/hisilicon/luofu/config-6.18 gained the cfg80211 delta from
  ../../wifi-cfg80211/cfg80211.config: CONFIG_CFG80211=m (plus CRDA/PS/debugfs-free
  defaults; signed-regdb enforcement stays at its default y and the asymmetric-key
  chain it selects is part of the delta). WIRELESS/RFKILL/WLAN already come from
  target/linux/generic/config-6.18.
- The file is a DERIVED OpenWrt kernel delta; this session regenerated it once with
  'yes "" | make kernel_oldconfig' and re-derived it from a fresh .config to prove the
  merge is prompt-free (see BUILD-VERIFICATION.md).
- Kernel build evidence: cfg80211.ko 2,328,548 B
  sha256 949393f9676cc7bab18def92f1f96a07c45bd0ff599ee418c16ba7d54715170c,
  vermagic 6.18.60 SMP mod_unload ARMv7 p2v8; vmlinux/zImage rebuilt
  (zImage 3,346,824 B sha256 5cfc1632...); Module.symvers now exports the six
  cfg80211 symbols the driver imports.
- Image build evidence: sysupgrade container 6,267,199 B sha256 22b72852...
  (kernel slice inside: 3,372,638 B sha256 13e81214..., slot hash 1794a531...).

## 2. Exact rebuild sequence (host, isolated tree)

Use the project release-line tree; do not run a concurrent 'make world' in the same
tree while another lane is verifying modules against it (this lane acknowledges doing
exactly that earlier and re-ran the cfg80211 module check at the end - see
BUILD-VERIFICATION.md section 5).

~~~
cd /home/shibbityshwab/openwrt-lab-selectorfix-img
# target tree must be the canonical one from opensource/target-skeleton (synced),
# config-6.18 includes the cfg80211 block:
grep -n 'CONFIG_CFG80211=' target/linux/hisilicon/luofu/config-6.18
make -j16 target/linux/prepare          # RC=0, no NEW config prompts (verified)
make -j16 target/linux/compile          # RC=0, produces net/wireless/cfg80211.ko
make -j16                               # RC=0, produces the sysupgrade container
# extract the kernel slice to flash:
rm -rf /tmp/cfg80211-slice && mkdir -p /tmp/cfg80211-slice
tar -xOf bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin \
    sysupgrade-cudy_wr3000-v2/kernel > /tmp/cfg80211-slice/kernel.bin
sha256sum /tmp/cfg80211-slice/kernel.bin   # 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
~~~

Deployable modules (host-built, same tree/config):
- /home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/net/wireless/cfg80211.ko
  (2,328,548 B sha256 949393f9..., vermagic 6.18.60 SMP mod_unload ARMv7 p2v8)
- opensource/docs/incoming/wifi-cfg80211/src/hi5622_wifi.ko
  (505,076 B sha256 d2b3cf79... rebuilt this session against this exact tree/config;
  depends=cfg80211, 0 warnings. The lane's build script exits 1 at its redundant
  provider-symbol check because the real tree Module.symvers now exports the symbols -
  the compile+modpost steps PASS; see section 5. If the source changes, rebuild with
  the lane's tools/build.sh and use the new hash.)

## 3. Attended bench install (Lead; recovery door armed)

Preconditions, in order:
1. Human at the bench; recovery card READY (power-off + RESET + TFTP); UART capture
   running read-only to an absolute path (this is a kernel-only write, not a recovery
   action, but the host must be able to fall back).
2. No other lane has a build running in that tree (the kernel slice must be final).
3. Box on our image, pair b (kernelb/rootfsb), SSH 192.168.1.1 reachable.

Kernel slot choice (important): the box currently runs pair b, so the inactive kernel
slot is a (kernela). Our kernel has CONFIG_CMDLINE_FORCE with ubi.mtd=rootfsb, so the
new kernel will mount the existing rootfs b either way.
- Push to the inactive slot with the existing tool (it refuses to write the running
  slot and waits for the box to come back):

~~~
cd /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
tools/bench-push-kernel.sh /tmp/cfg80211-slice/kernel.bin --slot a
~~~

- The tool verifies the uploaded file sha256, writes kernela (mtd11), verifies the
  whole 8,650,752-byte slot hash (1794a531...), sets the selector to pair a and
  reboots. Rootfs b is untouched.
- After boot the pair recorder (enabled 2026-11-24, /etc/init.d/luofu_record_pair)
  records the ROOTFS pair (b) in the selector cell. Consequence: a later reset returns
  to kernelb, i.e. the previous GP1-line kernel - a built-in rollback; keep the new
  kernel only for this session. If the new kernel fails to boot, the stock pair a
  kernel/rootfs and the loader failover are still intact (physical recovery as last
  resort).

## 4. Bench-load the driver (attended, one attempt)

After the box is back (uname -r 6.18.60, SSH works):

~~~
scp /home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/net/wireless/cfg80211.ko root@192.168.1.1:/tmp/
scp opensource/docs/incoming/wifi-cfg80211/src/hi5622_wifi.ko root@192.168.1.1:/tmp/
ssh root@192.168.1.1
  # driver modules are not installed in the rootfs on purpose; load from /tmp in order:
  insmod /tmp/cfg80211.ko
  insmod /tmp/hi5622_wifi.ko   # add fw_path / block paths exactly as the lane's REPORT sec 8 says
  lsmod | grep -E 'cfg80211|hi5622'
  dmesg | tail -40
~~~

Stop rules: one insmod attempt; if cfg80211.ko fails with unresolved symbols/vermagic,
record the exact dmesg, do not retry, reset the box (it returns to kernelb). Do not
write the running kernel slot, do not rmmod after release, never touch the vendor
modules or CA 0x400392F0. WiFi bring-up after the modules load follows the
wifi-cfg80211 REPORT sec 8 attended sequence.

## 5. Honest limits of this handoff

- The kernel + image are host-verified only. The cfg80211 module and the driver load
  are NOT hardware-verified by this lane.
- The image does not package cfg80211.ko (kmod-cfg80211 is not selected); the module is
  pushed to /tmp. A future image should carry it via a KernelPackage once the Wi-Fi
  driver lands.
- This is a BENCH config. The target-PR config must decide the Wi-Fi stack properly
  (CONFIG_CFG80211 in the target kernel config plus the package dependency, or a
  backports/mac80211 decision); see CONFIG-AUDIT.md.
- The review form (upstream/series-6.18) is unaffected: cfg80211 is not part of the
  luofu kernel patch series.
