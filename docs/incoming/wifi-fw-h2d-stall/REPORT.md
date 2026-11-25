# The firmware H2D stall: `out[0] = 0x00000008` decoded, ranked hypotheses, discriminating recipe

Lane: **fw-h2d-stall** (shared task **task-181**), 2026-11-27. **Host-only**: static analysis of the
local firmware image and the local unstripped vendor modules; **no device contact, no build, no
vendor bytes committed**. This lane fires nothing - the Lead fires.

**Inputs (local evidence only; paths on the Lead's workstation):**
`recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B,
sha256 `7fc87e20…311b`), the PD4-patched copy `cfe24bfb…037b1` (recipe:
`../wifi-fw-audit/recipes/PD4.recipe.json`), the unstripped `hi5622v100_plat.ko` and
`hi5622v100_wifi.ko` (`recovery-extract/lib/hisilicon/ko/`), and the openwrt objdump
(`arm-openwrt-linux-objdump`). Reproduce with `tools/verify_decode.py`.

Evidence markers: **[V]** re-derived in this lane from the local artifacts; **[R]** carried from the
committed record (cited); **[I]** inference; **[U]** not decidable without a device read.

**One-line answer:** `0x00000008` is the H2D mailbox pending bitmap with **bit 3 set** - the
"one SR message announced, not yet consumed" bit. It is **not** a message id, not an error code and
not message-specific. The firmware dispatcher **clears out[0] before it calls any handler**
(`out[0]=0` at file `0x818be` precedes the dispatch loop), so an announce that *stays* at
`0x8` proves the firmware's dispatch service has not run since the host wrote it. The stall is
therefore almost always **downstream of the previous dispatch**: the previous message's handler /
worker faulted or wedged the dispatcher (or the PD4 poller itself), and the frame that is left at
`0x8` is just the first victim. **Read the firmware exception frame before blaming the message.**

---

## 1. The `out[0]` word: final decode

### 1.1 What the word is [V, R]

| item | value | evidence |
| --- | --- | --- |
| register | CA `0x40039010`, named `out[0]` in the clean driver | firmware init stores the pointer at ctx+0x4: file `0x977a`/`0x9784` [R `phase32/interrupt-status-and-doorbell-path.md` §4.5] |
| format | 32-bit **pending-bit bitmap**, one bit per mailbox service id (0..9; 0xFFFE bound) | firmware dispatcher file `0x818ca`-`0x818f2` [V]; host writer below [V] |
| producer | **host** - writes the accumulated pending mask, then ORs bit 0 into the doorbell | plat.ko `pcie_msg_send` [V] |
| consumer | **device** - one dispatcher reads, clears, and dispatches each set bit | file `0x818ac` [V, R] |
| steady-state value | every SR-carried HCC frame is announced with **id 3**, so the mask is `1<<3 = 0x00000008` | vendor `shuangta_ete_sr_dscr_fill` ends `pcie_msg_send(chip, 3)` [R `phase37/fw-ring-message-table.md` §1] |

So the bench string "announce NOT consumed (out[0]=0x00000008 after 44/51 polls)" means exactly:
**"bit 3 was written by the host and the device has not consumed it"** - one unserviced SR announce.
The calibration ladder, the AP sequence and the rate frame all use the same bit; the value 0x8 says
nothing about which frame it was or whether the frame is well formed.

### 1.2 Host side: who writes it, and what a refusal looks like [V]

`hi5622v100_plat.ko` (section-relative offsets; `.text` base `0x38`):

* **`pcie_msg_send` @ `0x160f4`** - the normal sender, and the exact model our clean driver
  mirrors:
  * rejects id > 9 (returns error `0x8b2d`), rejects a bit that is already pending;
  * keeps two masks in the pcie-msg object: pending (`+0x48`) and in-flight (`+0x44`);
  * if in-flight is non-zero: **coalesce** (do not touch the device, return id);
  * else: `inflight = pending|1<<id`; **`*out[0] = inflight`**; `doorbell (CA 0x400392d4) |= 1`;
    unlock and return.
* **`pcie_msg_wait_for_clr` @ `0x173dc`** - the vendor's only "wait for consume": a **tight
  poll of the word at `*[obj+0x2c]` (the out[0] slot)**, bound **0xFFFE iterations**, no delay,
  returns `0x8b2d` on expiry. A full `.text` sweep finds **exactly one call site**:
  `bl 0x173dc` at `0x17508`, inside:
* **`pcie_msg_send_irq` @ `0x174a8`** (= the HCC channel send callback registered by
  `pcie_msg_init`): it **waits for out[0] to clear before publishing the next mask**:
  `*[obj+0x40] = 8`; `pcie_msg_wait_for_clr(obj+0x2c)`; on success publish `out[0] = pending`
  and `doorbell |= 1`; on timeout log and return the error.

  Consequences for bench interpretation: (a) **the vendor's flow-control wait is for the *previous*
  announce to clear, not for a response to the new frame**; (b) on timeout the vendor keeps the
  message **unsent** (the new mask is never published) and the caller gets an error - the mask that
  the vendor's wait target holds is the *previous* frame's pending word; (c) the wait is a tight
  spin much larger than our bounded window but with no sleeps. `[obj+0x40]` is a device-register
  pointer whose CA identity is **[U]** (not resolved in this lane; it is not the out[0] slot).
* **`wal_check_and_release_msg_resp` @ `0x109370` (wifi.ko)** - **not a wait**: it reads the
  saved status word from the response object, `kfree()`s it and returns the status (logging if
  non-zero). The vendor's *response* wait lives in the caller (`wal_send_cfg_event` @ `0x126c98`
  allocs the cfg event, sends via `wal_send_cfg_event_msg_tx`, then handles the response
  bookkeeping). So "response-to-response" is **not** the thing that blocks the mailbox; the mailbox
  block is a plain out[0]-not-cleared condition.

**Our clean driver is host-side correct**: `wifi-hcc-core/src/hcc_core.c` publishes
`out[0] = mbox_inflight`, ORs the doorbell, coalesces pending/in-flight exactly like the vendor,
and polls `out[0]` in `hcc_core_mbox_tick()` (bounded by `mbox_clear_timeout_ms`). Its
"lane closed" is our own fail-closed state, not a device protocol state.

### 1.3 Device side: the dispatcher, and the clear-before-dispatch rule [V, R]

Firmware H2D dispatcher, file `0x818AC` (verified instruction by instruction with the openwrt
objdump; also `phase37/fw-ring-message-table.md` §1 and `phase32/…doorbell-path.md` §4.5):

    push {r3-r7,lr} ; r6 = r0 (ctx = CPU 0x10C190)
    cbz  r0 -> return
    *ack      (ctx+0x0c = CA 0x400392f0) = 1
    r5 = *out[0] (ctx+0x04 = CA 0x40039010)      <- read the pending bitmap
    *out[0] = 0                                  <- CLEAR IT, BEFORE any handler
    *(ctx+0x10 = CA 0x400392d4) = 8              <- doorbell re-arm, the firmware's own ack word
    while (r5) {
        id = 31 - clz(r5 & -r5)                  <- lowest set bit
        if id > 9 -> return
        fn = table[id].fn (table = *(ctx+0x20)); if (fn) fn(table[id].arg)
        r5 &= ~(1<<id)
    }

The handler table is RAM, registered at runtime (ids **1, 3, 5, 6**; entry = 8-byte
`{fn, arg}`): id 1 wakes the global message queue, **id 3 is the SR announce** (sets the pcie
event word, then runs the firmware message worker), id 5 is a deliberate `BUG()` trap, id 6 is a
device-reply primitive. The dispatcher is entered **only** from the thunk at file `0x294`
(`ldr r0,=0x0010C190 ; b.w 0xC18AC`) [V: the only branch to `0xC18AC` in the image]. That
thunk is IRQ id **0x4C**'s registered handler in the stock firmware (IRQ entry file `0x64` ->
`0x82efc` -> fn array `0x17d430` -> file `0x294`) [R phase32 §4]. On our images the
interrupt is instead serviced by the **PD4 poller**:

* PD4 patches the boot-task success epilogue (CPU `0x4916A`) to branch into a 64-B stub at CPU
  `0x108240` (file `0xC8240`). The stub waits until the hcc instance is initialised
  (`*(0x10C19C) != 0`), then loops: poll `*(CA 0x40039010) != 0`; call the same thunk
  `0x294`; repeat. Before the handler table is installed it substitutes the mask-ROM RAZ page
  `0x2000` for the static table sentinel `0x118D68`, so the pre-init dispatch is
  **consume-only** (ack/clear/ack, no handlers); when the live table pointer is installed it does the
  real dispatch and restores the pointer [V stub decode; R `../wifi-irq-unmask/REPORT-PD4.md` §3.2].
* This is why a **fault inside a handler/worker is synchronous with the poller**: the poller is the
  boot task that called the dispatcher. A handler/worker that faults (or blocks) never returns, the
  poller never loops back, and every later host announce simply sits in out[0] at `0x8`.

**The rule that frames every hypothesis:**

> out[0] is cleared before the dispatch. If the host reads `0x8` after N polls, the device has
> not entered the dispatcher since that write. The frame under test was **not** dispatched, so it
> cannot itself be the thing that failed. The failure happened in the **previous** dispatch (or the
> poller is wedged/dead).

There is a second, subtle invariant: between "host writes out[0]" and "device clears it", the device
can be inside an earlier handler (e.g. the worker draining the previous SR frames). That is the
window in which a slow-but-alive path also produces `0x8`; §4 H4 covers it and the recipe
separates it from a fault.

---

## 2. What the message bodies do: the two handlers behind the two bench stalls [V]

The HCC body is the 12-byte HCC header + payload carried in the SR node (group = hdr[0]&0xf,
id = hdr[6:8] u16). The firmware's own group-0 table (40 entries) is at CA `0x10CCFC`
(`../wifi-h2d-catalog/REPORT.md` §4.3 [R], re-joined here). The two bench-stalled messages land
in these handlers:

### 2.1 g0 id7 `hmac_user_add` -> handler file `0x9832C` (CA 0x0009832D) [V partial]

Prologue: `push; sub sp,#0x4c`; fetch/validate the message header (`bl 0x409ea`); then

* look up an object by **hdr[3]** (message field B = the module/context id): `bl 0x961ae`;
  if NULL -> log + return 100 (a clean error, no dereference);
* check object fields `+0x4b8` and `+0x4b0` for non-zero (clean error otherwise);
* look up the user by the u16 at **hdr+0xC** (payload+0) via `bl 0x94588`; NULL -> clean error;
* only then write into the context/object and call deeper helpers.

So the handler is **precondition-guarded**: it needs an existing context (field B) and an existing
user/vap object. Posting it **before the VAP exists** (which our driver did at interface creation
in the 2026-11-26 23:00 fire; the vendor never posts g0 id7/id8 there - `../wifi-5ghz-vap-msg/`
lane finding [R]) is a precondition violation whose failure mode is a clean error *or* a deeper
write into a boot-zeroed context (see §2.3). The matching post-VAP post was consumed in 0 polls in
the 00:30 fire, so the wire is fine; the precondition/order is what differs.

### 2.2 g0 id2 `hmac_config_alg_send_event` -> handler file `0x96068` [V]

Prologue: header fetch `bl 0x409ea`; object lookup by **hdr[3]** (`bl 0xbc5d6`); NULL -> log
+ return 100. Then:

* scan a static descriptor table (entry stride 8, ids at entry+0x1b0, fn at entry+0x1b4) for
  **key == u16 at hdr+0x14 (payload+8)**; the terminator/marker key is `0x1451`; not found ->
  return 1000;
* call `fn(ctx, u16 at payload+0xA (declared blob length), pointer payload+0xC (blob), w0 = u32
  payload+0, w1 = u32 payload+4)`.

The proven calibration carrier and the failing rate frame are the **same frame layout**:
`12-byte HCC header | 8-byte token | key u16 0x0101 | blob length u16 | blob | 4-byte pad`
(calibration: 270-byte blob, total 298 B, proved consumed + answered on the bench [R
`../wifi-proto-seq/recipes/cal-alg-ladder.json` P3]; rate frame: 11-byte alg_cfg blob, total
39 B [R `../wifi-rate-ctl/REPORT.md` §4]). The transport/handler layer reads nothing outside the
frame in either case, so **there is no framing/length reason for the 39-B frame to stall the
mailbox**. If the 39-B frame wedges anything, it is inside the per-key alg worker (firmware-side
cfg-id dispatch) or - far more likely given §1.3 - the firmware was already dead from the previous
dispatch.

### 2.3 The known, bench-proven fault of exactly this class [R, decisive]

The PD4 lane proved (and the bench confirmed with mask `0x10`) a **precise data abort inside a
message worker**: a g0 id1/TXQ-sync frame whose field B names a context that exists (boot-populated
pointer) but whose `ctx+0x190` is still 0 (only a context-create message writes it) runs
`ldr r3,[ctx+0x190]` (=0) then `strb r1,[r3,#0x4c]` -> **data abort at PC 0x81040, fault
address 0x4C, mask 0x10** (`../wifi-irq-unmask/REPORT-PD4.md` §6.4). The crucial shape:

1. the announce is **consumed first** (dispatcher clears out[0], host logs "consumed 0 polls"),
2. then the worker faults inside the same dispatcher call,
3. the poller never returns -> the **next** announce stays at `0x8`,
4. page ends golden:0, mask 0x10.

That is exactly the signature the two bench stalls produced (`out[0]=0x8` after 44/51 polls,
then the exception), except nobody read the exception frame, so the culprit message was blamed.
The PD4 report also already names the fix direction: the **context-create must precede** the
TXQ/config messages, and the calibration ladder's proven sequence is exactly that
(`h2d_ctx_create` type **1** -> read back `ctx4+0x190 != 0` -> `h2d_txq_sync72` consumed ->
then the alg frames) [R `../wifi-proto-seq/recipes/cal-alg-ladder.json` P0-P3].

---

## 3. The two bench stalls, re-read under the decode (what they most likely were)

### 3.1 5GHz AP `user_add` (2026-11-26 23:00): the pre-VAP post

Sequence recorded: ladder rc=0, C2 (ctx_create + txq_sync72) "consumed 0 polls",
then **the interface-creation user_add (g0 id7/id8) was posted before the VAP add** ->
"announce NOT consumed (out[0]=0x00000008 after 51 polls)" -> lane closed -> firmware exception
(mask 0x10, page golden:0). In the fixed 00:30 fire the **same message posted after vap_add was
consumed in 0 polls** and the AP reached AP-ENABLED with page golden. So the wire/announce is fine.
Under §1.3 the 0x8 announce itself was never dispatched: the firmware had already stopped inside an
earlier dispatch (the C2 TXQ path is the §2.3 fault candidate; the pre-VAP user_add is the
precondition candidate - see H2). Either way **the stall is a precondition/fault problem, not a
transport rejection**.

### 3.2 RC `rate_cfg` (2026-11-27 07:00): the short alg_cfg blob

Sequence recorded: rate_ctl module loaded, ladder rc=0, "C2 consumed", then R1 SET rate_mode auto
built the exact 39-B g0 id2 key-0x0101 frame -> "H2D rate_cfg announce NOT consumed
(out[0]=0x00000008 after 44 polls, lane_closed=1)" -> refused -110 -> page golden:0 (exception).
Two facts point away from the short blob: (a) the frame layout is the proven calibration layout
with only the blob length changed (§2.2), and the same carrier/id/key is bench-proven; (b) the
**preceding C2** is the documented fault class: the rate-ctl fire recipe prescribes
`echo 3 > h2d_ctx_create` (`../wifi-rate-ctl/FIRE-RECIPE.md` R1 line 87 - its own comment says
"type 1 = the bench default" while the command writes **3**), whereas the calibration ladder that
was proved clean used type **1** and read back `ctx190 != 0` before the TXQ sync. A type-3
context-create may not have initialised the context that the subsequent field-B-4 TXQ sync needs,
i.e. the §2.3 data abort during C2 - after which the R1 announce could only sit at 0x8. **[I-high;
the exact type used on the fire is not recorded in the ledger - see recipe R-D1/R-D2.]**

---

## 4. Stall hypotheses, ranked (mechanism / prediction / falsifier)

### H1 (top) - downstream of a synchronous fault in the previous dispatch
Any handler or worker that faults inside the dispatcher call stops the PD4 poller. The next announce
sits at 0x8, the page loses golden, mask != 0. The known instance is the §2.3 NULL-`ctx+0x190`
data abort (PC 0x8103C/0x81040, mask 0x10); other handlers/workers can fault on a missing
precondition the same way.
*Predicts:* exception frame non-zero with a PC inside a handler/worker; mask 0x10 (DABT) or other;
the stall reproduces on the **second** message after a clean one, not the first;
the frame under test never reached its handler (no D2H answer).
*Falsifier:* exception staging all-zero and mask 0 after a stall (-> H4), or the stalled frame was
consumed and the fault came later (-> H3/worker-side).

### H2 - message precondition missing (context / VAP object not created)
The firmware handlers are precondition-guarded (g0 id7 needs an existing context by field B and a
user/vap object; g0 id2 needs the object by field B and a descriptor key). A boot-fresh context is
non-NULL but zeroed (`ctx+0x190 == 0`), so a handler can take a "valid pointer" path and then
dereference zero. This is the specific PD4-proven failure family.
*Predicts:* order matters and is reproducible - the same frame posted after the required create
consumes in 0 polls (already observed for the 5GHz user_add), posted before it fails;
a context-create + readback before the frame fixes it.
*Falsifier:* the proven-order control also stalls.

### H3 - the stalled frame's own processing wedges the firmware (content-specific)
If the frame *was* consumed (host log says 0 polls) and the firmware dies right after, the fault is
in that frame's handler/worker (e.g. an alg cfg id the firmware does not expect in this state, or a
per-key worker writing a zeroed structure). Note: this shows up as the *next* announce stuck, and
the decisive evidence is the exception PC, not the out[0] value.
*Predicts:* the fault PC maps to the g0 id2/g0 id7 handler or its worker; the calibration control
frame on the same boot is clean; the failure is blob-content-dependent across boots.
*Falsifier:* exception PC outside those handlers / clean control frame also fails.

### H4 - slow synchronous path (firmware busy), not a wedge
The dispatcher can be busy inside a long synchronous worker, or the PD4 poller can be busy with the
previous message, when the host's bounded window (44-51 polls) expires. The vendor would have waited
up to 0xFFFE tight polls (~thousands of reads) before declaring an error, and would *not* publish the
new mask (pcie_msg_send_irq, §1.2).
*Predicts:* out[0] eventually reads 0 if the wait is extended (hundreds of ms - seconds); page stays
golden, mask 0; no exception frame.
*Falsifier:* out[0] never clears over the extended window and/or exception frame non-zero.

### H5 (low) - host-side announce misuse / coalescing race
Checks against the vendor model: our mask write/coalescing/doorbell match `pcie_msg_send`
exactly [V §1.2], the device consumes the same word in the calibration ladder, and nothing in the
host path can make a written bit disappear. The only host-side contribution is the *bounded* window
(H4) and the fail-closed lane close afterwards.
*Falsifier:* a single frame posted from a quiet, calibrated state also stalls (would point back to
H1/H2 rather than to the host).

### [U] items left open (state them, do not paper over)
1. The CA identity of `[obj+0x40]` in `pcie_msg_send_irq` (the host writes 8 there before
   its flow-control wait). It is not the out[0] slot; not needed for the stall decode.
2. Whether the stock firmware's native IRQ id 0x4C path ever fires on our host. The PD4 poller
   exists because it did not; if it ever does fire concurrently, PD4's pre-init/consume-only guard
   and the poller's restore are designed to make it harmless, but that has never been observed.
3. Which `h2d_ctx_create` type the RC fire actually used (ledger silent; the recipe says 3, the
   proven calibration ladder used 1). R-D1 below resolves it.
4. Whether the firmware message worker runs synchronously inside the dispatcher for every message
   class. The PD4 bench observation (out[0] cleared, then the worker faulted -> next announce stuck)
   is only consistent with the poller being the blocked context; the exception-frame read makes it
   unambiguous.

---

## 5. The discriminating recipe

See `FIRE-RECIPE.md` for the runnable ladder. The single decisive observable is the **firmware
exception record**, which PD4 preserves at CPU `0x4000C..0x4004F` (BAR0 `0x6F800C`, 17
words: r0-r12, sp, lr, PC, SPSR), together with the mask CA `0x40008` and the page. One
read-only read after a stall converts "the message is bad" into "the previous dispatch faulted at
PC X" - the difference between H1, H2 and H3. The recipe's design:

* **R-D1 / R-D2 (cause vs victim):** reproduce with the calibration-proven C2 (ctx_create type 1,
  read `ctx4+0x190 != 0`, txq_sync72 consumed, mask 0), then ONE candidate frame; if it stalls,
  read the exception record and decode the PC.
* **R-D3 (content vs precondition):** the 298-B calibration control frame then the 39-B rate frame
  in one boot (two messages); if the control is clean and the rate frame stalls, H3; if the control
  also stalls, the C2/order (H1/H2).
* **R-D4 (order control, 5GHz):** pre-VAP user_add vs post-VAP user_add (already a natural
  experiment) with the VAP-object readback non-zero for the post-VAP arm.
* **R-D5 (dead vs busy):** extend the out[0] wait to ~2 s and post a payload-free g0 id0 stub frame
  (handler 0x41695 logs and returns, no context dereference) after the stall: consumed -> the poller
  is alive (H4); not consumed -> the poller is dead/wedged (H1/H2).

**Do not** retry in place after a stall (lane closed, page may be golden:0). One boot = one
hypothesis; power-cycle between arms; the Lead fires with the recovery door armed.

---

## 6. What this means for the driver (actionable, no code changed here)

1. **Never classify `out[0]=0x8` as a message rejection.** It is a device-side consumption
   timeout; the first diagnostic is the exception record + mask, and the suspect is the *previous*
   dispatch.
2. **Keep the proven C2 exact**: context-create type 1 -> verify `ctx4+0x190 != 0` ->
   txq_sync72 consumed -> then any alg/TXQ-bearing frame. Do not use unproven ctx_create types on a
   fire that matters.
3. **Treat `h2d_ctx_create` type as a protocol value, not a knob**: the field-B context is a
   precondition for later frames; the vendor creates it before use, and the boot only leaves zeroed
   bodies.
4. **Post order-bearing messages only in the vendor's order** (VAP first, then its users; context
   first, then TXQ). Where the vendor never sends a message at interface creation, we should not
   invent it there.
5. The bounded out[0] window should stay fail-closed but be generous enough to separate "slow" from
   "dead" (the R-D5 window), and the error path should capture the exception record before the
   power-cycle.

---

## 7. Provenance / scope

* All analysis is host-side, read-only, on local artifacts; no firmware bytes, no vendor object
  bytes and no calibration data are committed. `tools/verify_decode.py` re-derives the quoted
  offsets, mnemonics and symbol addresses from the reader's own local copies.
* Committed record this lane builds on (read, not re-derived): `../wifi-irq-unmask/REPORT-PD4.md`
  (PD4 patch + the fault model), `../../phase32/interrupt-status-and-doorbell-path.md`
  (doorbell -> dispatcher chain), `../../phase37/fw-ring-message-table.md` (dispatcher/table),
  `../wifi-h2d-catalog/REPORT.md` (firmware HCC group tables), `../wifi-rate-ctl/REPORT.md` +
  FIRE-RECIPE (the 39-B frame), `../wifi-proto-seq/recipes/cal-alg-ladder.json` (the proven
  298-B calibration frame and C2 order), `../wifi-5ghz-vap-msg/` (vendor never posts user_add at
  interface creation).
* No bench contact was made by this lane; no fire was executed.
