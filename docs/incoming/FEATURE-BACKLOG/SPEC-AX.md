# BACKLOG SPEC - 802.11ax (Wi-Fi 6) (AX-1..AX-7)

The vendor's per-user HE block: the user_add g0 id7/id8 (180 B): id7 = uid+0x0, MAC+0x2,
type 2 @0x8e, RSSI +100 @0xa4; id8 = uid+0, alg+8, VHT 24 B @0x10, HT 44 B @0x28,
**HE 58 B @0x54**, NSS @0xa5 (decoded by the rx-5g-vap lane).

## AX-1 - the HE capability advertisement - PARTIAL (lane ax-he-caps, task-177)
- The lane delivered: the vendor HE caps decode + the wiphy HE (iftype_data) + the hostapd gate
  + the user_add HE block + the patch contract + the patch (0001-he-caps.patch; tests 109/109).
- Remaining PASS: build the module with the patch, fire, and see the HE IE in the beacon
  (the observer's scan) + an ax client negotiates HE.
- Test: the fire recipe in docs/incoming/wifi-ax-he-caps/REPORT.md.

## AX-2 - the HE user info (the add_user HE block)
- Source: the HE 58 B @0x54 in id8 (the lane decoded the block layout).
- PASS: the HE fields set from the client's HE caps (the association's HE negotiation).
- Test: the association fire with an ax client (blocked by RX).
- Deps: RX (the association).

## AX-3 - DL/UL OFDMA
- Source: the vendor's OFDMA machinery: the MU EDCA, the trigger frames, the RU allocation
  (the firmware strings/orders in docs/incoming/wifi-firmware-re; the hmac_config_* MU senders).
- PASS: a DL OFDMA transmission (an ax client receives its RU) or the trigger-based UL.
- State: NOT STARTED.

## AX-4 - MU-MIMO
- Source: the sounding (NDPA/NDP) + the MU group config + the beamforming reports.
- PASS: a two-client MU-MIMO exchange.
- State: NOT STARTED.

## AX-5 - TWT
- Source: the TWT agreement messages (the vendor's TWT handlers/orders).
- PASS: a client's TWT agreement established.
- State: NOT STARTED.

## AX-6 - BSS coloring
- Source: the HE BSS color in the beacon + the OBSS PD config.
- PASS: the beacon's HE IE carries the color; two colors coexist.
- State: NOT STARTED (depends on AX-1).

## AX-7 - HE sounding/beamforming
- Source: the NDPA/NDP + the sounding report path.
- PASS: a sounding exchange.
- State: NOT STARTED (depends on AX-4).
