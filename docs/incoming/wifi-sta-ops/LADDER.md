# wifi-sta-ops - attended bench ladder (DESIGN ONLY, not fired)

Machine-readable: [ladders/sta-assoc-eapol.json](ladders/sta-assoc-eapol.json).
**This lane had no bench contact**: no device, UART or network access happened
while producing it.  The rungs below are for an attended session with the
recovery door READY and the bench link PASS - the project physical rules
(AGENTS.md, BENCH-FACTS.md) override anything written here.  Recovery is
physical: a human must be at the bench for every fire.

## Why this ladder exists

The ap-mgmt lane left association at "hostapd: Could not add STA to kernel
driver" because the driver has no station ops and the firmware has no
per-STA context; EAPOL frames (data frames) also need an RX delivery path.
This lane decoded and implemented both behind sta_ops=0 / eapol_rx=0
(fail-closed) and built a host-tested patch (+3 parameters) on the FINAL
ALL-PATCH tree.  The ladder turns that into hardware facts rung by rung and
stops honestly at the remaining blocker: **host -> AP data TX** (hostapd's
EAPOL-Key msg1 cannot leave the host yet; the netdev xmit still drops and is
counted in cfg80211_status as xmit_dropped).

## Module incantation (recorded before the fire; one load per boot)

    # common (the FINAL ALL-PATCH module recipe) + the lane params
    insmod /lib/modules/6.18.60/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 \
        sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 \
        sta_ops=<0|1> eapol_rx=<0|1> sta_user_del=<0|1>

* L1 fail-closed control: sta_ops=0 eapol_rx=0 sta_user_del=0.
* L2+ enabled: sta_ops=1 eapol_rx=1 sta_user_del=0.
* L7 experiment (separate boot): sta_user_del=1.
* Budget: the AP start uses ~5 posts, the VAP user_add+notify 2, the group
  key 2, each station add 2 - allow_frames=32 is enough for one client and
  one re-add; never raise the budget mid-boot.

## Rungs

| rung | state | client | what must happen | what must not happen |
| --- | --- | --- | --- | --- |
| L0 | host-only | - | build gate 0 warnings / 50 params; selftest 112/112; RE tool matches | any claim not backed by the artifacts |
| L1 | sta_ops=0 | associates | AP-ENABLED as before; add_station fails-closed log; hostapd "Could not add STA to kernel driver"; status sta_ops=0 | any g0 id7/id9/0x07d2 announce; page/mask change |
| L2 | sta_ops=1 | none | AP-ENABLED unchanged; status sta_ops=1 stations=0 user_add=0 | early station frames; budget use |
| L3 | sta_ops=1 eapol_rx=1 | associates | sta_user_add (180 B g0 id7) + sta_asoc_syn (32 B g0 id1 cmd 0x7d2) committed; hostapd reports the STA added; status stations=1 | post refusal, exception, page/mask change |
| L4 | same | 4-way attempt | xmit_dropped increments (EAPOL TX gap); rx stays 0 | pretending the handshake works |
| L5 | + future TX lane | 4-way | EAPOL msg2/msg4-like RX delivered (eapol >= 2, delivered >= 2); pairwise key_add (MAC=STA); CHANGE_STATION authorized=1; post-auth data flows | faking keys/authorization |
| L6 | same | leaves + re-adds | sta_kick (WAL 0x00d0, 48 B) committed; re-add posts the context again and succeeds | stale user blocking the re-add; exception |
| L7 | sta_user_del=1 (separate boot) | repeat L3+L6 | sta_user_del (24 B g0 id9) committed; no exception | double-delete exception (E2 crash class) |
| L8 | after L5 | associated | 30-60 min soak: no exception, page golden, counters consistent | new noise class / RX lane close |

## Expected dmesg lines (L3)

    sta_ops: .add_station 02:11:22:33:44:55 AID 1: ... (only on refusal)
    frame post 'sta_user_add': g0 id7 total 180 B ... posted ...
    frame post 'sta_asoc_syn': g0 id1 total 32 B ... posted ...
    sta_ops: STA 02:11:22:33:44:55 added (AID 1, vap 0, band 0): firmware context committed (g0 id7 user_add 180 B + g0 id1 cmd 0x7d2 asoc state 1); host station table=1
    sta_ops: CHANGE_STATION 02:11:22:33:44:55: authorized=1 ...   (only after L5)

## L6 expected lines

    sta_ops: DEL_STATION 02:11:22:33:44:55 (AID 1, subtype 12, reason 3): kick committed; host table=0
    frame post 'sta_kick': WAL_G2_CFG total 48 B ... posted ...

## Abort policy

* ANY firmware exception, page/mask deviation (0x86807FEF/7/2/0xABCD,
  mask 0), H2D lane close, or a repeating post refusal: power-cycle, stop
  the ladder, record the rung and the dmesg tail.
* One module load per boot; never reload after the ready handshake.
* sta_user_del=1 never runs on the same boot as a failed delete.
* The post path never retries; allow_frames exhaustion is a stop, not a
  budget change mid-boot.  The RX lane closes for the load on any anomaly -
  do not retry dp_rx_drain.
* Recovery is physical (power-off + RESET + TFTP): a human must be present.

## Not claimed

* The exact all-patch + sta-ops artifact was not fired anywhere yet.
* The 4-way handshake / PTK (L5) needs the host data TX lane; L4's expected
  stop is a deliverable, not a failure.
* .change_station sends no firmware message (documented [U]); L5 decides
  whether the firmware needs one.
