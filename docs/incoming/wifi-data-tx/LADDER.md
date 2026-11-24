# LADDER - wifi-data-tx (task-150)

**DESIGN ONLY.  Not fired by this lane.  No bench contact was made.**
Machine-readable copy: [ladders/data-tx.json](ladders/data-tx.json).

The Lead owns the bench, the UART capture and the recovery door.  Every fire
below is attended, with the recovery door READY (tools/recovery-card.sh) and
the bench link PASS (tools/bench-link.sh).  UART stays read-only
(tools/uart-capture.sh to an absolute path); the harness never writes the
tty.

Module to load: the lane artifact built by tools/build-data-tx-module.sh
(stripped 335,752 B sha256 91aa1aa5..., 54 params) or the same bytes baked
into the next image.  The base is the fired all-patch module **plus the
wifi-sta-ops patch**; there is no image with this module yet.

## Preconditions (all rungs)

* fresh golden boot: page 0x86807FEF/7/2/0xABCD, mask 0, staging 0;
* PD4 firmware pushed by hand (/lib/firmware/hi_wifi/FIRMWARE.PD4.bin, never
  packaged); the local Wi-Fi block staged (never committed);
* one module load per boot; after an exception -> power-cycle, no retry;
* stop rules unchanged: any page/mask drift or nonzero staging frame ends the
  rung immediately.

## Common module parameters

    insmod /lib/modules/$(uname -r)/hi5622_wifi.ko \
      fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
      allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
      vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
      hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
      rx_enable=1 beacon_static=1 beacon_vap_start=1 \
      wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
      band_vendor=0 scan_band_cfg=0 \
      sta_ops=1 eapol_rx=1 sta_user_del=0 \
      <LANE PARAMS: see the rung>

LANE PARAMS are the four new parameters only:
data_tx / data_tx_peer / data_tx_protect / data_tx_timeout_ms.

Boot chain (per the fired recipe, unchanged): prepare/upload/verify/seed/
release, C2 (h2d_ctx_create_t1 + h2d_txq_sync72), iface, hostapd WPA2 AP.
The data-TX path additionally needs the dp chain in the SAME boot:
cat $D/dp_probe, echo 1 > $D/dp_setup, echo 1 > $D/h2d_txq_addr_sync,
with D=/sys/bus/pci/devices/0000:00:00.0 (the band-0 endpoint; if the box
enumerates the two endpoints differently, use the one whose dp_status
exists).

---

## L0 - host-only gate (no fire)

    ./tools/build-data-tx-module.sh     # patch stack + 6.18.60 ARM build
    ./tools/run-host-check.sh           # portable selftest + patch/doc/ladder checks

PASS: build 0 warnings, 54 params, strip .text byte-identical, artifact
identity recorded; selftest all checks pass; host check PASS.  This proves the
code, not the wire: the bench rungs are the hardware evidence.

## L1 - fail-closed control (B1: data_tx=0)

Purpose: the default must be byte-for-byte the previous behaviour (the
accounted drop), and the new code must not touch the device.

LANE PARAMS: data_tx=0 data_tx_peer=1 data_tx_protect=0
data_tx_timeout_ms=200

Steps: fresh boot; insmod; AP start + hostapd WPA2 AP; observer attempts a
WPA2 association (expected to stop at AUTH today - the .mgmt_tx wall, see
L5); capture dmesg + $D/cfg80211_status + $D/data_tx_status.

PASS:
* AP-ENABLED as in the fired one-boot validation; WPA2 GTK key_add /
  set_default_key unchanged; page golden, mask 0;
* data_tx_status prints "data-tx: data_tx=0 disabled (no pool;
  fail-closed default)";
* no data-tx dmesg line except that one; hostapd's xmit attempts stay
  accounted under the old counter (xmit_dropped).

FAIL-IF: any data-tx pool allocation line, any device write traceable to the
lane, page/mask drift.

## L2 - enabled, quiet (B2: data_tx=1, no traffic)

Purpose: enabling the lane must not change AP start or the dp chain; the
pool is allocated host-side only.

LANE PARAMS: data_tx=1 data_tx_peer=1 data_tx_protect=0
data_tx_timeout_ms=200

Steps: fresh boot; insmod; boot chain; AP start + hostapd WPA2 AP; run the dp
chain (dp_probe -> dp_setup -> h2d_txq_addr_sync); no client, no traffic;
capture dmesg + data_tx_status.

PASS:
* dmesg "data_tx: lane ENABLED (data_tx=1) - 16 in-flight frames x 1024 B
  coherent at devva 0x... peer_required=1 protect=0 timeout_ms=200";
* data_tx_status: stalled=0 failed=0 in_flight=0 free=16, all counters 0;
* AP-ENABLED, dp setup/TXQ sync evidence as before; page golden, mask 0.

FAIL-IF: pool allocation outside the DMA window (lane stays disabled with a
clear error - record and stop), AP-start regression, any unexpected frame.

## L3 - synthetic EAPOL-Key msg1 through the real builder/ring (B2 continue)

Purpose: the first host -> AP data frame ever posted through the proven TX
path by the driver itself, before an association is possible.  The sysfs
probe calls exactly the same builder + MSDU ring + TXQ publish as
ndo_start_xmit; only the peer decision is forced to the diagnostic mode
(the destination is a documentation STA MAC).

Steps (same boot as L2, after dp chain):

    echo 1 > $D/data_tx_probe
    sleep 1
    cat $D/data_tx_status
    dmesg | tail -20

PASS:
* data_tx_probe returns rc=0 and logs "synthetic EAPOL-Key msg1 shape
  (113 B 802.3 -> data MPDU) posted ...";
* data_tx_status within one poll: posts=1 completed=1 in_flight=0,
  eapol: tx=1 msg1=1 msg3=0 4_4_like=1 (the vendor
  hmac_is_eapol_key_ptk_4_4 shape matches msg1), unprotected=1;
* dmesg: "data_tx: EAPOL-Key msg1/4 TX COMPLETED (peer 02:00:68:69:35:02,
  ... B MPDU, protected=0, key-data-len-zero=1)";
* optional on-air witness: a monitor interface on the observer host sees a
  data frame from the AP BSSID to 02:00:68:69:35:02 with LLC/SNAP 0x888e;
* page golden, mask 0, AP alive, no exception.

FAIL-IF / record as POST-ONLY: posts=1 but completed stays 0 and the timeout
closes the lane (data_tx_timeout_ms) -> the MAC did not consume a host-built
unicast data frame; record (this is the decisive datum that a peer/firmware
user context or an unproven TX descriptor field is missing), do NOT retry in
this boot.  Exception/page drift -> power-cycle.

## L4 - ndo_start_xmit without association: broadcast ARP TX COMPLETED

Purpose: prove the real netdev xmit path (dev_queue_xmit -> ndo_start_xmit),
not only the sysfs probe, without needing an association.  A broadcast ARP
request needs no peer context and no encryption.

LANE PARAMS (fresh boot B3, same chain + dp chain as L2):
data_tx=1 data_tx_peer=1 data_tx_protect=2 data_tx_timeout_ms=200

The force-clear protect mode keeps the ARP frame unprotected so the MAC
encryption question of T6 cannot mask the TX result.

Steps (after AP-ENABLED, dp chain, iface __ap exists):

    ip addr add 192.0.2.1/24 dev __ap        # documentation range, local only
    ping -I __ap -c 3 -W 1 192.0.2.10       # triggers broadcast ARP requests
    cat $D/data_tx_status
    dmesg | tail -20

PASS:
* data_tx_status: posts >= 1, completed >= 1, unprotected >= 1,
  eapol counters 0; dropped_peer may increment (the ICMP echo itself is a
  unicast to an unknown peer and is refused - that is the peer-aware rule
  working);
* dmesg: no timeout, lane not stalled;
* optional on-air witness: monitor capture of an unprotected ARP request
  (broadcast DA, AP BSSID as addr2/addr3, LLC/SNAP 0x0806);
* page golden, mask 0, AP alive.

FAIL-IF: posts>0/completed=0 -> POST-ONLY (record; the MAC may refuse
broadcast host frames too - compare with L3); exception -> power-cycle.

## L5 - the real 4-way handshake (BLOCKED TODAY on the mgmt-tx lane)

Purpose: the lane's reason to exist - hostapd's EAPOL-Key msg1/msg3 go
through ndo_start_xmit and the WPA2 4-way completes.

**Dependency (stated, not hidden):** a client cannot associate while the
driver's .mgmt_tx is fail-closed - the AP SME (hostapd) must send the AUTH
and ASSOC responses through the WAL mgmt-tx family (wifi-ap-mgmt decoded the
family; the op is still fail-closed, and there is no management RX).  The
2026-11-29 one-boot validation and the wifi-onair-observer lane both stop at
"AUTH: no answer".  Until a mgmt-tx follow-up lands, L5 can only be
attempted; its expected result today is EXPECTED-BLOCKED at association.

When the mgmt-tx path is available, in one boot (B4) with
data_tx=1 data_tx_peer=1 data_tx_protect=0 data_tx_timeout_ms=200,
sta_ops=1 eapol_rx=1 rx_enable=1:

1. start the AP (L2 chain + dp chain); confirm AP-ENABLED, GTK installed;
2. from the observer host, WPA2-PSK association with the published fixture
   (ssid cudy-ap-test; the throwaway test passphrase is in the fixture);
3. hostapd calls .add_station -> dmesg "sta_ops: STA ... added ... user_add +
   asoc_syn ... committed" (STA-ADDED instead of "Could not add STA");
4. hostapd sends EAPOL-Key msg1/4 -> our xmit -> MSDU ring -> completion:
   data_tx_status eapol msg1=1, dmesg TX COMPLETED for msg1/4;
5. the client's msg2/4 must be drained on the RX side: repeatedly
   echo 1 > $D/dp_rx_drain (rx_enable=1, eapol_rx=1) and watch
   cfg80211_status sta-ops rx frames/eapol/delivered; expected: msg2 reaches
   hostapd, hostapd installs the pairwise PTK (RX-only, cfg80211 add_key
   0x0078) and sends msg3/4;
6. msg3/4 -> xmit -> completion: data_tx_status eapol msg3=1, dmesg
   TX COMPLETED for msg3/4;
7. msg4/4 arrives protected; the firmware must decrypt with the per-user
   pairwise key (unknown, T6) -> hostapd PTKINITDONE installs RX_TX,
   .change_station AUTHORIZED; cfg80211_status stations=1 authorized=1;
8. data: ping through the AP VAP with an authorized station -> protected
   data TX completions; on-air observer sees the WPA2 handshake (4 EAPOL
   frames) then protected data.

PASS: all four EAPOL frames seen, hostapd logs "pairwise key handshake
completed", data_tx posts/completed counts match, page golden/mask 0.

OPEN UNKNOWNS the rung resolves (all [U] today):
* does the firmware encrypt a Protected-bit host-posted frame with the
  pairwise/GTK keys (T6)?  If not, msg3 (unprotected by hostapd ordering) is
  still TX-completest but msg4 arrives encrypted-by-STA and the AP's RX key
  path (wpa-static 0x0078 RX) must decrypt it - if msg4 never reaches
  hostapd, the pairwise RX key install on the firmware is the next RE target;
* do host-posted unicast data frames need an explicit firmware user context
  beyond .add_station (L3 POST-ONLY would be the signal);
* does the MAC accept the frame subtype 0 shape (T1).

## L6 - WPA3-SAE (future)

Same mgmt-tx dependency plus the SAE auth frame TX/RX path (wifi-wpa3-sae /
hostapd-sae).  Not part of this lane; listed so the ladder is not mistaken
for "WPA2 only is the whole goal".

---

## Abort policy

* any firmware exception, page word different from 0x86807FEF/7/2/0xABCD, or
  nonzero staging frame -> STOP the rung, power-cycle, capture dmesg + UART
  (read-only), report; no retry in the same boot;
* data_tx_status stalled=1 -> the lane closed itself (bounded timeout); the
  boot may continue for read-only evidence but no further TX rung is allowed;
* a POST-ONLY result (posts but no completion) is recorded honestly, never
  retried in the same boot;
* the recovery door and a human at the bench are mandatory for every fire.

## Teardown

    tools/bench-teardown.sh --apply

Leave TFTP only if a recovery is genuinely armed.  No rmmod after a released
boot (the firmware keeps running with the host rings).
