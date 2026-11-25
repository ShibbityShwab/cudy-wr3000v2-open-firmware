# TABLES - lane wifi-5ghz-beacon (task-175)

All vendor-module addresses are link-time `.text` offsets of a local,
unstripped `hi5622v100_wifi.ko`; the reproduction is `tools/beacon5g_re.py`
(55/55 on both pinned builds). Evidence grades: **[V]** re-derived here from
disassembly, **[V\*]** bench/pack-measured by a committed lane and cited,
**[I]** inference, **[U]** undecided without a device read.

---

## T1. Function address set (both vendor builds)

| function | 2.4.15 (`22fa789a...`) | 2.5.24 (`de78ec07...`) | role | probe |
| --- | ---: | ---: | --- | --- |
| `hmac_band_set_channel` | **0x13f49c** | 0x13fac4 | host: band+0xd29 = channel | A2 |
| `hmac_band_set_channel_and_bandwith` | **0x13f520** | 0x13fb48 | host: +0xd29, +0x10d2 = bandwidth | A3 |
| `hmac_band_set_band` | 0x13f460 | 0x13fa88 | host: +0xd28 = band type | A1 |
| `hmac_band_module_init` | 0x13f198 | 0x13f7c0 | zero `g_ast_band` (0x4660 = 2 x 0x2330), chip byte 0 | A/F |
| `hmac_band_get_by_id` | 0x13f0cc | 0x13f6f4 | `g_ast_band + 0x2330*band`, chip 0 only | A |
| `hmac_config_set_channel` | 0x720e8 | 0x766fc | the 0x0124 builder (AP channel path) | B |
| `hmac_config_set_freq` | 0x15b2e4 | 0x15b478 | STA-join sync, WAL cmd 0x0052 | - |
| `hmac_config_add_vap` | **0x15c0e8** | 0x15c95c | VAP create chain | C/F |
| `hmac_config_add_vap_check` | 0x15b900 | 0x15c174 | validates params+0x11/+0x12/+0x13 | C4 |
| `hmac_config_add_vap_init` | 0x15bac0 | 0x15c334 | host VAP object + users | C |
| `hmac_config_add_vap_sync_all` | 0x15be44 | 0x15c6b8 | posts the H2D VAP-create event | C3 |
| `wal_cfg80211_start_ap` | 0x10342c | 0x103634 | the five-frame AP start | D |
| `wal_hipriv_add_vap` | 0x147b7c | 0x147b7c | out-of-band AP VAP create (2g/5g parse) | E |
| `wal_send_cfg_event` | 0x1270d8 | 0x1270d8 | cfg-event carrier (cmd/len/body + optional response wait) | B/D |
| `wal_check_and_release_msg_resp` | 0x109578 | 0x109578 | the response wait (vendor sync point) | D3/D7/E1 |

The Lead's task-175 offsets (0x13f49c / 0x13f520 / 0x15c0e8) are the **2.4.15**
build; the tree pin used by the 5 GHz lane is the 2.5.24 build, hence the
different numbers for the same functions. Both builds pass the same 55 probes.

## T2. Host band object (`g_ast_band[2]`, stride 0x2330) [V]

| field | offset | writer | meaning |
| --- | ---: | --- | --- |
| band type byte | +0xd28 | `hmac_band_set_band` | 0 = 2.4 GHz, 1 = 5 GHz (host copy) |
| current channel | +0xd29 | `hmac_band_set_channel` / `...and_bandwith` | host copy of the programmed channel |
| bandwidth | +0x10d2 | `...and_bandwith` | host copy of the bandwidth |
| (array total) | 0x4660 | `hmac_band_module_init` memset | exactly two band objects |

`hmac_band_set_channel`/`...and_bandwith` send **no** firmware message
(no cfg-event call in either body); the channels reach the firmware only
through the 0x0124 body (T3). The STA path's `hmac_config_set_freq` is the
other caller and posts WAL cmd 0x0052 after the host write - not used by the
AP start.

## T3. The 0x0124 channel config (the vendor's first AP-start frame) [V]

Path: `wal_cfg80211_start_ap` -> channel helper (first call, synchronous) ->
`wal_send_cfg_event` (cmd 0x0124, body 4) + `wal_check_and_release_msg_resp`
-> firmware worker CA 0x841b8. Body:

| byte | vendor value | our driver value | 2.4 identical? | 5 GHz identical? |
| ---: | --- | --- | --- | --- |
| 0 | chandef band (0/1) | `p->band` | yes (0) | yes (1) |
| 1 | chip id = `OAL_NET_DEV_PRIV(dev)[2]` (= 0) | `p->vap_id` (= 0 for the AP) | yes (0) | yes (0) |
| 2 | chandef channel | `p->channel` (vendor table) | yes | yes (36) |
| 3 | 0 | 0 | yes | yes |

The firmware worker validates band/body[0] and channel/body[2], then stores
body[2]->ctx+0x10, body[0]->ctx+0x11, body[1]->ctx+0x12, 1->ctx+0x13 (valid),
then runs the PHY channel program (committed: CA 0x841b8, `wifi-beacon-static`
REPORT 3.2). **No missing host frame or field here.**

## T4. Netdev private layout and the AP VAP create [V]

| offset | value | writer | evidence |
| ---: | --- | --- | --- |
| `dev_priv[0]` | VAP index 0..18 | (VAP allocation) | E5: the 0x0124 helper bounds it by 18 and indexes the mac-res VAP array |
| `dev_priv[2]` | chip id (0) | `wal_hipriv_add_vap` after the VAP create | E4: `strb r3, [r8, #2]` from the base netdev's priv[2] |
| `dev_priv[14]` | 0 | `wal_hipriv_add_vap` | `strb r4, [r8, #14]` |
| `dev+0x1f0` | VAP info object | `wal_hipriv_add_vap` | start_ap/helper read it |
| `dev+0x328` | netdev private pointer | alloc_netdev | `OAL_NET_DEV_PRIV` |

VAP create sequence (`wal_hipriv_add_vap`): parse `create <name> <mode>
<2g|5g>` -> `hmac_band_type_to_band_id` + `hmac_band_get_wiphy` (per-band
wiphy) -> alloc_netdev(priv 0x100) -> VAP-add event cmd 0x0064 (body 24:
mode, band id, band type, band selector) -> `wal_send_cfg_event` +
**`wal_check_and_release_msg_resp`** -> `dev_priv[2/14]`, MAC ->
register_netdev -> netdev/wireless init; on register failure it sends cmd
0x0066 (VAP delete) as cleanup. The cfg80211 `.add_virtual_intf` is a stub
(previous lane), so this ioctl is the vendor's only VAP birth path.

## T5. Sync points: vendor vs our ladder [V] + [I]

| step | vendor | our `beacon_static=1` ladder |
| --- | --- | --- |
| 0x0124 channel config | post + **wait for firmware response** | post, wait for H2D announce consumption only |
| set-SSID 0x0005 | post (no visible wait) | post, announce consumption |
| auth 0x006D | post | post |
| AP config 0x0127 | post | post |
| VAP start 0x0065 | post + **wait for response** | post, announce consumption |
| VAP create (out of band) | post + **wait for response** | posted from start_ap, announce only |

The announce consumption is the transport drain (the firmware has taken the
node); it is **not** the worker's completion. The vendor's response wait is
the worker's completion. [V] vendor; [I] our ladder can therefore run the AP
frames before the 0x0124 RF/channel program finishes - the band-1 candidate.

## T6. E2 readback audit (why the zeros were mis-addressed) [V]

Convention: BAR0 = CPU + 0x6b8000 (region 5). Committed bench recipe: slot 0
CPU **0x171458** = BAR0 0x829458; base u16 CPU **0x172504** = BAR0 0x82a504;
`ctx0 = u32(slot0) + u16(base)`.

| read | tree constant | BAR0 actually read | committed CPU | delta |
| --- | ---: | ---: | ---: | ---: |
| module-table slot 0 | 0x175458 | 0x82d458 | 0x171458 | +0x4000 |
| base u16 | 0x17a504 | 0x832504 | 0x172504 | +0x8000 |

E2 line `module_ctx+0x10 u32=0x00175468 (channel=104 band=84 field=23
valid=0)`: channel 104 / band 84 are invalid, valid = 0, and 0x00175468 is a
pointer into the `0x175xxx` per-bank context region (bank 1 context CA
0x175a18) - not a channel context. `ctx+0x190 == 0` likewise read unrelated
RAM. **Both E2 readback claims are void as firmware evidence.** The patch
corrects the constants and re-reads the real fields.

## T7. Corrected read-only gate pack (what the patched fire logs)

| object | CPU | BAR0 | live value when open | meaning |
| --- | ---: | ---: | --- | --- |
| module-table slot 0 | 0x171458 | 0x829458 | 0x00119758 | context-array base |
| module-table base u16 | 0x172504 | 0x82a504 | small u16 | ctx offset |
| ctx0 | slot0+u16(base) | +0x6b8000 | e.g. 0x0011a98c | module-0 context |
| ctx0+0x10 | +0x10 | | `ch | band<<8 | f<<16 | 1<<24` | 0x0124 worker body + valid |
| ctx0+0x190 | +0x190 | | non-zero after create | firmware VAP object [V\*] |
| ctx0+0x4b0 | +0x4b0 | | non-zero | per-link context (VAP-add bind) |
| ctx0+0x4b8 | +0x4b8 | | non-zero | band object (VAP-add bind) |
| band-0 publish word | 0x15e408 | 0x816408 | 0x00174bf0 | band-0 bank context live |
| band-1 publish word | 0x15ff70 | 0x817f70 | 0x00175a18 | **5 GHz bank context live** |

The publish-word addresses are derived from the committed boot-gate decode:
band-0 object CA 0x15ca08, band-1 object CA 0x15e570, both +0x1a00; expected
values are the bank contexts CA 0x174bf0 / 0x175a18. Read-only; log-only.

## T8. Parameter contract (`ap_chan_wait_ms`)

| value | behaviour |
| ---: | --- |
| 0 (default) | legacy: no wait, no new dmesg gating; 2.4 and 5 GHz timing byte-identical to the E2 module |
| 1..1000 | band-1 AP only: after `ap_chan`, poll `ctx+0x10` every 5 ms until `channel/band/valid` matches the chandef; timeout -> abort before any AP frame (-EIO) |
| >1000 | refused at use time (-EINVAL), no frame sent |
| any (band 0 / `beacon_static=0`) | wait 0 (planner forces legacy) |
| invalid band (>1), NULL plan | -EINVAL (fail closed) |

## T9. Host-test matrix

| test | checks |
| --- | --- |
| planner defaults (wait 0, band 0/1, beacon_static 0/1) | 7 |
| wait matrix 0..1000 step 5 for band 1 | 2 x 201 |
| non-band-1 ignores the wait | 2 |
| fail-closed: >1000, band 2/0xffffffff, NULL out | 4 |
| corrected address arithmetic (BAR0 forms, old-offset audit) | 6 |
| ctx address formula (base u16, window bound, slot0 0) | 5 |
| channel-ctx predicate (+ E2 logged word is not ready) | 8 |
| patch contract on the driver tree | 10 |
| **total** | **437 planner + 10 contract, 0 failures** |

## T10. Fire gate pack (see FIRE-RECIPE)

| gate | PASS | FAIL/stop |
| --- | --- | --- |
| G6 module ctx line | `ctx0` resolved, `ctx+0x4b0`/`+0x4b8` non-zero | unresolved ctx or zero bind -> firmware lane |
| G6 band publish line | band0 == 0x00174bf0, band1 == 0x00175a18 | band1 == 0 -> firmware band-1 link/bank blocker, the timing candidate is not the cause |
| G6 wait line (wait=300) | "firmware channel context programmed after N ms (word=0x01000124)" | timeout line -> worker did not program band 1 |
| G7 observer | `iw scan freq 5180` sees `cudy-ap-test-5g` | silence after a successful wait -> firmware RF/beacon engine |
| G7 health | page 0x86807fef / state 1 / mask 0 | exception -> power-cycle, no retry |
