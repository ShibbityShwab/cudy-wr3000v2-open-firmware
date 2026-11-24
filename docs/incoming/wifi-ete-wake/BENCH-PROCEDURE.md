# BENCH-PROCEDURE - ETE DR (A->B) copy wake ladder (ete-wake lane)

Bench-only, attended, one idea per session. **Host commands only (driver attrs/reads); no
UART transmit, no device firmware changes, no flash.**

## 0. Gates (all must pass before any session)

| gate | check |
| --- | --- |
| G1 recovery door armed | `tools/recovery-card.sh` READY; physical power-off + RESET-hold + TFTP available |
| G2 bench link | `tools/bench-link.sh` PASS |
| G3 UART capture | read-only `tools/uart-capture.sh` running to an absolute path |
| G4 human at the bench | yes; a single variable per fire; stop rules below |
| G5 baseline image | PD4-patched firmware (image sha256 `cfe24bfb...`) + the clean driver tree used by the current baseline session |

Never write CA 0x400392F0. Never write the ETE status word (0x4003A874), ack word (0x4003A870),
the status/control word (0x4003A86C), any send word (0x4003ACxx), the engine blocks
(0x4003A540/0x4003A720), the doors (0x4003A7C0/0x4003A7D0) or the engine global area
(0x4003A000..+0x3FF). The ladder's only device write class is the **host-owned** DR group-B
published index `+0x38` (E1a/E1b).

## 1. Bench artifact: patched driver with the read-only snapshot + the E1 write attr

The ladder needs two attributes that the committed trees do not have:
`hcc_ete_wake` (RO, prints every R0 row) and `hcc_dr_pub` (WO, E1a/E1b: writes DR +0x38 to a
single rule with read-back).

* patch: [tools/hcc_dr_pub.patch](tools/hcc_dr_pub.patch) - applies to the current bench tree
  (`opensource/docs/incoming/wifi-h2d-send/`, `patch -p1`);
* build (host-only; the Lead rebuilds the module for the bench):

      cp -r opensource/docs/incoming/wifi-h2d-send /tmp/etewake-bench
      cd /tmp/etewake-bench
      patch -p1 < <repo>/opensource/docs/incoming/wifi-ete-wake/tools/hcc_dr_pub.patch
      RUN_SELFTEST=0 bash tools/build.sh        # device-free; uses the project 6.18.60 tree

* compile evidence (this lane, build path /tmp/etewake-pub): module **411,228 B**, sha256
  `9351305b...478196`, **0 compiler/modpost warnings**, vermagic
  `6.18.60 SMP mod_unload ARMv7 p2v8`, 32 `T h2d_*` symbols; the module hash is
  build-path dependent (debug info) - record the hash the deployed build prints;
* no other change: the patch only adds one RO show function and one WO store function in
  `src/hcc.c` plus the declaration in `src/hi5622_hcc.h`; the existing arm/service logic is
  untouched.

Sysfs path (same convention as the other lane bench docs):

    BDF=<endpoint bdf>   # 59e7:0005 function
    S=/sys/bus/pci/devices/$BDF

## 2. R0 - read-only snapshot (decides which gap is active; no writes)

Session shape (the standard golden-boot baseline, all commands host-side):

    insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin ...
    echo 1 > $S/prepare ; echo 1 > $S/upload ; echo 1 > $S/verify ; echo 1 > $S/seed
    echo 1 > $S/hcc_arm ; echo 1 > $S/hcc_loop
    # release 0x5A5A only (the existing release attr/policy)
    # wait: boot page 0x86807fef/7/2/0xabcd, state CA 0x118b60=1, mask CA 0x40008=0
    echo 1 > $S/h2d_ctx_create          # one context-create, type 1 (stages ctx+0x190)
    echo 1 > $S/h2d_txq_sync72          # one TXQ sync; firmware responds g1 id3/g1 id0x12
    cat $S/hcc_ete_wake                 # R0 pack (read-only)
    cat $S/hcc_status $S/hcc_events $S/hcc_ready $S/rd_status $S/rd_catalog $S/boot
    dmesg | tail -80
    # STOP: power-cycle (never rmmod after release)

R0 expectations on the current baseline (fills the ladder's decision matrix):

| row | expected D1 (current baseline) | meaning |
| --- | --- | --- |
| status CA 0x4003A874 | 0 (no bits 24-28) | hardware never raised DR service |
| ack CA 0x4003A870 | 0 | no dispatcher ack yet |
| engine global CA 0x4003A000 | 0x10A | same as the vendor golden boot |
| blocks CA 0x4003A540/720 | 1 / 1 | pcie_msg_init ran |
| doors CA 0x4003A7C0 / 0x7D0 | invalid-looking / 0 | differs from vendor 0x83FAC000 / 0 |
| DR0-3 pub(+0x38) / fill(+0x3C) | 0x400 / 0x10 | out-of-phase ring (vendor: equal) |
| DR0-3 send words | 0 | hardware state idle |
| DR record trig(+0x344) | 0 | sending trigger never ran |
| DR0 source node word1 | 0x??6d2b-class tag | the firmware did stage D2H nodes |
| SR send words | e.g. 0x00010001 after the H2D announce | the engine copy works for SR |

Decision:

* **D1** - status/ack 0, sends 0, fills 0x10, trig 0, staged nodes present -> hardware never
  raised the DR status; go to **E1a** on a fresh boot.
* **D2** - any status bit 24-28 set but trig 0 -> delivery gap (unexpected with PD4); capture
  the pack, stop, and re-open the boot/latch gate question before E1.
* **D3** - trig counters > 0 but fills still 0x10 -> the trigger ran and the engine did not
  copy; skip E1 and go straight to **E2** (door/global diff) with the pack.

## 3. E1a - single write class: DR published index := current fill

Preconditions: G1-G5, fresh boot, R0 shows **D1**, rings armed, boot latched.

    echo fill > $S/hcc_dr_pub           # writes each DR +0x38 = its current +0x3c value
    # immediately re-read the pack, 10 iterations over ~10 s:
    for i in $(seq 1 10); do cat $S/hcc_ete_wake; sleep 1; done
    dmesg | grep -E "HCC DR[0-3].*\+0x38|h2d_complete|engine fill" | tail -20

Expected if the published/fill relationship is the gate:

* status CA 0x4003A874 gains one or more bits 24-28 within the poll window;
* DR record `trig(+0x344)` becomes nonzero; send words become nonzero;
* `fill(+0x3c)` advances past 0x10 and/or host DRAM DR0-3 node word1 becomes tagged
  (engine path); the core logs `engine fill(+0x3c) advanced ... without a mailbox word` and
  drains via the engine path;
* pub(+0x38) read back equals the written value on all four channels.

Stop rules: any exception-mask change (CA 0x40008 != 0), boot page mutation, module oops or
no-consumption timeout -> STOP, power-cycle, do not retry, do not repeat the write class in the
same boot. One value rule per boot.

## 4. E1b - control: DR published index := 0 (vendor dr_reg_init value)

Only if E1a was negative. Fresh cold boot, same sequence, R0 repeated (D1), then:

    echo zero > $S/hcc_dr_pub           # writes each DR +0x38 = 0x00000000
    for i in $(seq 1 10); do cat $S/hcc_ete_wake; sleep 1; done

Same observables and stop rules. If E1a and E1b are both negative, the published index is not
the gate -> E2. If E1b is positive, the vendor-init value (not fill equality) is the required
state -> record it and repeat once on a second boot to confirm (same single write class).

## 5. E2 - read-only golden-vendor ETE window diff

Only after negative E1a/E1b. Flip to the stock vendor pair A per the established attended
procedure (no flash of our image; just boot the stock slot) with Wi-Fi up, and take the same
read pack rows read-only:

| row | vendor reference already known |
| --- | --- |
| handshake page | 0x86807fef / 7 / 2 / 0xabcd |
| state / mask | 1 / 0 |
| engine global | 0x4003A000 = 0x10A |
| blocks | 0x4003A540 = 1, 0x4003A720 = 1 |
| door0 / door1 | 0x83FAC000 / 0 (an older stock dump read 0x83FD4000 / 0) |
| status / ack (sampled idle) | 0 / 0 |
| DR send words / fills | live, e.g. DR0 0x041E041E fill 0x41E; DR3 0x00050005 fill 0x5 |
| DR pub(+0x38) | equals fill on every channel |

Any further word that differs between the vendor pack and ours names the remaining
device-internal state (door semantics, engine-global area, ROM-authored ETE init). **No writes
in this rung**; restore the baseline image afterwards (stock pair A untouched).

## 6. E3 - close-out

* E1a or E1b positive -> the host-owned destination-ring state was the missing enabler; fix the
  driver arm to the vendor relationship (publish = fill at arm; re-arm from the engine-path
  walk) and keep the direct reader as fallback.
* E1a/E1b/E2 all negative -> definitive: the DR start condition is device/ROM-internal state
  (hardware status bits 24-28 are never raised; the only known difference left is ROM-authored
  engine state). Document it and keep the direct group-A read as the supported receive path.
  Do not start speculative ETE register writes (the vendor stack has none, and the send
  words/status are engine protocol state).
* Do not write: status/ack/ctrl/send words, doors, engine global, 0x400392F0.
