# REPORT - the band-start pre-start path: why state[4] stays 0xFF when the VAP is bound

**Lane:** wifi-band-start-prestart (shared task **task-208**).  **HOST-ONLY**:
decode + probe design + fire recipe; the Lead fires.  Companions:
[README.md](README.md), [TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[tools/fw_prestart_re.py](tools/fw_prestart_re.py), [evidence/](evidence/).

Grades: **[V]** re-derived by this lane from the pinned artifacts; **[V\*]**
bench-measured by a committed lane; **[I]** inference; **[U]** needs the fire.

---

## 0. Answer in one paragraph

CA 0x4C3FC (the only writer of state[4]) fails and stores 0xFF at CA
0x4C4EE-0x4C4F0 exactly when the band VAP resolver CA 0x4AE7C returns NULL (or
when it is called with mode 2, the VAP-down mode).  With bandobj+0x12 == 1 (the
R3b readback proved count=1) the resolver's single entry comes down to:
mctx = 0xBCD7E(bandobj[0]) (== 0x961AE(idx) == 0xFE578(0,idx), the module
context); the entry is returned only if **mctx[4] == 2** (AP create type) **and
mctx[0x0E] in {1,2,3}**.  mctx[4] is set to 2 by the key-0x0064 VAP add
(CA 0xFE6BC stores body[0]); **mctx[0x0E] is set to 1 only by the key-0x0065
VAP start worker CA 0x46394** (AP branch, CA 0x46418-0x4641A).  The stock sends
key 0x0065 from its AP-start path (host module hmac_config_start_vap_event @
.text+0x15AF88, 12-byte body, called from hmac_start_bss_in_available_channel
@ 0x4826C) **before** the userspace green_ap "enable" 0x0242 command.  Our
ladder never sends the g0-id1 key 0x0065 before the 0x0242: it sends the WAL
group-2 equivalent (ap_vap_start, plan step 8) *after* ap_cfg, i.e. after the
0x0242 already failed.  mctx[0x0E] is therefore 0 at pre-start time, CA 0x4AE7C
returns 0, CA 0x4C3FC stores 0xFF, and CA 0x4AF0C refuses at 0x4AF32.
**Fix: post the 12-byte key-0x0065 VAP start (field B = the same module index)
between the 0x0064 VAP add and the 0x0242 band start.**  No further VAP-add field
is missing.

## 1. Inputs

| item | value |
| --- | --- |
| firmware | FIRMWARE.bin 928,920 B sha256 7fc87e20...311b (local pinned copy); CA = file offset + 0x40000; RAM phys = 0x40000000 + CA + 0x6B8000 |
| host module | hi5622v100_wifi.ko (rootfs-2.4.15), 3,581,748 B sha256 22fa789a...; ELF with symbols; read in place |
| firmware objdump | OpenWrt staging toolchain arm-openwrt-linux-objdump, -D -b binary -m arm -M force-thumb --adjust-vma=0x40000 |
| decode inputs | ../wifi-band1-start TABLES T1/T2/T4/T5/T8; ../wifi-band1-start-send-2 REPORT sections 4-5; ../wifi-fw-chan-worker TABLES T5 (the 5 GHz RF branch CA 0x40684 and gate CA 0x944DE); ../wifi-vap-start TABLES T3/T5/T6/T8 |
| fired state under analysis | R3b 2026-11-28: vap_add post index=0 band=1 ctx=0x00119758 perlink=0x00175a18 bandobject=0x0015e570 bandlist_count=1 published=0x00175a18; band_start post state[1]=0 state[4]=0xff counter=0 |

## 2. CA 0x4C3FC - full logic (answers task question 1)

Decoded instruction-by-instruction in [TABLES.md](TABLES.md) T1.  The three
things the previous lanes did not record:

1. **Two early returns happen before CA 0x4AE7C and never write state[4]**:
   state[0] bit0 must be set (else log 0x409), and u8(bandobj+0xD6E) must
   resolve through CA 0x581C8, which only resolves id 0 (CA 0x943AC returns the
   global object CA 0x170E28 only for a zero argument; every non-zero id logs
   0x6D and returns 0) - i.e. **bandobj+0xD6E must be 0** or the helper exits at
   0x4C41A.  Our R3b state[4]=0xFF proves both gates passed (the 0xFF store is
   only reachable after them).
2. **mode == 2 is a failure by design** (0x4C442-0x4C444): it reaches the 0xFF
   store even when the resolver returns an object.  The three direct callers
   pass mode 2 only from the key-0x0067 VAP-down worker (0x46098).
3. The **success path is two-staged**: state[4] is stored first (0x4C448);
   state[1]=1 is stored only after the mctx[0x190]-based ring math succeeds
   (0x4C498-0x4C4DA).  If u32[mctx+0x190] or its +0x10 word is 0, state[4] is
   still written with the index but state[1] stays 0 and the 0x0242 worker's
   "state[1] != 0" gate then refuses the start.  That is the second possible
   failure signature after the 0x0065 fix and the recipe's decision table keeps
   it separate.

The failure branch calls CA 0x4C3AC *before* the 0xFF store.  CA 0x4C3AC is the
deferred-start rung: it requires state[0] bit0, resets pending work (CA
0x4C388 -> 0x41478), and calls **CA 0x4AF0C only when state[1] == 3**; it then
always clears state[1] to 0.  So in the R3b failure the 0x4C3AC call was a
no-op (state[1] was 0) - consistent with the observed readback.

## 3. CA 0x4AE7C - what it iterates/validates (answers task question 2)

[TABLES.md](TABLES.md) T3 is the compiled pseudo-code.  The points that matter:

* It reads the **band object** (the same bandobj the 0x0064 bind stored at
  mctx+0x4B8): count at bandobj+0x12, entries as a byte array at bandobj+0.
  That is exactly the list the key-0x0064 worker appends (0x44C18-0x44C32).
* count must be **exactly 1**; any other count returns 0.
* for the entry it resolves **mctx = 0xBCD7E(u8[bandobj+i])**.  This lane proved
  by literal-pool arithmetic that CA 0xBCD7E is the same accessor as
  **CA 0x961AE(idx) = CA 0xFE578(0, idx)**: table CA 0x171458 + 4*idx, base
  u16 CA 0x171504.  So the "VAP object" of the resolver is the **module
  context**, not the 0xFE6BC-allocated vap object at mctx+0x4B4.
* the entry is returned only when **mctx[4] == 2** (the create type stored by
  CA 0xFE6BC from body[0]) and **u8(mctx[0x0E]-1) <= 2** (state 1, 2 or 3).
* before that check it runs the 5 GHz gate **CA 0x944DE(mctx)**: bound band
  object nonzero; mctx[0x11] (band) == 1; mctx[0x440] (band-config flag) == 1;
  then returns u8[bandobj+0xEC8] (band-up flag).  When that returns 1 it calls
  **CA 0x101890(mctx[0x11], mctx[0x10], mctx[0x12])** and, if that returns 1,
  returns **0** (log 0x1F2) - the "the band is already up, re-programme the RF
  only" branch.  On a fresh boot mctx[0x440] and bandobj[0xEC8] are 0, and
  mctx[0x10] (channel) is 0, so this branch is not the R3b failure.

So the resolver needs **more than count == 1**: count == 1, a resolvable mctx,
mctx[4] == 2, and mctx[0x0E] in {1,2,3}.  In the R3b state exactly the last one
is missing: nothing in our ladder writes mctx[0x0E] before the 0x0242.

## 4. What the stock provides that we lack (answers task question 3)

**The key-0x0065 VAP start.**  The firmware worker CA 0x46394 (dispatch table A
row 2) has an AP branch that sets **mctx[0x0E] = 1** at CA 0x46418-0x4641A
before any per-link work; for create type 1 (STA) it sets 4 instead (0x464D8).
It also writes mctx[17] = body[2], mctx[18] = body[3], mctx[15] = body[1]
(via CA 0xD4048) and u32[mctx+0x444] = body[4..7] [TABLES T7].

The vendor host emitter is hmac_config_start_vap_event at .text+0x15AF88: it
sends a 12-byte body with body[0] = the start argument, body[1] = vap+0x0F,
body[2] = vap+0x11 (**band**), body[3] = vap+0x12, body[4..7] = u32 vap+0x414.
Its AP caller is hmac_start_bss_in_available_channel (0x4826C) - i.e. the start
event is part of the AP BSS-start path, before userspace ever issues the
green_ap command.

The green_ap side is decoded too: wal_hipriv_green_ap (0x114268) ->
wal_green_ap_analysis_process (0x11400C) -> wal_send_cfg_event with the 20-byte
buffer {u16 key 0x0242, u16 len 0x10, data[16]}; the command table
g_ast_mac_green_ap_config_table (.data+0x1AD8) maps names to the firmware
subcommands: info=0, **enable=1**, work=2, pause=3, log_en=4, free_cca=5,
free_gap=6, work_slot=7, pause_slot=8, and "enable N" writes data[2]=N.  The
R3b frame body {01,00,01,00,00} is the firmware view of "enable 1" - so the
fired 0x0242 frame itself was the right command [V].  What it lacked was the
prior VAP start.

Our ladder's gap is two-fold:
1. the only VAP-start it emits is the WAL **group-2** cmd 0x0065 frame
   (ap_vap_start, 44 B) as **plan step 8**, after ap_cfg - i.e. after the
   0x0242 band start already ran;
2. its frame body carries body[0]=1 / cookie only; the g0-id1 key-0x0065
   carrier the firmware worker reads (field B, body[1..7]) is the better
   carrier because it is the same one our proven 0x0064/0x0242 sends use.
   The sibling wifi-vap-start lane already has a byte-exact 40-B g0-id1
   key-0x0065 KAT (there with body[2]=0; for band 1 body[2] must be 1).

## 5. State[0..4] semantics (answers task question 4, first half)

[TABLES.md](TABLES.md) T9 is the full map with writers.  Summary for the
pre-start decision:

* state[0] bit0: armed by the 0x0242 subcmd-1 body[2]==1 before CA 0x4C3FC;
  required by CA 0x4C3FC and CA 0x4C3AC.
* state[1]: 0 idle / 1 pre-started (CA 0x4C3FC success) / 2 MAC started (CA
  0x4AF0C tail) / 3 deferred-start rung (band/channel state machine CA
  0x4C246; only consumed by CA 0x4C3AC and CA 0x4BDCE).
* state[4]: the bound VAP/module index (0..0x12) or 0xFF "none"; written only
  by CA 0x4C3FC.  CA 0x4AF0C's guards: state[1]==2 -> already started
  (0x4AF2C); state[4]==0xFF -> refuse (0x4AF32); CA 0x961AE(state[4]) == 0 ->
  refuse (0x4AF3A).
* state[5]/[6]/[7]: ring sizing written by the CA 0x4C3FC success branch;
  state[0x64] start counter (CA 0x4AF0C); state[0x6C]/[0x70] the 0x0242
  subcommand 5/6 payloads.

## 6. Read-only probes (answers task question 4, second half)

The existing bench-proven probe chain (SLOT0 CA 0x175458 / BASE CA 0x17A504 /
mctx+0x4B8 = bandobj, from ../wifi-band1-start-send-2) already resolves mctx
and bandobj.  Every new probe is **object-relative** and therefore independent
of the absolute-address convention: mctx+4, +0x0E, +0x10, +0x11, +0x12,
+0x190, +0x440, +0x4AC; bandobj+0x12, bandobj[0], +0x1A00, +0xD6E, +0xEC8,
+0x14D0..+0x14D4; perlink+0x6C6.  [TABLES.md](TABLES.md) T10 lists them as
gates G1-G14 with pass conditions.  The decisive pre-fire gate is G5
(mctx[0x190] != 0) because the key-0x0065 worker dereferences mctx[0x190] at
CA 0x46470 on the AP path; a zero there turns the fix into a firmware fault.
G12 (mctx[0x0E]) is the after-post proof that the 0x0065 event was applied.

## 7. Fire recipe in one line

Insert exactly one new frame family: the 40-B g0-id1 **key 0x0065** VAP-start
frame (field B = vap_add_index, body {00,01,01,00, 00,00,00,00, 00,00,00,00})
right after the key-0x0064 VAP add and **before** the 0x0242 band start; keep
every other byte/order unchanged; gate it on G1-G11/G13 read-only probes and
prove G12=1 before letting the 0x0242 run.  Full procedure and decision table:
[FIRE-RECIPE.md](FIRE-RECIPE.md).

## 8. Honest limits

* Static decode only.  The 0x0065 worker's per-link setup path is decoded only
  to the fault boundary (TABLES T7 step 10-11); the CA 0x4C3FC success
  sub-branch's [mctx+0x190]+0x10 source is [U] and is carried as the recipe's
  second observable.
* The vendor host module is a different binary than the firmware; its symbol
  offsets are used for the *host-side sequence* only.  The firmware worker's
  field offsets are re-derived from the firmware image itself.
* CA 0x4AE7C's "already-programmed" return-0 branch (0x944DE==1 then
  0x101890==1) is decoded from the instruction flow but cannot occur on a
  fresh start with mctx[0x440]==0 and mctx[0x10]==0; the recipe's probe gate
  G9/G11 keeps it closed.
