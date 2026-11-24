# BUILD-NOTES - wifi-h2d-frames library and selftest

**Lane:** wifi-h2d-frames (task-93, host-only). **Date:** 2026-11-24.
**Contents:** pure-C H2D frame library (src/h2d_frames.h + .c + generated
src/h2d_catalogue.inc), device-free selftest + CLI (src/h2d_selftest.c), and
two build drivers (tools/build.sh, tools/kernel-check.sh). No vendor artifact is
read by the shipped tools; the generated catalogue parses COMMITTED markdown only.

## Result

| item | value |
| --- | --- |
| host selftest | 233 checks PASS, 0 failed |
| host compile | cc (GCC) 16.2.1, C99, -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes |
| host binary | /tmp/wifi-h2d-frames-build/h2d_selftest, 51,584 B (local build product, not committed) |
| kernel-context compile | h2d_frames.o linked into the 6.18.60 transport module, zero warnings under the tree CONFIG_WERROR, modpost clean |
| kernel-check module | 233,768 B, sha256 6a3efa57e628a9d32726a9d79ef867db5dcbef9c4d619aa37316221edb2a9c8c (local evidence only, not committed) |
| kernel-check vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| kernel-check symbols | 29 h2d_* symbols (h2d_build_*, h2d_frame_*, h2d_sr_*, h2d_idx_*, h2d_mailbox_plan, h2d_ready_decode, catalogue accessors) |
| catalogue generator | 48 vendor senders, 60 firmware receive slots, deterministic (--check) |

The module hash is build-path dependent (debug info records the build directory):
an earlier scratch copy built at /tmp/h2d-kernel-check produced 233,732 B sha256
48493d36f62a2857a765357f207d67e8f9345b7ac56ab9a0161b9aea2d84fc2e. Both builds are
warning-free; compare behaviour, not the hash, across scratch directories.

## Host build and selftest

```
cd opensource/docs/incoming/wifi-h2d-frames
./tools/build.sh
```

What it does:

1. runs tools/gen_catalogue.py (writes src/h2d_catalogue.inc and catalogue.md from
   the committed wifi-h2d-catalog report);
2. runs it again with --check and fails if the committed generated files are stale;
3. compiles src/h2d_selftest.c + src/h2d_frames.c with the strict flags above;
4. runs the suite (known-answer vectors, refusal matrix, SR node/packed-index,
   mailbox/post plan, catalogue invariants, D2H ready decode).

Toolchain: stock host cc (GCC 16.2.1 here). No kernel tree, no image build, no
device access. The CLI is the same binary:

```
/tmp/wifi-h2d-frames-build/h2d_selftest --help
/tmp/wifi-h2d-frames-build/h2d_selftest catalogue
/tmp/wifi-h2d-frames-build/h2d_selftest hdpp 0 0 4 0x00d8 0100000000000000000000000000000000000000 0000000001000000 ffffffff
/tmp/wifi-h2d-frames-build/h2d_selftest post 000100000c00000000005a5a 0 32 0x84543000 0 0
/tmp/wifi-h2d-frames-build/h2d_selftest d2h 040000001000020000005a5a50080601
```

## Kernel-context compile check (optional)

```
cd opensource/docs/incoming/wifi-h2d-frames
./tools/kernel-check.sh
```

It copies the COMMITTED ../wifi-hcc-service/src tree to /tmp/wifi-h2d-frames-kernel
(no other lane file is modified), copies this lane h2d_frames.h/.c and the generated
inc into the scratch tree, adds h2d_frames.o to the module object list, and builds
ONE out-of-tree module against the project already-built 6.18.60 tree
(module only - never an image/world build, never a device access).

Default paths (override by environment):

```
KERNEL_ROOT=/home/shibbityshwab/openwrt-lab-selectorfix-img
KDIR=$KERNEL_ROOT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR=$KERNEL_ROOT/staging_dir
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
```

Manual equivalent (what the script runs):

```
rm -rf /tmp/k && mkdir -p /tmp/k
cp -a ../wifi-hcc-service/src/. /tmp/k/
cp src/h2d_frames.h src/h2d_frames.c src/h2d_catalogue.inc /tmp/k/
# Kbuild: hi5622_pciet_hcc-y := pciet_main.o pciet_ete.o pciet_hcc_service.o h2d_frames.o
make -C "$KDIR" M=/tmp/k ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules
```

## Integrating the library into the bench module (for the Lead)

The kernel check only proves compilation; it does not wire a sysfs store. When a bench
session wants the exact first post-ready frame, the minimal integration is:

1. Kbuild: append h2d_frames.o to the module object list.
2. In the module store handler (where the live SR0 producer, node-array device VA and
   out[0]/out[2] values are already read):

```c
struct h2d_frame f;
struct h2d_post_plan plan;
enum h2d_err e;
u8 frame[H2D_MAX_TOTAL];
size_t total;

if (h2d_build_g0_id1_hdpp_txq_sync(&f, 0, 0, 4, token, 0x00d8,
                                   data, 20, tail, &e) != 0)
        return -EPERM;                      /* policy refusal, log h2d_strerror(e) */
if (h2d_plan_post(&f, live_sr0_producer, 32, live_node_word0,
                  live_out0, live_out2, &plan, &e) != 0)
        return -EBUSY;                      /* H2D_ESTATE = previous word unconsumed */
/* write plan.sr.node_word0/word1 at node index plan.sr.node_index, then the
 * packed producer plan.sr.producer_after to SR0+0x18, then mailbox id 3:
 * out[0] = plan.mb.pending_write, out[2] = plan.mb.doorbell_write.  All writes
 * through the module existing never-write-out[5] gated writer. */
```

Keep the store single-shot (refuse a second call until the announce drains) and log
the read-backs exactly as the existing hcc_sr_announce does. Do not add any other
device write.

## Files and provenance

| file | provenance |
| --- | --- |
| src/h2d_frames.h, src/h2d_frames.c | original clean-room code (this lane) |
| src/h2d_catalogue.inc, catalogue.md | GENERATED by tools/gen_catalogue.py from the committed wifi-h2d-catalog report (sec 5.1, App A, App B) |
| src/h2d_selftest.c | original clean-room code (this lane); vectors are derived constants published in committed lane reports |
| tools/*.py, tools/*.sh | original clean-room tooling (this lane) |
| REPORT.md | original analysis with per-claim evidence references |

No vendor .ko, firmware image, ROM dump, object code, calibration value, credential or
per-unit secret is committed. Global stop rules: no device RAM write, no mailbox id 5,
no group-4 H2D, never write CA 0x400392F0, no CECE during boot, UART read-only.
