# wifi-openwrt-pkg - OpenWrt package for the clean-room Hi5622V100 Wi-Fi host driver

**Lane:** wifi-openwrt-pkg (shared task task-95), host-only. **Date:** 2026-11-24.
**Status:** package created and BUILD-VERIFIED in the project OpenWrt tree
(kmod-hi5622-pciet-hcc, kernel 6.18.60, arm_cortex-a9, profile
DEVICE_cudy_wr3000-v2).  The lane never contacted the bench router; installing
it on the box is an attended Lead step.

This is the **interim packaging** of the compiled/bench-validated
wifi-d2h-direct build of hi5622_pciet_hcc.ko: PCIe endpoint transport +
vendor-order HCC service + direct-read device group-A D2H receive path.
It is replaced wholesale when the clean driver tree
(docs/incoming/wifi-driver-clean, task-94) lands - swap package/src/ and keep
the Kbuild object list / package interface.

## Hard rules satisfied by this package

* **No vendor bytes.** The 928,920-byte vendor firmware image and both
  custom blocks are LOCAL EVIDENCE ONLY and are not shipped.  The package
  provides only the mechanisms that consume them at bench time (fw_path and
  the hi5622-seed helper); see "Local-only data" below.
* **No device writes at load.**  The init script is disabled by default
  (enabled='0') and only modprobes the driver; probe is write-free (host ring
  allocation + sysfs attributes).  Release (0x5A5A), the CECE gate, firmware
  upload, custom-block pre-seed and every H2D step stay explicit attended
  bench actions.
* **Release-only policy is baked into the driver snapshot**: the legacy
  announce attr (blind 0xCECE) is refused in this build; the CECE policy is
  boot-complete + evidence gated.  Never write 0xCECE before handshake page
  +8 reads 2; never write CA 0x400392F0 (out[5]); never rmmod after a CPU
  release.
* **No kernel-patch changes**: the package adds only a package/ directory; it
  relies on the existing PCIe host patches 013/013a and the board DTS
  inbound dma-ranges that the bench-verified transport already requires.

## Contents

    package/hi5622-pciet-hcc/     the OpenWrt package tree (copy into $OWRT/package/)
      Makefile                    KernelPackage: kmod-hi5622-pciet-hcc
      src/                        driver source snapshot (hcc-service + d2h-direct)
        SHA256SUMS                snapshot integrity manifest
      files/hi5622-pciet.init     init script (disabled by default, write-free)
      files/hi5622-pciet.config   UCI policy defaults (enabled=0, fw_path)
      files/hi5622-status         READ-ONLY snapshot helper
      tools/hi5622-seed.c         guarded custom-block dump/write helper
      tools/frame_selftest.py     HCC frame/ring model (lane selftest)
      tools/d2h_selftest.py       direct-read D2H model (lane selftest)
    tools/selftest.sh             device-free package selftest (22 checks)
    tools/install-into-tree.sh    copy the package into the OpenWrt tree
    tools/build-package.sh        build + record evidence (sha/log/summary)
    build-log.txt                 full build log of the recorded build
    build-summary.txt             artifacts/sha/metadata of the recorded build
    REPORT.md                     lane report (design, evidence, limitations)
    BUILD-NOTES.md                build record and reproducibility notes

## Quick start (host, no device)

    cd opensource/docs/incoming/wifi-openwrt-pkg
    ./tools/selftest.sh          # 22 checks: snapshot, package contract, models
    ./tools/build-package.sh     # builds in the lab OpenWrt tree, records evidence

Expected recorded build (build-summary.txt, 2026-11-24):

    kernel module  hi5622_pciet_hcc.ko  262,920 B
                   sha256 dba4f8c28356b5a65b46da4ed6c9766f627672c286a20e3899f10c27dbf0f75e
                   vermagic 6.18.60 SMP mod_unload ARMv7 p2v8
    seed helper    hi5622-seed           62,536 B (static, stripped in package)
                   sha256 56df0591e322afd2108a4690925e5515aa589a0c56fb18e69e6f217805488c0f
    OpenWrt apk    kmod-hi5622-pciet-hcc-6.18.60-r1.apk  73,795 B
                   sha256 a7e2e58401641045926cf11e7a5101b062746cf505d765e939ddea5e5d4ce2db
    make rc        0, no compiler warnings, .config restored byte-identically

Default tree: /home/shibbityshwab/openwrt-lab-selectorfix-img (override with
--owrt DIR or $OWRT).  The build is reproducible: three consecutive builds
produced the same module and apk sha256.

## Bench-image integration (Lead, attended)

1. Copy the package into the tree and select it in the image config:

    cd /home/shibbityshwab/openwrt-lab-selectorfix-img
    /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/opensource/docs/incoming/wifi-openwrt-pkg/tools/install-into-tree.sh --owrt .
    echo 'CONFIG_PACKAGE_kmod-hi5622-pciet-hcc=m' >> .config
    make defconfig
    make package/hi5622-pciet-hcc/compile
    make world          # or the project's bench image target

   The built ipk/apk is
   bin/targets/hisilicon/luofu/packages/kmod-hi5622-pciet-hcc-6.18.60-r1.apk.

2. Or install the built apk on the running bench image (matching kernel
   revision 6.18.60~5de0e425...; use --allow-untrusted only if the image does
   not carry the build key):

    apk add --allow-untrusted /tmp/kmod-hi5622-pciet-hcc-6.18.60-r1.apk

3. Place the local vendor firmware image (LOCAL EVIDENCE ONLY, 928,920 B) at
   /lib/firmware/hi_wifi/FIRMWARE.bin (or point the fw_path module parameter
   at another firmware-loader name).

4. Load the driver explicitly (default config keeps it unloaded at boot):

    modprobe hi5622_pciet_hcc fw_path=hi_wifi/FIRMWARE.bin
    hi5622-status                 # read-only state snapshot (see below)

   or, on an attended image only:

    uci set hi5622-pciet.main.enabled='1'; uci commit hi5622-pciet
    /etc/init.d/hi5622-pciet start

5. Run the existing attended bench procedure (BENCH-PROCEDURE.md of
   wifi-d2h-direct): prepare -> upload -> verify -> custom-block pre-seed ->
   hcc_arm -> hcc_loop -> release 0x5A5A ONLY -> read rd_* / hcc_*.  Do not
   write CECE before handshake page +8 == 2.

### hi5622-status (read-only helper)

Prints endpoint/BARs, module params, local firmware presence/size/sha, the
UCI policy and the driver's read-only attributes (hcc_status, hcc_ready,
hcc_events, hcc_rings, rd_status, rd_catalog).  It performs no device writes.

### hi5622-seed (guarded custom-block helper)

The vendor host pre-seeds two configuration blocks into firmware RAM before
the CPU release: the platform descriptor (CPU 0x001B2800 -> BAR0 0x86A800,
24 words) and the Wi-Fi PHY/radio block (CPU 0x001B2860 -> BAR0 0x86A860,
618 words).  BAR0 = CPU + 0x006B8000.

    hi5622-seed dump  [--bdf BDF] [--platform FILE] [--wifi FILE]
    hi5622-seed write [--bdf BDF] [--platform FILE] [--wifi FILE] [--force]

dump reads the blocks (regenerate a local capture from a stock/vendor state;
read-only).  write parses "INDEX=0xVALUE" word-list files, validates the exact
word count, refuses when the handshake page (BAR0 0x877000, CA 0x1BF000)
shows the image already ran (page +8 == 2, or page +0 in the entry-A range
0x8680xxxx) unless --force is given, then writes in ascending order and
verifies every word by read-back.  It never touches out[5] (CA 0x400392F0),
the CECE gate or the release word.

## Local-only data (publication rule)

* Firmware image: vendor-derived; 928,920 B local artifact; never committed.
  Copy it to /lib/firmware/hi_wifi/FIRMWARE.bin on the bench image.
* Custom-block word files: vendor-derived; capture locally (hi5622-seed dump
  from the golden/stock state, or the lab's existing local capture files);
  never committed.  The package contains addresses/formats/guards only.
* The package selftest fails if a *.bin/*.ko/*.o/*.fw/*.img/*.dtb blob appears
  inside the package tree.

## Limitations (honest)

* Interim driver snapshot: it is the research module, not the clean driver
  tree; not upstreamable, no cfg80211 interface, bench image only.
* The post-ready firmware IRQ/H2D gate is still open (see ledger 2026-11-24):
  the direct-read D2H path works, the mailbox ack does not.  The package does
  not change that and must not be used to work around it automatically.
* The package has NOT been installed on the bench box by this lane; the build
  is verified host-side only.  The bench session stays attended.

See REPORT.md for the full evidence and BUILD-NOTES.md for the build record.
