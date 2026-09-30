# Phase 2 - Wi-Fi power, regulatory and calibration decode

Read-only analysis of the vendor Wi-Fi configuration. **No device access and no driver or
firmware modification was performed.**

## Sources

- `build/tmp/cfg_wifi.ini` - 1015 lines, three sections: `[HOST_WIFI_NORMAL]` (line 12),
  `[HOST_WIFI_PWR_LIMIT_LU]` (line 802), `[CUSTOM_REGDOMAIN_CFG]` (line 995).
- `build/custom/DRIVER-BLACKBOX.md` - sections 4 and 5 (silicon context, `alg` command outputs).

## Number provenance (the verification contract used by this document)

Every number in this file is one of:

1. **verbatim** - copied from `cfg_wifi.ini`, with its key named next to it; or
2. **tuple** - one of the 2.4 GHz calibration tuples quoted in the task; or
3. **derived** - an explicit count or conversion, with the arithmetic or the rule shown inline.

Numbers that are standard external knowledge (channelisation constants) are confined to the
**"External constants"** block near the end and are never mixed into a provenance claim.

---

## (a) `[HOST_WIFI_PWR_LIMIT_LU]` parsed

The section holds **186 key lines** = **166** `pwr_limit_*` keys + **20** non-`pwr_limit` keys
(arithmetic: 166 + 20 = 186, parsed with
`sed -n '/^\[HOST_WIFI_PWR_LIMIT_LU\]/,/^\[CUSTOM_REGDOMAIN_CFG\]/p' cfg_wifi.ini`). It is
introduced by the comment `#LU国家码限制` ("LU country-code restriction"), i.e. it is the
Luxembourg override of the world defaults stored in `[HOST_WIFI_NORMAL]`.

> **Section-scoped vs whole-file counts (reconciliation).** A whole-file
> `grep -c '^pwr_limit_' cfg_wifi.ini` returns **332**, not 166, because the same key names
> exist in two sections: `[HOST_WIFI_NORMAL]` **166** + `[HOST_WIFI_PWR_LIMIT_LU]` **166** =
> **332**. Every count in this section is **section-scoped** (166 here); **332** is the
> whole-file total. Both are correct and are not interchangeable.
>
> - Whole-file value histogram: `0x003C3C3C` x208, `0x001E1E1E` x42, `0x00323232` x33,
>   `0x00242424` x22, `0x00181818` x14, `0x00121212` x13 (208+42+33+22+14+13 = 332).
> - This section's value histogram: `0x003C3C3C` x42, `0x001E1E1E` x42, `0x00323232` x33,
>   `0x00242424` x22, `0x00181818` x14, `0x00121212` x13 (42+42+33+22+14+13 = 166). The extra
>   208 - 42 = 166 `0x003C3C3C` keys are the `[HOST_WIFI_NORMAL]` world defaults.
> - Whole-file band split: 2g **112** + 5g **220** = **332**. This section's band split (see
>   the matrices below): 2g **56** + 5g **110** = **166**.
> - The 20 non-`pwr_limit` keys here are `cali_dpd_txpwr_enable_2g` and `_5g` (2),
>   `cali_dpd_txpwr_pow_ref_2g/5g_val_band*` (10) and `cali_temp_dpd_txpwr_*` (8). A whole-file
>   `grep -c '^\(over_temp_pro_\|cali_temp_\|dpd_\)'` returns **14** because it also spans
>   `[HOST_WIFI_NORMAL]` and does not match the `cali_dpd_` prefix; it is not the count of this
>   section's non-`pwr_limit` keys.

### Key grammar

`pwr_limit_<band>_<mode>_ch<NN>` with:

- `band` in {`2g`, `5g`}
- `mode` in {`11b`, `11g`, `11a`, `11ax20`, `11ax40`, `11ax80`, `11ax160`}
- `ch` = channel number, written with a leading zero below 10 (`ch01` ... `ch09`).

Which band/mode pairs actually exist (all of them are enumerated in the matrix below):

| band | modes present | channels per mode |
| --- | --- | --- |
| 2g | `11b`, `11g`, `11ax20`, `11ax40` | 1-14 each |
| 5g | `11b`, `11a`, `11ax20`, `11ax40`, `11ax80`, `11ax160` | see 5 GHz matrix |

### Value format and decode

Value is an 8-hex-digit `0x` word = **4 bytes**. In **every one of the 166** entries the top
byte is `00` and the three payload bytes are identical, e.g.
`pwr_limit_2g_11b_ch01=0x00181818` -> bytes `00 18 18 18` (`18` hex = 24 decimal).

**Hypothesis H-a1 (three payload bytes = one limit per spatial stream, or per mode).**
The three payload bytes are three copies of a single power limit. Evidence:

1. In all 166 keys the three payload bytes are byte-identical, so they encode the same
   quantity three times, not three independent numbers.
2. `DRIVER-BLACKBOX.md` says so directly: *"three bytes = one limit per spatial stream/mode"*.
3. `[HOST_WIFI_NORMAL]` stores the same keys with the single maximum value `0x003C3C3C`
   for every channel and mode, while LU lowers selected ones; one logical limit per key
   reproduces exactly this pattern.

The leading `00` byte is not explained by H-a1. The INI's own build string identifies an
`AX3000` (a 2x2 radio), so only two streams exist while three payload bytes are populated.
It is consistent with either a reserved/unused 4th slot or a mode slot. **Flagged as an open
question, not asserted.**

**Hypothesis H-a2 (units = 0.5 dBm per LSB, i.e. `value / 2` = dBm).** The largest value in the
file is `0x3C` = 60 (whole-file count 208; within this section 42), and 60 / 2 = 30 dBm, which is
exactly the max-EIRP field (`30`) of every LU regdomain band (section (b)). Counter-check below.
Under H-a2 the raw bytes map as:

| raw byte | decimal | dBm under H-a2 | whole-file count |
| --- | --- | --- | --- |
| `12` | 18 | 9 | 13 |
| `18` | 24 | 12 | 14 |
| `1E` | 30 | 15 | 42 |
| `24` | 36 | 18 | 22 |
| `32` | 50 | 25 | 33 |
| `3C` | 60 | 30 | 208 |

These six are the only distinct payload bytes in the whole file; the count column is the
whole-file histogram (`grep '^pwr_limit_' cfg_wifi.ini | sed 's/.*=//' | sort | uniq -c`), which
sums to 13 + 14 + 42 + 22 + 33 + 208 = 332.

Alternative scalings, kept or rejected:

- **1 dBm/LSB** - rejected: `0x3C` would be 60 dBm (1 kW), physically impossible.
- **0.25 dBm/LSB** - rejected: the legal maximum would be only 15 dBm, inconsistent with a
  regdomain that permits 30 dBm.
- **0.5 dBm/LSB (H-a2)** - kept: the only candidate that reproduces the regdomain's 30 dBm
  ceiling.

### 2.4 GHz channel x rate matrix

| mode | channels | raw value | payload bytes (decimal) | count |
| --- | --- | --- | --- | --- |
| `11b` | 1-14 | `0x00181818` | 24/24/24 | 14 |
| `11g` | 1-14 | `0x001E1E1E` | 30/30/30 | 14 |
| `11ax20` | 1-14 | `0x001E1E1E` | 30/30/30 | 14 |
| `11ax40` | 1-14 | `0x001E1E1E` | 30/30/30 | 14 |

2.4 GHz subtotal (this section): **56** keys (14 + 14 + 14 + 14).

### 5 GHz channel x rate matrix

Consecutive channels sharing a value are collapsed to a range; every 20/40/80/160 MHz channel
key in the section is listed exactly once below.

| mode | channel ranges -> raw value | payload bytes (decimal) | count |
| --- | --- | --- | --- |
| `11b` | 36-196 (all 29 listed channels) -> `0x003C3C3C` | 60/60/60 | 29 |
| `11a` | 36-64 -> `0x00242424`; 100-144 -> `0x00323232`; 149-165 -> `0x00121212`; 184-196 -> `0x003C3C3C` | 36/36/36; 50/50/50; 18/18/18; 60/60/60 | 29 |
| `11ax20` | 36-64 -> `0x00242424`; 100-144 -> `0x00323232`; 149-165 -> `0x00121212`; 184-196 -> `0x003C3C3C` | 36/36/36; 50/50/50; 18/18/18; 60/60/60 | 29 |
| `11ax40` | 38-62 -> `0x00242424`; 102-142 -> `0x00323232`; 151,159 -> `0x00121212`; 186,194 -> `0x003C3C3C` | 36/36/36; 50/50/50; 18/18/18; 60/60/60 | 14 |
| `11ax80` | 42,58 -> `0x00242424`; 106,122,138 -> `0x00323232`; 155 -> `0x00121212`; 190 -> `0x003C3C3C` | 36/36/36; 50/50/50; 18/18/18; 60/60/60 | 7 |
| `11ax160` | 50,114 -> `0x003C3C3C` | 60/60/60 | 2 |

The `11b` 5 GHz channel set is the same 29 channels as the 20 MHz set, all at
`0x003C3C3C`:

`36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165,184,188,192,196`

5 GHz subtotal (this section): **110** keys (29 + 29 + 29 + 14 + 7 + 2, section-scoped).

Section subtotal: **56** + **110** = **166** `pwr_limit_*` keys, plus **20** non-`pwr_limit`
keys = **186** total. Every channel in the section is accounted for above. (Whole-file
`pwr_limit_*` total: **332** = 166 in `[HOST_WIFI_NORMAL]` + 166 here.)

The four 2.4 GHz modes are per channel 1-14 (14 distinct channels). The 5 GHz 20 MHz modes
cover 29 distinct channels; the 40/80/160 modes use the corresponding bonded centre channels.

The complete per-channel value assignment (used in the counter-checks) is:

- 5 GHz 20 MHz (and 5 GHz `11b`): `0x00242424` on 36,40,44,48,52,56,60,64; `0x00323232` on
  100,104,108,112,116,120,124,128,132,136,140,144; `0x00121212` on 149,153,157,161,165;
  `0x003C3C3C` on 184,188,192,196 (the `11b` rows use `0x003C3C3C` on all 29).
- 5 GHz `11ax40`: `0x00242424` on 38,46,54,62; `0x00323232` on 102,110,118,126,134,142;
  `0x00121212` on 151,159; `0x003C3C3C` on 186,194.
- 5 GHz `11ax80`: `0x00242424` on 42,58; `0x00323232` on 106,122,138; `0x00121212` on 155;
  `0x003C3C3C` on 190.
- 5 GHz `11ax160`: `0x003C3C3C` on 50,114.

---

## (b) `[CUSTOM_REGDOMAIN_CFG]` parsed

**16 keys** = **3** `ALPHA2` keys + **13** `BAND` keys (BZ has BAND0-BAND4 = 5; DE and LU have
BAND0-BAND3 = 4; 5 + 4 + 4 = 13; 3 + 13 = 16 - counts derived by parsing the section).

Entry grammar:
`custom_regdomain_<NN>_ALPHA2=<CC>` and
`custom_regdomain_<NN>_BANDx=<start MHz>,<end MHz>,<max width MHz>,<f4>,<max EIRP dBm>,<flag>`.

| index | country | band | start MHz | end MHz | max width | f4 | max EIRP | flag |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 00 | BZ | BAND0 | 2402 | 2482 | 40 | 0 | 30 | 0 |
| 00 | BZ | BAND1 | 5170 | 5250 | 80 | 0 | 30 | 0 |
| 00 | BZ | BAND2 | 5250 | 5330 | 80 | 0 | 30 | 24 |
| 00 | BZ | BAND3 | 5490 | 5710 | 160 | 0 | 30 | 24 |
| 00 | BZ | BAND4 | 5735 | 5835 | 80 | 0 | 30 | 0 |
| 01 | DE | BAND0 | 2402 | 2482 | 40 | 0 | 30 | 0 |
| 01 | DE | BAND1 | 5170 | 5250 | 80 | 0 | 30 | 0 |
| 01 | DE | BAND2 | 5250 | 5330 | 80 | 0 | 30 | 24 |
| 01 | DE | BAND3 | 5490 | 5710 | 160 | 0 | 30 | 24 |
| 02 | LU | BAND0 | 2402 | 2482 | 40 | 0 | 30 | 0 |
| 02 | LU | BAND1 | 5170 | 5250 | 80 | 0 | 30 | 0 |
| 02 | LU | BAND2 | 5250 | 5330 | 80 | 0 | 30 | 24 |
| 02 | LU | BAND3 | 5490 | 5710 | 160 | 0 | 30 | 24 |

Every field above is verbatim from the corresponding `custom_regdomain_NN_BANDx` value.

### Per-field hypothesis

| field | hypothesis | evidence / counter-check |
| --- | --- | --- |
| 1 | **start frequency, MHz** | Values `2402` / `5170` / `5250` / `5490` / `5735`; `2402` is the classic 2.4 GHz band start and `5170`/`5250`/`5490` are the EU 5 GHz sub-band lower edges. **Hypothesis** (frequencies are standard, not stated in the INI). |
| 2 | **end frequency, MHz** | Values `2482` / `5250` / `5330` / `5710` / `5835`. Counter-check: `2482` sits below the channel-14 centre and `5710` below the channel-144 centre, which is why those channels fall outside the LU plan (see below). **Hypothesis**, corroborated by the boundary behaviour. |
| 3 | **max channel width, MHz** | Only `40`, `80`, `160` occur, matching 2.4 GHz and 5 GHz channel-width steps: `40` on BAND0, `80` on BAND1/BAND2/BAND4, `160` on BAND3. **Hypothesis** with strong structural evidence. |
| 4 | **unknown, constant `0`** | `0` in all 13 BAND rows, so it carries no observable signal in this file. Candidate meanings (unproven): reserved, a TPC-required flag, or a max-antenna-gain field. **Explicitly unresolved - no decode is claimed.** |
| 5 | **max EIRP, dBm** | `30` in all 13 rows. Counter-check: the largest power-limit payload byte anywhere is `0x3C` = 60, and 60 / 2 = 30 under H-a2; the two independent tables agree on a 30 dBm ceiling. **Hypothesis** with a numeric cross-check. |
| 6 | **DFS / indoor flag, bitmask** | Only `0` or `24` occur. `24` = `0x18` = bit3 (`8`) | bit4 (`16`). In the nl80211/RRF flag bit order that is NO-OUTDOOR (8) | DFS (16). `24` appears **only** on BAND2 (`5250-5330`) and BAND3 (`5490-5710`) - exactly the bands that require DFS and indoor-only operation. **Hypothesis** with strong structural evidence. |

### Counter-checks against the power-limit numbers

- **Ceiling agreement (H-a2).** Every BAND row caps EIRP at `30` dBm. The maximum possible
  power-limit payload byte is `0x3C` = 60, and 60 / 2 = 30. The regulatory table and the power
  table are consistent only under the 0.5 dBm/LSB reading of H-a2.
- **Channel 14 is out of plan.** LU BAND0 ends at `2482` MHz while channel 14 is centred at
  2484 MHz (external constant). The power table nevertheless defines
  `pwr_limit_2g_11b_ch14=0x00181818`, `pwr_limit_2g_11g_ch14=0x001E1E1E`,
  `pwr_limit_2g_11ax20_ch14=0x001E1E1E`, `pwr_limit_2g_11ax40_ch14=0x001E1E1E`. Those four
  entries are unreachable under the LU plan.
- **Channel 144 is out of plan.** LU BAND3 ends at `5710` MHz while channel 144 is centred at
  5720 MHz (external constant). `pwr_limit_5g_11a_ch144=0x00323232` and
  `pwr_limit_5g_11ax20_ch144=0x00323232` are defined but unreachable; the 40 MHz block key
  `pwr_limit_5g_11ax40_ch142=0x00323232` overlaps that region.
- **Channels 149-165 and 184-196 are out of plan for LU.** No LU BAND covers `5735-5835` or
  the 184-196 region, yet `pwr_limit_5g_11a_ch149=0x00121212` ... `pwr_limit_5g_11a_ch165=0x00121212`
  and `pwr_limit_5g_11a_ch184=0x003C3C3C` ... `pwr_limit_5g_11a_ch196=0x003C3C3C` exist. They are
  in plan only for BZ, whose `custom_regdomain_00_BAND4=5735,5835,80,0,30,0` covers 5735-5835.
- **In-plan LU channels.** BAND1 (`5170-5250`) covers 36,40,44,48; BAND2 (`5250-5330`) covers
  52,56,60,64; and BAND3 (`5490-5710`) covers 100,104,108,112,116,120,124,128,132,136,140. That
  is 4 + 4 + 11 = 19 of the 29 listed 20 MHz channels. The remaining 10 are 144, 149, 153, 157,
  161, 165, 184, 188, 192 and 196.

---

## (c) Live 2.4 GHz calibration tuples (`iwpriv Hisilicon0 alg get_2g_power_param`)

> **Count discrepancy, stated honestly.** The task text says 27 tuples. The list as supplied
> contains **26** (counted by whitespace-splitting the quoted string: 3 + 2 + 4 + 1 + 3 + 3 + 4
> + 1 + 2 + 3 = 26). One entry is missing from the supplied data. This decode covers the 26
> present and does **not** invent a 27th.

Each tuple is 8 hex digits = **4 bytes**. Bytes are shown most-significant first:

| # | tuple | byte0 | byte1 | byte2 | byte3 |
| --- | --- | --- | --- | --- | --- |
| 1 | `17161605` | 23 | 22 | 22 | 5 |
| 2 | `17161605` | 23 | 22 | 22 | 5 |
| 3 | `17161605` | 23 | 22 | 22 | 5 |
| 4 | `17161505` | 23 | 22 | 21 | 5 |
| 5 | `17161505` | 23 | 22 | 21 | 5 |
| 6 | `15161401` | 21 | 22 | 20 | 1 |
| 7 | `15161401` | 21 | 22 | 20 | 1 |
| 8 | `15161401` | 21 | 22 | 20 | 1 |
| 9 | `15161401` | 21 | 22 | 20 | 1 |
| 10 | `15161400` | 21 | 22 | 20 | 0 |
| 11 | `08141200` | 8 | 20 | 18 | 0 |
| 12 | `08141200` | 8 | 20 | 18 | 0 |
| 13 | `08141200` | 8 | 20 | 18 | 0 |
| 14 | `0c111103` | 12 | 17 | 17 | 3 |
| 15 | `0c111103` | 12 | 17 | 17 | 3 |
| 16 | `0c111103` | 12 | 17 | 17 | 3 |
| 17 | `0b101002` | 11 | 16 | 16 | 2 |
| 18 | `0b101002` | 11 | 16 | 16 | 2 |
| 19 | `0b101002` | 11 | 16 | 16 | 2 |
| 20 | `0b101002` | 11 | 16 | 16 | 2 |
| 21 | `0b101001` | 11 | 16 | 16 | 1 |
| 22 | `0a0f0f01` | 10 | 15 | 15 | 1 |
| 23 | `0a0f0f01` | 10 | 15 | 15 | 1 |
| 24 | `0a0606ff` | 10 | 6 | 6 | 255 |
| 25 | `0a0606ff` | 10 | 6 | 6 | 255 |
| 26 | `0a0606ff` | 10 | 6 | 6 | 255 |

### Byte-level structure hypothesis

**Preferred hypothesis H-c1: `[s0][s1][s2][tag]` - three per-stream (or per-chain) values plus a
trailing tag/flag byte.** Evidence:

1. The leading three bytes form monotone, non-increasing *runs* as the list is walked
   (23,22,22 -> 23,22,21 -> 21,22,20 -> 8,20,18 -> 12,17,17 -> 11,16,16 -> 10,15,15 -> 10,6,6).
   This is the signature of a rate-ordered table whose power falls as rate order increases.
2. The trailing byte takes only `0`, `1`, `2`, `3`, `5` and `0xff`. Those are not power-like
   magnitudes; they read as an enum or index, and `0xff` is the natural -1 sentinel.
3. Three leading bytes match the three-payload-byte convention already seen in the
   `pwr_limit_*` values (section (a), H-a1), so the driver plausibly uses one shared
   power-triple layout.
4. If the same 0.5 dBm/LSB convention as H-a2 applied, tuple 1 `[23,22,22]` would be
   11.5 / 11 / 11 dBm - a physically sane per-stream calibration backoff. (**This is an
   inference from H-a2, not an independent measurement.**)

The list collapses into groups of identical tuples; the group sizes are 3, 2, 4, 1, 3, 3, 4,
1, 2, 3 (sum 26). A plausible reading is "one row per rate, repeated per chain", but the repeat
counts do not cleanly match the 2x2 chain count, so this is left open.

**Alternative interpretations considered:**

- **A-c1: two big-endian 16-bit fields `[b0b1][b2b3]`.** For tuple 1 that is `0x1716` = 5910 and
  `0x1605` = 5637. Rejected as power values: the magnitudes are far outside any dBm or
  0.5 dBm range, and there is no reason for the two halves to decrease together across the run.
  (Kept only as a note: the INI's `cali_upc_protect_limit_2g=0x01ff01ff` does use two 16-bit
  fields, so the driver *elsewhere* packs 16-bit words; the sizing does not transfer here.)
- **A-c2: two little-endian 16-bit fields.** Symmetric to A-c1; rejected for the same reason
  (byte order does not rescue the magnitude).
- **A-c3: signed 8-bit deltas throughout.** Only byte3 ever exceeds `0x7f` (`0xff` = -1), so the
  sign of bytes 0-2 cannot be established. Kept as a caveat to H-c1: bytes 0-2 might be signed
  offsets rather than magnitudes; the run structure is identical under either reading.
- **A-c4: `[rate_index][power0][power1][flag]` (rate index first).** Rejected: the leading byte
  reaches `0x17` = 23, which is not a plausible 2.4 GHz rate index (CCK has 4 rates, OFDM 8,
  HT/VHT MCS well under 23), whereas as a power it fits the run pattern exactly.
- **A-c5: per-rate rows copied once per chain.** Plausible and compatible with H-c1, but the
  group sizes (3, 2, 4, 1, ...) do not equal the 2 chains of the AX3000 board, so it cannot be
  confirmed from this data. Left open.

**What would settle it:** disassembling the `alg_get_2g_power_param` handler (and
`hmac_save_cali_data_to_file_2g`, named in `DRIVER-BLACKBOX.md`) in
`lib/modules/5.10.201/hi5622v100_wifi.ko`; the struct layout there is authoritative. **Not done
here - out of scope for this local-only pass.**

### Companion values

**`iwpriv Hisilicon0 alg get_xo_ppm_cali_param` -> `00006060`.** 8 hex digits = 4 bytes
`00 00 60 60`. Hypotheses:

- **H-x1 (preferred): two 8-bit trim codes.** The trailing pair `60` `60` is the crystal trim,
  duplicated once per radio; the leading `00 00` is padding. Evidence: the board exposes two PCIe endpoints
  (`0000:00:00.0` and `0001:00:00.0`, one per radio, `DRIVER-BLACKBOX.md` section 1), and the
  two trailing bytes are equal. `0x60` = 96 decimal.
- **H-x2: two 16-bit words `0x0000` and `0x6060`** (`0x6060` = 24672 decimal). Kept as an
  alternative; it cannot be distinguished from H-x1 without the handler.
- **H-x3: one 32-bit value `0x00006060`** = 24672. Rejected as less likely: `0x0000` in the high
  half suggests a padding/zero field rather than the top of one number.

The value is a **crystal offset/trim**, not a ppm figure in the usual sense; no unit is asserted.
**All three readings are hypotheses.**

**`iwpriv Hisilicon0 alg get_rssi_param` -> ten zero words.** All-zero RSSI calibration values
mean **no RSSI calibration is stored on this unit** (factory default). No structure is decoded
from zeros; this is stated as fact from `DRIVER-BLACKBOX.md` section 5 and the zero returns.

---

## (d) Tunable keys: effect and legal note

All values below are verbatim from `cfg_wifi.ini`. "Legal note" is a one-line reminder, not
legal advice.

| key(s) | value(s) | likely effect | legal note |
| --- | --- | --- | --- |
| `pwr_limit_2g_*`, `pwr_limit_5g_*` (all modes) | world defaults in `[HOST_WIFI_NORMAL]` are `0x003C3C3C`; LU overrides in section (a) | per-channel / per-rate / per-stream TX power cap applied by the driver | never set above the active country's regdomain max EIRP (LU BAND max EIRP = `30`) |
| `cali_upc_protect_limit_2g` | `0x01ff01ff` (two 16-bit fields `0x01ff`) | calibration/UPC protect limit: clamps the calibration or PA-drive loop for 2.4 GHz | a protect limit must never be relaxed so far that the TX cap above is bypassed |
| `cali_upc_protect_limit_5g` | `0x00ff00ff` (two 16-bit fields `0x00ff`) | same for 5 GHz | same: keep the clamp at or below the regulatory TX cap |
| `cali_temp_change_pow_amend` | `0x00050005` (two 16-bit fields `0x0005`) | power amendment applied when temperature changes (thermal backoff) | thermal backoff may only lower power; never use it to add power beyond the cap |
| `over_temp_pro_enable` | `1` | master over-temperature protection switch | keep enabled so high-temperature operation cannot exceed the power cap |
| `over_temp_pro_reduce_pwr_enable` | `1` | enables power reduction on over-temperature | keep enabled; disabling risks over-limit TX when hot |
| `over_temp_pro_safe_th` | `105` | safe temperature threshold | thermal only; lowering is safe, raising risks over-temp over-limit |
| `over_temp_pro_over_th` | `113` | over-temperature threshold | thermal only |
| `over_temp_pro_pa_off_th` | `115` | PA-off temperature threshold | thermal only; must stay above normal operating range |
| `junction_temp_diff` | `18` | junction-to-case temperature offset used by the thermal model | model-only; a wrong offset can under- or over-state the real die temperature |
| `dync_cali_temp_factor_2g_c0`, `_c1`, `dync_cali_temp_factor_5g_c0`, `_c1` | `0x00002710` | dynamic-calibration temperature slope factor per chain and band | calibration scaling; must not be used to lift TX above the regulatory cap |
| `cali_temp_dpd_txpwr_2g_c0_k`, `_c1_k`, `_c0_b`, `_c1_b` | `0x5`, `0x9`, `0x38240`, `0x3A619` | temperature -> TX-power DPD fit (per-chain slope k and intercept b) | DPD only linearises the PA; it must not be used to raise output beyond the cap |
| `cali_temp_dpd_txpwr_5g_c0_k`, `_c1_k`, `_c0_b`, `_c1_b` | `0xC5`, `0xCA`, `0x3A90C`, `0x3E18E` | same fit for 5 GHz | same |
| `delta_cca_ed_high_20th_2g` | `0` | CCA energy-detect threshold delta (dB) for 20 MHz, 2.4 GHz | do not raise the ED threshold above the regulatory requirement (EU: -62 dBm/20 MHz class rules) |
| `delta_cca_ed_high_20th_5g` | `-8` | CCA ED delta (dB) for 20 MHz, 5 GHz | as above; a positive delta would be the risky direction |
| `delta_cca_ed_high_40th_5g` | `0` | CCA ED delta (dB) for 40 MHz, 5 GHz | as above |
| `delta_cca_ed_high_80th_5g` | `0` | CCA ED delta (dB) for 80 MHz, 5 GHz | as above |
| `feature_flags` | `0xc03` (= `0b110000000011`, bits 0, 1, 10, 11 set) | driver feature bitfield; exact bit meanings require disassembly | enabling features must never disable regdomain enforcement or DFS |
| `beacon_2g_tx_policy` | `0` | beacon transmit rate/power policy selector, 2.4 GHz | beacon energy counts toward EIRP; keep within the cap |
| `beacon_5g_tx_policy` | `2` | beacon transmit rate/power policy selector, 5 GHz | same |
| `beacon_pow_adjust_2g`, `beacon_pow_adjust_5g` | `0x00030303` (three payload bytes `03`) | beacon power adjustment per stream | beacons are subject to the same EIRP cap as data |
| `smartant_board_type` | `0` | smart-antenna board configuration (0 = default/off here) | antenna gain contributes to EIRP; the regdomain plan must reflect it |
| `rf_pll_ppm_correct`, `rf_pll_ppm_threshold` | `0`, `2` | crystal/PLL ppm correction value and threshold | frequency accuracy only; must stay inside the regulatory ppm mask |
| `cali_dpd_txpwr_enable_2g`, `_5g` | `0x00000` | DPD TX-power calibration enable (both off here) | DPD only linearises the PA; never use it to exceed the cap |
| `cali_dpd_txpwr_pow_ref_2g_val_band1..3`, `cali_dpd_txpwr_pow_ref_5g_val_band1..7` | `0x8660866` ( = `0x08660866`, two identical 16-bit fields `0x0866`) | DPD power reference per band | reference only; the TX cap still governs |
| `edca_mpdu_th_no_intf_tcp`, `edca_mpdu_th_no_intf_udp` | `200`, `500` | EDCA/aggregation MPDU thresholds when no interference is detected | airtime/QoS only; does not exempt TX from the power cap |

---

## External constants (standard channelisation, NOT from the inputs)

These are the only **external standard** numbers used; every other number in this document is
verbatim from an input, taken from the tuple list, or an inline derived count/conversion whose
arithmetic is shown at its point of use. These frequencies are used only to explain why a
channel falls inside or outside a regdomain range, and are flagged here so no number is uncited.

- channel 14 centre: 2484 MHz
- channel 144 centre: 5720 MHz
- 2.4 GHz 1-13 centres span 2412-2472 MHz
- 5 GHz 20 MHz centres: 5180-5240 (36-48), 5260-5320 (52-64), 5500-5700 (100-140),
  5745-5825 (149-165); 184-196 band: 4910-4980 MHz

## Open items

1. The leading `00` byte in every `pwr_limit_*` value (H-a1) and the field-4 zero in every
   regdomain BAND row are both undetermined from this file alone.
2. The tuple list has 26 entries, not the 27 stated; one is missing.
3. The byte-3 tag semantics in H-c1 and the `0xff` sentinel need the `hi5622v100_wifi.ko`
   handler to confirm.
