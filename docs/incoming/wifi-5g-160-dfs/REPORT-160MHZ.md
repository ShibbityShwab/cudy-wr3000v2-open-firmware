# REPORT-160MHZ - the vendor 5 GHz 160 MHz machinery

**Lane:** wifi-5g-160-dfs (task-205), 2026-11-28. **HOST-ONLY** static decode;
no device contact, no fire. **The Lead fires.** Grade markers as in TABLES.md.

**DEPENDENCY: this feature needs the 5 GHz band-1 MAC start first.** The 5 GHz
RF is already programmed (F2 fire: mode 3 band 1, ch36-64 RF image) but the
band-1 MAC/TBTT arm was never posted by our ladder, so no 5 GHz beacon is on
air yet. The missing rung is H2D g0 id1 key 0x0242 (worker CA 0x4C524; body
{01,00,01,00,00}; field B = a fresh band-1 module context from key 0x0064 with
data[0x12]=1) - see ../wifi-band1-start/README.md (task-197) and the
wifi-band1-start-send lane (task-206). Until the on-air gate passes (an
observer scan on the 5 GHz channel finds the SSID), every 160 MHz rung below
is GATED: it would test nothing.

## 1. Result

1. **Capability [V]:** the vendor's 5 GHz band advertises VHT cap
   **0x00400064** = HTC_VHT (BIT22) | SUPP_CHAN_WIDTH_160MHZ (BIT2) | SGI80
   (BIT5) | SGI160 (BIT6), with the 2-SS VHT MCS map 0xfffa (MCS0-9 both
   streams). Hostapd defines the 160 bit as BIT(2)
   (hostapd src/common/ieee802_11_defs.h:1430, local hostapd 2.12 tree), so
   this word really is 160-capable (not an 80-only word).
   The project driver already carries it: hi5622_band5g_vht_cap_word(1) =
   0x00400064 in the image #3 module tree (hi5622_band5g.h; the wifi-5ghz
   lane's band5g profile). The bench fired a live vendor 160 MHz AP on ch44
   (../wifi-re-report/TABLES.md: "matches live ch44 @160 MHz"), which is the
   same capability word - independent confirmation.
2. **cfg80211 eligibility [V]:** net/wireless/chan.c (6.18) accepts a
   NL80211_CHAN_WIDTH_160 chandef when the channel has no
   IEEE80211_CHAN_NO_160MHZ flag and the wiphy VHT cap SUPP_CHAN_WIDTH field
   is 160MHZ or 160_80PLUS80 (chan.c:1270-1286). Our driver sets
   0x00400064 (& 0xC == 4) and its DFS flag macro does not set NO_160MHZ, so
   the capability/eligibility side is DONE. Nothing has to change in the
   wiphy for 160 MHz.
3. **The missing piece is the width selector [V]:** the vendor's channel
   config event **0x0124** has a 4-byte body **{band, bandwidth, channel, 0}**.
   Byte 1 is a **bandwidth selector**, not a VAP index. The host builds it in
   hmac_config_set_channel (0x764E4; key immediate MOV r1,#0x124 at 0x76744)
   and hmac_chan_follow_reset_channel (0x76D4C; key at 0x76E48); both call
   hmac_band_set_channel_and_bandwith (0x13F520) with (channel, bandwidth) for
   host bookkeeping (band+0xD29 = channel, band+0x10D2 = bandwidth) but put no
   width on any other wire message.
4. **Firmware width path [V]:** the 0x0124 worker CA 0x841B8 stores
   body[0] -> ctx+0x11 (band), body[1] -> ctx+0x12 (bandwidth), body[2] ->
   ctx+0x10 (channel), channel-table index -> ctx+0x13; then the post-store
   band-1 RF branch CA 0x40684 calls **CA 0x101890(band=ctx+0x11,
   field=ctx+0x12, channel=ctx+0x10)**, then CA 0xA2FA2 and CA 0x89840.
   CA 0x101890 (band-1 only) validates the channel via CA 0x10172C and then
   uses the **field** to select a 1..8-word descriptor window from the
   parameter table **CA 0x10BE68** (rows per channel-table index; field cases
   0..6 and 7..14; mapper CA 0x101650; the window words are compared before
   the predicate is returned). So the 80/160 choice reaches the RF programme
   through ctx+0x12 and the CA 0x101890 descriptor selection.
5. **The earlier CA 0x89924 reading, corrected [V]:** CA 0x89924 is the
   ch36-64 RF register image builder called by CA 0xA2FA2 when the channel
   table index is 8..28 (the range test at CA 0xA2FA2); it takes the band
   object only and has **no width argument of its own**. The RF image differs
   by channel range (ch36-64 vs ch100-196: e.g. R+0x3B8 = 0x00E800E8 vs
   0x009400E8, ../wifi-fw-rf-programme/TABLES.md T5), not by width. Width
   influences the CA 0x101890 predicate/descriptor before the image. The
   "CA 0x89924 width" is therefore the width-selected descriptor window, not
   an image parameter.
6. **What the driver must send [I]:** our AP-start ladder currently sends
   0x0124 body {band, 0, channel, 0} (the second byte was documented as a VAP
   index in the beacon lane; the firmware consumer proves it is the bandwidth
   selector). Value 0 == HAL channel assemble 20M (the vendor userspace
   bandwidth table g_pauc_bw_tbl starts with "20" at index 0 and reserves
   slots 1..15 for 40/80/160/...; ../wifi-algcfg-slices vendor wal_hipriv.c).
   The natural encoding is the vendor HAL channel-assemble enum
   (0=20M, 1=40M, 2=80M, 3=160M; 5M/10M are slots 16/17 in that enum
   ordering). **The exact value for 160 must be confirmed by one read**
   (PROBES.md P-160a: read ctx0 module+0x12 after a vendor/stock 160 MHz AP,
   or after our first 160 rung with a known-good descriptor) - marked [I]
   until then; do not hard-code it as proven.
7. **Channel layout [I, IEEE]:** contiguous 160 MHz uses one segment:
   ch36-64 -> CCFS1 ch50 (5250 MHz), ch100-128 -> CCFS1 ch114 (5570 MHz),
   ch149-177 -> CCFS1 ch163 (5815 MHz). CCFS2 (80+80 second segment) is 0:
   the vendor cap has the 160 bit but not the 160+80+80 bit (0x00400064 &
   0x8 == 0), so 80+80 is not supported and the driver must refuse
   NL80211_CHAN_WIDTH_80P80 (-EOPNOTSUPP). 80 MHz CCFS1 for reference:
   36-48->42, 52-64->58, 100-112->106, 116-128->122, 132-144->138,
   149-161->155, 165-177->171.

## 2. The host path in detail

    cfg80211 chandef width (hostapd: vht_oper_chwidth=2 for 160)
      -> driver start_ap set->chandef.width (NL80211_CHAN_WIDTH_160 = 4)
      -> vendor reference: wal_cfg80211_convert_width_to_value (0x101A98)
         table at host .rodata file 0x170620 = [20,20,40,80,80,160] for
         NL80211_CHAN_WIDTH 0..5 (160 -> 160 MHz; 80+80 -> 80 fallback)
      -> channel config 0x0124 body {band, bandwidth_enum, channel, 0}
      -> firmware worker CA 0x841B8 -> ctx+0x12
      -> CA 0x40684 -> CA 0x101890 field select -> CA 0xA2FA2/CA 0x89924
         RF image -> CA 0x89840 latch.

The vendor also has a "set bandwidth" WAL op: wal_config_set_bw (0x12B158) ->
hmac_config_set_bw (0x73A58) -> config event **0x0147** (MOVW r1,#0x147 at
0x73A68); the firmware worker CA 0x436BC stores body[0] to obj+0x531 (the MAC
bandwidth byte, consumed by the MAC config cluster around CA 0x119xxx). That
is a separate WAL/hipriv command ("set_bw") used by the vendor userspace for
fixed-bandwidth tests; the AP-start width does not need it: 0x0124 body[1] is
the AP width path. [V] for both messages, [I] for which one stock hostapd's
chandef actually drives on every AP start (the cfg80211 start_ap path uses
the 0x0124 channel event; the F2 5 GHz AP did program the RF image with
0x0124 alone).

## 3. Host implementation (summary; details in HOST-IMPLEMENTATION.md)

1. Map the cfg80211 chandef width to the bandwidth selector:
   20_NOHT/20 -> 0 (20M), 40 -> 1 (40M), 80 -> 2 (80M), 160 -> 3 (160M)
   [I value], 80P80/320 -> refuse (-EOPNOTSUPP) BEFORE any frame.
2. Put the selector in the 0x0124 body byte 1 instead of 0 in
   hi5622_ap_frames/hi5622_band5g_ap_chan_prepare (the builder currently
   sends {band, 0, channel, 0}); keep the byte-identical 2.4 GHz behaviour.
3. Keep the CAS/DFS interplay: a 160 MHz start on ch36-64 spans only
   non-DFS channels (36-64 includes 52-64 DFS in the middle!); ch36-64
   includes DFS channels 52-64, so a 160 MHz AP on CCFS1=50 needs CAC on the
   whole 80+80 span - hostapd's DFS handling covers the control channel and
   driver CAC must be able to run for the segment (see REPORT-DFS.md; rung
   160-2 uses the upper segment 100-128 only when DFS is implemented, else
   use 149-177 or accept that ch36-64 160 MHz is legal in US only when the
   whole span is clear).
4. Hostapd config: see hostapd/hostapd-160.conf. Since the driver wiphy
   already advertises the 160 bit, the config does NOT need to override
   vht_capab; the width comes from vht_oper_chwidth=2 +
   vht_oper_centr_freq_seg0_idx=50 (hostapd 2.12; for a VHT-only AP) and
   he_oper_chwidth=2 when ieee80211ax=1 (HE160; the HE Operation follows the
   same chandef). The driver's he_caps=1 advertisement already includes the
   5 GHz 160 MHz channel-width bit (wifi-ax-he-caps lane).

## 4. Open items / unknowns

* [U] the exact bandwidth selector value for 160 (3 assumed from the
  assemble-enum order; PROBES.md P-160a decides it with one read).
* [U] whether the CA 0x101890 descriptor for 160 MHz requires an additional
  host setup (the predicate is silent; the readback R+0x410 bit0 is the only
  outcome signal). The RF-gate readback pack is the decision instrument.
* [I] ch144: the vendor tables carry ch144, our driver does not advertise it;
  for a 160 MHz AP on 100-128 ch144 is not in the span, so it is not needed
  for this feature (it matters for the DFS 100-144 list).
* [U] HE160 vs VHT160: the vendor cap word is VHT; the HE caps path
  (he_caps=1) advertises 160 MHz HE channel width as well, but whether the
  firmware needs a different width selector for HE160 is not decoded; the
  first rung should be VHT160 (ieee80211ax=0) to isolate one variable.
