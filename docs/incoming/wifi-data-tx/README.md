# wifi-data-tx (task-150) - peer-aware host -> AP data TX

**Status: HOST-ONLY DELIVERABLE - built, host-tested, NOT fired.**  The Lead
owns the bench, the console and the recovery door.

This lane implements the missing direction of the 4-way handshake: hostapd's
EAPOL-Key msg1/msg3 (and any bridged data) leaving through the AP netdev's
ndo_start_xmit now build a real 802.11 data MPDU and go through the **proven
dp TX ring / MSDU-table publish / TXQ-table path** that the dp-tuning lane
fired as COMPLETED, instead of being dropped with a counter.  It is the
follow-up the wifi-sta-ops lane (task-149) named as its top next step.

    ndo_start_xmit
      -> 802.3 parse + peer-aware decision (sta-ops station table + AUTHORIZED)
      -> 802.11 data MPDU (FromDS=1, addr1=DA/addr2=BSSID/addr3=SA,
         LLC/SNAP AA AA 03 00 00 00 + type, optional Protected bit)
      -> coherent frame buffer [owner][16 B zero][MPDU] from a 16-buffer pool
      -> hi5622_dp_tx_data_post(): 8-byte MSDU ring entry -> dma_wmb ->
         packed-index publish into the device-RAM table entry -> read-back ->
         TXQ-table publish
      -> bounded completion poll on the device-consumed rptr -> skb freed,
         EAPOL-Key TX-completion detector (vendor 4/4-like shape) recorded

Fail-closed: **data_tx=0 (default)** keeps the previous accounted drop and
allocates nothing.  data_tx=1 + data_tx_peer=1 + a peer-aware decision + a
bounded completion timeout (lane closes on stall).

## Deliverables

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | full report: what/why, decoded facts, implementation, build identity, verification, limits, remaining blocker |
| [TABLES.md](TABLES.md) | machine-readable wire facts + the evidence trail (T1-T9) |
| [LADDER.md](LADDER.md) | attended ladder L0-L6 (design only), abort policy |
| [ladders/data-tx.json](ladders/data-tx.json) | the same ladder, machine-readable |
| [patches/0001-data-tx.patch](patches/0001-data-tx.patch) | the driver patch (11 files) on the FINAL ALL-PATCH + wifi-sta-ops tree |
| src/ | the new portable logic (host-tested) + kernel glue, verbatim copies of the patch files |
| tools/data_tx_selftest.c | 151-check portable selftest (literal wire vectors) |
| tools/build-data-tx-module.sh | reproducibility gate: base + sta-ops + this patch -> 6.18.60 ARM module + identity |
| tools/run-host-check.sh | fast host gate: selftest + patch/doc/ladder/evidence checks |
| evidence/ | build evidence + host-check output (text only) |
| refs/artifact-identity.txt | artifact + source identities and the pinned base chain |

## Identity

    lane artifact (local/gitignored)   stripped 335,752 B sha256 91aa1aa5...
    base fired all-patch artifact      stripped 308,080 B sha256 5b587949...
    prereq wifi-sta-ops (task-149)     50 params on the same base
    this lane                          54 params total
    vermagic 6.18.60 SMP mod_unload ARMv7 p2v8 ; depends=cfg80211

The lane artifact is NOT part of any image; the next bake must stack the
wifi-sta-ops patch and this patch.

## Reproduce / verify (host)

    ./tools/build-data-tx-module.sh
    ./tools/run-host-check.sh

## What is proven and what is not

* **Proven host-side:** the patch stack applies to the pinned FINAL
  ALL-PATCH tree + sta-ops, builds with 0 warnings, strip leaves .text
  byte-identical, 54 params, expected symbols only; 151/151 portable wire
  checks pass.
* **Not proven (needs the bench):** that the MAC consumes a host-posted
  unicast/broadcast data frame (L3/L4 answer it), that the firmware applies
  per-user keys for Protected-bit host frames, and the full 4-way - which is
  additionally **blocked at association** by the still-fail-closed .mgmt_tx
  path (a mgmt-tx follow-up lane is the dependency, stated in LADDER L5).

## Safety / publication

Text only in git.  No vendor binary/object/firmware/calibration byte, no
per-unit MAC/serial, no secret.  The vendor module and PD4 image stay
local/gitignored; derived offsets are cited to the committed lane docs.
Documentation-example MACs only (02:00:68:69:35:01/02).  No bench contact by
this lane.
