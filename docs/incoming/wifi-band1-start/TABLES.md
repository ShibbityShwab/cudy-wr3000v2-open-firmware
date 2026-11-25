# TABLES - lane wifi-band1-start (task-197)

All addresses are CPU addresses (CA) of the pinned image
(FIRMWARE.bin, 928,920 B, sha256 `7fc87e20...311b`; CA = file offset + 0x40000).
Host read paths: firmware RAM phys = 0x40000000 + CA + 0x6B8000;
device IO phys = CA + 0x3B8000. Grades: **[V]** re-derived by the committed
tool from the pinned artifacts; **[V\*]** bench-measured by a committed lane;
**[I]** inference; **[U]** needs a device read / stock trace.

---

## T1. The per-band MAC start CA 0x4AF0C [V]

Arguments: `r0` = band object, `r1` = per-band run-state struct
(band object + 0x14D0, i.e. the helper's `r5`). Band index source:
`u8(*(bandobj + 0x1A00) + 0x11D)` at CA 0x4AF5C.

| guard | CA | meaning |
| --- | ---: | --- |
| `ldrb r3,[r1]` bit1 | 0x4AF10/0x4AF14 | debug-log gate |
| `[r1+1] == 2` -> return | 0x4AF2A/0x4AF2C | **already started marker** |
| `[r1+4] == 0xFF` -> return | 0x4AF32/0x4AF36 | no bound VAP index |
| `0x961AE([r1+4])` == 0 -> return | 0x4AF3A/0x4AF40 | the bound VAP object must resolve |

Register writes when `band == 1` (CA 0x4AF66..0x4AFD4):

| register | operation | CA |
| --- | --- | ---: |
| 0x40114500 | write 0x55, read, write 0xFF, read (MAC-block reset) | 0x4AF72/0x4AF84 |
| 0x4011451C | read (u8 == 0xFF expected) | 0x4AF90 |
| **0x4010C220** | **OR 1, then OR 8, then OR 0x20, then OR 0x1000 (= 0x1029)** | 0x4AFB2..0x4AFD4 |
| 0x4011080C / 0x4011480C | OR 1 (gate, selected by u8(perlink+1) two-entry stack table) | 0x4AFF2..0x4AFFC |
| 0x40101220 | OR 1 (band-1 run bit) | 0x4B004..0x4B012 |
| 0x40110500/0x820C/0x8214 | reset sequence / enable | band-0 branch 0x4B06C..0x4B0E6 |

Tail: `0xF4504(perlink, band)`, `0xF5C54(bandobj)`, then
`0x96DBC(bandobj, vap)`; writes `[r1+1] = 2` and increments
`u32[r1+0x64]` (start counter) at 0x4B05C..0x4B064. [V]
Stop counterpart CA 0xBE294 clears the same bits; its callers are
0x4C27A (in 0x4C05C) and 0x4C6A0 (in 0x4C524 subcommand 3). [V]

## T2. The five direct call sites of 0x4AF0C [V]

| site | enclosing helper | how it is reached |
| ---: | --- | --- |
| 0x4BEB0 | 0x4BDCE | helper called from H2D workers key 0x0243 (0x841AA), key 0x01FE (0xA1594), VAP-down key 0x0067 (0x45F74 -> 0x974F8 -> 0x98B8A); and internal 0x644A4, 0xB8528, 0xB9780 paths |
| 0x4C34E | 0x4C05C | internal state machine: 0x4C05C <- 0x5A3BC <- 0x5A588 (also called from 0x5AB68, 0x6BA6E) [U trigger] |
| 0x4C3EC | 0x4C3AC | 0x4C3AC <- {0x4C3FC, 0x4C626}; 0x4C3FC <- {key 0x0067 worker 0x46098, key 0x0065 worker 0x4674A, key 0x0242 worker 0x4C5F4} |
| 0x4C612 | 0x4C524 (**key 0x0242**, subcommand 1) | WAL `set_green_ap` sync/conditional start |
| 0x4C652 | 0x4C524 (**key 0x0242**, subcommand 2) | WAL `set_green_ap` forced start |

No `BL`/literal reference to 0x4AF0C exists outside these five; the G+0x370
hook user (CA 0x971D8) is **not** a caller of 0x4AF0C. [V]

## T3. G hook block CA 0x1747C4, the NULL slot 0x174B34 (CA = G+0x370) [V]

| slot | installer site | stored value |
| --- | ---: | --- |
| G+0x364 | 0x5D13A | 0x565C5 |
| G+0x368 | 0x5CFC2 | 0x52BE1 |
| G+0x36C | 0x5CF60 | 0x727D1 |
| **G+0x370** | **none** | **NULL by design** |
| G+0x374 | 0x5D144 | 0x57639 |
| G+0x378 | 0x5C3B6 | 0x68C0B |

Installer CA 0x5B424, called from the pre-HCC boot init at CA 0x48442
(`bl 0x5b424`). Re-checks: no literal 0x174B34 or 0x174B38 anywhere in the
image; 19 image store instructions carry disp 0x370, none inside the hook
block/installer and none with a G-loaded base; the five other slots are
written. The only readers of G+0x370/G+0x374 are the three calls in the
VAP-on-band function CA 0x971D8 (0x9735C..0x97372). [V]

## T4. H2D config-event keys that reach a band start (table A 0x10C23C) [V]

| key | row | worker CA | role |
| ---: | ---: | ---: | --- |
| 0x0064 | 1 | 0x449E4 | module/VAP add + band bind (fresh-context rung) |
| 0x0065 | 2 | 0x46394 | VAP start -> 0x971D8 (hook path) |
| 0x0067 | 4 | 0x45F74 | VAP down; calls 0x4C3FC(bandobj,2) and 0x971D8 |
| 0x0073 | 20 | 0x84920 | VAP state sync; `body[0]=4` -> 0x971D8 at 0x8498E (hook path only) |
| 0x01FE | 53 | 0xA14BC | calls 0x4BDCE (band-start helper) |
| 0x0242 | 76 | 0x4C524 | WAL set_green_ap: **direct 0x4AF0C start/stop** |
| 0x0243 | 77 | 0x8419C | `0x4BDCE(bandobj,5,0)` -> 0x4AF0C if state[1]==3 |

Worker ABI: `worker(ctx, len, data)`; `ctx = 0xFE578(0, hdr[3])` is resolved
before dispatch, so `hdr[3]` (field B) is the per-module context index. [V]

## T5. Key 0x0242 worker CA 0x4C524: subcommand table (`tbb` at 0x4C53C) [V]

`r5 = ctx+0x4B8` (band object) must be non-NULL; `body[0]` selects:

| body[0] | target | effect |
| ---: | ---: | --- |
| 0 | 0x4C54A | diagnostics only |
| **1** | **0x4C5E2** | **sync/conditional start**: if `body[2]==1` set state bit0, `0x4C3FC(bandobj,0)`, then if `state[1]!=0` and `vap[14]==1` -> **0x4AF0C at 0x4C612** + 0x8F71C |
| **2** | **0x4C634** | **forced start**: `0x4C388(state)`, then **0x4AF0C at 0x4C652** + 0x8F71C; clears `state[1]` |
| 3 | 0x4C662 | stop: `0x961AE(state[4])` -> perlink -> **0xBE294 at 0x4C6A0** |
| 4 | 0x4C6A6 | set state bit1 from `body[1]` |
| 5 | 0x4C6BA | `state[0x6C] = body[8..11]` |
| 6 | 0x4C6C0 | `state[0x70] = body[12..15]` |
| 7 | 0x4C6C6 | `state[5] = body[3]` |
| 8 | 0x4C6CC | `state[6] = body[4]` |

Host emitter [V] (hi5622v100_wifi.ko, read in place):
`wal_config_set_green_ap` (.text+0x12AE20) -> `hmac_config_send_event(ctx,
key=0x0242, len, data)`. `wal_config_recently_cfg_event` is the key-0x0243
emitter. Neither is part of our clean host ladder.

## T6. Helper CA 0x4BDCE (band-restart glue) [V]

`r0` = band object, `r1` = mode, `r2` = payload. `r1` in {1,2}: resolve
`r2`, set run-state flags. `r1` in {4,5}: at 0x4BEA0 only arm when
`state[1] == 3` -> **0x4AF0C at 0x4BEB0**. Key 0x0243 passes `r1=5`
(CA 0x841A8), so it is the "band ready -> start" nudge.

## T7. enable-TRX / disable-TRX message processor CA 0xE8520 [V]

Selectors: `r6 = u8([r1+0xA])`, `r5 = u8([r1+0xE])`. Branches:

| selector | calls | effect |
| --- | --- | --- |
| `r5==0, r6==1` (0xE854C) | 0xD97BC, 0xD973C | band-0 MAC reset + **0x40108214 probe, final 0xF** |
| `r5!=0, r6==1` (0xE85B0) | 0xD968C, 0xD9604 | band-1 MAC reset + **0x4010C220 probe, final 0x1039** |
| `r6==1, r5==1` (0xE85E2..0xE85F2) | 0xD928C then stores | **the only writer of 0x4010E150=0x33 / 0x4010E154=0xFF / 0x4010E158=0xFF** |

The probes write test patterns, not the R3 arm value (0x1039 != 0x1029;
0x40108214 0xF is identical to the band-0 start OR-mask, so the band-0
`0xF` read alone cannot distinguish "start ran" from "enable-TRX probe ran").
No g0-id1 table A/B row reaches 0xE8520; no host key for it is known. [V/U]

## T8. Fresh band-1-bound module context (key 0x0064, worker 0x449E4) [V]

| frame field | value | CA evidence |
| --- | --- | --- |
| `body[0]` create type | 2 (AP) | 0x44A80 |
| `body[1]` parent module index | 0x12 (boot module) | 0x449EC -> 0xFE578 |
| `body[4]` module/VAP index created | free index N <= 0x12 | 0x44A24 -> 0x961AE |
| `body[6]` flag | 0 | 0x44C68 |
| `body[0x12]` band selector | **1 = band 1** | 0x44B6C -> 0xA4FC8 |
| bind | `ctx(N)+0x4B0 = *(bandobj+0x1A00)`, `+0x4B8 = bandobj`, `+0x4B4 = vap` | 0x44BBC/0x44BC2/0x44BC6 |

The bind only runs when `*(bandobj+0x1A00)` (published per-link) is nonzero;
it does **not** call 0x4AF0C itself - the start must follow via T5. Also note
the run-state struct at `bandobj+0x14D0` is **per band**, shared by all module
contexts of that band, so a "fresh context" resets the VAP side, not the band
start marker.
