# REPORT - the G9/G11 gate semantics: 0x440/0xEC8 are the RF-enable flags (1 = normal)

**Lane:** wifi-vap-start-gates (shared task **task-212**), 2026-11-28.
**HOST-ONLY:** no device contact, no fire; the Lead fires.
**Deliverable:** the decode of the key-0x0065 worker CA 0x46394, the
0x440/0xEC8 write/read semantics, the stock start_vap order, the corrected
G1..G14 gate set and the corrected fire recipe.

## 1. Verdict first [V]

The R4b fire's G9 refusal is a **host-side gate bug**, not a firmware fact:

1. the driver's `G9` required `u8[mctx+0x440] == 0` and `G11`
   `u8[bandobj+0xEC8] == 0` (see ../wifi-band1-start-send-4/TABLES.md T4
   and the `vap_start_g0 REFUSED (-16)` dmesg);
2. the firmware readers use the **opposite** polarity: **CA 0x944DE returns 1
   only when `mctx[0x11]==1 && mctx[0x440]==1 && bandobj[0xEC8]==1`**, and
   that 1 is the enable for the 5 GHz RF programme (CA 0x40684 -> CA 0x101890);
3. the key-0x0065 worker itself (CA 0x46394) **never reads either byte** -
   the range scan of 0x46394..0x467C8 finds no access to +0x440 or +0xEC8;
4. the R4b fire's own probes are the decisive evidence: the RF image
   `R+0x3AC = 0x03E6138A` (the ch36-64 image) was written, which happens only
   inside CA 0xA2FA2, which is reached only if CA 0x40684 passed, which needs
   CA 0x944DE == 1, which needs **both bytes == 1 at ap_chan time**. So at the
   G9 read (after ap_chan) `mctx[0x440] == 1` is the expected, normal state.
   The CW-2 working-state probe (`0x440=1, 0xEC8=1`) and the desc-pool
   expected pack (2,1,1) agree.

**Corrected polarity: G9' `u8[mctx+0x440] == 1`, G11'
`u8[bandobj+0xEC8] == 1`** (refuse on 0: the RF programme would be skipped
and no 5 GHz beacon can air). Nothing else in the gate set becomes looser:
two hard gates stay hard, their comparison is inverted.

## 2. The key-0x0065 worker CA 0x46394 [V]

Row 2 of the g0-id1 table A (CA 0x10C23C, 217 x 8 B) is
`{key 0x0065, worker 0x46395}` (Thumb bit) -> entry **CA 0x46394**, function
range 0x46394..0x467C8. Signature: `(mctx, ?, body)` = (r0, r1, r2) as the
dispatcher hands it.

Entry preconditions (the only ones the worker itself refuses on):

| # | CA | check | on failure |
| ---: | ---: | --- | --- |
| W1 | 0x463AA-0x463B4 | `CA 0x961AE(mctx[0])` (owner/user-id -> module ctx) != 0 | log 0x517, return **100** |
| W2 | 0x463D4-0x463DC | `mctx[0x4B8]` (band object) != 0 | log 0x51E, return **100** |

Everything else is applied work, not a gate:

| CA | action |
| ---: | --- |
| 0x463EC-0x463F8 | body[2] -> mctx[0x11], body[3] -> mctx[0x12], body[4..7] -> u32 mctx[0x444] |
| 0x463FC | CA 0xD4048(mctx, body[1]) |
| 0x46404 | CA 0x94518(mctx) (MAC VAP start) |
| 0x46408-0x4641A | type mctx[4]==2 -> AP branch: CA 0xBD114(mctx, mctx[0x190]); **movs r3,#1; strb r3,[mctx+0x0E]**. Type 1 -> mctx[0x0E]=4; other types -> no store. This is G12's 0->1. |
| 0x4641C-0x46430 | CA 0xD41AC(mctx); `[bandobj+0x1A00]` published is read but only logged when 0 (no refusal) |
| 0x46434-0x4644A | CA 0xF593C(mctx[0x4AC], type); published != 0 -> bandobj[0x13BC]=0xFF |
| 0x46450-0x4659A | per-link = mctx[0x4B0]; if !=0 and perlink[0x6C6]!=1: alloc 280 B + 92 B, set +0x6C4=16, +0x6C0, +0x6B8, +0x6C6=1 (beacon state) |
| 0x46460-0x46480 | AP: `vap = mctx[0x190]`; **deref `[vap+0x58]` at 0x46470** (fault if mctx[0x190]==0, G5); bit2 <- band field |
| 0x46482-0x4648A | mctx[0x0F]==10 -> CA 0xFDCE4(mctx) (VAP band-class) |
| 0x4648E-0x464B0 | if mctx[0x4A0] != 0: CA 0x5E95C(module), **CA 0x971D8(bandobj, mctx)** (band start/hook) |
| 0x464B4-0x467C6 | CA 0x5F450(module); if 0: CA 0x947E4, CA 0x8E51A, CA 0x4E4DC(module,19,0), **CA 0x4C3FC(bandobj, subcmd=1)** (the 0x0242 pre-start), log, return 0; if nonzero: error log, return that code |

**What the worker does NOT test:** mctx[0x440], bandobj[0xEC8], the band VAP
list count (bandobj+0x12), bandobj[0xD6E], the state struct, the channel word.
The range scan is in `tools/fw_vap_start_gates_re.py` / `evidence/decode.txt`
(`worker NEVER reads mctx[0x440] or bandobj[0xEC8]: PASS`). Those conditions
belong to the later 0x0242 path (the worker's own tail call CA 0x4C3FC ->
CA 0x4AE7C resolver tests count==1 and the CA 0x944DE gate; CA 0x4C3FC's
first guard is `bandobj[0x14D0] bit0` = the 0x0242 arm bit, so the worker's
internal call is normally a benign log-and-return until the 0x0242 runs).

**Consequence for the ladder:** after a successful 0x0065, `mctx[0x0E]=1`
(after the G5 wait) is the only postcondition the worker owes; the pre-start
observables state[4]/state[1] move only when the 0x0242 step runs. The R4b
recipe's tolerant `state4=0x00/0xFF` post line is therefore correct.

## 3. The 0x440 / 0xEC8 semantics [V for readers/writers, I for names]

**Readers (polarity is unambiguous):**

* **CA 0x944DE** (decoded line by line):
  ```
  if (!mctx) return 0;
  bandobj = CA 0xBCDC4(mctx[0]);            // ctx ? [ctx+0x4B8] : 0
  if (!bandobj) return 0;
  if (u8[mctx+0x11] != 1) return 0;         // 0x94500
  if (u8[mctx+0x440] != 1) return 0;        // 0x94506
  return u8[bandobj+0xEC8];                 // 0x9450E
  ```
  So it returns **1 iff band==1 && +0x440==1 && +0xEC8==1**.
* **CA 0x40684** (the 0x0124 worker's 5 GHz branch, called at 0x8422E):
  `if (mctx[4]!=2) return; if (CA 0x944DE(mctx)!=1) return;` then
  CA 0x101890 / CA 0xA2FA2 (the RF register image) / CA 0x89840.
  Every early return is **silent**.
* **CA 0x4AE7C** (the band VAP resolver behind the 0x0242 pre-start):
  calls CA 0x944DE at 0x4AEB0 and CA 0x101890 at 0x4AEBE. The same
  1-gates are therefore also a precondition of the band start, not only of
  the RF programme. (This is why a boot with 0x440=0 can fail the band start
  even though it never reached the RF branch.)

**Writers (config-derived enables, not transient state):**

| writer | write | source |
| ---: | --- | --- |
| CA 0x455D0 | `strb mctx[0x440] = bandobj[0x19A1]` | band/config-apply block (source field +0x19A1 is a band-config byte; the neighbouring +0x19A0..+0x19A6 bytes feed the same block) |
| CA 0x7100C | `strb mctx[0x440..0x448] = block[0x30],[0x31],[0x2e],[0x2f],[0x29],[0x43]...` | a config/parameter block copied into the module ctx |
| CA 0x48D26 | `str.w bandobj[0xEC8] = 0x01000101` (literal at CA 0x48DFC) | band bring-up/init: bytes +0xEC8=1, +0xEC9=1, +0xECA=0, +0xECB=1; the same routine sets band flags +0x1548.. and +0x13BC=0xFF |
| CA 0xB6C44 | `strb bandobj[0xEC8] = parsed u8`; clears +0xEC9 when 0 | band up/down message path (CA 0xC1DC0 parses the body) |

Neither writer produces a "transaction in progress" value: the module byte is
a config copy, the band byte is an up/down status set to 1 by band bring-up.
1 = **enabled/up = normal**; 0 = **not configured/disabled = RF programme
silently skipped**. No writer ever sets 0 to mean "busy".

**Consistency with the earlier evidence:** CW-2 read `0x440=1, 0xEC8=1` in
a working state; the desc-pool lane's expected probe pack was (type, 0x440,
0xEC8) = (2, 1, 1); the fw-rf-programme lane decoded the 5 GHz programme as
silently gated on both == 1; the F2 fire proved the programme runs with
them == 1; the R4b fire proved both were 1 at ap_chan. The prestart lane's
"fresh boot = 0" line [I] was a pre-start speculation and is the only source
that ever supported the 0-gate; it is refuted by the R4b fire.

## 4. The stock order: start_vap AFTER the channel config [V]

**The emitter.** `hmac_config_start_vap_event` (.text+0x15AF88,
0xB8 bytes) builds the 12-byte key-0x0065 body
`{arg=1, vap[0x0F], vap[0x11] (band), vap[0x12] (field), u32 vap[0x414]}`
and sends it with key 0x0065 through `hmac_config_send_event`, then
`hmac_vap_sync_update`.

**Its callers.** Disassembly of the vendor module shows exactly four:

| caller | call site | role |
| --- | ---: | --- |
| `hmac_start_bss_in_available_channel` (0x48204) | **0x4826C** | the AP BSS start |
| `hmac_config_do_start_vap` (0x15C7A8) | 0x15C848 | the VAP start command path |
| `hmac_sta_update_join_req_params` | 0x536D0 | STA |
| `hmac_config_sta_update_rates` | 0x62804 | STA |

`hmac_start_bss_in_available_channel` is not called by the cfg80211 start_ap
handler: it is the tail of `hmac_chan_start_bss` (0x48358 -> tail-b
0x48204), which is called by `hmac_start_all_bss_of_band` (0x13FEDC) and by
the channel-switch/restore paths (`hmac_chan_multi_switch_to_new_channel`
0x4860C, the psta resume/join paths). I.e. the 0x0065 start_vap is the
**BSS-start consumer of an already-configured channel**, not something that
runs before it.

**The 0x0124 channel config.** The only key-0x0124 emitter is
`hmac_config_set_channel` (0x764E4; `mov r1,#292` at 0x76744 ->
`hmac_config_send_event` at 0x76748), reached from
`wal_config_set_channel` (0x12AAB0 -> call at 0x12AB14): a userspace cfg
command that configures the channel before the AP is started.

**The cfg80211 AP transaction.** `wal_cfg80211_start_ap` (0x10342C) ->
`wal_cfg80211_start_ap_write_msg` (0x103340) sends the 160-B AP config
cfg-event through `wal_send_cfg_event` (**0x10338C**), *then* calls
`wal_start_vap` (**0x10339C**) which sends the group-2 cmd 0x0065 frame
(`mov r3,#101`, 16 B, body[0]=1). The start_vap is the **last** step of the
AP-config transaction.

**Corroboration (bench-proven):** ../wifi-re-report/REPORT.md section 6.15
records the working five-message AP order that reached AP-ENABLED as:
**channel config 0x0124 FIRST -> SSID 0x0005 -> auth 0x006D -> ap_cfg
0x0127 -> VAP start 0x0065 after ap_cfg**. Both the host call graph and the
bench-proven order put the start_vap after the channel config.

**Consequence:** the R4b order (ap_chan before vap_start_g0) is the stock
order. The remaining order question - stock also puts the AP config before
the start_vap, while the 9-step plan puts `ap_ssid/ap_cfg` after
`band_start` - is a separate residual risk (section 6), not a gate issue:
the decoded worker does not read the fields those frames set and the beacon
interval VAP+0x13A is written by the 0x0127 worker (CA 0x84DA4) which can
still update the state after the start.

## 5. Corrected gate set

Only two comparisons change. G-numbering is the fired R4b code
(../wifi-band1-start-send-4/TABLES.md T4; source
`hi5622_vap_start_g0.c:hi5622_vap_start_g0_precheck`):

| id | probe | fired R4b | corrected | on failure |
| ---: | --- | --- | --- | --- |
| G1 | mctx != 0 | same | same | -ENODEV, no 0x0065 |
| G2 | bandobj != 0 && u8[bandobj+0x12] == 1 | same | same (0x0242 pre-start precondition, fail-early) | -ENOENT |
| G3 | u8[bandobj[0]] == vap_add_index | same | same (0x0242 resolver) | -EEXIST |
| G4 | u32[bandobj+0x1A00] != 0 | same | same (bind publish; the 0x0065 worker only logs it, the 0x0242 path needs it) | -EAGAIN |
| **G5** | **u32[mctx+0x190] != 0** | same | **same (fault gate, CA 0x46470)** | -EFAULT |
| G6 | u32[mctx+0x4AC] | warn only | same | warn |
| G7 | u8[mctx+4] == 2 | same | same (AP branch) | -EPROTO |
| G8 | u8[mctx+0x11] == 1 | same | same (0x0124 must have run) | -EINVAL |
| **G9** | u8[mctx+0x440] | **== 0** | **== 1** (RF-enable, 1 = normal) | **-EAGAIN** (RF programme skipped) |
| G10 | u8[bandobj+0xD6E] == 0 | same | same (CA 0x4C3FC/0x581C8 band-id gate; fail-early for the 0x0242) | -EINVAL |
| **G11** | u8[bandobj+0xEC8] | **== 0** | **== 1** (band up, 1 = normal) | **-EAGAIN** (band not up) |
| **G12** | u8[mctx+0x0E] == 1 after the post | same | same | band_start refused -EIO |
| G13 | state[0]==0 && state[1]==0 && state[4] in {0x00,0xFF} | same | same (fresh-band guard; the 0x4C3FC arm bit stays 0 until the 0x0242) | -EBUSY |
| G14 | u8[perlink+0x6C6] | observation | same | log |

The patch is `patches/0001-g9-g11-rf-enable-invert.patch` (module tree:
`hi5622_vap_start_g0.c/.h`); the host selftest update is
`patches/0002-gate-selftest-update.patch` against
../wifi-band1-start-send-4/src/vap_start_g0_selftest.c (its default probe
must start from b440=1/ec8=1 and the two refusal cases invert).

## 6. Corrected fire recipe (summary; full rung in FIRE-RECIPE.md)

Same R4b plan order and knobs, rebuilt module with the two inverted gates:
```
vap_add desc_table dfs_mode ap_chan vap_start_g0 band_start ap_ssid ap_cfg ap_vap_start
```
Expected: the `vap_start_g0 pre:` pack now reads **`ec8=1 ... b440=1`** and
the verdict reads "...(the G1..G13 gates are open; G9/G11 = the RF-programme
enable, 1 = normal)", the 0x0065 frame is posted/consumed, G12 shows
`mctx[0x0e]=1`, the 0x0242 band_start posts, state[4]=0x00 (state[1]=1 or 2),
P1 0x404C4220 = 0x1029, P12 stays 0x01800060/0x03E6138A (already programmed by
the R4b ap_chan), page golden, and the ch40 beacon appears at 5200 MHz.

## 7. Residual risks / honest unknowns

1. **The inverted gates are only as good as the reader decode.** CA 0x944DE
   was decoded line by line here and by ../wifi-fw-chan-worker; the writers
   and the R4b fire corroborate it. If a PASS fire still shows no beacon, the
   next single rung is NOT a gate relaxation.
2. **The 0x0065 worker's internal CA 0x4C3FC call no-ops before the 0x0242**
   (its first guard is the 0x0242 arm bit bandobj[0x14D0] bit0; the worker
   calls it with subcmd=1 without arming). The separate band_start step is
   still required; after the 0x0065 the state[4] is expected unchanged.
3. **Order nuance vs stock:** stock emits the g0 0x0065 after the AP config
   too, while the plan posts `ap_ssid/ap_cfg` after `band_start`. The worker
   does not read the SSID/beacon fields, and VAP+0x13A (beacon interval) is
   written by the 0x0127 worker CA 0x84DA4; if the PASS fire beacons with an
   empty/blank SSID or missing beacon, the next single rung is moving
   ap_ssid/ap_cfg before vap_start_g0 - not a gate change. [U]
4. **Which H2D key first sets mctx[0x440] on a fresh boot remains [I]**
   (writer callers CA 0x455D0 / CA 0x7100C; the R4b fire shows it is already 1
   after vap_add/desc_table/dfs_mode/ap_chan). The corrected G9' still fails
   closed if a boot has it 0.
5. The band-object bring-up function containing CA 0x48D26 (start 0x47628)
   has no static caller in the image (entered via a runtime/indirect path);
   its identity is [I] from the field map, the write itself is [V].

## 8. How to reproduce

```
opensource/docs/incoming/wifi-vap-start-gates/tools/fw_vap_start_gates_re.py --selftest -v
opensource/docs/incoming/wifi-vap-start-gates/tools/fw_vap_start_gates_re.py --decode
opensource/docs/incoming/wifi-vap-start-gates/tools/fw_vap_start_gates_re.py --tables
```
(needs the two pinned local artefacts and an ARM objdump; exits 0 on 39/39
checks). Evidence: `evidence/decode.txt`, `evidence/tables.txt`,
`evidence/selftest.txt`.

## 9. Sources

* this lane: firmware CA 0x46394 worker, CA 0x944DE, CA 0x40684, CA 0x4AE7C,
  CA 0x4C3FC, writers CA 0x455D0/0x7100C/0x48D26/0xB6C44; module
  hmac_config_start_vap_event, hmac_start_bss_in_available_channel,
  hmac_chan_start_bss, hmac_start_all_bss_of_band,
  wal_cfg80211_start_ap(_write_msg), wal_start_vap, hmac_config_set_channel,
  wal_config_set_channel.
* ../wifi-band1-start-send-4/ - the R4b fire, order patch, the G1..G14 set,
  the R4b refusal pack.
* ../wifi-band-start-prestart/ - the stock-order table, the 0x4C3FC/0x4AE7C
  decode, the 0x944DE gate.
* ../wifi-fw-chan-worker/ - the 0x0124 worker and the three silent RF gates.
* ../wifi-fw-rf-programme/ - CA 0x40684 -> 0x101890 and the RF image.
* ../wifi-fw-desc-pool/ - the CW-2 working-state probe (0x440=1, 0xEC8=1).
* ../wifi-re-report/ - the bench-proven five-message AP order.
* ../wifi-band1-start-send-2/ - the 0x0242 resolver/state[4] decode.
* ../wifi-fw-beacon-tx/ + ../wifi-fw-tbtt-arm/ - the worker's beacon-state tail.
