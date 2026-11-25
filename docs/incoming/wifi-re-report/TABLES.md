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

Hardware status of the order (2026-11-26): step 2 and step 3 are proven; step 4 now posts the
real `user_add`/`user_add_notify` frames and the firmware commits the mailbox-layer VAP; step 6
posts the real scan request and the firmware consumes it, but the D2H scan response does not flow
yet because the per-radio VAP start step between 4 and 6 is still [U] (corrections C31/C33, REPORT
6.7). Steps 5 and 7 remain open.

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
| PD3B | no cave; event-0x4C raise loop in the boot epilogue (raise CA corrected to `0x40161188` by C27) | inline | ef0683db... | not fired on the bench at this report's cut-off [U] |
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
| `wifi-bench-tooling` | one-command sessions incl. protocol mode, phase matrix (T28), guards, blocked-outcome classification, bounded reboot, patch builder, bar0read helper |
| `wifi-proto-seq` | g1 id3/id0x12 decode, calibration (g0 id2 key 0x0101) family and response contract |
| `wifi-cfg80211` | cfg80211 M0-M2 wiphy/VAP implementation, registration gate, 39-op map |
| `wifi-datapath` | two-plane model, MAC slot-5 ring block, TX/RX formats, key tables A/B |
| `wifi-ete-wake` | ETE trigger chain, host-owned field comparison, E1a/E1b negative |
| `wifi-upstream-target` | target build verification, cfg80211 bench image, patch classification |
| `wifi-fw-audit` | PD4 minimality/safety audit, H1/H2 read-only pack, ranked variants, vendor-clean alternative analysis (task-115) |
| `wifi-vap-scan` | T27: byte-exact g0 id7/id8/id9/id0xC builders, firmware-handler decode, V1-V4/S1-S2 ladder |
| `wifi-scan-ap` | T22/T27: cfg80211 scan/AP wiring, provider caps 0x1f, bounded announce wait/frame gap, provider module |
| `wifi-datapath-bringup` | T26: host data-plane implementation (dp_*), MAC slot-5 program, TXQ address sync, E1 ladder |
| `wifi-vap-start` | T27: the VAP start family (mode-0 WAL vs mode-1 g0 id1 keys), provider extension, VAP-start ladder |
| `wifi-scan-d2h` | T27: g1 id15 beacon layout (802.11 body + 20 B trailer), g1 id17 channel records, firmware VAP-0 handler analysis |
| `wifi-scan-complete` | T27: scan auto window, channel-coverage completion, full beacon IE decode into cfg80211 BSS |
| `wifi-dp-txrx` | T31: 12-byte device-RAM MSDU table entry, TX publish/completion read-back, RX 0x28 B descriptor parser |
| `wifi-dp-air` | T31: the merged data-plane provider module and the F-AIR air-TX/RX ladder |
| `wifi-ap-start` | T29/T30: the four-message AP start, both carriers, the 156 B body, the hostapd capability gates v2-v7 |
| `wifi-hostapd-deploy` | T30: hostapd-mini package set, T0 deploy path, AP-start ladder readback pack |
| `wifi-bench-session` | T28/T31: the next-bench-session package, gates and the AP/air preconditions |
| `wifi-image-deploy`, `wifi-image-integration`, `wifi-abi-image` | T21: T0 zero-flash deploy, bench-image build, ABI-correct reproduction and the flash runbook |

Historical lanes (`wifi-framework-port`, `wifi-shim-port`, `wifi-source-hunt`, `wifi-wal-glue`,
`wifi-algcfg-slice`, `wifi-datapath-slice`, `wifi-thermal-re`, `wifi-driver-integration`) remain
in the tree as history; they are not load-bearing for this report's claims.

## T21. Bench image build and flash pipeline (2026-11-25 15:00; v4 ABI image + flash addendum)

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
| ABI reproduction (2026-11-26 18:00) | a serialized world build from tree `27663a0` reproduces the recorded image **byte-for-byte**: container `df6a78c3...` 6,533,439 B, kernel slice `13e81214...`, slot `1794a531...`, rootfs UBI `62344e80...`, seven `.apk` hashes unchanged | [V] |
| ABI-correct rootfs | the image rootfs apk db carries `P:kernel V:6.18.60~6f995278...-r1` = the tree `.vermagic` = the kmod `kernel=` pin; the running box reported `5de0e425` only because the 2026-11-25 install wrote the kernel slice without the rootfs; fix = kernel+rootfs from one build, never force apk | [V] |
| T0 userspace (2026-11-26 12:00) | zero-flash `apk add` of the five non-kmod packages into the running overlay: **49/49 gates, 0 failed**; native `iw` 6.17, `iwinfo`, `libiwinfo`, `wireless-regdb` persistent; receipt `/root/.wifi-t0-deploy.receipt`; rollback = `apk del` | [V*] |
| hostapd package set (2026-11-26) | hostapd-mini 283,790 B + hostapd-common 1,922 B + ucode-mod-nl80211 24,228 B + ucode-mod-rtnl 29,292 B; no `kernel=` dependency; 88/88 host checks + 14/14 selftests; deployed through the same T0 path (not baked into the container; a later rebuild should add it) | [V] |
| flash (2026-11-27 06:30) | **EXECUTED** with the stock-assisted installer (`stock-telnet-install.sh --inspect/--apply`): upload sha256 + slot read-back `1794a531...` matched, `ubiformat` OK, active pair untouched; the tool's env step errored but the environment already read `bootflag=b`, and the handover script (`fw_setenv bootflag b` + pristine `wifi_init` + `boot_reg=0x21`) completed; stock pair A remains the rollback | [V*] |
| post-flash boot + ABI receipt | 6.18.60 from `ubi.mtd=rootfsb`, selector `0x11`; `apk query --fields version kernel` = `6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1` = the kmod pin (kmod-ABI mismatch **closed**); rootfs-native `modprobe cfg80211`/`hi5622_wifi` rc=0 from `/lib/modules/6.18.60` (no `/tmp`); native `iw` 6.17 + `iwinfo`; T0 full-set verify **38 gates PASS** (one cosmetic receipt-missing - the flash wiped the overlay) | [V*] |
| AP on the flashed image | hostapd + ucode + PD4 firmware + the v7 ap-start driver; C2 clean -> `type __ap` -> `hostapd -B` -> `AP-ENABLED`, rc=0, four hostapd processes, `type AP, ssid cudy-ap-test`; all four AP-start frames consumed 0 polls; page `0x86807FEF` golden, mask 0 (T29) | [V*] |

## T22. cfg80211 interface - M0-M2 state and op map (2026-11-25)

Source: `wifi-cfg80211/REPORT.md` (host-side design + selftests) and ledger 2026-11-25 09:10/15:00
(bench result). One wiphy per PCI endpoint; the wiphy is registered only after the ready handshake.

| item | value / state | evidence |
| --- | --- | --- |
| M0 wiphy + netdev model | wiphy_alloc in probe (no device access), wiphy_register only after `g4 id1`+`g4 id2` decoded; one wiphy per endpoint | delivered host-side and **bench-validated** [V]/[V*] |
| M1 VAP lifecycle | `add/del/change_virtual_intf` netdev model; VAP created via nl80211; real `user_add`/`user_add_notify` frames now create the mailbox-layer firmware VAP; the per-radio VAP **start** object is still absent | **bench-validated** (wlan0, ifindex 7, managed, doc address; `user_id=0 vap_id=0` committed) [V*] |
| M2 scan/station | `scan` is wired to the provider: with `allow_frames>0` it builds and posts the real g0 id0xC 520 B frame, applies the mode-1 VAP base when configured (`vap_start_first`/`vap_start_mode`), waits a bounded window (5 s default; auto `3000+900*n` ms with `scan_timeout_ms`) and decodes g1 id17/g1 id15 into cfg80211; default fail-closed `-EOPNOTSUPP`; `get_station`/`dump_station` stubs; every M3+ op NULL. **D2 row = PASS** in the final v4 matrix (real results, not flags) | delivered + hardware-validated [V]/[V*] |
| M3 AP start (v4) | `.start_ap` posts the four-message AP sequence through the provider; requires the mode-1 VAP base first (posted once per load by v7). hostapd reaches `.start_ap`, the firmware consumes the frames and hostapd reports `AP-ENABLED`; `.stop_ap`/`.change_beacon` are host-side stubs, management TX/RX and `add_key` are fail-closed | delivered + hardware-validated [V]/[V*] |
| AP capability advertisements (v4) | fail-closed `.connect`/`.disconnect`/`.mgmt_tx`; `wiphy->mgmt_stypes` AP rx=tx=`0x3c15`; `WIPHY_FLAG_REPORTS_OBSS`; AP iftype change stores `wdev->iftype` and accepts `use_4addr=-1` | delivered [V] |
| bands / channels | 2.4 GHz ch 1-13 at 30 dBm; 5 GHz 19 LU-plan ch 36-48/52-64/100-140 at 23 dBm; 12 legacy rates + HT20 skeleton | [V]/[V*] (channel counts seen in dmesg/iw) |
| modes / ciphers | AP + STA + monitor; WEP40/WEP104/TKIP/CCMP/GCMP/AES-CMAC/BIP-CMAC-256; `max_scan_ssids = 12` | [V] |
| registration gate | pure function: `boot_latched && phase >= READY_TXQ_SYNC && id1 && id2`; workqueue; teardown under RTNL | [V] |
| 39-op map | 3 implemented (M1), 3 EOPNOTSUPP stubs (M2), 33 deferred (M3-M6); machine-readable in `hi5622_cfg80211_logic.c` | [V] |
| H2D payload builders | `user_add` g0 id7 (180 B wire), `user_add_notify` g0 id8 (180 B), `scan_req` g0 id0xC (520 B) byte-exact from the firmware handlers and **firmware-accepted** (2026-11-26); del-user g0 id9 (24 B) exact; STA join family still [U]; unpinned scan flags/channel-index [U] | [V]/[V*]/[U] |
| config-sync backend rule | g1 id3 is the generic channel: dispatch by `syn_id` (0x0101 alg, 0x0102 cali, 0x07xx per-VAP), never by size | [V] |
| tests | cfg80211 logic 454/454; inherited H2D 242/242; scan/AP/provider 248/248; source/contract 294/294 (the separate 144/145 provider-symvers item is a known redundancy); provider module 676,180 B sha256 `4a1c3b9a...`, 0 warnings | [V] |
| real-frame session (2026-11-26) | g0 id7 180 B + g0 id8 180 B + g0 id0xC 520 B consumed in 0 polls, no exception; scan aborted with 0 results (VAP-start wall) | [V*] |
| limits | control plane only: real scan results are bounded (no finish-ok 13/13 yet), no beaconing observed on the air, no association, no WPA2 keys, no MAC TX completion/RX capture; `AP-ENABLED` is a host-side fact | [V*/I]/[U] |

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
| **C2 fired** | 2026-11-25 16:30, `h2d_send_budget=8`: ctx + txq + six calibrations (3502-3507) in vendor order; every write consumed in 0 polls; **six 298 B g1 id3 answers** (catalogue count 7, `last_total=0x012A`, `last_len=298`); mask 0, staging 0, page golden, `ok=8 timeout=0 refused=0` | [V*] |
| **C2 automated** | 2026-11-25 23:40: phase C2 PASS - 28 steps, 54/54 checks, 0 failed, 7.02 s; per-phase evidence dir; this is the first fully automated bench-phase pass | [V*] |
| C2 limits | answer bodies counted, not decoded (buffers recycled before the read-only capture); values policy unchanged (zero-value structural frames only) | [V*]/[I] |
| values policy | per-unit factory calibration stays local; only zero-value structural frames are described here | [V] |

## T24. Wi-Fi frame-plane ring map (MAC host-interface block, slot 5)

Source: `wifi-datapath/REPORT.md` sec 2-9 (static RE from the vendor host driver + firmware) and
`wifi-datapath-bringup/REPORT.md` (host implementation, E1). **The host-side setup is implemented
and bench-fired (E1 PASS); no frame has traversed the rings yet.** Offsets below are device CA
unless marked CPU/BAR0.

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
| direct-read constraint | ETE messages only; MAC DMA is independent of the ETE A->B copy | E1 ran clean (no exception); no frame yet | [I]/[V*] |
| host implementation (dp-bringup) | `dp_probe` = one RO word of MAC band-0 `0x40040000`; `dp_setup` = one-shot: 8 MSDU-info table entries through the ACP alias with read-back + the 14-write MAC program with per-write read-back; `dp_rings`/`dp_status` = RO snapshots; `dp_txq_sync` = ONE g0 id1 key-`0x0320` frame with the TXQ-table device VA | module 462,952 B sha256 `8fc693ad...`, 0 warnings, 6.18.60 vermagic; not fired until E1 | [V] |
| **E1 bench result** | automated phase E1 **PASS, 37/37 checks, 0 failed**: `dp_probe` p0 clean, `dp_setup` write + p0-p3 gates, `dp_rings` p0-p3, then TXQ sync accepted (`txq_addr_sent=1 ok=2 timeout=0`); post pack mask 0, staging 0, page golden | ledger 2026-11-26 03:00; no exception | [V*] |
| merged data plane (v4) | wifi-dp-air merges the E1 data plane + dp-txrx into the mode-1 provider: module 886,864 B sha256 `ca32aec4...`, 1,483 device-free host checks, 0 warnings; the merged module's first TX published one frame to the MSDU ring (POST-ONLY) | ledger 2026-11-27 02:30; T31 | [V]/[V*] |
| what E1 does not show | no TX ring entry/RX buffer moved; RX completion ring + 0x28 B descriptor unconsumed [U]; rings assume user index 0 (no queueing user yet); TX 8 B entry + 20 B DMA descriptor untested; band 1 untouched | honest limits | [I]/[U] |
| bring-up order | D0 MAC register model -> D1 RX rings/NAPI -> D2 TX ring + TXQ sync -> D3 user/VAP -> D4 scan -> D5 STA join; read-only B2 probe first. Executed: B2a probe + dp_setup + TXQ sync (E1); next is a first frame | an extra iATU viewport once rebooted the box; never add one | [I]/[V*] |

## T25. PD4 research patch - identity, audit, H1/H2 and vendor-clean alternatives

Source: `wifi-fw-audit/REPORT.md` (task-115; harness `verify_pd4.py` 31 checks + 14/14 selftest,
`pd4_variants.py` 14/14), `wifi-irq-unmask/REPORT-PD4.md` sec 2-5, and the bench records. The
patched image exists only outside the repository (recipe-only publication).

| item | value | evidence |
| --- | --- | --- |
| PD4-ENTRY | file `0x0916A`, 4 B: `00 20 21 B0` -> `BF F0 69 B8` (`b.w 0x108240`), boot-task success epilogue; minimum possible redirect | [V] |
| PD4-LOOP | file `0x0C8240`, 64 B (zero in the vendor image) -> guarded `out[0]` poll stub; `sub sp,#4` restores AAPCS alignment after skipping the epilogue's `add sp`+pop; calls wrapper `0x40294` only when mailbox work is pending | [V] |
| patched image | sha256 `cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1` rebuilds byte-identically from the pinned blob; exactly 2 segments, 68 patched bytes, 59 differing byte positions, length unchanged 928,920 B, no other byte differs | [V] |
| cave | CPU `0x108197..0x108413` (637 B all-zero static-data gap between two referenced read-only tables `0x108054`/`0x108414`); exhaustive Thumb census: 0 branch/literal/ADR references; outside both boot memsets (`0x117C00..0x1ADDE0`, memset(`0x01000000,0,0xE0000`)) and outside the exception-staging frame `0x4000C..0x4004B` | [V] |
| what the stub changes | boot-task epilogue -> out[0] poll; calls firmware wrapper CPU `0x40294` -> dispatcher CPU `0xC18AC` (ack `out[5]=1`, clear out[0], doorbell `out[2]=8`, handler-table dispatch). Stub reads only CA `0x40039010`, writes no device CA; host never-write rule for CA `0x400392F0` unchanged | [V] |
| bench record | PD4 fires 2026-11-25 02:10 (cave bytes present before/after, fault reproduces) and the ordered lap 05:10/07:40 (announces consumed in 0 polls, mask 0, golden page) | [V*] |
| **H1 confirmed** (was the open question) | read-only pack (BAR0 `0x7C41B0` = CPU `0x10C1B0`, `0x7D0D68` = the live table, `0x2000` = ROM RAZ) on a PD4 golden boot at 2 s and 48 s after release: handler-table pointer CPU `0x10C1B0` = `0x00118D68` (image static value; file `0xCC1B0` contains it once), live table CPU `0x118D68` **all zero** at both reads, ROM RAZ window `0x2000` = 0, sanity anchor CPU `0x40000` = `0x00046971`. Interpretation: sentinel always equal -> PD4 consumes the mailbox through the ROM zero table, **handlers never execute**; the working duplex is non-mailbox ETE/polled H2D | ledger 2026-11-25 21:30; correction C29 | [V*] |
| H1 consequences | PD4-ALWAYS0 should behave identically (consume-only); PD4-NOSENT would enter the empty table and is not a diagnostic candidate; earlier lane claims that PD4 dispatches through the real handler table are refuted | ledger 2026-11-25 21:30 | [V*]/[I] |
| diagnostic variants | `PD4-NOSENT` force live branch, sha256 `0bc1b3e8c5b875095945ba37c700205237ba4049e53148d282e60f4ef5fc2820`; `PD4-ALWAYS0` force consume-only, sha256 `84e9cd9503fa8d2740d3382efe114639b29f439c0e611ec9504796a215590210`; `PD4-MIN` (36 B delta) / `PD4-LITE` (44 B) specified; one-shot epilogue call rejected | [V] |
| vendor-clean alternatives | NO host-only path proven. Vendor mailbox sequence already reproduced; deferred-send writes `out[5]=8` pre-kick (tried once in the takeover era: accepted, no frame accept); ETE 0x2D/0x2E do not deliver; ROM has no mailbox service; image has no other out[0] reader; CA `0x40160000` outside host-visible region | [V]/[V*] |
| one host-only candidate | spare inbound iATU viewport onto CA `0x40160000` + one-shot raise of event 0x4C: CA `0x40161188 = 0x1000`, mirror `0x40161288`; enable already set at CA `0x40161108` bit 12. New device-write class, untested, needs its own attended design + IRQ-storm bounds | [V]/[I]/[U] |
| verdict | patching required for the closed vendor blob **as delivered** (conditional only on the untested aperture experiment; not fundamental to the hardware); with H1 confirmed PD4 is a consume-only mailbox servicer, not a handler-dispatch path; PD4 = research instrument only, never a shipping change | [I high] |

Audit corrections folded into the report: PD3B raise CA `0x401611A0` -> `0x40161188` (C27) and
the "twin/ack word" = out[2] doorbell (C28), both in REPORT section 8.

## T26. MAC data-plane bring-up - host implementation, ladder and E1 (2026-11-26 03:00; v4 merge update 2026-11-27)

Source: `wifi-datapath-bringup/REPORT.md` (deliverable), `wifi-datapath/REPORT.md` (static RE),
`wifi-bench-tooling/PHASES.md` (E1 sequence) and the ledger 2026-11-26 03:00. The host-side data
plane is implemented and bench-fired; **no frame has traversed the rings yet**. The E1 phase is the
data-plane row of the phase matrix (T28).

| item | value / state | evidence |
| --- | --- | --- |
| module | `hi5622_wifi.ko` **462,952 B** sha256 `8fc693ad4c3c9338baa1089ecb2acd420be78eb059760bfc0fa356ade205cec9`, vermagic 6.18.60, 0 warnings, 13 `dp_*` T-symbols, inherited H2D 308/308 | v3 re-verified by kernel build + host tests | [V] |
| host tests | data-plane logic 69/69; inherited H2D 308/308; static decode 34/34; E1 sequence schema 10/10 | v3 re-run | [V] |
| `dp_probe` (RO) | ONE read-only word of MAC band-0 `0x40040000` (the B2a reachability probe); value recorded, not gated; no write | E1 p0 PASS | [V*] |
| `dp_setup` (WO "1") | allocate TXQ table 0x2524 B, 8 x 64 x 8 B TX MSDU-info rings, `tx_ba_info` 0x800 B depth 0x100, RX free rings 0x400/0x800/0x3ffc B, 256+512 coherent RX buffers; write 8 device-RAM MSDU-table entries through the ACP alias with read-back; run the 14-write MAC band-0 program in vendor order with per-write masked read-back | E1 write + p0-p3 PASS; one shot, failure closes the lane | [V*] |
| ready seed | `g4 id2` payload `0x01060850` = PKTRAM device CA of the MSDU-info ring-pointer table; MAC +0x10 receives ACP alias `0x02060850` | static + live vendor value | [V]/[V*] |
| MSDU-info table | 12-byte entries at `base + 12*(tid + user*8)`, `{ring_devVA, 0, 0}` at ring init | static decode | [V] |
| TX MSDU ring | depth 64 (0x40), 8-byte entries `{u32 buf_devva; u32 ctl}`; packed producer index (15-bit + phase bit 15) | static decode | [V] |
| RX free rings | 256 x 0x63c B and 512 x 0xb0 B; entries are 4-byte device VAs = `buffer + 4`; ring storage 0x400/0x800 B | static decode | [V] |
| completion ring | 4095 entries x 4 B (0x3ffc B); `+0x40`/`+0x4c` consumer/writer; format [U] | static decode | [V]/[U] |
| MAC program | 14 writes, vendor order, RMW where masked; live check `+0x38/+0x3c = 0x8000`, `+0x48 = 0xfffffcc1`; per-write read-back, first mismatch closes the lane | E1 dp_rings p0-p3 PASS | [V*] |
| TXQ-table address sync | `dp_txq_sync` posts ONE g0 id1 key-`0x0320` 32-byte frame carrying the 4-byte TXQ-table device VA; firmware worker CA `0x81445` stores it at fw RAM `+0x2524 + 0x252c*band` | accepted: `txq_addr_sent=1 ok=2 timeout=0` | [V]/[V*] |
| E1 result | automated phase E1 **PASS 37/37 checks, 0 failed**: probe, setup/read-back, rings, TXQ sync, post pack (mask 0, staging 0, page golden); library plan is 13 steps / 42 checks / 3 writes | ledger 2026-11-26 03:00; evidence dir under `build/evidence-bench-gates/` | [V*] |
| E1 does not show | no TX entry or RX buffer moved a frame; completion-ring/descriptor semantics unconsumed [U]; rings assume user index 0; first TX encoder + 20 B DMA descriptor untested on hardware; band 1 untouched | honest limits | [U]/[I] |
| v4 merge (wifi-dp-air) | the E1 data plane + wifi-dp-txrx are merged into the mode-1 provider: `hi5622_wifi.ko` 886,864 B sha256 `ca32aec4...` (revision 2), 1,483 device-free host checks, 0 compiler/modpost warnings; merge provenance enforced by the lane verifier; no new module parameter; the F-AIR sequence is 30 steps / 83 checks / 7 writes (sha256 `96354365...`) | `wifi-dp-air/BUILD-NOTES.md`, `wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json` | [V] |
| v4 first frame (POST-ONLY) | `dp_probe` READ -> probe_ok=1/0x20; `dp_setup` rings 256/512/4095 + TXQ table 0x2524 devva `0x835d4000` + 14 MAC regs verified (mask `0xfffffcc1`); key-0x0320 sync ok=3; `dp_tx_frame` published one 32 B probe-req at ring entry 0, table wptr 0->1, **completion_pending=1**; RX 0 captures | ledger 2026-11-27 02:30; T31 | [V*] |
| safety invariants | probe/setup never before armed+released; only MAC band 0 and one device-RAM table written; never CA `0x400392f0`, RC misc window, release or gate; no retry after failure; never add an iATU viewport | carried from the ladder | [V] |

## T27. The real firmware management frames (2026-11-26 04:30/06:30)

Source: `wifi-vap-scan/REPORT.md` + `recipes/frames.md` (builders, firmware-handler decode,
node word1s), `wifi-scan-ap/REPORT.md` + `BUILD-NOTES.md` (provider module), ledger 2026-11-26
04:30/06:30 (bench). All frames were built by the registered provider, validated against the
firmware H2D tables, gated (fail-closed) and posted through the working HCC carrier; each was
consumed by the firmware in 0 polls with no exception. Numbers are wire totals (12-byte header
included) unless stated.

| # | frame | direction / id | builder (firmware handler) | payload -> wire | SR node word1 | post (slot/devva/producer) | firmware result | evidence |
| ---: | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `user_add` | H2D g0 id7 | `hmac_user_add` (CPU `0x9832C`) | 168 B -> **180 B** | `0x00b46d2b` | SR0 slot 2, devva `0x8417c000`, producer 2->3, posted 1/8 | consumed 0 polls; **VAP committed** (`user_id=0 vap_id=0`) | [V]/[V*] |
| 2 | `user_add_notify` | H2D g0 id8 | `hmac_user_add_notify_alg` (uid, alg, 24 B VHT/44 B HT/58 B HE caps, nss) | 168 B -> **180 B** | `0x00b46d2b` | posted 2/8 | consumed 0 polls (after the bounded announce wait + 20 ms frame gap; the first attempt was refused inflight) | [V]/[V*] |
| 3 | `scan_req` | H2D g0 id0xC | `hmac_enc_scan_send_req` (entry CPU `0x6494C`) | 508 B -> **520 B** | `0x02086d2b` | SR0 slot 4, devva `0x8371f000`, producer 4->5, posted 3/8 | consumed 0 polls; **no result events** within 5000 ms -> scan aborted (results=0) | [V]/[V*] |
| 4 | `del_user` | H2D g0 id9 | `hmac_del_user_notify_dmac` | 12 B -> 24 B | `0x00186d2b` | - | byte-exact from the shipped del_user attr; not needed/fired in the recorded sessions | [V] |
| - | expected scan results | D2H g1 15/16/17, g2 2 | - | - | - | - | **none observed in the 06:30 session**; after the **mode-1 VAP base** (2026-11-26 14:00) the firmware answered 6 x 124 B `g1 id17` + 6 x 509 B `g1 id15`, later 12 BSS entries decoded (section 6.9) | [V*] |
| - | why no response (then) | - | firmware scan handler CA `0x6494C` reads **VAP id 0** and needs the per-radio VAP object **started**; g0 id7/id8 only register the mailbox-layer user/VAP | - | - | - | resolved by the **mode-1** carrier (`g0 id1` keys `0x0064`/`0x0065`), not the WAL group-2 carrier (mode-0 had no firmware effect); corrections C33/C34 | [V]/[V*] |
| - | un-pinned builder fields | five scan flag bytes, channel-index convention (idx = band-table position [I]), VAP id in the payload | - | - | - | - | accepted by the firmware anyway; fields stay [U]/[I] | [I]/[U] |

Provider/state around the session: module **676,180 B** sha256 `4a1c3b9a...` (caps `0x1f`,
9 cfg80211 imports, depends=cfg80211, 0 warnings; host 248/248 + inherited 454/454 + 242/242 +
static 294/294), `allow_frames` 0 by default (raised only in the attended session), `frame_gap_ms`
20; boot page `0x86807FEF/7/2/0xABCD`, state 1, mask 0, staging 0, heartbeats continuing [V]/[V*].
The owning lane later rebuilt the provider with the scan-D2H decoder integration (**704,720 B sha256
`20d38c5c...`**, `wifi-scan-ap/BUILD-NOTES.md`); the 676,180 B module is the historical fired
artifact recorded in the ledger (2026-11-26 05:30/06:30).

**v4 update (2026-11-26/27).** Two new frame families followed T27's real-frame session. The
**mode-1 VAP base** (vap_add `g0 id1` 48 B -> vap_start `g0 id1` 40 B; consumed 0 polls) unlocked
the first real scan results: 6 x 124 B `g1 id17` channel reports + 6 x 509 B `g1 id15` beacons
within 5 s, and 12 BSS entries decoded in a later 13-channel run (REPORT 6.9; T29 covers the AP
frames). The **AP start sequence** (vap_add 48 B -> vap_start 40 B -> ap_ssid `g0 id1` key
`0x0005` 64 B -> ap_cfg `g0 id1` key `0x0127` 184 B; consumed 0 polls each) drove hostapd to
`AP-ENABLED`; the map is T29, the capability gates T30.

## T28. One-command phase matrix (2026-11-26; final all-PASS run 17:00)

Source: `wifi-bench-tooling/PHASES.md` + `REPORT.md` sec 9.9, the ledger 2026-11-26 15:00/17:00,
and the evidence directories `build/wifi-bench-phase-matrix/20261124T094310Z/` (first D2 TRUE PASS)
and `build/wifi-bench-phase-matrix/20261124T094716Z/` (the one-command all-PASS run; gitignored:
PHASE-MATRIX.md + per-phase dirs + BRINGUP-STATUS.md). The v3-era run (05:30, evidence
`.../20261124T083424Z/`) had D2 BLOCKED-DRIVER; the mode-1 provider flipped it. The matrix is an
**attended operator tool**, not a release artifact; it never makes a PASS out of a documented
pending feature.

| phase | sequence (bundled) | plan steps/checks/writes | real result | key gates |
| --- | --- | ---: | --- | --- |
| boot | `bringup-boot-ready.json` | 5 / 18 / 0 | **PASS** (15/15 checks) | page `0x86807FEF/7/2/0xABCD`, state 1, mask 0, staging 0; ready id1+id2, id2 payload `0x01060850`, phase READY_TXQ_SYNC |
| C2 | `bringup-c2-calibration.json` | 28 / 55 / 8 | **PASS 54/54** | ctx_create type 1 + measured ctx4 body pack -> txq_sync72 clean -> six cfg_cal 3502-3507, each answered by a 298 B g1 id3 (catalogue delta + `last_total=0x12A`); `ok=8 timeout=0 refused=0`; page/state/mask golden |
| D1 | `bringup-d1-vap.json` | 11 / 24 / 0 | **PASS 21/21** | wiphy registered -> `iw phy phy0 info` -> `interface add wlan0 type managed` -> netdev + VAP counters -> `iw dev wlan0 del` -> removed |
| D2 | `bringup-d2-scan.json` | 22 / 48 / 2 | **PASS** (session result PASS; protocol verdict PASS) | iw rc=0 + dmesg `cfg80211: scan channel result` + results>0 + mask/staging 0 + golden page/state; the mode-1 provider load params are baked in (`allow_frames=12 vap_start_first=1 vap_start_mode=1 vap_start_module=0 frame_gap_ms=100 h2d_send_budget=8`); BLOCKED-DRIVER via signature gates only when results=0 |
| E1 | `bringup-e1-dataplane.json` | 13 / 42 / 3 | **PASS 37/37** | ready seed -> clean ctx_create -> RO `dp_probe` -> `dp_setup` + `dp_rings` (14 MAC writes + 8 device-RAM entries verified) -> ONE key-`0x0320` sync (`txq_addr_sent=1 ok=2 timeout=0`) -> mask/staging 0, page golden |
| all | - | - | **boot PASS, C2 PASS, D1 PASS, D2 PASS, E1 PASS** | one command, one box, bounded reboots between phases; evidence `20261124T094716Z` |

| harness property | behaviour (commit `0394f80` and predecessors) | evidence |
| --- | --- | --- |
| between-phase reboot | `--reboot-between` (all but last) / `--reboot-after`: immediate `/sbin/reboot -f` in the background, accept "back" only after unreachable/backwards-uptime/fresh-uptime plus a confirming re-read; observed back after 61.5 s, uptime 46 s, `seen_down=1` | ledger 2026-11-26 05:30 |
| bounds | every transport call clamped to the remaining budget; `--reboot-deadline` 300 s, `--reboot-poll` 4 s; child phases in killable process groups under a hard `--phase-timeout` (900 s default) - no orphan ssh/bench_session can survive the matrix | `wifi-bench-tooling/REPORT.md` sec 9.9 |
| classification | real runs classified from the session summary + protocol evidence, not the exit code: PASS / STOP / FAIL / ERROR / BLOCKED / NOT-RUN; documented pending-feature evidence maps to BLOCKED-DRIVER with a reason | `wifi-bench-tooling/PHASES.md` |
| capability probe | every attr/token a sequence touches must be a C string in the target module (>= 4 KiB real module), else the plan is refused before any bench time (`module-attr-missing` / `BLOCKED-TOKEN`) | `wifi-bench-tooling/REPORT.md` sec 9 |
| device-free tests | `tools/selftest.py` **563/563** (v4 re-run; was 503 in v3); dry-run of all five phases PLAN-OK with no device contact; the F-AIR sequence validation is part of the suite | v4 re-run |
| operator rules | `--yes-really --attended` only; host preflight (recovery door READY, UART capture, TFTP, single-session, ssh-host); no device writes outside the driver's one-shot attrs; UART stays read-only | project RUNBOOK/fire rules |

**Reading the matrix.** A BLOCKED-DRIVER row is an honest statement that a documented feature is
pending, not a failed run and never a pass; it stays the correct classification whenever the D2 scan
returns results=0. In the final v4 runs every row PASSed because the firmware actually produced
scan results (D2) and the data plane was set up and synced (E1) - not because a gate was relaxed.
The matrix is the operator-visible state table for the whole wave: boot (transport), C2
(calibration), D1 (management interface), D2 (scan with firmware results), E1 (data plane setup).

## T29. The AP start: message map and readback (2026-11-27 04:30; on the flashed image 06:30)

Source: `wifi-ap-start/REPORT.md`/`TABLES.md`/`HOSTAPD-BRINGUP.md` (static RE + v7 provider, task-121),
`wifi-hostapd-deploy/REPORT.md` (ladder, readback pack), ledger 2026-11-27 04:30 (first AP) and
2026-11-27 06:30 (AP on the flashed image). All frames were built by the registered provider, passed
the fail-closed gate and were posted through the working HCC carrier; each was consumed by the
firmware in 0 polls with no exception. Wire totals include the 12-byte HCC header.

| # | message | carrier / key | wire total | body | firmware worker / table A row | v7 fire post | firmware result |
| ---: | --- | --- | ---: | --- | --- | --- | --- |
| 1 | vap_add | H2D `g0 id1` key `0x0064` | **48 B** | 20 B | row 1 -> worker CA `0x449E4` (module/VAP context add; binds `module+0x4B0`/`+0x4B8`) | posted 3/8 | consumed 0 polls; creates the firmware VAP object at `ctx+0x190` |
| 2 | vap_start | H2D `g0 id1` key `0x0065` | **40 B** | 12 B (flag 1, cookie 0) | row 2 -> worker CA `0x46394` | posted 4/8 | consumed 0 polls |
| 3 | ap_ssid | H2D `g0 id1` key `0x0005` | **64 B** | 36 B `{u8 len, pad[2], ssid[33]}` | row 18 -> worker CA `0x82878` (copies body[3..] into VAP+0x24) | posted 5/8 | consumed 0 polls; ssid_len=12, freq 2437 MHz |
| 4 | ap_cfg | H2D `g0 id1` key `0x0127` | **184 B** | 156 B (T3 of `wifi-ap-start/TABLES.md`) | row 110 -> worker CA `0x84DA4` | posted 6/8 | consumed 0 polls; beacon_interval=100, DTIM=2 in the start_ap input |
| - | ap_auth | only on the WAL envelope: `g2 id0` cmd `0x006D`, 36 B total (4 B body) | - | u32 auth allowlist {0,1,4,8} | **no static key 0x006D in table A** | not sent on the g0 path | - |

**Hostapd side of the same fire:** `REPORTS_OBSS` set, `mgmt_stypes` AP rx=tx=`0x3c15`, start_ap reached
(`iftype=3 beacon_interval=100 dtim=2 head=63 tail=9`), then `wlan0: interface state
UNINITIALIZED->ENABLED`, `wlan0: AP-ENABLED`, `iw dev wlan0 info` -> `type AP, ssid cudy-ap-test`
(hostapd `-B` returns only after interface setup; rc=0 is the host-side half of the gate).

**Readback pack** (BAR0 = CA + `0x6B8000`; ctx0 BAR0 `0x7D1758` on the lead's boot, per the corrected
arithmetic in T30):

| read | address | v7 observed | meaning |
| --- | --- | --- | --- |
| golden page word0/+4/+8/+C | BAR0 `0x877000` | `0x86807FEF / 7 / 2 / 0xABCD` | firmware golden state preserved |
| state | BAR0 `0x7D0B60` | 1 | boot state |
| exception mask | CA `0x40008` | 0 | no exception in the AP fire |
| staging frame | BAR0 `0x6F800C` | 0 | no staged exception frame |
| `ctx0+0x18C` word | ctx0 BAR0 + `0x18C` | changed to `0x70094810` | AP-config worker effect (band/mode bits) |
| VAP pointer `ctx0+0x190` | ctx0 BAR0 + `0x190` | `0x01010F18` (nonzero) | the firmware VAP object exists |
| VAP `+0x10` / `+0x14` | VAP CA + `0x6B8000` + 0x10/0x14 | not read in this record | [I] dereference window; the PASS does not depend on it |
| `g+0xB4`/`g+0xB8` | BAR0 `0x829EDC`/`0x829EE0` | 0 | flush/doorbell clean |

**On the flashed image (2026-11-27 06:30).** The same AP result was reproduced after the ABI-correct
image was flashed: rootfs-native `modprobe cfg80211` + `modprobe hi5622_wifi` (rc=0 from
`/lib/modules/6.18.60`), the T0 full-set verify **38 gates PASS** (one cosmetic "receipt missing"
because the flash wiped the overlay), hostapd deployed with the ucode files, the v7 ap-start driver
and the PD4 firmware; C2 clean -> `type __ap` -> `hostapd -B` -> `AP-ENABLED`, rc=0, four hostapd
processes running, all four AP-start frames consumed 0 polls, page `0x86807FEF` golden, mask 0. That
closes the "bench image only" caveat of the 04:30 fire: the AP now runs on the flashed image - still
on the PD4-patched research firmware and still without an over-the-air/association proof (10.1).
Minutes later (2026-11-27 09:00) the AP was still live and the D2H catalogue had gained `g1 id0x23`
(35) count=6, 24 B (absent at boot) plus `g1 id3` count=20 x 466 B - likely AP/beacon-period activity
[I], still not an on-air proof and with the family meaning [U].

**Claim rules.** A consumed AP-start sequence plus a clean firmware readback is the control-plane
evidence; `AP-ENABLED` is a host-side fact. Beacons on the air, client association, WPA2 keys and
data-path frame movement are separate milestones (T31 and 10.2 Q1).

**v5 update (2026-11-28).** The order in this table is the v4 host-side sequence. The vendor's
actual AP start is **five** messages - channel config **0x0124 FIRST**, SSID 0x0005, auth 0x006D
(WAL only), AP config 0x0127, **VAP start 0x0065 AFTER ap_cfg** - and the host never posts a beacon
template (T34, C48). The five-message order was implemented and fired to `AP-ENABLED`; WPA2-AP on
the same AP installs its GTK (`key_add` 0x0078 96 B + `key_default` 0x0079 68 B, T33/C50).

## T30. The hostapd bring-up gates (2026-11-26 20:30 - 2026-11-27 04:30)

Source: `wifi-ap-start/HOSTAPD-BRINGUP.md` sec 1-12, `wifi-hostapd-deploy/REPORT.md` sec 2-5, ledger
2026-11-26 20:30..2026-11-27 04:30. Each row was a separate failed fire; each fix is a cfg80211
**advertisement** or an environment/host prerequisite, not a wire-byte change.

| # | observed failure (hostapd) | real gate (cfg80211 / kernel / host) | fix | first seen | status |
| ---: | --- | --- | --- | --- | --- |
| 1 | "Error loading ucode: Unable to resolve path for module 'common'" | hostapd-mini loads `/usr/share/hostap/*.uc` from the wifi-scripts package; the image lacked them | push `common.uc`/`wdev.uc` + /usr/share/hostap/wifi-detect.uc (bench); image fix `CONFIG_PACKAGE_wifi-scripts=y` | 2026-11-26 20:30 | workaround applied; image fix pending |
| 2 | "nl80211: Driver does not support authentication/association or connect commands" | hostapd's shared nl80211 init gate refuses a driver advertising neither AUTHENTICATE nor CONNECT; cfg80211 advertises CONNECT only with `.connect`/`.auth` | fail-closed `.connect` op (advertisement only) | 2026-11-26 20:30 | fixed (v2) |
| 3 | "nl80211: Could not configure driver mode" | hostapd's AP frame subscriptions (`NL80211_CMD_REGISTER_FRAME`) are rejected by `nl80211_register_mgmt()` when `.mgmt_tx` is absent | fail-closed `.mgmt_tx` op | 2026-11-26 23:40 | fixed (v4) |
| 4 | same line persists | the registration path then calls `cfg80211_mlme_register_mgmt()`, which needs `wiphy->mgmt_stypes` and the per-iftype rx bit | advertise AP rx/tx `0x3c15` + STA/monitor tables (dmesg: "mgmt_stypes advertised ... subscriptions accepted") | 2026-11-27 01:30 | fixed (v5) |
| 5 | "Failed to set beacon parameters" / "Could not connect to kernel driver" | initial beacon is `NEW_BEACON`; with userspace AP SME hostapd first registers `NL80211_CMD_REGISTER_BEACONS`, which cfg80211 rejects without `WIPHY_FLAG_REPORTS_OBSS` (hostapd returns -ENOBUFS before building NEW_BEACON) | set `WIPHY_FLAG_REPORTS_OBSS` (registration bookkeeping only) | 2026-11-27 01:30 | fixed (v6) |
| 6 | kernel WARN `net/wireless/core.c:532` in `wiphy_new_nm` | `WARN_ON(ops->connect && !ops->disconnect)` | paired fail-closed `.disconnect` stub | 2026-11-27 01:30 | fixed (v6) |
| 7 | set_mode/SET_INTERFACE path | hostapd always issues SET_INTERFACE(AP); a real type change hit M1 op bugs (no `wdev->iftype` store; `use_4addr=-1` refused) | create `type __ap`; v3 stores iftype + treats -1 as unspecified | 2026-11-26 21:30/23:00 | fixed (v3) + ladder rule |
| 8 | firmware exception on the second AP frame | AP workers deref `module_ctx+0x190` (VAP object) with no NULL check; the AP path never posted the mode-1 VAP base | post `g0 id1` `0x0064` 48 B -> `0x0065` 40 B once per load before the AP frames (fail-closed) | 2026-11-27 03:30 | fixed (v7) -> AP-ENABLED |
| 9 | vendor `wpad` unusable | it calls HiSilicon private ioctls (e.g. cmd 36580) and needs stock libs; monitor iface creation returns -95 | not a target - hostapd-mini is the fixture | 2026-11-26 20:30 | closed (negative) |
| 10 | hostapd version gate | `hostapd -v` exits 1 by design (upstream main.c) | match the `hostapd v2.12` text, not the exit code | host check | documented |

Host-side verification of the deploy/fixtures (no device): `deploy-hostapd.sh --check` **88/88**,
`tools/selftest.sh` **14/14**, `tools/check-ladder.py` **107/107**; the OPEN and WPA2 config fixtures
parse with a hostapd v2.12 build from the tree's own source and a deliberately broken config is
refused. Readback arithmetic correction: ctx0 CA `0x119758` -> BAR0 **`0x7D1758`** (the 2026-11-26
13:00 ledger line `0x7D2758` was off by 0x1000; ctx4 CA `0x11C458` -> `0x7D4458` cross-check), gated by
the lane's checker (correction C43).

**Limits.** These are advertisements, not implementations: management TX/RX, OBSS reporting, beacon
control and WPA2 `add_key` remain absent/fail-closed; `change_beacon` accepts SET_BEACON host-side
with no firmware write; hostapd/mini does not autostart from `apk add` (no `/etc/rc.d` link).

## T31. The data-plane air-TX ladder (F-AIR, 2026-11-27 02:30; v5: POST-ONLY superseded, see T37)

Source: `wifi-dp-air/LADDER-F-AIR.md` + `BUILD-NOTES.md` (task-126), the bundled sequence
`wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json` (30 steps / 83 checks / 7 writes,
sha256 `96354365867beaedeb77c001fed31a2afdb0d4d2edb949cd898fab2fcc8342f6`), the ledger 2026-11-27
00:30/02:30, and the module build: `hi5622_wifi.ko` **886,864 B** sha256 `ca32aec4...` (revision 2;
base provider 748,888 B `b42ff4ec...` + the E1 data plane + the wifi-dp-txrx logic; 1,483 device-free
host checks; 0 compiler/modpost warnings; vermagic 6.18.60). **The ladder's 02:30 fire is the first
data-plane TX the project has made.**

| stage | action | access mode | expected / gate | fire result |
| --- | --- | --- | --- | --- |
| F0 | boot ladder + `h2d_ctx_create` + `h2d_txq_sync72` | WO | golden page/state, mask 0; `h2d_status ctx_created=1 frames_sent=2 ok=2 timeout=0` | PASS (C2 clean) |
| F1 | interface add + link up + ONE scan (mode-1 VAP + channel precondition) | shell | `cfg80211_status vap0`; dmesg scan channel result; results>0 (results=0 -> **BLOCKED-DRIVER**, the scripted path still exercises F2-F5 as POST-ONLY evidence) | mode-1 provider, scan ran |
| F2a | `dp_probe` | **READ** (`cat`; rev2 also accepts `echo 1 >` as the same read) | `probe_ok=1 probe_value=0x00000020`; a write on rev1 returns rc=1 and leaves probe_ok=0 - the 00:30 failure mode (C40) | PASS |
| F2b | `dp_setup` | WO `echo 1 >` | `setup_done=1 failed=0 mac_regs=14 table_entries=8`; `dp_rings +0x38/+0x3c=0x8000 +0x48=0xfffffcc1`, 8 table entries; read-back mismatch closes the lane | PASS |
| F3 | `h2d_txq_addr_sync` (= `dp_txq_sync`) | WO | `txq_addr_sent=1 ok=3 timeout=0 frames_sent=3`; one `g0 id1` key-`0x0320` 32 B frame with the 4-byte TXQ-table devVA | PASS (`0x835d4000`) |
| F4 | `dp_tx_frame` (one 32 B broadcast probe request; optional `2` = 24 B null-data at slot 1) | WO | `dp_tx_status bound=1 frame_ready=1 posted=1 wptr=0x0001 completion_pending=0` = **COMPLETED (MAC)**; `completion_pending=1` = **POST-ONLY**; table read-back mismatch -> STOP | **POST-ONLY** (completion_pending=1) |
| F5 | `dp_rx_poll` + `cat dp_rx_capture` | WO one-shot / RO | `polled=1 captures=n`; 0 captures = **NO-RX** (recorded, not a failure; moves no ring pointer) | NO-RX (0 captures) |
| F6 | post packs | RO | mask/staging 0, state 1, page golden, out0/out2 0, `h2d_status timeout=0 refused=0 txq_addr_sent=1` | clean |

**02:30 fire, exact first-frame numbers.** `DP tx_frame: type=1 fc=0x0040 mpdu=32 B buf_devva=0x84565004
ring[tid=0 idx=0]={buf=0x84565004 ctl=0x00108020} wptr 0x0000->0x0001 table@CA 0x01060850=
{ring=0x835d7000 wptr=0x00000001 rptr=0x00000000} completion_pending=1`; `dp_setup` wrote the 14-reg
MAC band-0 program (mask `0xfffffcc1`) and the MSDU table (8 entries; CA `0x01060850` / ACP
`0x02060850`); the TXQ table is 0x2524 B at devVA `0x835d4000`; `dp_probe` read `0x00000020`.
Post pack: page golden, state 1, mask 0, staging 0 [V*].

**Outcome classification (the ladder's vocabulary).**

| observation | verdict | meaning |
| --- | --- | --- |
| posted=1, completion_pending=0 | COMPLETED (MAC) | the MAC consumed the entry and wrote the completion; on-air still needs the external observer -> air-CANDIDATE |
| posted=1, completion_pending=1 for the session | **POST-ONLY** | host -> ring -> device table -> read-back proven; the MAC did not consume (VAP/channel/MAC-state wall) |
| scan results=0 in F1 | BLOCKED-DRIVER | no firmware VAP/no results; never a TX or air claim |
| 0 RX captures | NO-RX | recorded, not a failure |
| table read-back mismatch / mask or page drift | STOP/FAIL | lane closed for the boot; power-cycle, capture, no retry |

**Honesty limits.** A broadcast probe request has **no ACK**, and the clean-room host can only prove
the MAC-completion chain; a true on-air observation needs an external observer (second station/
monitor on the same channel). Without it the strongest verdict is "air-CANDIDATE", never
"air-verified". One boot = one ctx-create + one txq_sync72 + one scan + one dp_setup + one 0x0320 +
at most two TX posts + one RX poll; never retry a failed step; never reload/reset in the same
session.

## T32. MAC RX - the live bank, the completion path and the 36 B device header (2026-11-28)

Source: `wifi-rx-static/REPORT.md` (task-137), the fired ladder evidence (ledger 2026-11-28 21:00),
and the stock register dumps quoted in the lane. **The fired `dp_rx_arm` is the proof rung:** the
live bank matched the vendor stock exactly, so the MAC RX consumer is running. The RX patch is
fail-closed (`rx_enable=0` default).

| object | address / value | notes | evidence |
| --- | --- | --- | --- |
| live host-MAC bank (band 0) | **CA 0x40050000** | machw word 8; reachable via the existing region-3 translation (BAR0 0x3b8000 + 0x50000) | [V] |
| +0x00 | host-MAC interrupt status | read by `shuangta_get_host_mac_int_status`; RX branch on status bit 2 (0x04) | [V] |
| +0x08 / +0x0c | norm / small free-ring **consumer rptr** (live) | stock mirror **0x20** each (32 buffers prefetched) | [V]/[V*] |
| +0x10 | completion-ring **producer wptr** (live) | the completion side of the RX path | [V] |
| config block (band 0) | CA 0x40040000 | ring addresses/sizes, host-owned wptr/rptr +0x38/+0x3c/+0x40, W1C status +0x44, mask +0x48 = 0xfffffcc1 | [V] |
| config +0x4c/+0x50/+0x54 | **never read by the vendor; stay 0** | the old `dp_rx_poll` telemetry source - the wrong-block bug | [V] |
| completion entry | **devVA of buffer+4** | converted to host VA; skb pointer at hostva-4 | [V] |
| device header | **36 B at buffer+4** | payload len at header+0x1a (= buffer+0x1e; gate len+0x28 <= 0x640), next sub-MSDU at header+0x20, flags/status at +0x5/+0x7, MPDU at **buffer+0x28** | [V] |
| ring index format | 15 index bits + phase bit 15 | full/empty resolved by index+phase compare (as the ETE rings) | [V] |
| what the host owes | **consumer only** | advance free-ring rptrs/refill from completed queues bounded by the live rptr delta, publish the completion consumer; the 12-write ring program is the whole device enable | [V] |
| patch actions | `dp_rx_arm` (RO validation), `dp_rx_drain` (only pointer mover; bounded, validates every entry/header, no retry) | `rx_enable`/`rx_clear_status`/`rx_drain_max` params; writes only config +0x38/+0x3c/+0x40 (+0x44 behind `rx_clear_status=1`) | [V] |
| fired `dp_rx_arm` | `status=0x00000182 mask=0xfffffcc1 norm_rptr=0x0020 small_rptr=0x0020 cmp_wptr=0x0000` | **exact vendor stock mirror** -> MAC RX consumer proven | [V*] |
| fired `dp_rx_drain` | no new completion (`rx_pending=0`) | no pointer moved - no on-air traffic; page golden/mask 0 | [V*] |
| host/build verification | rx selftest **51/51**; 6.18.60 build 0 warnings; patch round-trips | local artifact 280,736 B sha256 `5b43d0a2...` | [V] |

## T33. WPA2 key install - cfg80211 ops, firmware commands and the GTK fire (2026-11-28)

Source: `wifi-wpa-static/REPORT.md` + `TABLES.md` (task-139), the fired GTK ladder (ledger
2026-11-28 23:00). The fired WPA2 hostapd AP proved the group-key path on hardware; the pairwise
PTK remains RX/association-gated. All key frames ride the bench-proven g0 id1 config-event carrier.

| cfg80211 op | g0 cmd | body | firmware worker (table A) | fired? |
| --- | ---: | ---: | --- | --- |
| add_key | **0x0078** | **0x44 B**: key_index, pairwise, MAC[6], key[32], seq[16], key_len@0x38, seq_len@0x3c, cipher@0x40 | CA **0x0A5825** | **YES** - 96 B frame, idx=1, pairwise=0, cipher `0xfac04`, key_len=16, consumed 0 polls |
| set_default_key | **0x0079** | **0x28 B**: key_index, unicast, multicast (+ mgmt class flag) | CA **0x0A5C9D** | **YES** - 68 B frame, idx=1, uni=0, multi=1, consumed 0 polls |
| del_key | **0x007A** | 8 B | CA **0x0A4271** | not needed in the GTK rung |
| get_key | query event | reply layout [U] | [U] | not implemented (not needed for PSK) |
| set_default_mgmt_key | reuses 0x0079 with a class flag | vendor builder leaves a cipher byte the fw mgmt path wants | [V] | deliberately unregistered |

| item | value | evidence |
| --- | --- | --- |
| cipher mapping | firmware uses the **low byte** of the cfg80211 suite; classes data 0x0714 / mgmt 0x3840 / WEP 0x0022; indices data <=3, mgmt 4..5 | [V] |
| where the key lands | per-user key cache: cipher/index at user+0x310/0x311 (data) or +0x312/+0x313 (mgmt); key-table manager at module_ctx+0x4b8; **no host MAC-register step** | [V] |
| config requirement | cfg80211 6.18 requires `.add_key` + `.del_key` + `.set_default_key` together at wiphy_new or WARNs | [V] |
| fail-closed policy | `wpa_keys=0` compiled default; key bytes never logged; unknown ciphers -EOPNOTSUPP; ranges -EINVAL | [V] |
| verification | key selftest PASS; chain identity `269,688`/`b5dfb883` (CHAIN-ID-OK); patched module 0 warnings (282,048 B `11cee0aa...`) | [V] |
| limits | GTK only; PTK/4-way needs a client + RX; mgmt-key path unregistered; no client test | [U] |

## T34. The five-message vendor AP-start order (2026-11-28)

Source: `wifi-beacon-static/REPORT.md` + `TABLES.md` (task-140), the fired order (ledger 2026-11-28
22:00). The v4 four-message order is superseded (C48): the channel config comes **first** and the
VAP start **after** the AP config. Auth (0x006D) has no static g0 key and is WAL-only.

| # | message | carrier / key | wire | body | firmware worker | fired order |
| ---: | --- | --- | ---: | --- | --- | --- |
| 1 | channel config | WAL cmd **0x0124** (g0 key 0x0124) | 32 B g0 | 4 B {band, vendor per-VAP byte, channel, 0} | CA **0x841B8** -> module ctx +0x10..+0x13 | **FIRST** - `ap_chan posted (32 B, band=0 channel=6)` |
| 2 | set SSID | 0x0005 | 64 B g0 | 36 B | CA 0x82878 | posted 5/16 |
| 3 | auth algs | **0x006D** | 36 B **WAL only** | 4 B allowlist {0,1,4,8} | no static g0 key | omitted on the g0 carrier |
| 4 | AP config | 0x0127 | 184 B g0 | 156 B (T3 of wifi-ap-start) | CA 0x84DA4 | posted 6/16 |
| 5 | VAP start | 0x0065 | 40 B g0 | 12 B {flag 1, cookie} | CA 0x46394 | **AFTER ap_cfg** - `ap_vap_start posted after ap_cfg` |
| - | beacons | - | - | firmware SMAC (TBTT ISRs + suspend/resume; no host template/bcn ring) | - | on-air still needs an external observer |

| verification | result | evidence |
| --- | --- | --- |
| static probes | **33/33** on 2.5.24 and 2.4.15 + firmware | [V] |
| host selftest | **65/65**; 6.18.60 build 0 warnings | [V] |
| guard fix | `module_ctx+0x190` false-negative removed (async field; log-only for fresh post) | [V*] |
| fired result | plan `vap_add ap_chan ap_ssid ap_cfg ap_vap_start`; AP-ENABLED; page golden/mask 0 | [V*] |
| limits | on-air beacons unproven (external observer); AP-side mgmt TX (wal_cfg80211_mgmt_tx) not implemented | [U] |

## T35. The full-13-channel scan ceiling - g0 id10 decode and the clean negative (2026-11-28)

Source: `wifi-scan-full/REPORT.md` (task-138) + `wifi-scan-tuning` (task-133), ledger
2026-11-28 10:00/17:00. The g0 id10 per-slot array is a BA relay, and the scan engine consumes
**slot 0 only** - so the 13-channel question is closed as a structural negative, not a driver bug.

| item | value | evidence |
| --- | --- | --- |
| g0 id10 handler | CA 0x5FF64: payload[0]=3, payload[1]=1 (ADDBA rsp) / 2 (DELBA); object via 0xD6A1E; slot 0..7; contexts at obj+0xEC+slot*0x20 (8 x 0x20 B) | [V] |
| dispatch row | table CA **0x10CCFC**, 12 B rows `{u32 id; u32 fn; u32 aux}`; id-10 row **0x10CD5C**; 0x10CD08 is id 1 | [V] |
| vendor wire shape | **44 B** (12 B header + **32 B payload**), not the R4 36 B builder; subtype-2 flag at **payload+0x14** (not +0x08) | [V] |
| scan engine | object `[[module+0x4B8]+0x1A00]`; +0xFC start, +0xFD step, **+0xFE end/count**, +0x103 mode, +0x104/0x105 result counters, +0x106 stride (firmware default **6**) | [V] |
| why 13 impossible | engine reads **slot 0 only** (0xEC); **no subtype-1 field writes +0xFE** (only the subtype-2 clear path writes 0); extra slot configs cannot widen a request | [V]/[I] |
| H1 real effect | pacing alias: payload+0x10 presets the result counter (+0x104), payload+0x12 sets mode (+0x103); the subtype-2 companion clears +0xFE/+0x106/+0x05 | [V]/[I] |
| measured ceiling | **6 channels/request** without H1; **7** with H1 (8 with a larger counter preset but results=0); mode=1 dense/passive (4 ch, up to 14 BSS) | [V*] |
| batching fallback | one cfg80211 scan -> ceil(13/6)=3 requests (6+6+1) with per-batch coverage; achievable ~3.5-6 s | [V*] |
| BSS/iw path | decode proven (`entries=2 iw_visible=2`); len-prefix decode and cfg80211 inform work | [V*] |
| host verification | scan-full 90/90 + 65/65 static; 6.18.60 build 0 warnings (local 263,704 B `4dd008d8...`); inherited 37/37 + 196/196 | [V] |
| status | clean negative result; H1 stays off by default (`scan_band_cfg=0`) | [V]/[V*] |

## T36. STA connect, 5 GHz and HT/VHT - the decoded tables (2026-11-28)

Source: `wifi-sta-static/REPORT.md` + `TABLES.md` (task-141). **Static only at this cut-off** - the
patch is host-built and compile-verified but not fired; capability advertisement is not a working
client (association completion, RX/EAPOL and keys remain open).

| item | value | evidence |
| --- | --- | --- |
| STA connect | **WAL cfg-event** opcode **0x0123**, **108-byte body** (ssid_len, mfp, IE/SSID/BSSID/key host-buffer addresses, privacy, auth_type, key_len/key_idx, ie_len); same envelope as the AP-start WAL frames | [V] |
| STA disconnect | opcode **0x00d0**, **16-byte body** {bssid[6], reason u16, [15]=1}; the inherited `KICK_STA` allowlist row is cfg80211 disconnect (naming correction) | [V] |
| who auths/assocs | the **firmware** (vendor `.auth`/`.assoc` are NULL - connect-SME driver); completion is a D2H management event not yet decoded -> the patch never fakes `cfg80211_connect_done` | [V] |
| 5 GHz model | **one wiphy per band** on chip 0 (single host MAC, two RF chips; stock WIPHY=2); domain->band mapping of our endpoints [I] | [V]/[I] |
| band tables | 2.4 GHz **14** channels (ch1-14, max_power 30 template), 5 GHz **29** channels (36-64, 100-144, 149-165, 4.9 GHz 184-196) | [V] |
| HT | cap **0x0062** = SUP_WIDTH_20_40 | SGI_20 | SGI_40, both bands; static MCS rx_mask all zero (INI/runtime [U], not copied) | [V]/[U] |
| VHT | advertised both bands, **2 SS MCS0-9** (`0xfffa`), cap **0x00400000** (2.4 GHz) / **0x00400064** (5 GHz: HTC_VHT + 160 MHz + SGI80/160, matches live ch44 @160 MHz) | [V]/[V*] |
| patch / params | WAL allowlist {0x0123,108}; `sta_join` 0/1/2, `ht_mode` 0/1/2, `vht_cap`, `band_vendor`; host-built stripped 278,044 B `333c72a5...`, 0 warnings | [V] |
| ladder | B0-B6 designed; first controlled `sta_join=2` post carries the [U] host-buffer dereference risk | [V]/[U] |

## T37. The v5 fired-results summary - TX completion, E2 restart, soak and the static feature fires

Source: the ledger entries 2026-11-28 01:00/10:00/13:30/21:00/22:00/23:00/2026-11-29 00:00 and the
lane reports named per row. Every fire below was attended, on a fresh PD4 golden boot (page
`0x86807FEF`, state 1, mask 0) and ended with mask 0/staging 0 and no exception.

| fire (date) | module (stripped) | result | evidence |
| --- | --- | --- | --- |
| TX COMPLETED (2026-11-28 01:00) | dp-tuning `175a4617` / 251,924 B | TXQ-table queue-state publish after the ring push -> `COMPLETED (MAC)` device rptr == published wptr; `dp_txq_table` readback tie0 {count=1 bytes=32} | [V*] |
| H1 scan re-arm (2026-11-28 10:00) | scan-only R4 `713899a1` / 254,328 B | H1 band-config posted; 7 channels (ch1..ch7); `entries=2 iw_visible=2` BSS path proven | [V*] |
| final R4 combined validation + soak (2026-11-28 13:30/21:00) | R4 combined stripped **269,688 B sha256 `b5dfb883...`** (unstripped 990,760 B `64ece213...`) | AP-ENABLED -> E2 restart AP-ENABLED -> TX COMPLETED in one boot; 40-min soak 242/242 samples golden | [V*] |
| RX arm (2026-11-28 21:00) | rx-static `5b43d0a2` / 280,736 B | live bank CA 0x40050000 status 0x182, mask 0xfffffcc1, norm/small rptr 0x20 = exact vendor mirror; drain no completions (no traffic) | [V*] |
| five-message AP order (2026-11-28 22:00) | beacon-static fixed `300cd392` / 276,140 B | vendor order with `ap_chan` first and `ap_vap_start` after `ap_cfg`; AP-ENABLED | [V*] |
| WPA2-AP GTK (2026-11-28 23:00) | wpa-static `11cee0aa` / 282,048 B | key_add 96 B idx=1 cipher 0xfac04 + key_default 68 B multi=1 consumed 0 polls; AP-ENABLED | [V*] |
| static-wave artifacts (host-built, not fired) | scan-full 263,704 B `4dd008d8...`; sta-static 278,044 B `333c72a5...` | scan ceiling closed; STA/5GHz/HT static model + fail-closed patch | [V] |
| integration status | - | **DELIVERED + FIRED**: the final all-patch module (one module, 47 params, stripped 308,080 B sha256 `5b587949...`) was validated in one boot - see T38; the on-air proof still needs an external observer | [V]/[V*] |
## T38. The final all-patch module - one boot, all features (2026-11-29 01:00)

Source: `wifi-stability/FINAL-INTEGRATION.md` + `evidence/allpatch-*` (opensource `008aca5`), the
ledger 2026-11-29 01:00, and the module's own build script
`wifi-stability/tools/build-all-patch-module.sh` (re-stages the chain, applies the nine-patch stack
with six documented additive conflict resolutions, and refuses drift). **This is the project's
culminating device-level validation:** every static-wave feature in one module, fired on one fresh
PD4 golden boot.

| item | value | evidence |
| --- | --- | --- |
| chain | base `44c6cc12` -> image-polish `d145a205` -> dp-tuning `175a4617` -> scan R4 -> stability `b5dfb883` -> rx-static -> beacon-static (+guard fix) -> wpa-static -> sta-static | [V] |
| module identity | **stripped 308,080 B sha256 `5b587949...`** (full: `5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41`); 47 parameters; 0 compiler/modpost warnings; cfg80211 contract 10; strip leaves .text byte-identical | [V] |
| fire params | `fw_path=PD4`, `wifi_block_path`, `allow_frames=32`, `h2d_send_budget=10`, `vap_start_first/mode=1`, `hcc_trace=0`, `stop_ap_mode=0`, `rx_enable=1`, `beacon_static=1`, `beacon_vap_start=1`, `wpa_keys=1`, `sta_join=0`, `ht_mode=0`, `vht_cap=0`, `band_vendor=0`, `scan_band_cfg=0` | [V*] |

| # | feature | one-boot observation | evidence |
| ---: | --- | --- | --- |
| 1 | AP, vendor five-message order | `plan (5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start`; `ap_chan` 32 B band=0 channel=6 before `ap_cfg`; `ap_vap_start` after `ap_cfg`; **hostapd AP-ENABLED (rc=0) with the WPA2 config**, ssid `cudy-ap-test`, type AP | [V*] |
| 2 | WPA2-AP GTK | `key_add` g0 cmd 0x0078, 96 B, idx=1, pairwise=0, cipher 0xfac04, key_len=16 + `set_default_key` 0x0079, 68 B, multi=1; both consumed 0 polls, workers CA 0x0A5825/0x0A5C9D | [V*] |
| 3 | TX completion | `dp_tx_complete_poll verdict=COMPLETED` (TXQ publish enabled; table `{wptr=1, rptr=1}`) | [V*] |
| 4 | RX arm | live bank CA 0x40050000, status 0x00000182, mask 0xfffffcc1, norm/small rptr 0x0020 = vendor mirror; free-rptr movement proves the MAC consumed published free entries | [V*] |
| 5 | post pack | page `0x86807FEF` golden, state 1, mask 0, staging 0, no exceptions | [V*] |

**What this changes in the report.** The individual feature modules of T32-T37 are superseded as
the end state by this single artifact; the features' evidence rows stand, and the earlier
"integration tasked" statements are corrected by C56. **What it does not change:** the air-side
proof is still external (beacon/association/4-way PTK need a client or an observer), no RX frame
has been captured (no traffic), `sta_join=0` left the STA join unexercised, the 5 GHz band config was
not fired, and the final module still has to be baked into the self-contained image.

## T39. The final association module - chain, conflicts, artifact and one-boot validation (2026-11-25)

Source: `wifi-stability/ASSOCIATION-INTEGRATION.md` + `wifi-stability/evidence/assoc-identity.txt`,
`wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md` sec T1.5 and the ledger 2026-11-25 11:30-13:00. The
module .ko stays local (gitignored); only identity facts are committed.

| item | value | evidence |
| --- | --- | --- |
| chain | all-patch `5b587949` (base `44c6cc12` -> polish `d145a205` -> dp-tuning `175a4617` -> scan R4 -> stability `b5dfb883` -> rx -> beacon + `72dabe8` -> wpa -> sta) + wpa3-sae + ap-mgmt + sta-ops + data-tx | [V] |
| conflict resolutions | exactly two, both additive: Kbuild object union; cfg80211 teardown-hook union. `mgmt_stypes` owned by wpa3-sae (AP tx=0x3c2a) | [V] |
| artifact | **stripped 344,472 B sha256 `251d08f0...`** (full: `251d08f0c0a579f27cc3a3fb9f96df99c851a3315fa40f1fe2c7df267419d98b`); unstripped 1,323,608 B sha256 `1d470e61...` | [V] |
| corrected-carrier rebuild | **stripped 344,372 B sha256 `ab9e18a4...`** (full: `ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e`); unstripped 1,324,532 B sha256 `2d9c3f11...` | [V] |
| parameters | **56** module parameters; 0 compiler/modpost warnings; strip leaves .text byte-identical; vermagic 6.18.60 SMP mod_unload ARMv7 p2v8; depends=cfg80211 | [V] |
| cfg80211 contract | 12 provider-symvers lines; imports `cfg80211_bss_iter`, `cfg80211_mgmt_tx_status_ext`, `cfg80211_rx_mgmt_ext`; no undefined symbols | [V] |
| new params (fail-closed defaults) | `mgmt_tx=0`, `mgmt_rx=0`, `sta_ops=0`, `eapol_rx=0`, `sta_user_del=0`, `data_tx=0`, `data_tx_peer=1`, `data_tx_protect=0`, `data_tx_timeout_ms=200` + the 47 all-patch params | [V] |
| build script | `wifi-stability/tools/build-association-module.sh`: asserts the all-patch identity, applies each lane patch to its declared base, 3-way merge, two checked-in resolves, conflict-marker refusal, build, strip .text compare, 56-param check, unique capability bits, symvers/imports check, atomic publish | [V] |
| one-boot validation | AP five-message order + hostapd AP-ENABLED; TX `COMPLETED` (TXQ publish; device rptr == wptr); RX arm **status 0x182 / mask 0xfffffcc1 / rptr 0x0020** with the AP-first order; one `g0 id6` mgmt frame posted 86 B, consumed 0 polls; page `0x86807FEF` golden, mask 0, staging 0 | [V*] |
| fire recipe | fresh PD4 golden boot; one load with the association params above + the proven ladder params; AP 5-message order -> `dp_probe` -> `dp_setup` -> `h2d_txq_addr_sync` -> `dp_rx_arm` -> hostapd WPA2 AP -> client L5; one load per boot, no retry on exception | [V] |
| availability | the artifact is /tmp local evidence and may be cleaned; rebuilt by the build script + `wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch`; the image-2 lane stages `ab9e18a4` | [V] |

## T40. The corrected management-TX device carrier - HCC g0 id6 (2026-11-25)

Source: `wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md` sec T1/T1.5 (vendor-module disassembly of
2.5.24 + sibling cross-check), the host test `wifi-sibling-gpl/tools/mgmt_ctrl_selftest.c` (41/41)
and the ledger 2026-11-25 13:00. This supersedes the ap-mgmt WAL `0x01F2` carrier (C57).

| item | value | evidence |
| --- | --- | --- |
| vendor call chain | `wal_cfg80211_mgmt_tx` -> WLAN_CFGID_CFG80211_MGMT_TX (host-internal) -> `hmac_wpas_mgmt_tx` 0x4050c -> helper 0x3fe68: `hcc_msg_alloc(48 + frame_len)` + `hcc_msg_tx` | [V*] |
| 12-B HCC header | +0x00 group 0 (low nibble); +0x01 low nibble 0x1, high nibble vap/chip<<4; +0x02 `[vap+1]`; +0x03 `[vap]`; +0x04 u16 total len = payload+12; +0x06 u16 **id 6 (MGMT)**; +0x08 flags 0; +0x09 dst = hdr[1]>>4; +0x0A `0x5A5A` | [V*] |
| 48-B device TX control | +0x00 u16 frame_len-24; +0x02 u8 24; +0x06 u8 0x03 flags; +0x08 u8 (cookie & 0xf)<<4; +0x09 u16 station user/assoc id (0x0fff when none); +0x0b..+0x2f zero | [V*] |
| frame | inline at payload+48 = wire offset 60; vendor host bound frame_len > 25 and <= 588 B; our builder accepts up to 1612 B | [V*] |
| completion | device DMAC->HMAC config-syn event **0x01F3 (499)** -> `hmac_mgmt_tx_event_status` 0x427e4 (payload `{u32 frame_id; u8 status; ...}`) -> re-posts host-internal HCC **g2 id13** with `{frame_id +12, status +16}` | [V*] |
| what 0x01F2 is | the host-internal WAL->HMAC cfg-event (the ap-mgmt lane's decode); its absence from PD4 firmware table A is **expected**, not a blocker | [V]/[V*] |
| DMA requirement | none: the frame travels inline, so the VMSPLIT-3G host-DRAM-address problem disappears for management TX | [V] |
| corrected patch | `wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch` - 5 files, 366 diff lines, applies cleanly to the final association tree; build 0 warnings; stripped module 344,372 B `ab9e18a4...` | [V] |
| host test | `wifi-sibling-gpl/tools/mgmt_ctrl_selftest.c`: **41/41** (header fields, payload map, frame min/max refusals, known/unknown station user id, deprecated WAL builder record, g1 id13 RX decode) | [V] |
| fire | one `g0 id6` management frame **posted 86 B, consumed 0 polls**; page `0x86807FEF` golden, mask 0, staging 0, no exception | [V*] |
| old ladder | the ap-mgmt M2 rung ("`mgmt_tx=1` with the WAL TLV") is **withdrawn**; fire the corrected R-B rung instead | [V] |

## T41. Management RX - the D2H CRX event g1 id13 (2026-11-25)

Source: `wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md` sec T3 (vendor dispatch-table cross-check:
.data records at offsets 0x5e4/0x5e8) + `wifi-ap-mgmt/REPORT.md` (the original g1 id13 decode) +
`wifi-public-research-2/REPORT.md` sec 5 (sibling CRX/DRX split). Supersedes the "mgmt on the data
ring" assumption (C59) and the g2/g1 id13 conflation (C58).

| item | value | evidence |
| --- | --- | --- |
| event | D2H HCC group 1 / id **13** (0x0d) = management RX (CRX class) | [V*] |
| prefix | **60-byte prefix** = 12-byte HCC header + 48-byte RX-info block; 802.11 frame at prefix+48; frame length = total - 60 | [V] |
| RX info fields | +7 channel; +17 u16 frame_len; +39 s8 RSSI | [V] |
| delivery | `cfg80211_rx_mgmt_ext` (ap-mgmt lane), only when `mgmt_rx=1` (fail-closed default 0) | [V] |
| dispatch pairing | vendor .data pairs cfg-event id **0x0d** with `hmac_rx_process_mgmt_event` - the only AP/STA FSM RX dispatch; ring-entry processing never dispatches mgmt | [V*] |
| sibling cross-check | Hi3881 `FRW_EVENT_TYPE_WLAN_CRX` (mgmt/control/scan/peer) vs `FRW_EVENT_TYPE_WLAN_DRX` (data); 48-byte class matches our RX-info block | [V]/[I] |
| what it is not | device mgmt-TX completion = DMAC **0x01F3**; host-internal WAL relay = **g2 id13**. Three distinct objects | [V] |
| why the ring is empty in a scan | management frames never ride the data ring; the data ring carries EAPOL/ARP/DHCP/ping data after association | [V]/[V*] |
| open | no live `g1 id13` frame decoded yet (client RF-gated); RX-info tail beyond channel/len/RSSI [U] | [V]/[U] |

## T42. Sibling GPL + public research - sources, transfers, negatives (2026-11-25)

Source: `wifi-sibling-gpl/REPORT.md` + `wifi-sibling-gpl/TABLES.md` (Hi3881V100 OpenHarmony
`7c1e2d5b...`, GPLv2; Hi1105 `297b33b0...`, no open licence; local vendor-module cross-checks
[V*]); `wifi-public-research-2/REPORT.md` + `SOURCES.tsv` + `VERIFY-fetch.txt` (51/51
byte+sha fetch matches). Study-only: no sibling source text is committed (licence + clean-room).

| item | value | evidence |
| --- | --- | --- |
| CRX/DRX split | management/control (incl. scan results, peer reports) is one event family; data is another; mgmt never on the data ring | [V] |
| RX enable | publishing the free-buffer producer **is** the RX enable; no reset-mask write, no enable bit, no H2D RX message (both siblings + our vendor init) | [V]/[V*] |
| ring format cross-check | packed pointer = 15 index bits + phase bit 15; Hi1105 large depths: small free ring 512, completion ring 4095 (ours 512 / 4095); entries 8 B there vs 4 B here; normal ring 4095 vs 256 | [V] |
| lifecycle | fill free ring -> publish producer -> consume completions -> refill -> republish; a **refill watermark** is the sustained-traffic rule (iwlwifi RX_STALLED is the failure mode) | [V] |
| negative 1 | Hi3881 per-VAP `SET_RX_FILTER` (AP-UP 0xF7B9FAEA; AP+STA 0x73B9FACA; BIT4 broadcast mgmt) has **no luofu sender** - no symbol/string/immediate/VAP-state call site in 2.4.15 or 2.5.24; do not port | [V]/[V*] |
| negative 2 | no public exact-chip source: OpenHarmony has only hi3881v100; Cudy WR3000 2.0 GPL section present but **empty** (raw HTML verified; v1 has a real GPL row); no Jeton GPL; no mainline HiSilicon Wi-Fi driver; no public datasheet/FCC register doc | [V] |
| scan cap | the sibling host sends the entire cfg80211 channel list in one `mac_scan_req`; no host-side cap - our 6->7/8 ceiling is DMAC/firmware-side | [V] |
| AP association order | `hmac_user_add` (CTX ADD_USER) -> assoc response -> user/asoc state sync; matches our `g0 id7` -> `0x07d2` order | [V]/[V*] |
| EAPOL | data on the normal RX data path in both worlds (classifier type 6 log only); rides the data-ring drain after association | [V] |
| hardening proposals | P1 completion-entry validity wait + clear-after-read; P3 stall classification (IDLE/PREFETCH/PROGRESS/SPURIOUS_WPTR); P2 association-ordering guard - host-tested (`rx_model_selftest.c` 94/94), not in the fired module | [V] |
| licensing | our opensource tree is MIT; Hi3881 is GPLv2 (facts/quotes only, no code); Hi1105 has no open licence (structure vocabulary only) | [V] |

## T43. RX arm ordering - AP-start first, then dp_setup, then rx_arm (2026-11-25)

Source: `wifi-sibling-gpl/REPORT.md` sec 6 H7 + the v6 bench record (ledger 2026-11-25 13:00);
supersedes the "any order" assumption (C61).

| sequence | live-bank status CA 0x40050000 +0x00 | other live reads | evidence |
| --- | ---: | --- | --- |
| `dp_setup` -> AP start -> `dp_rx_arm` | **0x02** | armed state cleared by the AP-start path | [V*] |
| AP start -> `dp_setup` -> `dp_rx_arm` | **0x182** | mask 0xfffffcc1, norm/small rptr 0x20 (vendor mirror) | [V*] |
| vendor reference | - | RX rings initialised **once** at device init (`shuangta_rx_host_init_dscr_queue`: address/size/buffer len + fill both free rings + publish producers), never re-initialised; the AP-start path does not touch RX | [V*] |
| bench rule | AP-start first -> `dp_setup` -> `rx_arm`; re-arm after any AP start/restart; the v5 arm fire and the association one-boot validation both follow it | [V*] |
| open | which write in the AP-start path clears the status is [U]; a one-message bisect over the five AP messages would pin it | [U] |

## T44. The R11 scan sweep - final numbers (2026-11-25)

Source: `wifi-scan-tuning/FINDINGS-R10C.md` (first points), `wifi-scan-full/TEST-LADDER-R11.md`
(ladder design/stop conditions) and the ledger 2026-11-25 12:00 (sweep values). Refined 44-byte
vendor-shaped g0 id10 band-config; one rung per `field10`; page/mask golden/0 on every rung.

| `field10` | channels swept | note | evidence |
| ---: | ---: | --- | --- |
| 25 | **8** | maximum observed (best rung) | [V*] |
| 29 | 7 | - | [V*] |
| 33 | 6 | - | [V*] |
| 41 | 6 | - | [V*] |
| 64 | 6 | - | [V*] |
| 13 (earlier R10c) | 7 | consistent with the counter-preset model | [V*] |

| item | value | evidence |
| --- | --- | --- |
| H1 effect | pacing: payload+0x10 presets result counter +0x104; payload+0x12 sets mode +0x103 - not a channel count | [V] |
| structural limit | scan engine consumes slot 0 only; no subtype-1 field writes the count byte +0xFE; ceiling is DMAC/firmware-side | [V] |
| 13 channels | only via host batching (6+6+1); no single-request path | [V]/[V*] |
| shipped default | `scan_band_cfg=0` (6-channel single-shot + batching); H1 opt-in for 7-8 | [V] |
| open | `field10` semantics beyond the measured shape [I]; 5 GHz channel ids [U] | [I]/[U] |

## T45. The v6 fired-results summary and the RF gate (2026-11-25)

Source: ledger 2026-11-25 11:30-13:00 (the corrected association-module fire and the RF/swarm
state), `wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md`, `wifi-stability/ASSOCIATION-INTEGRATION.md`.
Every fire row was attended on a fresh PD4 golden boot and ended page `0x86807FEF` golden, mask 0,
staging 0, no exception.

| fire / measurement (date) | module | result | evidence |
| --- | --- | --- | --- |
| corrected mgmt-TX carrier (2026-11-25 13:00) | `ab9e18a4` / 344,372 B / 56 params | one `g0 id6` management frame posted **86 B**, consumed **0 polls**; the device carrier is hardware-accepted | [V*] |
| association-module one-boot (2026-11-25 13:00) | `ab9e18a4` | AP-ENABLED (five-message order) + TX `COMPLETED` + RX armed **0x182** with the **AP-first order**; page golden/mask 0 | [V*] |
| R11 final sweep (2026-11-25 12:00) | scan-full/scan-tuning patches | **8 channels max** at `field10=25`; 29/33/41/64 -> 7/6/6/6; no 13-channel rung | [V*] |
| RF measurement (2026-11-25) | bench | **~44 dB path loss** with no antennas; AP TX **~-64 dBm at 30 cm**; client RX marginal; host MT7922 flaky | [V*/I] |
| sibling/public research (host-only, 2026-11-25) | - | architecture confirmed; luofu SET_RX_FILTER **negative**; scan cap DMAC-side; no public exact-chip source / Cudy 2.0 GPL empty; 51/51 fetch manifest | [V] |
| association L5 (client auth/assoc/4-way) | `ab9e18a4` ready | **NOT CAPTURED** - client association is RF-gated until the antennas are attached; the software chain (mgmt TX/RX, station ops, EAPOL/data TX) is in place | [V]/[U] |
| image bake #2 successor - RX watermark (2026-11-25) | final association module rebuilt with the RX refill watermark (`rx_refill_wm`, default 75, fail-closed at boot): staged **`d2c608e7...` 347,388 B / 57 params**, installed **328,788 B `b4c076bf...`** (.text identical); SAE hostapd `451dd49f...` unchanged; kernel slice UNCHANGED | **baked + host-verified, NOT flashed**: container **7,188,799 B sha256 `8ba1ad22...`**; kernel slice 3,372,638 B `13e81214...` / slot `1794a531...` (rootfs-only); rootfs UBI **3,801,088 B `31e7a39c...`**; squashfs 3,318,696 B `cb42bfed...`; 83 pkgs; **verify 84/84 PASS**; three bakes byte-identical. The earlier `0d9429ec`/`ab9e18a4` row is the pre-watermark image-2 history | [V] (`wifi-final-image-2/HASHES.txt`) |
| image bake #2 (pre-watermark history, 2026-11-25) | SAE hostapd `451dd49f...` + WPA2/WPA3 configs + the corrected module `ab9e18a4` (staged 344,372 B; installed 325,932 B sha256 `4f1b45c1...`, .text identical; 56 params); kernel slice UNCHANGED | **baked + host-verified, NOT flashed**: container **7,188,799 B sha256 `0d9429ec...`**; kernel slice **3,372,638 B `13e81214...` / slot `1794a531...`** (rootfs-only); rootfs UBI **3,801,088 B `44df3a63...`**; 83 packages; **verify 82/82 PASS**; three recorded bakes byte-identical | [V] (`wifi-final-image-2/HASHES.txt`) |
