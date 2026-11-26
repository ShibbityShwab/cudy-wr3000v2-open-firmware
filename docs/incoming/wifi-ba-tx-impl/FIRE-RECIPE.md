# FIRE-RECIPE - wifi-ba-tx-impl (task-217)

**The Lead fires. This lane never contacted the router.** HOST-ONLY lane.
Every rung below is an attended bench action; the recovery door must be armed
first (RUNBOOK + `tools/recovery-card.sh`), and the 5 GHz lessons stand:
**one module load per power cycle**, **the page gate is checked AFTER the
boot** (the pre-boot page is not golden - a false gate blocked a working fire
on 2026-11-26), the box is physically power-cycled before each fire.

## Artifact to deploy (host-side)

    /tmp/batx-artifact/hi5622_wifi.ko
    406,168 B  sha256 234b3d4b961037e1a5465cc4cc38ab4603b2c71c451d06ac5ba7212b0104d841
    66 params (ba_enable included)   vermagic 6.18.60 SMP mod_unload ARMv7 p2v8

    sha256sum /tmp/batx-artifact/hi5622_wifi.ko     # must match before the copy
    tools/rscp.sh /tmp/batx-artifact/hi5622_wifi.ko root@192.168.10.1:/root/hi5622_batx.ko

The full-chain build that produces it: `tools/build-ba-tx.sh` (rebuild and
re-check the sha if anything drifted).

## Rung B1 - the `ba_enable=0` no-regression (ALREADY PASSED, do not rerun)

The committed BA skeleton `78460841` with `ba_enable=0` was fired on
2026-11-27 06:00: insmod=0, the ladder all rc=0, the 2.4 GHz AP enabled, page
golden:1, **no `ba:` dmesg lines** (ledger entry "BA B1 PASSED"). This lane's
module keeps the same gate semantics (TABLES T6), so B1's evidence carries
over; B2 below is the stronger form of the same test on the wired core.

## Rung B2 - `ba_enable=1`, 2.4 GHz AP, NO PEER (**evaluable now**)

Purpose: prove the **fail-closed no-peer** behaviour of the wired core: armed,
inert - zero BA frames/messages, no regression.

Preconditions: fresh physical power cycle; TFTP/recovery armed; the 2.4 GHz
profile that the bench already proved (`/tmp/fire-24g.sh` pattern).

    # on the router (attended)
    D=/sys/bus/pci/devices/0000:00:00.0
    L=/tmp/fire-ba-b2.log; : > "$L"
    modprobe cfg80211 2>/dev/null; iw reg set US 2>/dev/null
    insmod /root/hi5622_batx.ko \
      fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
      allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
      vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
      rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 \
      wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 \
      scan_band_cfg=0 band5g=0 vap5g=0 \
      ba_enable=1 >> "$L" 2>&1
    echo "insmod=$?" >> "$L"; sleep 2
    for a in prepare upload verify seed hcc_arm hcc_loop; do
      echo 1 > $D/$a 2>/dev/null; printf "%s=%s " $a $? >> "$L"; sleep 1
    done
    echo 1 > $D/release 2>/dev/null; echo "release=$?" >> "$L"; sleep 2
    i=0; while [ $i -lt 20 ]; do
      cat $D/hcc_ready 2>/dev/null | grep -q id1_seen=1 && break
      sleep 1; i=$((i+1)); done
    echo "ready=$i" >> "$L"
    cat $D/boot | head -1 >> "$L"
    cat $D/boot | grep -q golden:1 || { echo "STOP: not golden:1 after boot" >> "$L"; exit 9; }
    printf 1 > $D/h2d_ctx_create; sleep 1; printf 1 > $D/h2d_txq_sync72; sleep 1

    # the proven 2.4 GHz AP
    PHY=$(ls /sys/class/ieee80211/ | head -1)
    iw phy $PHY interface add wlan0 type __ap >> "$L" 2>&1
    ip link set wlan0 up >> "$L" 2>&1
    hostapd -B -i wlan0 /etc/hi5622/hostapd-open.proven.conf >> "$L" 2>&1; echo "ap=$?" >> "$L"
    sleep 6
    iw dev wlan0 info 2>&1 | grep -E "ssid|type" >> "$L"

    # the BA evidence
    echo "-- the armed line (exactly one):" >> "$L"
    dmesg | grep -c "ba: TX-direction block-ack ARMED" >> "$L"
    echo "-- any BA traffic? (must be NONE):" >> "$L"
    dmesg | grep -E "frame post 'ba_|ba: (relay|ampdu|notify|addba|delba)|ba_mgmt_tx" >> "$L" || echo "NONE" >> "$L"
    echo "-- the status line:" >> "$L"
    cat $D/cfg80211_status | grep -E "^ba:" >> "$L"
    echo "-- the page:" >> "$L"
    cat $D/boot | head -1 >> "$L"
    cat "$L"

**PASS (all must hold):**

1. `insmod=0`, the ladder all rc=0, `ready` < 20, the boot page
   `golden:1` (checked after the boot), no exception/OAM line;
2. exactly **one** dmesg line `ba: TX-direction block-ack ARMED (ba_enable=1)`;
3. **zero** BA traffic lines: no `frame post 'ba_relay|ba_ampdu|ba_notify|ba_mgmt_tx'`,
   no `ba: addba request tx` / `ba: relay` / `ba: ampdu` lines - with no peer
   the negotiation cannot start (fail-closed);
4. `<D>/cfg80211_status` has the `ba:` line with `ba_enable=1 (tx-addba)`,
   `peers=0 active=0 pending=0`, all counters 0, and `ba_glue: ap_up=1
   band=0 channel=<the hostapd config's channel> posts=0 ...`;
5. the 2.4 GHz AP is up (`iw dev wlan0 info` shows the SSID/type) and the page
   is still `golden:1` at the end (no crash);
6. `allow_frames` was not consumed by BA (`posts=0`).

**FAIL / ABORT rules:**

* the page is not `golden:1` after the boot or changes later -> **STOP**, power
  cycle, do not probe further (a crash leaves the AP latched DIRTY);
* any BA H2D post or action frame appears with no peer -> **FAIL** (the gate
  or the trigger is wrong; save the log + `cfg80211_status`, power cycle);
* `insmod` != 0, a ladder step != 0, or the module warns `ba_enable=... is
  invalid` while the line asked for 1 -> **FAIL** (wrong artifact);
* a hang/no telnet -> recovery door (power-off + RESET + TFTP).

After B2: **power cycle** before any other module load (one-load rule).

## Rung B2b - the invalid-value negative control (optional, separate boot)

    # fresh boot; load the same line with ba_enable=2
    dmesg | grep "ba_enable=2 is invalid"        # exactly one warning
    # then the same AP; the BA path cannot be triggered with no peer, so the
    # observable is the warning + the status line "ba_enable=2 (invalid)".

PASS: one warning, status `(invalid)`, no BA traffic, page golden. This rung
adds little over B2; it is only for the fail-closed matrix record.

## Rung B3+ - the real BA session (**BLOCKED on the RX gate**)

**Dependency, stated honestly: a client cannot associate today.** The RX path
is gated by the firmware-internal RX thread (the F1 is not a missing H2D
frame; `../wifi-f1-from-capture`, `../wifi-rx-h2d-trace`), and the peer's
ADDBA response would arrive over the same mgmt-RX (group1 id13) / D2H relay
path. Until the RX gate opens (or the EDCA auth test in
`../wifi-f1-from-capture` produces a delivered frame), the negotiation cannot
complete end-to-end and **no BA session rung is evaluable**.

When a peer can associate (with `ba_enable=1`, `data_tx=1`, and the same
2.4 GHz AP):

* **B3 (AP as TX originator)**: send traffic to the associated client (or
  observe the fw D2H 2 / 0x1D action 0). Expected: a `ba_mgmt_tx` post with a
  33-B ADDBA request (dmesg `frame post 'ba_mgmt_tx' ...`), the peer's ADDBA
  response -> `ba_relay` post (44 B, g0 0x0A subtype 1) + `ba_notify` post
  (32 B, g0 0x1C value 0), status counters `req=1 rsp=1 relay=1/0 notify=1
  active=1`, page golden:1.
* **B4 (A-MPDU boundaries)**: sustained data -> `ba_ampdu` posts (20 B,
  g0 0x0B start/end) with the counters `ampdu=start/end` incrementing, page
  golden.
* **B5 (teardown)**: client leaves / DELBA -> `ba_relay` subtype 2 (44 B) +
  `ba_notify` value 2, session cleared in the status line.

PASS for B3-B5 = the dmesg/`frame post` lines above + the status counters +
the page golden + (if a sniffer is available) the action frame on air. FAIL /
abort rules as B2, plus: a BA post that is refused by the post gate
(`frame post 'ba_...': REFUSED`) means the budget/phase/boot gate was not in
the expected state - stop, save the log, do not retry.

## What each rung proves (and does not)

| rung | proves | does not prove |
| --- | --- | --- |
| B1 (passed) | the compiled-in core is inert at `ba_enable=0`, no regression | that the wired hooks are correct |
| **B2** | the wired core is armed and **inert with no peer** (no frame/H2D), AP/page healthy | the negotiation, the relays, the TX boundaries |
| B2b | the invalid value is refused | anything about the wire |
| B3-B5 | the TX-direction negotiation + relays + boundaries | the RX scoreboard (parked) / the RX direction sync |

**Do not claim an A-MPDU is on air from B2.** B2 is a fail-closed /
no-regression rung only; the BA session evidence needs B3.
