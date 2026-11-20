# incoming/qm-decode - round 4l: W14 page classification + W15 content dump (patch 050)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 17).  Prior: `032`...`049`.
This package delivers **patch 050**: the page-by-page classification of the W14 diff (runtime counters,
not missing configuration) and a read-only content dump that proves it on both sides.  No device
access in this lane.

Apply `shared.patch` from the `opensource` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. W14 page map (image 603fcded + stock devmem)

Fifteen pages are non-zero on stock and zero/different on ours (pe/eqm/eps/pa/ifc/l2/napt/mcc/opp/fam
at offsets 0x1000/0x2000/0x6000/0x40000/0x41000/0x42000/0x46000).  Persistent-looking pages already
**match** (pe+0x2000/0x42000 0xffff0000, eqm+0x0/0x40000, eps+0x0, pfe_glb 0x7af, mcc+0x0 0x201410,
pub+0x57000 0x10001).

## 2. Classification: the differing pages are runtime counters/status, not config

Evidence (all [I]-strong, from the pinned fixtures + the page map):
- every differing offset appears in the vendor modules **only as a read inside
  hi_dp_dfx_dump_cnt / hi_pfe_dfx_dump_cnt** (the DFX counter-dump functions) - there is **no init
  write** to any of them in the decoded access inventory;
- the same value 0x1b6 appears in five engines (pe+0x1000, pe+0x41000, eqm+0x2000, eqm+0x42000,
  eps+0x6000) - a mirrored/global age counter;
- the two fam pages read 0x2f9 and 0x2fa (one apart, sampled microseconds apart) - a running counter;
- our own values are small non-zero counters (0x11), consistent with counters that advance with
  uptime rather than configuration;
- the vendor table-manager kthread (hi_lsw_pub tab_mgr_kthread) and CFE aging path update such
  registers at runtime, and those modules cannot load on this 6.18 kernel.

**Conclusion: don't replay them.**  Writing counters would be wrong and would not open RX.

## 3. Patch 050 (W15): content dump to confirm the classification

`luofu_switch.pagedump=1@BT@ (read-only): prints the first 16 words of every differing page plus the
matched control pages, reads each page **twice 10 ms apart** and labels it **STABLE**
(persistent-candidate) or lists the words that **CHANGED** (runtime counter/status - do not replay).
A stock devmem dump of the same pages confirms the labels; only pages stable on both sides become a
following gated write set.  No hardware writes; the armed path is unchanged.

## 4. Expected console lines

```
W15_PAGE_DUMP: read-only content dump of the W14 differing pages + matched controls (16 words, read twice 10 ms apart), no hardware writes
page-dump pe+0x01000 [ 1.. 4]: ...
...
page-dump pe+0x01000 word 0 CHANGED 0x000001b6 -> 0x000001b7 (runtime counter/status, do not replay)
page-dump pe+0x02000 [ 1.. 4]: ... ffff0000 ...
page-dump pe+0x02000 STABLE (persistent-candidate)
...
W15_PAGE_DUMP: done; compare these contents with a stock devmem dump of the same pages
```

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/050-net-lsw-pagedump-w15.patch` | W15 page-content dump + stable/volatile labelling | `ae2972c2dc2082ea1cc3b659a34ca1059ae24a3c46cf01b3f37531c79fa5b833` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | all pending docs (W10..W15) | `8d233f7b625af29bf0ae4ecba87468ea55a67209ca2b139cd88e6a3291b927ef` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- Series apply (current full series + 050 = 51 patches): 51/51, 0 failed hunks, 0 .rej/.orig
  (`logs/10-series-apply-051b.log`).
- WERROR compile clean; checkpatch 0 errors, 0 checks (`logs/11-checkpatch-050.log`).
- Shared docs patch applies cleanly; no devicetree/binding/Kconfig change.

## 7. GEMAC side

Our gmac dump covers offsets 0x0000-0x023f for eth2 port 11 (`build/evidence-bench-gates/2026-11-20-gmac-dump-ours.txt`).
The stock partial read covers the same 0x0-0x23f range; if a full MAC diff is wanted, a re-read is
needed only for any offsets beyond 0x23f (the MAC window continues to 0xfff) - say the word and the
next dump can sweep 0x0-0x3ff on both sides.
