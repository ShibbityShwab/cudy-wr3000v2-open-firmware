# REPORT - the HE capability advertisement (AX-1): vendor decode, the wiphy patch, and the beacon chain

**Lane:** wifi-ax-he-caps (shared task task-177).  **HOST-ONLY**: no router
contact, no bench fire from this lane; the Lead fires.  **Base hardware:**
Hi5622V100, 802.11ax (Wi-Fi 6), 2x2 dual-band; Wi-Fi 7/EHT is not on this
silicon and nothing here assumes it.

**Deliverables of the lane** (all under this directory):

| path | content |
| --- | --- |
| `TABLES.md` | T1-T11: every decode address, the vendor HE cap values, the hostapd gate, the patch contract, the expected on-air bytes |
| `REPORT.md` | this file: findings, the chain, the patch design, falsification |
| `FIRE-RECIPE.md` | the attended rung, exact PASS/FAIL gates (2.4 GHz core + 5 GHz variant) |
| `BUILD-NOTES.md` | how the module is produced from the pinned base and what the artifact contract is |
| `patches/0001-he-caps.patch` | the fail-closed patch against the 58-param vap5g tree |
| `src/hi5622_he_caps.{c,h}` | portable HE-cap blob + policy + IE builder (kernel and host compile the same code) |
| `src/he_caps_selftest.c` | host selftest of the policy, the blob, the data contract and the exact IE bytes |
| `tools/build-he-caps-module.sh` | rebuilds the pinned vap5g base, applies the patch, builds against linux-6.18.60, asserts the artifact contract |
| `tools/run-host-tests.sh` | the no-device test entry point |
| `evidence/` | host-test and build transcripts |

## 1. What was asked and what "smallest evaluable" means here

The 802.11ax tree (FEATURE-BACKLOG AX-1) begins with two independent facts
that must both be true before any HE data can appear on the air:

1. **the wiphy advertises HE capabilities for the AP iftype** - otherwise the
   shipped hostapd refuses `ieee80211ax=1` with "Driver does not support HE"
   before it even builds a beacon (T8); and
2. **the beacon carries the HE Capabilities element** - the on-air HALF of
   the same feature.

The vendor decode (sections 2-5) supplies both the exact capability values
and the explanation of how the vendor's beacons get their HE IEs.  The patch
(section 6) implements (1) behind a default-off parameter; the fire recipe
evaluates (2).  A pass on (1) alone is host-visible and cheap; (2) is the
only thing that proves the firmware is willing to advertise HE.

## 2. The vendor's wiphy HE advertisement - decoded

`wal_cfg80211_init` (vendor host module 0x10686c) installs the two band
objects, and each 5.10 `struct ieee80211_supported_band` carries exactly one
`ieee80211_sband_iftype_data` entry in `.rodata` (T1/T2).  The decoded
capabilities are in T3.  They are deliberately sparse - this is the whole
vendor HE advertisement, not a fragment:

* iftype mask = **AP only**;
* HE MAC cap = HTC HE only (byte0 0x01);
* HE PHY cap byte0 = 0x02 on 2.4 GHz (40 MHz in 2.4 GHz) and 0x0c on 5 GHz
  (40/80 MHz + 160 MHz in 5 GHz), every other PHY byte zero;
* HE-MCS/NSS = 2 spatial streams, MCS 0-9 (0xfffa), 160 MHz additionally on
  5 GHz, 80+80 never;
* PPE thresholds empty and the PPE-present bit clear; HE 6 GHz capability 0.

There is no wiphy-level iftype data and no 6 GHz sband: the Hi5622V100 has no
6 GHz radio, so a `he_6ghz_capa` of 0 is the correct value, not an omission
(T3).  This matters because "HE 6GHz" in the 802.11ax capability tree does
not exist for this part; only the 2.4/5 GHz HE feature set does.

## 3. Where the vendor host gets the caps, and why the beacon is a firmware effect

`mac_vap_init_mib_11ax` (0x2d7ac) initialises a per-VAP 11ax MIB whose
HE-capability bytes (MAC 6 at MIB+0x198, PHY 11 at MIB+0x1a0) are the T3
values, with the band-gated PHY[0] switch (band 0 -> 0x02, band 1 ->
0x0c).  `mac_fill_he_cap_ie` (0x245d4) renders the standard HE Capabilities
element from those bytes, and `mac_set_he_capabilities_ie` only calls it
from the (re)association response / association request paths (T4).  The
vendor's beacon path never touches any of it; the beacon/beacon-IE work is
inside the firmware, and the host AP-start path carries only the 156-byte
AP-config body.  That is the negative result of the existing beacon lane
(../wifi-beacon-static T6) and it is consistent with everything decoded here.

The firmware-facing lever our driver already has is the AP-config `wireless
mode` byte (+0x8e of the 156-byte body): our builder sets **10 (HE)** iff
the hostapd beacon tail contains an extension IE (0xFF)
(../wifi-ap-start TABLES T7).  So the chain we are testing is:

    he_caps=1 -> wiphy HE caps (AP iftype) -> hostapd accepts ieee80211ax=1
      -> hostapd beacon tail contains HE Capabilities (ext 0x23) + HE Operation (0x24)
      -> our start_ap maps "extension IE present" to AP-config mode 10
      -> firmware-generated beacon (expected: includes HE IEs)

Only the last arrow is unproven and only a fire can settle it.  This lane
deliberately does **not** claim it works: the recipe's PASS is the on-air HE
Capabilities element bytes from T10.

## 4. The user_add HE capability block (the AX-2 prerequisite) - decoded

The AX-2 backlog item asks for the HE/EHT user info in the add_user message.
This lane decoded the full block map without changing the message (AX-1 does
not need to fill it):

* g0 id8 `hmac_user_add_notify_alg` (0x8e40c) copies three hdl blocks
  verbatim: VHT 24 B at payload +0x10 (user+0x7c), HT 44 B at payload +0x28
  (user+0x50), HE 58 B at payload +0x54 (user+0x94) - T5.  The sizes match
  the 5ghz-vap-msg T3 table; the source offsets are new.
* The 58-byte HE hdl is a 2-byte prelude + a **37-byte parsed-capability
  block** + a 9-byte HE Operation cache + 10 residual bytes (T6).  The
  37-byte block is exactly the standard HE Capabilities element body
  (MAC 6, PHY 11, MCS 12 - the parser stores 4 or 8 MCS bytes by PHY width -
  plus 8 PPE bytes), and the HE Operation cache is the standard 9-byte
  element body.  `mac_ie_parse_he_cap` (0x217f4) ANDs the peer's caps with
  the local MIB caps while parsing - i.e. the hdl the firmware receives is
  the negotiated intersection, not the raw peer IE.
* HT 44 B / VHT 24 B follow the same pattern: parse the peer IE into the
  per-user hdl, forward the hdl verbatim in id8.  Block maps are in T7.

Consequence for AX-2: filling id8 is a "copy the wiphy's advertised
HT/VHT/HE caps into the three hdl blocks" job; the wire encoding is already
correct and our current zero-filled blocks are why the firmware has no
negotiated rate/HE context to work with.  That is the next lane, not this
one.

## 5. The fail-closed patch (what this lane delivers)

One module parameter, default off:

    he_caps=0  (default) - no iftype_data at all; byte-identical behaviour
                           to the current bench module (hostapd still refuses
                           ieee80211ax=1)
    he_caps=1            - advertise the T3 caps for the AP iftype on every
                           installed band (2.4 GHz 0x02 / 5 GHz 0x0c)
    he_caps>1            - refuse: hi5622_cfg80211_init() fails with -EINVAL,
                           no wiphy, no AP; never guessed

Implementation notes:

* the cfg80211 side is two static `struct ieee80211_sband_iftype_data`
  arrays (one per band), selected in `hi5622_cfg11_setup_one_band()` and
  installed with the 6.18 helper `ieee80211_set_sband_iftype_data()`; the
  fields that 6.18 added (EHT/UHR/nested data) stay zero, which is correct
  for a Wi-Fi 6 radio;
* the values are not trusted blindly: at band setup the driver rebuilds the
  same blob from the portable `hi5622_he_caps.c` and **memcmp**s it against
  the static entry (mac/phy/mcs/ppe).  A drift refuses the band (-EINVAL)
  instead of advertising a wrong capability;
* everything is behind `he_caps`, so the proven AP path is unchanged unless
  the Lead opts in; the status sysfs line reports the flag for the fire log.

Host tests (`tools/run-host-tests.sh`) cover the decision matrix, the
band-specific blobs, the data contract and the exact T10 IE bytes; the build
script pins the 58-param vap5g base module and asserts the 59-param artifact
contract (BUILD-NOTES).

## 6. What a PASS proves, what a FAIL falsifies, and the limits

**PASS** (T10 bytes found in the on-air beacon for the enabled band, hostapd
does not log "Driver does not support HE", page golden): the first Wi-Fi 6
capability of the driver is real - hostapd negotiates HE capability with the
driver and the firmware beacon advertises it.  That is AX-1's whole claim.

**FAIL** (hostapd still refuses HE / beacon has no ext 0x23 / the extension
IE is present in the tail but the firmware beacon does not carry it): the
two halves separate cleanly.  A hostapd refusal means the cfg80211
advertisement shape is wrong (fixable in this lane's code).  A beacon
without HE while the tail had it means the firmware does not derive its HE
IEs from the AP-config mode byte alone - then HE-on-air requires the
firmware-side capability path (INI/firmware config) and moves to a new lane;
it does **not** invalidate the wiphy advertisement, and an ax client can
still see the AP as HE-capable only if the beacon carries the IE, which is
exactly why the fire is decisive.

Limits, stated honestly:

* an **actual AX association** (HE MCS negotiation, OFDMA, TWT, BSS color
  operation) remains blocked by the RX gate: with frames=0 hostapd never
  processes a client's auth/assoc, so the recipe's PASS is a beacon/scan
  level result, not an association;
* the HE caps are the vendor's *advertisement*, not a claim that the
  firmware datapath supports every advertised bit.  The vendor itself
  advertises 160 MHz and HTC-HE with an empty PPE set; our patch copies the
  advertisement faithfully and nothing more;
* the STA iftype is deliberately not advertised (vendor parity): HE STA
  operation is RX-path dependent anyway;
* the decode fixture is the 2.5.24 host module; the on-air behaviour is the
  6.18.60 bench firmware (PD4).  The addresses are fixture-relative, the
  capability semantics are firmware-version independent (802.11ax standard).

## 7. Reproduce

    cd opensource/docs/incoming/wifi-ax-he-caps
    ./tools/run-host-tests.sh          # no device, no network
    ./tools/build-he-caps-module.sh    # no device; builds /tmp artifact only

The vendor decode itself is reproducible with the commands at the top of
`TABLES.md` (objdump on the local untracked host module fixture).  The fire
is the Lead's; `FIRE-RECIPE.md` is the one-shot rung.
