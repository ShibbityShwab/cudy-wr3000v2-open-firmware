# TABLES - the band-start pre-start path (CA 0x4C3FC / CA 0x4AE7C)

All firmware addresses are CPU addresses (CA) of the pinned image:
FIRMWARE.bin, 928,920 B, sha256 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b,
CA = file offset + 0x40000; firmware RAM phys = 0x40000000 + CA + 0x6B8000.
Host module: hi5622v100_wifi.ko (rootfs-2.4.15 copy, 3,581,748 B, sha256
22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2), ELF with
symbols, read in place.  Grades: [V] re-derived by tools/fw_prestart_re.py from
the pinned artifacts; [V*] bench-measured by a committed lane; [I] inference;
[U] needs a device read / stock trace.

---

## T1. CA 0x4C3FC - the only writer of state[4] (r0 = band object, r1 = mode)

| # | CA | instruction / action | meaning |
| ---: | ---: | --- | --- |
| 1 | 0x4C400-0x4C412 | r4 = bandobj+0x14D0; test state[0] bit0 | if bit0 == 0: log 0x409, **return (no state[4] write)** |
| 2 | 0x4C424-0x4C42C | r0 = u8(bandobj+0xD6E); CA 0x581C8(r0) | nonzero only for id 0; if 0: log 0x40F, **return (no write)** |
| 3 | 0x4C436-0x4C43C | r5 = CA 0x4AE7C(bandobj) | the band VAP resolver (T3) |
| 4 | 0x4C43E-0x4C440 | if r5 == 0 | goto failure 0x4C4E6 |
| 5 | 0x4C442-0x4C444 | if mode == 2 | goto failure 0x4C4E6 **even if r5 != 0** (key 0x0067 VAP-down mode) |
| 6 | 0x4C446-0x4C448 | state[4] = u8(r5[0]) | **the "bound VAP index" store** (r5 = mctx; r5[0] = its index) |
| 7 | 0x4C44A-0x4C450 | test state[0] bit0 again | if 0: log 0x250 -> tail (state[1] untouched) |
| 8 | 0x4C45E-0x4C468 | state[1] > 1 ? | if >1: log 0x256 -> tail (state[1] untouched) |
| 9 | 0x4C46A-0x4C476 | m = CA 0x961AE(state[4]) | if m == 0: log 0x25E -> tail |
| 10 | 0x4C498-0x4C49E | r2 = u32[m+0x190]; r2 = u32[r2+0x10] | if r2 == 0: log 0x26E -> tail (no state[1] write) |
| 11 | 0x4C4A0-0x4C4C0 | state[7] = 2*((r2 / (state[5]+state[6])) & 0xff); zero-div guard sets state[5]=state[6]=(r2>>1)&0xff | ring/descriptor sizing |
| 12 | 0x4C4C2-0x4C4C8 | CA 0xA3C98(m, state+0x28, state[6]) | buffer setup |
| 13 | 0x4C4DA | **state[1] = 1** | the "pre-started" marker the 0x0242 worker and CA 0x4AF0C read |
| 14 | 0x4C47C-0x4C492 | log 0x425 (mode, state[1], state[4], r5[14]) | tail, return |

Failure branch **0x4C4E6**:

| # | CA | action |
| ---: | ---: | --- |
| F1 | 0x4C4E6-0x4C4EA | CA 0x4C3AC(bandobj, state) (T4) - runs before the 0xFF store |
| F2 | 0x4C4EE-0x4C4F0 | **movs r3,#0xFF; strb r3,[r4,#4] -> state[4] = 0xFF** |
| F3 | 0x4C4F2-0x4C4FC | log 0x42B (mode, bandobj+0x12 count) |
| F4 | 0x4C500-0x4C516 | if r5 != 0 and mode != 2: log 0x42F (r5[14]) |

**What makes 0x4C4EE store 0xFF: r5 == 0 (resolver returned NULL) or mode == 2.**
Callers of CA 0x4C3FC: key 0x0067 worker 0x46098 (mode 2), key 0x0065 worker
0x4674A (mode 1), key 0x0242 worker 0x4C5F4 (mode 0) [V].

## T2. CA 0x4C3AC - the "other call" / deferred-start rung (r0 = bandobj, r1 = state)

| CA | action | meaning |
| ---: | --- | --- |
| 0x4C3AE-0x4C3BC | test state[0] bit0 | if 0: log 0x286, return |
| 0x4C3C8 | CA 0x4C388(state): CA 0x41478(state+0x10); log 0x76 if state[0] bit0 | pending-work reset |
| 0x4C3CC-0x4C3D0 | if state[1] == 3 | log 0x28E; **CA 0x4AF0C(bandobj, state) - the unconditional start rung** |
| 0x4C3F0-0x4C3F2 | state[1] = 0 | cleared on every call |

## T3. CA 0x4AE7C - the band VAP resolver (r0 = bandobj)

Pseudo-code, exactly as compiled:

    r5 = bandobj; r8 = log ctx
    if (u8[bandobj+0x12] != 1) {                 // count != 1
        if (u8[bandobj+0x14D0] & 2) log 0x1E0 (bandobj+0xD6D);
        return 0;                                 // 0x4AE88..0x4AEFC
    }
    for (i = 0; i < u8[bandobj+0x12]; i++) {     // one iteration when count==1
        m = 0xBCD7E(u8[bandobj+i]);               // T5: == 0x961AE == 0xFE578(0,idx)
        if (!m) { log 0x1EA(idx); continue; }
        rr = 0x944DE(m);                          // T6 (5 GHz gate)
        if (rr == 1) {
            if (0x101890(u8[m+0x11], u8[m+0x10], u8[m+0x12]) == 1) {
                log 0x1F2; return 0;              // 0x4AEC6..0x4AED2: RF-only path
            }
        }
        if (u8[m+4] == 2 && (u8)(u8[m+0x0E] - 1) <= 2)
            return m;                             // 0x4AED4..0x4AEE2: THE SUCCESS
        // else: next entry
    }
    return 0;

The per-entry gate is therefore: **m[4] == 2 (AP create type) AND
m[0x0E] in {1,2,3}** (1 = set by the key 0x0065 AP start (the 0->1 store);
2 = observed before CA 0x4AF0C normalises it to 1 on a later start; 3 =
deferred-start marker, T4/T7).

The return-0 branch at 0x4AEC6 requires 0x944DE == 1 (band already up: T6) and
0x101890 == 1 (RF programme).  On a fresh band-1 VAP m[0x10] (channel) is 0, so
0x101890's parameter round-trip (CA 0x10172C, table CA 0x10BE68, mapper CA
0x101650) cannot take its success path; the branch cannot explain a fresh-boot
failure [V/I].

## T4. The 0x0242 worker CA 0x4C524, subcommand 1 (sync/conditional start)

    r5 = u32[ctx+0x4B8]                 // band object (must be nonzero)
    r4 = r5 + 0x14D0                    // state
    if (body[0] == 1) {                 // 0x4C5E2
        if (body[2] == 1) {             // 0x4C5E2-0x4C5E8
            state[0] |= 1;              // 0x4C5EA-0x4C5F0 (arm)
            CA 0x4C3FC(bandobj, 0);     // 0x4C5F4 - writes state[4] / 0xFF
            m = CA 0x4AE7C(bandobj);    // 0x4C5FA - SECOND call
            if (!m) return;             // 0x4C5FE-0x4C600
            if (state[1] == 0) return;  // 0x4C602-0x4C606
            if (u8[m+0x0E] != 1) return;// 0x4C608-0x4C60C
            CA 0x4AF0C(bandobj, state); // 0x4C612 -> 0x4010C220 |= 0x1029
            CA 0x8F71C(bandobj);
        } else {
            if (state[0] & 1) {         // 0x4C61E-0x4C620
                CA 0x4C3AC(bandobj, state);
                state[0] &= ~1;         // 0x4C62A-0x4C632
            }
        }
    }

Subcommand 2 (0x4C634): state[1] = 0; CA 0x4AF0C directly; 0x8F71C; state[1] = 0.
Subcommands 3-8 (stop / log_en / free_cca / free_gap / work_slot / pause_slot)
per ../wifi-band1-start/TABLES.md T5 [V].

## T5. Object resolution - CA 0xBCD7E == CA 0xFE578(0, idx) == CA 0x961AE(idx)

| element | CA space | evidence |
| --- | --- | --- |
| module-context pointer table | table[idx] = u32[0x171458 + 4*idx] | literal 0x170E08+0x650 in 0xBCD7E; literal 0x170E28+(0x18C+idx)*4 in 0xFE578 |
| common offset | base = u16[0x171504] | literal 0x170E08+0x6FC / 0x170E28+0x6DC (same address) |
| module ctx (mctx) | mctx = table[idx] + base | both functions return it |
| mctx[0] | index | 0xFE6BC stores body[4] |
| mctx[4] | create type (2 = AP) | 0xFE6EA-0xFE6EE: mctx[4] = body[0] |
| mctx[0x0E] | VAP start state (1..3) | 1 = key 0x0065 AP branch 0x46418-0x4641A (0->1), 2->1 at 0x4B04E, 4 = non-AP paths |
| mctx[0x10] | channel | 0xFE738: 0 |
| mctx[0x11] | band (1 = 5 GHz) | 0xFE72A-0xFE736: body[17] if 1 else 0 |
| mctx[0x12] | field | 0xFE734-0xFE752 (AP branch: 0) |
| mctx[0x190] | async-populated object ptr | 0x4C498 deref; 0x5A144/0x5B496 writers; bench R2: 0 right after a fresh post, nonzero after settle |
| mctx[0x440] | band-config flag | writers 0x455D0 / 0x7100C; 0x944DE tests == 1 |
| mctx[0x444] | key-0x0065 body[4..7] | 0x463F6-0x463F8 |
| mctx[0x4AC] | start-helper object ptr | 0x4AF0C tail; key 0x0065 worker 0x46438 |
| mctx[0x4B0] | per-link (published) | bind 0x44BBC |
| mctx[0x4B4] | VAP object | bind 0x44BC6 (from CA 0xB1D90) |
| mctx[0x4B8] | band object | bind 0x44BC2 |

Band object fields (bandobj = mctx[0x4B8]):

| offset | field | evidence |
| ---: | --- | --- |
| +0x00..+0x11 | band VAP list entries (module indices) | 0x44C28: bandobj[count] = body[4] |
| +0x12 | band VAP list count (u8) | 0x44C24: count+1 |
| +0x13 | count for create type 1 | 0x44C2C-0x44C32 |
| +0x1A00 | published per-link (u32) | bind gate 0x44B76-0x44B7C |
| +0x14D0 | per-band run-state struct (shared by all module contexts of the band) | 0x4AF50 callers, 0x4C524 |
| +0xD6E | band-id byte (CA 0x581C8 gate = must be 0) | 0x4C424 |
| +0xEC8 | band-up flag (u8 of a u32 word; writer 0x48D26 sets 0x01000101) | 0x944DE return |

## T6. CA 0x944DE - the 5 GHz gate (r0 = mctx)

    if (!mctx) return 0;                          // log 0x68C
    r0 = mctx[0]; bandobj = 0xBCD C4(r0)          // lookup(idx); return ctx ? [ctx+0x4B8] : 0
    if (!bandobj) return 0;                       // log 0x692
    if (u8[mctx+0x11] != 1) return 0;             // 0x94500-0x94504
    if (u8[mctx+0x440] != 1) return 0;            // 0x94506-0x9450C
    return u8[bandobj+0xEC8];                     // 0x9450E

## T7. key 0x0065 firmware worker CA 0x46394 (AP branch, mctx[4] == 2)

| # | CA | action |
| ---: | ---: | --- |
| 1 | 0x463A8 | save mctx[0x0E] for a log |
| 2 | 0x463EC-0x463F8 | mctx[17] = body[2]; mctx[18] = body[3]; u32[mctx+0x444] = body[4..7] |
| 3 | 0x463FC | CA 0xD4048(mctx, body[1]) -> **mctx[15] = body[1]** (0xD4072-0xD4074) |
| 4 | 0x46408-0x4640C | if mctx[4] != 2 -> STA branch (0x464D8: mctx[0x0E] = 4) |
| 5 | 0x4640E-0x46414 | CA 0xBD114(mctx, mctx[0x190]) - tolerates a zero argument |
| 6 | **0x46418-0x4641A** | **movs r3,#1; strb r3,[mctx+0x0E] -> mctx[0x0E] = 1** (the 0->1 store; CA 0x4AF0C's tail also writes 1 here, only when it was 2) |
| 7 | 0x4641C-0x46426 | CA 0xD41AC(mctx); may return early (log only) - mctx[0x0E] stays 1 |
| 8 | 0x46434-0x4643C | CA 0xF593C(mctx[0x4AC], mctx[4]) |
| 9 | 0x46440-0x4644A | if published != 0: bandobj[0x13BC] = 0xFF (0x464E0), continue |
| 10 | 0x46450-0x464F8 | if mctx[0x4B0] == 0: log 0xFA, continue; else if u8[perlink+0x6C6] != 1: per-link buffer setup (0x464FA-0x4659A) |
| 11 | 0x46460-0x4647E | AP path: dereferences **mctx[0x190]** at 0x46470 ([+0x58] bit2 <- band field) - **faults if mctx[0x190] == 0** |
| 12 | 0x464AE-0x464B0 | CA 0x971D8(bandobj, mctx) - the VAP-on-band hook path (G+0x370 is NULL) |

Reverse: key 0x0064 worker CA 0x449E4 (create/bind/list):
mctx created by CA 0xFE6BC (0x44AA8-0x44AAC), band object resolved by CA 0xA4FC8
(body[0x12]) at 0x44B6C-0x44B70, bind requires published != 0 (0x44B76),
stores +0x4B0/+0x4B4/+0x4B8 at 0x44BBC-0x44BC6, appends the list entry at
0x44BF8-0x44C32.

## T8. The stock host sequence (vendor hi5622v100_wifi.ko, in-place)

| element | address | fact |
| --- | --- | --- |
| hmac_config_start_vap_event | .text+0x15AF88 | builds the 12-byte key-0x0065 body: [0]=arg, [1]=vap+0x0F, [2]=vap+0x11 (band), [3]=vap+0x12 (field), [4..7]=vap+0x414; key 0x0065, len 0x0C |
| its AP caller | 0x4826C (hmac_start_bss_in_available_channel) | sends the VAP start as part of the AP BSS start path |
| green_ap enable emitter | 0x114268 wal_hipriv_green_ap -> 0x11400C wal_green_ap_analysis_process -> 0x126C98 wal_send_cfg_event | userspace private command; buffer = {u16 key 0x0242, u16 len 0x10, data[16]} |
| green_ap command table | .data+0x1AD8 g_ast_mac_green_ap_config_table (9 x 8 B) | names info(0) enable(1) work(2) pause(3) log_en(4) free_cca(5) free_gap(6) work_slot(7) pause_slot(8); the numeric argument of enable/work_slot/pause_slot/log_en/free_cca/free_gap is written to data[2]/data[3]/data[4]/data[1]/data[8]/data[12] |
| wal_config_set_green_ap | .text+0x12AE20 | sends key 0x0242 through hmac_config_send_event; no in-module caller (userspace/private path) |

The R3b frame body {01,00,01,00,00} is exactly the firmware's view of
"enable 1" (data[0]=1, data[2]=1).  The stock's order is
VAP add (key 0x0064) -> VAP start (key 0x0065, hmac BSS start) -> ...
-> green_ap enable (key 0x0242); our ladder posts the WAL group-2
equivalent of the VAP start as plan step 8, i.e. *after* the 0x0242.

## T9. state struct bandobj+0x14D0 - field map [V]

| field | width | writers (CA) | meaning |
| --- | --- | --- | --- |
| state[0] bit0 | 1 | set 0x4C5EA (0x0242 subcmd 1 body[2]==1); clear 0x4C62A | band-start request armed; required by 0x4C3FC / 0x4C3AC |
| state[0] bit1 | 1 | 0x4C6AC (0x0242 subcmd 4 = log_en) | debug/log enable |
| state[0] bit (0xE0 mask) | 3 | 0x4C142 read in the band state machine | other run flags |
| state[1] | 1 | 1 @0x4C4DA; 2 @0x4B05E; 3 @0x4C246/0x4C24A; 0 @0x4C3F2, 0x4C65E | 0 idle / 1 pre-started / 2 MAC started / 3 deferred start rung |
| state[2], state[3] | 1 | - | logged by CA 0x4C388 / 0x4C3AC; semantics [U] |
| state[4] | 1 | index @0x4C448; 0xFF @0x4C4EE | bound VAP/module index (0..0x12) or 0xFF "none" |
| state[5], state[6] | 1 | 0x4C4A0-0x4C4BC; 0x4C6C6/0x4C6CC (0x0242 subcmd 7/8) | ring sizing divisors |
| state[7] | 1 | 0x4C4C0 | 2*ratio ring figure |
| state[0x0C] | 1 | - | read by CA 0x4ACxx band state machine |
| state[0x64] | u32 | 0x4B05C-0x4B064 | start counter (incremented by CA 0x4AF0C) |
| state[0x6C] | u32 | 0x4C6BA (0x0242 subcmd 5 = free_cca) | free-CCA window |
| state[0x70] | u32 | 0x4C6C0 (0x0242 subcmd 6 = free_gap) | free-gap window |

## T10. Read-only probe map (all object-relative, so mapping-convention safe)

Resolve mctx through the existing bench-proven readback chain
(../wifi-band1-start-send-2 patch: SLOT0 CA 0x175458, BASE CA 0x17A504,
mctx = slot + base; mctx+0x4B8 = bandobj).  All probes below are then relative
to mctx / bandobj / perlink and need no new absolute addresses.

| id | probe | read | pass condition |
| --- | --- | --- | --- |
| G1 | mctx | u32 | nonzero |
| G2 | list count | u8 bandobj+0x12 | == 1 |
| G3 | list entry | u8 bandobj[0] | == vap_add_index (0) |
| G4 | published | u32 bandobj+0x1A00 | nonzero |
| G5 | mctx[0x190] | u32 | nonzero (else the key-0x0065 worker can fault at 0x46470) |
| G6 | mctx[0x4AC] | u32 | nonzero (start-helper object; logged only) |
| G7 | type | u8 mctx+4 | == 2 (AP) |
| G8 | band | u8 mctx+0x11 | == 1 (5 GHz) |
| G9 | band-config flag | u8 mctx+0x440 | == 0 before the first start |
| G10 | band-id gate | u8 bandobj+0xD6E | == 0 |
| G11 | band-up flag | u8 bandobj+0xEC8 | == 0 before the first start |
| G12 | VAP state | u8 mctx+0x0E | 0 before key 0x0065; **1 after** |
| G13 | run state | state[0], state[1], state[4] | 0 / 0 / 0 (fresh) |
| G14 | per-link branch | u8 perlink+0x6C6 | observation only (0 -> buffer setup path) |

## T11. Key-0x0065 frame to insert (40 B, same carrier as the 0x0242 frame) [V/I]

    header 12 B: 00 01 00 <field B = vap_add_index> <total u16 = 0x0028> 01 00 00 00 5a 5a
    token   8 B: 00 00 00 00 01 00 00 00
    key u16    : 65 00                    (0x0065 VAP start)
    len u16    : 0c 00                    (12-byte body)
    body 12 B  : 00 01 01 00 00 00 00 00 00 00 00 00
    tail  4 B  : ff ff ff ff

body[0] = 0 (channel argument, unused by the worker); body[1] = 1
(vap+0x0F state byte); **body[2] = 1 (band = 5 GHz; must be 1 or the later
5 GHz RF branch 0x40684 skips the VAP)**; body[3] = 0 (field); body[4..7] = 0.
The sibling lane wifi-vap-start already has the same 40-B g0-id1 key-0x0065
carrier KAT (there with body[2]=0); only the body bytes differ.
