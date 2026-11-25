# wifi-fw-dfs-worker - the 0x01F7 worker's acceptance: field B selects the BAND

**Lane:** `wifi-fw-dfs-worker` (shared task **task-191**). **HOST-ONLY**: static
decode of the pinned local firmware image and vendor module in place; no device
contact, no write to any artifact, no fire. **The Lead fires.**

**Why this lane exists.** The F1 fire (../wifi-dfs-mode-send/) posted a correct
29-byte g0-id1 config event, **key 0x01F7, len 1, body {0x03}**, after the
AP-start vap_add and before the 0x0124 channel post; the transport consumed it,
the driver read the published per-link and the attach flag as healthy - yet the
firmware did **not** write the band object's RF mode byte `P+0xF04` (0x15F474
stayed 0) and the band-1 RF image stayed the pre-programme values. This lane
decodes the whole acceptance path and finds the frame was *not dropped at all*:
it was dispatched to the per-key worker CA 0xB5B9C, which ran - but against the
**WRONG BAND**, because header field B (`hdr[3]`) is the per-module **context
selector**, and our frame carried field B = 0x04 while the AP-start ladder's
context (field B = 0) is the one bound to **band 1**.

Read in this order:

1. **REPORT.md** - the g0-id1 dispatcher (key/len/body offsets, table A row 93,
   no length/flag gate), the worker's exact checks and mode parse, the
   field-B -> per-module-context -> band-object chain, the boot C2 ctx_create
   binding module 4 to **band 0**, why the fired frame programmed band 0, the
   fallback hypothesis and the discriminating probes.
2. **TABLES.md** - every address, instruction, table entry, context slot,
   frame byte and probe address.
3. **FIRE-RECIPE.md** - the read-only probe pack (works on the still-live F1
   AP), the one-byte field-B fix, the F2 fire rungs, verdicts and abort rules.
4. **tools/fw_dfs_worker_re.py** - re-derives every claim from the pinned local
   artifacts: `--selftest` (142/142 PASS), `--decode`, `--frame`, `--probes`,
   `--ctx`.
5. **patches/0001-fieldb-context.patch** - the two one-byte source changes
   (0x01F7 + 0x00F7 frame builders) on the fired module source tree; **not
   built by this lane** (the implementation lane must rebuild and update the
   lane-190 golden-frame host tests).

Bottom line (details in REPORT.md):

1. The 29-B frame is well-formed for the path: the g0-id1 handler CA 0x43D58
   reads **key = u16 hdr+20**, **len = ONE BYTE hdr+22**, **body = hdr+24**,
   looks the key up in table A (CA 0x10C23C, 217 x {u16 key; u16 pad; u32
   worker}) and calls the worker; **there is no length, flag or checksum
   gate**. Row 93 = key **0x01F7** -> worker **0x0B5B9D**. [V]
2. `hdr[3]` (field B) is not a constant: it is the **per-module context
   index** resolved by CA 0xFE578(0, fieldB) = `u32(G+0x630+4*fieldB) +
   u16(G+0x6DC)` with G = CA 0x170E28 and 19 boot-created contexts
   (`0x119758 + k*0xB40`). The vendor builder itself fills `hdr[3]` from the
   hmac ctx byte 0 (`ctx[0]`). [V]
3. The worker CA 0xB5B9C takes the band object from **ctx+0x4B8**, the
   published per-link from **band+0x1A00**, the attach flag from
   **band+0xD70** (must be 1) and the mode from **body[0]** (1..5). Its only
   mode-byte store is CA 0xB5D1C; mode 3 calls CA 0x89924 which writes the
   ch36..64 RF image into R = `*(published+0x2D8)`. [V]
4. A context's band binding is created by the **key-0x64** worker
   CA 0x449E4 from **body[0x12]**: `ctx+0x4B8 = 0x15CA08 + 0x1B68*band`.
   The boot C2 ctx_create (key 0x64, module 4, body[0x12]=0) binds module 4 to
   **band 0**; the AP-start vap_add (key 0x64, module 0, body[0x12]=1) binds
   module 0 to **band 1**. The committed wifi-vap-start bench readback
   (module-4 ctx+0x4B0 = 0x00174BF0, ctx+0x4B8 = 0x0015CA08) confirms it. [V]
5. Therefore our field-B=4 0x01F7 frame ran the worker against **band 0**:
   it wrote mode 3 to band0+0xF04 (0x15D90C) and the ch36..64 image into band
   0's R block. The 5 GHz band 1 object (0x15E570, the AP's context 0) was
   untouched - exactly the observed state. The host's "preconditions met"
   readback read context 0 (band 1), which was true but not what the frame
   addressed. **Nothing was silently dropped; the target was silently wrong.**
   Probe context 4's band pointer and band 0's mode byte to confirm on the
   live box (FIRE-RECIPE P0). [V model / I for the live values]
6. **Fix**: build the 0x01F7 frame with field B = **the AP-start module index**
   (`vap_start_module`, 0 in the fired recipe), and the 0x00F7 desc-table
   frame likewise. One byte each: the fixed 29-B frame is
   `000100001d00010000005a5a0000000001000000f701010003ffffffff`.
   Then `P+0xF04` becomes 3 and CA 0xA2FA2 can run the band-1 programme. [V
   bytes / I effect]

No vendor binary, firmware byte, calibration value, MAC or secret is stored
here; the only bytes committed are *our own host-built protocol bytes*.
