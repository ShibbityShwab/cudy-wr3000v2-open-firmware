# TABLES - address, boot, protocol and patch tables

Companion data file for [REPORT.md](REPORT.md). Every table names its evidence sources;
the narrative in REPORT.md explains why each row matters. Evidence markers:

| marker | meaning |
| --- | --- |
| `[V]` | re-verified by the lane that first recorded it, from a named local artifact, with a committed reproduction tool |
| `[V*]` | measured on the bench (device-attended session) by an earlier attended lane; cited by ledger date |
| `[I]` | inference from code/data; not measured directly |
| `[U]` | unknown / needs a device read, ROM evidence or vendor source |

Ledger dates refer to `mem-entries.md` in the superproject (append-only, newest wins); lane
reports are the paths under `opensource/docs/incoming/`. This file restates facts; it does not
create new ones - where a row disagrees with its source, the source wins and this file is fixed.

---

## T1. Host BAR0 window map (endpoint inbound iATU, six viewports)

The endpoint exposes a 16 MiB BAR0 divided into six device-side windows. The RC driver programs
the endpoint iATU with the **compact 7-word, absolutely-addressed** layout (source: wifi-transport-port
REPORT, bench-confirmed; phase18 A.4-A.6):

| idx | host BAR0 range | size | device CA target | content | evidence |
| ---: | --- | ---: | --- | --- | --- |
| 0 | `0x000000-0x1BFFFF` | 0x1C0000 | `0x00000000` | chip mask ROM (16 KiB) + WRAM | [V*] |
| 1 | `0x1C0000-0x1D7FFF` | 0x18000 | `0x00400000` | CPU1 ITCM/DTCM windows | [V*] |
| 2 | `0x1D8000-0x3B7FFF` | 0x1E0000 | `0x01000000` | PKTRAM: device ETE ring nodes | [V*] |
| 3 | `0x3B8000-0x4D7FFF` | 0x120000 | `0x40000000` | device IO block: boot gate, mailbox, ETE engine | [V] |
| 4 | `0x4D8000-0x6B7FFF` | 0x1E0000 | `0x02000000` | ACP SRAM: device D2H frame bodies | [V*] |
| 5 | `0x6B8000-0x8CFFFF` | 0x218000 | `0x01200000` | firmware CPU space (image, handshake page, TCM descriptors) | [V*] |

Host physical address of an endpoint window on the bench (endpoint 0):
`phys = 0x40000000 + BAR0_offset` [V*]. The second RC endpoint enumerates with BAR0
`0x58000000` (source: wifi-transport-port SUMMARY, A-run) [V*].

The MAC host-interface ring block (CA `0x40040000`/`0x40060000`, figure in REPORT 6.4) lies
**inside region 3**; its host offsets are therefore `0x3B8000+(CA-0x40000000)` = `0x418000` /
`0x438000`. An earlier lane recorded a bus error on a MAC-window read, so ladder B2 starts with a
single read-only probe before any host write [V]/[V*].

## T2. Address translation and naming rules (project-wide)

| rule | statement | evidence |
| --- | --- | --- |
| IO CA -> BAR0 | `BAR0 = 0x3B8000 + (CA - 0x40000000)`, valid for device CAs in `0x40000000..0x4011FFFF` (region 3) | [V] |
| **region-3 mask bug** | the inherited macro `HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)` is wrong for CAs >= `0x40100000`: `out[3]` CA `0x40101438` must map to BAR0 `0x4B9438`, not `0x3B9438`; fixed in wifi-driver-clean and hardware-verified | [V*] |
| firmware CPU -> BAR0 | `BAR0 = 0x6B8000 + CPU`, valid for CPU `0x0..0x217FFF` (region 5) | [V] |
| ROM offset | ROM is mapped at CPU 0, so **ROM dump offset == CPU address** | [V] |
| image file offset | CPU address = file offset + `0x40000`; upload base CA `0x01240000` = CPU `0x40000` = file 0 | [V*] |
| **CPU vs device CA** | low addresses such as `0x40008`, `0x4000C`, `0x1BF000`, `0x118B60` are **firmware CPU** addresses (region 5); addresses >= `0x40000000` are **device IO CAs** (region 3). Do not apply the IO formula to CPU addresses | [V] |
| host physical | endpoint 0 BAR0 is mapped at host phys `0x40000000` | [V*] |
| D2H frame bodies | device ACP CA `0x02000000+`; host BAR0 via region 4 = `0x4D8000 + (CA - 0x02000000)` | [V*] |
| device ring nodes | PKTRAM CA `0x01000000+`; host BAR0 via region 2 = `0x1D8000 + (CA - 0x01000000)` | [V*] |
| distinct firmware objects | `0x10C0F4` = global message object (mailbox arg); `0x10C190` = dispatcher hcc instance (fields `+4` out0, `+0xC` ack CA `0x400392F0`, `+0x10` doorbell CA `0x400392D4`, `+0x20` handler table); `0x10F1F0` = hcc instance holding the ETE ring records - three different objects, relationship [U] | [V]/[U] |

## T3. Mask ROM map (16 KiB identity window; ROM cpu address == dump offset)

Source: wifi-rom-dump REPORT section 2, `tools/romdis.py` (86/86 selftest). ROM content is
2,073 bytes; `0x81C-0x3FFF` reads all-zero; phys `0x40004000+` reads `0xFFFFFFFF` (unmapped)
[V]/[V*].

| ROM range | content | evidence |
| --- | --- | --- |
| `0x000-0x01F` | ARM vector table: 5x `ldr pc,[pc,#0x18]` + NOP + 2x `ldr pc,[pc,#0x14]` | [V] |
| `0x020-0x03B` | 7 vector target words `0x8C,0x3C,0x4C,0x5C,0x6C,0x40064,0x7C` (IRQ target is image trampoline `0x40064`; FIQ target is ROM stub `0x7C`) | [V] |
| `0x03C-0x08B` | 5 ARM exception stubs x16 B: `push {r0}; ldr r0,=slot; ldr r0,[r0]; bx r0` | [V] |
| `0x08C-0x1DF` | ARM reset/boot path: caches/MPU off -> per-mode stacks -> MPU-style CP15 regions -> MPU on -> MPIDR dispatch | [V] |
| `0x1E0-0x1F8` | 7-word table: handler slots `0x40060,0x4005C,0x40058,0x40054,0x40050` then stack tops `0x1BF110, 0x1BEF00` | [V] |
| `0x1FC-0x60B` | Thumb-2 boot/S1 code (helpers, CPU0 primary path, CPU1 S1 path) | [V] |
| `0x60C-0x617` | primary/secondary Thumb entries `0x54D/0x565` + 2 ARM `ldr pc,[pc,#-4]` dispatch words | [V] |
| `0x618-0x7F7` | two field-merge tables (16 x 16 B + 14 x 16 B) used by the analog bring-up | [V] |
| `0x7F8-0x81B` | build banner: "Hi1152V100-family CS ROM", dated 2022-09-21 16:21:44 (identification only) | [V] |
| `0x81C-0x3FFF` | zero / RAZ - no ROM content | [V] |

Key ROM functions (same source):

| entry | role |
| --- | --- |
| `0x1FC` | read 16-bit `CA 0x4000010C` (the boot-stage gate) |
| `0x208` | write `0xBAAD` to `CA 0x4000010C` |
| `0x218` | write `0xCEED` to `CA 0x4000010C` |
| `0x344` / `0x4C4` | analog/PHY bring-up + AFE wait; full cold-boot fallback (`0xBAAD` -> analog -> page bit2 -> poll release -> `0xCEED`) |
| `0x480` | return 0 iff 16-bit `CA 0x40000108 == 0x5A5A` (host release predicate) |
| `0x50C` / `0x54C` | **CPU0 primary decision / entry**: page=0, gate check, `0xCEED`, re-read `[0x40000]`, `blx` image entry A, spin |
| `0x564` | **CPU1 secondary path (S1)**: `0xCEED` gate + `0xABCD` magic, WFI, writes page `+8 = 1<<core` at `0x5FE`, `blx` entry B `0xC742C` |

## T4. Boot contract objects

| object | address | width | golden/expected | evidence |
| --- | --- | --- | --- | --- |
| CPU release word | device CA `0x40000108` (BAR0 `0x3B8108`) | 32-bit | host writes `0x00005A5A` once, after upload + seeding | [V*] |
| boot-stage gate | device CA `0x4000010C` (BAR0 `0x3B810C`) | 16-bit used | ROM needs `0xCEED` to survive until page `+8 == 2`; image handlers spin for `0xCECE` | [V]/[V*] |
| handshake page | CPU `0x1BF000` (BAR0 `0x877000`, phys `0x40877000`) | 5 words | `0x86807FEF / 0x7 / 0x2 / 0xABCD` (+0x10 a non-contract changing value) | [V*] |
| page word 0 | page `+0` | 32-bit progress bitmap | `0x86807FEF` golden; bit14 only after `+8==2` | [V] |
| page word 1 | page `+4` | 32-bit | `0x7` golden (vendor) | [V*] |
| page word 2 (S1 ACK) | page `+8` | 32-bit | `2` written by mask-ROM CPU1 S1 only | [V]/[V*] |
| page word 3 | page `+C` | 32-bit | `0xABCD` written by image entry A | [V] |
| boot-task state | CPU `0x118B60` (BAR0 `0x7D0B60`) | 32-bit | `1` = boot task ran | [V*] |
| exception-seen bitmap | CPU `0x40008` (BAR0 `0x6F8008`) | halfword | `0` = no exceptions; see T18 | [V*] |
| first-fault context frame | CPU `0x4000C..0x4004B` (BAR0 `0x6F800C`) | 17 words | all zero when no exception taken | [V*] |
| boot slot / FIQ slot | CPU `0x17D42C` (BAR0 `0x82942C`) | 32-bit | `0x000C19D1` on a golden boot | [V*] |
| ROM status word | CPU `0x10938` | 32-bit | `0xE5823000` on stock | [V*] |
| TCM ITCM window | CA `0x00400000` (BAR0 `0x1C0000`) | word0 | `0xE59FF018` (vendor + ours) | [V*] |
| TCM DTCM window | CA `0x00410000` (BAR0 `0x1D0000`) | word0 | `0x40034000` (vendor + ours) | [V*] |
| TCM coreword | BAR0 `0x1C0938` | 32-bit | `0x37C8F8D3` (vendor + GP4/GP5/GP8 ours) | [V*] |
| platform-custom block | CPU `0x1B2800` (BAR0 `0x86A800`) | 24 words | see T6 | [V*] |
| wifi-custom block | CPU `0x1B2860` (BAR0 `0x86A860`) | 618 words (0x9A8 B) | PHY/radio config; local-only capture | [V*] |
| firmware upload | CA `0x01240000` (file 0, CPU `0x40000`) | 928,920 B | contiguous, verify window BAR0 `0x6F8000` diffs=0 | [V*] |
| image entry A | CPU `0x46970` (file `0x6970`) | 32-bit header +0x00 | CPU0-dispatched by ROM | [V] |
| image entry B | CPU `0xC742C` (file `0xC342C`) | 32-bit header +0x04 | CPU1-dispatched after S1 | [V] |
| image exception trampoline | CPU `0x40064` | code | ROM IRQ target -> event dispatcher `0xC2EFC` | [V] |
| image vector/FIQ handler table | header `+0x50..+0x60` | 5 words | `{0x103140, 0x103114, 0x1030E8, 0x1030BC, 0x103090}` = {FIQ, data-abort, prefetch-abort, SVC, undefined} | [V] |

## T5. Golden boot state - vendor vs our boots

| observation | stock vendor boot | our GP8 (unpatched fw) | our PD3/PD4 patched | ordered lap (PD4) | evidence |
| --- | --- | --- | --- | --- | --- |
| page `+0` | `0x86807FEF` | `0x86807FEF` | `0x86807FEF` (intact) | `0x86807FEF` | [V*] |
| page `+4` | `0x7` | `0x7` | `0x7` | `0x7` | [V*] |
| page `+8` | `0x2` | `0x2` | `0x2` | `0x2` | [V*] |
| page `+C` | `0xABCD` | `0xABCD` | `0xABCD` | `0xABCD` | [V*] |
| state CPU `0x118B60` | 1 (working vendor stack) | `1` | `1` | `1` | [V*] |
| exception mask (CPU `0x40008`) | `0` | `0` | `0` (after successful dispatcher lap) | `0` | [V*] |
| TCM `0xE59FF018 / 0x40034000 / 0x37C8F8D3` | yes | yes (requires custom-block pre-seed) | yes | yes | [V*] |
| deferred notify `g+0xB4 / g+0xB8` | - | `0` only after the F4 fix (were stuck at `0x40`) | `0` | `0` | [V*] |
| ready pair `g4 id1 + id2` (direct read) | vendor stack consumes them | seen: id1 + id2 payload `0x01060850` | seen | seen | [V*] |
| `out[0]` H2D pending after one announce | - | `0x8` stuck (firmware consumer dead) | `0x8 -> 0` in 0 polls | `0x8 -> 0` | [V*] |
| exception mask after TXQ sync | n/a | n/a | solo: `0x12` (fault) | ordered: `0` | [V*] |

## T6. Platform-custom block (24 words at CPU 0x1B2800; captured read-only from the stock boot)

Captured 2026-11-24 05:00 from a working vendor boot. Our transport must write it before release
(the driver builds words 0-3 from the base plugin / INI; words 4-23 are the captured vendor
block). The four TCM section bounds are words 1-4 (offsets `+4..+0x10`) [V*].

| word | offset | value | meaning |
| ---: | ---: | ---: | --- |
| 0 | +0x00 | `0` | reserved |
| 1 | +0x04 | `0x00117BC4` | ITCM source address (CPU) |
| 2 | +0x08 | `0x0000A968` | ITCM length |
| 3 | +0x0C | `0x0012252C` | DTCM source address (CPU) |
| 4 | +0x10 | `0x0000076C` | DTCM length |
| 5 | +0x14 | `0x001B2800` | self address (platform block) |
| 6 | +0x18 | `0x00000060` | self size (0x60 B) |
| 7 | +0x1C | `0x001B2860` | wifi-custom block address |
| 8 | +0x20 | `0x000009A8` | wifi-custom block size |
| 9-11 | +0x24.. | `0x2, 0x1, 0x28` | boot/handoff parameters [I] |
| 12-23 | +0x30..+0x5C | `0x64, 0x1CC, 0xFFFFC92D, 0xFFFFFFC0, 0x1DA, 0xFFFFC97D, 0xFFFFFFA0, 0x1ED, 0xFFFFC92D, 0xFFFFFFC0, 0x80000000, 0` | analog/PHY merge parameters [I] |

## T7. Wi-Fi-custom block (618 words at CPU 0x1B2860; local-only evidence)

Captured read-only from the stock boot 2026-11-24 06:20. It is PHY/radio configuration data; it
contains **host-DRAM-range words** (e.g. idx 612 = `0x844C0000`, idx 614 = `0x83DDA000`), so a
blind byte copy is unsafe and the block is staged at bench time from a local file, never
committed or blindly replicated [V*]/[I].

## T8. HCC wire frame - the 12-byte header

Source: wifi-ete-frames REPORT section 1.3 [V] (vendor host builders in `hi5622v100_plat.ko`).

| offset | width | field | notes |
| ---: | ---: | --- | --- |
| +0x00 | u8 | group (low nibble) | dispatch key; groups used: 0, 1, 2, 3, 4 |
| +0x01 | u8 | low nibble = alloc state (vendor 1); high nibble = resource/core | high nibble is copied to +0x09 by `hcc_msg_tx` |
| +0x02 | u8 | field A | message-specific (vap id in several builders) |
| +0x03 | u8 | field B | user/vap/module-id selector |
| +0x04 | u16 | total length = payload + 12 | `hcc_msg_alloc` bound payload `0x674` |
| +0x06 | u16 | message id | dispatch key in group |
| +0x08 | u8 | field C | message-specific |
| +0x09 | u8 | resource/queue selector | copied from +0x01 high nibble |
| +0x0A | u16 | transport tag `0x5A5A` | required on every HCC frame |
| +0x0C | ... | payload | up to 0x674 B |

## T9. Mailbox layer (the six "out" words) and the H2D mailbox ids

Source: wifi-host-replica REPORT sections 3-4, wifi-h2d-catalog REPORT section 3 [V].

| register | CA | host BAR0 | direction | use |
| --- | --- | --- | --- | --- |
| `out[0]` H2D pending mask | `0x40039010` | `0x3F1010` | host write, device clears | publish `1<<mailbox_id`; polled until 0 |
| `out[1]` D2H bitmap | `0x40039014` | `0x3F1014` | device write, host read | pending D2H mailbox ids; bit2 = vector/exception announce |
| `out[2]` H2D doorbell | `0x400392D4` | `0x3F12D4` | host OR bit0 | ring the device |
| `out[3]` D2H ack | `0x40101438` | `0x4B9438` | host write 1 | first step of the vendor D2H service order |
| `out[4]` D2H re-arm | `0x40101414` | `0x4B9414` | host write 1 | last step of the vendor D2H service order |
| `out[5]` | `0x400392F0` | `0x3F12F0` | **firmware-owned** | host must NEVER write (hard rule); firmware writes it as its dispatch ack pulse |
| D2H doorbell | `0x40101434` | `0x4B9434` | device -> host, host read-only | D2H notify |

Mailbox-layer H2D ids (the value posted in `out[0]`):

| id | firmware handler | content | host rule | evidence |
| ---: | --- | --- | --- | --- |
| 1 | CPU `0x40511` / file `0x510` | none (bit only) | safe liveness/PM sleep path; vendor PM layer uses it after boot | [V] |
| 3 | CPU `0xC5145` -> body `0xC5128` | none at mailbox layer; body is the SR ring | the only announce for every SR commit | [V] |
| 5 | CPU `0xC19DD` / file `0x819DC` | - | **TRAP** (`movs r3,#0; str r3,[r3]; udf #0xff`) - never post | [V] |
| 6 | CPU `0x8CCE5` / file `0x4CCE4` | none (bit only) | firmware reply primitive (device word `CA 0x40100100` -> 20-byte group/type message) | [V] |
| 0,2,4,7,8,9 | - | - | no registration; dispatcher drops them | [V] |


## T10. Firmware H2D receive tables (60 registered slots)

Source: wifi-h2d-catalog REPORT sections 4-6 and the generated
`wifi-h2d-frames/catalogue.md` (machine-readable; risk classes from the per-handler payload-access
census). Format below: id = handler CA (risk). Groups 0 and 2 are the only H2D receive groups;
group 4 is device->host only [V].

**Group 0 - 40 registered ids + the early single id 0x2f (41 slots):**

| id | handler CA (risk) |
| --- | --- |
| 0x00 | `0x00081695` (NO_ACCESS) |
| 0x01 | `0x00043D59` (PTR_DEREF) - the config-event group used by key 0x64 / key 0xD8 |
| 0x02 | `0x000D6069` (PTR_DEREF) |
| 0x03 | `0x000B5FE1` (PTR_DEREF) |
| 0x06 | `0x000601FD` (PTR_DEREF) |
| 0x07 | `0x0009832D` (NO_ACCESS) |
| 0x08 | `0x00096225` (PTR_DEREF) |
| 0x09 | `0x000974F9` (NO_ACCESS) |
| 0x0A | `0x0005FF65` (PTR_DEREF) |
| 0x0B | `0x0005E2F5` (BYTE_READ) |
| 0x0C | `0x0006494D` (PTR_DEREF) |
| 0x0D | `0x0005DE79` (PTR_DEREF) |
| 0x0E | `0x000A4A65` (PTR_DEREF) |
| 0x0F | `0x00062EC1` (PTR_DEREF) |
| 0x10 | `0x0005E28D` (PTR_DEREF) |
| 0x11 | `0x00062E1D` (PTR_DEREF) |
| 0x12 | `0x0005DE7D` (PTR_DEREF) |
| 0x13 | `0x000968A5` (PTR_DEREF) |
| 0x14 | NULL (NO_HANDLER) |
| 0x15 | `0x00063A2D` (PTR_DEREF) |
| 0x16 | `0x00062D89` (NO_ACCESS) |
| 0x17 | `0x0005F951` (NO_ACCESS) |
| 0x18 | `0x0005F8BD` (PTR_DEREF) |
| 0x19 | `0x00061649` (PTR_DEREF) |
| 0x1C | `0x0005DFA1` (NO_ACCESS) |
| 0x1D | `0x000B568D` (NO_ACCESS) |
| 0x1E | `0x00087801` (PTR_DEREF) |
| 0x20 | `0x00085631` (NO_ACCESS) |
| 0x22 | `0x0008F669` (BYTE_READ) |
| 0x23 | `0x0008D4BD` (PTR_DEREF) |
| 0x24 | `0x000B55E1` (PTR_DEREF) |
| 0x27 | `0x00081B19` (PTR_DEREF) |
| 0x28 | `0x000B5FB3` (PTR_DEREF) |
| 0x29 | `0x0004BC51` (BYTE_READ) |
| 0x2B | `0x000A1521` (PTR_DEREF) |
| 0x2C | `0x0004AAE5` (NO_ACCESS) |
| 0x2D | `0x0004A8E9` (BYTE_READ) |
| 0x2E | `0x0008CFC5` (NO_ACCESS) |
| 0x2F | `0x00040C19` (NO_ACCESS) - the "early single id" registration |
| 0x30 | `0x000938F7` (BYTE_READ) |
| 0x31 | `0x0004A9E1` (BYTE_READ) |

**Group 2 - 19 registered ids:**

| id | handler CA (risk) |
| --- | --- |
| 0x02 | `0x000B9781` (NO_ACCESS) |
| 0x03 | `0x000BBC9D` (NO_ACCESS) |
| 0x04 | `0x0008F52D` (PTR_DEREF) |
| 0x05 | `0x000B9781` (NO_ACCESS) |
| 0x06 | `0x00092889` (PTR_DEREF) |
| 0x09 | `0x000627FD` (PTR_DEREF) |
| 0x0A | `0x000956A9` (PTR_DEREF) |
| 0x0B | `0x00060B4D` (BYTE_READ) |
| 0x0C | NULL (NO_HANDLER) |
| 0x0E | `0x0008E2AD` (NO_ACCESS) |
| 0x1A | `0x000608BD` (NO_ACCESS) |
| 0x1B | `0x000845E1` (PTR_DEREF) |
| 0x1F | `0x0004C083` (NO_ACCESS) |
| 0x23 | `0x0004B1B1` (PTR_DEREF) |
| 0x25 | `0x000811E3` (NO_ACCESS) |
| 0x26 | `0x0008785D` (PTR_DEREF) |
| 0x27 | `0x0008CF85` (NO_ACCESS) |
| 0x28 | `0x000C5ED1` (NO_ACCESS) |
| 0x2A | `0x0008F669` (BYTE_READ) |

Risk-class totals over all 60 slots: NO_ACCESS 20, BYTE_READ 8, PTR_DEREF 30, NO_HANDLER 2 [V].
Consequence: only payload-free/no-access handlers are safe to synthesize; PTR_DEREF frames must
be built from a real vendor capture or from the firmware parser.

## T11. Vendor post-ready startup order (userspace trace) and its HCC mapping

Source: wifi-vendor-userspace REPORT/TABLES (static trace of the stock 2.5.24 rootfs + 2.4.15
delta), cross-checked against wifi-host-replica [V*]/[V]. The driver owns boot/ready; no userspace
message makes DEVICE READY.

| # | event | host action | HCC message | response/event |
| ---: | --- | --- | --- | --- |
| 1 | boot | driver release + ready handshake | (device -> host g4 id1, g4 id2) | - |
| 2 | first post-ready | driver TXQ config sync | H2D g0 id1 cmd 0xD8 (key 0xD8), total 0x30 | observed g1 id3 (36 B) |
| 3 | calibration | 6 x alg set_* | H2D g0 id2, key 0x0101, 298 B | g1 id3, syn 0x0101, 298 B (C1 observed) |
| 4 | netifd VAP create | add_virtual_intf path | H2D g0 7/8 (hmac_user_add / notify) | - |
| 5 | hostapd start_ap | WAL cfg | group 2 config commands | - |
| 6 | scan | iw scan | H2D g0 0xC | g1 15-17 / g2 2 |
| 7 | STA join | wpa_supplicant | H2D g0 0xF/0x10 | connect events |

Footnote: the context-create message (g0 id1 key **0x64**) is not on the userspace trace; it is
the firmware-internal precondition our lane discovered for the TXQ-sync handler and must be sent
before step 2 on our boot (REPORT.md finding F5) [V*]/[I].

## T12. Device -> host (D2H) catalogue - observed and vendor-defined

Sources: wifi-hcc-re, wifi-host-replica (vendor host registration tables), wifi-d2h-direct REPORT
section 5 (observed), ledger 2026-11-24 14:00 and 2026-11-25 06:00.

| group | id | total observed | handler / meaning | evidence |
| ---: | ---: | ---: | --- | --- |
| 4 | 0x00 | 592 / 536 / 500 B classes | OAM device log blob (oam_rx_post_action) | [V*] |
| 4 | 0x01 | 12 B | device_plat_ready - first half of the ready handshake (bound 2.0 s) | [V*] |
| 4 | 0x02 | 16 B total, 4 B payload | host_ready = DEVICE READY (bound 20 s); payload 0x01060850 = device-RAM address of the read/MSDU-info-ring pointer table (CPU 0x1060850) | [V*] |
| 4 | 0x03 | 73 B | exception_info; payload starts "DEVICE EXCP_INFO" | [V*] |
| 4 | 0x04 | 22-byte nodes | heartbeat; payload carries ASCII HEARTBEAT; restarts the vendor 10 s heartbeat | [V*] |
| 3 | 0x12 | 16 B | hmac_d2h_temp_state_update (18) | [V*] |
| 1 | 0x03 | 36 B | firmware response observed after our TXQ config sync | [V*] |
| 1 | 0x12 | 40 B | firmware response observed after our context-create | [V*] |
| 3 | 15-17 / 2 | - | scan/ACS event classes (vendor registry) | [V] |

Vendor host registration counts (wifi-host-replica): D2H handler table ids {1,2,3,4,5,6,7};
ids 0/8/9/10 dropped; register rejects id > 10. The vendor D2H **event catalog** (message ids, not
the mailbox bitmap) contains 28 MAC events + 12 + 8 chip events + 5 boot messages + 7 hdpp - see
wifi-driver-re SUMMARY for the complete list.

## T13. Observed frames - exact bytes

All values are host-visible protocol bytes; no vendor binary/firmware content is reproduced.
The `??` marks header bytes the firmware leaves uninitialised in that builder [V]/[V*].

**F1 - ready handshake, device -> host, g4 id1 (12 B, no payload):**

```
04 00 ?? 00 0c 00 01 00 ?? 00 ?? ??
```

**F2 - ready handshake, device -> host, g4 id2 DEVICE READY (16 B, payload 4 B):**

```
04 00 ?? 00 10 00 02 00 ?? 00 ?? ?? 50 08 06 01      payload dword = 0x01060850
```

**F3 - host -> device, TXQ config sync 48 B wire (group 0 id 1, key 0xD8, len 0x14):**

```
000100043000010000005a5a0000000001000000d80014000100000000000000000000000000000000000000ffffffff
```

**F4 - host -> device, context-create (group 0 id 1, key 0x64, len 0x14, type 3; type 1/2 differ
only in payload[+0xC] = data[0]):**

```
000100043000010000005a5a0000000001000000640014000304000004000000000000000000000000000000ffffffff
```

**F5 - firmware response to TXQ config sync, g1 id3 (36 B) - captured from device ring dr0 tail
node {ptr CA 0x020493FC, len 0x24}:**

```
header : 01 00 00 04 24 00 03 00 00 00 5a 5a
payload: 00000000 00000000 000807e0 04010100 00010001 00000000
```

**F6 - firmware response to context-create, g1 id12 (40 B) - captured from device ring dr0 tail
node {ptr CA 0x02049440, len 0x28}:**

```
header : 01 00 00 12 28 00 12 00 00 00 5a 5a
payload: 00000000 0a000000 1c000000 00000000 00000000 00000000 0401000a
```

Note on F6: the ledger entry (2026-11-25 06:00) lists nine payload words; the header total
(0x28 = 40) implies seven (28 B), so the last two listed words are attributed to adjacent
staging and are marked [I]; re-read pending. The proto-seq lane later decoded both frames
structurally: g1 id3 is the generic config-sync channel (`syn_id` at payload+8; the 36 B frame is
an unsolicited VAP-MIB update with syn `0x07E0` and field B 4), and g1 id0x12 is the firmware
ACS-response family (field B 0x12, payload_len 28, body semantics device-side [U]) (correction C20).

**F7 - host -> device, calibration config event (group 0 id 2, key 0x0101, 298 B; C1):**

```
header : 00 01 00 04  2a 01  02 00  00 00 5a 5a    group 0, field B = module/vap (default 4),
                                                    total 0x12A = 298, id 2, tag 0x5A5A
payload: 00000000 00000000                          token[8]
         01 01                                       u16 key 0x0101
         0e 01                                       u16 len 270 (0x10E)
         <270-byte alg_cfg blob>                     cfg_id u16 | type 0 (SET) | pkt_type 0 |
                                                    dev_ret u32 0 | need_w4 1 | resv 0 |
                                                    value[260] (offset +0x0A within value area)
         00 00 00 00                                 inherited pad
wire / node : 298 B / 298 B    node word1 = (298 << 16) | 0x6D2B = 0x012A6D2B
```

Expected device answer: one D2H g1 id3, total 298 B, `syn_id 0x0101`, `body_len 270`, request
cfg id echoed, `dev_ret 0`, no host reply. C1 fired cfg id 3507 (`set_rssi_param`, zero values);
the observed answer was a g1 id3 frame with total/len 298 B (count 2, `last_total=0x012A`), body
recycled before capture [V]/[V*].

## T14. ETE rings and node format

Source: wifi-transport-port REPORT section 1, wifi-ete-frames/wifi-ete-engine, phase42/43 [V]/[V*].

| object | address | notes |
| --- | --- | --- |
| ETE channel file base | device CA 0x4003A000 (BAR0 0x3F2000), 7 x 0x50 | per-channel enable +0x00 bit0, ctrl +0x08, resource +0x28, second enable +0x48 |
| SR0/SR1/SR2 | block +0x400/+0x450/+0x4A0 | host ring in program group A +0x10..+0x1C; bench base example SR0 0x8360C000 (ch 0x400) |
| DR0-DR3 | block +0x590/+0x5E0/+0x630/+0x680 | host ring in group B +0x30..+0x3C; example DR0 base 0x83634000 (ch 0x590) |
| ETE engine global | device CA 0x4003A000 (engine global area +0x000..+0x3FF) | vendor golden value 0x10A |
| engine doors | 0x4003A7C0 / 0x4003A7D0 | vendor 0x83FAC000; ours initialized after GP1 |
| engine status | 0x4003A874 | 0 = idle (never started) |
| engine ack | 0x4003A870 | - |
| engine send words | 0x4003AC00+ / 0x4003AC18+ | device-authored only |
| ring node | 8 B | word0 = device-side address of the host-DRAM buffer; word1 = (len << 16) OR 0x6D2B |
| packed index | 10-bit index + 0x400 wrap-phase bit | depth-1 = 0x1F |
| host int/mask word | 0x40039508 | host RMW 0xFFE0F8F8 (vendor-exact arming) |
| device D2H AP staging rings | PKTRAM CA 0x01060110, 0x01060440 (BAR0 0x238110, 0x238440) | direct-read path (finding F4) |
| device D2H frame bodies | ACP CA 0x02000000+ (region 4 BAR0) | direct-read path |

## T15. Firmware patch recipes (research artifacts - not shipping changes)

The patched firmware images are built by `wifi-irq-unmask/tools/fw_pd_builder.py` and
`wifi-bench-tooling/tools/fw_patch_builder.py` from the pinned local vendor blob; only the
patched copies exist outside the repo (in /tmp/hi5622-patched). Source: wifi-irq-unmask
REPORT-PD2/PD3/PD4 + ledger entries (dates given).

| patch | entry hunk (file offset, bytes) | stub home | patched image sha256 (prefix) | bench result |
| --- | --- | --- | --- | --- |
| PD1 | 0x7610 31 46 03 48 BB F0 -> F8 F7 40 FE FC E7; 0x916A 00 20 21 B0 -> FE F7 51 BA | inline in the handshake retry body | f52a9322... | S1 completed but data abort (mask 0x10), page later clobbered; crash-loop - 2026-11-24 16:10 [V*] |
| PD2 | 0x916A -> FE F7 62 B9 (b.w 0x47432) | CPU 0x47432 (62 B; supposed dead config-3 block) | 7a691807...1b7728 | boot FAILED: page 0x6F/3/9DFBEF6A/0, state garbage - the cave is LIVE on our boot - 2026-11-24 19:00 [V*] |
| PD3 (A) | 0x0916A 00 20 21 B0 -> F6 F7 4F BF (b.w 0x4000C) | CPU 0x4000C..0x4004B (image-header exception-record area, 64 B zero) | eea9f2ff...3905ce | **SUCCESS**: golden page intact, state 1, mask 0; one announce consumed in 0 polls - 2026-11-24 22:10 [V*] |
| PD3B | no cave; event-0x4C raise loop in the boot epilogue | inline | ef0683db... | not fired on the bench at this report's cut-off [U] |
| PD4 | 0x0916A -> BF F0 69 B8 (b.w 0x108240) | CPU 0x108240 inside static-data gap 0x108197..0x108413 (637 B; zero branch/literal refs; outside both boot memsets and the exception staging area) | cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1 | cave verified present before/after; g0-id1 fault reproduces; ordered lap then succeeds with no exception - 2026-11-25 02:10 / 05:10 [V*]; audit/delta: T25 |

PD3/PD4 stub (our own code, 64 B at 0x4000C / 0x108240; identical logic in both patches):
`ldr r7,=0x10C19C` (hcc+0xC init guard) -> wait for [hcc+0xC] != 0 -> poll out[0] at
CA 0x40039010 -> on pending, set up the dispatcher arguments and `bl` the firmware wrapper
0x40294 -> loop / return to the boot-task epilogue. PD2 additionally substituted a consume-only
ROM RAZ window while the handler-table sentinel was still static.

## T16. Context-create verification pack (read-only bench checks)

Source: wifi-ctx-create README/REPORT and ledger 2026-11-25 05:10 [V*]. BAR0 = CPU + 0x6B8000.

| object | CPU CA | BAR0 | expected |
| --- | ---: | ---: | --- |
| module-table entry 4 (G+(0x18C+4)*4) | 0x171468 | 0x829468 | 0x0011C458 |
| ctx4 body | 0x11C458 | 0x7D4458 | 0 before; id+type after |
| **ctx4+0x190** (allocated buffer) | 0x11C5E8 | 0x7D45E8 | 0 before; non-zero after (observed 0x01011448 with type 1) |
| G singleton | 0x169E90 | 0x821E90 | 0x00170E28 |
| exception mask | 0x40008 | 0x6F8008 | 0x00000000 |
| first-fault frame | 0x4000C | 0x6F800C | all zero |

Observed post-create pack (type 1, 2026-11-25 05:10): ctx+0x190 = 0x01011448, ctx[0] = 4,
ctx+0x10 = 0x00010001, ctx+0x434 = 0x0100478C, ctx+0x187 = 0x2B000000, table entry
0x0011C458, G = 0x00170E28; no exception.

## T17. Timing and policy constants

Source: wifi-host-replica REPORT sections 6-7, wifi-hcc-core hcc_core.h [V].

| constant | value | note |
| --- | ---: | --- |
| HZ (vendor and our trees) | 100 | jiffy math |
| plat-ready bound | 2000 ms | 200 jiffies; g4 id1 |
| device-ready bound | 20000 ms | 2000 jiffies; g4 id2 |
| PM sleep ack | 500 ms | 50 jiffies |
| heartbeat | 10000 ms | restarted by g4 id4; vendor boot path leaves it OFF |
| H2D clear poll bound | 0xFFFE | pcie_msg_wait_for_clr |
| boot latch bound | 5000 ms | page valid ~0.2 s on GP8; latch once, never re-gate on a live page read |
| service tick | 5 ms recommended | polled D2H path |
| ring depth | 32 (0x1F) | SR/DR |

## T18. Exception-seen bitmap and image handler order

Source: wifi-rom-dump REPORT section 9 corrections [V].

| bit | value | handler |
| ---: | ---: | --- |
| 1 | 0x2 | undefined instruction |
| 2 | 0x4 | SVC |
| 3 | 0x8 | prefetch abort |
| 4 | 0x10 | data abort |

Observed masks: 0 = clean; 0x0A = undefined + prefetch (early boots, NOT svc+data-abort);
0x10 = data abort only (PD1); 0x12 = data abort + undefined (the two-stage cascade of the
solo TXQ-sync fault). Image header handler order at +0x50..+0x60:
{FIQ, data-abort, prefetch-abort, SVC, undefined}.

## T19. The bench session ladder (generic shape of every attended fire)

This is the shape of the sessions cited in this report; the operational details live in
`wifi-driver-clean/BENCH-PROCEDURE.md`, `wifi-bench-tooling`, `wifi-h2d-send/BENCH-PROCEDURE.md`
(Ladder C) and the `wifi-irq-unmask` recipes. **Never run any of this unattended.**

| step | action | pass gate |
| ---: | --- | --- |
| S-pre | host preflight (bench link, recovery door armed, module/firmware sha256 pinned) | all pins match; recovery READY |
| S0 | power-cycle / clean boot of the instrument image | both PCIe links train |
| S1 | insmod the instrument module; read-only probe | probe lines, no oops |
| S2 | prepare six iATU viewports; read back | 6/6 read back |
| S3 | upload firmware 928,920 B; verify target window | diffs = 0 |
| S4 | pre-seed platform block 24 words + wifi block 618 words; read back | read-back equal |
| S5 | arm ETE rings / IRQ mask RMW; start the polled service loop | rings armed |
| S6 | **release-only**: write 0x00005A5A to CA 0x40000108; never 0xCECE during boot | - |
| S7 | golden gate: page +8 == 2 and words 0x86807FEF/7/2/0xABCD; state 1; mask 0 | latch once, do not re-read forever |
| S8 | ready pair via direct reader; then at most the messages planned for that session | per-session plan; one variable per fire |
| S9 | calibration ladder C1: one `g0 id2` key 0x0101 frame after the ordered lap | announce consumed in 0 polls; one 298 B g1 id3 answer; page/state/mask clean |
| S10 | cfg80211 bench-image session: load `cfg80211.ko` + cfg80211-enabled driver | wiphy allocated; no device write from the cfg80211 layer |
| S11 | boot sequence -> ready pair -> wiphy registration | `/sys/class/ieee80211/phy0`; `iw phy` lists Wiphy phy0; VAP add/del works, no H2D sent by cfg80211 |

Stop rules (from the lanes' procedures): one idea per fire; never retry a frame that was not
consumed; never write CA 0x400392F0; never write 0xCECE during boot; no raw flash writes without
the preflight; capture dmesg/UART read-only, then power-cycle.

## T20. Where the detail lives (lane sources used by these tables)

| lane directory | owns |
| --- | --- |
| `wifi-rom-dump` | mask ROM map, S0/S1, the 0xCEED gate, ROM-side corrections |
| `wifi-rom-s01`, `wifi-rom-flow`, `wifi-s1-cpu1`, `wifi-chip-reset` | handshake page, S1 identity, TCM handoff, reset pulse |
| `wifi-ete-frames`, `wifi-ete-engine`, `wifi-ete-init` | wire frame header, ETE window/rings, engine wake analysis |
| `wifi-hcc-re`, `wifi-hcc-link`, `wifi-hcc-service`, `wifi-hcc-core` | HCC contract, vendor sequence, host service, tested state machine |
| `wifi-h2d-catalog`, `wifi-h2d-frames`, `wifi-h2d-inject`, `wifi-h2d-send` | H2D dispatch tables, byte-exact builders, send path, ordered ladder |
| `wifi-d2h-direct` | direct device-ring D2H receive service |
| `wifi-ctx-create` | context-create RE and the ordered precondition |
| `wifi-host-replica` | vendor host post-release behaviour (the definitive host sequence) |
| `wifi-driver-re`, `wifi-firmware-re`, `wifi-transport-port` | host-driver/firmware interfaces and the transport contract |
| `wifi-irq-unmask` | PD/PD2/PD3/PD4 patch analysis, recipes and verifiers |
| `wifi-fw-postboot`, `wifi-fw-link-event`, `wifi-event-router`, `wifi-boot-gate`, `wifi-boot-kick`, `wifi-fw-ready-gate` | image post-boot internals and the IRQ/event gap |
| `wifi-vendor-userspace` | userspace surfaces, startup trace, HCC mapping, cfg80211 scope |
| `wifi-driver-clean`, `wifi-openwrt-pkg` | clean-room instrument driver and its OpenWrt package |
| `wifi-bench-tooling` | one-command sessions incl. protocol mode, guards, patch builder, bar0read helper |
| `wifi-proto-seq` | g1 id3/id0x12 decode, calibration (g0 id2 key 0x0101) family and response contract |
| `wifi-cfg80211` | cfg80211 M0-M2 wiphy/VAP implementation, registration gate, 39-op map |
| `wifi-datapath` | two-plane model, MAC slot-5 ring block, TX/RX formats, key tables A/B |
| `wifi-ete-wake` | ETE trigger chain, host-owned field comparison, E1a/E1b negative |
| `wifi-upstream-target` | target build verification, cfg80211 bench image, patch classification |
| fw-audit lane (task-115; deliverable in progress) | PD4 minimality/safety audit and vendor-clean alternative analysis |

Historical lanes (`wifi-framework-port`, `wifi-shim-port`, `wifi-source-hunt`, `wifi-wal-glue`,
`wifi-algcfg-slice`, `wifi-datapath-slice`, `wifi-thermal-re`, `wifi-driver-integration`) remain
in the tree as history; they are not load-bearing for this report's claims.

## T21. Bench image build and flash pipeline (2026-11-25 15:00)

Source: `wifi-upstream-target/BUILD-VERIFICATION.md` sec 5 + `CFG80211-BENCH-IMAGE.md` +
ledger 2026-11-25 13:30/15:00. The image is a host-build artifact that was then flashed in an
attended bench session; the target tree is the project's own skeleton, not an upstream submission.

| artifact / step | value | evidence |
| --- | --- | --- |
| kernel config delta | `CONFIG_CFG80211=m` plus the resolved WLAN/RFKILL/signed-regdb symbols; prompt-free `kernel_oldconfig` re-derivation | [V] |
| cfg80211.ko | 2,328,548 B sha256 `949393f9676cc7bab18def92f1f96a07c45bd0ff599ee418c16ba7d54715170c`, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8` | [V] |
| clean-room driver | `hi5622_wifi.ko` 505,076 B sha256 `d2b3cf7917df1a7439c5dad1badd83bf97d627ae54ca9f5db7e34216edc2edaa`, depends=cfg80211, 0 compiler/modpost warnings | [V] |
| sysupgrade container | 6,267,199 B sha256 `22b72852ff2415a6d951aadc18206f88e3402dd6661252df853f48dffde95c22`; manifest sha256 `335c79b0...` | [V] |
| kernel slice (`kernel.bin`) | 3,372,638 B sha256 `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` | [V] |
| full-slot hash | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` (verified over the written kernelb slot) | [V]/[V*] |
| flash route | box on GP1-line image (pair B) + stock on pair A; stock fetch of the slice -> `mtd erase/write` **kernelb** -> full-slot verify -> restore pristine stock files -> handover `bootflag=b` + `boot_reg=0x21` -> reboot | [V*] |
| boot evidence | `uname -r` 6.18.60; rootfsb; selector raw `0x11`; dmesg `endpoint reset pulse: data 0x0300000f -> 0x1330000f` at 2.6 s (GP1 013a in-probe pulse) | [V*] |
| safety state | stock pair A kernel+rootfs untouched and bootable; recovery door armed during the flash; modules loaded from /tmp (not in the rootfs) | [V*] |
| status | BENCH image / research instrument only: not a release, cfg80211 config not in the luofu kernel series, install path not run end to end | [V] |

## T22. cfg80211 interface - M0-M2 state and op map (2026-11-25)

Source: `wifi-cfg80211/REPORT.md` (host-side design + selftests) and ledger 2026-11-25 09:10/15:00
(bench result). One wiphy per PCI endpoint; the wiphy is registered only after the ready handshake.

| item | value / state | evidence |
| --- | --- | --- |
| M0 wiphy + netdev model | wiphy_alloc in probe (no device access), wiphy_register only after `g4 id1`+`g4 id2` decoded; one wiphy per endpoint | delivered host-side and **bench-validated** [V]/[V*] |
| M1 VAP lifecycle | `add/del/change_virtual_intf` netdev model; VAP created via nl80211, deleted again; no firmware VAP message sent | **bench-validated** (wlan0, ifindex 7, managed, doc address) [V*] |
| M2 scan/station stubs | `scan`, `get_station`, `dump_station` registered, return `-EOPNOTSUPP`; every M3+ op NULL | delivered [V] |
| bands / channels | 2.4 GHz ch 1-13 at 30 dBm; 5 GHz 19 LU-plan ch 36-48/52-64/100-140 at 23 dBm; 12 legacy rates + HT20 skeleton | [V]/[V*] (channel counts seen in dmesg/iw) |
| modes / ciphers | AP + STA + monitor; WEP40/WEP104/TKIP/CCMP/GCMP/AES-CMAC/BIP-CMAC-256; `max_scan_ssids = 12` | [V] |
| registration gate | pure function: `boot_latched && phase >= READY_TXQ_SYNC && id1 && id2`; workqueue; teardown under RTNL | [V] |
| 39-op map | 3 implemented (M1), 3 EOPNOTSUPP stubs (M2), 33 deferred (M3-M6); machine-readable in `hi5622_cfg80211_logic.c` | [V] |
| open H2D payloads | user/vap add `g0 id7/8` (0xA8), scan `g0 id0xC` (0x1FC), STA join family - payload structs [U]; del-user `g0 id9` (0x0C) exact builder exists | [V]/[U] |
| config-sync backend rule | g1 id3 is the generic channel: dispatch by `syn_id` (0x0101 alg, 0x0102 cali, 0x07xx per-VAP), never by size | [V] |
| tests | cfg80211 logic 454/454; inherited H2D 242/242; source selftest 144/145 (known provider-symvers redundancy); kernel module 0 warnings | [V] |
| limits | management model only: no firmware VAP object, no scan results, no association, no data path | [V*/I] |

## T23. Calibration ladder C1/C2 - carrier, cfg ids and response contract (2026-11-25 12:30)

Source: `wifi-proto-seq/REPORT.md` sec 4-7, `wifi-h2d-send/REPORT.md` sec 13, ledger
2026-11-25 11:00/12:30. All six messages ride the proven config-event carrier (same SR node,
mailbox id-3 announce, release-only boot); no new host register writes.

| item | value | evidence |
| --- | --- | --- |
| carrier | H2D `g0 id2`, key `0x0101`, field B = module/vap (default 4 = the type-1 created object) | [V]/[V*] |
| frame / node | 298 B frame = 12 B header + 286 B payload (token[8] + key u16 + len u16 + 270 B blob + 4 B pad); node word1 `0x012A6D2B` = declared total | [V] |
| blob | `u16 cfg_id | u8 type (0=SET) | u8 pkt_type | u32 dev_ret | u8 need_w4 (1) | u8 resv | value[260]`; value area at blob+0x0A | [V] |
| the six commands | 3502 set_2g_power (104 B), 3503 set_2g_low_power (104), 3504 set_5g_power (70), 3505 set_5g_low_power (70), 3506 set_xo_ppm (u32), 3507 set_rssi (40) - vendor order 1..6 | [V] |
| expected answer | one D2H `g1 id3`, total 298 B, `syn_id 0x0101`, `body_len 270`, request cfg id echoed, `dev_ret 0`, cookies echoed; no host reply | [V] |
| **C1 fired** | one `set_rssi_param` (3507) zero-value frame after ctx-create type 1 + txq_sync72 on a PD4 golden boot: consumed in 0 polls; answer catalogue count=2, `last_total=0x012A`, `last_len=298`; mask 0, staging 0, page golden; driver ok=3 timeout=0 | [V*] |
| C1 limits | answer body recycled before the read-only capture: length/count evidence, body expected-by-construction; not a decoded payload | [V*] |
| **C2 next** | fresh boot, `h2d_send_budget=8`: create + txq + six calibrations in vendor order; one new message class per boot; capture the answer body immediately after the fire | [I] |
| values policy | per-unit factory calibration stays local; only zero-value structural frames are described here | [V] |

## T24. Wi-Fi frame-plane ring map (MAC host-interface block, slot 5)

Source: `wifi-datapath/REPORT.md` sec 2-9 (static RE from the vendor host driver + firmware).
**No frame has been sent or received by our host yet.** Offsets below are device CA unless
marked CPU/BAR0.

| object | address / geometry | notes | evidence |
| --- | --- | --- | --- |
| MAC host-interface block | CA `0x40040000` (band 0) / `0x40060000` (band 1); slot 5, stride 0x2000 | distinct from the ETE block; no cross-writes between modules | [V] |
| MSDU-info ring pointer table | ready `g4 id2` payload `0x01060850` -> ACP alias `0x02060850` -> MAC `+0x10` | seeds the TX ring-pointer table (both bands) | [V]/[V*] |
| RX buffer lengths | `+0x1c`: norm `0x63c`, small `0xb0` | live vendor 0x00b0063c | [V*] |
| RX free rings | norm: 256 x 0x63c B (`+0x20`, size `+0x2c`); small: 512 x 0xb0 B (`+0x24`, size `+0x30`) | host posts buffers, device fills + writes 0x28 B descriptor | [V] |
| RX completion ring | 4095 entries (`+0x28`, size `+0x34`); host consumer `+0x40`, device writer `+0x4c` | NAPI/ISR drain | [V] |
| TX BA-info buffer | depth 256 (`+0x00`), device VA (`+0x04`) | 16-byte completion records | [V] |
| MAC interrupt | status `+0x44` (W1C), mask `+0x48` (live `0xfffffcc1`) | polling alternative to ETE completion | [V] |
| TXQ table | 0x2524 B host-DRAM non-cached; device VA synced by H2D `g0 id1` key `0x0320`, 4-byte payload | vendor's first post-ready message | [V] |
| TX ring entry | 8 B `{u32 buf_devva; u32 ctl}`; ctl len[11:0], path[14:12], flag[15], tid[19:16], vap[21:20] | per (user,tid) non-cached ring; 20 B DMA descriptor at skb->data-0x14 | [V] |
| packed ring index | 15-bit index + phase bit 15 (same convention as ETE rings) | publish with dmb/dsb + sev | [V] |
| RX descriptor | 0x28 B device-written at buffer start, then the 802.11 MPDU; parsed into 0x30 B msdu-info | bit semantics [I]/[U]; offset/width table in lane report sec 7.2 | [V] |
| key tables A / B | firmware CPU `0x10C23C` (217 slots) / `0x103BF8` (88 slots), 8 B `{u16 key; u16 pad; u32 worker}`, sentinel `{0x1451,0}` | g0 id-1 command namespace; keys 0x0004/0x0064/0x0078/0x00D8/0x0128/0x0311/0x0320 | [V] |
| control-message family | `g0 7/8` user-add 0xA8, `g0 9` del-user 0x0C, `g0 0xC` scan 0x1FC, `g0 0xF/0x10/0x11` STA join 0x40/0x10, `g0 0x12` EDCA 0x74, `g2` WAL cfg ids [U] | D2H: `g1 15/16/17`, `g2 2`, `g2 3-6` | [V]/[U] |
| direct-read constraint | ETE messages only; MAC DMA is independent of the ETE A->B copy | live hypothesis, ladder B2 tests it | [I] |
| bring-up order | D0 MAC register model -> D1 RX rings/NAPI -> D2 TX ring + TXQ sync -> D3 user/VAP -> D4 scan -> D5 STA join; read-only B2 probe first | an extra iATU viewport once rebooted the box | [I]/[V*] |

## T25. PD4 research patch - delta, cave proof and audit boundary

Source: `wifi-irq-unmask/REPORT-PD4.md` sec 2-5 (static/verification) + bench records; audit owned
by the fw-audit lane (shared task `task-115`; deliverable in progress at this revision). The
patched image exists only outside the repository.

| item | value | evidence |
| --- | --- | --- |
| PD4-ENTRY | file `0x0916A`, 4 B: `00 20 21 B0` -> `BF F0 69 B8` (`b.w 0x108240`), boot-task success epilogue | [V] |
| PD4-LOOP | file `0x0C8240`, 64 B (zero in the vendor image) -> guarded `out[0]` poll stub; calls wrapper `0x40294` only when mailbox work is pending; consume-only substitution while the handler table is the static sentinel | [V] |
| patched image | sha256 `cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1`, length unchanged 928,920 B; only the two segment windows differ from the pinned blob | [V] |
| cave | CPU `0x108197..0x108413` (637 B all-zero static-data gap between two referenced read-only tables `0x108054`/`0x108414`); no branch target, no literal/address materialisation, outside both boot memsets (`0x117C00..0x1ADDE0`, `0x1000000..0x10E0000`) and outside the exception-staging area `0x4000C..0x4004B` | [V] |
| why patching is needed | event 0x4C (H2D mailbox arrival) is delivered only through the image vector/dispatcher path; the event-controller window CA `0x40160000` is not host-visible; the image never polls the mailbox; the vendor host sends nothing between release and ready | [V]/[V*] |
| rejected alternatives | PD3's header-record area (its own exception staging overwrites the stub); PD2's config-3 block (live on our boot); no vendor-clean mailbox-delivery path demonstrated | [V*]/[I] |
| bench record | PD4 fires 2026-11-25 02:10 (cave bytes present before/after, fault reproduces) and the ordered lap 05:10/07:40 (announces consumed in 0 polls, mask 0, golden page) | [V*] |
| audit status | the fw-audit lane (task-115) formalizes minimality/safety and the vendor-clean alternative analysis; at this revision the settled verdict is **research instrument only, never a shipping change** | [V]/[I] |

