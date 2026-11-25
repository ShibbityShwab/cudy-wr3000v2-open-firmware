# wifi-band-start-prestart - why state[4] stays 0xFF when the VAP is bound

**Lane:** wifi-band-start-prestart (shared task **task-208**).  **HOST-ONLY**:
firmware/host-module decode + read-only probe design + fire recipe; no device
contact, no write, no fire.  **The Lead fires.**  Companions:
[TABLES.md](TABLES.md), [REPORT.md](REPORT.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[tools/fw_prestart_re.py](tools/fw_prestart_re.py), [evidence/](evidence/).

Grades: **[V]** re-derived from the pinned artifacts by this lane; **[V\*]**
bench-measured by a committed lane; **[I]** inference; **[U]** needs the fire.

---

## 0. Answer in one paragraph

The 0x0242 band-start pre-start **CA 0x4C3FC** is *not* a count check: it calls
the band VAP resolver **CA 0x4AE7C** and writes state[4] = entry[0] only when
the resolver returns a non-NULL band-VAP object.  The resolver does need
bandobj+0x12 == 1 (the R3b readback's count=1 satisfies it), but for the single
entry it then requires the **module context** to have mctx[4] == 2 (AP create
type) **and mctx[0x0E] in {1,2,3}** (the VAP started/up state).  mctx[0x0E] is
set to 1 in its AP branch by the key-0x0065 VAP start worker CA 0x46394 (CA
0x46418-0x4641A; CA 0x4AF0C later normalises a 2 to 1, and the non-AP paths
write 4).  The stock sends that 0x0065 event as
part of its AP-start path (host module hmac_config_start_vap_event, 12-byte
body); our ladder sends the equivalent only as a **late WAL group-2** frame
(ap_vap_start, plan step 8, *after* ap_cfg) - i.e. after the 0x0242 has already
failed.  So with count=1 and a fully bound VAP, mctx[0x0E] == 0 makes the
resolver return 0, CA 0x4C3FC takes the failure branch and stores **0xFF** at CA
0x4C4EE-0x4C4F0, and CA 0x4AF0C refuses at 0x4AF32.  **The missing message is
key 0x0065 (12-byte VAP-start body) posted before the 0x0242**, not another
VAP-add field.

## 1. What this lane adds over the sibling decodes

* CA 0x4C3FC's **full** logic: the two early returns that exit *without*
  writing state[4], the mode==2 forced failure, the success sub-branches
  (state[1] and the [mctx+0x190]+0x10 ring math), and the only 0xFF store.
  The 0x0242 worker calls CA 0x4AE7C a second time after the pre-start.
* CA 0x4AE7C's **exact return semantics**: the mctx[4]==2 &&
  (mctx[0x0E]-1)<=2 gate, the 0x944DE -> 0x101890 "already-programmed" return-0
  branch, and the object-identity proof that CA 0x4AE7C's 0xBCD7E(idx) is the
  **same** module context as 0x961AE(idx) = 0xFE578(0,idx) (both use table CA
  0x171458 + u16 CA 0x171504).
* The key-0x0065 firmware worker's body map (body[1..7]) and its **single**
  mctx[0x0E]=1 store, plus the stock host emitter/order
  (hmac_config_start_vap_event @ .text+0x15AF88, called from
  hmac_start_bss_in_available_channel @ 0x4826C) and the vendor green_ap
  enable emitter body (command table g_ast_mac_green_ap_config_table).
* **Read-only probes** that separate every remaining branch *before* a fire, and
  a fail-closed fire recipe (R4) that inserts exactly one frame family.

## 2. Honest limits

* The decode is **static**; no bench contact in this lane.  The 0x0065 worker's
  per-link setup path (CA 0x464F2-0x4659A) is decoded only far enough to bound
  the fault risk (it can dereference mctx+0x190 - see
  [FIRE-RECIPE.md](FIRE-RECIPE.md) section 3 gate G5).
* The vendor host module is the **local rootfs copy** (same version pair,
  3,581,748 B sha 22fa789a...); it is read in place and only derived symbol
  offsets/behaviour are quoted.  No vendor byte is committed.
* Whether key 0x0065 alone flips state[4] to the index, or whether the later
  ap_chan step must also precede the 0x0242 (the success sub-branch reads
  [mctx+0x190]+0x10), is the fire's second observable; the recipe carries both
  outcomes as a decision table.

## 3. Reproduce (host-only, read-only)

    python3 tools/fw_prestart_re.py --selftest --host-modules <ko-dir>
        # 95 checks, 0 failures (firmware sha/literals/instruction flow + host
        # module sha/symbols/key-0x0065 emitter shape)
    python3 tools/fw_prestart_re.py --decode    # the decode summary
    python3 tools/fw_prestart_re.py --probe     # the read-only probe pack
    python3 tools/fw_prestart_re.py --frame     # the 40-B key-0x0065 frame
    python3 tools/fw_prestart_re.py --host-table --host-modules <ko-dir>
        # 9 green_ap command names/ids, 0 failures

The tool finds the local pinned FIRMWARE.bin and the OpenWrt objdump itself
(pass --fw/ARM_OBJDUMP to override); it reads the vendor artifacts in place and
prints only derived addresses, mnemonics, table words and probe addresses.
Evidence: [evidence/selftest.txt](evidence/selftest.txt),
[evidence/decode.txt](evidence/decode.txt), [evidence/probe.txt](evidence/probe.txt),
[evidence/frame.txt](evidence/frame.txt), [evidence/host-table.txt](evidence/host-table.txt).
