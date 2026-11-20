# incoming/qm-decode - round 4k: all-window page scan (patch 049)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 16).  Prior: `032`...`048`.
This package delivers **patch 049**: read-only page-granularity scans of every LSW and PIE window,
to locate the vendor CFE runtime flow/port tables that our image never programs.  No device access.

Apply `shared.patch` from the `opensource` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. State after W13_RXCFG (image 28034919)

W13_RXCFG programmed and verified pie+0x3404 = 0x1b04; the gmac dump works (printed at both
link-ups); the full W3-W13 chain armed with a stable heartbeat through the traffic test.  RX still
never fires: zero PIE RX polls, no box frames.  **Every comparable programmable register now matches
the stock inventory** (PIE except the no-writer 0x3400/0x340c, LSW W3-W12, MAC link words, W13).

## 2. Decode finding: the missing class is vendor CFE runtime state

- `hi_kcfe_pub@BT@ imports only netdev/netdevice-notifier APIs - it performs **no hardware access**;
  the table-manager/hardware consumers are `hi_kcara@BT@ / `hi_swa@BT@ (`hi_tab_mgr_*@BT@,
  `hi_port_set_igr_car@BT@, `hi_queue_*@BT@), and none of them can load on this 6.18 kernel.
- So the flow/port-forwarding tables the stock vendor stack installs on netdev/port events are
  simply absent on our image - and the W11 dump only sampled a few offsets per window, so those
  tables can be anywhere in the 15 MMIO windows.

## 3. Patch 049 (W14): page scans for the stock-vs-ours table diff

- `luofu_switch.scan=1@BT@: after the armed path, reads one word per 4 KiB page of every MMIO window
  (15 windows x 128 pages; queue/flow DDR windows skipped) and prints only **non-zero pages** as
  `lsw-scan <window>+0x<off> = <word>@BT@, then a summary count.  No writes.
- `luofu_pie.scan=1@BT@: same for the 16 pages of the 64 KiB PIE window
  (`pie-scan <off> = <word>@BT@).
- Stock side (devmem loop, same addresses):

```
for w in 0x11000000 0x11080000 0x11100000 0x11180000 0x11200000 0x11280000 0x11380000 \
         0x12000000 0x12080000 0x12100000 0x12180000 0x12280000 0x12380000 0x12400000 \
         0x12480000; do
  for o in $(seq 0 4096 520192); do
    v=$(devmem $(printf 0x%x $((w+o))))
    [ "$v" != "0x00000000" ] && echo "$w+0x$(printf %x $o) $v"
  done
done
# PIE window: base 0x10a70000, offsets 0x0..0xf000 step 0x1000
```

Diffing the non-zero page lists shows exactly which table pages exist on stock and not on ours; the
next patch (gated replay) targets those pages, decoded against the owning vendor module and with the
usual snapshot/readback/restore discipline.  This is the current most direct route to the
wire-to-CPU forwarding enable.

## 4. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/049-net-lsw-pie-page-scan.patch` | W14 all-window page scans (read-only) | `87d79556ad6611b81f1f2306918e41ebe751ad6dac878ba54239f5e5dd4b7d1e` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | all pending docs (W10+token+W11+W12+W13+W13_RXCFG+W14) | `ed23f4bf31fd13162b442cce692ec22a7de13f7ac3586cb2980f1a7924c68f9b` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 5. Validation

- Series apply (current full series + 049 = 50 patches): 50/50, 0 failed hunks, 0 .rej/.orig;
  both changed drivers byte-identical to the lane tree (`logs/10-series-apply-050b.log`).
- WERROR compile clean; checkpatch 0 errors, 0 checks (`logs/11-checkpatch-049.log`).
- Shared docs patch applies cleanly; no devicetree/binding/Kconfig change.

## 6. GEMAC side note

The gmac.dump capture (`build/evidence-bench-gates/2026-11-20-gmac-dump-ours.txt@BT@) is ready; a stock
devmem read of the four GEMAC window bases (0x14380000/0x14400000/0x14480000/0x14500000, offsets
0-0x23f) will complete that comparison in parallel with the page scan.
