# wifi-ete-init lane — vendor ETE channel initialisation vs the B3 P0 ringinit

**Lane:** wifi-ete-init (shared task task-81). **Date:** 2026-11-23. **Host-only:**
the vendor modules were read in place; the bench router was never contacted, no image was
built, and no vendor byte/binary is committed here. Deliverables: this report,
[ETE-INIT.patch](ETE-INIT.patch) (compile-ready, built clean), [BENCH-PROCEDURE.md](BENCH-PROCEDURE.md),
and [tools/ete_scan.py](tools/ete_scan.py) (reproducible verifier, **41/41 assertions pass**).

Evidence markers as in the transport headers: **[V]** re-verified this lane from the named
local artifact, **[V\*]** device-measured by an earlier attended lane, **[I]** inference,
**[U]** unknown.

---

## 0. Bottom line (read this first)

1. **The complete host-side ETE init in the vendor platform driver is 3 groups of writes, and
   every one is now identified with exact values and order** (sec. 2): (a) one read-modify-write
   of the ETE interrupt word CA `0x40039508 &= 0xFFE0F8F8` in `pcie_ete_intr_init`; (b) per SR
   channel, group-A base / depth-1 / producer / ctrl in `pcie_ete_sr_reg_init`; (c) per DR
   channel, group-B base / depth-1 / published index in `pcie_ete_dr_reg_init`. The 2.5.24 and
   2.4.15 builds are **byte-identical** for all ten ETE init functions (sec. 1).
2. **The ETE send/flag words at CA `0x4003AC00+4i` (DR0-3) and `0x4003AC18+4i` (SR0-2) are never
   written by the host driver** — in either build. The init paths only store their kernel VA in
   the ring context; every access is a read (plus the SR "already sending" check). They are
   **device/hardware-authored state**: hi16 = target send index, lo16 = device progress index,
   both *packed ring indices* (low 10 bits index, bit 10 wrap phase) [V].
3. **The golden diff is not an init diff.** `0x04050405` / `0x040E040E` decode to packed index
   `0x405` / `0x40E` in both halves and **exactly equal the live DR fills** `0x4003A5CC` /
   `0x4003A6BC` — i.e. the vendor capture records **1029 / 1038 nodes already transferred on
   DR0/DR3 during its boot**, not a probe-time register seed. A "send-index init" write would
   have to write 0 (empty ring), which is indistinguishable from our current zero state: there
   is no missing host seed. **The task premise "B3 does not initialise the ETE send registers"
   is therefore falsified; the correct conclusion is sec. 7 (fallback: firmware-side pump).**
4. The host-owned ETE init write B3 *was* actually missing is the **intr mask RMW**
   (CA `0x40039508 &= 0xFFE0F8F8`, once, before ring arming) — plus the vendor writes depth/ctrl
   as **read-modify-write bitfields** (upper register bits preserved) whereas B3 used plain
   writes. [ETE-INIT.patch](ETE-INIT.patch) adds exactly those, plus a **read-only** P0 baseline
   and the `b3_ete_send` watch that makes the question decidable at the bench.
5. **Fallback conclusion (sec. 7):** the vendor ETE *send* path that matters is firmware-side.
   The firmware's `pcie_msg_init` programs the DR source ring (group A, device PKTRAM) and sets
   the channel enable `+0x00 |= 1` [V\*, ../wifi-ready-emit]. No host-side ETE write can turn on
   that pump. If the patched run shows the DR send words/fills still frozen **and** group-A
   base / enable still 0, the blocker is upstream of host ETE init (firmware D2H pump / ready
   emission), not this register set.

---

## 1. Method and evidence base

Local artifacts (never committed; the repo's no-vendor-binary rule):

| artifact | build | used for |
| --- | --- | --- |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` | stock 2.5.24 | primary [V] |
| `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko` | 2.4.15 | cross-check [V] |

Both modules are **not stripped** (full `.symtab`), so every `pcie_ete_*` function is named
with its exact address and extent. The ETE init functions were disassembled with the project
cross-binutils (`build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump -dr`) and
independently re-disassembled by [tools/ete_scan.py](tools/ete_scan.py) (pyelftools + capstone);
the tool re-asserts every fact in this report (`selftest`: 41/41 PASS). The two builds'
init functions are byte-identical (`compare`), so there is no version ambiguity.

Two **static data** findings pin the address map [V]: `.data` contains the CA pair
`{0x4003A000, 0x40039508}` (ETE channel block, ETE interrupt word) returned by
`get_pcie_ete_res()` and consumed by the ring/intr init paths. `pcie_ete_init` call order is
`get_pcie_ete_res -> pcie_ete_intr_init -> pcie_ete_rings_init -> pcie_ete_chn_res`.

---

## 2. The complete vendor ETE initialisation sequence [V]

Function addresses are 2.5.24 (2.4.15 identical). "CA" = device CPU address; the host reaches
these through the BAR0 iATU window (BAR0 offset = CA − 0x40000000 for the ETE/glue blocks, i.e.
CA `0x4003A000` = BAR0 `0x3F2000`).

### 2.1 Order (probe path only; no reset/deinit writes in this sequence)

| # | function | action |
| --- | --- | --- |
| 0 | `pcie_ete_init` 0x07820 | `get_pcie_ete_res` -> CA pair; alloc 3×276 B SR + 4×108 B DR ring contexts; then 1→4 |
| 1 | `pcie_ete_intr_init` 0x07528 | installs rx/tx callback pointers in the ETE context; maps CA `0x40039508`; **RMW: `*(u32 *)(0x40039508) &= 0xFFE0F8F8`** — the only global ETE write |
| 2 | `pcie_ete_rings_init` 0x07680 | maps the ETE channel block (CA `0x4003A000`); SR init loop i=0..2 then DR init loop i=3..6 |
| 3 | `pcie_ete_init_src_ring` 0x07068 | per SR channel: store `ctx+0xDC = ETE + cfg[i].off` (0x400/0x450/0x4A0), `ctx+0xEC = ETE + 0xC18 + 4i` (**send-word pointer, stored only**), allocate ring buffers, then `pcie_ete_sr_init` |
| 4 | `pcie_ete_sr_reg_init` 0x14A48 | per SR channel: *reads* the send word at `+0xC18+4i`, takes **hi16**, logs a warning when non-zero ("already sending") and **continues**; then the writes in 2.2 |
| 5 | `pcie_ete_init_dst_ring` 0x0727C | per DR channel i=3..6 (records DR0..DR3): store `ctx+0x50 = ETE + cfg[i].off` (0x590/0x5E0/0x630/0x680), **`ctx+0x5C = ETE + 0xC00 + 4(i-3)`** (send-word pointer, stored only), allocate buffers; loop runs i=3..6 |
| 6 | `pcie_ete_dr_init` 0x14908 → `pcie_ete_dr_reg_init` 0x1483C | per DR channel: the writes in 2.2 |
| 7 | `pcie_ete_chn_res` 0x07490 | vendor-only per-PCIe-channel LRES write: for each vendor pcie channel object with dir flag `byte[obj->lres+0x18]==0`, `*(u32 *)(lres_hwbase + 0x2E8) &= 0xFFFFFC20` (keeps bit5 + bits 10-31; barriers `dsb sy` / `dsb st` / `arm_heavy_mb`). This mask belongs to the vendor's own PCIe RX-channel interrupt routing object (`pci_dev_res_init`/`oal_pci_lres_init`), **not** the ETE channel block; B3 has no such objects and does not port it (documented, not needed) [V] |

### 2.2 Exact host writes, values and conditions

| target (CA) | register | op | value / rule | condition |
| --- | --- | --- | --- | --- |
| `0x40039508` | ETE interrupt word | RMW | `v = readl(); v &= 0xFFE0F8F8; writel(v)` — clears bits 0-2, 8-10, 16-20; keeps 3-7, 11-15, 21-31 | once, always, before any ring write |
| SR0/1/2 `+0x10` (`0x40000410/…`) | group A base | store | `pcie_hostca_to_devva(ring buffer)` = device address of the host-DRAM ring | always |
| SR `+0x14` | group A depth-1 | RMW `bfi(0,10)` | low 10 bits = `cfg.depth-1 = 31`; upper bits preserved | always |
| SR `+0x18` | group A producer | store | `ring.ctx[0xC]` = 0 at init | always |
| SR `+0x08` | ctrl | RMW `bfi(0,3)` | low 3 bits = `cfg.byte5 & 7 = 0` for all three channels; upper bits preserved | always |
| DR0-3 `+0x30` | group B base | store | device address of the host-DRAM node array | always |
| DR `+0x34` | group B depth-1 | RMW `bfi(0,10)` | low 10 bits = 31; upper bits preserved | always |
| DR `+0x38` | group B published index | store | `ring.ctx[0x1C]` = 0 at init (B3 pre-posts `0x400` = 32 free buffers by design) | always |
| SR `+0xC18+4i` | send/flag word | **READ only** | hi16 must be 0 at init; non-zero -> warning log, init continues | always |
| DR `+0xC00+4(i-3)`, SR `+0xC18+4i` | send/flag words | **NEVER written** | — | no writer exists in either build |
| `+0x00` enable | channel enable | **NEVER written by host** | firmware writes `|= 1` | — (2.4.15 has an unreferenced `pcie_ete_chn_enable` helper: zero call sites; 2.5.24 keeps the same code with no symbol and no reference) |

Not written by the host init either: DR `+0x08`, group-A consumer `+0x1C`, group-B fill `+0x3C`.
No delays/polls exist in this sequence; the only conditional behaviour is the SR "already
sending" **warning** (not a reject — `pcie_ete_sr_reg_init` jumps straight back into the
remaining writes).

### 2.3 Where the send words come from (and where they do not) [V]

`pcie_ete_init_src_ring` / `..._dst_ring` compute `ETE + 0xC18 + 4i` and `ETE + 0xC00 + 4i`
respectively and store them into the ring context (`+0xEC` / `+0x5C`). A whole-`.text`
instruction dataflow scan (all functions, both builds) finds **zero stores through either
context slot**; all six ETE reachable uses are reads:

| function | slot | use |
| --- | --- | --- |
| `pcie_ete_sr_reg_init` 0x14A70 | SR +0xEC | hi16 "already sending" check |
| `pcie_tx_done_handle` 0x16930 | SR +0xEC | low16 = device progress -> u16 consumed by the TX-done handler |
| `pcie_ete_get_rls_num` 0x14758 | SR +0xEC | debug log of the raw word |
| `pcie_ete_dr_get_uploadbuf` 0x15188/0x15200 | DR +0x5C | debug log only |
| `pcie_ete_rcv_buff_check` 0x14F08 | DR +0x5C | debug log only |

Encoding (locked by these reads + the firmware side, ../wifi-ready-emit sec. 3.3): one 32-bit
word per channel, **hi16 = target send index** (firmware advances as it queues), **lo16 = device
progress index** (engine advances as it transfers); both are the project ring index
`(idx & 0x3FF) | phase << 10` (`pcie_ete_ring_ptr_plus`, depth 32). hi16 == lo16 means the
firmware and engine are in sync (ring empty relative to the queue).

### 2.4 Golden-value derivation (this is the decisive finding)

| golden item | CA | raw | decode |
| --- | --- | --- | --- |
| DR0 send | `0x4003AC00` | `0x04050405` | hi16 = 0x0405 = packed idx 5, phase 1; lo16 = same |
| DR3 send | `0x4003AC0C` | `0x040E040E` | hi16/lo16 = 0x040E = packed idx 14, phase 1 |
| DR0 fill | `0x4003A5CC` | `0x00000405` | group-B +0x3C, packed idx 5, phase 1 |
| DR3 fill | `0x4003A6BC` | `0x0000040E` | group-B +0x3C, packed idx 14, phase 1 |

The fill registers are the engine's own live counters for the channel; the send word's two
halves mirror that state. Phase bit set = at least one 32-entry wrap = **1029 / 1038 nodes
transferred** on DR0/DR3 during the vendor's boot. These are **live traffic records captured
after boot**, not register-reset/probe values: nothing in the vendor host driver writes them at
probe, and an "initial" write for an empty ring would be 0 — exactly what our capture shows.
Their being zero means "this device never pumped a node", not "the host forgot a seed".

---

## 3. The host-owned ETE init writes B3 was actually missing

Compared with B3 P0 before this lane (`../wifi-transport-port/b3/pciet_b3.c`):

| vendor write | old B3 P0 | delta / bug |
| --- | --- | --- |
| `0x40039508 &= 0xFFE0F8F8` | never written | **missing**; now added (host-facing receive mask, firmware never reads it — B3 polls, so this is completeness/safety-fidelity, not a device gate) |
| SR/DR depth-1 `bfi(0,10)` | plain `writel(depth-1)` | **bug**: clobbered the register's upper bits; now RMW |
| SR ctrl `bfi(0,3)` | plain `writel(0)` | **bug**: same class; now RMW |
| SR send-word hi16 check | absent | added read-only warning (vendor behaviour on "already sending") |
| send/fill/group-A observability | only `b3_rings`/P4 reads | added `b3_ete_send` + P0 baseline (read-only) |

Everything else matched (base stores, SR producer 0, DR published pre-post as B3 design).

---

## 4. What the firmware owns (why a host "send init" cannot exist)

From the prior lane's firmware reversal [V\*, ../wifi-ready-emit sec. 3.1-3.3, replayable with
its `tools/readyemit.py`]: the four DR channels each have a **device-side source ring (group A,
in device PKTRAM)** and a host destination ring (group B, host DRAM); the ETE engine copies A→B.
The firmware's `pcie_msg_init` writes the DR group A base / depth-1 / producer and the ctrl
nibble, then sets `+0x00 enable |= 1`; it also reads the send word hi16 as its target index
and low16 as its progress index. The host's only DR duties are group B + the mailbox
ack/drain/re-arm service. The engine's sending trigger is invoked from the **device's own ETE
status word** (CA `0x4003A874`), not from any host register. This lane re-verified that the
vendor host driver never touches group A on DR channels, never enables a channel, and never
writes the send words — consistent with the firmware-side model. See also
[../wifi-ready-emit/REPORT.md](../wifi-ready-emit/REPORT.md) sec. 3 and 5.

---

## 5. The patch (compile-ready)

[ETE-INIT.patch](ETE-INIT.patch) — unified diff against
`opensource/docs/incoming/wifi-transport-port/b3/`; applies with:

    cd opensource/docs/incoming/wifi-transport-port/b3
    patch -p1 < ../../wifi-ete-init/ETE-INIT.patch     # touches b3/pciet_b3.c, b3/hi5622_b3.h

It is also the delta to apply before the next attended run (the patched tree is what was built
and verified here; the source in `../wifi-transport-port/b3/` is left untouched for its lane).

Hunks:

1. `hi5622_b3.h`: new section 3a with the recovered constants —
   `HI5622_CA_ETE_STATUS 0x4003A874`, `HI5622_ETE_INTR_KEEP 0xFFE0F8F8`,
   `HI5622_ETE_SEND_DR0 0xC00`, `HI5622_ETE_SEND_SR0 0xC18`, stride 4, and the authorship
   note (host writes nothing at `0xC00`).
2. `pciet_b3.c`:
   - `b3_ete_intr_init()`: vendor-exact RMW + read-back; called at the **start** of
     `b3_arm_all()` (vendor order: intr word first, then rings) and logged.
   - `b3_ete_depth_init()` / `b3_ete_ctrl_init()`: bfi-equivalent RMW so upper register bits
     are preserved (SR `+0x14`/`+0x08`, DR `+0x34`).
   - `b3_arm_sr()`: now reads the SR send word and warns on non-zero hi16 (vendor behaviour);
     read-back checks compare the masked bitfield, so a legitimate RTM upper bit cannot report
     a false MISMATCH.
   - `b3_ete_send_baseline()` + `b3_build_ete_send()`: P0-time baseline (read-only) and the
     formatted watch of all seven send words, all four DR fills, the firmware-owned DR
     group-A state (base/depth/prod/cons), enable bits, ETE status and int word.
   - new **read-only** sysfs attribute `b3_ete_send`; P0 logs the same text as `B3 P0-ETE:`
     lines to dmesg.
   - no new write to any firmware-owned field; the only new device write is the vendor's
     CA `0x40039508` RMW, routed through the existing never-write gate (`0x400392F0` still
     refused) and applied **before** the CPU release (P0 runs pre-release in the B3 sequence).

Build verification (module-only, the B3 tooling; nothing else was built):

    cd <patched b3 tree> && make        # exact line in ../wifi-transport-port/b3/Makefile
    KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    CROSS_COMPILE=.../toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

| item | value |
| --- | --- |
| result | **BUILD OK, zero compiler errors, zero warnings, modpost clean** (`CONFIG_WERROR=y` tree) |
| module | `hi5622_pciet_b3.ko`, ELF 32-bit LSB ARM EABI5, not stripped |
| size | 193,196 B |
| sha256 | `d577bed5dd93da54d26ed556bd20adbf0f67c4654ce2637c138df3c51784fd06` |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| new attr | `b3_ete_send` (RO); module params unchanged |

Build log and the `.ko` are local evidence only (gitignored / not committed); the sha256 is for
the build directory `/home/shibbityshwab/wifi-ete-init-work/b3`. The patch was additionally
re-applied to a **fresh copy** of `../wifi-transport-port/b3/` and rebuilt there (different
directory -> different `.ko` hash because an out-of-tree module embeds its build path): build
exit 0, zero warnings, and `.text`/`.rodata`/`.data`/`.modinfo` are **byte-identical** to the
verified build (`.text` sha256 `d99f479df6d0…`), i.e. the committed patch alone reproduces the
verified module.

---

## 6. Expected bench observables after the fix

Full step-by-step sequence: [BENCH-PROCEDURE.md](BENCH-PROCEDURE.md) (runs on top of
`../wifi-transport-port/b3/B3-PLAN.md`, which stays authoritative for gates and order).
The new/updated observables:

| # | when | command | expected if host side is complete and firmware pumps | interpretation when not |
| --- | --- | --- | --- | --- |
| E1 | P0 dmesg | `dmesg \| grep 'B3 P0'` | one `B3 P0: ETE intr CA 0x40039508 … readback=0x… OK`; 7/7 `readback=OK`; then `B3 P0-ETE:` block with `host send-index writes performed: 0` | intr MISMATCH -> stop (device state not trusted) |
| E2 | P0, pre-release | `cat b3_ete_send` | all seven `send=` words **0x00000000**; status CA 0x4003A874 = 0 (CPU still held); DR groupA base/enable 0; host groupB pub=0x400, fill=`0x10`-class reset | any non-zero send word before release -> stale device state, stop and record |
| E3 | after release+announce (before drain) | `cat b3_ete_send` | **the discriminator:** send words may stay 0 here (firmware may not have pumped yet) — record exactly; check DR `enable(+0x00)` and group-A base: firmware `pcie_msg_init` must have set them by now | groupA base==0 **and** enable bit0==0 on all four DR channels -> firmware never configured the DR source rings (fallback sec. 7) |
| E4 | during/after P1 watch (30 s) | `cat b3_ete_send; cat b3_p1_events; cat b3_p3_hcc` | DR fills **advance** (`fill=` != baseline, `advance>0`); send words become **non-zero** (hi16/lo16 = packed index, typically equal = in sync); out[1] bit6 counter (d2h_notify) and bit3 (ETE done) non-zero; `b3_p3_hcc` `g4_id1_seen=1` / `g4_id2_seen=1` as the ready gate | fills frozen at baseline **and** send words 0 after a served watch -> engine never transferred: firmware-side blocker (not host ETE init) |
| E5 | P4 | `cat b3_p4_state` | ETE intr word == pre-P0 read & 0xFFE0F8F8 (RMW held); per-channel enable/group-A values as recorded in E3 | — |

The `b3_ete_send` attribute prints, per DR channel: send word with hi16/lo16 decoded and a
`sync=` flag, firmware group-A base/depth-1/prod/cons and `enable(+0x00)`, host group-B
base/depth-1/pub, the live `fill(+0x3C)` and `advance=` nodes since the P0 baseline; per SR
channel: send word + group-A state. It is read-only by construction (no writel in the builder).

Stop rules are in BENCH-PROCEDURE.md sec. 4: any MISMATCH, abort/SError/hang, unexpected
non-zero send word **before** release, or a second P0 arm — stop, capture, power-cycle; do not
poke registers to "make it send".

---

## 7. Fallback conclusion — if the vendor ETE init is firmware-side

The evidence says the part of the vendor ETE init that makes the engine send is **firmware-side**,
so the fallback is the expected branch if E3/E4 stay dead:

* The host cannot substitute for it: the send words are read-only state; group A belongs to the
  firmware; `+0x00 enable` is firmware-written (the only host helper that could set it is dead
  code in both vendor builds); the sending trigger hangs off the ETE status word
  CA `0x4003A874`, which is 0 in our runs.
* Therefore, if after the patched run the DR send words/fills are still 0 even though
  `pcie_msg_init` should have run (E3), the blocker is **upstream of host ETE init** — the
  firmware D2H pump / ready-emission path studied by `../wifi-ready-emit` (that lane's
  conclusion is the same: no host write feeds the source→host copy; the missing piece is
  device-side). Do not add ETE register writes on speculation.
* If group-A base/enable are **0** on all DR channels at E3, the firmware never reached its ETE
  channel bring-up; the next step is a firmware-side trace, not a host register experiment.

---

## 8. Limitations / negative results

* The "no host writer" result is from a whole-`.text` instruction dataflow scan (store whose
  base register was loaded from ring-context `+0x5C`/`+0xEC`); it would miss a pointer copied
  through a helper/function boundary. The six direct read sites plus the absence of any other
  `0xC00`/`0xC18` computation in either module (exactly one each, both only stored into the
  context) make that residual risk negligible [V].
* `pcie_ete_chn_res`'s `+0x2E8` RMW belongs to the vendor's PCIe LRES channel object
  (`oal_pci_lres_init`), not the ETE channel block; B3 has no such objects, so it is documented
  but not ported. It is not ETE send state and cannot provide a send seed.
* The firmware's group-A/DEVICE PKTRAM state is not directly host-readable except through the
  group-A base/depth/enable registers that `b3_ete_send` now reports; PKTRAM contents are out
  of scope for a host-only lane.
* The patched module has **not** been loaded on a device by this lane (host-only); the bench
  run is the Lead's attended session.

---

## 9. Reproduce

    PY=<repo>/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ete-init
    $PY tools/ete_scan.py selftest     # 41/41 PASS: every fact in secs. 1-2, 4
    $PY tools/ete_scan.py inventory    # function inventory + init order + CA pair + 0xffe0f8f8
    $PY tools/ete_scan.py sendregs     # zero host stores through +0x5C/+0xEC; read-site table
    $PY tools/ete_scan.py compare      # 2.5.24 vs 2.4.15: ten init functions byte-identical

Module paths default to the two local artifacts above; override with `--new-ko`/`--old-ko`.
Patch applies clean (`patch -p1 --dry-run`) and the patched tree builds with the B3
`Makefile` (sec. 5).

## 10. Files

| file | purpose |
| --- | --- |
| `REPORT.md` | this document |
| `ETE-INIT.patch` | compile-ready patch to `../wifi-transport-port/b3/` (built + verified clean) |
| `BENCH-PROCEDURE.md` | attended-run delta: commands, expected observables E1-E5, interpretation matrix, stop rules |
| `tools/ete_scan.py` | reproducible vendor-module verifier (selftest 41/41) |
| `README.md` | lane summary |
