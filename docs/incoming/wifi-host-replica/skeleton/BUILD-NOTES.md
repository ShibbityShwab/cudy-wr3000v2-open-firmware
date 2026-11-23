# BUILD-NOTES - host replica skeleton (6.18.60, out-of-tree)

**Result (verified):** hi5622_host_replica.o compiles clean against the project's built
kernel tree (openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/
linux-hisilicon_luofu/linux-6.18.60) together with the two transport objects; the full
module links and modposts:

    CC [M]  pciet_main.o
    CC [M]  pciet_ete.o
    CC [M]  hr.o
    LD [M]  hi5622_pciet.o
    MODPOST Module.symvers
    CC [M]  hi5622_pciet.mod.o
    LD [M]  hi5622_pciet.ko

The only diagnostic was the transport module's pre-existing modpost warning
("missing MODULE_DESCRIPTION() in hi5622_pciet.o"), unrelated to this skeleton.
The .ko/objects are build artifacts and are NOT committed; they were produced in an
ephemeral scratch directory (/tmp/hr-build, a copy of the transport build plus the two
skeleton files).

**Build line** (same as the transport module, see ../../wifi-transport-port/build/BUILD-NOTES.md):

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    make ARCH=arm CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- \
         -C $KDIR M=<scratch dir with pciet_main.c pciet_ete.c pciet_host_replica.c + headers> modules

The scratch dir is a copy of ../wifi-transport-port/build plus this skeleton's
.h/.c; no file outside this deliverable directory is modified by the lane.

**Integration (two one-line hooks in pciet_main.c, not applied here):**

1. probe/attach: add a struct hr_replica to the transport context owner and call
   hr_attach(pci, hr) once pci resources are mapped.
2. after the existing release/announce sysfs step: call hr_on_released(hr).
3. in pciet_poll_thread(): call hr_service(hr) instead of (or in addition to)
   pciet_d2h_scan(ctx) - hr_service implements the vendor ack/drain/re-arm order
   and dispatches the same d2h[] table, so the raw scan should be dropped to
   avoid double dispatch.
4. on remove: hr_detach(hr) before freeing the rings.

**Compile-time guarantees enforced by the skeleton**

- BUILD_BUG_ON: the written D2H registers are never the never-write out[5]
  (CA 0x400392f0), struct hcc_msg_header == 10 B, node == 8 B, header == 12 B.
- No write exists to out[5], to the RC misc window, or to H2D id 5 in the boot path
  (the id-5 path is only reachable through hr_h2d_announce(5), which warns loudly).
- The exception-context capture only READS CA 0x400008 / 0x40000c..0x400044.

**What the skeleton does NOT do**

- It does not modify the transport sources (deliberate: separate lane/file), so the
  transport's existing pciet_msg_wait_for_clr() out[2] poll remains until the hook is
  applied; hr_h2d_announce() in the skeleton already implements the vendor order
  (wait out[0], publish out[0], OR out[2]).
- It does not arm SR1/SR2/DR1-3; the transport exposes sr0/dr0 only.  The spec calls
  for all seven channels; extending the transport struct is a follow-up one-liner.
- PM layer (H2D id 1 / D2H ids 4-5) is logged only; enabling it needs the vendor's
  50-jiffy sleep-ack completion and GPIO IRQ 82.
