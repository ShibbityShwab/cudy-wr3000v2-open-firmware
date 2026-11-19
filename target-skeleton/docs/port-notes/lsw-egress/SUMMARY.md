# incoming/lsw-egress - LSW TX egress decode + R1/R3/R5 probe-configuration stage (patch 025)

**Lane:** `lsw-egress`, shared task `task-57`, 2026-11-19.
**Status:** HOST-ONLY DRAFT. Nothing in this package has been executed on the board. There was
**no device access** of any kind (no SSH/telnet/TFTP/flash/UART); the router was not touched.
Publication remains NOT READY.

This directory is an **incoming** deliverable: it carries no direct edit to a shared repository
file. The kernel patch is already in
`target-skeleton/target/linux/hisilicon/patches-6.18/` (direct lane write scope). Apply
`shared.patch` from the `opensource/` root for the documentation changes, then move this SUMMARY
to `target-skeleton/docs/port-notes/lsw-egress/SUMMARY.md` (or append it to
`port-notes/lsw/SUMMARY.md`).

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/025-net-lsw-egress-luofu.patch` | third attended LSW write stage (`luofu_lsw.egress=1`, requires `arm=1`): pre-write conformance, the vendor R1/R3/R5 probe-configuration writes (dp/ips/eqm), readback against values derived from the pre-write snapshot, restore-on-mismatch, ready = probe config + qid map; R4/`fam` read-only; EQM queue tables excluded | `0fd8b1eafd701703c785d087f864d27142335ee335c60517e390a21773e39ce7` |
| `target-skeleton/docs/incoming/lsw-egress/shared.patch` | `docs/LSW-SPEC.md` (R1/R3/R5 rows, EQM command-word decode, `port_bitmap` decode, new section 7.8 TX egress, W-table/risks/summary/unknowns), `docs/SKELETON-SMOKE.md` (new LSW egress session: gate matrix, attended sequence, proof split), `target-skeleton/README.md` (025 row + session blurb) | `bbf101094636514bdcbc2200a7af7526974a7a5349ad67e16c78a94f3152e893` |
| `target-skeleton/docs/incoming/lsw-egress/SUMMARY.md` | this file (moved to `port-notes/lsw-egress/SUMMARY.md` on integration) | - |

`shared.patch` applies to the current `opensource/` working tree with `git apply --check`
**RC=0** (3 files, 457 lines). It does not touch the DTS, the binding, `config-6.18`, the image
recipe or any other lane's files; patch 025 does not change the devicetree, so no DTS/binding
change is required.

## 2. What the session decoded (evidence grade per item)

### 2.1 How a TX frame gets to a switch egress port

- **Per frame: zero LSW register writes.** No vendor path issues `hi_lsw_write()` per frame; the
  only `hi_lsw_*` writes in `hi_lsw_dp` are probe/queue-init and the qid-map hook, and
  `hi_kpie`'s TX path only writes PIE doorbells/descriptors. [V code shape]
- **The frame reaches the LSW as a 32-byte PIE TX descriptor**, produced by
  `hi_pie_tx_set_desc` (v250 ops table). The vendor request built in `hi_pie_drv_xmit@0x42ac`
  carries: descriptor `desc8` bytes 0-3 (`{port, vport, ...}` from the MAC netdev) at request
  `+8..+11`; the per-port queue handle (PIE per-port context `+0`) at request `+14` bits 4..;
  skb type/length/address fields elsewhere. [V code shape]
- **The queue handle is zero in this build.** `hi_net_dev_probe` initialises context `+0` to 0,
  `hi_net_dev_open` only sets the up flag at `+4`, and no module writes it ([V] all call sites).
  So the only egress identity the vendor MAC TX descriptor carries is the port/vport word pair.
- **The init-time queue/egress state is EQM indirect-table programming, and it is NOT in our
  write set.** `hi_queue_init@0x507c` writes EQM tables 19 (2-word cell-count record
  `{0xffffffff, 0xffffc3e8}`), 16 (16 qids/row, 5-bit selector + valid bit), 32 (per queue-port
  byte 0 = port id + 3-bit per-slot priorities), 34 (per qid: 6-bit queue-port, 4-bit slot,
  valid), 35/37/38 (per-qid scheduling/shaping) plus direct words `eqm+0x18/0x38/0x40` (row R3).
  [V code shape of the writers; table contents/meaning U] `hi_queue_enable`/`qid_map_set` at MAC
  netdev probe program the port-to-qid rows that were measured (7.3). The qid-to-egress EQM
  table *contents* needed for our own block are still [U] (row E1).
- **What is therefore still unverified:** whether the engine uses the descriptor port/vport
  word directly as the egress port, or selects through an EQM per-queue entry. That is a
  silicon question; software cannot answer it, a bench trace or GPL source can. The reviewed
  write set (W3/W4/W5) is a *verification* step, not a proven data path.

### 2.2 The R1/R3/R5 probe-configuration write set (minimum init for a TX attempt)

Exact sequences recovered from `hi_dp_probe@0x8b0..0xd1c` and `hi_queue_init@0x507c`, in the
order the vendor writes them; measured post-boot values are the 2026-11-18 baseline:

| write | value / derivation | evidence |
| --- | --- | --- |
| `ips+0x10` | `found \| BIT(16)` | code shape [V]; measured bit set (0x00790011) [V] |
| `ips+0x08` | `42` (`0x2a`) | code shape [V]; measured [V] |
| `dp+0x68` | `0` | code shape [V]; measured [V] |
| `dp+0x64` | `1` then the cell-size selector (`1` = 0x200-byte cells) | code shape [V]; measured 1 [V]; two-write latch meaning [I] |
| `dp+0x1c` | `(u16 queue DDR base \| BIT(16))` = `0x00018000` on this board | code shape [V]; u16 is the checked DT `queue` window base low half [V]; measured [V] |
| `fam+0x504` | **read-only conformance**: low14 must equal `(queue_size / cell_size) & ~7` = `0x2000`; vendor only lowers it (clamp case [U], not replayed) | code shape [V]; measured 0x2000 [V] |
| `eqm+0xa4` | `(found & ~0x3fff) \| ((fam_low14 - 0x40) & 0x3fff)` = `0x1fc0` | code shape [V]; measured [V] |
| `eqm+0x18` | `(found & ~0x80003fff) \| ((eqm+0xa4 + 0x3d00) & 0x3fff) \| BIT(31)` = `0x80001cc0` | code shape [V]; measured [V] |
| `eqm+0x38` | `(found & ~0x3fff) \| ((eqm+0xa4 & 0x3fff) >> 2)` = `0x7f0` | code shape [V]; measured [V] |
| `eqm+0x40` | `found \| BIT(31)` = `0x80000000` | code shape [V]; measured [V] |

Every expectation is computed from the pre-write snapshot with the vendor formulas, so the
readback tests the write path and not an invented value. The register *semantics* of every word
remain [U]; only the vendor write shape and the measured values are established.

### 2.3 Two side decodes closed in the spec

- **`port_bitmap` decode [V].** `of_property_read_u64()` assembles `cell[0] << 32 | cell[1]`, so
  the in-memory words on the little-endian target are `0x1ff0` (bits 4..12) then `0xf01` (bits
  32, 40, 41, 42, 43). The `hi_queue_init` walk visits ports **{4..12, 32, 40..43}**; that is the
  only reading that reproduces the measured qid map word for word (ports 4..12 qids 1..72, port
  32 q_num=0 consumes no qid, ports 40..43 qids 73..104). The property is the queue-allocation
  port bitmap; *why* the loader writes that set remains [U].
- **EQM indirect command word decode [V code shape].** Word written to `eqm+0x48`: bits 15..0
  index, bits 26..20 table id, bit30 1=write/0=read, bit31 start/busy (polled), bit29 error.
  Data words go to `eqm+0x5c` then the `g_eqm_tab_ba` address table (writes `+0x60/0x64/0x68/0x70`,
  reads `+0x50/0x54/0x58`, word 0 of a read `+0x4c`). The table *contents* and the engine's use
  of them remain [U].

## 3. Gate model (fail-closed; default boot unchanged)

| boot parameters | LSW driver (021/022/025) | GEMAC netdevs (023) |
| --- | --- | --- |
| none | read-only baseline validate + refuse; no write | none |
| `luofu_lsw.arm=1` | W3 power, verify, snapshot; refuse; no LSW register write | none |
| `luofu_lsw.egress=1` or `qidmap=1` without `arm=1` | explicit `-EINVAL` refusal (new in 025) | none |
| `arm=1 egress=1` | W3 + W4 R1/R3/R5 write + readback; qid map absent -> restore found values, power down, refuse | none |
| `arm=1 qidmap=1` | W3 + W5 qid map write + readback; probe config absent -> restore, power down, refuse (new in 025) | none |
| `arm=1 egress=1 qidmap=1` | W3 + W4 + W5 both verified -> `luofu_switch_ready()` true | requires `luofu_pie.arm=1` |

`luofu_switch_ready()` now means **both** the reviewed probe configuration and the qid map are
written and read back. This is a deliberate tightening of patch 022's readiness: the attended
sequence must add `luofu_lsw.egress=1` to get netdevs.

## 4. Validation (isolated clones under `build/linux-kbuild/waves/2026-11-19-lsw-egress/`, no device)

- **Series apply (001-025):** pristine `linux-6.18.60` copy (`cp -a --reflink=auto` of the
  local pristine tree) + patches `001`-`025` (25 patches; `024` is the PIE lane's patch, applied
  as delivered during this session at sha256
  `d81a44fb7c004e66d9c686d0d38ee5cccdf52e8ba6f9e454a92f091352ae36f1`): **25/25 applied, 0
  failed hunks, 0 `.rej`, 0 `.orig`, RC=0** (`validation/logs/20-series-apply-001-025.log`).
  Resulting `luofu-switch.c` equals this lane's source byte for byte; `luofu-pie.c`/`luofu-gmac.c`
  are 024's. Patch 025 is file-disjoint from 024 (switch driver vs PIE/GMAC/bindings), so order
  does not matter, but 024 must be re-validated by its own lane if it changes.
- **Full kernel compile (001-025):** isolated reflink copy of the compiled OpenWrt luofu kernel
  build dir with patches 024+025 applied, `make ARCH=arm
  CROSS_COMPILE=arm-openwrt-linux-muslgnueabi- W=1 -j16 vmlinux`: **RC=0**; all three changed
  drivers rebuilt (`luofu-switch.o`, `luofu-gmac.o`, `luofu-pie.o`); `vmlinux`
  **133,286,956 B**, sha256
  `63648e754d95ac289f43c7954036ec0ab1cc0e18fb91a65efb08503f616a0371`
  (`validation/logs/62-compile-vmlinux-001-025.log`). The only warnings are six pre-existing
  `-Wmissing-include-dirs` notices for the copied build environment's absent
  `staging_dir/usr/include` shim; none come from the changed drivers (a `W=1` driver-only build
  of 025 is warning-free; the same three notices appear in the pre-025 build).
- **checkpatch:** 0 errors; one `COMMIT_MESSAGE` style warning because the patch file is a pure
  unified diff like every other patch in this series (`validation/logs/40-checkpatch-025.log`).
- **Not changed / not re-validated:** patch 025 changes no DTS, binding, `config-6.18` or image
  file (so no `dt_binding_check` / board-DTS re-run is needed for it alone); 024's own binding
  changes were not re-checked by this lane. `026` (mac-nvmem lane) is present in the skeleton but
  is outside this validation.

## 5. Proof vs unverified

**Proven (host-side):** patch applies in the 001-023 skeleton series; full kernel compiles with
WERROR, `W=1` clean for the changed files, checkpatch 0 errors; the gate composition, the
conformance checks, the derived readback expectations and the restore paths are reviewable code;
the R1/R3/R5 expectations are the measured 2026-11-18 values.

**Unverified / open:** every hardware claim. The stage has never run; the write values are
vendor-shaped and measured but their field semantics are [U]; the loader/DT pre-state on our own
boot is not measured (the W4 conformance gate is what will tell us); the EQM queue tables needed
for a working forwarding path are not written by any patch; the per-frame egress selection and
the TX/RX data path remain [U]. A registered netdev after a successful W4+W5 boot must not be
read as a working link.

## 6. Risks and mitigations

| risk | severity | mitigation |
| --- | --- | --- |
| register write hangs the CPU/console path | high | attended only; one gate added per boot; recovery card armed first; every write is the vendor's reviewed word, read back; restore + power-down on mismatch/refusal |
| probe config applied but EQM queue tables absent -> misleading ready | med | ready means only that the reviewed write set is verified (documented in LSW-SPEC 7.8, SKELETON-SMOKE); traffic test must fail closed in reporting |
| `fam+0x504` pre-state differs from the measured state | med | conformance refuses before the first write and logs the found value; that refusal is itself useful evidence, do not bypass |
| `dp+0x64` two-write latch meaning [I] | low/med | best-effort: vendor shape replayed exactly; readback verifies the final value |
| block left half-configured on an unexpected failure | low | every refusal restores the found words and powers the block down; `remove()` also restores |
| DTS/binding drift from other lanes | low | this patch touches no DTS/binding; shared.patch is docs only |

## 7. Attended test sequence (summary; exact console lines in SKELETON-SMOKE.md)

1. Recovery door armed; recovery card current. Boot stock/our image with **nothing**: confirm
   the read-only refusal (no write).
2. `luofu_lsw.arm=1`: W3 only; capture the 30-word snapshot and qid-table counts; power cycle.
3. `luofu_lsw.arm=1 luofu_lsw.egress=1`: W4; expect either the W4 verified line with
   `dp+0x1c=0x18000 dp+0x64=0x1 eqm+0x18=0x80001cc0 eqm+0x38=0x7f0 eqm+0x40=0x80000000
   eqm+0xa4=0x1fc0 ips+0x08=0x2a` and then the qid-map-absent refusal/power-down, **or** the
   `fam+0x504 ... computed cell count 0x2000` conformance refusal (record the found value; stop).
   Capture `dmesg | grep luofu-switch`; confirm console/CPU survived; power cycle.
4. `luofu_lsw.arm=1 luofu_lsw.egress=1 luofu_lsw.qidmap=1`: W4+W5; expect both verified lines
   and `connectivity path armed: R1/R3/R5 probe configuration and the W2 qid maps ...`.
5. Add `luofu_pie.arm=1`: expect GEMAC netdevs (eth0..eth3 in DT order; bench cable on eth2).
   Test carrier/ethtool/static-address ARP/ping; record the first failing step. Expected open
   failure: no frame forwards until the EQM queue tables (E1) and the per-frame egress selection
   are solved (a wiring-level bench trace or GPL source).

## 8. Integration steps for the lead

1. Commit `target-skeleton/target/linux/hisilicon/patches-6.18/025-net-lsw-egress-luofu.patch`
   (already written; sha256 above) as the skeleton's next patch.
2. Apply `shared.patch` from the `opensource/` root (3 docs files; `git apply --check` RC=0).
3. Copy 025 into the upstream review form (`opensource/upstream/series-6.18/0025-net-lsw-egress-luofu.patch`)
   and re-run the upstream series validation of 001-025. Patch 024 (PIE lane) is present in the
   skeleton now; if it changes after this session, re-validate 024 and the 001-025 chain (025 is
   file-disjoint from 024, so no rebase is expected).
4. Move this SUMMARY to `target-skeleton/docs/port-notes/lsw-egress/SUMMARY.md` or append it to
   `port-notes/lsw/SUMMARY.md`.
5. Do not rebuild/flash the image from 025 as part of this lane: the attended sequence in
   section 7 is the next gate.

## 9. Local evidence (gitignored, absolute paths under the repo)

`build/linux-kbuild/waves/2026-11-19-lsw-egress/`: `work/` (patch source + base snapshots +
`hi_lsw_dp.annotated.dis`, `hi_kpie.annotated.dis`), `validation/series-tree-final` (pristine
6.18.60 + 001-025), `validation/compile-tree` (isolated compiled build dir + 024 + 025),
`validation/logs/` (series apply, checkpatch, full-compile logs). No vendor binary is copied
outside the local fixture lane; nothing here is for publication.
