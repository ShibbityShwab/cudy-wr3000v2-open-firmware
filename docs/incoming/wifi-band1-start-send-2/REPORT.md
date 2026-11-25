# REPORT - the explicit VAP/module index of the key-0x0064 VAP add (R3b iteration 2)

**Lane:** wifi-band1-start-send-2 (shared task **task-207**).  **HOST-ONLY**:
implementation + host build + host tests + firmware decode; no device contact,
no write, no fire.  **The Lead fires.**  Companions: [README.md](README.md),
[TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[BUILD-NOTES.md](BUILD-NOTES.md), [patches/0001-vap-add-index.patch](patches/0001-vap-add-index.patch),
[tools/](tools/), [src/](src/), [evidence/](evidence/).

Grades: **[V]** re-derived from the committed sibling decodes/artifacts or
this lane's host tests; **[V\*]** bench-measured by a committed lane; **[I]**
inference; **[U]** needs the fire.

---

## 0. Answer in one paragraph

The R3b fire (2026-11-28, ../wifi-band1-start-send/FIRE-RECIPE.md) posted the
correct 33-B key-0x0242 band-start frame and the firmware refused it at its
R3b precondition: the per-band run-state readback
`band_start post: module=0 bandobject=nonzero state[1]=0 state[4]=0xff
counter=0` (CA 0x4AF0C early-out at 0x4AF32).  The Lead's hypothesis was that
our key-0x0064 VAP add does not set its body **data[4]** (the VAP/module index
the 0x0064 worker binds).  This lane **verified that hypothesis is not true
for the frame that was fired**: the fired builder already emits
`data[4] = 0` (and `data[0x12] = 1`), proven byte-exactly with the real
portable builder in the host test.  What was genuinely missing in the code is
that the index was **implicit** (it followed the `vap_start_module` knob) and
the frame's header **field B (hdr[3]) was not tied to it**, so the bind ctx
and the start ctx could diverge.  This lane makes the index explicit
(`vap_add_index`, read-only, default 0), writes it into data[4], data[1]
(self-parent) and hdr[3], fail-closes the start before any frame on
`index > 0x12` or `index != vap_start_module`, and adds a **read-only
post-add readback** of the band-object bind and the band VAP list.  That line
is the decisive next-fire evidence: it separates "the 0x0064 add did not
bind" from "the state[4]=0xFF is produced later by the 0x4C3FC/0x4AE7C
band-list scan".  At the fire defaults every emitted 0x0064/0x0242 byte is
unchanged; the host artifact is 373,056 B, sha256 `b8f6fe35...`, 64 params,
283 host checks / 0 failures.

## 1. Inputs

| item | value |
| --- | --- |
| fired base tree | /tmp/bandstart-build - the tree that built the fired 0x0242 module (/tmp/bandstart-artifact/hi5622_wifi.ko, 371,212 B, sha256 `e2312402...a028`, 63 params) |
| base lineage | F2 fire module (364,184 B `12427b86...`) + ../wifi-band1-start-send/patches/0001-band-start-send.patch |
| decode inputs | ../wifi-band1-start TABLES T1/T2/T4/T5/T8 (CA 0x4AF0C, its call sites, the 0x0242 worker, the 0x0064 bind), ../wifi-ctx-create REPORT (worker CA 0x449E4 field semantics; hdr[3] == data[4] rule), ../wifi-vap-start TABLES/recipes (the key-0x0064 carrier and body fields) |
| firmware | the pinned local FIRMWARE.bin (928,920 B, sha256 `7fc87e20...311b`); read-only host decode, no byte committed |
| build | 6.18.60 kernel tree + arm-openwrt-linux-muslgnueabi gcc 14.4.0 (OpenWrt staging) |
| artifact | /tmp/vapbound-artifact/hi5622_wifi.ko (LOCAL ONLY, never committed) |

## 2. The fired frame already carried data[4] = 0 [V]

The fired 0x0242 module's provider (hi5622_vapscan_provider.c) builds the
g0-id1 key-0x0064 frame as
`vsf_build_vap_add(iftype=AP, module = p->module, parent = p->module,
band = p->band)`; the AP-start caller passes
`p->module = hi5622_vap_start_module` (default 0, and the fire recipe sets
`vap_start_module=0`).  The builder writes `data[0]=iftype(2)`,
`data[1]=parent(0)`, `data[4]=module(0)`, `data[0x12]=band(1)`.

This lane re-derives it with the **real** builder from ../wifi-vap-start/src/
and the committed KAT is (evidence/frame.txt):

    index 0, band 1 (the fire): 000100003000010000005a5a0000000001000000640014000200000000
                                0000000000000000000000000000000100ffffffff
    body: 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00
          ^type       ^data[1]  ^data[4]=0 (SET)                          ^data[0x12]=1

So the Lead's "OUR vap_add does NOT set data[4]" is **not** the fired frame:
the byte is present and zero.  The lane records this correction instead of
re-implementing an already-set field.

## 3. The genuine one-field-class defect: the index was implicit and the header field B was not tied to it [V]

The key-0x0064 config event is resolved by the firmware dispatcher through
**hdr[3] (field B)**: CA 0x43D58 fetches ctx = CA 0xFE578(0, hdr[3]) and then
calls worker CA 0x449E4(ctx, len, body).  The worker itself selects the
created/bound module from **body data[4]** (CA 0x44A24 -> 0xBC726 -> CA
0x44A4C -> CA 0x961AE = ctx lookup(0, data[4])).  The bench-validated ctx
create rule (../wifi-ctx-create REPORT §3) is explicit: "the same message
should use hdr[3] = data[4] (the handler's lookup must be the same module)".
The later key-0x0242 worker resolves its own ctx from its field B and takes
the band object from ctx+0x4B8 - the object the 0x0064 add bound.

In the fired base the provider never set `field_b`: every 0x0064 frame was
posted with hdr[3] = 0 (the builder default) while data[4] followed
`vap_start_module`.  With the fire defaults (0/0) the bytes coincide, which
is why the frame looked correct; any non-zero `vap_start_module` would have
posted a frame whose dispatcher ctx and created ctx differ.

The fix (this lane's patch):

1. new read-only parameter **`vap_add_index`** (uint, 0444, default 0 = the
   single AP): the VAP/module index;
2. the key-0x0064 builder writes it into **data[4]**, **data[1]** (self-parent;
   the parent gate CA 0x44A10 requires parent_ctx[1] == 0, true for the fresh
   target ctx) and **hdr[3]** (field B);
3. the provider refuses if the requested module differs from the parameter;
4. start_ap refuses **before any frame** when `vap_add_index > 0x12` or
   `vap_add_index != vap_start_module` (the bind ctx and the 0x0242 start ctx
   must be the same index);
5. after a successful key-0x0064 post the driver logs a read-only snapshot of
   the bind and the band VAP list (section 5).

At default 0 the emitted frames are byte-identical to the fired base.

## 4. Where state[4] comes from - the decode that redirects the next rung [V]

Host decode of the pinned firmware (objdump -M force-thumb; CA = file offset +
0x40000):

* **CA 0x4C3FC is the only writer of `state[4]`** (the per-band run-state at
  bandobj+0x14D0; state[4] is the bound VAP/module index).  The key-0x0242
  subcmd-1 worker (CA 0x4C524) calls it before its start gate.
* Success path: CA 0x4C436 calls **CA 0x4AE7C(bandobj)**; when it returns
  non-zero, CA 0x4C448 stores its byte 0 into `state[4]`
  (`ldrb r0,[r0,#0]; strb r0,[r4,#4]`), then the helper fills the buffer
  parameters and writes `state[1] = 1` (CA 0x4C4DA).
* Failure path: CA 0x4C4E6 calls CA 0x4C3AC and then
  `movs r3,#0xff; strb r3,[r4,#4]` **(CA 0x4C4EE-0x4C4F0) - the observed
  state[4] = 0xFF**.  The worker's own start gate then fails because
  state[1] stays 0.
* **CA 0x4AE7C** (the band VAP resolver) first reads `bandobj+0x12` (the band
  object's VAP list count written by the key-0x0064 worker) and requires
  **exactly 1** (CA 0x4AE84-0x4AE86); it then resolves the list entry, tests
  the band gate CA 0x944DE, calls the RF programme CA 0x101890, and returns 0
  (with a log) when that branch succeeds (CA 0x4AEC6-0x4AED2) or when no
  entry matches `vap[4]==2 && vap[14] in 1..3` (CA 0x4AED4-0x4AEE2).
* The band VAP list is appended **only by the key-0x0064 worker**, at CA
  0x44BF8-0x44C32 (`bandobj[0x12]++ ; bandobj[count] = data[4]`), after the
  bind stores CA 0x44BBC-0x44BC6 (ctx+0x4B0 per-link, +0x4B8 band object,
  +0x4B4 VAP).  The bind branch requires the published per-link word
  `*(bandobj+0x1A00)` to be non-zero (CA 0x44B6C-0x44B7C).

Consequence: `state[4] = 0xFF` localizes to **either** (a) the 0x0064 worker
stopping before the list append (then the band list is empty and/or ctx+0x4B8
is zero), **or** (b) CA 0x4AE7C's own gate/RF branch refusing an existing
single entry.  The committed 0x0242 readback (state[1]/state[4]/counter) cannot
tell them apart; the new readback in section 5 can.

## 5. The decisive new evidence: the read-only post-add readback [V]

After a successful key-0x0064 post in the beacon_static start_ap path the
driver logs (read-only firmware RAM reads through the existing
`hi5622_fw_ram_read`; no wire byte, no gate):

    cfg80211: vap_add post: index=%u band=%u ctx=0x%08x perlink=0x%08x
              vap=0x%08x bandobject=0x%08x bandlist_count=%u published=0x%08x (%s)

Reads: ctx = module table slot(index) + base; perlink/vap/bandobject =
ctx+0x4B0/+0x4B4/+0x4B8; bandlist_count = u8 bandobj+0x12; published =
bandobj+0x1A00.  Verdicts (host-tested strings):

| observed | verdict | next rung |
| --- | --- | --- |
| ctx unresolved | module ctx unresolved | wrong index / module table not populated |
| bandobject = 0 | the key-0x0064 bind did NOT run | fix the 0x0064 frame/preconditions (data[4]/hdr[3]/band/published) |
| count = 1 | bound; band VAP list count=1 | the later state[4]=0xFF is in the 0x4C3FC/0x4AE7C path, not the VAP add |
| count = 0 | band VAP list EMPTY | the 0x0064 worker stopped before the list append |
| count > 1 | list count>1 | CA 0x4AE7C needs exactly one entry - duplicate add |

## 6. Host tests [V]

`./tools/run-host-tests.sh` (evidence/host-tests.txt): **283 checks, 0
failures**:

| part | checks | content |
| --- | ---: | --- |
| src/vap_index_selftest.c | 260 | gate matrix; plan matrix; byte-exact key-0x0064 frames for index 0 and 4 (data[4], data[1], hdr[3], data[0x12], length, key, total, tag); the frames through the real builder validator; key-0x0242 field B == the same index for every index 0..0x12 (33 B, key, body {01,00,01,00,00}); the readback verdict strings |
| tools/vap_add_frame_test.py | 23 | independent re-derivation of the two KATs from the carrier geometry only; source contract on the patched tree (data[4]/data[1]/hdr[3] hunks, both fail-closed gates, the readback line+count, the 0x0242 field-B write from the plan module, the 0x0064 post before the 0x0242 plan) |

No device contacted.

## 7. Artifact [V]

| field | value |
| --- | --- |
| stripped | /tmp/vapbound-artifact/hi5622_wifi.ko, **373,056 B** |
| sha256 | `b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71` |
| unstripped | 1,402,780 B, sha256 `28e14fb4a5dd8d1c60985ab001b218e94dd32544796a2872306a3a570237c1dd` |
| .text sha256 | `ff4fb171e16212ee4b7038b15855ac14f402d3e2b30c7ac64d6194955a8ae99c` |
| params | **64** (63 fired base + `vap_add_index`) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0; debug-strip keeps .text identical |
| cfg80211 symvers | 12 lines |
| size delta | +1,844 B vs the 371,212 B fired 0x0242 artifact |

## 8. Honest limits [U]

* **Not fired.**  Whether the explicit index changes the outcome, and whether
  the readback shows `bandlist_count=1`, is the Lead's fire.
* **At the fire defaults the emitted bytes are unchanged** (index 0 was
  already written).  The fire's new observables are (a) the `vap_add post:`
  readback line and (b) the refusals when the operator varies the index
  (negative controls in FIRE-RECIPE R4).
* **The firmware decode is static.**  CA 0x4AE7C's exact return semantics
  under the 0x944DE/0x101890 branch pair are read from the instruction flow
  (CA 0x4AEB4-0x4AEE2); a stock H2D trace of a 5 GHz AP start would settle
  which condition the stock hits.
* **state[4] is a firmware-RAM proxy.**  The driver does not write it; only
  the 0x0242 worker's CA 0x4C3FC can.  The fire probes stay read-only.
* **The merged image-#3 module** (64 params, sha ...) does not contain this
  lane; a fire needs this lane's artifact or a rebase onto the merged tree
  (the patch touches only hi5622_scan.c and hi5622_vapscan_provider.c).
