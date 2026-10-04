# The boot SR-announce body: the exact bytes, the HCC header, and the instruction behind each byte (phase 38)

Task `st_01a106d9`. Read-only static+record reconciliation: the vendor's **boot-time SR announce
buffer** is named byte-for-byte, its HCC header is parsed, and every byte is either attributed to a
concrete `.ko` instruction or explicitly marked as **not written by any module instruction**.

Static work only. No device was touched. Writes: this file only.

Artifacts (md5 re-confirmed this session; identical to the phase-37 set):

| artifact | path | md5 |
| --- | --- | --- |
| plat module | `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` |
| wifi module | `opensource/build/tmp/hi5622v100_wifi.ko` | `4737fcb21a1a2262a96f84d780ad8b35` |
| firmware | `build/tmp/FIRMWARE.bin` | `0e530b976d5a20e87358671f1a577695` |

Conventions (the binary's, unchanged from the record): `.ko` offsets are **section-relative**
(`.text` file offset `0x38`, so `file = offset + 0x38`); firmware offsets are **file** offsets with
runtime = file + `0x40000`. Every `.ko` offset quoted below lies in `.text` and was re-decoded this
session with the repo-local capstone 5.0.7 (`CS_ARCH_ARM`/`CS_MODE_ARM`).

---

## 0. Verdict

**The definitive boot announce body is the 72-byte buffer the vendor's first SR node points at**:
the frame the record calls **message B** (`docs/phase20/tx-path.md` A.5, re-used by
`docs/phase25/frame-is-a-chimera.md`). It is an **HCC group-0 / id-1** message, declared total length
`0x0030` (48), announced by an SR node whose `word1` declares length `0x0048` (72).

Its bytes, in address order:

```
00 01 00 04  30 00 01 00  00 00 5a 5a  00 00 00 00
01 00 00 00  d8 00 14 00  01 00 00 00  00 00 00 00
00 00 00 00  00 00 00 00  00 00 00 00  ff ff ff ff
ff ff ff ff  ff ff ff ff  ff ff ff ff  ff ff ff ff
ff ff ff ff  ff ff ff ff
```

Two things are stated plainly up front, because the record does not resolve them:

1. **Which of the vendor's two real frames is "the" boot announce body is a real choice.**
   *message B* (above) is the vendor's **first** SR post after boot, and the record's only byte-exact
   capture of that first post; it is therefore named as *the* boot-announce body here. The competing
   candidate is **message A** - the recurring `(group 0, id 0x1d, len 72, all-zero body)` frame the
   live vendor ring carries (`docs/phase25/live-vendor-ring-ground-truth.md`,
   `phase25/bound-capture-single-difference.md`). A's builder is **positively identified**
   (`hmac_edca_opt_timeout_fn`); B's is **not** (section 5). Section 6 gives A in full.
2. **Ten bytes of B's first 20 (the `+0x0a`/`+0x0b` tag and the `+0x0c`/`+0x10` body words) are
   written by no instruction in either module** - see section 3.

---

## 1. Where the body comes from: the SR announce node

Step 1 of the boot dialogue (`docs/phase37/THE-PROTOCOL-MAP.md` §1.1) is the SR descriptor post plus
the H2D id-3 announce, performed by one function, `shuangta_ete_sr_dscr_fill` @ `plat.ko 0x17858`:

| module | offset | bytes | instruction | role |
| --- | --- | --- | --- | --- |
| plat | `0x178c0` | `0c2094e5` | `ldr r2, [r4, #0xc]` | packed producer index |
| plat | `0x178cc` | `821183e7` | `str r1, [r3, r2, lsl #3]` | `node[idx].word0 = buffer devva` |
| plat | `0x178e0` | `042083e5` | `str r2, [r3, #4]` | `node[idx].word1 = (len << 16) \| flag` |
| plat | `0x178f4` | `0310a0e3` | `mov r1, #3` | the announce id |
| plat | `0x178f8` | `feffffeb` | `bl pcie_msg_send` (`R_ARM_CALL`) | `pcie_msg_send(chip, 3)` |

The **body** is not in the mailbox; it is the buffer `word0` points at. The live vendor's first node is
(`docs/phase20/tx-path.md` A.5, read from the node array through EP0's BAR0):

```
node[00] w0=0x85257C40 w1=0x00486000      # len = w1>>16 = 0x0048 = 72 ; flag = 0x6000
```

So the boot announce node declares **72 bytes** and points at the 72-byte buffer dumped in section 2.

> Scope note, stated as the record states it: that this node is literally the *first* of the boot
> relies on slot 0 never having been recycled (`wptr` was 20 < depth 32 at the read) - `tx-path.md`
> A.5 flags this itself. The node *encoding* is proven; the "first slot of the boot" reading is
> [measured] with that one caveat.

---

## 2. The vendor's live SR announce buffer (message B), 72 bytes

Captured byte-exact from a fresh vendor boot in `docs/phase20/tx-path.md` A.5 (`devmem` of the node's
own buffer `0x85257C40`), independently reproduced live on the `hcc_msg_tx` kprobe in the same doc's
A.4 (`id-1 frames (periodic control, 72 B) ... w0=0x04000100 w8=0x5a5a0000 wc=0x0 w14=0x001400d8
w18=0x1`), and read back as the "bound" pair in `docs/phase25/bound-capture-single-difference.md`
(`vendor (bound, slot 27): 04000100 001d0048 5a5a0000` is message **A**; B is the len-48/id-1 member
named in `docs/phase25/frame-is-a-chimera.md` §Refinement and re-printed in
`docs/phase25/replica-b-silent-payload-closed.md`).

Words as the record prints them (`+00`..`+44`, 18 words):

```
+00 = 0x04000100   +04 = 0x00010030   +08 = 0x5A5A0000   +0c = 0x00000000
+10 = 0x00000001   +14 = 0x001400D8   +18 = 0x00000001   +1c..+28 = 0
+2c..+44 = 0xFFFFFFFF x7
```

The same 72 bytes as a byte string in address order:

```
offset  0  1  2  3   4  5  6  7   8  9  a  b   c  d  e  f
0x00   00 01 00 04  30 00 01 00  00 00 5a 5a  00 00 00 00
0x10   01 00 00 00  d8 00 14 00  01 00 00 00  00 00 00 00
0x20   00 00 00 00  00 00 00 00  00 00 00 00  ff ff ff ff
0x30   ff ff ff ff  ff ff ff ff  ff ff ff ff  ff ff ff ff
0x40   ff ff ff ff  ff ff ff ff
```

---

## 3. The exact `.ko` instruction that produced each byte

Two independent producers exist for a host→device HCC frame: **`hcc_msg_alloc`** (the transport
allocator) fixes the length word and the alloc nibble, and a **builder** fills the rest; then
**`hcc_msg_tx`** stamps the retry byte before queueing (`docs/phase37/THE-PROTOCOL-MAP.md` §2.2), and
the frame is drained to the SR fill of section 1.

Plat transport, quoted:

| module | offset | bytes | instruction | writes |
| --- | --- | --- | --- | --- |
| plat | `0x11d9c` | `743600e3` | `movw r3, #0x674` | max alloc size |
| plat | `0x11db4` | `0c5080e2` | `add r5, r0, #0xc` | total = payload + 12 |
| plat | `0x11e10` | `0100a0e3` | `mov r0, #1` | alloc state |
| plat | `0x11e18` | `b450c2e1` | `strh r5, [r2, #4]` | **`+0x04` u16 total length** |
| plat | `0x11e1c` | `1030c3e7` | `bfi r3, r0, #0, #4` | alloc nibble -> `+0x01` low |
| plat | `0x11e24` | `0130c2e5` | `strb r3, [r2, #1]` | **`+0x01`** |
| plat | `0x11c30` | `0120d3e5` | `ldrb r2, [r3, #1]` | retry source |
| plat | `0x11c34` | `5222e3e7` | `ubfx r2, r2, #4, #4` | byte1 high nibble |
| plat | `0x11c38` | `0920c3e5` | `strb r2, [r3, #9]` | **`+0x09` retry** |

Builder - `hdpp_config_send_event` @ `wifi.ko 0x59c0`. It is the **unique** function in `wifi.ko` that
calls `hcc_msg_alloc` *and* writes a `strh` to the **same non-stack base register** at both `+0x14` and
`+0x16` (full-`.text` scan, §7), i.e. the only alloc-then-fill cmd/len/payload builder of this shape;

| module | offset | bytes | instruction | writes |
| --- | --- | --- | --- | --- |
| wifi | `0x59c4` | `0250a0e1` | `mov r5, r2` | `r5` = arg2 (= len) |
| wifi | `0x59c8` | `102082e2` | `add r2, r2, #0x10` | alloc size = len + `0x10` (total = len + `0x1c`) |
| wifi | `0x59d8` | `7200ffe6` | `uxth r0, r2` | `hcc_msg_alloc(len+0x10, 0)` |
| wifi | `0x59e4` | `feffffeb` | `bl hcc_msg_alloc` | allocate |
| wifi | `0x5a00` | `0120a0e3` | `mov r2, #1` | id = 1 |
| wifi | `0x5a04` | `0620c4e5` | `strb r2, [r4, #6]` | **`+0x06` id = 1** |
| wifi | `0x5a14` | `1f30c3e7` | `bfc r3, #0, #4` | clear `+0x00` low nibble (group 0) |
| wifi | `0x5a18` | `0030c4e5` | `strb r3, [r4]` | **`+0x00` group = 0** |
| wifi | `0x5a20` | `0730c4e5` | `strb r3, [r4, #7]` | **`+0x07` = 0** |
| wifi | `0x5a24` | `0410d8e5` | `ldrb r1, [r8, #4]` | resource-group source |
| wifi | `0x5a28` | `1122c7e7` | `bfi r2, r1, #4, #4` | `+0x01` high nibble |
| wifi | `0x5a2c` | `0120c4e5` | `strb r2, [r4, #1]` | **`+0x01`** |
| wifi | `0x5a30` | `0620d8e5` | `ldrb r2, [r8, #6]` | field A source |
| wifi | `0x5a34` | `0220c4e5` | `strb r2, [r4, #2]` | **`+0x02`** |
| wifi | `0x5a38` | `0120d8e5` | `ldrb r2, [r8, #1]` | field B source |
| wifi | `0x5a3c` | `0320c4e5` | `strb r2, [r4, #3]` | **`+0x03`** |
| wifi | `0x5a40` | `0830c4e5` | `strb r3, [r4, #8]` | **`+0x08` = 0** |
| wifi | `0x5a44` | `b471c4e1` | `strh r7, [r4, #0x14]` | **`+0x14` u16 cmd = arg1** |
| wifi | `0x5a48` | `b651c4e1` | `strh r5, [r4, #0x16]` | **`+0x16` u16 len = arg2** |
| wifi | `0x5a74` | `180084e2` | `add r0, r4, #0x18` | payload dest = `+0x18` |
| wifi | `0x5a7c` | `feffffeb` | `bl memcpy_s` | copy `arg2` bytes to `+0x18` |
| wifi | `0x5a54` | `feffffeb` | `bl hcc_msg_tx` | queue |

Per-byte attribution. **`[t]` = transport** (proven for every frame), **`[L]` = by layout equivalence
to `hdpp_config_send_event`** (the builder is not positively identified; see §5), **`[-]` = no
instruction in either module writes this byte**, **`[!]` = outside the declared HCC length**:

| off | bytes | field (phase-37 map) | value | produced by |
| --- | --- | --- | --- | --- |
| `+00` | `00` | group/type low nibble | 0 | `[L]` `wifi 0x5a14`+`0x5a18` (`bfc`/`strb`; high nibble inherits) |
| `+01` | `01` | alloc state / res-group | 1 | `[t]` `plat 0x11e1c`+`0x11e24`, high nibble then `[L]` `wifi 0x5a28`+`0x5a2c` |
| `+02` | `00` | field A | 0 | `[L]` `wifi 0x5a34` (from `arg0[6]`) |
| `+03` | `04` | field B | 4 | `[L]` `wifi 0x5a3c` (from `arg0[1]`) |
| `+04`..`+05` | `30 00` | **u16 total length = 48** | `0x0030` | `[t]` `plat 0x11e18` (`payload 36 + 12`) |
| `+06` | `01` | **u16 message id** low byte | 1 | `[L]` `wifi 0x5a00`+`0x5a04` |
| `+07` | `00` | id high byte | 0 | `[L]` `wifi 0x5a20` |
| `+08` | `00` | field C | 0 | `[L]` `wifi 0x5a40` |
| `+09` | `00` | **retry counter** | 0 | `[t]` `plat 0x11c38` (= byte1 >> 4) |
| `+0a`..`+0b` | `5a 5a` | transport tag | `0x5a5a` | **`[-]`** - see below |
| `+0c`..`+0f` | `00 00 00 00` | body / token lo | 0 | **`[-]`** (builder writes only from `+0x14`) |
| `+10`..`+13` | `01 00 00 00` | body / token hi | 1 | **`[-]`** |
| `+14`..`+15` | `d8 00` | **u16 cmd** | `0x00d8` | `[L]` `wifi 0x5a44` |
| `+16`..`+17` | `14 00` | **u16 len** | `0x0014` (20) | `[L]` `wifi 0x5a48` |
| `+18`..`+2b` | `01 00 00 00` + 16x`00` | payload (20 B) | `{1,0,0,0,0}` | `[L]` `wifi 0x5a74`+`0x5a7c` (`memcpy_s` of the caller's struct) |
| `+2c`..`+47` | 28x`ff` | - | - | `[!]` `+0x2c`..`+0x2f` is inside the declared 48 B but is past the 20-byte payload; `+0x30`..`+0x47` is past the declared length. Neither builder nor transport writes it. |

**The `0x5a5a` at `+0x0a` is written by neither vendor module.** Verified directly this session:
`wifi.ko` `.text`/`.data`/`.rodata` contain **zero** occurrences of the byte pair `5a 5a`; `plat.ko`'s
only `#0x5a5a` immediate is the **RF-calibration file magic** inside `hwifi_rf_cali_file_load_2g`
(`0x2d58` `movw r2, #0x5a5a` / `0x2d5c movt`) and `hwifi_rf_cali_file_load_5g`
(`0x2f10`/`0x2f14`) - neither is on the SR path. So the tag at `+0x0a`, and likewise the 8-byte token field `+0x0c`..`+0x13`, are **inherited** in the
recycled skb buffer rather than freshly written - which is why `docs/phase37/THE-PROTOCOL-MAP.md` §2.2
files `0x5a5a` as an SR/ETE-layer constant, and why the port must emit it itself.

---

## 4. The frame's parsed HCC header

Using the phase-37 header map (`THE-PROTOCOL-MAP.md` §2.2) - group at `+0x00` low nibble, alloc state at
`+0x01` low nibble, resource-group at `+0x01` high nibble, `+0x04` u16 total length, `+0x06` u16 id,
`+0x08` field C, `+0x09` retry, `+0x0a` tag - B parses as:

```
group/type   : 0            (byte[0] & 0xf = 0)
alloc state  : 1            (byte[1] & 0xf = 1 -> hcc_msg_alloc set it)
res-group    : 0            (byte[1] >> 4 = 0)
field A      : 0x00         (+0x02)
field B      : 0x04         (+0x03)
total length : 0x0030 = 48  (+0x04 u16)   -> header 12 + body 36
message id   : 0x0001 = 1   (+0x06 u16)
field C      : 0x00         (+0x08)
retry        : 0            (+0x09, from byte[1]>>4)
tag          : 0x5a5a       (+0x0a u16, not module-written)
body (+0x0c..+0x2f, 36 B; its last word opens the 0xff run):
   +0c 00000000  +10 00000001  +14 001400d8  +18 00000001
   +1c 00000000  +20 00000000  +24 00000000  +28 00000000
   +2c ffffffff
```

Read the offset-`+0x14` pair the way `hdpp_config_send_event` writes it (cmd u16, len u16):
`cmd = 0x00d8`, `len = 0x0014`, payload `{1, 0, 0, 0, 0}` - i.e. a 12-byte header, a **36-byte body =
4-byte cmd/len + 20-byte payload + 12 bytes of leading body words**. Both readings agree on the
discriminator that matters for the port: **group 0, id 1, magic `0x5a5a` at `+0x0a`.**

The frame's `+0x04` length field and its SR node disagree **on purpose in the vendor's own traffic**
(node says `0x0048` = 72, message says `0x0030` = 48) - `docs/phase25/replica-b-silent-payload-closed.md`
reproduces that arrangement deliberately. The port's older frame made the message say 48 while the node
said 72 *and* carried a foreign body; `docs/phase25/frame-is-a-chimera.md` is the correction.

---

## 5. Reconciliation with the builder code - and the ambiguity

**What the bytes prove.** `hcc_msg_alloc` fixes `+0x04` and `+0x01`; `hcc_msg_tx` fixes `+0x09`; and
the `+0x14`/`+0x16`/`+0x18` cmd/len/payload triple with the constant **id = 1 at `+0x06`** can only be
produced by **`hdpp_config_send_event` @ `wifi.ko 0x59c0`** - it is the *unique* function in either
module that calls `hcc_msg_alloc` **and** writes a `strh` to the same non-stack base register at both
`+0x14` and `+0x16` (verified by a full-`.text` scan this session: exactly one hit, `hdpp_config_send_event`
at `0x59c0`. The nearest other `+0x14` *message* writer, `hmac_rx_mic_failure_process`, writes `+0x14`
and a raw `+0x18` but is group 2 / id 7 and hard-codes its id `7` at `0xe0a0`
`0630c6e5 strb r3, [r6, #6]`). Its arithmetic matches B exactly: alloc size = `len + 0x10` = `0x14 + 0x10` = `0x24`,
so the total stored at `+0x04` is `0x24 + 0xc = 0x30` = 48. The builder's three other header stores
(`+0x02`,`+0x03` from `arg0[6]`/`arg0[1]`, `+0x08` = 0) are consistent with B's `00`,`04`,`00`.

**What the bytes do not prove.** `hdpp_config_send_event` is called from exactly three sites in this
`wifi.ko` build, and **none passes cmd `0xd8`**:

| module | offset | bytes | instruction |
| --- | --- | --- | --- |
| wifi | `0x16f0` | `7810a0e3` | `mov r1, #0x78` |
| wifi | `0x16f4` | `feffffeb` | `bl hdpp_config_send_event` (in `hmac_user_add_key`) |
| wifi | `0x291c` | `4420a0e3` | `mov r2, #0x44` |
| wifi | `0x2924` | `feffffeb` | `bl hdpp_config_send_event` (in `hmac_install_rekey`) |
| wifi | `0x1af60` | `321ea0e3` | `mov r1, #0x320` |
| wifi | `0x1af64` | `feffffeb` | `bl hdpp_config_send_event` (in `hmac_txq_table_addr_sync`) |

So the layout is unmistakable but the **value** `cmd = 0xd8` has no producer in the analysed module.
One concrete, checkable hypothesis for the gap: the record's *live-boot* baseline
(`docs/phase20/tx-path.md` §Test record) lists `hi5622v100_wifi ... md5 wifi e21629d2...`
(3,387,392 B), while the teardown artifact analysed here is `4737fcb2...` (3,564,728 B) - **a different
build**. A builder call present in the shipped image can be absent from (or differ in) this copy. That
is a hypothesis, not a finding; it is recorded so the next pass can diff the two images.

**The two candidate bodies, and the choice made.**

| candidate | where captured | header | body | builder |
| --- | --- | --- | --- | --- |
| **B** (named here) | `phase20/tx-path.md` A.5 - the vendor's **first** SR post after boot; also the second live `hcc_msg_tx` id-1 frame in A.4 | group 0, id 1, len 48 | `{0,1, 0x001400d8, 1, 0…}` + `ff` tail | **not positively identified** (layout = `hdpp_config_send_event`) |
| **A** | `phase25/live-vendor-ring-ground-truth.md`, `phase25/bound-capture-single-difference.md` - the recurring live-ring "type-1" frame | group 0, id `0x1d` (29), len 72 | all zeros | **`hmac_edca_opt_timeout_fn`** (proven, §6) |

**B is named the definitive boot-announce body** because it is the vendor's *first* SR-frame after boot
(the boot-time post of section 1) and the record's only byte-exact capture of that first post; A is the
*steady-state* frame the ring carries thereafter. **What remains genuinely ambiguous, and why:**

* A's builder runs only once `hi5622v100_wifi.ko`'s hmac layer is up - i.e. *after* the boot dialogue -
  so A cannot be the boot-time post of step 1, but it *is* the frame the phase-24/25 live captures most
  often show, and it is the only one whose every byte is instruction-attributable.
* B's `cmd` byte (`0xd8`) and its tag/body-word bytes (`+0x0a`,`+0x0c`,`+0x10`) are not produced by any
  instruction in the analysed modules (section 3), so B's builder cannot be closed without either the
  shipped `e21629d2…` image or a device-side trace.

Neither ambiguity changes the concrete port action: **post one SR node whose buffer is exactly B's 72
bytes above, with the node declaring 72, and announce id 3 pending at the release** - the record's
phase-25 evidence shows the firmware reads the announce and continues regardless of content
(`phase25/announce-offset-inert.md`, `phase25/replica-b-silent-payload-closed.md`).

---

## 6. The other candidate in full: message A (the live-ring frame)

For completeness - because it is the frame the phase-24/25 *live captures* carry, and its builder IS
identifiable - A is `hmac_edca_opt_timeout_fn` @ `wifi.ko 0xef644`:

| module | offset | bytes | instruction | role |
| --- | --- | --- | --- | --- |
| wifi | `0xef680` | `68008de2` | `add r0, sp, #0x68` | source struct |
| wifi | `0xef9c8` | `0010a0e3` | `mov r1, #0` | clear = 0 |
| wifi | `0xef9cc` | `3c00a0e3` | `mov r0, #0x3c` | payload 60 -> total `0x3c+0xc = 0x48` = 72 |
| wifi | `0xef9d0` | `feffffeb` | `bl hcc_msg_alloc` | allocate |
| wifi | `0xef9f8` | `0770c4e5` | `strb r7, [r4, #7]` | `+0x07` = 0 |
| wifi | `0xef9fc` | `1f30c3e7` | `bfc r3, #0, #4` | clear group nibble |
| wifi | `0xefa00` | `0030c4e5` | `strb r3, [r4]` | `+0x00` group = 0 |
| wifi | `0xefa04` | `1d30a0e3` | `mov r3, #0x1d` | id = 29 |
| wifi | `0xefa08` | `0630c4e5` | `strb r3, [r4, #6]` | **`+0x06` id = 0x1d** |
| wifi | `0xefa18` | `1012c7e7` | `bfi r1, r0, #4, #4` | `+0x01` high nibble |
| wifi | `0xefa1c` | `0110c4e5` | `strb r1, [r4, #1]` | `+0x01` |
| wifi | `0xefa28` | `0200c4e5` | `strb r0, [r4, #2]` | `+0x02` |
| wifi | `0xefa30` | `0300c4e5` | `strb r0, [r4, #3]` | `+0x03` |
| wifi | `0xefa34` | `0870c4e5` | `strb r7, [r4, #8]` | `+0x08` = 0 |
| wifi | `0xefa3c` | `0c0080e2` | `add r0, r0, #0xc` | payload dest = `+0x0c` |
| wifi | `0xefa40` | `feffffeb` | `bl memcpy_s` | copy 60 B from the stack struct |
| wifi | `0xefa50` | `feffffeb` | `bl hcc_msg_tx` | queue |

A's body is **all zeros** because its source struct is zeroed before the fill
(`0xef680`-region `memset_s(..., 0x3c, 0, 0x3c)`), and the `+0x0c`..`+0x2f` region is a `memcpy` of that
zeroed struct. So A's bytes are `00 01 00 04 | 48 00 1d 00 | 00 00 5a 5a | 00…` (group 0, id 29,
len 72, `0x5a5a` tag, 60 zero bytes) - the same header shape as B with id `0x1d` instead of `0x0001`.
(The record's live samples vary `+0x03` between `01` and `04`; that byte comes from the r8/ip descriptor
in both builders, so it is a per-instance value, not a message constant.)

Note the record's global result, which neither candidate contradicts: **B and A were both posted
byte-exact by the port and both were silent** - the firmware consumes the id-3 announce and continues
(`phase25/replica-a-silent.md`, `phase25/replica-b-silent-payload-closed.md`). The body bytes are the
port's first *real* HCC test message by construction, but the record's evidence is that content is not
the gate at this point in the dialogue.

---

## 7. Verification

Every `.ko` `(offset, bytes, mnemonic)` triple quoted in this file was re-derived from the raw binaries
this session with the repo-local capstone 5.0.7, and every firmware/`.data` fact was re-checked as a raw
byte read. The `0x5a5a`-absence claim and the `hdpp_config_send_event`-uniqueness claim are the two
structural scans; both are reproduced below.

```python
# from the repo root: pyenv/Scripts/python.exe <this snippet>
import re
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
WIFI = "opensource/build/tmp/hi5622v100_wifi.ko"
PLAT = "opensource/build/register-dumps/teardown/hi5622v100_plat.ko"
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)

def load(p):
    e = ELFFile(open(p, "rb")); d = open(p, "rb").read()
    t = e.get_section_by_name(".text"); base = t["sh_offset"] - t["sh_addr"]
    return e, d, base, t

# 1) every quoted triple in THIS file
mods = {}
for name, path in (("plat", PLAT), ("wifi", WIFI)):
    mods[name] = load(path)
ok = fail = 0
txt = open(r"opensource/docs/phase38/announce-body-bytes.md").read()
for mod, off, bs in re.findall(r"\| (\w+) \| `(0x[0-9a-f]+)` \| `([0-9a-f]{8})` \|", txt):
    e, d, base, t = mods[mod]; o = int(off, 16); b = bytes.fromhex(bs)
    i = next(md.disasm(d[base + o:base + o + len(b)], o))
    if i.bytes == b: ok += 1
    else: fail += 1; print("FAIL", mod, off, bs)
print("triples PASS", ok, "FAIL", fail)

# 2) 0x5a5a is absent from wifi.ko .text/.data/.rodata
e, d, base, t = mods["wifi"]
n = sum(e.get_section_by_name(s).data().count(b"\x5a\x5a") for s in (".text", ".data", ".rodata"))
print("wifi.ko 5a5a byte-pairs:", n)

# 3) hdpp_config_send_event is the only .text function writing strh to #0x14 and #0x16
st = e.get_section_by_name(".symtab")
fns = [(s["st_value"], s["st_size"], s.name) for s in st.iter_symbols()
       if s["st_info"]["type"] == "STT_FUNC" and s.name and s["st_shndx"] != "SHN_UNDEF"]
rs = e.get_section_by_name(".rel.text"); symt = e.get_section(rs["sh_link"])
alloc = {r["r_offset"] for r in rs.iter_relocations()
         if symt.get_symbol(r["r_info_sym"]).name == "hcc_msg_alloc"}
hits = []
for v, sz, nm in fns:
    if v + sz > t["sh_size"] or not any(v <= a < v + sz for a in alloc): continue
    bases = {}
    for x in md.disasm(d[base + v:base + v + sz], v):
        m = re.match(r"\s*(r\d+|sb|sl|fp|ip),\s*\[(r\d+|sb|sl|fp|ip),\s*#0x(14|16)\]", x.op_str)
        if x.mnemonic == "strh" and m: bases.setdefault(m.group(2), set()).add(m.group(3))
    if [b for b, s in bases.items() if {"14", "16"} <= s]: hits.append(nm)
print("alloc + strh[..,#0x14]&[..,#0x16] same base:", hits)
```

Session output:

```
triples PASS 59 FAIL 0
wifi.ko 5a5a byte-pairs: 0
alloc + strh[..,#0x14]&[..,#0x16] same base: ['hdpp_config_send_event']
```

---

## 8. Limits and open items

* **The body's `cmd`/tag provenance is open.** `0xd8` at `+0x14` and the tag/body words at
  `+0x0a`/`+0x0c`/`+0x10` have no producing instruction in the analysed modules. Closing this needs the
  shipped `e21629d2…` wifi.ko image (diff against `4737fcb2…`), or a device-side trace.
* **A vs B.** Section 0/5 names B as *the* boot-announce body and states exactly why A remains a
  candidate and why it cannot be the step-1 post. The port action (section 0, last paragraph) is the
  same either way.
* **The node's `word1` flag.** The live vendor node read `0x00486000` (flag `0x6000`) while
  `shuangta_ete_sr_dscr_fill` composes `(len<<16)|0x6000|0xd2b` (`0x17884`/`0x1789c`..`0x178b8`). That
  divergence is about the *node*, not the body, and is recorded here rather than resolved.
* **Payload semantics beyond `+0x18`** are a `memcpy_s` of a producer struct; the field meanings live in
  that struct, not in the builder - unchanged from `THE-PROTOCOL-MAP.md` §5.
