# wifi-ppdu-init - the PPDU descriptor-list INIT: what calls it, when, and why the F1 probe was on the wrong object

**Lane:** `wifi-ppdu-init` (shared task **task-219**). **HOST-ONLY**: reads the
pinned local firmware image in place; no device contact, no flash, no fire.
Companions: [REPORT.md](REPORT.md) (the decode), [PROBES.md](PROBES.md) (corrected
read-only probe pack), [FIRE-RECIPE.md](FIRE-RECIPE.md) (re-probe / stock-control
recipe), [tools/fw_ppdu_init_re.py](tools/fw_ppdu_init_re.py) (stdlib
re-derivation tool, `--selftest` **36/36 PASS**), [evidence/](evidence/).

Grades: **[V]** re-derived here by the committed tool from the pinned image
(sha256 `7fc87e20...c3ef311b`, 928,920 B; CA = file offset + 0x40000);
**[V*]** committed earlier lane / stock dump; **[I]** inference; **[U]** needs a
device read.  This lane **corrects** one claim of
`../wifi-rx-f1-thread` (its PROBES P4 self-check address) - see REPORT sec. 5.

---

## Verdicts (short form)

1. **The init is a boot-phase firmware-internal step, not an H2D message.** The
   chain is: image word0 (CA 0x40000 = 0x46971, fn 0x46970) **registers** the
   function 0x47624 (Thumb entry 0x47625) through CA 0xC745C at 0x47416-0x47424;
   that registered body calls **0xD1B68** at 0x48314 in its boot-time init
   sequence; 0xD1B68 calls **0xC016C** (the PPDU descriptor-list create/init) at
   0xD3BAC with the per-band object.  **[V] every site re-derived.**
2. **When:** before the host ladder matters - it is a firmware boot/init task.
   The call at 0x48314 is preceded by a bounded wait (11 x 50-tick sleeps on
   `u32[0x18179C]`/`[ptr+0x50]`) and is **unconditional once the earlier init
   steps of that body returned 0**. **[V] structure, [I] runtime order.**
3. **Preconditions:** gate `u8[0x17FCA8+0xB68]` = u8[0x180810]` must be **exactly 1**
   (0 = skip silently, >1 = error 0x65); the per-device array 0x174BE0 (2
   devices) and the device count `u8[0x174BEE]=2` are written by 0xD1B68 itself;
   the object is `0x174BF0 + 0xE28*band` (getter CA 0xF4F48, bands 0..1 only).
4. **The bench datum's object was wrong.** `ctx = u32[0x171458]+u16[0x171504]`
   resolves the resolver's **module-context #0 = 0x119758** (the 19x0xB40 context
   array base is the literal at CA 0x49330), while the init writes the list into
   **0x174BF0+0xE28*band**.  So `u32[0x119758+0x67C]=0` is **expected whether or
   not the init ran** - it is not evidence that the PPDU list was never
   initialized.  Corrected self-check = **u32[0x174BF0+0x67C] == 0x001060F7**
   (phys 0x4082D26C), plus head self-pointer 0x0017525C.
5. **Why it might not run on our boot (if the corrected probe still shows the
   list absent):** the firmware-internal gate/task path, ranked: (H1)
   `u8[0x180810] != 1`; (H2) the registered body never reaches 0x48314 because an
   earlier step in that body returned an error (two `bne.w` abort branches
   before the call); (H3) the boot-entry registration/first-init order differed.
   No captured stock H2D family is a trigger - the init is not message-driven.
   **[I], with the corrective probe as the discriminator.**
6. **Candidate host-visible action: none is authorised by this decode.** The
   per-lane stock-only g0-id1 keys (`wifi-smac-start`) and the captured
   families (`wifi-f1-from-capture`) are post-boot H2D handling, not this
   init's trigger.  The next step is the **corrected read-only probe + stock
   control** (PROBES/FIRE-RECIPE), *not* a host message fire.

## Files

| path | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the chain decode, the init decode, preconditions, the object correction, limits |
| [PROBES.md](PROBES.md) | P1-P8 corrected read-only probe pack (firmware-RAM phys addresses) |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | re-probe first, stock control second, firmware-internal escalation third |
| [tools/fw_ppdu_init_re.py](tools/fw_ppdu_init_re.py) | `--selftest` / `--chain` / `--probes`; 36/36 PASS |
| [evidence/selftest.txt](evidence/selftest.txt), [chain.txt](evidence/chain.txt), [probes.txt](evidence/probes.txt) | tool output (regeneration) |

## Status / next

* Read-only static lane **complete**; the deliverable is a **correction + probe
  pack**, not a fire.  No firmware bytes or blobs are committed here (only
  decoded addresses/words; the tool reads the local image at run time).
* The Lead should run PROBES P1-P8 on the next attended 2.4 GHz AP boot, and the
  same P2-P7 on a stock boot as the control.  If the corrected list is present
  on our boot, the F1 root cause is **not** the list init and the f1-thread
  PROBES P4/P8 decision table must be re-read with the corrected object.
