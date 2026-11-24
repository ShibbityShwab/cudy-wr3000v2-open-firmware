# LADDER-G-TXQ - device TX-consumer enablement (attended only)

**Lane:** wifi-dp-tuning (shared task task-134). **NOT FIRED** by the lane.
Every stage is attended; the standard preconditions are unchanged: recovery
door armed/READY, bench link PASS (tools/bench-link.sh), read-only UART
capture to an absolute path, image/module hashes recorded, no vendor Wi-Fi
module loaded, one clean driver, never rmmod after the 0x5A5A release.

Image baseline: the self-contained ABI image (container ae5e28bf, kernel slice
13e81214/slot 1794a531) with the PD4 firmware artifact pushed per policy.
Module: the tuned hi5622_wifi.ko **251,924 B sha256 175a4617...** (build with
tools/build.sh; compare with sha256sum on the box before insmod). Inherited
combined module was 242,572 B sha256 44c6cc12.

One-command form: ladder/bringup-g-txq-consumer.json (33 steps / 366 lines,
sha256 ac37b13d...) through the bench tooling once the new attributes are in
bench_guards.DP_ATTRS. Manual form below.

## Exact attribute sequence

| # | attribute | mode | command | expected |
| --- | --- | --- | --- | --- |
| 1 | module load | - | insmod cfg80211.ko; insmod hi5622_wifi.ko with the G-TXQ params | no device write at load |
| 2 | boot ladder | WO | prepare/upload/verify/seed/hcc_arm/hcc_loop/release | golden page 0x86807FEF/7/2/0xABCD, state 1, mask 0 |
| 3 | h2d_ctx_create_t1 | WO | echo 1 > | ctx_created=1 |
| 4 | h2d_txq_sync72 | WO | echo 1 > | txq_sent=1 ok=2 timeout=0 (0xd8 data[0]=1: consumer context enable) |
| 5 | iface + scan | shell | iw phy phy0 interface add wlan0 type managed; ip link set wlan0 up; iw dev wlan0 scan | firmware VAP + channel; scan phase=finished results>=1 (results=0 = BLOCKED-DRIVER, no air-TX precondition) |
| 6 | dp_probe | READ | cat dp_probe | one read-only MAC word, probe_ok=1 |
| 7 | dp_setup | WO | echo 1 > | setup_done=1 failed=0 mac_regs=14 table_entries=8 |
| 8 | h2d_txq_addr_sync | WO | echo 1 > | txq_addr_sent=1 ok=3 timeout=0 |
| G1 | dp_mac_ext | RO | cat dp_mac_ext | role-5 tail read-only; record +0x5c/+0x6c (vendor live dump 4/4); do NOT write them here |
| G2 | dp_txq_table | RO | cat dp_txq_table | publications=0 user0_flags=0x00 (table untouched before the first TX) |
| G3 | dp_tx_frame | WO | echo 1 > | posted=1; dmesg "DP txq: published user=0 tid=0 count=1 bytes=.. sn=.. flags=0x01" |
| G4 | dp_tx_status | RO | cat | posted=1 completion_pending=<n> TXQ publish: enabled=1 publications=1 |
| G5 | dp_txq_table | RO | cat | user0_flags=0x01; tid 0: table{count=1 sn=.. bytes=..} |
| G6 | dp_tx_complete_poll | WO | echo 1 > | ONE bounded poll (tx_complete_ms); dmesg verdict; then cat dp_tx_status shows "tx_complete_poll: verdict=COMPLETED|POST-ONLY" |
| G7 | dp_rx_poll / dp_rx_capture | WO/RO | echo 1 >; cat | polled=1 captures=<n>; 0 = NO-RX recorded, not a failure |
| G8 | post packs | RO | bench read pack | mask 0, staging 0, page golden, state 1, out0/out2 0 |

## Verdicts and decision matrix

| observation | meaning | next |
| --- | --- | --- |
| G6 verdict COMPLETED (MAC): device rptr == published wptr within tx_complete_ms | the TXQ-table queue-state publish is (part of) the consumer notification; the host publishes ring wptr + queue state exactly like the vendor enqueue tail | record; air claim still needs an external observer (broadcast mgmt frame has no ACK) -> air-CANDIDATE |
| G6 verdict POST-ONLY: rptr did not advance in the window | hypothesis falsified (or the window too short: tx_complete_ms is up to 45000; record the value used) | do NOT retry in this boot. Next boot: optional mac_tail_writes=1 + dp_mac_tail_init after dp_setup, then the same TX; if still POST-ONLY, the remaining candidates are the firmware smac trx-enable family [U] and deeper PIE/EQM setup [U] |
| dp_txq_table before G3 not zero | the driver already published something in this load (unexpected; only a TX post publishes) | STOP, record; do not attribute to the tuning |
| dp_txq_table after G3 flags != 0x01 / count != 1 | the publish path did not run as derived | STOP the data-plane rung; host path bug, no further writes |
| MSDU table read-back mismatch at G3 | the publish did not land | STOP; lane closed for this load, power-cycle |
| mask/staging nonzero or page/state drift at any point | firmware exception / handshake disturbed | STOP the boot, power-cycle, capture UART |
| G7 captures >= 1 | first RX evidence; record the raw completion word + descriptor hex (format [U]) | keep read-only in this boot; decode in a follow-up |
| G7 captures = 0 | NO-RX | record; not a failure; RX completion format remains [U] |

## Stop rules

One boot = one ctx-create + one txq_sync72 + one scan + one dp_setup + one
0x0320 + at most 8 TX posts + one completion poll + one RX poll; never retry a
failed step; never reload/reset inside the same attended session; never touch
band 1, the mailbox/ETE plane, CA 0x400392F0 or the RC misc window; the
recovery door is armed for every session that writes the MAC. The
mac_tail_writes=1 experiment (write 4 to MAC band0 +0x5c/+0x6c, read-back
verified) is a separate boot and remains [U]: it is not part of the default
ladder and not required for the host contract.

## What a PASS/FAIL means for the project

* COMPLETED (MAC) upgrades the air-TX milestone from POST-ONLY to
  COMPLETED (MAC); the on-air beacon/data proof still needs an external
  observer (second station/monitor) - the existing honesty rule (C41/F14).
* POST-ONLY keeps the documented state unchanged and narrows the [U] to the
  firmware consumer + trx-enable direction; the tuning is then a negative
  result with a positive host-side side effect (the driver now publishes the
  vendor queue state regardless).
