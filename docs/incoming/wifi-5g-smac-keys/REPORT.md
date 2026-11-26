# REPORT - lane wifi-5g-smac-keys (task-223)

**Host-only**: static analysis of the pinned local firmware image and the local unstripped vendor
modules. No device contact, no build, no fire, no vendor bytes committed. All firmware addresses are
**CA = file offset + 0x40000** of FIRMWARE.bin (928,920 B, sha256 `7fc87e20...311b`). Grades as in
TABLES.md; [V] items are re-derived by `tools/fw_5g_smac_keys_re.py --selftest` (**129/129 PASS**).

**Inputs (local only, never copied here):** `recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`;
`recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko` (unstripped, 3,581,748 B, sha256
`22fa789a...c7d2`); the committed lanes `../wifi-smac-start`, `../wifi-tbtt-timer`,
`../wifi-f1-from-capture`, `../wifi-band1-start-send-5`, and the openwrt-lab ARM objdump.

---

## 0. TL;DR

1. **The dispatcher is now fully decoded [V]:** g0-id1 config events dispatch on the u16 at
   hcc-hdr **+20** against two `{u16 key; u16 pad; u32 worker|1}` tables (CA 0x10C23C, 217 rows;
   CA 0x103BF8, 88 rows) at CA 0x43D58; the handler gets `r2 = hdr+24` (payload) and
   `r1 = hdr[22]` (selector). That closes the "where does a g0-id1 key land" question for all 25
   stock keys and the 9 ladder keys (TABLES T2/T3).
2. **All 20 stock-only handlers are decoded** (TABLES T2): they are VAP/channel/regulatory/MIB/
   security/DFS **config syncs**, not a per-band SMAC bring-up. None inlines a write of the band
   object's +672 (SMAC device-chain A base), none loads the attached RAM block, none calls the boot
   SMAC init (single caller CA 0x48314). The only SMAC-touching ones are 0x0000 (per-chain MAC
   registers in chain A), 0x07E7 (band-1 SMAC sub-base 0x4010E000 for DFS CAC), and indirectly
   0x0073/0x0069 (VAP state/mode syncs that can reach the band-start CA 0x971D8).
3. **The +0xE4 "device-chain status" word is a symptom, not a setpoint [V]:** the working band's
   ctx pointer is `[band_object+672]` = chain A base (0x40042000); the word is written only by five
   SMAC ISR bodies (mask/ack, 0x80, ...) and 0x00A00096 does not exist as an image constant. Its
   high bits are hardware liveness, so band1 = 0 means the band-1 chain never produced/acked
   activity - it does not identify a host key.
4. **The per-band attach is boot-only and runs for both bands [V]:** the 0xD1B68 loop writes chain A
   base 0x40042000 / 0x40062000 to +672 (band selector [obj+0x11D]; both branches converge on the
   same store sites) and the RAM block 0x114F18 (u16 table) to +640 for both bands. "Band1 never
   advanced to the attached SMAC context" can therefore only be a post-boot difference *on band0*,
   or a probe-offset error - the record itself has working band0 +672 = the +640 value (R0 settles
   it; FIRE-RECIPE).
5. **Candidate ranking (TABLES T8):** none of the 20 keys *is* the activation; by plausibility of
   fixing band-1 chain liveness the only candidates are the VAP state/mode syncs the stock sends
   and our ladder omits - **0x0073 (1-B state)**, **0x0069 (4-B mode)**, then **0x0000 (6-B MAC,
   the only chain-register writer)** and **0x07E8 (1-B band, per-channel TX-cali sync, 17 stock
   sends)**. The fire recipe tests them one rung per fresh boot, premise probes first.

---

## 1. Method

1. Re-derive the config-event dispatcher and its two tables (literal scan for CA 0x10C23C /
   0x103BF8; row decode; call-site checks). `--selftest` asserts the prologue, the hdr+20 key load,
   both table literals, both byte bounds, the NULL-worker reject, the payload pointer and the
   indirect call.
2. Decode key -> worker for the 25 stock keys (committed FAMILY-TABLE) and the 9 ladder keys, and
   pin 20 representative rows by index (TABLES T1/T2).
3. Recursive-descent each of the 20 handler bodies (Thumb, bounded from the table worker) and
   extract calls, literal device/RAM addresses and store offsets; classify the effect.
4. Cross-map to the vendor host emitters by finding the `hmac_config_send_event` /
   `hdpp_config_send_event` call sites whose `r1` is loaded with the key (`wifi.dis.txt` of the
   local unstripped module), giving the emitter name and the body geometry (TABLES T2/T8).
5. Re-derive the per-band object + SMAC device-chain model: wrapper CA 0x180814, band accessor
   CA 0xF4F48, boot init CA 0xD1B68 (caller CA 0x48314), the +608/+640 store groups, and the five
   ISR writers of [chainbase+0xE4].
6. Rank candidates against the bench symptom (band-1 chain status 0) and write the read-only
   premise probes + ordered candidate rungs (FIRE-RECIPE).

## 2. The dispatcher (new, complete) [V]

`CA 0x43D58` is an HCC message callback (no direct BL caller - installed through a function
pointer). It fetches the hcc header, reads `u16 hdr[20]` as the config key, walks table A
(CA 0x10C23C, 217 rows, bound 1736 B) then table B (CA 0x103BF8, 88 rows, bound 704 B), matches
the key, takes `row[+4]` as the worker (Thumb bit set; NULL is rejected and logged), and calls
`worker(hcc_ctx, hdr[22], hdr+24)`. Both tables end with a sentinel row `{key 0x1451; 0; NULL}`.

This mapping reproduces every stock-only dispatch the record lists (e.g. 0x01CF -> 0x84D14,
0x03E4 -> 0x84260, 0x0073 -> 0x84920, 0x0000 -> 0x840B4) and the ladder keys
(0x0065 -> 0x46394, 0x0127 -> 0x84DA4, 0x0242 -> 0x4C524, ...). It also explains the absence of a
handler for keys outside the tables (no host key resolves to the timer writers / SMAC init /
accessor, matching the prior lane's negative).

## 3. The 20 stock-only keys (TABLES T2)

Grouped by what the firmware handler does:

* **MAC/chain:** `0x0000` set_mac_addr - copies the 6-B MAC into the chip MAC struct, posts a
  22-B per-chip message (CA 0xF4744) in one mode, then calls CA 0xBD038 and CA 0xBC830. Those two
  functions resolve the band object through CA 0xF4F48, read `[obj+672]` (chain A base) and write
  the 16 per-chain MAC slots at `chain + idx*0x80 + {0xEC,0xF0}` and `{0xF4,0xF8}`. This is the
  only stock-only key that touches the SMAC device chain directly.
* **VAP state/mode:** `0x0073` vap_state_syn (1 B state; state 4 calls band start CA 0x971D8 but
  only on the `[ctx+4]==1` branch; state 7 -> CA 0x97008; state 1 -> CA 0x4F214/0x4E4DC),
  `0x0069` set_mode_event (4 B: state + two mode bytes; drives VAP FSM CA 0xD4048 and, for state>4,
  the big start path), `0x03E4` down_vap/channel-follow, `0x0067`-adjacent behaviour.
* **Channel/BW/MIB:** `0x0052` set_freq, `0x01CF` set_mib_by_bw, `0x015D` 40M-intolerance,
  `0x07D9` channel-status sync, `0x07E8` per-channel TX-cali sync (1 B band; calls CA 0xBCDC4).
* **Regulatory/DFS/ACS:** `0x00F7` country, `0x011E` ACS, `0x07E7` DFS CAC (the only stock-only
  handler writing the band-1 SMAC sub-base 0x4010E000 block).
* **Security/flags:** `0x0078` key install, `0x000B` U-APSD enable, `0x003C` short-preamble,
  `0x006F` HE GI, `0x02BD` 11v BSS transition, `0x025F` custom RF (12 insns, no device write),
  `0x0005`/`0x01EE` unresolved (small field-store handlers).

The **honest negative** [V]: no stock-only handler body contains a store to object offset +672, a
literal 0x114F18/0x40042000/0x40062000/0x180814, or a BL to the boot SMAC init CA 0xD1B68. The
20 keys are not the per-band attach.

## 4. The SMAC device chain and the 0x00A00096 word

### 4.1 Model (TABLES T5) [V]

* Wrapper CA 0x180814: +8 = band0 object, +0x1C = band1 object (14 ISR wrappers load it; nothing
  else does, and no code stores it - the object pointers are runtime RAM values; the bench read is
  the reference).
* Band accessor CA 0xF4F48: object = 0x174BF0 + 0xE28*band.
* Boot init CA 0xD1B68 (single caller CA 0x48314) memsets the 7,272-B band region, loops band
  instances (r4 = obj+0x40, base 0x174C30, stride 0xE28), and per band writes:
  * **+672 = chain A base** (band0 0x40042000, band1 0x40062000; literals 0xD20F4/0xD20F8;
    selector [obj+0x11D]; both branches converge on stores 0xD2046..0xD207E at +608..+636);
  * **+640 = RAM block 0x114F18** and the rest of the attached block set (+576..+600).
* The ISR common bodies read the context pointer from **[obj+672]** and then use device registers
  at **ctx+228 / ctx+232** (= chain A base + 0xE4 / +0xE8). So those two are *device* words, not
  RAM fields.

### 4.2 The status word 0x00A00096 [V addresses, U bit assignment]

Five ISR stores write [ctx+228]: CA 0xAE474 (writes `(([obj+700] & 0x01000200) & ~[ctx+232])`),
0xAEAD8, 0xAEBB0 (writes 0x80), 0xB53F8, 0xB5420. A full-image scan finds **no 0x00A00096
constant** and no other path can build it from the ISR masks. The live value is therefore
**hardware liveness bits + the firmware mask/ack bits**: band0 = 0x00A00096 shows an active/acked
chain, band1 = 0 shows a chain that never ran the ISR/mask path. This is a *symptom* - equal to the
prior lane's finding that the timer words are equal on both bands. It cannot be used to pick a
host key by itself.

### 4.3 The attach asymmetry question (R0)

The record's bench datum has the *working* band0 object +672 = 0x114F18 - which is exactly the
value the boot init writes at **+640**, not +672; band1 +672 = 0x40062000 matches its boot value.
Two readings fit the same numbers:

* **H1 (probe slip):** band0 was read at +640, band1 at +672. Then the firmware model is uniform
  for both bands at boot and the whole "band1 never advanced" premise needs a different
  discriminator (the +0xE4 device word / arm register), not the 20 keys.
* **H2 (post-boot overwrite):** band0's +672 really became the RAM block. Then some *band-0*
  bring-up path copies/overwrites the context pointer - and the writer is not in the 20 keys
  (no such store anywhere in their bodies, no literal 0x114F18 store to +672 in the image).

R0 of the fire recipe settles it by reading **both** offsets on **both** bands in the same boot.
The recipe below is deliberately gated on R0.

## 5. Candidate ranking and the fire plan [V decode, I ranking]

The stock sends all 20 keys during its boot capture (TABLES T7; e.g. 0x0000 x2, 0x0073 x4,
0x0069 x7, 0x07E8 x17). Our ladder sends only 9 keys and omits all four candidates. By *reach into
the band-1 SMAC path*:

1. **0x0073 vap_state_syn** - 1 B state; the only stock-only handler that calls the band start
   CA 0x971D8. If the 5 GHz VAP's firmware FSM never receives its state sync, the per-band start
   path may never run for that VAP. **Rung R2** posts state 4 (then 7 in R2b if needed).
2. **0x0069 set_mode_event** - 4 B; drives the VAP FSM and the large start path. **Rung R3.**
3. **0x0000 set_mac_addr** - 6 B MAC; the only key that writes chain-A registers. **Rung R4**
   (per-VAP MAC at VAP-add time).
4. **0x07E8 chan_tx_cali_sync** - 1 B band; per-channel TX cali sync, stock sends it at
   `hmac_chan_start_bss`. **Rung R5.**

Not candidates: 0x07E7 (DFS CAC; only relevant with radar CAC on the same channel), 0x01CF/0x015D/
0x0052/0x07D9 (channel/BW/MIB sync, no chain access), 0x0078/0x00F7/0x011E (security/regulatory/ACS),
0x0005/0x01EE [U].

## 6. Negative results

* No stock-only handler calls/implements the per-band SMAC init (CA 0xD1B68 has exactly one caller,
  CA 0x48314) or the RAM attach.
* No stock-only handler writes [obj+672] or [obj+640]; no handler loads 0x114F18.
* The 0x00A00096 word is not a firmware constant and only five ISR stores ever write [ctx+228];
  band1 = 0 is therefore consistent with "chain never produced an ISR", not with a missing
  "set-the-status" host key.
* Keys 0x0005 and 0x01EE have no resolved vendor emitter name in the local module scan (their
  send-sites are indirect/wrapped); their handlers are small field stores and are not candidates.
* 0x025F set_cus_rf is only 12 instructions (copy + log) - not an RF-programming key; the 5 GHz RF
  image is host-programmed (committed record), so it is not the band-1 chain activation either.

## 7. Open items [U]

1. The **+640 vs +672** probe ambiguity (R0) - settles H1/H2.
2. Whether the band-1 chain A base is a *clock/reset-gated* device block (then the trigger may be
   a host MMIO write to the band-1 arm register, not a g0-id1 key): P3/P4/P5 in TABLES T6 measure it.
3. Exact word values/bit assignment of [chainbase+0xE4]/[+0xE8] on band0 vs band1 in the same boot
   (P3/P4).
4. Whether 0x0073/0x0069 bodies must match the stock byte values; the recipe uses the decoded
   field semantics (state/mode) with one value per rung.

## 8. Reproduce / verify

    .provision-venv/bin/python tools/fw_5g_smac_keys_re.py /path/to/FIRMWARE.bin --selftest
    .provision-venv/bin/python tools/fw_5g_smac_keys_re.py /path/to/FIRMWARE.bin --dump

Evidence: [evidence/selftest.txt](evidence/selftest.txt) (129/129 PASS), [evidence/decode.txt](evidence/decode.txt)
(29-key table, family cluster, ISR writers, chain-base values). Nothing vendor is committed; only the
addresses/ids/lengths above.
