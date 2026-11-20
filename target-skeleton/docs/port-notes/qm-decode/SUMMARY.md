# incoming/qm-decode - round 4i: PIE/GEMAC RX-state dump (patch 047)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 14).  Prior record: `032`...`046`.
This package delivers **patch 047**: the read-only RX-state comparison (PIE + GEMAC) needed to find
the missing RX-enable path after all LSW stages verified.  No device access in this lane.

Apply `shared.patch` from the `opensource` root (carries W10 + token fix + W11 + W12 + W13 docs), then move
this SUMMARY to `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Why this comparison

The W12 image (0a6849ed) verified W3-W12: corrected W10 tab128 entries read back exactly as stock,
W12 EPS/DP words verified, heartbeat stable through traffic - yet **no PIE RX interrupt has ever
fired** and no box frame has ever appeared.  The LSW stock-vs-ours dump windows are now exhausted
(only non-config EQM indirect mirrors remain), so the comparison moves to the PIE RX/TX and GEMAC
register windows.

## 2. Patch 047 (read-only diagnostics, no writes)

- `luofu_pie.dump=1@BT@: after arming, prints the PIE RX/TX state at the addresses the stock
  inventory (PIE-DATAPATH 4.5) already measured: per-channel blocks 0x0180/0x0200/0x0300/0x0380/
  0x0400+ch*4, 0x1000, 0x2200, 0x3200/0x3204/0x3208, 0x3210/0x3290/0x3310+ch*4, 0x3400..0x340c,
  0x3500..0x353c, 0x3700, TX rings 0x2000..0x201f, RX rings 0x3000..0x301f.
- `luofu_gmac.dump=1@BT@: at every link-up, prints MAC window offsets 0x0000-0x023f (ctrl,
  max-frame, port-mode, status blocks) for comparison with devmem reads of the stock gmac windows
  0x14380000/0x14400000/0x14480000/0x14500000.
- Nothing is written; both stages are opt-in and do not change the armed path.

## 3. Leading RX-enable candidates (from the historic stock inventory)

| address | stock | ours (driver) | note |
| --- | --- | --- | --- |
| PIE 0x3404 | 0x1b04 | 0 (reg_init) | per-channel RX config, [U] encoding; vendor rx_init_chan programs it per channel |
| PIE 0x3700 | 0xffff | never written | GRO size word, [U]; written unconditionally by the vendor path |
| PIE 0x3200 | 0x2001 (bit0 enable + bit13) | bit0 set by our rx_enable | bit13 meaning [U] |
| PIE 0x0400 masks | ch0/ch1 = 0 unmasked | our active channels unmasked | matches |

The dump decides whether our live values match; any difference becomes the gated **W13 write set**
(snapshot / stable-two-reads / exact readback / restore, same discipline as W3-W12).

## 4. Attended procedure

1. Boot the current series with the full chain plus `luofu_pie.dump=1 luofu_gmac.dump=1@BT@ (no other
   changes); capture the `pie-dump@BT@/ @BT@`gmac-dump@BT@ blocks.
2. On a stock boot, devmem-read the same PIE offsets (base 0x10a70000) and the gmac window offsets
   above.  The stock PIE values from the 2026-11-18 inventory are already in PIE-DATAPATH 4.5 for
   the PIE side.
3. Diff: a difference at 0x3404/0x3700 (or any other RX-path register) is the next write set; the
   matching values confirm the RX enable is elsewhere (LSW flow/port state outside the dumped
   windows) and the next target becomes the CFE/rootif table.

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/047-net-pie-gmac-rxstate-dump.patch` | PIE + GEMAC read-only RX-state dumps | `587e0f851b55e30b350d76bbcd7b9fceb74cbe2c12692c310bd0dc7f0e889b0a` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | W10 + token fix + W11 + W12 + W13 docs (5 files) | `6d5c9746b4d563b8d7100a05d2fcd973e563799a83271af2baffb5c151848706` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- Series apply (current full series + 047 = 48 patches): 48/48, 0 failed hunks, 0 .rej/.orig.
- WERROR compile clean for both changed drivers; checkpatch 0 errors, 0 checks
  (`logs/11-checkpatch-047.log`).
- Shared docs patch applies cleanly to the current `opensource` tree; no devicetree/binding/Kconfig change.
