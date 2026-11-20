# incoming/qm-decode - round 4f: gate-token naming fix (patch 044)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 11).  Prior record: `032`, `036`,
`038`, `040`, `041`, `042`, `043` (W10 IFC).  This package delivers **patch 044**:
the IFC/W10 gate-token naming fix (driver messages, comments and docs).  No device access.

Apply `shared.patch` from the `opensource` root - it now carries the W10 section *and* the token fix -
then move this SUMMARY to `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. The bug

Image 08449a86 was built with `luofu_lsw.ifc=1` and the W10 stage never ran: the refusal message said
"set luofu_lsw.ifc=1", but the switch driver is built-in under the module name **luofu_switch**, so
the accepted parameter token is `luofu_switch.ifc=1` (identical to the working
arm/egress/qidmap/eps/qminit/pfe tokens).  The parameter names themselves were always correct; the
error strings, comments and docs all used the wrong `luofu_lsw.` prefix and led the cmdline astray.
The same wrong prefix was present in the GEMAC refusal message and in the shared docs.

## 2. Fix (patch 044)

- Drivers: every `luofu_lsw.<gate>` occurrence in error strings, comments and parameter descriptions
  changed to `luofu_switch.<gate>` - luofu-switch.c (27), luofu-switch-regs.h (5), luofu-gmac.c (2).
  Error messages now name the working syntax, e.g. "set luofu_switch.ifc=1" and "the attended boot
  needs luofu_switch.arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1 ifc=1".
- Shared docs: all gate spellings in LSW-SPEC.md, SKELETON-SMOKE.md, PIE-DATAPATH.md, TARGET-DESIGN.md
  and the target README changed to `luofu_switch.` (~70 occurrences), including the new W10 section.
  Historical kernel symbol names (`luofu_lsw_probe` in a System.map quote) are deliberately left
  untouched - they are symbols, not parameter tokens.

## 3. Working attended tokens (all module-built-in -> underscore module names)

```
luofu_switch.arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1 ifc=1
luofu_gmac.w9=1 [hwinit=1 [hwinitwrite=1 hwinit_port=11]] | luofu_gmac.trace=1 txhalt=1 macconf=1
luofu_pie.arm=1 nodoorbell=1
luofu_smp_early_probe=2
```

## 4. Bench expectations for the next run (image with 001-045)

Same W10 expectations as before, but with `luofu_switch.ifc=1`: the W10 start line (command-engine
pre-state, tab128 base), `W10: IFC default-classification table programmed and verified: 8 entries ...`,
then the armed line, GEMAC registrations, link, heartbeat.  Then the traffic test: a box ARP reply
on the wire (or PIE RX trace lines) with no watchdog hang.  Failure signatures remain the engine
bit29 / poll-timeout lines and the tab128 readback mismatch (restore + refusal).

## 5. W9 corrected single-port write

Unchanged recommendation: second boot of the same image with `luofu_gmac.w9=1 hwinitwrite=1`
(cmdline-only change), after the IFC run; heartbeat detector makes it a cheap independent test of
the corrected 1518 frame limit.

## 6. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/044-net-lsw-ifc-gate-token-fix.patch` | driver gate-token fix (messages/comments/param descriptions) | `a89ec91abdd6849c7ed159b5c4d14d4fc2418ab19095f20fe485b0545b123905` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | W10 docs + full `luofu_switch` token fix across 5 docs | `63228c25d48f58b650465891923e930cbbe6d822919ff564b72db7a9fb72eed5` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 7. Validation

- **Series apply (current full series + 043 + 044 = 45 patches):** 45/45, 0 failed hunks,
  0 .rej/.orig; all changed drivers byte-identical to the lane tree
  (`logs/10-series-apply-045.log`).
- **WERROR compile:** luofu-switch.o / luofu-gmac.o rebuilt clean under `W=1 -Werror`.
- **checkpatch:** 0 errors, 0 checks, only the expected commit-description warning
  (`logs/11-checkpatch-044.log`).
- Shared docs patch verified to apply cleanly to the current `opensource` tree.
- No devicetree/binding/Kconfig change; no vendor binary or per-unit value added.
