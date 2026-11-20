# incoming/qm-decode - round 4j: W13_RXCFG PIE RX-config + gmac dump fix (patch 048)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 15).  Prior: `032`...`047`.
This package delivers **patch 048**: the W13 gated write stage for the PIE RX-config word
(pie+0x3404 = stock 0x1b04) plus the gmac.dump emission fix, with decode of the rx_init_chan
formulas.  No device access in this lane.

Apply `shared.patch` from the `opensource` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. W13 dump result (image a24ba64e, evidence build/evidence-bench-gates/2026-11-20-pie-dump-ours.txt)

Everything matched the stored stock inventory (0x1000=0x000a01c8, 0x2200=0x0045e000, 0x3200=0x00002001,
0x3700=0x0000ffff, 0x0180/0x0200/0x0380=1 per channel, 0x0300 RX IRQ status=0) **except pie+0x3404:
ours 0x00000000 vs stock 0x00001b04**.  That is the most direct explanation for the never-firing RX
interrupt.

## 2. Decode of hi_pie_rx_init_chan (pinned hi_kpie.ko)

- `0x3404 |= ((cfg[16] & 3) << (ch * 2))` - 2 bits per initialised RX channel;
- `0x3408 |= BIT(ch)` when that channel's ring size is > 9600 bytes (ours is 0, consistent);
- `0x3504 + ch*0x10 = ring_size >> 7` - the RX ring-size unit our driver already programs;
- **no writer** for 0x3400 / 0x340c exists in the recovered paths (loader/hardware words; ours read
  0x103 / 0x00500858 and are deliberately left alone);
- 0x3700 (GRO size) matches stock already.

## 3. Patch 048 = W13_RXCFG + dump fix

- New gate `luofu_pie.rxcfg=1@BT@: snapshot pie+0x3404, require stable across two reads, write the
  stock-observed **0x1b04**, read back exactly, restore the snapshot on mismatch, fail-closed.  The
  computed 0x3408 expectation is logged but not written (0 for our <=9600 rings, matches stock).
- Fail-closed arming: with `luofu_pie.arm=1@BT@ the engine refuses to arm unless rxcfg is set, so the
  next image cmdline must include `luofu_pie.rxcfg=1@BT@.
- **gmac.dump fix:** patch 047 placed the dump call after the macconf gate's early return, so image
  a24ba64e printed no gmac-dump lines unless macconf=1; the call now runs on every link-up.

## 4. Expected bench log lines

```
luofu-pie 10a70000.pie: W13_RXCFG: pie+0x3404 pre=0x0 -> stock 0x1b04 (decoded rx_init_chan 2-bit/channel field); ring-size>9600 bitmap would be 0x0 (0x3408 currently 0x0, not written)
luofu-pie 10a70000.pie: W13_RXCFG: pie+0x3404 programmed and verified = 0x1b04 (was 0x0)
luofu-pie 10a70000.pie: PIE v250 armed: 2 RX channels, 2 TX channels, 4 interrupts
...
luofu-gmac ... eth2: W13_MACSTATE_DUMP: read-only GEMAC state of eth2 port 11, no hardware writes
luofu-gmac ... eth2: gmac-dump 0x0000: ...   (now printed with macconf off too)
```
Failure signatures (box stays up, engine refuses): W13_RXCFG stability refusal or readback mismatch
with the snapshot restore notice.  After this stage the traffic test should show the first PIE RX
interrupt / pie-trace RX/poll lines and a box ARP reply if 0x3404 was the missing enable; if not, the
next targets are the LSW flow/port state outside the dumped windows and the CFE/rootif table.

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/048-net-pie-rxcfg-w13.patch` | W13_RXCFG write stage + gmac.dump fix | `a642a997c79b2144bd533f35d84b2772f015019e74492043e85a324912470d5d` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | all pending docs (W10+token+W11+W12+W13+W13_RXCFG) | `eaed6c968500f3abd45e565e34f9f822d69bd23986eb1a2a20fe931d3747dc37` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- Series apply (current full series + 048 = 49 patches): 49/49, 0 failed hunks, 0 .rej/.orig;
  both changed drivers byte-identical to the lane tree (`logs/10-series-apply-049b.log`).
- WERROR compile clean; checkpatch 0 errors, 0 checks (`logs/11-checkpatch-048.log`).
- Shared docs patch applies cleanly to the current `opensource` tree; no devicetree/binding/Kconfig change.
