# REPORT - wifi-data-tx: peer-aware host -> AP data TX (task-150)

**Lane:** wifi-data-tx (shared task task-150), **host-only**.
**Deliverable:** the missing host -> AP data TX path on the FINAL ALL-PATCH
clean-room tree + wifi-sta-ops: AP netdev ndo_start_xmit -> 802.11 data MPDU
(FromDS=1, LLC/SNAP) -> proven dp MSDU-ring/TXQ post -> bounded completion,
with hostapd's EAPOL-Key msg1/msg3 as the motivating traffic; host tests and
an attended ladder.
**Fired by this lane:** nothing - no device contact, no UART, no network, no
vendor binary or secret committed.  The Lead owns the bench.

Machine-readable facts: [TABLES.md](TABLES.md).
Ladder: [LADDER.md](LADDER.md) / [ladders/data-tx.json](ladders/data-tx.json).
Patch: [patches/0001-data-tx.patch](patches/0001-data-tx.patch) (11 files,
1,565 added lines).  Build identity:
[refs/artifact-identity.txt](refs/artifact-identity.txt).

---

## 0. TL;DR

1. **The gap this lane closes.**  After the 2026-11-29 one-boot validation the
   AP starts (5-message vendor order), hostapd reaches AP-ENABLED, the WPA2
   GTK is installed, TX is COMPLETED and RX is armed - but hostapd's
   EAPOL-Key msg1/msg3 are sent through its l2_packet socket ->
   dev_queue_xmit -> the AP netdev's ndo_start_xmit, and that was only an
   accounted drop ("xmit_dropped", wifi-sta-ops task-149 section 7).  This
   lane turns it into a real transmit path.
2. **What it does.**  ndo_start_xmit -> peer-aware decision (sta-ops station
   table) -> 802.3 to 802.11 data MPDU (24-byte header, FromDS=1, addr1=DA,
   addr2=BSSID, addr3=SA, LLC/SNAP AA AA 03 00 00 00 + type) -> a coherent
   frame buffer in the exact vendor layout -> the **proven dp TX sequence**
   (8-byte MSDU ring entry, dma_wmb, packed-index publish into the device-RAM
   table entry + read-back, TXQ-table publish) -> bounded completion poll on
   the device-consumed rptr -> skb freed, EAPOL-Key TX-completion detector
   run.
3. **Fail-closed by construction.**  New master parameter **data_tx=0**
   (default): the AP netdev keeps the old accounted drop and nothing is
   allocated.  data_tx=1 enables the lane; unicast requires a station-table
   peer (data_tx_peer=1 default); non-EAPOL data to an unauthorized peer is
   refused; the Protected bit is only set for authorized peers (hostapd's
   own msg3 ordering makes msg1/msg3 unprotected); a bounded completion
   timeout closes the lane for the module load with no retry.
4. **The vendor "4/4-like" TX-completion detector is implemented literally**
   (pairwise + key-data-length 0, hmac_is_eapol_key_ptk_4_4) and recorded,
   **but deliberately not used to authorize a station**: in the host-SME AP
   flow that shape matches msg1 (which has key-data-length 0), so using it as
   an authorization hook would authorize a peer before the handshake ran.
   hostapd's .change_station keeps ownership of AUTHORIZED.  Documented as a
   deviation from the vendor hook, with the AP-mode reasoning (TABLES T4).
5. **Base = the fired all-patch module + the wifi-sta-ops patch** (task-149).
   The lane is peer-aware, so it needs the station table; the patch therefore
   applies to the 50-param sta-ops successor, not to the 47-param fired tree.
   Resulting module: **54 params** (47 + 3 + 4).
6. **Build gate (host):** applies wifi-sta-ops + this patch to the pinned
   FINAL ALL-PATCH tree, asserts the fired base artifact sha 5b587949...,
   builds the 6.18.60 ARM module with **0 warnings**, strip leaves .text
   byte-identical, **stripped 335,752 B sha256 91aa1aa5...**, 54 params,
   expected symbols, no unexpected imports.  (The lane artifact is local/
   gitignored; only the identity is committed.)
7. **Host tests:** portable logic selftest **151/151 checks** (literal-byte
   MPDU vectors for EAPOL msg1/msg3/4-like and IPv4/ARP, LLC/SNAP placement,
   Protected bit position, QoS variant, EAPOL-Key field offsets, refusal
   paths, peer-decision matrix, frame-buffer layout, device-window checks,
   packed-index wrap arithmetic); run-host-check.sh additionally cross-checks
   the patch/ladder/docs/evidence.
8. **The 4-way itself is still blocked one step earlier** on a client
   association: the driver's .mgmt_tx is fail-closed and there is no mgmt RX,
   so hostapd cannot send AUTH/ASSOC responses (the documented usable-AP
   blocker; the on-air observer lane expects AUTH timeout).  Therefore this
   lane's ladder validates the data path without association (sysfs
   data_tx_probe msg1-shaped frame; broadcast ARP through ping) and keeps the
   full 4-way rung L5 as "EXPECTED-BLOCKED today", with its dependency stated
   in the ladder.
9. **Open unknowns the ladder resolves (all [U] today):** whether the
   firmware encrypts a Protected-bit host-posted data frame with the per-user
   key (no RE evidence covers TX-side key selection); whether msg3 - which
   hostapd sends before installing the TX key - suffices; whether the MAC
   accepts the subtype-0 frame shape; whether an extra firmware user context
   is needed for host TX.  data_tx_protect lets the ladder isolate the
   encryption question.
10. **Honest limits:** MPDU <= 768 B (the proven DP_TXRX_MAX_MPDU bound, so
    frames above ~734 B payload are dropped, not fragmented); 16-frame pool,
    drop-on-full; no software crypto; the sysfs dp_tx_frame instrument must
    not be used while the data path is live; the lane artifact was not fired.

## 1. Why this lane exists

The AP work reached a precise wall, stated by the previous lanes rather than
hidden:

* wifi-ap-mgmt (task-143): the vendor AP management lives in host software
  (hmac_ap_up_rx_mgmt -> hmac_mgmt_encap_auth_rsp / _asoc_rsp_ap ->
  wal_cfg80211_mgmt_tx); our .mgmt_tx is fail-closed, so a client AUTH gets
  no answer (the "usable-AP assoc blocker").
* wifi-wpa-static (task-139): the key commands 0x0078/0x0079/0x007A are
  decoded and the WPA2-AP GTK install is hardware-proven; the PTK needs the
  4-way, which needs association and EAPOL exchange.
* wifi-sta-ops (task-149): the per-STA firmware context (.add_station) and
  EAPOL/data RX delivery are implemented and host-tested, and the lane ends
  with the top next step: "host -> AP data TX", because hostapd's EAPOL-Key
  msg1/msg3 leave through ndo_start_xmit, which was a drop stub.
* The 2026-11-29 one-boot validation proved the rest of the chain in one
  boot: vendor 5-message AP start -> AP-ENABLED, WPA2-AP GTK, TX COMPLETED
  (dp_tx_frame), RX arm, page golden.

This lane is that next step.  It is the highest-value follow-up because with
association (mgmt-tx lane) plus this data path the 4-way handshake becomes
reachable end-to-end by hostapd alone.

## 2. Decoded facts reused (not re-derived, not copied)

All wire offsets are the published results of the earlier lanes
([TABLES.md](TABLES.md) T9 has the trail).  In short:

* **dp TX (wifi-dp-txrx/dp-tuning, hardware-fired):** the 8-byte MSDU ring
  entry ctl (len/path/header flag/tid/vap type), the per-TID producer index
  publish into the device-RAM 12-byte table entry {ring devva, wptr, rptr},
  the packed index encoding (idx[14:0] | phase<<15), the TXQ-table publish
  order, the frame buffer layout [host word][16 B zero][MPDU], and the
  completion observable: device rptr == published wptr ("COMPLETED").
* **sta-ops (task-149):** the host station table with the AUTHORIZED flag,
  the 802.11 address/QoS header length rules decoded from the vendor RX
  conversion, and the EAPOL-Key classification offsets (pairwise bit 3 at
  EAPOL+5, key-data-length at EAPOL+97) plus the vendor 4/4-like detector
  hmac_is_eapol_key_ptk_4_4 and its TX-completion call site.
* **wpa-static (task-139):** the pairwise key install g0 id1 cmd 0x0078
  (body: key index, pairwise, MAC[6], key[32], seq[16], key_len, seq_len,
  cipher) that a future msg3 encryption path would rely on.
* **hostapd 2026.08.07 (host tool, not vendor material):**
  PTKINITNEGOTIATING installs the pairwise PTK with KEY_FLAG_PAIRWISE_RX
  **before** sending msg3 (msg3 therefore has no TX key -> unprotected on the
  wire), and PTKINITDONE installs it RX_TX after msg4, then AUTHORIZED is set
  via the driver's .change_station.  This ordering is why the default protect
  policy keys off "authorized", not "key installed".

## 3. What is implemented

### 3.1 Portable logic (src/hi5622_data_tx_logic.c/.h, host-tested)

* 802.3 parse (14 B, VLAN refused) -> 802.11 data MPDU builder:
  FC 0x0208 (or 0x0288 QoS, test-only), Protected bit FC bit 14, 24/26-byte
  header, addr1=DA / addr2=BSSID / addr3=SA, seq control, LLC/SNAP
  AA AA 03 00 00 00 + type, no FCS; MPDU bounds at 768 B;
* EAPOL / EAPOL-Key classifier (type, length >= 95, pairwise, key-data
  length, message class msg1/msg3/other, 4/4-like shape);
* peer-aware TX decision: multicast allowed, unicast requires a known peer
  when peer_required, non-EAPOL to an unauthorized peer refused, Protected
  bit policy AUTO/ALWAYS/NEVER;
* frame buffer writer [owner][16 B zero][MPDU] and device-VA window check;
* completion arithmetic on the packed index with phase/wrap checks and
  fail-closed refusal of a backwards consumer.

### 3.2 Kernel glue (src/hi5622_data_tx.c/.h)

* module parameters data_tx (0), data_tx_peer (1), data_tx_protect (0),
  data_tx_timeout_ms (200);
* a 16-buffer coherent pool (0x400 B each, 16 KiB) allocated at cfg80211
  init **only when data_tx=1**, every buffer range-checked against
  0x80000000..0x8fffffff;
* ndo_start_xmit: validates skb (AP iface, running, linear, >= 14 B), runs
  the decision, builds the MPDU, posts; always consumes the skb
  (NETDEV_TX_OK) and accounts every refusal by category;
* completion: a delayed work (5 ms while in flight) reads the device rptr
  through hi5622_dp_tx_data_poll, frees the oldest in-flight skb per
  consumed ring entry, runs the EAPOL-Key TX-completion detector (one dmesg
  line per classified EAPOL completion, no key material), and arms the
  bounded timeout;
* timeout / stall: the lane closes itself for the load (stalled=1), returns
  the net stack's skbs, never reuses the DMA buffers after a stall;
* sysfs data_tx_status (read-only evidence: gates, counters, EAPOL classes,
  last frame) and data_tx_probe (write 1: a synthetic EAPOL-Key msg1-shaped
  802.3 frame to the documentation STA 02:00:68:69:35:02 through the same
  builder/ring/TXQ code as ndo_start_xmit, so the attended ladder can prove
  the TX path before an association exists).

### 3.3 Patch to the proven dp layer (minimal)

* hi5622_dp_txrx.{c,h}: two exported helpers -
  hi5622_dp_tx_data_post() (the exact ring-push/publish/TXQ sequence with
  read-back) and hi5622_dp_tx_data_poll() (read-only table snapshot); a
  tx_post_lock spinlock serializes the per-TID producer index against the
  existing sysfs dp_tx_frame instrument (which now takes the same spinlock
  inside its mutex), and the bound object is published as ctx->dptx for the
  atomic xmit path (no mutex in softirq context);
* hi5622_driver.h: the opaque ctx->dptx handle;
* hi5622_cfg80211.c/.h: data_tx state pointer, init/remove hooks, status
  line, data_tx_probe/data_tx_status attributes;
* hi5622_cfg80211_ops.c: ndo_start_xmit routes to the data path when
  data_tx=1, otherwise the exact previous accounted drop;
* Kbuild: two new objects.

No existing behaviour changes when data_tx=0: the xmit stub is
byte-for-byte the same logic.

## 4. Fail-closed policy (summary)

| situation | behaviour |
| --- | --- |
| data_tx=0 (default) | no pool allocation, old accounted drop, one dmesg line at init |
| unicast to unknown peer | refused + counted (data_tx_peer=1; =0 is the explicit bench diagnostic mode) |
| non-EAPOL to unauthorized peer | refused + counted (no pre-handshake data leak) |
| EAPOL to associated-but-unauthorized peer | allowed (the 4-way needs it), unprotected |
| authorized peer / group key | Protected bit per data_tx_protect |
| dp gate closed (no dp_setup / txq sync / phase) | frame refused + counted, no device write |
| table read-back mismatch | dp lane closed (failed=1), data lane refuses thereafter |
| completion timeout | lane stalled for the load, skbs returned, buffers not reused, one error line |
| probe/pool outside DMA window | lane disabled with a clear error at init |

## 5. Build identity and verification

Reproduce:

    ./tools/build-data-tx-module.sh     # base + sta-ops + this patch + build + identity
    ./tools/run-host-check.sh           # host selftest + patch/doc/ladder checks

Result (evidence/build-data-tx.txt):

    unstripped 1,282,348 B sha256 103323b8...
    stripped     335,752 B sha256 91aa1aa5...
    vermagic 6.18.60 SMP mod_unload ARMv7 p2v8 ; depends=cfg80211
    params 54 ; 0 warnings ; strip .text byte-identical

Reproducibility: building the same patch stack twice gives identical .text,
.modinfo and __versions; the only byte difference is the 20-byte
.note.gnu.build-id (fed from the absolute build path), which is expected for
the project's kernel module build and is recorded honestly rather than
claimed away.

A second build was also made from the patch applied to a pristine copy of the
sta-ops successor tree (git apply --check PASS, build PASS, .text identical).

## 6. Integration state and the remaining blocker

* The lane artifact is NOT part of any image yet.  The next image bake must
  stack wifi-sta-ops (task-149) and this patch: 54 params, base artifact
  sha 5b587949... asserted by the build gate.
* **The full 4-way handshake is still blocked at association** by the
  fail-closed .mgmt_tx / absent mgmt RX (not by this lane).  The ladder is
  ordered so that the data path is proven independently first:
  L1 control, L2 enabled-quiet, L3 sysfs EAPOL msg1-shaped probe through the
  real ring, L4 broadcast ARP through ndo_start_xmit via ping, then L5 full
  4-way as EXPECTED-BLOCKED today with its dependency and open unknowns
  listed.
* Coordination: sta-ops provides the station table and AUTHORIZED flag this
  path reads; the RX lane's dp_rx_drain cadence (one manual action per
  drain) is part of L5's msg2/msg4 collection; the WPA lane's pairwise
  0x0078 install is the encryption dependency this lane cannot validate
  host-side.

## 7. Limits / unknowns

* **MPDU size:** the proven dp path refuses MPDUs above 768 B
  (DP_TXRX_MAX_MPDU).  With 24+8 bytes of header/LLC that means a payload up
  to ~736 B - enough for EAPOL, ARP, ICMP, DNS, most DHCP/TLS handshake
  frames, but not a full-MTU bulk transfer.  Larger frames are dropped and
  counted (dropped_big); fragmentation/A-MSDU is a follow-up, not claimed.
* **Firmware encryption [U]:** the MAC/firmware must apply the per-user key
  for Protected-bit frames.  No RE evidence pins TX-side key selection; the
  ladder isolates it with data_tx_protect and the L5 unknowns.
* **msg3 semantics:** by hostapd's ordering msg3 is transmitted before the
  TX key exists; the lane's AUTO policy therefore leaves it unprotected.  If
  L5 shows the client rejects an unprotected msg3, the fallback is to force
  Protected with data_tx_protect=1 (once the firmware TX key behavior is
  understood) - recorded, not added speculatively.
* **Station table read is lockless** in xmit (atomic context); aligned reads
  of a table written only from process-context cfg80211 ops; the peer can
  only receive frames after being added.  Documented, not a correctness
  claim for concurrent station churn.
* **The "4/4-like" detector ambiguity** (msg1 also matches) is documented
  and intentionally not wired to authorization.
* **dp_tx_frame mixing:** the sysfs instrument and the data path share the
  ring; the spinlock keeps writes safe, but completion attribution would be
  ambiguous, so ladders never mix them.
* The exact lane artifact was NOT fired; the ladder is its first hardware
  use.

## 8. Safety / publication

* No vendor binary, object, firmware image, calibration value, per-unit
  MAC/serial or secret is contained in the patch, src copies, tests or docs.
  The vendor module and PD4 firmware stay local/gitignored; only derived
  offsets/constants appear, each with the T9 trail to the public lane doc.
* The two MACs used in tests are documentation examples
  (02:00:68:69:35:01/02); the probe frame body is synthetic (all zero), not
  key material.
* No device/UART/network access was performed while producing this lane.
  The ladder is design-only and must be fired by the Lead with the recovery
  door READY.
