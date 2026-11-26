# TABLES - wifi-vap-start-gates (key 0x0065 worker + 0x440/0xEC8 semantics + stock order)

**Lane:** wifi-vap-start-gates (task-212). All addresses are CPU addresses for
the firmware (CPU = file offset + 0x40000, FIRMWARE.bin 928,920 B sha
`7fc87e20...`) or .text-relative for the vendor module
(hi5622v100_wifi.ko 3,581,748 B sha `22fa789a...`). [V] = decoded in this
lane from the pinned local artefacts (`tools/fw_vap_start_gates_re.py`).

## T1. key 0x0065 -> worker CA 0x46394 [V]

| element | value |
| --- | --- |
| g0-id1 dispatcher table A | CA 0x10C23C, 217 x 8 B `{u16 key, u16 ?, u32 worker\|1}` |
| A[2] | key 0x0065 -> worker 0x46395 (Thumb) -> **CA 0x46394** |
| worker range | CA 0x46394..0x467C8 |
| signature | (r0=mctx, r1=?, r2=body) |

## T2. worker CA 0x46394 - refusals, stores, calls [V]

| CA | action |
| ---: | --- |
| 0x463AA-0x463B4 | `CA 0x961AE(mctx[0])`; 0 -> log 0x517, **return 100** |
| 0x463D4-0x463DC | `mctx[0x4B8]` band object; 0 -> log 0x51E, **return 100** |
| 0x463EC-0x463F8 | body[2] -> mctx+0x11; body[3] -> mctx+0x12; body[4..7] -> mctx+0x444 |
| 0x463FC | CA 0xD4048(mctx, body[1]) |
| 0x46404 | CA 0x94518(mctx) (MAC VAP start) |
| 0x46408-0x4641A | type==2 (AP): CA 0xBD114(mctx, mctx[0x190]); **mctx[0x0E]=1**. type==1: mctx[0x0E]=4. other: no store |
| 0x4641C-0x46430 | CA 0xD41AC(mctx); u32[bandobj+0x1A00] logged only |
| 0x46434-0x4644A | CA 0xF593C(mctx[0x4AC], type); published!=0 -> bandobj[0x13BC]=0xFF |
| 0x46450-0x4659A | perlink=mctx[0x4B0]; first start: 280 B + 92 B buffers, +0x6C4=16, +0x6C6=1 |
| 0x46460-0x46480 | AP: **deref mctx[0x190] at 0x46470** ([`+0x58`] bit2 <- band) -> G5 fault gate |
| 0x46482-0x4648A | mctx[0x0F]==10 -> CA 0xFDCE4(mctx) |
| 0x4648E-0x464B0 | mctx[0x4A0]!=0 -> CA 0x5E95C(module), **CA 0x971D8(bandobj, mctx)** |
| 0x464B4-0x467C6 | CA 0x5F450(module); 0 -> CA 0x947E4, 0x8E51A, 0x4E4DC(module,19,0), **CA 0x4C3FC(bandobj, 1)**, log, return 0; nonzero -> error log + return code |

**Not accessed anywhere in 0x46394..0x467C8:** mctx+0x440, bandobj+0xEC8,
mctx+0x10 channel, state struct, band VAP list count, bandobj+0xD6E.
The scan assertion is `worker NEVER reads mctx[0x440] or bandobj[0xEC8] = PASS`.

## T3. the CA 0x944DE RF-enable gate [V]

```
if (!mctx) return 0;
bandobj = CA 0xBCDC4(mctx[0]);           /* ctx ? [ctx+0x4B8] : 0 */
if (!bandobj) return 0;
if (u8[mctx+0x11] != 1) return 0;        /* 0x94500 */
if (u8[mctx+0x440] != 1) return 0;       /* 0x94506 */
return u8[bandobj+0xEC8];                /* 0x9450E */
```

| consumer | call | effect of 0 |
| --- | ---: | --- |
| CA 0x40684 (5 GHz RF branch) | 0x40692 | silent return: CA 0x101890 / CA 0xA2FA2 never run |
| CA 0x4AE7C (band VAP resolver) | 0x4AEB0 | resolver returns 0 -> CA 0x4C3FC fails -> state[4]=0xFF |

## T4. 0x440 / 0xEC8 writers [V address, I name]

| writer | instruction | source | meaning |
| ---: | --- | --- | --- |
| CA 0x455D0 | `strb.w r3,[r6,#0x440]` | `bandobj[0x19A1]` | band/config apply copy |
| CA 0x7100C | `strb.w r3,[r6,#0x440]` (+0x441..+0x448) | config block `[r0+0x30,0x31,0x2e,0x2f,0x29,0x43]` | config copy |
| CA 0x48D26 | `str.w r3,[r4,#0xEC8]` | literal CA 0x48DFC = **0x01000101** | band bring-up: +0xEC8=1,+0xEC9=1,+0xECB=1 |
| CA 0xB6C44 | `strb.w r0,[r5,#0xEC8]` | parsed message byte (CA 0xC1DC0), clears +0xEC9 if 0 | band up/down |

Polarity: **1 = RF-enable / band-up = normal**; 0 = not configured/disabled
(the RF programme is skipped with no log). Nothing writes 0 to mean "busy".

## T5. stock host order [V]

| element | address | fact |
| --- | ---: | --- |
| hmac_config_start_vap_event | .text+0x15AF88 | builds `{arg=1, vap[0x0F], vap[0x11], vap[0x12], u32 vap[0x414]}`, key 0x0065, len 12 -> hmac_config_send_event |
| hmac_start_bss_in_available_channel | .text+0x48204 | calls the emitter at **0x4826C**; tail of hmac_chan_start_bss |
| hmac_chan_start_bss | .text+0x48358 | tail-b 0x48204 |
| hmac_start_all_bss_of_band | .text+0x13FD88 | calls 0x48358 at 0x13FEDC |
| hmac_config_set_channel | .text+0x764E4 | key 0x0124: `mov r1,#292` 0x76744 -> hmac_config_send_event 0x76748 |
| wal_config_set_channel | .text+0x12AAB0 | calls 0x764E4 at 0x12AB14 |
| wal_cfg80211_start_ap | .text+0x10342C | -> write_msg (below) |
| wal_cfg80211_start_ap_write_msg | .text+0x103340 | `wal_send_cfg_event` (160-B AP config) at 0x10338C **then** `wal_start_vap` at 0x10339C |
| wal_start_vap | .text+0x114E10 | g2 cmd 0x0065 (`mov r3,#101`, 16 B, body[0]=1, body[4..7]=vap cookie) |

Bench-proven five-message order (../wifi-re-report/REPORT.md 6.15):
channel config 0x0124 FIRST -> SSID 0x0005 -> auth 0x006D -> ap_cfg 0x0127 ->
VAP start 0x0065 after ap_cfg -> AP-ENABLED.

## T6. fired gate matrix (send-4 T4) vs corrected

| id | fired R4b | corrected | refusal |
| ---: | --- | --- | --- |
| G1 | mctx != 0 | same | -ENODEV |
| G2 | bandobj != 0 && u8[+0x12]==1 | same (0x0242 precondition) | -ENOENT |
| G3 | u8[bandobj[0]]==index | same | -EEXIST |
| G4 | u32[bandobj+0x1A00]!=0 | same | -EAGAIN |
| G5 | u32[mctx+0x190]!=0 | **same (fault gate 0x46470)** | -EFAULT |
| G6 | u32[mctx+0x4AC] | observation | warn |
| G7 | u8[mctx+4]==2 | same | -EPROTO |
| G8 | u8[mctx+0x11]==1 | same | -EINVAL |
| **G9** | **u8[mctx+0x440]==0** | **u8[mctx+0x440]==1** | -EAGAIN |
| G10 | u8[bandobj+0xD6E]==0 | same (0x4C3FC precondition) | -EINVAL |
| **G11** | **u8[bandobj+0xEC8]==0** | **u8[bandobj+0xEC8]==1** | -EAGAIN |
| G12 | u8[mctx+0x0E]==1 post | same | -EAGAIN |
| G13 | state[0]==0,state[1]==0,state[4] in {0,0xFF} | same | -EBUSY |
| G14 | u8[perlink+0x6C6] | observation | log |

## T7. expected probe pack after the corrected fire (ch40 AP)

| probe | expected |
| --- | --- |
| 'vap_start_g0 pre:' line | `band=1 ... count=1 entry0=0 published!=0 d6e=0 ec8=1 b190!=0 b440=1`; verdict "...(the G1..G13 gates are open; G9/G11 = the RF-programme enable, 1 = normal)" |
| 0x0065 frame | 40 B g0-id1 key 0x0065, consumed, posted N/9 |
| 'vap_start_g0 post:' | `mctx[0x0e]=1 mctx[0x11]=1`; state1/state4 unchanged from fresh (0x00/0xFF) - the 0x0242 arm bit has not run yet |
| 0x0242 band_start | posted, `state[1]=1` (or 2), `state[4]=0x00` |
| P1 0x404C4220 | 0x1029 |
| P9 0x40817A40 | u8+4 = 0x00, u8+1 in {1,2} |
| P12 R+0x3A8 / R+0x3AC | 0x01800060 / 0x03E6138A (already programmed at ap_chan) |
| P13 | the ch40 beacon at 5200 MHz |
| page | 0x86807FEF / state 1 / mask 0 |

## T8. evidence

`evidence/decode.txt` (33 PASS lines incl. the worker no-access scan),
`evidence/tables.txt`, `evidence/selftest.txt` (39 checks, 0 failures).
Regenerate with `tools/fw_vap_start_gates_re.py` (see README).
