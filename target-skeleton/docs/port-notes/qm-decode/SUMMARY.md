# incoming/qm-decode - round 4m: static GEMAC config words (W16, patch 051)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 18).  Prior: `032`...`050`.
This package delivers **patch 051**: the W16 static GEMAC config stage from the MAC window diff.
No device access in this lane.

Apply `shared.patch` from the `opensource` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. The GEMAC diff (144 common words, window 0x14480000)

Six differences (ours -> stock): 0x1c 0x0 -> 0x1, 0xa4 0x40000 -> 0x30000, 0xac 0x5800002 ->
0x1c00002, 0xb0 0xc043006 -> 0x1608006, 0xb8 0x210021 -> 0x690069, 0x100 0x334005ee -> 0x33400600.

## 2. Classification from hi_kmac.ko

- **0x1c = static config** [V]: hi_mac_hw_config writes it as 1 (the same word the macconf diagnostic
  sequence writes).  Ours was 0 only because macconf was off in the dump image.
- **0x100 = max-frame + pause word** [V measured, U formula]: hi_mac_hw_mtu sets the low 14 bits to
  (frame-length argument + 4); hi_mac_hw_set_pause preserves the word and only rewrites the pause
  bits in the MSB byte (word bits 28-29).  Stock 0x33400600 -> limit 0x600 = **1536**, i.e. 18
  bytes above our MTU+18 value 0x5ee = 1518: the vendor frame-length convention adds 36, not 18.
- **0xa4 / 0xac / 0xb0 / 0xb8 = statistics counters** [V read-only]: read through the read-op in
  himac_read_stats / himac_get_stats64 (32-bit counter pairs); no init write exists in the recovered
  paths.  Their differences are runtime counter values - **deliberately not replayed**.

## 3. Patch 051 (W16) = `luofu_gmac.macregs=1@BT@

Required for netdev registration (fail-closed).  Snapshot / stable-two-reads / write / exact
readback / restore-on-mismatch for:
- gmac+0x1c = 1;
- gmac+0x100 = (pre & ~GENMASK(13,0)) | 0x600 (keeps the vendor pause bits and upper 18 bits).

Counter words are left untouched.  Expected lines:

```
luofu-gmac ...: W16_MACREG: gmac+0x1c pre=0x0 -> 1; gmac+0x100 pre=0x334005ee -> 0x33400600 (stock-observed max-frame 1536, our W9 formula was 1518); counter words 0xa4/ac/b0/b8 are statistics, not replayed
luofu-gmac ...: W16_MACREG: static GEMAC config programmed and verified (0x1c=1, 0x100=0x33400600)
```

Failure signature (box stays up, netdev refused): W16_MACREG stability refusal or readback mismatch
with the snapshot restore notice.

## 4. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/051-net-gmac-macregs-w16.patch` | W16 static GEMAC config (0x1c=1, 0x100 limit 1536) | `45d4c6ec97c0f5b3d57d7a85368fa126c1e16852d6f2a31a615fbd0ff26e4311` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | all pending docs (W10..W16) | `5d237264961d8e628de8bfddc465b2b3e557ac1cce999cd7e0029b924f7331c6` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 5. Validation

- Series apply (current full series + 051 = 52 patches): 52/52, 0 failed hunks, 0 .rej/.orig
  (`logs/10-series-apply-052b.log`).
- WERROR compile clean; checkpatch 0 errors, 0 checks (`logs/11-checkpatch-051.log`).
- Shared docs patch applies cleanly; no devicetree/binding/Kconfig change.

## 6. Note

Our dump covers 0x0..0x2fc and the stock read 0x0..0x23c; the remaining stock words 0x240..0x2fc
can be read if a full-window diff is wanted - say the word and the next dump keeps the same range
on both sides.
