# TABLES - lane wifi-5g-smac-keys (task-223)

Firmware: 928,920 B sha256 `7fc87e20...311b`, runtime **CA = file offset + 0x40000**.
Grades: **[V]** re-derived by `tools/fw_5g_smac_keys_re.py` from the image; **[V\*]** committed
record/local vendor module (not re-derived here); **[M]** bench-measured earlier; **[I]** inference;
**[U]** needs a device read. Host probe paths: device IO phys = **CA + 0x3B8000**; firmware RAM
phys = **0x40000000 + CA + 0x6B8000**. Never write CA 0x400392F0; never read RC misc window.

## T1. The g0-id1 config dispatcher (key -> worker) [V]

| item | value |
| --- | --- |
| dispatcher | CA **0x43D58** (`push {r4,r5,r6,lr}`; HCC message callback, no direct BL caller) |
| key | `ldrh r2,[r4,#20]` = **u16 hcc-hdr + 20** |
| table A | CA **0x10C23C**, 217 rows, end bound `cmp r3,#1736` (0x6C8 = 217*8) |
| table B | CA **0x103BF8**, 88 rows, end bound `cmp r3,#704` (0x2C0 = 88*8) |
| row | `{u16 key; u16 pad; u32 worker|1}`; match -> `ldr r3,[r1,#4]`; NULL worker rejected |
| handler args | `r2 = hdr+24` (payload), `r1 = hdr[22]` (per-VAP/config selector byte), indirect `blx r3` |
| sentinel | both tables end with `{key 0x1451; 0; NULL}` (never a live handler) |

Key->worker is at **T2** (20 stock-only) and **T3** (the 9 ladder keys). `--dump` prints all 29.

## T2. The 20 stock-only g0-id1 keys decoded [V firmware / V\* vendor emitter, body length]

| key | worker CA | vendor emitter (local `hi5622v100_wifi.ko`) | host body | firmware handler effect |
| ---: | ---: | --- | ---: | --- |
| 0x0000 | 0x840B4 | `hmac_config_set_mac_addr` | 6 B MAC | copies MAC into `[chip+400]`; if `[chip+1001]==2` posts 22-B per-chip msg CA 0xF4744; then **CA 0xBD038** and **CA 0xBC830** write the per-chain MAC registers of the SMAC device chain A (`[obj+672] + idx*0x80 + 0xEC/0xF0/0xF4/0xF8`, idx 0..15) |
| 0x0005 | 0x82878 | [U] | [U] | 26 insns: validate/log + one byte store; role [U] |
| 0x000B | 0xA09F0 | `hmac_config_set_uapsden` | [U] | writes chip flag `[ctx+396]` and RAM word 0x117B80; U-APSD enable |
| 0x003C | 0x811B8 | `hmac_config_set_shpreamble` | [U] | writes `[ctx+836]`; short-preamble flag |
| 0x0052 | 0x42044 | `hmac_config_set_freq` / `hmac_config_priv_iter_vap_set_channel` | [U] | channel/frequency update: writes `[ctx+16]`/`[ctx+19]`; calls 0x10172C (channel check) and 0x40684 |
| 0x0069 | 0x839B4 | `hmac_set_mode_event` | 4 B: `{state, byte17, byte18}` | VAP mode sync: payload -> `ctx+15/+17/+18/+29`; calls VAP FSM CA 0xD4048; for state>4 runs the large start path (25 calls incl. 0x7D718, 0x5FA6C, 0xA4DB0, 0xEA664, 0xBCEB4, 0xB2168, 0xF5C54, 0x40684) |
| 0x006F | 0x81C44 | `hmac_config_set_he_gi` | [U] | writes `ctx+2097`, `ctx+423`, `ctx+424`; HE guard interval |
| 0x0073 | 0x84920 | `hmac_config_vap_state_syn` | **1 B: new VAP FSM state** | stores state `ctx+14`; **state 4 calls band start CA 0x971D8 but only on the `[ctx+4]==1` branch**; state 7 calls 0x97008; state 1 calls 0x4F214/0x4E4DC; posts per-chip msg CA 0xB1E60 |
| 0x0078 | 0xA5824 | `hmac_config_11i_add_key` / `hmac_install_rekey` | key blob | MAC security key install state machine (409 insns); not bring-up |
| 0x00F7 | 0x45A30 | `hmac_config_set_country` | [U] | regulatory country tables; writes `ctx+3776` |
| 0x011E | 0xA1A58 | `hmac_config_acs` | [U] | ACS result/timer (0xFDDD8, 0x4BCFE) |
| 0x015D | 0xB2410 | `hmac_40M_intol_sync_event` | [U] | 40 MHz-intolerance sync; writes `ctx+1324`, bandobj+564; calls 0xB23CC/0xB23A4, 0x63988 |
| 0x01CF | 0x84D14 | `hmac_config_set_mib_by_bw` | [U] | per-bandwidth MIB fields `ctx+844`/`ctx+864` (0xFDDF8/0xFDB30) |
| 0x01EE | 0x4213C | [U] | [U] | 29 insns; writes `ctx+1238`; role [U] |
| 0x025F | 0x82850 | `hmac_config_set_cus_rf` | [U] | 12 insns: custom-RF parameter copy + log; no device write in the body |
| 0x02BD | 0x438F0 | `hmac_11v_cfg_bsst_switch` | [U] | writes `ctx+348`; 802.11v BSS-transition config |
| 0x03E4 | 0x84260 | `hmac_config_down_vap` / `hmac_check_ap_channel_follow_sta` | [U] | channel-follow / down-VAP: writes `ctx+16..19`; calls 0x10172C, 0xFDCE4 |
| 0x07D9 | 0x4366C | `hmac_config_ch_status_sync` | [U] | channel-status sync; 27 insns, no device/SMAC write |
| 0x07E7 | 0xB58FC | `hmac_dfs_cac_start/stop` / `cac_sync_state` / `cac_timeout_fn` | [U] | DFS CAC; **the only stock-only key whose handler writes the band-1 SMAC sub-base**: 0x4010E000/+4/+8, +0xA0/+0xA4/+0xA8 (226 insns) |
| 0x07E8 | 0x429B0 | `hmac_chan_tx_cali_sync` | **1 B: band value** | writes `ctx+29`; calls CA 0xBCDC4; the stock post site is `hmac_chan_start_bss` / channel switch / DFS (17 sends in the 104 s capture) |

None of the 20 handler bodies inlines a write of object offset **+672** or a load of 0x114F18 /
0x40042000 / 0x40062000 / 0x180814 (tool check). The per-band chain/attach fields are boot-only.

## T3. The 9 keys our documented ladder sends (diff context) [V / V\* vendor]

| key | worker CA | vendor emitter | note |
| ---: | ---: | --- | --- |
| 0x0064 | 0x449E4 | `hmac_config_add_vap_sync_all` | VAP add |
| 0x0065 | 0x46394 | `hmac_config_start_vap_event` | VAP start (creates beacon state, calls band start) |
| 0x0067 | 0x45F74 | `hmac_config_down_vap` | VAP down |
| 0x00D8 | 0x81038 | `hmac_config_mib_amsdu_aggregate_ative` | MIB/AMSDU |
| 0x0124 | 0x841B8 | `hmac_config_set_channel` / `hmac_chan_follow_reset_channel` | channel |
| 0x0127 | 0x84DA4 | `hmac_config_set_beacon` | beacon staging |
| 0x01F7 | 0xB5B9C | `hmac_config_set_country_for_dfs` | country/DFS |
| 0x0242 | 0x4C524 | `wal_config_set_green_ap` | the "band start" frame (calls CA 0x971D8) |
| 0x0243 | 0x8419C | `wal_config_recently_cfg_event` | recent-config ack |

## T4. The 0x84xxx hmac-config family (handler cluster CA 0x82850..0x84DA4) [V]

Workers of the family in the two tables: 0x840B4 (0x0000 set_mac_addr), 0x82878 (0x0005),
0x839B4 (0x0069 set_mode), 0x84920 (0x0073 vap_state_syn), 0x84D14 (0x01CF mib_by_bw),
0x84260 (0x03E4 down_vap/chan_follow), plus non-stock keys 0x844DC, 0x848CC etc. They share the
same shape: `handler(chip_ctx, selector_byte, payload)` -> validate/log -> update one chip/VAP field
or post one per-chip/per-band message. **Only 0x0000 reaches the SMAC device chain** (T2).

## T5. Per-band object + SMAC device chain model [V]

| item | band0 | band1 | source |
| --- | --- | --- | --- |
| wrapper RAM var | CA 0x180814: +8 = band0 object, +0x1C = band1 object (M: 0x174BF0 / 0x175A18) | | `ldr r3,=0x180814; ldr r0,[r3,#8]` wrappers |
| band accessor | CA 0xF4F48: `0x174BF0 + 0xE28*band` | | [V] |
| object field +672 | SMAC device chain A base: **0x40042000** | **0x40062000** | boot init 0xD1B68 loop, literals 0xD20F4/0xD20F8, stores 0xD2046..0xD207E (+608..+636) |
| object field +640 | RAM block **0x114F18** (u16 table) | **same 0x114F18** | attach stores 0xD2CA2..0xD2CC4 (+576..+600) |
| chain A access | ISR body: `r2=[obj+672]`; reads/writes `[r2+228]/[r2+232]` = device regs base+0xE4/+0xE8 | | [V] |
| status word | `[band0 chain A+0xE4]` = 0x00A00096 (M) | `[band1 chain A+0xE4]` = 0 (M) | Lead 2026-11-26 |
| status word writers | 5 SMAC ISR stores: 0xAE474 (mask/ack), 0xAEAD8, 0xAEBB0 (128), 0xB53F8, 0xB5420 | | [V] |
| 0xA00096 as constant | absent from the image | | [V] |
| per-band loop selector | `[obj+0x11D]` byte (set at 0xD1D74 from the band byte); 0 -> 0xD2044 (band0 base), 1 -> 0xD2094 (band1 base), both -> shared store sites | | [V] |

Consequence: the boot init writes the chain-A base for **both** bands, and the RAM-block attach for
**both** bands. The 0x00A00096-vs-0 divergence can therefore only come from post-boot activity of
the band-0 chain (IRQ/liveness bits in the device word), or from a probe-offset error (the record
also has the working band's obj+672 = the *obj+640* value 0x114F18 - see FIRE-RECIPE R0).

## T6. Probe pack (read-only; physical addresses) [V geometry]

| # | object | CA | width | phys | expected |
| ---: | --- | ---: | ---: | ---: | --- |
| P0 | SMAC wrapper +8 / +0x1C (runtime object pointers) | 0x18081C / 0x180830 | 4 | 0x4083881C / 0x40838830 | 0x174BF0 / 0x175A18 (or the runtime values to use below) |
| P1 | band0 obj **+640** / **+608** / **+672** | 0x174E70 / 0x174E5C / 0x174E90 | 4 | 0x4082CE70 / 0x4082CE5C / 0x4082CE90 | 0x114F18 / ? / 0x40042000 |
| P2 | band1 obj **+640** / **+608** / **+672** | 0x175C98 / 0x175C84 / 0x175CB8 | 4 | 0x4082DC98 / 0x4082DC84 / 0x4082DCB8 | 0x114F18 / ? / 0x40062000 |
| P3 | band0 chain A +0xE4 / +0xE8 / +0x00 | 0x400420E4 / 0x400420E8 / 0x40042000 | 4 | 0x403DA0E4 / 0x403DA0E8 / 0x403DA000 | M: 0x00A00096 / 0 / - |
| P4 | band1 chain A +0xE4 / +0xE8 / +0x00 | 0x400620E4 / 0x400620E8 / 0x40062000 | 4 | 0x403FA0E4 / 0x403FA0E8 / 0x403FA000 | M: 0 / 0 / - |
| P5 | band1 SMAC arm/IRQ word (send-5 P1) | 0x4010C220 | 4 | 0x404C4220 | 0x1029 on a started band-1 |
| P6 | chain A per-chain MAC reg (idx0) chain0/1 | 0x400420EC/0x400420F4 and 0x400620EC/0x400620F4 | 4 | 0x403DA0EC / 0x403DA0F4 / 0x403FA0EC / 0x403FA0F4 | nonzero after 0x0000 posts |
| P7 | RAM block 0x114F18 head (+228 u16 pair) | 0x114F18 / 0x114FFC | 4 | 0x407DCF18 / 0x407DCFFC | [U] |

If a runtime pointer from P0 differs from 0x174BF0/0x175A18, recompute P1/P2 by adding the same
offsets (0x260/0x280/0x2A0) to the *runtime* pointer - do not reuse the literals.

## T7. How often the stock sends each stock-only key (104 s boot capture) [V\*]

| key | n | key | n | key | n | key | n |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0x0000 | 2 | 0x0005 | 5 | 0x000B | 57 | 0x003C | 1 |
| 0x0052 | 5 | 0x0069 | 7 | 0x006F | 1 | 0x0073 | 4 |
| 0x0078 | 1 | 0x00F7 | 1 | 0x011E | 1 | 0x015D | 3 |
| 0x01CF | 3 | 0x01EE | 1 | 0x025F | 1 | 0x02BD | 1 |
| 0x03E4 | 1 | 0x07D9 | 7 | 0x07E7 | 3 | 0x07E8 | 17 |

Source: ../wifi-f1-from-capture/evidence/FAMILY-TABLE.txt. The two per-VAP keys (`0x0000` n=2,
`0x0073` n=4) and the per-channel TX-cali sync (0x07E8 n=17) were delivered **by the stock and are
absent from our ladder**.

## T8. Candidate ranking (host-visible body + stock order context) [V decode / I ranking]

| rank | key | body | stock call-site context (vendor) | why it can matter for band-1 chain liveness |
| ---: | ---: | ---: | --- | --- |
| 1 | **0x0073** vap_state_syn | **1 B: state** | `hmac_fsm_change_state` -> `hmac_config_vap_state_syn(vap,1,&state)` on every VAP state change | the only stock-only key whose handler reaches the band-start CA 0x971D8; our ladder starts the VAP without ever telling the firmware the VAP state |
| 2 | **0x0069** set_mode_event | **4 B: {mode, b17, b18}** | `hmac_config_set_mode` | drives the firmware VAP FSM (0xD4048) and, for state>4, the full band start path; our ladder never posts it |
| 3 | **0x0000** set_mac_addr | **6 B MAC** | `hmac_config_add_vap_mac_addr` (VAP add), `wal_config_set_mac_addr` | the only stock-only handler that writes SMAC device-chain-A registers (per-chain MAC regs); stock sends it once per VAP |
| 4 | **0x07E8** chan_tx_cali_sync | **1 B: band** | `hmac_chan_start_bss` (also channel switch/DFS) | per-channel TX calibration sync; 17 sends in the stock boot; 5 GHz channel bring-up normally includes it |
| - | 0x07E7 dfs_cac | - | DFS only | writes the band-1 SMAC sub-base but only for radar CAC; irrelevant for a non-DFS ch40 AP |
| - | 0x01CF/0x015D/0x07D9/0x0052/... | - | channel/BW/MIB/regulatory | no SMAC device-chain access; not activation candidates |

Body layouts are [V] firmware-side (payload fields the handler consumes) and [V\*] vendor-side
(what the emitter writes); the exact byte values for 0x0069 are host VAP-struct state, so the
recipe fires them as literal rungs one at a time.
