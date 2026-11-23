# Device event controller / router: register map, first-event analysis and the delivery gap (event-router lane)

Host-only lane "event-router", shared task task-78, 2026-11-23. Inputs are local artifacts
only: the vendor firmware image at
`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`
(928,920 B, **local evidence only - never copied or committed**), the vendor modules
`hi5622v100_plat.ko` / `hi5622v100_wifi.ko`, and the committed prior lane reports (cited).
No device was contacted; no image was built; nothing was written outside this directory.

Evidence markers: **[V]** verified here from the image (or the module ELF) with the tools in
`tools/`; **[V\*]** device-measured by an earlier attended lane and cited; **[I]** inference
from code structure/dataflow; **[U]** unknown / not decidable without a device read or ROM dump.

Address conventions (project-wide, unchanged):
CPU address = file offset + 0x40000; host BAR0 for **all** firmware-internal addresses
(image, RAM objects, ROM window) = **CPU + 0x6B8000**; host BAR0 for the device IO block =
**0x3B8000 + (CA - 0x40000000)**. The device IO window is region 3 (CA 0x40000000-0x4011ffff),
so every event-controller register below is *physically writable from the host through BAR0*,
even though the vendor driver never uses that capability [V].

---

## 0. TL;DR - answers to (a) register map, (b) first event, (c) host action

**(a) The device event controller is a two-level hardware router, fully reversed (section 2).**
The CPU-side block at CA **0x40160000** contains: global enable **0x40160100** (=1 by
`0xC3024`), max priority level **0x40160104** (=0xFF), config **0x40160108** (=3), the
**current-vector status 0x4016010C** (low 10 bits = event id, read by dispatcher `0xC2EFC`),
and **ack/EOI 0x40160110** (dispatcher writes the full status word back). Per-event state lives
in banks indexed by event id 0x10..0x5F: **enable 0x40161100+4*(id>>5)** (W1S bit id&31, writer
`0xC6FF4`), **software pending-set 0x40161180+4*(id>>5)** and its mirror
**0x40161280+4*(id>>5)** (W1S, writer `0xC6DB8`), **priority 0x40161400+4*(id>>2)** (byte
`level<<4`, writer `0xC1A3C`), **target byte 0x40161800+4*(id>>2)** (1/2 from static table
CPU 0x105F8C+3*id+2, writer `0xC74B0`). A second, independent controller exists at
**0x4018010C/0x40180110** (dispatcher in the image tail at `0x120A0C`, handler table at CPU
0x107B8 below the image base) with no in-image caller [V]/[U]. The "pump" block is
**0x40036000+0x40*slot** with a global status word **0x40036800** (bit n = slot n pending) and
mask **0x40036900**, used by the radio event pump `0x46848` for handlers 0x3E/0x3F [V]. The ETE
interrupt word **0x40039508** is host-driver owned (vendor `pcie_ete_intr_init` RMW
`&=0xffe0f8f8`); the firmware's own ETE-control write is a *different* register
**0x4003A86C** (`&=0xE0E0F8F8`) [V]. The 0x40105000 block touched by entry A is SoC
clock/reset glue, **not** event routing [V].

**(b) Expected first events after a boot: first the firmware's own software raises, then the
timer.** `pcie_msg_init` (inside boot-task-dispatched function `0x49334`) **software-raises
event 0x4C and 0x4E at CPU 0x49754/0x4975A**, then registers/enables them at 0x49800/0x49816
[V]. These are latched pending during init and are the first events a working deliverer would
see (0x4C -> H2D mailbox dispatcher `0xC18AC`, 0x4E -> D2H flush `0xC6108`). The first
**hardware** source the image itself arms is the **timer at CA 0x40160600** (reload 0x61A800,
control 7, start 1), which raises **event 0x1D** (handler `0xC6C88`, the per-core timer tick)
- but only from the scheduler `0xC704C`, which is entered last by entry A/B [V]. Radio events
(0x32/0x38/0x3D/0x53/0x55 and pump 0x3E/0x3F) need PHY/radio activity; ETE completion
(0x2D/0x2E) needs host-armed ETE rings; mailbox 0x4C can also be triggered by the host's
mailbox doorbell write [V/V\*].

**Why none asserts/delivers in our boot:** the pending events **were** raised but the
**delivery path is never entered**. The image's only dispatch entry is the ROM-vectored ARM
trampoline at CPU 0x40064 -> `0xC2EFC` [V], and no image code enables IRQ in any reachable
context before the **scheduler**: the only `cpsie i` sites are inside the dispatcher itself,
the deferred-dispatch path, and the scheduler idle routine `0xC2B7A` reached from `0xC704C`
[V]. Entry A reaches the scheduler only after an **inter-core/ROM handshake that spins forever
until CPU 0x1BF008 == 2** (section 6.3): `{0,0xABCD}` -> 0x1BF008/0x1BF00C, wake via
CA 0x40161F00 = 0x20000, then poll [V]. If the partner never answers, the boot task is never
queued by entry A and the scheduler never runs, so the timer is never armed and no context ever
unmasks IRQ - the latched 0x4C/0x4E stay undelivered. The current status word 0x4016010C = 0 is
consistent with "no IRQ currently being taken" (it is the arbiter's current vector, acked by a
write to 0x40160110); it does **not** prove nothing was raised [I]. The pump status
0x40036800 = 0 is expected (no radio event source active) and is not the gate [V/I]. The ETE
word 0x40039508 = 0x3f3f1f1f is the pre-clear default (the vendor host driver would clear it to
0x3f201818) - a separate host-side ETE arming gap, not the device event controller [V/V\*].

**(c) Host action: only a diagnostic one exists, and it is decisive.** No vendor-driver
register write to the 0x4016/0x4018 controller exists (both modules contain **no** such CA
constants; only 0x40039508 appears, as an ioremap resource) [V]. The only host-generated device
event the firmware expects is the **H2D mailbox announce** (write pending bitmap to out[0]
CA 0x40039010 plus doorbell bit0 to out[2] CA 0x400392D4), which is measured DEAD [V\*]. Since
the controller's **software pending-set register is physically host-writable through BAR0**, the
single bench action is to replay the firmware's own raise for event 0x4C:
**write 0x00001000 to CA 0x40161188 and to CA 0x40161288 (BAR0 0x519188 / 0x519288)**, after a
read-only 24-word fingerprint, then poll. This replays the image's own raise/unmask primitive
`0xC6DB8(0x4C)` bit-for-bit (its exact hardware polarity is [U], but the write is the same one
the firmware makes). Expected observables in section 7. This cannot fix a
masked CPU IRQ; a null result with the pending bit still latched is the definitive
firmware/ROM-internal proof.

---

## 1. Method

* `tools/devreg.py` - device-register access census for the firmware image. It resolves every
  PC-relative literal load and movw/movt pair, tracks the constant through a bounded
  straight-line window, and reports every load/store whose effective address falls in a device
  CA range (plus targeted `access`/`defs`/caller queries). It also disassembles with resolved
  literals in Thumb and ARM.
* `tools/ko_devca.py` - the same census for the vendor kernel modules via pyelftools +
  capstone (mapping-symbol aware ARM/Thumb, symbol attribution, .rodata/.data word scan).
* `tools/evctrl_probe.py` - prints the read/write plan for the next bench action with exact
  BAR0 offsets and expected values. It never opens a device.
* Prior-lane tools reused verbatim for cross-checks: `wifi-fw-link-event/tools/fw_cfg.py`
  (`regs`, `dump`, `calls`, `findlit`) and `wifi-hcc-link/tools/ko_xref.py`.

Negative results are stated as such: e.g. "no writer of X" means a full-image sweep found no
resolved access to X (literal exact, literal+disp, or movw/movt construction), not merely a
missing grep hit.

---

## 2. The device event controller register map

All rows are **CA** (device IO window, host BAR0 = 0x3B8000 + CA - 0x40000000). "Writer" names
the image function/site that performs the write; "reader" the consumer. Values in the right
column are what a successfully-initialised firmware leaves behind.

### 2.1 CPU-side core block CA 0x40160100

| CA | BAR0 | role | writer (CPU) / value | reader | ev |
| --- | ---: | --- | --- | --- | --- |
| 0x40160100 | 0x518100 | **global event-controller enable** | `0xC3024` 0xC308E = 1 | hw | [V] |
| 0x40160104 | 0x518104 | level/priority threshold, low byte = 0xFF (255) | `0xC3024` 0xC3054 | `0xC1A3C` (register check: reject if `level<<4 >= byte`) | [V] |
| 0x40160108 | 0x518108 | controller config word = 3; exact meaning [U] (vector/source count?) | `0xC3024` 0xC305A | none found | [V write, U role] |
| 0x4016010C | 0x51810C | **current event vector / status**, low 10 bits = event id (0..0x5F) | hardware | `0xC2EFC` 0xC2F04 | [V] |
| 0x40160110 | 0x518110 | **ack / EOI**: dispatcher writes the *whole* status word back | `0xC2EFC` 0xC2F52 | hw | [V] |

Dispatcher body (`0xC2EFC`, Thumb) [V]: `r7 = [0x4016010C]; idx = r7 & 0x3FF;
handler = (idx <= 0x5F) ? [0x17D398+0x98+4*idx] : 0; ... cpsie i; if (handler) blx handler;
cpsid i; [0x40160110] = r7`. It also takes the system lock CA 0x17D3D8, does per-core
accounting via 0x4181C and tests the system-object flag [0x17D398+0x74]. The table is
**CA 0x17D430 = 0x17D398+0x98** (0x60 entries, Thumb pointers), populated by entry A,
`pcie_msg_init` and the PHY/radio init blocks.

### 2.2 Per-event banks (event id 0x10..0x5F)

| CA | role | writer | value for id | ev |
| --- | --- | --- | --- | --- |
| 0x40161100 + 4*(id>>5) | **IRQ enable**, W1S bit `id&31` | `0xC6FF4` (tail of `0xC6DA2`) | `1 << (id & 31)` (plain store, so W1S semantics) | [V] |
| 0x40161180 + 4*(id>>5) | **raise/unmask bank A** (code role = the image's event-raise primitive; hardware bit polarity [U], see note) | `0xC6DB8` | `1 << (id & 31)` | [V code, U polarity] |
| 0x40161280 + 4*(id>>5) | **raise/unmask bank B** (mirror; the same primitive writes both banks) | `0xC6DB8` | `1 << (id & 31)` | [V code, U polarity] |
| 0x40161400 + 4*(id>>2) | **priority level**, byte lane `id&3` = `level << 4` | `0xC1A3C(id,level)` (RMW) | level 0..0xF | [V] |
| 0x40161800 + 4*(id>>2) | **target/route byte**, byte lane `id&3` = static table value | `0xC74B0` (RMW, only ids > 0x1F and id < coreword) | table CPU 0x105F8C + 3*id + 2 | [V] |
| 0x40161380 | init word | `0xC3024` 0xC302C = 0xFFFFFFFF | - | [V write, U role] |

Registration flow [V]: `0xC6DA2(id, level, handler)` validates `id <= 0x5F` and tail-calls
`0xC74B0`, which (1) stores the handler at `0x17D430 + 4*id`, (2) calls
`0xC1A3C(id, level)` which writes the priority byte and returns -1 for invalid id/level, then
(3) for ids > 0x1F writes the target byte at 0x40161800 from the static 3-byte table.
Enabling is a *separate* call: `0xC6FF4(id)` sets the 0x40161100 bit. Valid ids for
`0xC6DB8`/`0xC6FF4` are 0x10..0x5F (a null-write BUG trap guards anything else); events
0x00-0x0F (handlers 0,1,2) therefore have **no** image-side enable/raise path [V] - open item.
Raise is additionally gated by `id < [0x17D398+0]` (`(cores+1)<<5`, 0x60 for 3 agents) [V].

**Static 3-byte table CPU 0x105F8C** (used for the target byte at 0x40161800) [V]:
for every id 0x20..0x5C the record is `{id, level_hint, target}` with target 0x01 for almost
all ids and 0x02 for id 0x3F (e.g. id 0x3E -> 01, id 0x3F -> 02). This is the per-event
core/route assignment: 0x3E/0x3F are the two pump registrations (handler 0x46848), routed to
routes 1 and 2.

**Note on the 0x40161180/0x40161280 bit polarity [U].** The code role is unambiguous:
`0xC6DB8(id)` writes `1<<(id&31)` to both banks, and callers use it at init (0x4C/0x4E from
`pcie_msg_init`), from radio interrupt sources (0x32/0x53/0x38/0x3D/0x55) and from the
exception handler; it is the image's "raise this event" primitive [V code]. The hardware bit
could be "set pending" or "unmask/clear mask": entry A also *initialises* these banks with
0xFFFFFFFF (0x40161180 words 1-2 and 0x40161380 words 1-2 at CPU 0x474AA; 0x40161380 word 0 by
`0xC3024`), which only makes sense as a mask/clear initialisation [V]. A bench read/write of the
bank (section 7) resolves the polarity; either way this primitive is what the firmware uses to
make event 0x4C happen. It is a separate bank from the per-event enable 0x40161100, whose W1S
semantics are clear [V].

**Priority levels actually configured** [V]: `0xC3024` sets ids 0x00->3, 0x01->0xE, 0x02->0,
0x1D->0xE; entry A registers 0x3E/0x3F (level 5) and 0x40/0x42 (level 5, but see below);
`pcie_msg_init` registers 0x2D/0x2E/0x4C/0x4E (level 5); the radio blocks register their ids
(levels from a table). 0x40161400..0x4016141C is first padded with 0xFFFFFFFF by `0xC3024`.

### 2.3 Timer block CA 0x40160600 - the image's own hardware event source (event 0x1D)

Writer is the scheduler `0xC704C` (CPU 0xC704C) [V]:

    CA 0x40160608 <- 0        ; stop
    CA 0x4016060C <- 1        ; start/arm
    CA 0x40160600 <- 0x0061A800
    CA 0x40160604 <- 0x0061A800
    CA 0x40160608 <- 7
    CA 0xC6FF4(0x1D)          ; enable event 0x1D

Handler 0x1D is the Thumb function at `0xC6C88` (entry A stores 0xC6C89 at
`0x17D430[0x1D]` at CPU 0x46E84): it re-arms CA 0x4016060C = 1 at CPU 0xC6C92 and services the
per-core timer/thread arrays at CPU 0x173060 (+0xCC per core). `0xC3024` also gives event
0x1D priority level 0xE (highest configured). **A firmware booted normally therefore takes a
periodic event 0x1D from this timer; it is the first hardware event the image itself
creates** [V/I]. Note the block addresses are consecutive and the writes are made from the
same function that enables 0x1D, so the timer is the event 0x1D source [I, high confidence].

### 2.4 Per-core control / inter-core doorbells

| CA | role | writer / value | ev |
| --- | --- | --- | --- |
| 0x40161000 | core/controller presence word = 1 | entry A 0x46ED4 only writer | [V write, U role] |
| 0x40161004 | core source: entry A takes `(v & 0x1F) + 1` and shifts <<5 | read by entry A 0x46E8A, by `0xC3024`? no | [V] |
| 0x40161C00 | written 0 by entry A (0x46E98) | [U] role (event routing/base?) | [V write, U role] |
| 0x40161F00 | **inter-core SW interrupt/trigger**: `(1<<core)<<16 | 1` | `0xC20C0` (CPU 0xC20EA); exception handler writes 0x00010002 / 0x00020002; entry A writes 0x00020000 before its handshake poll | [V/I] |

`0x40161F00` is the only register that looks like a CPU-to-CPU doorbell: the exception handler
(0xC6E14) writes `0x10002` (core 0) or `0x20002` (core 1) after raising its per-core event,
and entry A writes `0x20000` when it starts the 0x1BF008 handshake [V]. Whether the host may
safely write it is **not** established - not part of the recommended action.

### 2.5 The radio event pump block CA 0x40036000 and status/mask 0x40036800/0x40036900

Handler 0x3E/0x3F = `0x46848` (event pump), fully decoded [V]:

    r7 = CPU 0x117C00 (boot global); base = [r7+0xEE0] (=0 at boot)
    status = word[ ((base + 0x8000000 + 0x6D00) << 3) ]        ; == CA 0x40036800 at boot
    for each set bit n (lowest first):
        block = CA 0x40036000 + 0x40*n
        if (status bit n) && ([block+0x10] & 0x80): *cb = [r7+0xEE4]=0x461B7, cb(n, payload)
        if (status bit n) && ([block+0x10] & 0x40): *cb = [r7+0xEE8]=0x467D9, cb(n, payload)
        if ([block+0x10] & 0x01) == 0: CA 0x40036018+0x40*n <- 1 ; 0x4003601C+0x40*n <- 1
        clear status bit n

* per-slot flags at `block+0x10`: bit7 -> callback A (0x461B7), bit6 -> callback B (0x467D9),
  bit0 = "no explicit ack" [V].
* 32-byte payload at `block+0x20..+0x3F` copied to the handler stack buffer [V].
* `CA 0x40036900` is a global mask/state word read by `0xBBD54`, the device-side pump request
  helper: it tests bit `[obj+8]`, and if free writes `1<<[obj]`/`1<<[obj+4]` into the
  per-slot block and sets `1<<[obj+8]` in 0x40036900 [V]. This is how radio/PHY code raises
  pump slots. Status 0x40036800 = 0 in our state is expected while the radio side produces
  nothing; it is not the boot gate.

### 2.6 Mailbox block CA 0x40039000 / doorbells

| CA | role | ev |
| --- | --- | --- |
| 0x40039010 | mailbox word out[0]: 11-bit pending bitmap (H2D announce / D2H ids) | [V] |
| 0x40039014 | mailbox word out[1]: D2H pending bitmap (bit2 platform-up = 0x4) | [V\*] |
| 0x400392D4 | mailbox word out[2]: H2D announce doorbell (bit0 OR'd by host `pcie_msg_send`) | [V] |
| 0x40101438 | out[3] D2H ack (host writes 1) | [V] |
| 0x40101414 | out[4] D2H re-arm (host writes 1) | [V] |
| 0x400392F0 | out[5] - **never write** | [V] |
| 0x40101434 | D2H doorbell (device RMW |=1) | [V] |
| 0x40101410 | bit0 set by `pcie_msg_init` (CPU 0x497A4) - D2H notify enable [U exact role] | [V write] |

The host->device message path is **out[0] pending bitmap + out[2] bit0 doorbell** (vendor
`pcie_msg_send(chip,3)`), which the device must consume in the mailbox dispatcher `0xC18AC`
(wrapper 0x40294, event 0x4C) / D2H flush `0xC6108` (wrapper 0x402A0, event 0x4E) [V].
`pcie_msg_init` clears out[0]/out[1] to 0 at CPU 0x4978C/0x4978E [V]; that is why out[0..2]=0
after boot is expected even with a live event layer.

### 2.7 ETE interrupt word CA 0x40039508 (and the firmware's separate ETE control 0x4003A86C)

* `0x40039508` is the ETE interrupt/mask word. The vendor **host** driver maps it from
  `g_shuangta_ete_priv_res` (.data: {0x4003A000, 0x40039508}) and `pcie_ete_intr_init`
  performs an RMW with mask `0xffe0f8f8` (clears bits 0-2, 8-10, 16-20, 24-28) [V module
  data + prior lane]. No host write reaches it in our transport unless we run that init.
  Current device read 0x3f3f1f1f is the **pre-clear default**; the cleared form is
  0x3f201818 (= 0x3f3f1f1f & 0xffe0f8f8) [V\*/V].
* The **firmware** write at CPU 0x496F4-0x496FC (`pcie_msg_init`) is NOT to 0x40039508: it
  dereferences the static resource word CPU 0x10F1FC = **0x4003A86C** and ANDs it with
  `0xE0E0F8F8`, after setting bits 12 and 29 of the same register at CPU 0x496B0-0x496C6 [V].
  So 0x4003A86C is the device-side ETE control/interrupt register the firmware manages; the
  earlier forward reference "firmware writes 0xE0E0F8F8 at 0x96B0-0x96FC" is this site (file
  0x96B0 = CPU 0x496B0). The host-visible ETE interrupt block and the firmware ETE control are
  two different registers - do not conflate them [V correction].

### 2.8 The second controller CA 0x4018010C / 0x40180110 (open)

The image tail contains a **second event dispatcher** at CPU `0x120A0C` [V]:

    r4 = [CA 0x4018010C]; idx = r4 & 0x3FF; counter = [page+8]; cpsie i;
    if (idx <= 0x5F) { handler = [CPU 0x107B8 + 4*idx]; if (handler) blx handler; }
    cpsid i; [CA 0x40180110] = r4; counter--

with the handler table at **CPU 0x107B8**, i.e. *below the image base* 0x40000 (ROM/RAM region,
not image data). It has **no in-image caller or pointer** [V negative].

New finding: the code at **CPU 0x120A6C** (immediately before the dispatcher) is a
**secondary-core / CPU1 boot init** [V]:
* it disables and re-enables the MMU/caches through CP15 (`mrc/mcr p15, c1` M bit off at
  0x120A9A-0x120AAA, TLB/cache invalidation via `mcr p15, c6` at 0x120AE6-0x120AF6, M bit on
  at 0x120B00-0x120B0C) - the signature of a core starting from reset with the MMU off;
* it writes a **mirror** of entry A's SoC-glue block at CA **0x40033024..0x40033048** (the same
  constants 0x1A/3/0x70/0x7FF/0x50/0x50A/3/0x301 that entry A writes to 0x40105024..) [V];
* it memsets a 0x180-byte handler table at **CPU 0x107B8** (`blx 0x1220BC`), reads
  CA **0x40181004** low 5 bits +1 <<5 and stores the coreword at CPU 0x10938, then initialises
  the second controller (0x40181004 / 0x40181400..0x4018141C) and its per-event banks - the
  same structure as the CPU0 controller but at CA 0x4018xxxx / table 0x107B8 [V];
* its dispatcher is 0x120A0C (0x4018010C / 0x40180110).

Interpretation [I]: **the two controllers are per-core.** CPU0 (the image's main init/scheduler
path) owns 0x40160100 / 0x17D430; CPU1 (the image tail at 0x120A6C) owns 0x4018010C / 0x107B8.
The image therefore contains its own secondary-core bootstrap. The entry-A handshake
(CPU 0x1BF008, wake via CA 0x40161F00, section 6.3) is most plausibly CPU0 waiting for CPU1 to
come up and announce readiness (0x1BF008 = 2). If CPU1 never starts, the CPU0 scheduler/timer
*and* the whole second controller stay dead - exactly the observed state. Open question [U]: who
starts CPU1 (the ROM release path, or a mechanism this image expects from the ROM), and whether
it can be observed from the host. This is **not** on the host-actionable path but it is the
first thing a ROM decode should answer.

### 2.9 The 0x40105000 block is NOT event routing (correction)

Entry A (CPU 0x46E10-0x46E4C) writes CA 0x40105024 <- 0x1A, 0x40105028 <- 3,
0x4010502C <- 0x70, 0x40105030 <- 0, 0x40105034 <- 0x50A, 0x40105038 <- 0x50,
0x40105044 <- 0x7FF, 0x40105048 <- 3, and 0x4010502C/30 later <- 0 and <- 0x301 [V]. These are
SoC clock/reset/glue constants, not event status: the event dispatcher reads 0x4016010C
(section 2.1) and nothing in the dispatcher touches 0x40105000 [V]. The older rom-flow statement
"event dispatcher reads CA 0x40105000" is superseded (as already corrected by fw-link-event).

---

## 3. Who initialises what (ownership census)

| initialiser | function / site | what it does to the event layer | ev |
| --- | --- | --- | --- |
| **entry A** (CPU 0x46970, header+0) | 0x46E70-0x46ED4 | fills handler table 0x17D430 with coreword/system entries; writes 0x40161C00=0, 0x40161000=1; calls `0xC3024`; registers+enables 0x3E/0x3F (0x47200-0x47218); registers 0x40/0x42 (0x46F70/0x46F7A); writes boot-global pump callbacks 0x118AE0.. ; then reaches the 0x1BF008 inter-core handshake | [V] |
| **entry B** (CPU 0xC742C, header+4) | 0xC742C-0xC7456 | `[CPU 0x1BF004] |= 0x10`; 0xC2082/0xC2020; **calls `0xC3024`**; 0xC2098; 0xBEDD8(3); 0xC73A0; tail `b 0xC704C` (scheduler) | [V] |
| **`0xC3024` controller init** | called by entry A 0x46EDA and entry B 0xC7440 | 0x40161380 = -1; 0x40161400..0x4016141C = -1; 0x40160104=0xFF; 0x40160108=3; priorities for ids 0,1,2,0x1D; **0x40160100=1** | [V] |
| **scheduler `0xC704C`** | entry A 0x47428 / entry B tail | arms timer 0x40160600/04/08/0C (0x61A800, 7, 1); **enables event 0x1D**; enters idle `0xC2B7A` which does `cpsie i` | [V] |
| **`pcie_msg_init`** (inside function 0x49334(r0=0), called by the boot task at 0x4781E via the static module table CPU 0x10F254[1] = 0x49335) | 0x49334-0x49852 | registers+enables 0x2D/0x2E (0x496CA/0x496DC/0x496E8/0x496F0); clears ETE ctrl 0x4003A86C; sets g=0x10C0F4; registers 0x4C/0x4E (0x49800/0x4980C) and enables them (0x49816/0x4981C); **raises 0x4C and 0x4E (0x49754/0x4975A)**; clears out[0]/out[1] | [V] |
| **boot task** (CPU 0x47628) | 0x47CE0 | registers event 0x02 -> 0xC20F9 (no enable possible: id < 0x10) | [V] |
| **PHY/radio init** | 0xD1EF4.., 0xD21DC.., 0xD3F64, 0xBEE40 | register/enable per-PHY ids; raise 0x32/0x53/0x38/0x3D/0x55; helper 0x46938 raises the per-core event id from table 0x105FDC+core*8+4 | [V] |
| **exception handler** (CPU 0xC6E14) | 0xC6E14-0xC6F66 | raises the per-core event, clears 0x40101250 bit5, writes flags, writes 0x40161F00 (inter-core), announces crash via out[1]=4 + doorbell 0x40101434 | [V] |

No H2D message, no host register write and no host DMA can reach any of these blocks: the
image has no host-call edge into the init functions (consistent with wifi-hcc-link), and the
boot task is dispatched from the ROM-armed reset slot CA 0x17D42C (image only disarms it) [V/I].
The event layer is thus **entirely firmware/ROM-owned at the write level**.

---

## 4. The dispatch path (recap, and the only way in)

1. ROM exception vector -> image ARM trampoline at **CPU 0x40064** (image header +0x64; ARM,
   saves context, calls `0xC2EFC`, restores with `rfeia`) [V].
2. `0xC2EFC` reads CA 0x4016010C, dispatches handler `[0x17D430 + 4*idx]`, then writes the
   status word to CA 0x40160110 (EOI) [V].
3. 0x40064 is **not referenced by image code** (no BL/literal/ADR/movw-movt) and `0xC2EFC` has
   no other caller; the ROM vector is the only invoker [V].

The image contains exactly the pieces the ROM needs: header entry A/B, five ARM exception
handlers at header +0x50..+0x60, and the IRQ/FIQ trampoline at +0x64. This is an ABI with the
ROM; the ROM side is unreadable except for the low vector window (section 7 read plan).

---

## 5. Expected FIRST events after a cold boot (ordering)

Ordering derived from code path order [V for each site]:

| # | event | source | raised at | handler | preconditions |
| ---: | ---: | --- | --- | --- | --- |
| 1 | **0x4C** | firmware itself (`pcie_msg_init`) | CPU 0x49756 | 0x40295 -> mailbox dispatcher 0xC18AC (H2D pending bitmap) | boot task calls 0x49334(r0=0); IRQ delivery |
| 2 | **0x4E** | firmware itself | CPU 0x4975C | 0x402A1 -> D2H flush 0xC6108 | same |
| 3 | **0x1D** | hardware timer CA 0x40160600 (reload 0x61A800, ctrl 7) | periodic | 0xC6C88 timer tick | scheduler `0xC704C` reached (entry A after 0x1BF008 handshake, or entry B tail) |
| 4 | 0x2D / 0x2E | PCIe/ETE (host-armed ETE rings) | ETE/DR done | 0x462F9 / 0x4624D | host programs ETE rings; ETE IRQ block |
| 5 | 0x3E / 0x3F | radio event pump block 0x40036000/0x40036800 | per-slot | 0x46848 pump -> callbacks 0x461B7/0x467D9 | PHY/radio activity |
| 6 | 0x32/0x38/0x3D/0x53/0x55 (+per-core id from 0x105FDC) | PHY/radio blocks | CPU 0xB0CF8/0xB0D36/0xD4758/0xBEE2E and 0xC6E14 | per-id handlers | radio init/exception |
| 7 | 0x4C again | **host**: out[0] pending bitmap + out[2] bit0 doorbell | host write | same as #1 | delivery path alive |

The firmware therefore expects its **own** 0x4C/0x4E raises (which are pending immediately after
`pcie_msg_init`, *before* the handlers are even registered at 0x49800/0x4980C - the dispatcher
is expected to run only once IRQs are enabled anyway) and then the **timer 0x1D** as the
steady-state hardware event. The host-triggerable event is #7 (dead in our state). Nothing in
the image raises 0x4C/0x4E again later, so a host mailbox announce is the only way to re-arm
the H2D path once the layer works.

---

## 6. Why nothing is delivered in our boot

### 6.1 The observation that matters

The parent state has handler table 0x17D430 populated for 0x2D/0x2E/0x3E/0x3F/0x40/0x42/0x46/
0x4C/0x4E, g = 0x0010C0F4 valid, registry 0x171B6C populated, boot state 1, reset slot
0x17D42C = 0xC19D1 [V\*]. The registrations for 0x4C/0x4E are at CPU 0x49800/0x4980C, i.e.
**after** the raises at 0x49754/0x4975A in the same straight-line body with no intervening
exit (only registration errors branch out at 0x49804/0x49812) [V]. Therefore **in our boot the
firmware has already software-raised events 0x4C and 0x4E**; "CA 0x4016010C = 0, nothing
pending" cannot mean "no event was ever raised", it means **no event has been delivered/vectored
to the CPU** (0x4016010C is the arbiter's current vector, acked through 0x40160110) [I].

The discriminating word is the pending-set register
**CA 0x40161188 (bank A) / 0x40161288 (bank B), bits 12 (0x4C) and 14 (0x4E)** - never read in
any prior lane. Expected if the raises latched and were not delivered: 0x40161188 = 0x00005000.
Expected if hardware consumed/never latched them: 0. This single word decides "delivery dead"
vs "raise path also dead" (section 7).

### 6.2 The IRQ-unmask gate: the image unmasks IRQ only from the scheduler/dispatcher

All `cpsie i` sites in the image [V]:

| CPU | enclosing | role |
| ---: | --- | --- |
| 0xC2F46 | `0xC2EFC` dispatcher | temporary unmask **inside** the IRQ handler |
| 0xC26C0 | deferred-dispatch loop 0xC26B8 | reached from exception return path |
| 0xC2B80 | idle `0xC2B7A` | `cpsie i; b 0xC26E0`; **called from the scheduler `0xC704C` at 0xC70B8** |
| 0x120A32 | second dispatcher 0x120A0C | inside IRQ handler |
| 0x121BBC | late/thread code | thread context |
| 0xBF68A / 0xBF6B4 | PHY/thread code | thread context |

There is **no** `cpsie` in entry A, entry B, `pcie_msg_init`, or the boot task [V negative].
So before the scheduler starts, every reachable init context runs with the ARM IRQ mask set (the
ROM release context leaves I=1, as reset does); the image only ever unmasks from the scheduler
idle or from inside an IRQ - a chicken-and-egg that the firmware resolves by starting the
scheduler. Our boot never gets there:

### 6.3 Entry A is gated before the task queue and the scheduler by an inter-core/ROM handshake

CPU 0x473CC-0x47618 [V] (inside entry A, before the task queue at 0x47424 and the scheduler call
at 0x47428):

    r4 = CPU 0x1BF008
    [0x1BF008] = 0 ; [0x1BF00C] = 0xABCD
    cache flush (blx CPU 0x102EE4)
    CA 0x40161F00 <- 0x00020000          ; inter-core/ROM wake
    isb
    wait:  r3 = [0x1BF008] ; cmp r3,#2 ; bne wait_path
    wait_path: r0 = 0x1BF008 ; blx CPU 0x102E68 (cache maintenance) ; b wait   ; infinite poll
    success:  [CPU 0x118B24] |= 0x4000 ... build task descriptor {0x104243, 0x47625, kind 7}
              bl 0xC745C (task framework, queues boot task 0x47628)
              bl 0xC704C (scheduler: arms timer, enables event 0x1D, enters IRQ-enabled idle)

The success path is only taken when the partner (the other CPU or the ROM service that
0x40161F00 wakes) writes 2 to CPU 0x1BF008 [V code]. If it never answers, entry A **never
returns**, so (i) the boot task is not re-queued by entry A, (ii) `0xC704C` never runs, (iii) the
timer 0x40160600 is never programmed, (iv) no execution context ever executes `cpsie i`, and
(v) the already-latched 0x4C/0x4E events are never delivered. This is the most economical
explanation of the whole observed state - including the observed boot task execution (it is
dispatched separately from the ROM-armed reset slot CA 0x17D42C; the image only disarms that
slot at 0x47D06) and the SVC/data-abort masks (SVC needs no IRQ; the abort comes from the
descriptor path).

The pending question is therefore **whether CPU 0x1BF008 reads 2** (handshake completed) or 0
(still spinning), which also tells us whether the timer was ever armed (CA 0x4016060C = 1 or 0).
Both are one-word reads (section 7).

### 6.4 Candidate gate table (what the new reads decide)

| # | gate | fingerprint read | "gate passed" | "gate is the blocker" |
| ---: | --- | --- | --- | --- |
| G1 | event controller globally enabled | CA 0x40160100 | 1 | 0 -> entry A never reached 0x46EDA (contradicts the observed 0x17D430[0]) |
| G2 | controller config | 0x40160104 / 0x40160108 | 0xFF / 3 | else partial init |
| G3 | per-event enables | 0x40161100/04/08 | 0x2000 / 0xC0006000 / 0x5000 (see probe) | missing bits -> that init block did not run |
| G4 | 0xC6DB8 raise-bank words for 0x4C/0x4E | 0x40161188 / 0x40161288 | 0x5000 (writes landed, nothing cleared) | 0 -> raise path also dead, or the bank is write-only (phase 2 resolves) |
| G5 | entry A handshake completed | CPU 0x1BF008 (BAR0 0x877008) | 2 | 0 -> entry A spinning; scheduler never starts |
| G6 | scheduler ran / timer armed | 0x40160600/04/08/0C | 0x61A800 / 0x61A800 / 7 / 1 | zeros -> `0xC704C` never ran |
| G7 | ROM vectors IRQ to the image | ROM words CPU 0x20..0x3C (BAR0 0x6B8020..0x6B803C), 0x1E4 | IRQ/FIQ word into image (0x40064/0x40065) | ROM-internal target -> ROM owns IRQ delivery |
| G8 | CPU IRQ masked in the boot context | not directly readable; implied by G5/G6 and by the absence of any cpsie before the scheduler | - | needs golden trace/ROM |

A useful sanity value: CA 0x40160110 read tells whether an EOI was ever written; read it
(read-only), never write it.

### 6.5 Not the cause (with evidence)

* **Pump status 0x40036800 = 0** - expected; the radio pump has no source events in this state
  (section 2.5) [V/I].
* **Mailbox out[0..2] = 0** - expected: `pcie_msg_init` zeroes out[0]/out[1] at CPU
  0x4978C/0x4978E [V].
* **ETE word 0x40039508 = 0x3f3f1f1f** - pre-clear default; host `pcie_ete_intr_init` is not
  run by our transport (would make 0x3f201818), but this is a host-visible ETE interrupt-block
  gap, not the device event controller [V/V\*].
* **PHY table / radio enables** - prior lanes showed the PHY tables are applied; radio events
  are downstream of the delivery path anyway.

---

## 7. The single next bench action (attended, recovery armed): event-controller delivery probe

Two-phase, one bench step. Phase 1 is **read-only**; phase 2 is one conditional 4-byte write
that is **exactly the write the firmware's own raise primitive `0xC6DB8` performs**, so it does
not invent an event or a register value. The host must not write CA 0x40160110 (EOI), any
0x17D4xx handler slot, any firmware RAM object except the two diagnostic words below (and only
if the Lead approves phase 3), or out[5].

### Phase 1 - read-only fingerprint (no writes; 21 words)

`BAR0 = 0x3B8000 + CA - 0x40000000` for IO, `BAR0 = CPU + 0x6B8000` for internal.

| # | CA / CPU | BAR0 | expected on a healthy armed firmware | decides |
| ---: | --- | ---: | --- | --- |
| 1 | 0x40160100 | 0x518100 | 0x00000001 | G1 |
| 2 | 0x40160104 | 0x518104 | 0x000000FF | G2 |
| 3 | 0x40160108 | 0x518108 | 0x00000003 | G2 |
| 4 | 0x40161000 | 0x519000 | 0x00000001 | entry A past 0x46ED4 |
| 5 | 0x40161C00 | 0x519C00 | 0x00000000 | entry A past 0x46E98 |
| 6 | 0x4016010C | 0x51810C | 0 = no IRQ being taken (nonzero = event mid-dispatch) | live status |
| 7 | 0x40160110 | 0x518110 | last EOI word (read-only!) | dispatch ever happened |
| 8 | 0x40161100 | 0x519100 | 0x00002000 if scheduler armed timer 0x1D, else 0 | G3/G6 |
| 9 | 0x40161104 | 0x519104 | 0xC0006000 (0x2D+0x2E+0x3E+0x3F enabled) | G3 |
| 10 | 0x40161108 | 0x519108 | 0x00005000 (0x4C+0x4E enabled) | G3 |
| 11 | 0x40161188 | 0x519188 | 0x00005000 = 0x4C+0x4E still latched (delivery dead); 0 = consumed/never latched | **G4** |
| 12 | 0x4016118C | 0x51918C | 0 | - |
| 13 | 0x40161288 | 0x519288 | same as #11 (mirror bank) | G4 |
| 14 | 0x40160600 | 0x518600 | 0x0061A800 (scheduler ran) | G6 |
| 15 | 0x40160604 | 0x518604 | 0x0061A800 | G6 |
| 16 | 0x40160608 | 0x518608 | 0x00000007 | G6 |
| 17 | 0x4016060C | 0x51860C | 0x00000001 | G6 |
| 18 | 0x4016141C | 0x51941C | byte1 = 0xE0 (id 0x1D level 0xE) | priority bank live |
| 19 | 0x4016142C / 0x4016143C / 0x4016144C | 0x51942C/3C/4C | byte lanes 0x50 at ids 0x2D/0x2E, 0x3E/0x3F, 0x4C/0x4E | priority bank live |
| 20 | CPU 0x1BF008 / 0x1BF00C | 0x877008 / 0x87700C | 0 / 0xABCD (still spinning) vs 2 / 0xABCD (released) | **G5** |
| 21 | CPU 0x20..0x3C, 0x1E4 | 0x6B8020..0x6B803C, 0x6B81E4 | vector-target words (ROM); 0x1E4 = 0x0004005C [V\*] | **G7** |

Decision after phase 1:
* #11/#13 = 0x5000 and #6 = 0 but #1..3 healthy -> **events latched, delivery dead**. If #8=0 /
  #14-17 = 0 / #20 = 0, it is the section 6.3 story: entry A never released, scheduler never
  ran, no context unmasks IRQ. Proceed to phase 2 for the direct delivery test, then the
  conclusion is ROM/CPU-internal and needs the ROM vector decode (#21) or a vendor golden trace.
* #11/#13 = 0 -> the raise path itself is dead (unexpected); stop; re-check #1..#3 and the
  handler table before any write.
* #20 = 2 and #17 = 1 -> the scheduler really did start; then the blocker is purely CPU/ROM IRQ
  delivery (phase 2 + #21).

### Phase 2 - the one write: replay the firmware's own raise for event 0x4C

Only if phase 1 shows #1 = 1, #10 = 0x5000 (enable present) and #11/#13 = 0x5000 or 0:

    write 0x00001000 to CA 0x40161188   (BAR0 0x519188)   ; bank A, event 0x4C
    write 0x00001000 to CA 0x40161288   (BAR0 0x519288)   ; bank B, exact 0xC6DB8 equivalent
    then poll for <= 500 ms (+ a few seconds for a slow path):
      CA 0x4016010C, CA 0x40160110, CA 0x40161188, CA 0x40161288,
      CA 0x40039010/0x40039014 (did the mailbox dispatcher clear/consume?), CA 0x17D430[0x4C]

Value: event id 0x4C -> word index 0x4C>>5 = 2 -> 0x40161180+8 = 0x40161188, bit 0x4C&0x1F = 12
-> 0x1000 (and its mirror 0x40161288). `0xC6DB8(0x4C)` writes exactly these two words; the
firmware itself has already done it once at CPU 0x49756, so this is a replay, not a new event
type [V].

Expected observables:
* **Success (delivery path works):** within a tick the dispatcher runs: CA 0x40160110 gets a
  status word written (non-zero history), the handler 0x40295 -> 0xC18AC executes (it writes
  `[g+0xC]=1`, reads/clears the mailbox words and writes 8 to `[g+0x10]`), the pending bits
  clear, and CA 0x40039010/14 show the dispatcher's reset pattern. In that case the H2D path is
  alive: immediately re-run the existing id-1 wake probe (out[0] |= 2, out[2] |= 1) and expect
  out[0] bit to clear and out[2] to read 8.
* **Null (delivery path dead):** CA 0x40161188 bit12 stays set (the W1S landed, nothing
  consumed it), CA 0x4016010C/0x40160110 stay 0. This is the **definitive** answer that the
  missing stage is CPU/ROM IRQ delivery (masked IRQ or ROM vector not entering image 0x40064),
  not an unarmed event source and not a host-actionable gap.
* **Ambiguous (pending bit reads 0 immediately):** treat the pending registers as
  write-only/self-clearing; re-read CA 0x40161188/#10/#1 and use #21 + a vendor golden trace.

### Phase 3 (only if phase 1 confirms G5 = spinning; Lead decision, not part of this lane)

If CPU 0x1BF008 reads 0 (entry A still spinning at 0x473F0) the *only* host action that could
unblock the scheduler is to write **2** to CPU 0x1BF008 through BAR0 0x877008, faking the
inter-core handshake reply. This is a firmware-RAM write (not a device-register write), it
pretends the partner core/ROM completed its part, and it can double-dispatch the boot task
(state 1 path) - it must be a separate, explicitly approved experiment, recovery armed. A
cleaner alternative is a vendor golden trace (below).

What would close the question without a write:
1. Read #21 (ROM vector target words + 0x1E4) and, if they point into ROM, decode the ROM
   IRQ entry - the only part of the contract outside the image.
2. A vendor golden trace (attended stock 5.10): record whether `0xC2EFC` ever runs, what
   CA 0x4016010C and 0x40161188 carry after boot, and whether CA 0x1BF008 ever reads 2 - that
   directly shows whether a stock cold boot passes the handshake and dispatches.

---

## 8. Evidence table (new claims of this lane)

| # | claim | artifact / method | grade |
| ---: | --- | --- | --- |
| E1 | event-controller core block 0x40160100/04/08/0C/10 roles and the dispatcher's 0xC2EFC read/ack at 0xC2F04/0xC2F52 | `devreg.py dis 0xC2EFC`; `fn_0xC3024` | [V] |
| E2 | `0xC3024` is the controller init: global enable 0x40160100=1, 0x40160104=0xFF, 0x40160108=3, priority pad + ids 0/1/2/0x1D | `devreg.py dis 0xC3024` | [V] |
| E3 | enable register 0x40161100+4*(id>>5) W1S bit id&31, ids 0x10..0x5F | `devreg.py dis 0xC6FF4`; call-site census | [V] |
| E4 | raise primitive 0xC6DB8 writes 0x40161180+4*(id>>5) and 0x40161280+4*(id>>5); valid ids 0x10..0x5F, gated by id < (cores+1)<<5 | `devreg.py dis 0xC6DB8` | [V] |
| E5 | registration 0xC6DA2->0xC74B0 writes handler 0x17D430+4*id, priority byte via 0xC1A3C, target byte 0x40161800 from static table 0x105F8C+3*id+2 for ids > 0x1F | `devreg.py dis 0xC74B0 0xC1A3C` | [V] |
| E6 | timer block 0x40160600/04/08/0C armed by scheduler `0xC704C`; event 0x1D enabled there; handler `0xC6C88` re-arms and services per-core timer arrays | `devreg.py dis 0xC704C` / `0xC6C88` | [V]/[I] |
| E7 | `pcie_msg_init` runs inside function 0x49334(r0=0), called by the boot task at 0x4781E via static module table CPU 0x10F254[1]=0x49335; it registers+enables 0x2D/0x2E/0x4C/0x4E and raises 0x4C/0x4E before registering them | `devreg.py dis 0x49334..0x49852`; `dis 0x477EC..0x4782A`; `lits` | [V] |
| E8 | firmware ETE write is CA 0x4003A86C (static resource CPU 0x10F1FC), mask 0xE0E0F8F8 - not 0x40039508; 0x40039508 is the vendor host resource (0xffe0f8f8) | `devreg.py dis 0x496B0`; CPU 0x10F1F0 data dump; plat.ko .data resource | [V] |
| E9 | pump block semantics: status 0x40036800 bit/slot, block 0x40036000+0x40n, flags 0x80/0x40/0x01, payload +0x20..0x3F, callbacks from boot global +0xEE4/+0xEE8; 0x40036900 mask via helper 0xBBD54 | `devreg.py dis 0x46848 0xBBD54` | [V] |
| E10 | entry A blocks in an infinite poll on CPU 0x1BF008 == 2 after writing {0,0xABCD} and CA 0x40161F00=0x20000, before the task queue (0x47424) and scheduler (0x47428) | `devreg.py dis 0x473CC..0x47618` | [V] |
| E11 | the image contains no `cpsie i` before the scheduler; all cpsie sites are dispatcher/idle/thread code | full-image CPSR-instruction scan | [V] |
| E12 | entry B also calls `0xC3024` (0xC7440) and tails into the scheduler `0xC704C` (0xC7456) | `devreg.py dis 0xC742C` | [V] |
| E13 | a second controller exists at 0x4018010C/0x40180110 with dispatcher 0x120A0C and handler table at CPU 0x107B8 (below image base); no in-image caller | `devreg.py dis 0x120A0C`; branch/literal negative scan | [V]/[U] |
| E17 | CPU 0x120A6C is a secondary-core/CPU1 boot init: CP15 MMU off->on, mirror of entry A's 0x401050xx glue at CA 0x400330xx, memsets the 0x107B8 handler table, stores the coreword at CPU 0x10938, initialises the 0x4018 controller; dispatcher 0x120A0C | `devreg.py dis 0x120A6C 0x160`; `dis 0x120B10 0xA0` | [V] |
| E18 | the two event controllers are per-core (CPU0 0x4016/0x17D430, CPU1 0x4018/0x107B8); entry A's 0x1BF008 handshake is CPU0 waiting for CPU1 readiness | E12/E13/E17 dataflow | [I] |
| E14 | the vendor host modules contain no 0x4016/0x4018 event-controller CA constants (0x40039508 is the only event-block CA, as an ioremap resource) | `ko_devca.py scan` + raw u32 search on both modules | [V] |
| E15 | 0x40105000 block is SoC clock/reset glue written by entry A; not event routing | `devreg.py dis 0x46DE0..0x46E60` | [V] |
| E16 | 0x4C/0x4E were raised before their registration in the same straight-line `pcie_msg_init` body, so a booted state with [0x4C]/[0x4E] handlers proves the raises happened | E7 + handler-table observation [V\*] | [V]/[I] |

Corrections to prior lanes (all [V] here):
* "firmware writes E0E0F8F8 to ETE intr 0x40039508" -> the firmware writes **0x4003A86C**
  (static resource), while the vendor **host** driver owns 0x40039508. File 0x96B0 = CPU
  0x496B0 is the site of the bit-12/29 writes to 0x4003A86C.
* "the image expects hardware event delivery through the CA 0x40105000 block" -> the dispatcher
  reads 0x4016010C; 0x40105000 is SoC glue (already noted by fw-link-event, now proven).
* "pcie_msg_init = 0x496C8" -> 0x496C8 is a label in the middle of the init function
  **0x49334**; the entry from the boot task is the static module-table pointer 0x49335.

---

## 9. Open items ([U])

1. **Did entry A's 0x1BF008 handshake ever complete in our boot?** One word (CPU 0x1BF008 via
   BAR0 0x877008) decides the whole section 6 story. Phase 1 #20.
2. **CPU IRQ mask / ROM vector:** whether the ROM leaves IRQ enabled for our release path and
   where ROM vector word 0x38 points. Needs the ROM vector words (phase 1 #21) and a ROM decode.
3. **Readback semantics of 0x40161180/0x40161280:** write-only W1S or readable pending latch?
   Phase 2 resolves empirically.
4. **Role of 0x40160108 (=3), 0x40161000 (=1), 0x40161C00 (=0), 0x40161380 (=-1):** written but
   no in-image reader; these are controller configuration bits whose exact hardware meaning is
   [U]. They are set identically by a normal firmware boot, so they are a good arming fingerprint
   but not individually decisive.
5. **Events 0x00-0x0F** (handlers 0,1,2) have no enable/raise path through 0xC6FF4/0xC6DB8
   (the helpers reject id < 0x10 with a BUG trap) - they must be ROM-raised or special; not on
   the boot path in evidence.
6. **Second controller 0x4018xxxx / CPU1 boot init 0x120A6C:** who starts CPU1 and how CPU1's
   completion is signalled at CPU 0x1BF008 is [U] (the ROM release path or a ROM service). The
   image-tail code is a complete CPU1 bootstrap that expects to run from reset (MMU off), so it
   is most likely entered from the ROM's secondary-core release, not from image code. This is
   the highest-value ROM-decode target now.
7. Whether entry A's handshake partner is CPU1 (running the image tail code at 0x120xxx) or a
   ROM service. The image tail programs 0x40181004/0x40181400.., which suggests a second-core
   path exists in the image.

---

## 10. Reproduction (host-only; local blob only; nothing vendor committed)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-event-router/tools

    # controller map / dispatch chain
    $PY $T/devreg.py dis 0xC2EFC 0xA0            # dispatcher: 0x4016010C -> table 0x17D430 -> 0x40160110
    $PY $T/devreg.py dis 0xC3024 0xA0            # global init: 0x40160100/04/08, 0x40161380, 0x40161400
    $PY $T/devreg.py dis 0xC6DA2 0x20            # registrar -> 0xC74B0
    $PY $T/devreg.py dis 0xC74B0 0xA0            # handler + priority + target byte
    $PY $T/devreg.py dis 0xC1A3C 0x60            # priority byte / id validation
    $PY $T/devreg.py dis 0xC6FF4 0x60            # enable bits 0x40161100
    $PY $T/devreg.py dis 0xC6DB8 0x60            # raise: 0x40161180 + 0x40161280
    $PY $T/devreg.py dis 0x46848 0xE0            # pump 0x40036800/0x40036000 blocks
    $PY $T/devreg.py dis 0xC704C 0x110           # timer 0x40160600 + enable 0x1D + idle cpsie
    $PY $T/devreg.py dis 0xC6C88 0x60            # timer handler 0x1D
    $PY $T/devreg.py dis 0x473CC 0x90            # entry A inter-core handshake + task/scheduler entry
    $PY $T/devreg.py dis 0x49334 0x40            # boot-task module init entry (static table 0x10F254[1])
    $PY $T/devreg.py dis 0x496C8 0x170           # register/enable 0x2D/0x2E/0x4C/0x4E, raises, ETE ctrl
    $PY $T/devreg.py dis 0x120A0C 0x60           # second dispatcher 0x4018010C -> table CPU 0x107B8
    $PY $T/devreg.py census --lo 0x40160000 --hi 0x401A0000
    $PY $T/devreg.py access 0x4016010C 0x40160110 0x40161100 0x40161180 0x40161280 0x40161400 0x40161800 0x40161F00
    $PY $T/devreg.py defs --lo 0x40160000 --hi 0x40162000

    # cross-checks with the prior-lane tools
    $PY opensource/docs/incoming/wifi-fw-link-event/tools/fw_cfg.py regs
    $PY opensource/docs/incoming/wifi-fw-link-event/tools/fw_cfg.py calls 0xC704C   # scheduler call sites

    # vendor modules: no event-controller constants (0x40039508 only)
    $PY $T/ko_devca.py --ko /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko scan --lo 0x40000000 --hi 0x401a0000
    $PY $T/ko_devca.py --ko /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko scan --lo 0x40000000 --hi 0x401a0000

    # the next bench action (prints addresses only; never opens a device)
    $PY $T/evctrl_probe.py

`devreg.py` and `ko_devca.py` read the local artifacts in place; `evctrl_probe.py` prints the
probe plan only. No vendor bytes are copied into this directory.

---

*Lane: event-router, 2026-11-23, shared task task-78. Host-only, image + vendor-module static
RE. Complements fw-link-event (dispatch chain discovery), rom-flow (boot stages/exception
contract), abort-rca (descriptor abort), boot-kick (boot task / live state), hcc-link (H2D
negative) and h2d-catalog (HCC tables). Vendor blob, module contents and device values remain
local evidence only.*
