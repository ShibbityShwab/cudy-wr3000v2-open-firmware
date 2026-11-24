# wifi-openwrt-pkg - REPORT (shared task task-95)

**Lane:** wifi-openwrt-pkg (host-only packaging). **Date:** 2026-11-24.
**Status:** DELIVERED - OpenWrt kernel-module package created and build-verified
in the project OpenWrt tree.  The lane never contacted the bench router; the
bench install/run remains an attended Lead step.

---

## 0. Result in one table

| item | value |
| --- | --- |
| package | kmod-hi5622-pciet-hcc 6.18.60-r1 (target-gated to hisilicon/luofu) |
| OpenWrt tree | /home/shibbityshwab/openwrt-lab-selectorfix-img |
| kernel | 6.18.60 (arm, cortex-a9, profile DEVICE_cudy_wr3000-v2, OpenWrt GCC 14.4.0) |
| build command | make package/hi5622-pciet-hcc/compile V=s |
| make result | rc=0, zero compiler warnings/errors, apk built |
| module | hi5622_pciet_hcc.ko, 262,920 B |
| module sha256 | dba4f8c28356b5a65b46da4ed6c9766f627672c286a20e3899f10c27dbf0f75e |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| package apk | kmod-hi5622-pciet-hcc-6.18.60-r1.apk, 73,795 B |
| apk sha256 | a7e2e58401641045926cf11e7a5101b062746cf505d765e939ddea5e5d4ce2db |
| seed helper | hi5622-seed, 62,536 B static ARM ELF |
| seed sha256 | 56df0591e322afd2108a4690925e5515aa589a0c56fb18e69e6f217805488c0f |
| reproducibility | three consecutive builds: same module + apk sha256 |
| .config after build | byte-identical to the pre-build copy (package is opt-in) |
| device-free selftest | 22/22 checks PASS (tools/selftest.sh) |
| vendor bytes in package | none (selftest enforces; firmware/custom blocks are local-only) |

---

## 1. Scope and constraints

Task task-95 asked for: an OpenWrt package for the clean-room Wi-Fi driver and
its firmware/custom-block handling, integrated with the existing
hisilicon/luofu target, **actually built** in the tree with the build log/sha
recorded, plus docs, all host-only inside opensource/docs/incoming/.

The clean driver tree (task-94, docs/incoming/wifi-driver-clean) was still
being written while this lane ran, so the task's fallback applies: package the
current hi5622_pciet_hcc module as the interim artifact with a clean
interface.  The package interface is deliberately source-swappable, so the
clean driver can replace src/ without touching the Makefile, init script,
helper tooling or safety policy.

Constraints honoured:

* no vendor firmware bytes, custom-block data, vendor object or secret is
  committed or shipped (project publication rule);
* no kernel-patch changes and no target changes: the package adds only
  package/hi5622-pciet-hcc/ to the tree and relies on the existing PCIe host
  patches 013/013a + board DTS dma-ranges that the bench-verified transport
  already requires;
* no bench/device contact (host-only lane); the init script performs no device
  writes and is disabled by default;
* the package is target-gated, bench-image-only, and is not a cfg80211 /
  mac80211 interface.

---

## 2. Driver source snapshot and provenance

package/hi5622-pciet-hcc/src/ is the assembled wifi-d2h-direct build
(the latest committed bench-validated module, commit 7a8e542):

* wifi-hcc-service/src (commit 03ad691) - transport skeleton
  (hi5622_pciet.h, pciet_main.c, pciet_ete.c) + HCC service
  (hi5622_hcc_service.h, pciet_hcc_service.c) + Kbuild;
* wifi-d2h-direct/src/hcc-d2h-direct.patch applied with patch -p1
  (9 hunks: direct-read poll in the service pass, rd_* attribute
  create/remove, pciet_hcc_rd() accessor, Kbuild object);
* wifi-d2h-direct/src/hi5622_d2h_direct.h and pciet_d2h_direct.c;

The assembled tree is **byte-identical** to the wifi-d2h-direct lane build
tree (cmp SAME for all eight source files).  src/SHA256SUMS pins the snapshot;
tools/selftest.sh verifies it.  The integration patch is kept inside src/ as
provenance (not applied at package build time - the snapshot is already
patched).

Snapshot sha256 (src/SHA256SUMS):

| file | sha256 |
| --- | --- |
| Kbuild | bfefbce9f2add4c5aa5b9ba56e79830e22560438f9aca80d3437f0170129294d |
| hcc-d2h-direct.patch | b7c87b668e9020030e340ab819dc2f1f0446a203c5cfa7fc854cf12f06c6734d |
| hi5622_d2h_direct.h | 297f51ab96e6e747a36c325361e7e2e9a99539e2c4df67c0a66af0e0b7f9630f |
| hi5622_hcc_service.h | 20c00d05c08df2ae0a45594c308caea6e93319afd0f85726d9a4be02e6b944bf |
| hi5622_pciet.h | fa65432acc0b0041a269fd9ee15ad1088a50a48ae4872a9f1eb1381cd7a0bbeb |
| pciet_d2h_direct.c | 07582199bd56ac6cb859b8c393abfdee204951fd163c210e833cc2ac15af59c6 |
| pciet_ete.c | a5562d2b5da37f38ae29972cc07df3b18dab7488b3c38fdcab29c9678ea6c760 |
| pciet_hcc_service.c | 187a9231a82e5d112bb1fb839b5fda91d277df5f9ca2d7f7cfc41e49a1b3ca87 |
| pciet_main.c | 6e0aa73bfef2605436a545ffa5d2c3da72eb0811cee16e82e92252db3e445342 |

(also committed as src/SHA256SUMS, which tools/selftest.sh verifies)

Replacement path: when task-94 lands, drop the clean driver files into src/,
update src/SHA256SUMS and the Kbuild object list, re-run
./tools/selftest.sh && ./tools/build-package.sh.  Nothing else changes.

---

## 3. Package design

### 3.1 Makefile (package/hi5622-pciet-hcc/Makefile)

* include rules.mk + kernel.mk + package.mk; PKG_LICENSE GPL-2.0-only;
  PKG_MAINTAINER project; PKG_BUILD_PARALLEL.
* define KernelPackage/hi5622-pciet-hcc:
  SUBMENU Wireless Drivers, TITLE clean-room bench driver,
  DEPENDS @TARGET_hisilicon_luofu (target gate),
  FILES $(PKG_BUILD_DIR)/hi5622_pciet_hcc.ko;
* deliberately NO AUTOLOAD entry: the module must never auto-bind on an
  unrelated image (the init script is opt-in as well);
* conffiles /etc/config/hi5622-pciet;
* Build/Prepare copies the local src/ snapshot and hi5622-seed.c into
  PKG_BUILD_DIR - no PKG_SOURCE, no download, no vendor blob;
* Build/Compile = $(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules, then a STATIC
  userspace build of hi5622-seed;
* install hook writes the module plus /etc/init.d/hi5622-pciet,
  /etc/config/hi5622-pciet, /usr/sbin/hi5622-status, /usr/sbin/hi5622-seed.

Static seed helper rationale (found during the lane): the first build put a
dynamically linked helper into the kmod package and the OpenWrt dependency
audit rejected it ("Package kmod-hi5622-pciet-hcc is missing dependencies for
the following libraries: libc.so libgcc_s.so.1").  A kernel module package
must stay libc-free, so the helper is linked with -static -s (62,536 B ARM
static ELF, no DT_NEEDED).  The module itself is unaffected.

### 3.2 Init script + UCI policy (files/)

* /etc/init.d/hi5622-pciet (START=95): loads the module ONLY when
  /etc/config/hi5622-pciet has enabled='1' (default 0).  It rejects absolute
  fw_path values (firmware-loader names are relative to /lib/firmware), runs
  modprobe hi5622_pciet_hcc fw_path=<name>, and nothing else.  It never
  releases the CPU, never writes CECE, never uploads firmware, never seeds
  blocks.  stop() refuses to unload the module: after a CPU release a
  power-cycle is the reset, never rmmod.
* /etc/config/hi5622-pciet: enabled '0', fw_path 'hi_wifi/FIRMWARE.bin' with
  comments restating the local-evidence and safety rules.
* /usr/sbin/hi5622-status: read-only snapshot (endpoint/BARs, module params,
  local firmware presence/size/sha, UCI policy, read-only driver attributes
  hcc_status/hcc_ready/hcc_events/hcc_rings/rd_status/rd_catalog) and the
  safety reminders.  No writes, no devmem.

### 3.3 hi5622-seed (tools/hi5622-seed.c)

Guarded helper for the two vendor custom blocks that the firmware needs before
the CPU release (platform descriptor 24 words at BAR0 0x86A800 = CPU
0x001B2800; Wi-Fi PHY/radio block 618 words at BAR0 0x86A860 = CPU 0x001B2860;
BAR0 = CPU + 0x006B8000):

* dump mode (read-only) regenerates local word-list files from a stock/golden
  state; write mode writes them in ascending order and verifies every word by
  read-back;
* write mode refuses when the handshake page (BAR0 0x877000, CA 0x1BF000)
  shows the image already ran (+8 == 2, the CPU1 S1 ack) or entry-A progress
  (+0 in 0x8680xxxx), unless --force is supplied for a diagnostic run;
* it only touches the two block windows plus the read-only page; it never
  writes CA 0x400392F0 (out[5]), the CECE gate or the release word;
* it embeds addresses, sizes and formats only - never block contents or
  firmware bytes.
* --bdf defaults to 0000:00:00.0 (the bench endpoint).

---

## 4. Build verification (recorded)

Command (as committed, see tools/build-package.sh):

    ./tools/build-package.sh
    -> installs package into $OWRT/package/hi5622-pciet-hcc
    -> backs up $OWRT/.config, temporarily adds
       CONFIG_PACKAGE_kmod-hi5622-pciet-hcc=m, restores it on exit
    -> make package/hi5622-pciet-hcc/clean && compile V=s
    -> records log + summary in /home/shibbityshwab/wifi-openwrt-pkg-work/

Recorded build: 2026-11-24, make rc 0, full log sha256
8030efd57597e9722b9f2ce1896bdfc24becb35ba7e8d47fe6065933e243426a
(build-log.txt, 31,617 B; build-summary.txt in this directory).

Kernel module (build_dir, unstripped):

    hi5622_pciet_hcc.ko  262,920 B
    sha256 dba4f8c28356b5a65b46da4ed6c9766f627672c286a20e3899f10c27dbf0f75e
    ELF32 ARM EABI5 relocatable, license GPL
    vermagic 6.18.60 SMP mod_unload ARMv7 p2v8
    alias pci:v000059E7d00000005sv*sd*bc*sc*i*
    params: fw_path (charp), poll_ms (uint), twin_enabled (bool),
            use_irq (bool), hcc_poll_ms (uint), hcc_cece_mode (uint),
            rd_ab_write (uint)

Package payload (installed tree, mode/size/path):

    -rw------- 999    ./etc/config/hi5622-pciet
    -rw-r--r-- 80328  ./lib/modules/6.18.60/hi5622_pciet_hcc.ko   (stripped)
    -rwxr-xr-x 1856   ./etc/init.d/hi5622-pciet
    -rwxr-xr-x 2565   ./usr/sbin/hi5622-status
    -rwxr-xr-x 61751  ./usr/sbin/hi5622-seed
    (plus apk bookkeeping under ./lib/apk/packages/)

Resulting apk (bin/targets/hisilicon/luofu/packages/):

    kmod-hi5622-pciet-hcc-6.18.60-r1.apk  73,795 B
    sha256 a7e2e58401641045926cf11e7a5101b062746cf505d765e939ddea5e5d4ce2db
    kernel dependency: kernel=6.18.60~5de0e425f1313eeec82615317fdc1f76-r1

Reproducibility: three consecutive clean builds (manual override, temporary
.config symbol, and the committed script) all produced module sha
dba4f8c2... and apk sha a7e2e584....  .config was restored byte-identically
every time (verified by diff in build-summary.txt).

Device-free selftest: ./tools/selftest.sh -> 22/22 PASS, including
src/SHA256SUMS, Makefile contract, script syntax, "init is write-free",
"no AUTOLOAD", package-tree hygiene (no binary/blob), the seed-helper
-Wall -Wextra -Werror compile, the HCC frame/ring model (83 checks) and the
D2H direct-reader model (92/92 checks).

---

## 5. Integration with the hisilicon/luofu target

* The package is gated DEPENDS @TARGET_hisilicon_luofu; it compiles against
  the tree's existing kernel build (linux-6.18.60) and the OpenWrt GCC 14.4.0
  external toolchain.  No target, DTS or kernel-patch change is made by this
  package; the bench-verified transport already needs the existing PCIe host
  patches 013/013a and the RC inbound dma-ranges 0x80000000..0x8fffffff in the
  board DTS.
* Image inclusion: add CONFIG_PACKAGE_kmod-hi5622-pciet-hcc=m (or y) to the
  bench image config, run make package/hi5622-pciet-hcc/compile, then the
  project's image build; the apk lands in
  bin/targets/hisilicon/luofu/packages/.  The module is installed as
  /lib/modules/6.18.60/hi5622_pciet_hcc.ko and loaded only when the attended
  session (or uci enabled='1') asks for it.
* Running-image install: apk add --allow-untrusted <apk> when the image
  carries the matching kernel revision; the package pins
  kernel=6.18.60~5de0e425...-r1, so a bench image built from a different
  kernel revision needs a rebuild of the image (or a matching rebuild of the
  package) rather than forcing the dependency.
* Lab-tree side effects of the recorded build: the package source directory
  is installed at $OWRT/package/hi5622-pciet-hcc (safe: not selected in
  .config); build_dir outputs and the apk remain.  The staging-root
  (root-hisilicon) copies that the test build installed were REMOVED after
  recording so that an unselected incremental image build cannot pick up stale
  files; re-run tools/build-package.sh (and select the package) before an
  actual bench image build.

---

## 6. Firmware and custom-block handling (local-only)

| artifact | source | handling |
| --- | --- | --- |
| vendor firmware image 928,920 B | local evidence (recovery-extract) | NOT shipped; place at /lib/firmware/hi_wifi/FIRMWARE.bin; fw_path is a firmware-loader name relative to /lib/firmware |
| platform descriptor 24 words (BAR0 0x86A800) | vendor-derived capture | NOT shipped; written by hi5622-seed from a local word-list file before release |
| Wi-Fi PHY/radio block 618 words (BAR0 0x86A860) | vendor-derived capture | NOT shipped; same helper; local-only word list |

The package ships addresses, formats and guards only.  tools/selftest.sh
fails if a *.bin/*.ko/*.o/*.fw/*.img/*.dtb/*.dtbo file appears inside the
package tree, and all committed files are text.

---

## 7. Bench usage and safety (for the Lead session)

1. recovery door armed, bench link PASS, UART read-only capture running
   (nothing in this package changes those rules);
2. install/load per README; hi5622-status for the read-only starting snapshot;
3. follow BENCH-PROCEDURE.md of wifi-d2h-direct unchanged:
   prepare -> upload -> verify (diffs=0) -> custom-block pre-seed with local
   files via hi5622-seed write -> hcc_arm -> hcc_loop -> release 0x5A5A ONLY;
4. never write CECE before handshake page +8 == 2; never write CA 0x400392F0;
   never rmmod after a release; one idea per session; capture dmesg + UART.
5. The direct-read D2H catalogue (rd_catalog/rd_decode) is the working
   receive path; the post-ready firmware IRQ/H2D gate is still open (ledger
   2026-11-24) and is not addressed by packaging.

---

## 8. Evidence index

| file | what |
| --- | --- |
| build-log.txt | full V=s build log of the recorded build (sha 8030efd5...) |
| build-summary.txt | artifacts, sha256s, payload, .config restore proof |
| package/hi5622-pciet-hcc/src/SHA256SUMS | driver snapshot integrity |
| package/hi5622-pciet-hcc/Makefile | package contract |
| tools/selftest.sh | 22-check device-free selftest (run: 22/22 PASS) |
| tools/build-package.sh | reproducible build + evidence recorder |
| tools/install-into-tree.sh | package installer for the OpenWrt tree |
| README.md | quick start / bench integration / local-only data |
| BUILD-NOTES.md | build record and lab-tree state |

Local (not committed): full build logs and summaries under
/home/shibbityshwab/wifi-openwrt-pkg-work/; the built module/apk under the
OpenWrt tree build_dir/bin; the vendor firmware and custom-block word files
remain local evidence.

---

## 9. Limitations / handoff

* Interim driver: research module packaged as-is; swap src/ when the clean
  driver tree (task-94) lands.  The package interface and safety policy are
  designed to survive that swap without changes.
* The package has not been installed on the bench box by this lane; the bench
  run (and the apk install) is attended Lead work.
* Not upstreamable: no cfg80211/mac80211 interface, no vendor firmware
  redistribution, bench image only.  This package is an integration vehicle
  for the bench image, not a release artifact.
* The firmware IRQ-unmask / H2D mailbox ack gate (ledger 2026-11-24 15:10 and
  16:10) remains open and is out of scope here.

---

## 10. Publication compliance

No vendor binary, firmware byte, custom-block content, ROM byte, secret or
device identifier was committed.  Only source/text written by the project,
addresses/sizes already documented in the committed clean-room lanes, build
logs (text) and package scripts are added.  The firmware image and the
custom-block word lists stay on the local host as local evidence.
