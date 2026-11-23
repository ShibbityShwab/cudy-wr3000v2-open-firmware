# BENCH-PROCEDURE — patched B3 P0: ETE intr RMW + zero-host-write send-word watch

**Lane:** wifi-ete-init (task-81). **Host-only lane:** this lane did not touch the device.
The run below is the Lead's attended session and is a **delta on
[../wifi-transport-port/b3/B3-PLAN.md](../wifi-transport-port/b3/B3-PLAN.md)** — that document
stays authoritative for preconditions (G1-G8), hard rules, step 0-11 order, and teardown.
Read it first; this file only adds/changes what the ETE-init patch touches.

Reference for every register below: [REPORT.md](REPORT.md) sec. 2 (exact vendor writes) and
sec. 6 (observables). The patched module is **read-only** on the `0xC00`/`0xC18` send words; the
only new device write is the vendor-identical ETE interrupt-word RMW, done at P0 (pre-release).

---

## 0. Build and stage the patched instrument

    # 1. apply the patch to a build tree (the incoming dir is left untouched for its lane)
    cp -a opensource/docs/incoming/wifi-transport-port/b3 /home/shibbityshwab/wifi-ete-init-work/b3
    cd /home/shibbityshwab/wifi-ete-init-work/b3
    patch -p1 < <repo>/opensource/docs/incoming/wifi-ete-init/ETE-INIT.patch

    # 2. module-only build (NEVER an image build)
    make            # Makefile line: KDIR=…/linux-6.18.60, OpenWrt 14.4.0 cross gcc

Check: zero warnings/errors; `sha256sum hi5622_pciet_b3.ko` — for the verified build directory
this lane used: `d577bed5dd93da54d26ed556bd20adbf0f67c4654ce2637c138df3c51784fd06`
(a different build directory changes the hash: the module embeds its build path; compare
`.text/.rodata/.data/.modinfo` byte-for-byte if in doubt).

    # 3. stage onto the bench host module path exactly as in B3-PLAN sec. 5 (scp; no image)
    #    gate G4: modinfo hi5622_pciet_b3.ko -> vermagic 6.18.60 SMP mod_unload ARMv7 p2v8

Expected module surface: unchanged base attrs + `b3_p0_ringinit`/`b3_p1_watch`/… and the **new
read-only `b3_ete_send`**. Load ONLY `hi5622_pciet_b3.ko` with `poll_ms=0` (B3-PLAN step 0).

---

## 1. Steps 0-5 = B3-PLAN unchanged, with two additions

Run B3-PLAN steps 0-3 (load, `prepare`, `upload`, `verify`) exactly as written, then:

### Step 4 — P0 arm all rings (pre-release; B3-PLAN step 4 + ETE-init additions)

    echo 1 > b3_p0_ringinit
    dmesg | grep -E 'B3 P0|B3 P0-ETE'

New/changed expected dmesg lines (record all of them):

1. `B3 P0: ETE intr CA 0x40039508 0x???????? -> 0x???????? (vendor pcie_ete_intr_init AND 0xffe0f8f8) readback=0x???????? OK`
   — **E1**. `OK` required. (`MISMATCH` -> STOP, do not release: device state not trusted.)
2. the seven ring lines: SR*/DR* base/depth/readback; depth is printed as
   `depth-1=0x1f (raw 0x……)` because the vendor update is a bitfield RMW; the read-back gate is
   on the low 10 bits, so a legitimate upper bit does not produce a false MISMATCH. **All 7
   `readback=OK` required.**
3. the summary line (`B3 P0: armed 7 channels; … pre-post …`).
4. the new `B3 P0-ETE:` block (same text `b3_ete_send` prints). Pre-release **E2** expected:
   - `intr CA 0x40039508=0x…… vendor-mask 0xffe0f8f8 applied` (value must equal the pre-P0 read & 0xFFE0F8F8);
   - **all seven `send=0x00000000`**;
   - `status CA 0x4003a874=0x00000000` (CPU still in reset);
   - DR lines: `fw groupA base=0x00000000 … enable=0x00000000` (firmware not running yet),
     `host groupB base=0x8xxxxxxx depth-1=0x1f pub=0x00000400 fill=0x…… advance=0`;
   - the last line must read `host send-index writes performed: 0`.
   - If any send word is **non-zero before release** -> stale device state: STOP, record,
     power-cycle (do not continue).

Checkpoints (in addition to B3-PLAN step 4): `cat b3_ete_send` shows exactly the pre-release
E2 picture; `cat b3_rings` unchanged semantics.

### Step 5-6 — release + announce = B3-PLAN unchanged

    echo 1 > release
    echo 1 > announce

---

## 2. E3 — the discriminator, after announce, before draining

    sleep 1
    cat b3_ete_send
    cat b3_p4_state

Record verbatim (this is the observation the ETE-init question turns on):

| field | expected value / meaning |
| --- | --- |
| `intr CA 0x40039508` | equals the pre-P0 read & 0xFFE0F8F8 (RMW held) |
| `status CA 0x4003a874` | the device ETE status word; 0 in all our runs so far |
| DR* `send=` | may still be 0 here — the firmware may not have pumped yet; **record it, no gate** |
| DR* `fw groupA base=` | must become non-zero **if** the firmware's `pcie_msg_init` ran |
| DR* `enable(+0x00)` | same: non-zero bit0 means firmware enabled the channel |
| DR* `host … fill=…` | compare with the P0 baseline |

Decision:
* groupA base != 0 and/or enable bit0 set on >=1 DR channel -> firmware reached its ETE channel
  bring-up; continue to E4.
* **all four DR channels groupA base == 0 AND enable bit0 == 0** -> firmware never configured the
  DR source rings: this is the REPORT.md sec. 7 fallback; continue the B3-PLAN D2H service/watch
  anyway to capture the mailbox side (E4), then report the fallback as the session finding.

---

## 3. E4 — service + watch 30 s, then re-read the send words

    echo 1 > b3_drain
    echo 30 > b3_p1_watch        # BLOCKS up to ~40 s; never interrupt
    cat b3_ete_send
    cat b3_p1_events
    cat b3_p3_hcc
    cat b3_p2_result
    cat b3_p4_state

Expected if the device pump runs (host side complete):

| observable | expected |
| --- | --- |
| DR `fill(+0x3C)` | advanced vs the P0 baseline (`advance>0`) on >=1 channel |
| DR `send=` | **non-zero**; hi16 = target, lo16 = progress, both decode to one packed index (low10 idx + bit10 phase); `sync=1` when equal |
| `b3_p1_events` | bit3 (ETE transfer done) and/or bit6 (d2h_notify) counters > 0 |
| `b3_p3_hcc` | `g4_id1_seen=1` (device_plat_ready) and `g4_id2_seen=1` (host_ready) — the two vendor init waits |
| `b3_p2_result` | decoded DR nodes (word1 len, word0 buffer VA, 12-B HCC header) |

If **fills stay at baseline and all send words remain 0** after a served watch: the engine never
transferred anything; see REPORT.md sec. 7 (firmware-side pump/ready path, not host ETE init).
Do not add speculative ETE writes — the send words are read-only device state.

---

## 4. Stop rules (in addition to B3-PLAN sec. 2)

1. `B3 P0: ETE intr … MISMATCH` or any ring `readback=MISMATCH` -> STOP before release; record;
   power-cycle (while the CPU is still in reset, reload is safe; prefer a power cycle).
2. Any non-zero DR/SR send word at P0/E2 **before** release -> STOP; stale device state.
3. Abort / SError / hang / unexpected dmesg at any point -> STOP, capture dmesg+UART, power-cycle;
   no troubleshooting by extra writes.
4. Never write CA `0x400392F0`; never read the RC misc window `0x10161000`; UART read-only;
   no `rmmod` after release (power-cycle instead); only one P0 arm per module load (`-EBUSY` is
   the expected second-write answer, not a retry prompt).
5. Do not "make it send" by poking the send words / enable bits: not vendor behaviour, and the
   patched instrument deliberately has no path to those registers.

## 5. Evidence to capture (hand back to the Lead)

* full `dmesg` from load through E4 (`B3 P0`, `B3 P0-ETE`, `B3 D2H`, `B3 P1`);
* `cat b3_ete_send` at E2 and E3 and E4 (three snapshots: pre-release / post-announce / post-watch);
* `cat b3_p1_events`, `cat b3_p3_hcc`, `cat b3_p2_result`, `cat b3_p4_state` after E4;
* UART capture (read-only) for the same window;
* the `sha256sum` of the loaded `.ko` and `modinfo` vermagic line.

Expected overall: at minimum the pre/post-release `b3_ete_send` pair decides whether the golden
vendor send-word state is reachable from our boot at all (E3/E4); if it is not, the fallback
conclusion in REPORT.md sec. 7 stands and the next investigation is the firmware D2H pump, not
this register block.
