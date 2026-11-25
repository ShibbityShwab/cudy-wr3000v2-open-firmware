# BACKLOG SPEC - power management, roaming, services (PM-1..2, RO-1..3, SV-1..4)

## PM-1 - the AP-side power save (TIM/DTIM, PS-Poll/U-APSD)
- Source: the beacon's TIM/DTIM (the hostapd config + the vendor's beacon template), the
  PS-Poll/U-APSD handlers, the firmware's buffering.
- PASS: a PS client buffers/receives via the TIM (the AP holds the frames).
- Deps: RX (the client association).

## PM-2 - the chip power gating / the per-VAP power
- Source: the vendor's power messages (the power-perf lane: the sleep machine is dead on this
  board; the per-VAP chip power gating + the cfg80211 STA PS sync cmds 0x1ee/0xe8, the hmac
  event 0x3e3, the PCIe-PM event 0x283).
- PASS: the per-VAP power gating active + the idle current measured.
- State: ANALYSED (docs/incoming/wifi-power-perf/).

## RO-1..RO-3 - 802.11k/v/r
- Source: the vendor's RRM (the neighbor report), the BTM (the BSS transition), the FT (the
  fast handover keys).
- PASS each: the respective frame exchange with a client.
- Deps: RX.

## SV-1 - WPS
- Source: the vendor's WPS (the EAP-WSC path + the hostapd WPS).
- PASS: a WPS enrollee gets the credentials.
- Deps: RX.

## SV-2 - Mesh (802.11s)
- Source: the vendor's mesh (the peer link, the HWMP - likely not supported by the vendor's
  firmware; check the vendor strings).
- PASS: a mesh peer link.
- State: NOT STARTED (may be unsupported).

## SV-3 - Monitor mode / spectral
- Source: the driver's monitor iftype (advertised) + the spectral scan (the vendor's
  hmac_spectral_init + the CSI/spectral reports).
- PASS: a monitor capture of a real frame; or the spectral data.
- State: PARTIAL (monitor advertised; not exercised).

## SV-4 - per-STA statistics
- Source: the vendor's stats (the RSSI/rate/retry counters; the user_add's RSSI +100 @0xa4).
- PASS: the stats shown (the client's per-STA counters).
- Deps: RX.
