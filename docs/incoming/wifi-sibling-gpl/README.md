# wifi-sibling-gpl - Hi3881V100 / Hi1105 sibling GPL RX + flow-control + AP/STA comparison

**Lane:** wifi-sibling-gpl (shared task task-151), **host-only** (web research + static
analysis; no bench contact, no device access).  **Written:** 2026-11-25.

**Deliverables**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | full analysis: source set, Hi3881V100 RX/mgmt paths, Hi1105 PCIe/HAL/N-API paths, our-model comparison, ranked RX root-cause hypotheses, concrete patch proposal, ladder |
| [TABLES.md](TABLES.md) | the decoded constants and side-by-side tables (T1-T9) with source line citations |
| [PATCH-PROPOSAL.md](PATCH-PROPOSAL.md) | concrete, host-tested proposal: completion-entry validity wait + clear-after-read + RX stall classification (RX), station-before-assoc-response ordering guard (mgmt/assoc) |
| [MGMT-CARRIER-CORRECTION.md](MGMT-CARRIER-CORRECTION.md) | **task-151 follow-up requested by the Lead:** the device mgmt-TX carrier is a raw HCC g0 id 6 control message (not WAL 0x01F2) - full static decode, corrected builder/patch (builds 0 warnings), the luofu SET_RX_FILTER negative result, g1 id13 RX assessment, EAPOL watermark note, and the M2' fire recipe |
| [patches/0001-mgmt-tx-hcc-g0id6-carrier.patch](patches/0001-mgmt-tx-hcc-g0id6-carrier.patch) | corrected mgmt-TX carrier patch against the final association tree (applies cleanly; 5 files) |
| [src/hi5622_mgmt_logic.*](src/) | reference implementation of the corrected builder (same code as the patch, host-compilable) |
| [tools/mgmt_ctrl_selftest.c](tools/mgmt_ctrl_selftest.c) | 41/41 host checks for the corrected carrier + g1 id13 RX decode |
| [tools/verify_quotes.py](tools/verify_quotes.py) | host tool that re-fetches every cited raw URL and re-checks the quoted snippets/line numbers (no vendor bytes stored) |
| [tools/rx_model_selftest.c](tools/rx_model_selftest.c) | strict-C99 host test for the proposed RX credit/validity/stall model (original clean-room code) |

**Licensing (read this before reusing anything)**

* Hi3881V100 = **GPLv2**: repo licence file lists `common/platform/wifi/hi3881v100` under
  "GNU General Public License, version 2", and every driver file carries the GPLv2 header.
  Quotes in this lane are **study-only**; no code is copied into our tree.
* Hi1105 (hongmengkernel) = **NOT open-licensed**: no LICENSE/COPYING file in the repo and the
  source files say "Copyright (c) HiSilicon ... All rights reserved."  It is used here as a
  **structure/vocabulary reference only** - no code, no constants as code, no reuse path.
* Our deliverables are original clean-room analysis/implementation (GPL-2.0 compatible) and
  contain no vendor binary, no firmware byte, no calibration value, no per-unit identity.

**Headline (details in REPORT.md §6)**

1. The "RX captures still zero while the laptop scans" observation is **consistent with the
   vendor architecture**: management frames are delivered on a separate control-event path, not
   on the MAC data completion ring.  Evidence: in our own vendor 2.5.24 module the D2H
   cfg-event dispatch table pairs **event id 0x0d (13) with `hmac_rx_process_mgmt_event`**, and
   that handler is the only entry into the AP/STA management FSM; the data ring entry processing
   never dispatches management.  The same split is explicit in the Hi3881 GPL stack
   (`FRW_EVENT_TYPE_WLAN_CRX` = mgmt/control vs `FRW_EVENT_TYPE_WLAN_DRX` = data) and in the
   Hi1105 HAL (RX completion ring -> data netbufs vs separate firmware-message events).
   Ring captures need **data** frames (post-association EAPOL/ARP/DHCP), not scan probes.
2. Sibling HALs **cross-validate the rx-static lane's "no extra RX enable" conclusion**: the
   Hi1105 RX start is ring reset/zero -> program base+size -> deassert reset -> fill free ring ->
   publish producer; there is no separate RX-enable register or H2D message.  What the host owes
   is the consumer/refill half (our `dp_rx_drain`).
3. Two sibling-derived refinements are worth carrying into our driver: a bounded
   **completion-entry validity wait** (producer pointer can be visible before the entry payload
   is committed) and **clear-after-read** of consumed entries; both are in PATCH-PROPOSAL.md and
   host-tested.
4. AP/STA management ordering cross-check: the vendor AP SME adds the **station/user context
   first**, then sends the assoc response, then syncs the associated state - exactly the
   `g0 id7 user_add` -> mgmt-tx assoc response -> `0x07d2 asoc-state` order the sta-ops lane
   implemented for us.  The sibling evidence supports a fail-closed ordering guard.

**Method / reproducibility:** all sibling sources were fetched with `git clone` from the two
public repos (pins in REPORT.md §2) into `/tmp`, read locally, and quoted with raw-URL + line
numbers; `tools/verify_quotes.py` re-fetches and re-checks every quote (network required).
