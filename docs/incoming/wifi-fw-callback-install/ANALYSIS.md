# ANALYSIS - the G+0x370 installer: there is none in the firmware image

**Lane:** `wifi-fw-callback-install` (shared task task-186). **HOST-ONLY**:
no device, bench link, UART or network access. Study half; the search half is
`SEARCH-RESULTS.md` (committed first, evidence in `evidence/search.txt`).
Grades: **[V]** = verified instruction/value/address from the pinned image or
modules, **[I]** = inference (provenance given), **[U]** = undetermined.

**Bottom line:** the firmware image contains **no instruction that installs
`G+0x370` (CA 0x174B34)**. The boot installer that installs the five other
slots in the same hook block is `CA 0x5B424`, called by the pre-HCC boot init
at `CA 0x48442`; it runs on our host-only boot (proven by the bench-read
`G+0x374 = 0x57639`, which is exactly its literal), and it deliberately never
touches `G+0x370`. The slot is then only ever *read* by VAP start-on-band
`CA 0x971D8` with a safe `cbz` skip. No host module carries the address or
the values either. **Therefore the NULL `G+0x370` is not a skipped host boot
step, and the C5a hypothesis ("stock platform boot installs the band-1
MAC-start callback") is not supported by the artifacts. C5a should be
retired as a cause unless a stock/phase differential shows the slot non-NULL.**

---

## 1. The one installer that exists (exact writer instructions)

Enclosing initializer: **`CA 0x5B424`**, first instruction
`movs r3,#64`, prologue `stmdb sp!,{r4-r9,sl,fp,lr}` at `CA 0x5B426`
**[I]** (entry is exact: it is a `bl` target; see §2). It loads
`fp = CA 0x1747C4` (`ldr r3,=G; mov fp,r3` at `CA 0x5CF48/0x5CF4C`) and
installs:

| slot | exact writer instruction | stored value | value literal |
| --- | --- | --- | --- |
| G+0x364 | `0x5D13A  str.w r3,[fp,#868]` | 0x000565C5 | CA 0x5D27C |
| G+0x368 | `0x5CFC2  str.w r3,[fp,#872]` | 0x00052BE1 | CA 0x5D1E8 |
| G+0x36C | `0x5CF60  str.w r2,[r3,#876]`  | 0x000727D1 | CA 0x5D1C8 |
| **G+0x370** | **no such instruction anywhere in the image** | - | - |
| G+0x374 | `0x5D144  str.w r3,[fp,#884]` | 0x00057639 | CA 0x5D280 |
| G+0x378 | `0x5C3B6  str.w r3,[r4,#888]` | 0x00068C0B | CA 0x5C540 |

**[V]** `evidence/selftest.txt:8-13`, `evidence/search.txt` §11,
`evidence/installer-callsite.txt`. The same function installs ~140 other G
callbacks (the general G callback-table initializer **[I]**). A second
boot function `CA 0x5967C` only *clears* G+0x368 to zero and does not touch
G+0x370 **[V]** `evidence/search.txt` §11.

### 1.1 Why "no writer" is not a scan artefact

* No `movw/movt` immediate `0x4b34/0x4b38`; no LE32 literal of
  `0x00174B34/0x00174B38` (`evidence/selftest.txt:4-5`).
* G is **outside the image** (CA 0x1747C4 = file 0x1347C4 > 0xE2C98), so only
  code that loads the `CA 0x1747C4` literal can reach it; all 76 such load
  sites were taint-scanned, with one level of direct-callee argument
  propagation (`evidence/search.txt` §3/§4/§13).
* Every word-store in the image with immediate offset `880 (0x370)` (17
  sites) has a non-G base: per-link/per-VAP structs (`[rX+0x2D8]`), global
  object `CA 0x14A298`, tag-dispatch structs (`evidence/search.txt` §12).
* No `strd`/`stm` block store covers the hook block with a G base; no
  register-offset ("`[G+rX<<n]`") access to G exists at all
  (`evidence/search.txt` §14b2); no G alias is ever stored into another
  object (`evidence/search.txt` §14b).
* Method limits (honest): linear static scan of the vendored image; indirect
  calls with a G argument are not followed, but no G alias is stored anywhere,
  so a G base can only come from the 76 literal sites or direct args, both
  covered.

## 2. WHEN the installer runs, and proof it ran on our boot

`CA 0x48442: bl 0x5b424` sits in the pre-HCC boot-init body that, in order:

1. `0x48422` memset CA 0x1471C8 (2 B)
2. `0x4842A` memset CA 0x118CF0 (96 B, 0xFF)
3. `0x48438` memset CA 0x176938 (0x1004 B, 0)
4. **`0x48442` bl 0x5b424  <- the G callback installer**
5. `0x485FA..0x4861E` build the descriptor-pool default
6. `0x48620` `bl 0x1017F8` (descriptor-pool populator, lane task-184)

**[V]** `evidence/installer-callsite.txt:1-36`. The desc-pool boot block
itself contains **no G store** (`evidence/search.txt` §6), so the
"boot init block installs the callbacks" candidate is eliminated: the
callbacks are installed a little earlier in the same boot-init body.

**The installer ran on our host-only boot.** Bench read 2026-11-27:
`G+0x374 = 0x00057639`; the installer's literal at CA 0x5D280 is exactly
`0x00057639` [V]. The same boot therefore took the same path that leaves
G+0x370 untouched. The NULL is a property of this firmware build, not of the
host boot sequence.

## 3. What the hook block 0x364..0x378 actually is (decoded)

All callbacks receive a context/band object in r0; the block is consumed as
**optional hooks** (all call sites are `ldr`+`cbz`+`blx`):

```
G+0x364 = 0x565C4 : creates/initialises a per-entity 136-byte object
                    (0x508B2 id lookup), sets 0x08070FFF at +4, list heads,
                    registers timer callback 0x57554 and starts a 1000 ms
                    timer 0xD6B68; the timer callback ages list entries.
G+0x368 = 0x52BE0 : queue/EDCA init: two memsets + 5x CA 0x521E8.
G+0x36C = 0x727D0 : iterates 18 entries and calls 0x727B0, which zeroes a
                    2048-byte state at 0x15B3A8 and sets +1266 = 1
                    (cleanup/reset shape).
G+0x370 = (NULL)  : never installed, never written.
G+0x374 = 0x57638 : structural twin of 0x565C4 (same object/insert-helper
                    0x566D8, same lock 0x14A52C, same 136-byte object);
                    re-creates/restarts the object after tearing down the
                    old one.
G+0x378 = 0x68C0A : calls 0x961AE (the VAP/MAC-start path of lane
                    wifi-beacon-static: 0xD41AC -> 0x94518 -> 0x961AE) then
                    0x66EB6/0x64C6C/0x652AC/0xC1E44/0xBC524/0xAB89E/0x66CC7C.
```

Read sites (G-based) in the whole image **[V]** `evidence/callbacks.txt`:

* `G+0x370` and `G+0x374`: **only** inside VAP start-on-band `CA 0x971D8`
  (`0x97360`/`0x97366` and `0x9736A`/`0x97372`), in that order,
  guarded by `if G+0 != 0` and per-slot `cbz`; the callee gets r0 = VAP ctx.
  No band test at the call site: it is a *band-parameterised* hook pair, not a
  band-1-only pair.
* `G+0x378`: a *different* path at `0x0B56EA` (function `CA 0x0B568C`)
  calls it with (type, 60-byte buffer).
* `G+0x364/0x368/0x36C`: **no G-based read exists in this image** (they are
  installed but not reached through G in this firmware build).

**Consequence:** the desc-pool lane's labels "G+0x370 = MAC start /
G+0x374 = beacon start" are **[I], unverified**. What is verified is that
`0x971D8` treats both as optional hooks and skips a NULL.

## 4. The intended G+0x370 value: [U] - not derivable from the artifacts

Nothing in the image references the slot with a *value*: no writer, no literal,
no code-pointer table entry, no read that compares it to a constant. The two
installed twins `0x565C4` (G+0x364) and `0x57638` (G+0x374) are the only
structural relatives; their installer assignments are *source-adjacent*
(literal pool CA 0x5D27C then CA 0x5D280, stores at 0x5D13A then 0x5D144), so
they are the same role for two entities. If an external installer exists, the
first candidates to compare against a copy read from a *non-NULL* `G+0x370`
would be that twin family - but no CA can be named or justified today.

The alternative (and better supported) reading: this firmware build does not
use `G+0x370` at all; the intended value never shipped. The C5a branch of the
desc-pool verdict table ("G+0x370 or G+0x374 == 0 -> C5a") is therefore
**over-sensitive**: 0x370 is NULL on every boot of this image, including the
stock boot (same rootfs `/lib/firmware/hi_wifi/FIRMWARE.bin`, sha 7fc87e20
**[V]**).

## 5. Host-side (platform) candidate: the primitives exist, the install does not

Scanned all vendor `hi_*.ko` for the G/hook-block addresses and the five
installed callback values: **no hits** **[V]**. So no host module hardcodes
`0x174B34/0x174B38/0x1747C4/0x82CB34/0x4082CB34` or the callback CAs.

The platform module does have a generic device-address write primitive and a
firmware-load path, **[V]** local disassembly of
`hi5622v100_plat.ko`:

* `h2d_write32` (file text 0xEF2C) -> `oal_pcie_inbound_ca_to_va`
  (0x8DD8) -> `str r6,[r5]` at 0xEF88: writes a 32-bit value to any mapped
  **firmware CA**. `h2d_read32` (0xEFF4) is the read twin.
* `firmware_download_function` (0xF9F8) / `firmware_download` (0xF834) /
  `firmware_file_send` (0xF56C) download `FIRMWARE.bin` into the device.
* "patch read fail" / "patch send fail" strings exist near that path.
* `hi5622v100_wifi.ko` references the plat h2d_write32 pointer
  (data offset 0x2DAC50) and contains the host-MAC start path
  (`hmac_start_all_bss_of_band`, `hmac_start_bss_in_available_channel`,
  `hmac_config_h2d_*_event`), i.e. the host-visible trigger that produces the
  VAP-start H2D chain (our five-frame ladder's 0x0065 already exercises it).

So the *mechanism* for a host to write G+0x370 exists, but **nothing in the
artifacts uses it for that slot**, and no host module owns the address/value
pair. "The platform init installs it" is therefore **not demonstrated**; at
best it is an unverified external-installer hypothesis that only a stock
differential can settle.

## 6. Discriminating read-only probes (for the next attended session; not fired here)

Convention: bar0read phys = **0x40000000 + CPU + 0x6B8000**. All reads are
side-effect free; abort on page/exception anomaly per runbook. The Lead fires;
this lane has no bench contact.

### P0 - running-FW identity (proves the probed image is this file)

| target | phys |
| --- | --- |
| FW image first word (CA 0x40000) | 0x406F8000 |
| FW image second word (CA 0x40004) | 0x406F8004 |

(phys = 0x40000000 + CPU + 0x6B8000, the committed convention.)
Expected 0x00046971 and 0x000C742D **[V]**. If different, the running FW is
not the sha-pinned image and every conclusion here must be re-based.

### P1 - the hook block on the live 5 GHz AP (the decisive differential)

| target | phys | expected on our boot |
| --- | --- | --- |
| G+0x364 | 0x4082CB28 | 0x000565C5 |
| G+0x368 | 0x4082CB2C | 0x00052BE1 |
| G+0x36C | 0x4082CB30 | 0x000727D1 |
| **G+0x370** | **0x4082CB34** | **0x00000000** |
| G+0x374 | 0x4082CB38 | 0x00057639 |
| G+0x378 | 0x4082CB3C | 0x00068C0B |
| G head | 0x4082C7C4 | nonzero |

Verdict:

| readback | meaning |
| --- | --- |
| exactly the expected pattern, G+0x370 = 0 | **C5a retired**: the slot is NULL by design; a C5a "fix" is not a fix. The 5 GHz beacon blocker stays C1 (RF programme) and the other host-side links. |
| G+0x370 nonzero | an external installer exists (different FW/other-CPU/host patch). Capture the value + P0 identity; compare the value to the twin family 0x565C4/0x57638 and to function starts near them. Re-open this lane. |
| G+0x374 == 0 or G head == 0 | the boot installer did not run this boot: real C5a-style boot-order issue (not expected; our read had 0x57639). |
| any of 0x364/0x368/0x36C/0x378 differ from expected | the running FW build differs from the sha-pinned image (check P0). |

### P2 - after an attended AP/VAP start (same session)

Re-read P1 immediately after the VAP start frame and again after the 0x0124
RF re-post. A callback installed later by a message would appear here; a slot
that stays at the boot constant proves there is no runtime installer. No
writes.

### P3 - stock differential (only if a stock boot is ever available)

Same P1+P0 reads on the stock boot. Nonzero stock G+0x370 with the same P0
identity would contradict this analysis (then the install is time/order
dependent and invisible to our scan); nonzero stock with a *different* P0
identity means stock runs a different FW - capture that image first.

## 7. Fire recipe

**No fire is justified for C5a by this lane.** The evidence says the slot is
not installed by any component of the shipped stack, so there is nothing to
"turn on" with a host message and no candidate address to write. Concretely:

* R0 (next attended session, read-only): run P0+P1+P2. Expected: 0x370 = 0.
  Then delete C5a from the ladder and spend the session on C1 (RF programme
  re-post 0x0124, per lane wifi-fw-desc-pool FIRE-RECIPE R1/R2) - unchanged
  preflight/recovery/attendance gates (tools/fire-preflight.sh,
  tools/recovery-card.sh, never write CA 0x400392f0, never read the RC misc
  window).
* If P1 shows G+0x370 != 0: do not write; capture the value and P0 identity,
  report, and re-open the installer question. A forced write of a guessed
  callback CA into G+0x370 would be an unvalidated firmware-RAM poke and is
  **not** authorised by this analysis.
* Do not add a "install the callback" patch to the host ladder: the only host
  primitive is the plat generic inbound-CA write32, and the target value is
  unknown [U].

**Correction to lane wifi-fw-desc-pool:** its FIRE-RECIPE R0 verdict row
"G+0x370 or G+0x374 == 0, or G head == 0 -> C5a" should drop 0x370 (and its
T5 probe-18 note); 0x374/G head are the discriminating reads.

## 8. Reproduce

```
cd opensource/docs/incoming/wifi-fw-callback-install
python3 tools/fw_callback_install_re.py --selftest    # 20 PASS, 0 FAIL
python3 tools/fw_callback_install_re.py --search      # evidence/search.txt
python3 tools/fw_callback_install_re.py --callbacks   # evidence/callbacks.txt
python3 tools/fw_callback_install_re.py --dump 0x483EC 0x4844A
python3 tools/fw_callback_install_re.py --dump 0x5CF30 0x5D160
```

## 9. Publication and safety boundary

No vendor binary, firmware byte, extracted table or credential is committed;
the tool reads the pinned local artifacts and the evidence files carry only
derived instruction addresses and small constants. No device/UART/bench/network
access was used; no fire was performed.
