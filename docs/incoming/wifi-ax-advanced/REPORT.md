# REPORT - the vendor 802.11ax advanced machinery: OFDMA, MU-MIMO, TWT, BSS color

**Lane:** `wifi-ax-advanced` (shared task **task-195**).  **HOST-ONLY**: no
router contact, no bench fire from this lane; the Lead fires.  **Base
hardware:** Hi5622V100, 802.11ax (Wi-Fi 6), 2x2 dual-band (Wi-Fi 7/EHT is not
on this silicon).  **Starting state:** the 2.4 GHz HE AP is live - the on-air
beacon carries HE Capabilities + HE Operation (params `0x003FF4`, BSS Color
25); the RX gate is open but no client association has been proven yet.

## 1. What was asked and what "smallest evaluable" means here

AX-3..AX-7 in docs/incoming/FEATURE-BACKLOG/SPEC-AX.md (OFDMA, MU-MIMO, TWT,
BSS coloring, HE sounding) were all "NOT STARTED".  The vendor's advanced AX
features do not appear in our clean-room driver at all: our AP-start ladder
posts an AP-config body (mode 10 = HE) and nothing else AX-specific.  So the
smallest evaluable unit is:

1. **decode** the vendor host's AX control messages and the firmware workers
   that consume them (this lane);
2. extract a **portable host frame model** for the config events that are
   safe to post (this lane: `src/`);
3. define a **read-only probe pack** that confirms the decode on the live
   firmware and resolves the one runtime pointer the static decode cannot
   (this lane: FIRE-RECIPE R0);
4. define the **one or two rungs** that can be evaluated on the current
   2.4 GHz HE AP without the RX gate (R1/R2), and state plainly which
   features need an associated AX client (R3 - blocked by RX).

## 2. Findings (details in TABLES.md)

**F1 [V] The vendor has two AX control planes, not one.**  The static g0-id1
config-event tables (table A CA 0x10C23C, 217 rows; table B CA 0x103BF8, 88
rows) carry the small AX feature switches.  The large AX *parameter* masses
(OFDMA RU/user scheduling, MU-MIMO sounding, spatial reuse) are **not** in
those tables: they travel on the **ALG config text plane**, g0-id1 key
`0x0128` (table B row 28, worker CA 0x8313C), whose body is a token list
whose first token selects the firmware algorithm module (`sch` = scheduler,
`cmn`, `err_name`).  The vendor module carries the matching parameter
vocabulary (`alg_*ofdma*`, `alg_*sounding*`, `alg_*sr_*`), and its
host-side entry points (`wal_config_alg_cfg_param_host_entry`,
`alg_ofdma_config_param_output_entry`, `alg_sounding_config_param_output_entry`,
`alg_sr_config_param_output_entry`, ...) are all in the module .symtab
(TABLES T8).

**F2 [V] Ten AX config keys and their workers are decoded to the field.**
HE GI `0x006F`, ext-TWT IE `0x00E9`, TWT enable `0x035D`, MU EDCA
selectors `0x005B..0x005F`, BFER `0x04C8`, BFEE `0x0335`, trigger SU
BF feedback `0x032E`, cyclic-scheme `0x0353`, multi-AP SR `0x04B6`,
multi-AP tool `0x04B4`.  Every worker parses a fixed body and writes a
specific field of the firmware VAP object (`*(u32)(ctx+0x190)`) or the
per-module context: TWT -> `ctx+0xDA` bits 6/7; ext-TWT IE ->
`VAP+0x3DE`; HE GI -> `VAP+0x831`/`+0x1A7`/`+0x1A8`; MU EDCA ->
`VAP+0x1D4+4*sel+24*ac`; BFER/BFEE/trigger -> `VAP+0x1A4..+0x1A8`,
`+0x364..+0x367`; multi-AP SR -> `ctx+0xEE` (TABLES T3).

**F3 [V] TWT has a clean, minimal host path.**  The vendor user command
`wal_hipriv_twt_enable` accepts exactly {1,3}; `hmac_config_twt_enable`
posts key `0x035D` with a one-byte body; the firmware worker maps bit0 to
TWT-enable and bit1 to TWT-required in `ctx+0xDA`.  This is the one AX
feature this lane can hand to an implementation as a complete, byte-exact
recipe (19-byte body, 29-byte frame).  The *agreements* are per-STA D2H
events (`hdpp_d2h_user_twt_syn`, 0x2B6C) and need an associated STA.

**F4 [V]/[I] MU-MIMO/sounding is not a config event.**  The AP-side beam-
forming enables are config events (`0x04C8/0x0335/0x032E`), but the sounding
machinery itself is the ALG parameter plane plus a per-user sounding blob
(`hmac_sounding_config_param` reads a userspace parameter file and stores it
at user+0x94; the per-user HE block then travels in the g0 id8 user-add
message - see ../wifi-ax-he-caps T5/T6).  There is nothing to post for
"sounding" until the user object exists, i.e. until a client associates.

**F5 [V]/[L] BSS color is a firmware effect, not a host message.**  The
vendor host has complete HE Operation/BSS-color builders
(`mac_set_he_operation_params` = `4 | ((u16(MIB+0x1BA)>>6)<<4)`, exactly
the observed on-air `0x003FF4`; `mac_set_bss_color_info` = `obj+0xCC &
0x7F`; ALG SR param id `0xCE5` -> `hmac_sr_cfg_param_bss_color`), but no
g0-id1 key carries the color and the firmware beacon never reads host IEs
(../wifi-ax-he-caps certified negative).  The live beacon nevertheless shows
Color **25** [L] - so the color is firmware-side state.  This lane's honest
conclusion: without a firmware worker or a firmware patch we cannot *set*
the BSS color; R2 classifies whether it is a constant or derived (and
whether any host config knob has an effect at all).

**F6 [V] The ALG text plane is fully decoded except the handler bodies.**
Worker CA 0x8313C: length floor 8, token-count/offset body, space-to-NUL
canonicalisation, token[0] compared against the three-module name table CA
0x104FC0 ("sch"/"cmn"/"err_name"), then a call through the boot hook block
`CBH+4*(0xD2+module)` (CBH = CA 0x1747C4).  The three handler addresses are
runtime-installed (the same installer CA 0x5B424 as
../wifi-fw-callback-install; no immediate store in the image installs these
slots), so they are **read-only probes** (R0/TABLES T11), after which the
handler bodies can be decoded statically.  The host sender
`hmac_config_alg` (0x58E18) has exactly one semantic special case
(`sch`+`usr_bw` = per-STA bandwidth); everything else is forwarded raw.

**F7 [V] MU EDCA is a five-field-per-AC vendor extension.**  The host
builder posts one 12-byte body per parameter (`{u16 selector; u16 pad; u32
ac; u32 value}`) on keys `0x5B..0x5F`; the firmware writes five fields per
AC into a 24-byte-stride array at `VAP+0x1D4`.  The standard element
builder reads all five, so the semantic map is [I] (four standard per-AC
parameters + one flag byte), not [V].

## 3. The minimal host implementation path (delivered in ../src/)

The portable model (`src/hi5622_ax_events.{c,h}`) builds the g0-id1 envelope
and the nine one/two-byte AX config events plus the MU EDCA 12-byte body and
the ALG text body, with fail-closed validation:

* unknown keys are refused; every known key has a fixed body-length and
  value-range contract (TWT {1,3}; HE GI {1,2,4}; BFER 0..3; MU EDCA
  selector 0x5B..0x5F and AC 0..3; ALG token count >= 1, offsets in range,
  NUL-terminated tokens, module name in {sch, cmn, err_name});
* field B is a caller argument (must be the AP-start module index, 0 in the
  fired 2.4 GHz ladder - ../wifi-fw-dfs-worker);
* the envelope reproduces the fired 0x01F7 frame byte for byte (selftest),
  so the transport behaviour is the one already proven on the bench;
* a build-time switch in the integration lane (the pattern of
  ../wifi-dfs-mode-send and ../wifi-desc-table-send) should default **off**
  and only post the events requested by a parameter, after AP start.

What can be fired on the current 2.4 GHz HE AP: the TWT enable frame and the
ext-TWT IE frame, the MU EDCA selector frames and the BF/cyclic/SR enable
frames, each followed by a read-back of the decoded firmware fields
(FIRE-RECIPE R1).  What cannot: any effect that requires an associated AX
client (OFDMA trigger/TB PPDU, MU-MIMO NDPA/NDP, TWT agreements) or an
on-air MU EDCA/TWT element - unless the firmware beacon builder is shown to
read the decoded VAP state, which R1's scan-diff settles empirically.

## 4. What a PASS proves, what a FAIL falsifies

**R0 PASS** (all probes as expected, ALG handler slots nonzero): the decode's
field map matches the live firmware, and the three ALG handler addresses are
now known - the remaining OFDMA/sounding decode is then a pure static task.

**R2 PASS** (frame consumed, firmware field changed, page golden): our
clean-room driver can drive the vendor AX feature switches through the same
g0-id1 carrier that already worked for the AP ladder - the AX-5 (TWT enable)
and AX-6 (multi-AP SR enable) host halves are then real, independent of the
RX gate.  A FAIL at the read-back separates cleanly: worker not reached
(key/carrier/field B) vs field not written (body/length) vs page not golden
(the frame was harmful - stop, power-cycle, no retry).

**R1 PASS** (color identical across hostapd `he_bss_color`/SSID/channel
changes): the on-air color is a firmware constant; changing it needs a
firmware path and is out of scope for a host-only patch.  A different color
after a host-side change would prove a host→firmware color channel we have
not decoded - then re-open the decode.

## 5. Limits, stated honestly

* The feature set above is the **vendor's** machinery as encoded in the
  pinned 2.5.24 module + 6.18.60 bench firmware; it is not a claim that the
  firmware's scheduler/environment supports full DL/UL OFDMA with two
  clients, and no throughput or MU gain is claimed.
* The ALG handler bodies [U] and the firmware HE-IE builder [U] are the two
  remaining static gaps; both are gated on R0 probe values.
* The vendor host builders' **host-mirror** object offsets (hmac+0x18C VAP,
  +0xD2/+0xE6 per-module) are host memory, not device addresses; only the
  firmware-side offsets in TABLES T3 are probe targets.  Do not conflate.
* The on-air observation (Color 25, params 0x003FF4) is the Lead's bench
  evidence [L]; everything else here is re-derivable offline by
  `tools/fw_ax_advanced_re.py --selftest` (221/221).
* No vendor byte, firmware byte or secret is committed: the only protocol
  bytes in this lane are our own host-built frames.

## 6. Reproduce

    cd opensource/docs/incoming/wifi-ax-advanced
    python3 tools/fw_ax_advanced_re.py --selftest   # 221/221, no device
    ./tools/run-host-tests.sh                       # C model + RE tool
    python3 tools/fw_ax_advanced_re.py              # all decode tables

The fire is the Lead's; FIRE-RECIPE.md is the rung list.
