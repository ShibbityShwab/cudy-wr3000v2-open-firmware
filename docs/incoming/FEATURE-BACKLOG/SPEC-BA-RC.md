# BACKLOG SPEC - aggregation + rate control (BA-1..3, RC-1..3)

## BA-1 - A-MPDU aggregation - PARTIAL (lane rx-ba-agg, task-178)
- The lane decoded: the hmac_ba_* machinery (hmac_ba_rx_init @0xdb580), the ADDBA/DELBA
  action-frame builders (hmac_config_addba_req, hmac_mgmt_rx_addba_rsp/delba), the host<->fw BA
  message contract, the session/window layouts, the minimal A-MPDU session-setup sequence
  (both directions).
- GAP (verification): the committed selftest is kernel-header-dependent (linux/types.h +
  linux/moduleparam.h) - the "183 checks" need a userspace-compilable portable model committed
  (the other lanes' pattern) or a kernel M= build script.
- PASS: the session-setup sequence implemented + a fire where an aggregated TX/RX is observed
  (the BA session established; the completion/A-MPDU evidence).
- Deps: the RX path (aggregation RX needs the capture) - the TX side can be tested earlier.

## BA-2 - A-MSDU
- Source: the vendor's A-MSDU path (the MSDU aggregation in the TX builder + the firmware's
  A-MSDU handling; the user_add's mib_amsdu_aggregate_active key 0xD8).
- PASS: an A-MSDU TX observed (the frame's QoS/A-MSDU present bit).
- State: NOT STARTED.

## BA-3 - the BA reorder window
- Source: the firmware's reorder (the BA window messages + the scoreboard).
- PASS: out-of-order frames reordered before the host.
- Deps: RX.

## RC-1 - the rate control / alg engine - IN PROGRESS (lane rx-rate-ctl, task-179)
- The source: the vendor's alg tables (.data g_ast_hmac_config_alg_*_output_table etc.), the
  hmac_config_alg path, the alg cfg messages, the per-rate tx power tables.
- PASS: the vendor's rate adaptation enabled (vs the default) + the rate selection observable
  (the per-rate stats/TX rate).
- Deps: none host-side; the fire needs the TX path (proven).

## RC-2 - the per-rate TX power / regulatory limits
- Source: the power table + the channel limits (the regdb + the vendor's power config).
- PASS: the per-channel power limits applied (the TX power matches the country rules).
- State: NOT STARTED.

## RC-3 - the calibration data usage
- Source: the calibration lanes' findings (the cal data from the vendor's flash + the firmware's
  cal consumption).
- PASS: the calibrated power/ IQ applied (the vendor's cal results used).
- State: PARTIAL (the calibration analysis lanes exist).

### BA-1 update (the lane's final commit 792d2af + the closing report)
- Decoded byte-exact: the firmware owns the per-TID BA contexts; the host does the 802.11
  negotiation + a session mirror. Three host->fw messages: **g0 0x0A** (44 B BA relay; subtype
  1 = ADDBA rsp relay, subtype 2 = DELBA), **g0 0x0B** (20 B A-MPDU start/end), **g0 0x1C**
  (32 B TX BA notify). OTA frames: ADDBA req/rsp 33 B, DELBA 30 B. D2H BA events: 2 create,
  3 delete, 0x0F peer-DELBA, 0x1D unified action.
- **Two findings**: (a) the vendor's userspace (the custom hostapd) does BA - stock hostapd
  does not -> our driver must negotiate in-kernel (the minimal patch); (b) the TX direction
  is the minimal BA-1 cut; the AP-as-RX-recipient path uses a user-sync writer whose wire
  encoding is [U].
- Delivered: the fail-closed skeleton (58 params, ba_enable=0, provably unwired), the
  **183-check portable selftest** (PASS - the earlier PARTIAL is resolved), the 147 static
  anchors, the B0-B7 recipe (B1/B2 evaluable now; B3-B6 blocked on the RX gate).

