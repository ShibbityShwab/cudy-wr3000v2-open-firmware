# FIRE-RECIPE - re-probe first; stock control second; no host message is indicated

**Lane:** `wifi-ppdu-init` (task-219).  This lane's decode does **not** justify a
write to the device.  The init it decodes is a firmware boot-phase step with no
host-message trigger; the only "fire" this lane asks for is a **read-only probe
pass + stock control**, and a conditional escalation.  The Lead owns any run.

## 0. Preconditions for any run (physical, unchanged)

Bench link PASS (`tools/bench-link.sh`), recovery door READY
(`tools/recovery-card.sh`), read-only UART capture running to an absolute
path (`tools/uart-capture.sh`), golden page/status (f1-thread P0), and the
`STATUS.md` next-action gate.  No bench contact by this lane; the Lead
fires.  No `rmmod`, no `0x400392f0` write, no RC misc window.

## 1. Rung 0 - the corrective probe (read-only, ~1 min)

Run **PROBES P1-P8** on the live 2.4 GHz AP boot (this replaces f1-thread P4).
Record the eight words + page/status + the D2H ready-pair state in one line per
boot.  This alone settles the F1 question for the descriptor list:

* **P2 == 0x001060F7** -> the list was initialised; the f1-thread "list never
  initialised" claim is retired by the corrected object.  Do not fire anything
  for the list; move the F1 residual to the firmware RX consumer / first-PPDU
  path (the f1-thread sec. 6 residual).  This is the expected outcome: the
  sibling lane `../wifi-smac-start` measured a per-band +672 value that the
  same 0xD1B68 loop writes on our bench boot, so the loop (and therefore the
  0xC016C call before its end) ran.
* **P2 == 0** -> record P1/P6/P7; go to rung 1.

## 2. Rung 1 - stock control (read-only)

Repeat P1-P8 on a stock boot at the same phase (2.4 GHz AP up).  Expected on
stock: P2=0x001060F7, P3=P4=0x0017525C, P7=2, P1=1.  The difference set between
our boot and stock is the candidate root cause:

| difference | meaning |
| --- | --- |
| P1 differs (stock 1, ours 0/>1) | the gate/state delta; the init was skipped or the attach aborted |
| P7 differs (stock 2, ours !=2) | 0xD1B68 never wrote the array header -> the body never ran |
| P2/P3 differ with P1=P7=2 | the init ran but wrote elsewhere (band/object mix-up) - re-check P5/P6 |

## 3. Rung 2 - firmware-internal escalation (only if rung 0/1 say the init did not run)

No new host message or boot-order change is identified by this decode as the
fix, and inventing one would be a blind fire.  The escalation options, in order:

1. **Boot-stage correlation (read-only):** capture the firmware RAM tuple
   (P1/P2/P7 + the driver boot status) at the earliest point the vendor driver
   allows, to find whether the gate flag was ever 1 and whether the registered
   body ran.  This is a read-only step.
2. **The existing rungs of the f1-thread fire recipe** (EDCA auth post) are *not*
   re-authorised here: they test a different hypothesis (H2D context sync) and
   are write paths.  If the Lead still wants to run them, they are governed by
   `../wifi-rx-f1-thread/FIRE-RECIPE.md` unchanged.
3. **A host-visible action remains unfound** for this init: the captured stock
   H2D families and the 20 stock-only g0-id1 keys are runtime handling and do not
   appear in the 0x46970 -> 0x47624 -> 0xD1B68 -> 0xC016C chain.  Until a boot
   stage is found where the gate is 1 on stock and not on ours, no message is
   prescribed.

## 4. Acceptance / rollback

* Rung 0 acceptance: 8 words recorded, tuple coherent, page/status unchanged.
* Rung 1 acceptance: same-phase stock tuple; difference set written down.
* No recovery action is expected from a read-only pass.  If a read aborts the
  window, stop that pass and reboot only through the normal attended cycle.
* Any fire beyond a read-only pass is **not** covered by this recipe.
