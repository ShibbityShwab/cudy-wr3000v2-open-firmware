# BUILD-NOTES - hi5622_wifi.ko with the first real post-ready H2D message path

**Lane:** wifi-h2d-send (task-98, extended by task-99 and task-102), host-only.
**Date:** 2026-11-24.
**Source:** this directory's `src/` (a copy of the wifi-driver-clean tree plus
the wifi-h2d-frames library plus the new h2d_send_logic/h2d_send files; no
build-time patch is applied).  The built `.ko` is local evidence only and is
NOT committed (`src/.gitignore` covers build products; no vendor
binary/object/firmware byte is involved).

## Result

| item | value |
| --- | --- |
| module | hi5622_wifi.ko |
| size | **399,816 B** |
| sha256 | **b11ffea6d1cebb9f03a3a4d162dbc0c5cbd684bb6298bcf22d5e6d9ebb1d882f** |
| ELF | 32-bit LSB relocatable, ARM, EABI5 (with debug_info, not stripped) |
| license | GPL |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| depends | (none - one self-contained module) |
| objects | main.o transport.o ete.o d2h.o hcc.o service.o hcc_core.o hcc_core_kernel.o h2d_frames.o h2d_send_logic.o h2d_send.o |
| compiler/modpost warnings | **0** (project tree builds with -Werror) |
| exported `T h2d_*` symbols | 31 (h2d_frames builders/validators + h2d_send_logic gate/build/parse/emit-plan incl. `h2d_build_ctx_create`) |
| PCI alias | pci:v000059E7d00000005sv*sd*bc*sc*i* |
| host H2D logic selftest | **202/202 PASS** |
| driver selftest (extended) | **226/226 PASS** |
| firmware g0 id1 handler analysis selftest | **38/38 PASS** (reads the local pinned vendor blob; not part of the kernel build) |

Module params: `h2d_send_budget` (uint, default 1), `fw_path`, `wifi_block_path`,
`plat_block_path`, `allow_unseeded_release`, `hcc_poll_ms`, `hcc_cece_mode`,
`rd_ab_write`.

The module hash is **build-path dependent** (debug info records the build
directory), so a copy built from another path has a different hash but the
same behaviour/flags; record the hash printed by the build you deploy.

## task-99 extension (72-byte live-node variant + static handler analysis)

* `H2D_MSG_TXQ_SYNC72` / `h2d_send "txq_sync72"` / `h2d_txq_sync72` (WO):
  the same 48-byte byte-exact TXQ frame, committed with the SR node buffer
  padded to the vendor's live 72 bytes (24 x 0xff after the frame) and node
  word1 `(72<<16)|0x6d2b`; declared header total stays 0x30.  Same gates
  (READY_TXQ_SYNC/ACTIVE, latched boot, one-frame budget, once-only TXQ).
* The built module hash above is the task-102 build; the task-99 build was
  394,272 B sha256 `4677ceac...` and the task-98 build 391,456 B sha256
  `047be179...`.
* `tools/fw_g0id1_handler.py` + `REPORT-G0ID1-HANDLER.md`: static analysis of
  the firmware group-0 id-1 receive handler (parse offsets, key tables, the
  key-0xd8 worker, the fault candidates and the dispatcher path).  The tool is
  host-only and needs the local pinned vendor firmware blob + capstone from
  `.provision-venv`; it is not part of `tools/build.sh`.

## task-102 extension (ordered context-create -> TXQ sync)

* `H2D_MSG_CTX_CREATE` / `h2d_send "ctx_create"` / `h2d_ctx_create` (WO):
  the synthesized group-0 / id-1 config event with u16 key 0x0064
  (hdpp_config_send_event shape token[8] | key | len | data | pad; data type 3,
  vap 4, user 4; declared total 0x30, len 0x14, 0xff inherited tail; SR node
  48 B = declared total, mailbox id-3 announce).  Same carrier and safety
  gates as txq_sync72, plus READY-only and once-per-load.  It makes the
  firmware allocate `ctx+0x190`, the pointer the key-0xd8 TXQ worker
  dereferences; the ordered ladder is create -> read-only pack -> txq_sync72.
* Gate integration: the portable core keys on group/id and therefore latches
  ACTIVE/`txq_sync_submitted` on the create too (both frames are g0/id1).  The
  driver keeps the semantic split (`h2d_status` shows `ctx_sent` vs
  `txq_sent`; the once-only TXQ gate uses the real TXQ counter) and allows the
  TXQ in ACTIVE only after this load's create.  `hcc_core.*` and
  `h2d_frames.*` are unchanged.
* Ordered bench decision: ONE module load with `h2d_send_budget=2` (declared
  at insmod).  A reboot loses the created context, and a second module load
  would require `rmmod` after the release (forbidden).  See
  BENCH-PROCEDURE.md Ladder C.
* Selftests: host 202/202 (byte-exact ctx vector, builder refusals, ordered
  gate matrix, grammar, parse->build) and driver 226/226 (+22 ctx_create
  source/contract checks).  Build still 0 compiler/modpost warnings.

## Build line

Canonical (host selftest + kernel module + driver selftest + evidence):

    ./tools/build.sh

It exports `STAGING_DIR` (the OpenWrt GCC wrapper requires it), builds with:

    KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -C $KDIR M=$(pwd)/src modules

Logs go to `/tmp/hi5622-wifi-h2d-send-build.log`; the host selftest binary
goes to `/tmp/wifi-h2d-send-build/h2d_logic_selftest` (local build products).

## Verification steps (all device-free)

| step | command / file | result |
| --- | --- | --- |
| host compile of the H2D logic | `cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes` on h2d_logic_selftest.c + h2d_send_logic.c + h2d_frames.c + hcc_core.c | PASS (57,256 B binary) |
| host emission/gating selftest | `/tmp/wifi-h2d-send-build/h2d_logic_selftest` | **202/202 PASS** |
| kernel module build | project 6.18.60 tree, `-Werror`, modpost | PASS, 0 warnings/errors |
| artifact evidence | `modinfo` / `sha256sum` / `nm` | see table above; `h2d_send_budget` present in modinfo |
| driver selftest | `python3 tools/selftest.py` | **226/226 PASS** (133 inherited + 93 H2D checks) |
| firmware handler analysis | `.provision-venv/bin/python tools/fw_g0id1_handler.py --selftest` | **38/38 PASS** |

The driver selftest additions check: the H2D source/Kbuild layout, main.c
wiring, the h2d_frames library byte-identical provenance, the six EXACT
builders, the txq_sync72 node-length variant (emit plan, 72-byte pad, node
word1 `(72<<16)|0x6d2b`, same gate block, sysfs attr/grammar token), the
ordered ctx_create frame (selector, constants, payload type/vap/user,
config-event builder, READY-only/once gate reasons, grammar token, sysfs attr,
telemetry, plan/status exposure, ordered-TXQ exception), the
boot/phase/one-frame/consumption gate strings, exactly one
`hcc_core_sr_commit` call site (no retry), no direct mailbox/gate/out[5]
write, the bounded scheduled-sleep poll, the `h2d_send_budget` parameter, the
telemetry fields (incl. the recorded node length), and **file-by-file parity
with the reference wifi-driver-clean tree** (exactly five files modified:
main.c, hcc.c, service.c, hi5622_hcc.h, Kbuild; the new h2d_* files are new
in this lane and absent from the reference tree).

## Build-time notes

* `CONFIG_MODULE_STRIPPED` is handled by the copied main.c (module metadata
  emitted unconditionally), unchanged from the clean driver.
* The STAGING_DIR warning from the OpenWrt GCC wrapper would otherwise produce
  10 spurious compiler warnings; `tools/build.sh` exports it and still counts
  zero warnings.
* `h2d_frames.c` / `h2d_frames.h` / `h2d_catalogue.inc` are byte-identical
  copies of the wifi-h2d-frames lane deliverable (checked by the selftest when
  that lane tree is present); `hcc_core.*` are byte-identical to the
  wifi-hcc-core lane.
* No image build, no world build, no boot claim: this is an out-of-tree module
  against the already-built 6.18.60 tree.  Nothing in this lane contacted the
  device; the first load is the attended session in BENCH-PROCEDURE.md.