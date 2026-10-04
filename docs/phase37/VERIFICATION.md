# Phase-37 report verification (independent re-check, 2026-10-04)

Task `st_01a105b1`. Every claim quoted below was re-derived from the raw binaries in this session
with the repo-local capstone 5.0.7 (`pyenv/Scripts/python.exe`) - `CS_ARCH_ARM/CS_MODE_ARM` for the
two `.ko` modules, `CS_ARCH_ARM/CS_MODE_THUMB` for `FIRMWARE.bin` - and compared with the report
text. This file is the only artifact written by the verification pass; the checker lives at
`build/tmp/phase37_verify.py` (gitignored scratch).

## Verdicts

| # | report | verdict |
| --- | --- | --- |
| 1 | `sr-message-catalog.md` | **PASS** |
| 2 | `d2h-message-catalog.md` | **FAIL** - one wrong claim: section 7's log line `0x0018c0: movw r2, #0 -> .LC19` |
| 3 | `fw-ring-message-table.md` | **PASS** |
| 4 | `boot-dialogue-sequence.md` | **PASS** |

Verdict rule used: a report PASSes when every load-bearing offset/byte/instruction/relocation claim
it makes reproduces against the cited binary; it FAILs when any quoted claim is contradicted by the
binary. A cosmetic count in a report's own self-test prose is recorded as a note, not a verdict
change, when the underlying claims verify.

## Artifacts (md5 re-confirmed this session)

| artifact | size | md5 |
| --- | --- | --- |
| `opensource/build/tmp/hi5622v100_wifi.ko` | 3,564,728 B | `4737fcb21a1a2262a96f84d780ad8b35` |
| `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | 364,660 B | `23660bc285393e678d5cade1c36c194b` |
| `build/tmp/FIRMWARE.bin` | 928,920 B | `0e530b976d5a20e87358671f1a577695` |

All three md5s match the values printed in the four reports. `plat.ko` is byte-identical to
`build/tmp/hi5622v100_plat.ko`; `wifi.ko` to `build/register-dumps/wifidrv0/dev-modules/hi5622v100_wifi.ko`.

Conventions applied for decoding (they are the binary's, not the report's):
`.ko` offsets are section-relative (`sh_addr = 0`), so the file offset is
`symbol_offset + (sh_offset - sh_addr)` (`.text` base `0x38`, `.text.unlikely` base `0x1d35c`);
firmware offsets are file offsets and `FIRMWARE.bin` links at runtime base `0x40000`, so branch
targets are read at `file + 0x40000` (i.e. capstone prints `#0xc187a` for what a report calls
`#0x8187a`).

## Checker summary

`pyenv/Scripts/python.exe build/tmp/phase37_verify.py` -> **exit 0**, `TOTAL CHECKS: 599`,
`UNEXPECTED FAILURES: 0`, `EXPECTED/DOCUMENTED DEFECTS CONFIRMED: 1`.

The checker covers: (a) every `` `offset` `bytes` `mnemonic` `` triple extracted from
`sr-message-catalog.md` (module- and section-resolved); (b) explicit addressed instruction checks
for the other three reports; (c) literal-pool word and raw-byte checks; (d) ELF relocation-target
checks (`bl`/`b` to a named symbol) for the registration and call sites the reports name; (e) the
structural claims (caller-set sizes, pointer words, section containment).

---

## 1. `sr-message-catalog.md` - PASS

Evidence:

* All **190** distinct `` `offset` `bytes` `mnemonic` `` triples in the file were parsed and
  matched to an instruction in the correct section of `wifi.ko` or `plat.ko`: bytes equal and
  capstone mnemonic equal. PASS 190 / FAIL 0.
  (Reproduce: the report's own regex, run over the file, yields 190 triples - all distinct offsets.)
* The `plat` header/domain quotes reproduce exactly, e.g.
  `0x11d9c 743600e3 movw r3, #0x674`, `0x11db4 0c5080e2 add r5, r0, #0xc`,
  `0x11e18 b450c2e1 strh r5, [r2, #4]`, `0x11e10 0100a0e3 mov r0, #1`,
  `0x11e1c 1030c3e7 bfi r3, r0, #0, #4`, `0x12a20 0f1001e2 and r1, r1, #0xf`,
  `0x11c30/0x11c34/0x11c38` (the retry-counter copy), and the `hcc_msg_process` trio
  `0x12084 0030d2e5` / `0x1208c 0f3003e2` / `0x1209c b620d2e1`.
* The post-path quotes reproduce: `0x11bf8 10402de9 push {r4, lr}`,
  `0x112b0 0620d2e5 ldrb r2, [r2, #6]`, `0x10ba4 182092e5 ldr r2, [r2, #0x18]`,
  `0x167e4 003051e2 subs r3, r1, #0`, `0x178f4 0310a0e3 mov r1, #3`,
  `0x178cc 821183e7 str r1, [r3, r2, lsl #3]`, `0x178e0 042083e5 str r2, [r3, #4]`.
* The shared-helper quotes reproduce: `0x5714c f0412de9 push {r4,r5,r6,r7,r8,lr}`,
  `0x57168 b042dde1 ldrh r4, [sp, #0x20]`, `0x5716c 100084e2 add r0, r4, #0x10`,
  `0x57190 b680c4e1 strh r8, [r4, #6]`, `0x57198 1f30c3e7 bfc r3, #0, #4`; and the two
  helper call sites `0x57294 acffffeb bl #0x5714c` (`hmac_config_send_event`) and
  `0x60cbc 22d9ffeb bl #0x5714c` (`hmac_config_alg_send_event`). `0x57174` carries an
  `R_ARM_CALL` relocation to `hcc_msg_alloc`.
* The WAL analogue quotes reproduce: `0x126f68 0220a0e3 mov r2, #2`,
  `0x126f6c 0640cce5 strb r4, [ip, #6]`, `0x126f78 1230c3e7 bfi r3, r2, #0, #4`.
* Firmware: `FIRMWARE.bin` file `0xcc1b0` = `68 8d 11 00` = `0x00118d68` (the H2D table pointer),
  as quoted.
* The `0x5a5a`-absence claim holds: zero occurrences of the byte pair `5a 5a` in `wifi.ko`
  `.text`/`.data`/`.rodata`.
* Section containment: the quoted `.ko` offsets (`0x11bf8`, `0x11c3c`, `0x178f4`, `0x178cc`,
  `0x11d9c`, `0x1204c`, `0x59c0`, `0xb4090`, `0x128b7c`, `0xf58a8`, `0x5bf8`) all fall inside
  `.text` of the named module.

Note (not a verdict change): the report's section 4 says "This catalog quotes 224 triples ...
PASS 224". The file actually contains **190** distinct `` `off` `bytes` `mnemonic` `` triples (the
report's own regex finds 190), and all 190 pass. The "224" is an error in the self-reported count,
not in any load-bearing claim; every triple present is correct.

## 2. `d2h-message-catalog.md` - FAIL

The report's substantive content verifies. Section 7's verification log contains **one line that the
binary contradicts**:

> `0x0018c0: movw r2, #0 -> .LC19 [R_ARM_MOVW_ABS_NC]  exception_info_msg_process (.text.unlikely)`

Actual bytes at `plat.ko` `.text.unlikely` offset `0x18c0`:

```
0x18c0 70402de9 push {r4, r5, r6, lr}
0x18c4 184190e5 ldr r4, [r0, #0x118]
0x18c8 01c0d4e5 ldrb ip, [r4, #1]
0x18cc f0601ce2 ands r6, ip, #0xf0
```

The instruction at `0x18c0` is `push {r4, r5, r6, lr}`, not `movw r2, #0`; the `movw r0, #0`
instruction is at `0x18e0` (with an `R_ARM_MOVW_ABS_NC` relocation to `.LC74`, not `.LC19`), and the
register is `r0`, not `r2`. The symbol `exception_info_msg_process` *is* at `.text.unlikely` `0x18c0`
(confirmed: it is an `STT_FUNC`, size 124), so only the quoted instruction line is wrong.

Everything else checked reproduces:

* `plat` `pcie_msg_init` @ `0xb6e4`: the four `pcie_msg_register` calls with ids 1/3/6/7 at
  `0xb82c`/`0xb858`/`0xb884`/`0xb8b8` (each an `R_ARM_CALL` to `pcie_msg_register`); the id loads
  `0xb824 mov r1, #1`, `0xb854 mov r1, #6`, `0xb880 mov r1, #7`, `0xb8ac mov r1, #3`.
* `pcie_msg_register` @ `0x15fbc`: `0x15fcc cmp r1, #0xa`,
  `0x16004 strne r2, [r5, r4, lsl #3]`, `0x1600c strne r3, [r4, #4]`.
* `pcie_msg_handle` @ `0x171f8`: `0x17254 str r1, [r3]`, `0x1725c ldr r5, [r3]`,
  `0x17260 str r2, [r3]`, `0x172b4 rbit r6, r5`, `0x172b8 clz r6, r6`,
  `0x172d8 ldr r3, [r4, #0x20]`, `0x172dc ldr sl, [r3, r6, lsl #3]`,
  `0x172e8 beq #0x1739c`, `0x172f0 blx sl`.
* Handlers: `0x8784 bx lr` (id-1 stub); `0x8730/0x8738/0x873c` id-3 body; `0x15efc` is the id-6/7
  tail-call trampoline (`feffffea b`, relocation target `pcie_wkup_thread`, i.e. `b #0x1629c`);
  `0x1629c` entry and `0x162ac str r2, [r4, #0x28]`; group-4 handlers `0xeb78`, `0xebd8`/`0xebf8`/
  `0xec00`/`0xec7c`/`0xed24`, `0xdabc`/`0xdaf0`, `0x10214`/`0x10280`.
* `plat_init_bal_hcc_excp` @ `0xe380`: `0xe3a8 mov r2, #5`, `0xe3b4 mov r0, #4`,
  `0xe3b8 bl hcc_msg_register_tab_chip` (`R_ARM_CALL`).
* Group-4 `.data` table: `0x2660` id=0, `0x266c` id=1, `0x2678` id=2, `0x2684` id=3, `0x2690` id=4,
  with `.rel.data` handler relocations `+0x2664 -> oam_rx_post_action_function`,
  `+0x2670 -> device_plat_ready_msg_process`, `+0x267c -> host_ready_msg_process`,
  `+0x2688 -> exception_info_msg_process`, `+0x2694 -> heartbeat_msg_process`. This is exactly the
  report's correction of the phase-24 "off by one" - id 1 is `device_plat_ready`, id 2 is
  `host_ready` - and it is byte-verified.
* `hcc_msg_process` @ `0x1204c`: `0x12058 ldr r2, [r0, #0x118]`, `0x12084 ldrb r3, [r2]`,
  `0x1208c and r3, r3, #0xf`, `0x12090 cmp r3, ip`, `0x1209c ldrh r2, [r2, #6]`,
  `0x120b8 adds r3, r3, r2, lsl #4`, `0x120c0 ldr r3, [r3, #4]`, `0x120cc bx r3`.
* `hcc_msg_register_tab_customise` @ `0x118c4`: `0x11924/0x11940/0x11944/0x1194c/0x11950/0x1195c`
  all match.
* `wifi.ko` registration sites: `0x3f768 mov r2, #0x1c`, `0x3f780 mov r2, #0xc`,
  `0x3f790 mov r2, #8`, `0x123cd0 mov r2, #0x13`, `0x1354 mov r2, #5`, `0x1368 mov r2, #7`, with
  `R_ARM_CALL` targets `hcc_msg_register_tab_chip` (`0x3f778/0x3f788/0x3f798/0x123ce0/0x135c`) and
  `hcc_msg_register_tab_core` (`0x136c`).
* Firmware: `d2h_notify` @ file `0x86170` (`cmp r1, #0xa`, `0x861a8 ldr.w r1, [r5, #0x9c]`,
  `0x861b0 str r3, [r1]`, `0x861b2 ldr.w r4, [r5, #0xa4]`, `0x861c0 str r3, [r4]`), and the
  binding writer (`0x9706 str r2, [r3]`, `0x9760/0x9762` the `0x40101434`/`0x40039014` literal
  loads, `0x9784 strd r2, r3, [r5, #0xd0]`). The literal at file `0x861dc` = `0x00172130`.
* The four H2D send sites: `0x1513c mov r1, #5` / `0x15144 bl pcie_msg_send` (`R_ARM_CALL`) and
  `0x178f4 mov r1, #3` / `0x178f8 bl pcie_msg_send` (`R_ARM_CALL`); the other two
  (`0xaf8` `__ksymtab`, `0x2920` `.data`) are data references, not calls.

**FAIL claim (exact):** section 7, the `exception_info_msg_process (.text.unlikely)` log line reads
`0x0018c0: movw r2, #0 -> .LC19 [R_ARM_MOVW_ABS_NC]`; the binary has
`0x18c0: push {r4, r5, r6, lr}` (the `movw r0, #0 -> .LC74` is at `0x18e0`).

## 3. `fw-ring-message-table.md` - PASS

Evidence:

* Dispatcher @ file `0x818ac`, whole body: `0x818ac push {r3,r4,r5,r6,r7,lr}`,
  `0x818ae mov r6, r0`, `0x818b0 cbz r0, #0x818c8`, `0x818b2 movs r7, #1`,
  `0x818b6 ldr r2, [r0, #0xc]`, `0x818b8 str r7, [r2]`, `0x818ba ldr r2, [r0, #4]`,
  `0x818bc ldr r5, [r2]`, `0x818be str r1, [r2]`, `0x818c0 movs r1, #8`,
  `0x818c4 str r1, [r2]`, `0x818c6 cbnz r5, #0x818ca`, `0x818c8 pop {r3,r4,r5,r6,r7,pc}`,
  `0x818ca rsbs r4, r5, #0`, `0x818cc ands r4, r5`, `0x818ce clz r4, r4`,
  `0x818d2 rsb.w r4, r4, #0x1f`, `0x818d6 cmp r4, #9`, `0x818d8 bhi #0x818c8`,
  `0x818da ldr r3, [r6, #0x20]`, `0x818dc add.w r2, r3, r4, lsl #3`,
  `0x818e0 ldr.w r3, [r3, r4, lsl #3]`, `0x818e4 cbz r3, #0x818ea`,
  `0x818e6 ldr r0, [r2, #4]`, `0x818e8 blx r3`, `0x818ea lsl.w r4, r7, r4`,
  `0x818ee bic.w r5, r5, r4`, `0x818f2 b #0x818c6`. (All branch targets print at the
  runtime base; the report's `#0xc18xx` values are the same targets as `file + 0x40000`.)
* Registrar A @ file `0x8187a` .. `0x81898`: all quoted instructions match. Caller sweep (linear
  Thumb sweep, whole blob) finds **exactly one** `BL` to `0x8187a`: file `0x9838` - matching the
  report's "exactly one caller". `0x982a ldr r2, [pc, #0x38]`, `0x9830 mov r1, r6`,
  `0x9832 add.w r3, r5, #0x34`, `0x9838 bl #0xc187a`.
* Registrar B @ file `0x7c1dc` .. `0x7c1f2`: all quoted instructions match. Caller sweep finds
  **exactly three** `BL`s to `0x7c1dc`: file `0x7cf0` (id 5), `0x8226` (id 1), `0x88f8` (id 6) -
  matching the report's table (`0x8224/0x8226`, `0x7cee/0x7cf0`, `0x88ea/0x88f8`).
* id-1 handler @ file `0x510`: `0x510 ldr r3, [pc, #0xc]`, `0x512 add.w r1, r3, #0x274`,
  `0x516 ldr.w r0, [r3, #0x270]`, `0x51a b.w #0xc6d0c`; tail target `0x86d0c` body
  (`0x86d0e/0x86d10/0x86d12/0x86d1a/0x86d1e/0x86d38/0x86d48`) all match; list-add @ `0x82114`
  through `0x8211e` matches.
* id-3 handler @ file `0x85144` -> `0x85128`: `0x85144 b #0x85128` thunk, and
  `0x85128 push {r4,lr}` .. `0x8513e b.w #0x82a54` match; the `ldrex/strex` primitive
  `0xc24bc..0xc24cc` matches.
* id-5 handler @ file `0x819dc`: raw bytes `00 23 1b 60 ff de` = `movs r3,#0` / `str r3,[r3]` /
  `udf #0xff` - the `BUG()` trap, as quoted.
* id-6 handler @ file `0x4cce4` .. `0x4cd6e`: the prologue, the `ldr r3, [pc, #0xa8]`,
  `0x4cd2c/0x4cd38/0x4cd44/0x4cd46/0x4cd60/0x4cd64/0x4cd68/0x4cd6a` all match.
* Message format: constructor `0xc2822` (`0xc2838 adds r4, #0xc`, `0xc285c strh r4, [r0,#4]`,
  `0xc285e bfc r3, #0,#4`), length check `0x62354/0x62356/0x62358/0x6235a`, decoder `0x8f8` ..
  `0x91e` - all match.
* Pointer words: `0x29c -> 0x0010C190`, `0x2a8`/`0x2b0 -> 0x0010C0F4`, `0x520 -> 0x00117C00`,
  `0x7c1fc -> 0x00170E08`, `0x818a8 -> 0x00172130`, `0x84d0 -> 0x00040511`,
  `0x7ddc -> 0x000c19dd`, `0x8994 -> 0x0008cce5`, `0x9864 -> 0x000c5145`,
  `0x4cd94 -> 0x40100100`, `0xcc1b0 -> 0x00118D68`, `0x97b0 -> 0x0010C0C0` - all 13 read the
  quoted value.

## 4. `boot-dialogue-sequence.md` - PASS

Evidence:

* `plat` send path: `pcie_msg_send` @ `0x160f4` body quotes `0x16194 ldr r2, [r6, #0x2c]`,
  `0x1619c str r3, [r2]`, `0x161a4 ldr r2, [r6, #0x34]`, `0x161ac orr r3, r3, r1`,
  `0x161b0 str r3, [r2]` all match. `pcie_msg_send_irq` @ `0x174a8` quotes `0x174f8`,
  `0x17504`, `0x1753c`, `0x1754c`, `0x17550` all match. The callback registration
  `0xb794 movw fp, #0` + `0xb7d0 str fp, [r3, #0x60]` matches.
* id-3 post: `shuangta_ete_sr_dscr_fill` @ `0x17858` - `0x178c0 ldr r2, [r4, #0xc]`,
  `0x178cc str r1, [r3, r2, lsl #3]`, `0x178e0 str r2, [r3, #4]`, `0x178f0 ldr r0, [r5, #0x80]`,
  `0x178f4 mov r1, #3`, and `0x178f8 bl pcie_msg_send` (`R_ARM_CALL`) all match.
* id-5 post: `pcie_ete_rcv_buff_check` @ `0x14d74` - `0x15138 ldr r3, [r4, #0x68]`,
  `0x1513c mov r1, #5`, `0x15140 ldr r0, [r3, #0x80]`, `0x15144 bl pcie_msg_send`
  (`R_ARM_CALL`) all match; the reach chain relocations `0x1655c -> pcie_ete_dr_get_uploadbuf`
  and `0x152c4 -> pcie_ete_rcv_buff_check` both resolve.
* D2H id 6/7: `0xb854 mov r1, #6` + `0xb858 bl pcie_msg_register`,
  `0xb880 mov r1, #7` + `0xb884 bl pcie_msg_register`; `0x15efc` tail-call relocation target is
  `pcie_wkup_thread`.
* Firmware id 2: `0x86f54 movs r2, #4`, `0x86f5a str r2, [r3]`, `0x86f62 add.w r3, r3, #0x420`,
  `0x86f66 str r2, [r3]`; literal pools `0x97e0 -> 0x40039010`, `0x97dc -> 0x40039014`,
  `0x86fbc -> 0x40039014`, `0x29c -> 0x10c190`, `0x2a8 -> 0x10c0f4`, `0x9864 -> 0xc5145`.
* Init order `init_module` @ `0x1a75c` and `plat_hcc_init` @ `0x1a690`: every quoted `bl`/`b`
  resolves through `.rel.text` to the named symbol (`plat_res_init`, `oal_main_init`,
  `oam_main_init`, `sdt_drv_main_init`, `low_power_init`, `plat_hcc_init`; then `plat_custom_init`,
  `plat_main_init`, `pcie_init_static_res`, `bal_init`, `hcc_init`, `plat_exception_init`).
  `pcie_comm_init` @ `0x171cc` -> `0x171e8 bl pcie_thread_init` and `0x171f4 b pcie_msg_init`.
  `pcie_main_init` `.text.unlikely` `0x768/0x8e8/0x944` resolve to `pcie_init_default`,
  `pcie_ete_init`, `pcie_comm_init`. `pcie_ete_init` `0x78b8/0x78d4` resolve to
  `pcie_ete_intr_init`/`pcie_ete_rings_init`.
* Section 5 relocation-type audit reproduces: `0xb794` type 43 (`R_ARM_MOVW_ABS_NC`), `0xb798`
  type 44 (`R_ARM_MOVT_ABS`), `0x15144` type 28 (`R_ARM_CALL`), `0x178f8` type 28 - i.e. only two
  of the sites the phase-36 scan called "init-path call sites" are actually calls, as the report
  states.
* Firmware dispatcher mirror quotes (`0x818b8`..`0x818e8`) and the table-register stores
  (`0x81894 str.w r2, [r4, r1, lsl #3]`, `0x81898 str r3, [r5, #4]`) match; the report's
  `0x982a/0x9838` register call matches (target printed at runtime base as `#0xc187a`).

Note (not a verdict change): the report's own checker prints branch operands in **file-offset**
form (`bl #0x8187a`, `b #0x1629c`) because it disassembles with address = file offset; decoded at
the true runtime base the operands print as `#0xc187a` / `#0x1629c`-equivalent targets. The
*instruction bytes and relocation targets* match; only the printed numeric style differs, and the
report documents this convention.

## Reproducer

```bash
cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2
pyenv/Scripts/python.exe build/tmp/phase37_verify.py    # exit 0, 599 checks, 0 unexpected failures
```

(`build/tmp/phase37_verify.py` is gitignored scratch; the four reports and this file are the
tracked artifacts.)
