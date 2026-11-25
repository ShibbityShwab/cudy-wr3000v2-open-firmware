# TABLES - lane wifi-fw-chan-worker (task-180)

All firmware addresses are CPU addresses (CPU = file offset + 0x40000 of the
pinned \u00a7FIRMWARE.bin\u00a7, sha256 \u00a77fc87e20...311b\u00a7).  All host-module addresses
are link-time \u00a7.text\u00a7 offsets of the pinned \u00a7hi5622v100_wifi.ko\u00a7 2.4.15 build
(sha256 \u00a722fa789a...532c7d2\u00a7).  Everything here is re-derived and asserted by
\u00a7tools/fw_chan_worker_re.py --selftest\u00a7 (**89/89 PASS**); the tool reads the
local blobs in place and copies no vendor byte into the repository.
Evidence markers as in REPORT.md: **[V]** re-derived here, **[V\*]** bench
measured by a committed lane / the Lead, **[I]** inference, **[U]** needs a
device read.

---

## T1. Dispatch: the g0 id-1 cfg-event path [V]

| element | address | evidence |
| --- | --- | --- |
| group-0 id-1 dispatcher | CA **0x43D58** | prologue bytes \u00a70df10702...\u00a7; dispatcher window in the tool |
| message-header fetch | CA 0x409EA (\u00a7bl 0xFE578\u00a7 path) | \u00a7fcf73dfe\u00a7 at 0x43D6C |
| module index | \u00a7payload[3]\u00a7 -> lookup(0, id) | \u00a7c178\u00a7 at 0x43D72, \u00a7bl 0xFE578\u00a7 at 0x43D76 |
| context lookup | CA **0xFE578** (G + 0x18C + id, u16 G+0x6DC, G = 0x170E28) | PD4/ctx-create committed lanes |
| key | \u00a7u16 payload+0x14\u00a7 | \u00a7a28a\u00a7 at 0x43D90 |
| subkey | \u00a7u8 payload+0x16\u00a7 | \u00a7a17d\u00a7 at 0x43DF0 |
| body | \u00a7payload+0x18\u00a7 | \u00a704f11802\u00a7 at 0x43DEC |
| key table A | CA **0x10C23C**, 217 x {u16 key; u16 pad; u32 worker} | literal at 0x43E18 = 0x0010C23C |
| key table B | CA **0x103BF8**, 88 entries | literal at 0x43E1C = 0x00103BF8 |
| key 0x0124 row | **A[108] @ CA 0x10C59C -> worker 0x0841B9** | key=0x0124 worker=0x0841b9 |
| key 0x03E4 row | A[8] -> sibling worker 0x084261 | same body store, no RF calls |
| status | worker return; 0 = ok, non-zero logged and returned | tail window 0028b6d0e678 |

## T2. Worker CA 0x841B8: gates, stores, post-store calls [V]

| step | address | bytes / meaning |
| --- | --- | --- |
| prologue | 0x841B8 | 0023f7b5 (movs r3,#0; push {r0,r1,r2,r4-r7,lr}) |
| per-link gate | 0x841BC | d0f8b074 = ldr.w r7,[r0,#0x4B0]; NULL -> return **0x64** (6426 at 0x841DE) |
| validate call | 0x841E6 | 0df10702a97828787df0 = &index; r1=body[2]; r0=body[0]; bl 0x10172C |
| validation failure | 0x84204 | 4ff47a76 = mov.w r6,#0x3E8; returns **0x3E8**, no store |
| store window | 0x8420A | ab78204623742b7863746b78e274a374: +0x10=body[2], +0x11=body[0], +0x13=index, +0x12=body[1] |
| post-store calls | 0x8421A | 79f063fd FDCE4; d4f8ac04 load module+0x4AC; 70f0d9fe F4FD8; 3a46 d7f8201e 2046 args; bcf729fa C40684; 97f8c831 63b1 perlink+0x1C8 gate; 12f0dbfe C97008 |
| tail | 0x84252 | 0023 e375 = ctx+0x17 = 0; return 0 |

## T3. Validation helper CA 0x10172C and the channel tables [V]

| element | value |
| --- | --- |
| helper | CA **0x10172C**; band-0 count #0x0E at 0x10174C; band-1 count #0x1D at 0x101736 |
| band-0 table | CA **0x117B48**, 14 entries stride 4: 1..14 (literal at 0x101764 = 0x00117B48) |
| band-1 table | CA **0x117AD4**, 29 entries stride 4: 36 40 44 48 52 56 60 64 100 104 108 112 116 120 124 128 132 136 140 144 149 153 157 161 165 184 188 192 196 (literal at 0x101760 = 0x00117AD4) |
| success | return 0, *out = table index (strb r5,[r2] at 0x101748) |
| failure | return **0x3E8** (4ff47a70 at 0x10175A), no *out write |

## T4. The module channel context word (ctx+0x10) [V]

word = channel | band<<8 | body[1]<<16 | index<<24  (bytes +0x10, +0x11,
+0x12, +0x13).

| band | channel | table index | word | old "valid==1" gate |
| ---: | ---: | ---: | ---: | --- |
| 1 | 36 | 0 | **0x00000124** | REFUSE (the G6b line) |
| 1 | 40 | 1 | 0x01000128 | PASS |
| 1 | 196 | 28 | 0x1c0001c4 | REFUSE |
| 0 | 1 | 0 | 0x00000001 | REFUSE |
| 0 | 6 | 5 | 0x05000006 | REFUSE |
| 0 | 14 | 13 | 0x0d00000e | REFUSE |

The G6a readback chan=0x00000124 (channel=36 band=1 field=0 valid=0) [V*]
is therefore the *correct successful* store; the old gate could never pass for
ch36 (REPORT section 4).

## T5. Post-store device work and the silent 5 GHz gates [V]

| call | address | behaviour |
| --- | --- | --- |
| VAP band-class | CA **0xFDCE4** | reads module+0x11/+0x12; writes [module+0x190]+0x1A0 bits 1..7; no RF |
| module+0x4AC path | CA **0xF4FD8** | operates on the module+0x4AC object; no RF |
| 5 GHz RF branch | CA **0x40684** | body only if module[4]==2 (022b at 0x4068A) **and** CA 0x944DE == 1 (0128 at 0x40696); then 0x101890(band,field,channel), CA 0xA2FA2, CA 0x89840 |
| 5 GHz gate | CA **0x944DE** | module+0x11==1 (0x94500), module+0x440==1 (0x94506), then [CA 0xBCDC4(module[0]) + 0xEC8] (0x9450E) |
| band object | CA **0xBCDC4** | lookup(id); return ctx ? [ctx+0x4B8] : 0 (08b5 d9f7f2f9 08b1 d0f8b804 08bd) |
| 5 GHz RF programme | CA **0x101890** | band != 1 -> return 0 (0128 ... 02d0 0020); re-validates via CA 0x10172C; parameter table **CA 0x10BE68** (literal at 0x101BA0); mapper CA 0x101650; returns 1 on the checked descriptor condition |
| general channel apply | CA **0x97008** | when perlink+0x1C8 != 0; module[4] in {1,2}; uses [module+0x10] channel; band object/per-link updates |
| writer of module+0x440 | CA **0x455D0** (strb.w r3,[r6,#0x440]) | source [r7+0x19A1] (band/config apply path) [I]; also CA 0x7100C from a config block +0x30 |
| writer of perlink+0x1C8 | CA **0x455C8** | strb.w r8,[r3,#0x1C8] next to the +0x440 write |

## T6. Vendor host cfg-event transaction (the "synchronous path") [V]

| element | 2.4.15 offset | facts |
| --- | ---: | --- |
| wal_send_cfg_event | **0x126C98** (size 1140) | allocates a request with a 12-byte prefix: byte 0 = flag, u16 len@+6, u32 seq@+8 (global g_wal_config_seq_num), body copied at +12; sync mode waits for completion |
| wal_alloc_cfg_event | 0x126AB0 (size 340) | allocates the request entry |
| D2H completion | calls at 0xFBAA0 / **0xFBB40** | copies the answer body, then wal_set_host_msg_response_by_addr(addr, seq, data, len) |
| wal_set_host_msg_response_by_addr | 0x1269D8 (size 216) | completes the matching pending request (data/length stored on the entry) |
| wal_check_and_release_msg_resp | **0x109370** (size 104) | frees the request, returns the answer data pointer |
| channel helper send | **0x102F20-0x102F50** | cfg chunk {u16 cmd=0x0124, u16 len=4, band, chip, channel, 0}; wal_send_cfg_event(dev,1,8,...) then wal_check_and_release_msg_resp; the helper does not read the answer body |
| the answer body | - | **[U]** (the completion body is copied but the vendor does not consume it) |

What our synthesized frames do differently: the token/request prefix bytes our
h2d builders leave zero (vsf_build_cfg_event writes payload[0]=0,
payload[4]=1) and no pending request is registered, so the firmware's cfg
answer is not a signal our driver consumes. This is the CW-2 candidate trigger
in REPORT section 7. The answer is an ordering trigger, not a data input.

## T7. Read-only BAR0 pack (the fire rung) [V*] expected values from G6a

BAR0 = CPU + **0x6B8000** (project convention). One-step reads:

| object | CPU | BAR0 | len | G6a-expected |
| --- | ---: | ---: | ---: | --- |
| module table slot 0 | 0x171458 | 0x829458 | 4 | 0x00119758 |
| module table base u16 | 0x172504 | 0x82A504 | 2 | 0x0000 -> ctx0 = 0x00119758 |
| ctx0 channel word | 0x119768 | 0x7D1768 | 4 | 0x00000124 (ch36 band1 index0) |
| ctx0 index byte | 0x11976B | 0x7D176B | 1 | 0 (the decoded table index) |
| ctx0 create type | 0x11975C | 0x7D175C | 1 | 2 = VSF_IFTYPE_AP |
| ctx0 VAP object | 0x1198E8 | 0x7D18E8 | 4 | 0x01010F18 (G6a) |
| ctx0 per-link | 0x119C08 | 0x7D1C08 | 4 | 0x00175A18 (G6a) |
| ctx0 band object | 0x119C10 | 0x7D1C10 | 4 | 0x0015E570 (G6a) |
| ctx0 module+0x440 | 0x119B98 | 0x7D1B98 | 1 | 1 = CA 0x101890 reaches the RF programme |
| band1 publish word | 0x15FF70 | 0x817F70 | 4 | 0x00175A18 (G6a live) |
| band0 publish word | 0x15E408 | 0x816408 | 4 | 0x00174BF0 (G6a live) |
| band1 object+0xEC8 | 0x15F438 | 0x817438 | 1 | 1 = CA 0x944DE passes |
| band1 object+0x19A1 | 0x15FF11 | 0x817F11 | 1 | source of module+0x440 [I] |
| bank1 ctx +0x1C8 | 0x175BE0 | 0x82DBE0 | 1 | non-zero = CA 0x97008 runs |

Two-step paths for a non-G6a boot: read ctx0+0x4B0 (per-link) and
ctx0+0x4B8 (band object) first, then add +0x1C8 / +0xEC8 / +0x19A1.
The band objects are at CA 0x15CA08 (band 0) / 0x15E570 (band 1); their
+0x1A00 publish words are 0x15E408 / 0x15FF70 (committed boot-gate decode).

**Decision tree.** module[4]==2 && module+0x440==1 && bandobj+0xEC8==1 ->
the CA 0x101890 RF programme was invoked; any term 0 -> it was skipped
silently and the 5 GHz failure is that gate, not the 0x0124 worker.

## T8. The G6a/G6b inputs this lane explains [V*] (Lead's fire lines)

| rung | line | decoded meaning |
| --- | --- | --- |
| G6a | band0=0x00174bf0 band1=0x00175a18 | both firmware band/link contexts live (not the failure) |
| G6a | chan=0x00000124 (channel=36 band=1 field=0 valid=0) | the complete, successful 0x0124 store; bytes 36,1,0,index 0 |
| G6b | REFUSED ... did not report channel=36 band=1 valid after 300 ms (word=0x00000124) | host gate required index==1; impossible for ch36 -> void negative |
| G6a | page golden:1 | no exception; the worker did not fault |
