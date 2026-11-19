# incoming/qm-decode - LSW EQM queue-manager decode + W7 init write stage (patch 032)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-19.
**Status:** HOST-ONLY DRAFT. Nothing in this package has been executed on the board. There was
**no device access** of any kind (no SSH/telnet/TFTP/flash/UART); the router stayed on the
EPS-gate image (patch 031) and nothing was reflashed. Publication remains NOT READY.

This directory is an **incoming** deliverable: it carries no direct edit to a shared repository
file. The kernel patch is already in
`target-skeleton/target/linux/hisilicon/patches-6.18/` (direct lane write scope). Apply
`shared.patch` from the `opensource/` root for the documentation changes, then move this SUMMARY
to `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` (or append it to `port-notes/lsw/SUMMARY.md`).

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/032-net-lsw-eqm-qminit.patch` | fifth attended LSW write stage (`luofu_lsw.qminit=1`, requires `arm=1 egress=1 qidmap=1 eps=1`): the recovered EQM queue-manager init tables 19/32/35/17/16/34 for the nine wired queue groups (ports 4..12, qids 1..72, 169 entries / 845 words) with pre-write conformance, pre-write snapshot, computed readback expectations, restore-on-mismatch; extends readiness so GEMAC only registers on the complete W3-W7 write set; also corrects the patch-025 EQM indirect error bit and restores the W6 EPS words on failure/remove | `c8e4c7a342cf5a5cfdecbb4af37f34f07f822492880e175ee86c8ce4a7d54062` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | `docs/LSW-SPEC.md` (E1 row status, 7.8 write-set update, new section 7.9 W7 decode, 12.3 W6/W7 campaign rows, 12.4 risks), `docs/SKELETON-SMOKE.md` (new LSW QM/EQM session: gate matrix, validation, proof split, attended sequence), `target-skeleton/README.md` (032 row) | `0c45ce939399e30c1bf88c934b029845c0fb1ba4abe5df1acf95596b2438ea51` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file (moved to `port-notes/qm-decode/SUMMARY.md` on integration) | - |

`shared.patch` applies to the current `opensource/` working tree with `patch -p1` (3 files,
237 diff lines); it does not touch the DTS, the binding, `config-6.18` or any other lane's files. Patch
032 changes no devicetree/binding/Kconfig file either, so no DTS/binding/config change is required.

## 2. What the decode recovered (evidence map)

All of it is **read-only host disassembly of the pinned fixture**
`rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko` (67,164 B, sha256
`f0010305caf15c6da71981b7c4b51c7f9c0ce55c1911b0ca031a44892fdcd535`). It is the same fixture the
LSW-egress lane used; no vendor binary was added to the tree, and the disassembly helper lives
only in the gitignored wave directory.

### 2.1 The EQM indirect-table engine (both directions) [V code shape]

From `hi_eqm_write_tab@0x1cdc` / `hi_eqm_read_tab@0x1e18`:

| command word -> `eqm+0x48` | field |
| --- | --- |
| bits 15..0 | table index |
| bits 26..20 | table id (7-bit, from `tab & 0x7f`) |
| bit 30 | 1 = write, 0 = read |
| bit 31 | start/busy: command sets it, the driver polls until it clears |
| bit 29 | status: operation error |

- write data words 0..4 -> `eqm+0x5c/+0x60/+0x64/+0x68/+0x70`; read data words 0..4 <-
  `eqm+0x4c/+0x50/+0x54/+0x58/+0x6c` (the fixture's `g_eqm_tab_ba` pairs at `.rodata+0x220`);
- **one operation moves at most 5 data words** (the vendor clamps its 8-word call buffers to 5);
- 1001 poll iterations then a timeout error `0x8fffffd8` / error `0x8fffffd3`.

**Independent measured corroboration [V].** The 2026-11-18 read-only inventory captured the
idle `eqm+0x48 = 0x41100012` and `eqm+0x5c = 0x00000bb8`. Decoding that word with the table
above gives table ((0x41100012 >> 20) & 0x7f) = 17, index (0x41100012 & 0xffff) = 18, write
bit set, start cleared, no error; and 0xbb8 = 3000 is exactly the rate of the vendor's table-17
profile write for ports 5..7. That is the vendor's **last queue-init operation** (hi_queue_init
programs ports 4..7 last) and it validates both the command-word decode and the table-17 shape
without any guess. It also corrects patch 025's driver header, which had recorded the error bit
as bit 5; the vendor tests (status >> 24) & 0x20, i.e. bit 29 (LSW-SPEC 7.5 already said bit29).

### 2.2 The vendor's queue-group table and the W7 write set

The vendor chip-init is `hi_queue_init@0x507c` (called from `hi_dp_probe@0x8b0` after the
dp/eqm/ips probe configuration) plus the group helper `0x31d4` and per-qid helper `0x2b40`.
It walks the devicetree `port_bitmap` (`0xf01 0x1ff0` -> bits {4..12, 32, 40..43}), allocates
one contiguous qid run per populated group from a build-time 46-entry queue-group table in
**`.data+0xd0`** (46 x 56 B: +4 port, +8 base qid, +12 q_num, +16 profile base, +20 profile
selector, +24 scheduler id, +28 rate, +32 queue-port, +36 slot base, +40 ...), and programs:

| # | table, index | write shape | words | value source |
| --- | --- | --- | --- | --- |
| EQM-1 | 19, 0 | 0xff-filled buffer, word 1 low14 = 1000, 5 words written | {0xffffffff, 0xffffc3e8, 0xffffffff, 0xffffffff, 0xffffffff} | vendor constant [V] |
| EQM-2 | 32, P (P = 4..12) | read 5, byte0 = P*8, 3-bit slot priorities 7..0 in word 1 bits 0..23, other bits preserved | 5 | vendor build-time field +8 [V]; helper 0x20f8/0x2184 |
| EQM-3 | 35, qid | 5 words, byte 2 = 7, rest 0 | {0x00070000, 0, 0, 0, 0} | vendor constant [V] |
| EQM-4 | 17, profile | 5 words: rate&0x1fff, 0x10000000, 0xffff0000\|rate, (sch&0x1ff)<<20\|cells, 0x07f03fff; cells = rate if (eqm+0x38&0x3fff) < rate else rate/2 | 5 | vendor formula [V]; helper 0x24d0 |
| EQM-5 | 16, qid>>4 | read 5, byte (qid&15) = 0x80 \| (profile&0x1f), bits 6:5 preserved | 5 | vendor formula [V]; helper 0x223c |
| EQM-6 | 34, qid | read 5, word0 = queue-port P in bits 5:0, slot in 17:14, bit13 = 1, bit18 = 1, other bits preserved | 5 | vendor formula [V]; helper 0x22ec |

Per-group constants for the nine wired groups (vendor build-time table rows 2..10, i.e. the
groups the measured qid map proves are populated):

| port | profile base | profile selector | scheduler id | rate |
| --- | --- | --- | --- | --- |
| 4 | 0x10 | 1 | 0x20 | 3000 |
| 5 | 0x10 | 2 | 0x20 | 3000 |
| 6 | 0x10 | 2 | 0x20 | 3000 |
| 7 | 0x10 | 2 | 0x20 | 3000 |
| 8..12 | 0x00 | 8 | 0x20 | 2500 |

`profile(port, slot) = base + (selector <= 7 ? selector : slot)`;
`qid(port, slot) = 1 + port_index*8 + slot` with port_index the position of the port in the
populated-port allocation model patches 021/022 validate against the measured qid maps. That
gives qids 1..72 and profiles {0..7, 17, 18} -> **169 entries / 845 words** (1 table-19 + 9
table-32 + 72 table-35 + 10 table-17 + 5 table-16 + 72 table-34).

**Vendor table rows actually used** (dump reproduced with the wave's `dump-tables.py`; ports 4..12 are
entries 2..10 of the vendor's .data+0xd0 table):

| entry | port | q_num | profile base (+16) | profile sel (+20) | sch (+24) | rate (+28) | queue-port (+32) | slot base (+36) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2 | 4 | 8 | 0x10 | 1 | 0x20 | 3000 | 4 | 0x20 |
| 3 | 5 | 8 | 0x10 | 2 | 0x20 | 3000 | 5 | 0x28 |
| 4 | 6 | 8 | 0x10 | 2 | 0x20 | 3000 | 6 | 0x30 |
| 5 | 7 | 8 | 0x10 | 2 | 0x20 | 3000 | 7 | 0x38 |
| 6 | 8 | 8 | 0x00 | 8 | 0x20 | 2500 | 8 | 0x40 |
| 7 | 9 | 8 | 0x00 | 8 | 0x20 | 2500 | 9 | 0x48 |
| 8 | 10 | 8 | 0x00 | 8 | 0x20 | 2500 | 10 | 0x50 |
| 9 | 11 | 8 | 0x00 | 8 | 0x20 | 2500 | 11 | 0x58 |
| 10 | 12 | 8 | 0x00 | 8 | 0x20 | 2500 | 12 | 0x60 |

(The static base-qid column of those entries is a build-time placeholder; the runtime allocator
assigns the contiguous model from qid 1, which is what patches 021/022 validate and what W7
uses.  The field names are descriptive; the vendor semantics of +16/+20/+24/+28 are [U].)

Vendor build-time constants used by the table-17 formula: `g_eqm_tab_ba` (read data words 1..4 from
`+0x50/+0x54/+0x58/+0x6c`, word 0 from `+0x4c`; write words 1..4 to `+0x60/+0x64/+0x68/+0x70`,
word 0 to `+0x5c`) and `.data..read_mostly` = {0x800, 2, 0x18} (the 2 is the formula's
minimum divider).

### 2.3 How a MAC TX frame gets an egress port (what is now closed, what is not)

- **Per frame the LSW is still written zero times** (unchanged from the LSW-egress decode). The
  frame reaches the engine as the 32-byte PIE TX descriptor: `+8` bits 3:0 = egress port
  number, `+8` bits 26:11 = `1 << port`, `+17` = vport, and **no qid and no queue
  handle**. The measured bench TX descriptor from patch 031 (port 11, len 90) is
  `7400005a 82b5a002 3040000b 00010020`: word 2 = 0x3040000b has port 0xb in bits 3:0 and
  `1 << 11 = 0x800` in bits 26:11 - exactly the decoded tag.
- **The vendor's per-port queue handle is zero in this build** (PIE per-port context +0 is never
  written; LSW-egress decode [V]), so the only egress identity the frame carries is the port
  tag/bitmap.
- **New:** the queue-manager side is no longer a blank. The vendor programs a queue-port/deque
  record for every populated port (table 32) and a per-qid deque map qid -> (queue-port, slot,
  input, valid) (table 34) at init, before any frame flows. What remains [U] is the silicon
  selection: whether a tagged descriptor is dequeued straight through the port tag or needs the
  excluded records (table 38, the ports 40..43 VP class). A W7 boot is the first experiment that
  can distinguish the two: either the ping egresses or it does not.

## 3. Gate model (fail closed; default boot unchanged)

| boot parameters | switch driver (021/022/025/031/032) | GEMAC netdevs (023) |
| --- | --- | --- |
| none | read-only baseline/probe/refusal; no write | none |
| `luofu_lsw.arm=1` | W3 power + snapshot; refuse; no LSW register write | none |
| `arm=1 egress=1` | W3+W4 verified; qid map absent -> restore, power down, refuse | none |
| `arm=1 egress=1 qidmap=1` | W3+W4+W5 verified; eps absent -> restore, refuse | none |
| `arm=1 egress=1 qidmap=1 eps=1` | W3-W6 verified; qminit absent -> restore, refuse | none |
| `arm=1 egress=1 qidmap=1 eps=1 qminit=1` | W3-W7 all verified -> `luofu_switch_ready()` true | requires `luofu_pie.arm=1` too |
| any write gate without `arm=1` | explicit `-EINVAL` refusal | none |

`luofu_switch_ready()` is now complete-write-set based (W3+W4+W5+W6+W7), so the GEMAC
front end cannot register a netdev on a partial data path; its refusal message names the whole
chain. The W7 stage itself: refuses unless the EQM indirect interface is idle; reads every
target entry twice and refuses if an entry is not stable; snapshots all 169 entries; computes
the expected content from the snapshot plus the vendor formulas; writes; reads back; restores
the whole snapshot on the first mismatch (also from `remove()`), and powers the block down on
any probe failure.

## 4. Validation (isolated clone under `build/linux-kbuild/waves/2026-11-19-qm-decode/`, no device)

- **Fixture:** `rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko`, 67,164 B, sha256
  `f0010305caf15c6da71981b7c4b51c7f9c0ce55c1911b0ca031a44892fdcd535` - the same pinned fixture the
  LSW/pie lanes used; no vendor binary was added to the repository.
- **Series apply (001-032):** pristine `linux-6.18.60` copy + all 32 patches in order: **32/32
  applied, 0 failed hunks, 0 .rej/.orig files**, and the resulting
  `luofu-switch.c`/`luofu-switch-regs.h`/`luofu-gmac.c` are byte-identical to the lane work tree
  (`logs/30-series-apply-001-032.log`, sha256 `6a9f15b2567689ede8800c60c6186f0ca2b23c7d2c416f53831917067b23cdfa`).
- **Isolated clone prepare/compile:** a reflink copy of the 001-031 selectorfix OpenWrt lab under
  the wave directory (the shared clone and `openwrt-lab-selectorfix-img` were **not modified**) with
  patch 032 added to `target/linux/hisilicon/patches-6.18/`:
  `make target/linux/prepare` **RC=0** (log sha256
  `4fe411715bb1f2f9db93394005ed5a29e1e6a6cb4dd2ab5e67559cbdf170eb42`) and
  `make target/linux/compile` **RC=0** in 777.8 s wall (log sha256
  `8c48155587718b8a3b48f8a3db772c80de360e4f27879b91951fccae5bed98f6`) with `CONFIG_WERROR=y` and
  **zero `warning:` and zero `error:` lines in the whole build log**, including the two changed
  drivers (`luofu-switch.o` 98,292 B, `luofu-gmac.o` 51,768 B); `vmlinux` linked at
  57,096,140 B, sha256 `687cf391209bc57cd64f02174661fbb13d15526829940d08dc3c7c8ad9a8ae06`. The prepared clone
  tree is byte-identical to the lane work tree, i.e. the compile validated the delivered patch.
- **checkpatch:** 0 errors, 0 checks, and the one expected `COMMIT_MESSAGE` warning that every
  pure unified diff in this series produces (`logs/31-checkpatch-032.log`).
- **dt_binding_check:** targeted to the 13 `hisilicon,luofu-*` bindings of the series: **RC=0**
  (dtschema 2026.9; the only note is the host's missing optional `yamllint`) -
  `logs/20-dt-binding-check-luofu.log`. Patch 032 changes no binding, so this re-validates the
  001-032 tree state rather than a binding change.
- **Board DTS:** the skeleton board DTS (`target-skeleton/dts/luofu-r116.dts`) compiles with cpp +
  dtc with **0 errors and 0 warnings** (`validation/board-dts.dtb`, 12,076 B).
- **config:** patch 032 adds **no config symbol** (it changes no Kconfig/config file);
  `make kernel_oldconfig` runs **RC=0 twice with byte-identical output** (sha256
  `e7d38aae12e422efa42217eaedd2b4d908d6ee16ac273be1ec0b257b780c7ab5`), i.e. the 001-032 tree's target config
  is idempotent (`logs/22-config-idempotence.log`). The only symbol delta between that
  regeneration and the delivered skeleton `config-6.18` is the lab image's attended-gate
  `CONFIG_CMDLINE` line and the previously documented `CONFIG_XZ_DEC_BCJ` oldconfig
  resolution - neither is a patch-032 change.
- **Targeted driver compile:** with `W=1`, `luofu-switch.o` and `luofu-gmac.o` rebuild
  **RC=0 with no warnings** (`logs/32-targeted-compile.log`).
- **Not changed / not re-validated by 032:** patch 032 touches no devicetree, binding, Kconfig,
  config-6.18 or image file, so no other lane's validation is invalidated; the rest of the series
  is unchanged from 001-031.

## 5. Proof vs unverified

**Proven (host-side):** the W7 write set is the vendor's own queue-init write shape for the nine
populated wired queue groups; the indirect engine protocol is decoded and corroborated by the
measured idle register state (table 17 / index 18 / 3000); the value derivations, conformance
checks, snapshot/restore paths and the five-gate composition are reviewable code; the 001-032
series applies and compiles clean; checkpatch 0 errors.

**Unverified / open:** every hardware claim. No W7 write has ever been executed; the vendor
table *field semantics* are [U] (the values are typed from the vendor code, not understood); the
pre-write state of our own block is a fresh/uninitialised block (the 2026-11-19 W3 snapshot
showed dp+0x1c = 0, eqm+0x48 = 0, eps channel words = 0: the loader does not run the vendor
queue init), so the readback proves the write path, not that the block reached the vendor's
post-boot state; table 38 (per-qid scheduler state, needs the vendor table-manager id) and the
ports 40..43 VP class are excluded; the IFC/L2/VLAN tables, the queue/flow DDR layout, the RX
path and the final forwarding result remain open. A successful W7 boot must not be read as
"the link works".

## 6. Risks and mitigations

| risk | severity | mitigation |
| --- | --- | --- |
| W7 writes 169 EQM entries whose field semantics are [U]; a wrong-but-self-consistent write passes readback | med | values are the vendor's own init writes; pre-write interface-idle + two-read stability conformance; full snapshot restore on any mismatch; fail-closed + power-down on refusal; attended boot with recovery armed |
| EQM indirect interface hangs the engine (busy never clears) | med | 1001-poll timeout bound per operation; the stage restores the snapshot and fails; small write set (169 operations) |
| table 38/scheduler state missing -> queue never schedules -> still no egress | med | documented as the first thing to check on the attended run; not guessed; the vendor's measured last write (table 17/18) proves shaping profiles are in the set we do program |
| readiness now requires five gates: an operator may boot 4 of 5 and see a refusal | low | every refusal log names the missing gate and the exact parameter; SKELETON-SMOKE has the sequence |
| the block state does not survive the write set (wedge) | high | attended only, recovery door armed, physical RESET+TFTP is the only recovery; power-cycle between variants; no reflash with this patch |

## 7. Attended sequence (recovery card armed; physical RESET+TFTP is the only recovery)

1. **W3 only:** `luofu_lsw.arm=1`. Expect the W3 snapshot line and the refusal.
2. **W3+W4+W5:** `arm=1 egress=1 qidmap=1`. Expect the R1/R3/R5 and qid-map verified lines,
   then the refusal naming the missing EPS gate.
3. **W3-W6:** `arm=1 egress=1 qidmap=1 eps=1`. Expect the W6 line and the refusal naming the
   missing QM gate.
4. **W3-W7 + PIE:** `arm=1 egress=1 qidmap=1 eps=1 qminit=1` plus `luofu_pie.arm=1` (and
   `luofu-gmac` netdevs). Expected console sequence: W3 snapshot -> W4
   "probe configuration programmed and verified" -> W5 "qid map programmed and verified" ->
   W6 "EPS channel-map/companion words programmed and verified" -> W7 "W7 snapshot: 169 EQM
   table entries (845 words) read and stable, indirect interface idle" -> W7 "programmed and
   verified" -> "connectivity path armed" -> GEMAC registration lines. Then: ping the host over
   the bench link while watching the PIE TX read pointer and the switch/EPS drop counters; one
   variable per boot; power-cycle between variants.
5. Any wedge or hang: physical RESET+TFTP only; there is no in-kernel reset path on this image.

## 8. Unverified list and next attended step

Unverified: all hardware behaviour of W3-W7; table-38 scheduler/buffer state; the ports 40..43
VP class; IFC/L2/VLAN; queue/flow DDR layout; RX delivery; whether a tagged PIE TX descriptor
egresses once the queue manager is configured; whether the loader/eFuse state leaves any hidden
precondition for the EQM tables.

**Next attended step:** the 4-step sequence above, all on one recovery-armed session, with the
final boot carrying all five gates plus `luofu_pie.arm=1`, then a single ping while capturing UART
and the TX/read-pointer + drop counters. Success criterion: the W7 readback log appears and a
frame is seen on the wire (or the counters prove where the engine drops it); failure criterion:
any readback mismatch (the stage restores and refuses) or a wedge (power-cycle + recovery).

## 9. Reproduction

```sh
# fixture (read-only; not in the tree)
sha256sum rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko
# -> f0010305caf15c6da71981b7c4b51c7f9c0ce55c1911b0ca031a44892fdcd535

# addend-resolving annotated disassembly of the pinned fixture (gitignored wave dir)
.provision-venv/bin/python build/linux-kbuild/waves/2026-11-19-qm-decode/ann.py \
  rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko \
  build/linux-kbuild/waves/2026-11-19-qm-decode/hi_lsw_dp.addr.dis

# the build-time queue-group table (.data+0xd0, 46 x 56 B) and g_eqm_tab_ba
.provision-venv/bin/python build/linux-kbuild/waves/2026-11-19-qm-decode/dump-tables.py

# patch series + compile validation (isolated clone)
ls build/linux-kbuild/waves/2026-11-19-qm-decode/logs/
```

## 10. Integration notes

- The kernel patch is in the skeleton's `patches-6.18/` (032) and must be applied after 031; it
  modifies `luofu-switch.c`, `luofu-switch-regs.h` and `luofu-gmac.c` only, and is file-disjoint
  from every other lane's patches.
- `shared.patch` applies on top of the current `opensource/` HEAD; if another lane's
  shared.patch lands first, only the README patch-table row and the LSW-SPEC 7.9 anchor need a
  merge.
- No vendor binary, no device artifact and no per-unit value is in this package.
