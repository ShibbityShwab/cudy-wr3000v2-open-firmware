# BUILD-NOTES — hi5622_pciet_b3.ko (B3 bench instrument)

**Lane:** b3-instrument (task-66, host-only). **Date:** 2026-11-22.
**Base:** full copy of the clean-room transport skeleton in ../build/
(wifi-transport-port lane) + the B3 additions hi5622_b3.h / pciet_b3.c.
**Never loaded on a device by this lane** — the bench run is the Lead's attended session
(procedure: [B3-PLAN.md](B3-PLAN.md)).

## Result

| item | value |
| --- | --- |
| module | `hi5622_pciet_b3.ko` |
| **sha256** | `0a25d44849e9216b710b94e6df686d150551be804260f2bafb854a3da580718b` |
| size | 179,200 B |
| ELF | 32-bit LSB relocatable, ARM, EABI5, BuildID sha1 `724e8505…` (debug_info, not stripped) |
| license | GPL |
| **vermagic** | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| depends | (none) |
| module params | `prepost` (uint, B3), plus the base params `poll_ms`, `twin_enabled`, `use_irq`, `fw_path` |
| PCI alias | `pci:v000059E7d00000005sv*sd*bc*sc*i*` |
| build cleanliness | **zero compiler errors/warnings, zero modpost warnings** (`CONFIG_WERROR=y` tree) |

The `.ko` is built evidence only (kept at
`/home/shibbityshwab/wifi-transport-b3-work/build/hi5622_pciet_b3.ko`); it is **not committed**
(the repo ignores `*.ko`; no binary/binary-derived artifact is added to tracked text).

The sha256 above is for that canonical build directory. An out-of-tree module embeds its build
path in the symbol table (`STT_FILE` entries), so a rebuild in another directory produces a
different `.ko` hash; the code is identical — the committed sources were rebuilt from a
`git archive` copy and `.text`, `.rodata`, `.data` and `.modinfo` compared byte-for-byte
with the canonical build (all identical, zero warnings).

## Build line (module only — NEVER an image / "make world" build)

    export KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    make ARCH=arm CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- \
         -C $KDIR M=$(pwd) modules

(`make` in this directory runs exactly that line; `make clean` removes build products.)

Compiler: `arm-openwrt-linux-muslgnueabi-gcc (OpenWrt GCC 14.4.0 toolchain-c0e63334e4) 14.4.0`.
Kernel tree: the built luofu 6.18.60 tree above. Clean build 2026-11-22 18:41 (+07).

### modpost note

This kernel config sets `CONFIG_MODULE_STRIPPED=y`, so `MODULE_DESCRIPTION()` /
`MODULE_AUTHOR()` are compiled out of `.modinfo` and modpost emits a spurious
"missing MODULE_DESCRIPTION()" notice for **every** module (the base transport module shows it
too). `pciet_b3.c` keeps its description in a non-stripping `MODULE_INFO(description, …)` tag so
the B3 build is warning-clean; `MODULE_LICENSE` remains in `pciet_main.c`.

## Files

| file | change vs `../build/` |
| --- | --- |
| `hi5622_pciet.h` | copy **+ one marked field** `struct hi5622_b3 *b3;` in `struct hi5622_pci` and its forward declaration |
| `pciet_main.c` | copy **+ marked hunks**: `-DHI5622_B3_BUILD` selects driver name `hi5622_pciet_b3`; include `hi5622_b3.h`; `pciet_b3_probe()` in probe, `pciet_b3_remove()` in remove |
| `pciet_ete.c` | **verbatim copy** (B3 reuses `pciet_ete_ring_alloc/free` only) |
| `hi5622_b3.h` | **new** — B3 contract constants/structs (HCC 12-B header, mailbox ack/rearm, channel sets) |
| `pciet_b3.c` | **new** — the whole instrument: P0 arm, P1 D2H service/watch, P2/P5 DR snapshots, P3 HCC decode, P4 state, sysfs surface |
| `Kbuild` | `obj-m := hi5622_pciet_b3.o` = `pciet_main.o pciet_ete.o pciet_b3.o`; `ccflags-y += -I$(src) -DHI5622_B3_BUILD` |
| `Makefile` | convenience wrapper for the exact build line above |
| `b3-transport.patch` | the change list vs the main transport dir: modified files diffed from `../build/`, new files as `/dev/null` creations. Reproduce the tree with `cp -a ../build b3 && patch -p0 < b3-transport.patch` |
| `selftest-idx-arith.c` | host-side unit test of the packed ring-index arithmetic (see below) |
| `BUILD-NOTES.md`, `B3-PLAN.md` | this file and the attended bench plan |

**The main transport directory `../build/` was not modified** (another lane rebases there).

## What B3 adds (contract surface)

Built against `opensource/docs/incoming/wifi-hcc-re/REPORT.md` §4 (ETE), §5.1/5.2/5.4
(mailbox sequence), §8 (driver contract), §9 (probes P0-P5), and `SUMMARY.md`:

1. **P0 - arm all rings**: SR0-2 (group A: `+0x10` base, `+0x14` depth-1, `+0x18` producer,
   `+0x08` ctrl=0) and DR0-3 (group B: `+0x30` base, `+0x34` depth-1, `+0x38` published free
   index), 32 x 8-B nodes, `dma_alloc_coherent`, packed index `(idx & 0x3ff) | phase << 10`.
   Firmware-owned `+0x00` enable, DR `+0x08`, SR `+0x1c`, DR `+0x3c` are **never written**
   (read back only). Refuses a second arm in one module load.
2. **P1 - D2H service**: event-driven contract order (probe-read `out[1]`; if zero -> **no
   device writes at all**; else ack `out[3]=1` -> re-read/drain `out[1]=0` -> re-arm
   `out[4]=1` -> dispatch lowest bit first ids 0-10). A 30 s watch window logs every non-zero
   `out[1]` (raw word + per-bit counters, bits **3** and **6** called out) and runs a DR
   `+0x3c` fill watchdog that catches deposits arriving without a mailbox notify.
3. **P2/P5 - DR completion**: read `+0x30/+0x34/+0x38/+0x3c/+0x00` per channel, compute
   completed nodes with the vendor pointer arithmetic, decode up to 8 nodes
   (`word1>>16` = len, `word0` = buffer device VA; 12-B payload prefix), return buffers by
   advancing `+0x38` (drain) or leave everything untouched (read-only snapshot). P5 dumps node
   `word1` + first 16 B of each completed buffer for the "deposited but zero nodes" case.
4. **P3 - HCC group-4 ready gate**: decode the 12-B HCC header
   `{group@0, rsrc@1, len=payload+12@4, id@6, dst@9, 0x5a5a tag@0xa, payload@0xc}`;
   flag `chip group4 id1` (`device_plat_ready_msg_process`) and `id2`
   (`host_ready_msg_process`, payload must be <= 0x63 B) — the two vendor init waits.
   Records payload bytes and logs a full hex dump.
5. **P4 - post-release state (read-only)**: `out[0]` H2D pending, `out[1]`, `out[2]`,
   `out[5]` (read-only — never written), D2H doorbell, ETE intr word, per-channel `+0x00`
   enable and ring indices; confirms the firmware re-asserted its channel enables and that no
   H2D is pending.
6. **Safety**: compile-time asserts + a runtime gate on every writer refuse CA `0x400392f0`;
   no H2D is posted (the contract requires no reply and device id 5 is a `BUG()` trap); the
   base `send`/`ringinit`/`poll` attrs are untouched and P0 refuses to arm twice.

## Validation performed (host-side only)

- Clean module build against the real 6.18.60 tree, zero warnings/errors (`CONFIG_WERROR=y`),
  modpost clean; ELF ARM, `modinfo` vermagic as above.
- All device-written constants pass `BUILD_BUG_ON` gates that no writable offset resolves to
  `0x400392f0`; the two mailbox write CAs are asserted against the base header values.
- Host unit test of the packed ring-index arithmetic
  (`selftest-idx-arith.c`, exact copies of `b3_idx_plus()` / `b3_idx_count()`):

      gcc -O2 -Wall -Wextra -o /tmp/idx_test selftest-idx-arith.c && /tmp/idx_test
      -> "count+walk consistency over 64 start positions x 33 deltas: OK; ALL OK"

  It pins the P0 pre-post value (32 publishes -> `0x400`), the full-ring completion count (32),
  the one-node wrap crossing, and walk-consistency for every reachable packed pair — i.e. the
  arithmetic the DR drain loop relies on.

## Not done / not proven by this lane

- The module has **never been loaded on a device** (host-only lane; no bench contact).
- No H2D message is sent by B3 (out of P0-P5 scope; the base `send` smoke attr still exists but
  is not used by the B3 procedure).
- IRQ delivery: still the polled path (the luofu host driver cannot demux INTx yet).
- The [U] unknowns of the contract remain [U] until the bench run; `B3-PLAN.md` lists them and
  which probe pins each.
