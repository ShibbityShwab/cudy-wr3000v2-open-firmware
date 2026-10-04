# Phase-38 report verification (2026-10-04)

Independent re-derivation of the four phase-38 reports. Every quoted `.ko`/firmware offset was
re-disassembled this session with the repo-local `pyenv/Scripts/python.exe` (capstone 5.0.7:
`CS_ARCH_ARM`+`CS_MODE_ARM` for the modules, `CS_ARCH_ARM`+`CS_MODE_THUMB` for the firmware blob),
and every cited web URL was re-fetched this session. The three binaries were confirmed against the
md5s the reports state:

| artifact | path | md5 (re-computed) | size |
| --- | --- | --- | --- |
| plat module | `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` | 364,660 B |
| wifi module | `opensource/build/tmp/hi5622v100_wifi.ko` | `4737fcb21a1a2262a96f84d780ad8b35` | 3,564,728 B |
| firmware | `build/tmp/FIRMWARE.bin` | `0e530b976d5a20e87358671f1a577695` | 928,920 B |

All three match the values asserted in the reports, so every offset below was checked against the
same images the reports analysed.

---

## Verdicts

| # | report | verdict |
| --- | --- | --- |
| 1 | `announce-body-bytes.md` | **PASS** |
| 2 | `d2h-vocabulary-verified.md` | **PASS** |
| 3 | `open-gaps-static-attempt.md` | **PASS** |
| 4 | `web-sdk-research.md` | **PASS** |

---

## 1. `announce-body-bytes.md` - PASS

Conventions re-checked first: `plat.ko` and `wifi.ko` both have `.text` file offset `0x38`
(`file = offset + 0x38`), confirmed from the ELF section table.

**Every quoted `(offset, bytes)` triple reproduces.** A regex over the file's `| mod | offset |
bytes | instruction |` tables found **59** triples (the table's own reproduce-block expected 59);
all 59 read back the exact bytes, and decoding each byte string in full-buffer context yields the
quoted mnemonic+operands for all 59 (branch targets resolve because decoding starts from the whole
`.text`, not a 4-byte slice).

```
hex triples: PASS 59 FAIL 0
announce-body mnemonic matches (59 ok)
```

**Structural claims reproduce:**

* `wifi.ko` `.text`/`.data`/`.rodata` contain **zero** `5a 5a` byte pairs - the report's core
  "the tag is written by no module" claim.
* `plat.ko`'s only `movw`/`mov #0x5a5a` immediates are at `0x2d58` and `0x2f10` (the RF-cal file
  magics), matching the report.
* The uniqueness scan over full `.text`: the only `STT_FUNC` that calls `hcc_msg_alloc` **and**
  writes a `strh` to the same non-stack base register at `+0x14` and `+0x16` is
  `hdpp_config_send_event` - the claim "exactly one hit" holds.
* `hdpp_config_send_event` has exactly **3** `R_ARM_CALL` sites (`0x16f4`, `0x2924`, `0x1af64`),
  as the "three call sites, none passes cmd `0xd8`" argument requires.
* The competing-writer check reproduces: `hmac_rx_mic_failure_process` at `0xe0a0` is
  `strb r3, [r6, #6]` (hard-coded id 7), exactly as the report states.

```
wifi.ko 5a5a byte-pairs: 0
plat.ko movw/mov #0x5a5a at: ['0x2d58', '0x2f10']
0xe0a0 strb r3, [r6, #6] 0630c6e5
unique alloc+strh[0x14][0x16] builder: ['hdpp_config_send_event']
```

The report's declared *open items* (cmd `0xd8`/tag provenance, A-vs-B choice, node `word1` flag)
are labelled as open in the report itself and are not contradicted by any byte; they are recorded
as open here too, not scored.

**Verdict: PASS** - no quoted offset, byte, mnemonic, or structural claim failed re-derivation.

---

## 2. `d2h-vocabulary-verified.md` - PASS

Method re-run directly against `hi5622v100_wifi.ko`: parse `.data` at file offset `0x243ab0`
(confirmed: section `sh_offset` = `0x243ab0`), read the six tables, and resolve each handler word's
`R_ARM_ABS32` (type 2) relocation through the symbol table.

* All six table offsets and counts reproduce: `.data+0x04`/5, `.data+0x40`/7, `.data+0x584`/28,
  `.data+0x6d4`/12, `.data+0x764`/8, `.data+0x1c74`/19.
* The id word in every one of the **79** slots equals the id the report tabulates, in the on-disk
  order the report prints (including the non-id-sorted tails: group 1 ends `...36, 38, 39, 37`;
  group 0 keeps id 4 before id 3).
* **77** of the 79 handler words carry an `R_ARM_ABS32` reloc; the remaining **2** (group-2 hmac,
  ids 22 and 23) have no reloc and a zero word - the "absent (null)" slots.
* Of the 77, **76** resolve to local `STT_FUNC` symbols and exactly **1** is an import
  (`SHN_UNDEF`) - `hcc_timer_process`. All handler-slots' stored addends are `0`, as claimed.
* Sampled `id -> handler -> st_value` pairs (e.g. slot `0x44` -> `hmac_tx_complete_event_handle`
  @ `0x1c470`; `0x1d50` -> `wal_hiwifi_report_proc` @ `0x126b50`; `0x7bc` -> `hmac_report_beacon_frame`
  @ `0x49ce8`) all match.

```
entries 79 | local 76 import 1 absent 2 | id mismatches 0
imports: ['hcc_timer_process']
reloc types: [2]
handler slots with nonzero stored addend: []
sample symbol/st_value mismatches: 0
```

The report's own appended check output (77 verified / 2 absent) agrees with this independent run.

**Verdict: PASS** - table addresses, counts, ids, reloc classes and symbol resolutions all reproduce.

---

## 3. `open-gaps-static-attempt.md` - PASS

The report's section-6 verification block lists quoted offsets for both images. Parsed
deterministically and re-disassembled in context:

* **`FIRMWARE.bin` (Thumb, file offsets): 43/43** quoted offsets decode identically (targets printed
  by the report as runtime addresses `0xcXXXXX` equal the file offset + `0x40000`).
* **`plat.ko` (ARM, `.text`-relative): 19/19** quoted offsets decode identically.

```
fw: 43 match, 0 mismatch
plat: 19 match, 0 mismatch
TOTAL mismatches: 0
```

**Load-bearing structural/negative claims also reproduce:**

* `0x400a1434` occurs nowhere in `FIRMWARE.bin` (the report's correction that the step-4 doorbell is
  `0x40101434`, not `0x400a1434`).
* The literal `0x40039014` occurs in the image only at file `0x97dc` and `0x86fbc`; `0x40101434`
  only at `0x97d8` - matching the "no other writer exists" provenance argument.
* The ops-table words reproduce: `0xcf258`=`0x000402AD`, `0xcf270`=`0x000C6171` (the `d2h_notify`
  slot, Thumb bit set), `0xcf274`=`0x000C187B`, `0xcf27c`=`0x000C71D7`.
* The cited literal pools reproduce: `0x861dc`/`0x97cc`=`0x00172130`, `0x81d84`=`0x00170E08`,
  `0x86fbc`/`0x97dc`=`0x40039014`, `0x97d8`=`0x40101434`, `0x2a8`=`0x0010C0F4`,
  `0x4f524`=`0x40030100`.
* The id-5 handler stub bytes at file `0x819d0` are three identical `BUG()` stubs
  (`00 23 1b 60 ff de` x3), and the id-5 registration literal `0x7DDC` = `0x000C19DD`.
* `pcie_msg_send` (`plat.ko`) has exactly **two** `R_ARM_CALL` sites, `{0x15144, 0x178f8}` - id 5
  and id 3, so "id 5 has one producer" holds.
* Sample quoted instructions reproduce exactly: `plat 0x15144 bl` (to `pcie_msg_send`),
  `plat 0x15024 b #0x15138`, `plat 0x160f4 push {r4,r5,r6,r7,r8,lr}`, `plat 0x172f8 bics r5,r5,r3,lsl r6`,
  `firmware 0x2a0 ldr r0,[pc,#4]` / `0x2a2 b.w #0x86108`, `0x8192A push {r4}`, `0x81936 str.w r4,[r3,r2,lsl #3]`.

The report's conclusions that rest on *unreachability* (the "statically not resolvable" reachability
notes) are stated as unresolved in the report itself and were not scored as facts; every *positive*
offset and structural claim checked here holds.

**Verdict: PASS** - every quoted offset re-decodes identically and each structural claim reproduces.

---

## 4. `web-sdk-research.md` - PASS

Six cited URLs re-fetched this session; all returned content, and the report's cited byte counts
match exactly:

| source | report says | re-fetched | content-level check |
| --- | --- | --- | --- |
| `hcc_comm.h` (OpenHarmony hi3881v100) | 200, 9,621 B | 9,621 B | `struct hcc_header`, `pay_len`, `HCC_HDR_TOTAL_LEN 64`, `hcc_extend_hdr`, `HCC_CONFIG_FRAME 0x80` all present (5/5) |
| `oal_sdio_comm.h` | 200, 6,190 B | 6,190 B | `D2H_MSG_WLAN_READY`, `D2H_MSG_FLOWCTRL_OFF`, `H2D_MSG_TEST`, `H2D_MSG_DEVICE_MEM_INFO`, `D2H_MSG_COUNT` all present (5/5) |
| `hcc_host.c` | 200, 51,018 B | 51,018 B | fetched OK |
| `oal_hcc_bus.c` (hi1105) | 200, 50,778 B | 50,778 B | `SDIO/PCIE/USB`, `hcc_message_register` present (2/2) |
| `pcie_firmware_msg.c` (hi1105) | 200, 20,365 B | 20,365 B | `frw_ringbuf_t`, `firmware_msg` present (2/2) |
| Hi1105 product page (hisilicon.com) | 200, 1,011 B | 1,139 B raw text | 802.11ax / "2x2 160 MHz" / PCIe interface content present |

The report's negative-fetch claims were spot-checked: the OpenWrt forum thread and the gitee mirror
still return **0 bytes**. (The gitcode mirror now serves the page instead of the rate-limit notice
the report recorded - a transient that changes no verdict, and the report already flags gitcode as
unusable for evidence.)

The report's own framing - that **nothing public documents `hi5622v100`/luofu, the mailbox block, or
these ids**, and that the cited sources are *sibling* chips (different transport, not
byte-compatible) - is consistent with everything fetched.

**Verdict: PASS** - all cited URLs return content, cited byte counts match, and content-level claims hold.

---

## Summary

| report | verdict | basis |
| --- | --- | --- |
| `announce-body-bytes.md` | **PASS** | 59/59 byte triples + mnemonics; 0 wifi `5a5a`; unique builder; 3 call sites |
| `d2h-vocabulary-verified.md` | **PASS** | 6 tables / 79 slots / 77 relocs / 2 nulls / 1 import; all ids + sampled symbols reproduce |
| `open-gaps-static-attempt.md` | **PASS** | 43/43 firmware + 19/19 plat.ko offsets; doorbell/pool/stub/provenance claims reproduce |
| `web-sdk-research.md` | **PASS** | 6/6 URLs return content; byte counts exact; content-level tokens present |

No report contained a claim that failed re-derivation.
