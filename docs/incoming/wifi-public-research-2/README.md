# wifi-public-research-2 - index

**Lane:** public-research2 (shared task `task-152`), host-only web research for the
Cudy WR3000 v2.0 (Hi5671YV200 "luofu v2" + Hi5622V100) Wi-Fi RE project.
**No bench contact, no device access, no vendor binaries/firmware committed.**
**Written:** 2026-11-25 (host clock).

## Files

| file | contents |
| --- | --- |
| `REPORT.md` | main deliverable: exact-chip source hunt, Hi3881/Hi1105 sibling AP/STA management flow, the mgmt-TX/RX contract, the DMAC RX filter, Linux RX-ring cross-check, scan hints, licensing, negatives |
| `RANKED-NEXT-TRIES.md` | actionable ordered list for the Lead (static vs bench, pass/fail, risks) |
| `SOURCES.tsv` | every fetched source: URL (commit-pinned), byte count, sha256, local evidence path + first line |
| `VERIFY-fetch.txt` | re-download check of all 51 remote sources at the pinned commits (51/51 byte+sha256 match) |

Local (gitignored) evidence: `build/wifi-public-research-2/fetched/`
(`hi3881/`, `hi1105/`, `linux/`, `oh/`, `web/`). Nothing from it is required to
read this deliverable - all claims are traceable to URLs/hashes in
`SOURCES.tsv`.

## Headline

1. **The mgmt-TX carrier we implemented is a host-internal WAL request, not a
   device message.** The sibling drivers (Hi3881 2021, Hi1105 Wi-Fi 7) dispatch
   `WLAN_CFGID_CFG80211_MGMT_TX` to in-host `hmac_wpas_mgmt_tx`, which sends
   the frame to the DMAC as a `WLAN_CTX / DMAC_WLAN_CTX_EVENT_SUB_TYPE_MGMT`
   event; completion is a CRX "mgmt tx status" event. The PD4 table-A absence
   of `0x01F2` is therefore expected. Our committed vendor disassembly
   corroborates (raw `hcc_msg_alloc`/`hcc_msg_tx` with MGMT payload, not the
   WAL TLV).
2. **Management RX is a CRX event (our g1 id13 family), not data-ring traffic.**
   Probe requests under a client scan are expected there; ring drain staying 0
   for mgmt is normal. EAPOL/data after association is what the ring sees.
3. **The DMAC RX filter must be programmed by the host** (AP-UP single-VAP value
   `0xF7B9FAEA`; support_any clears BIT4 for other-BSS broadcast mgmt incl.
   probe req). Our AP start has no filter step visible -> ranked try #2.
4. **Linux RX-ring cross-check** (mt76/iwlwifi/ath10k/ath11k/brcmfmac/mwifiex):
   publishing the free-buffer producer index *is* the RX enable; completion
   entries carry buffer addresses; the host consumes/refills/republishes at a
   watermark. Matches the rx-static live-bank model; suggests adding a refill
   watermark before sustained traffic.
5. **No public exact-chip source, no GPL drop** (Cudy WR3000 2.0 GPL section empty;
   Jeton no GPL; OpenHarmony only hi3881v100; mainline none) - upstream/legal
   blocker unchanged.

## Licensing note (short)

`opensource/` is MIT. The sibling driver code is GPL-2.0 (per-file) / Apache-2.0
(dir label); **do not copy code text into this repo** - use facts/structures in
original clean-room code only (details in `REPORT.md` section 10).

## Reproduce

```sh
# pins
#   OpenHarmony device_soc_hisilicon: 7c1e2d5b35c090009bbe195f09337701c60af032
#   hongmengkernel (hi1105):         297b33b07b56adb3fa53baf618fdbc16ad9fd447
#   torvalds/linux:                  4dce97c6c265544342e716e62fd24bbb232590c5
# SOURCES.tsv: url <TAB> bytes <TAB> sha256 <TAB> evidence-path :: first-line
# VERIFY-fetch.txt: re-fetch status of the 51 remote rows
```
