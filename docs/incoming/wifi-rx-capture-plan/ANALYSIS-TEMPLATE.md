# ANALYSIS-TEMPLATE - E-RX-1 result worksheet

Fill this from the classifier output and the evidence bundle. Keep the raw
values; do not paraphrase. A verdict without the evidence rows is not evidence.

Bundle: build/wifi-rx-capture/______________________    date/time UTC: ________________
Module: sha256 ______________________ params rx_enable=___ rx_refill_wm=___ mgmt_rx=___
Session: AP-ENABLED ___  dp_setup ___  txq_addr_sync ___  dp_rx_arm ___  drain loop running? ___

## 1. Preconditions at capture start (router check + pre snapshot)

| check | expected | observed | pass? |
| --- | --- | --- | --- |
| live status CA 0x40050000 +0x00 | 0x00000182 | | |
| norm rptr / small rptr | 0x20 / 0x20 | | |
| cmp wptr | 0x0 | | |
| interrupt mask CA 0x40040000 +0x48 | 0xfffffcc1 | | |
| page 0x1bf000 | 0x86807fef/7/2/0xabcd | | |
| state CA 0x118b60 | 1 | | |
| exception mask CA 0x400008 | 0 | | |
| staging CA 0x40000c | 0 | | |
| hcc excp_announce | 0 | | |
| dmesg "DP rx_arm: live bank" this load | present | | |
| raw /dev/mem sampler | AVAILABLE | | |
| client AP visible in scan | yes (-70 dBm gate) | | |

## 2. Trigger proof (client side)

| item | value |
| --- | --- |
| harness attempts run | |
| client "send auth to 06:05:04:03:02:01" lines | count: ____ first/last: __________________ |
| client stage per attempt (AUTH-TIMEOUT expected) | |
| AP BSSID / channel | 06:05:04:03:02:01 / 6 |
| attempt windows (epoch from host-markers.log) | |

## 3. During-window observations (sampler)

| signal | baseline | during max/any | post | changed? |
| --- | --- | --- | --- | --- |
| live status word | | | | |
| status bits set (decode REPORT 5.2) | | | | |
| norm rptr | | | | |
| small rptr | | | | |
| cmp wptr | | | | |
| page golden flag | | | | |
| vector mask / staging | | | | |
| excp_announce | | | | |

If a crash occurred: first-deviation tick/epoch ________; last action before it
________; crash-first.txt quote ______________________.

## 4. Memory channels (pre vs post snapshot)

| channel | pre | post | changed? |
| --- | --- | --- | --- |
| cmp ring non-zero entries | | | |
| norm ring non-zero entries (256) | | | |
| small ring non-zero entries (512) | | | |
| buffer header diffs (count) | n/a | | |
| other live-bank window diffs | n/a | | |
| config block window diffs | n/a | | |

## 5. D2H catalogue and host delivery

| item | pre | post | delta |
| --- | --- | --- | --- |
| hcc d2h words | | | |
| per-id counts (d2h bitmap) | | | |
| rd_catalog (g/id counts) | | | |
| rd_decode g1 id13 frames kept | | | |
| dmesg mgmt RX delivered lines | | | |
| cfg80211_status / hostapd log activity | | | |

## 6. Verdict worksheet (circle one; classifier prints the same)

    [ ] V-MAC-NOSEE            all host-visible RX channels at baseline, trigger proven
    [ ] V-MAC-SAW-NOWRITE      status RX error bits / bit2 without a completion write
    [ ] V-MAC-WROTE-NOEVENT    completion/buffer write appeared, no g1 id13
    [ ] V-EVENT-NOT-DELIVERED  g1 id13 appeared, no host delivery line
    [ ] V-DELIVERED-NO-AP      g1 id13 + delivery, no AP-layer progress
    [ ] V-CRASH                exception page/mask/staging deviation
    [ ] V-NOTRIGGER            no client transmission proof
    [ ] V-INCONCLUSIVE         missing raw / sampler / trigger channel

Classifier verdict: ____________________  reason: ______________________________________
Agrees with manual reading? ___  if not, why: _________________________________________

## 7. Next action (from REPORT.md 7.2)

______________________________________________________________________________________
______________________________________________________________________________________

## 8. Notes / new observations

* Any status bit outside the decoded map (bit > 10) seen? raw word: ______________
* Any D2H (g,id) family delta not explained? ____________________________________
* Any page/state word that changed but stayed "golden"? _________________________
* Anything the static RE got wrong? ____________________________________________

