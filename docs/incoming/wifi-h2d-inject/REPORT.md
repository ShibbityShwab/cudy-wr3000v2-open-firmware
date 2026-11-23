# H2D message injector — validated host-side HCC frame path (Hi5622V100 / WR3000 v2.0)

**Lane:** wifi-h2d-inject (task-73, host-only). **Written:** 2026-11-23.
**Status:** host-side implementation COMPLETE and compile-verified (two module variants +
63-case userspace selftest). **This lane never touched the bench router**, ran no image/world
build, and committed no vendor binary/firmware byte. The bench procedure in section 7 is
for the Lead's next attended session.

Evidence markers follow the project convention: **[V]** re-verified from a named local
artifact or a command run in this session, **[V\*]** device-measured by an earlier attended
lane and recorded in the named report, **[I]** inference, **[U]** unknown / not yet pinned.

Contract sources (read first):
- <code>../wifi-hcc-re/REPORT.md</code> **§3.1** (12-byte HCC header + <code>hcc_msg_alloc</code>
  bound), **§3.3** (SR node + <code>pcie_msg_send</code> announce), **§3.5** (device id-5
  BUG() trap), **§5.5/§8** (dispatch tables + the driver-facing H2D contract).
- <code>../wifi-transport-port/REPORT.md</code> (transport, B1/B2 bench-verified),
  <code>../wifi-transport-port/build/</code> (the reference skeleton this lane patches),
  <code>../wifi-transport-port/b3/</code> (B3 instrument reference tree),
  <code>../wifi-boot-kick/REPORT.md</code> §18-21 (current firmware state / why the next lane
  is the H2D protocol-frame lane).

---

## 0. TL;DR

The transport skeleton can now inject **one arbitrary, hard-bounded HCC H2D frame from
userspace** through the decoded vendor send path:

    frame (12-byte HCC header + payload, payload <= 0x674)
      -> SR0 node {devVA, (len<<16)|0x6d2b}, packed producer commit
      -> mailbox announce: out[0] (CA 0x40039010) |= 1<<3, then out[2] (CA 0x400392d4) |= 1
      -> bounded drain-poll of out[0] + read-only out[5] (device ack) checkpoint

exposed as sysfs attributes on the transport module (<code>h2d_arm</code>,
<code>h2d_inject</code>, <code>h2d_raw</code>, <code>h2d_check</code>, <code>h2d_clear</code>,
<code>h2d_selftest</code>, <code>h2d_stats</code>), with:

- **hard refusal of HCC message id 5** in the parser, builder *and* validator, and a mailbox
  announce **hard-wired to id 3** — the device's mailbox id-5 handler is the firmware
  BUG() trap (bytes 00 23 1b 60 ff de) [V] so there is no user-controlled path to it;
- **hard bounds**: payload <= 0x674 / total <= 0x680 (hcc_msg_alloc movw bound [V]),
  group/dst 4-bit, SR node length = total frame length, no write ever targeting
  CA 0x400392f0 (out[5]) [V];
- **the exact same pure-C frame model** (parser/builder/validator) compiled into the kernel
  module and into a device-free userspace selftest: **63/63 cases pass on the host** [V];
- **two compile-ready patches**: one against the reference transport tree
  (<code>build/</code>, minimal instrument) and one against the B3 instrument tree
  (<code>b3/</code>, the current bench instrument), both built clean against the project's
  6.18.60 tree with <code>CONFIG_WERROR=y</code> and vermagic
  <code>6.18.60 SMP mod_unload ARMv7 p2v8</code> [V].

The single decisive bench question this unlocks: **does the boot-parked firmware service the
H2D mailbox at all?** Section 7 gives the one-idea session and its observables
(<code>out[0]</code> drains, <code>out[5]</code> device ack reads 1, optional
<code>out[1]</code> deposit) — no RAM pokes.

---

## 1. Deliverables (all inside this directory)

| path | what |
| --- | --- |
| <code>files/pciet_h2d_frame.h</code> | pure-C frame model: request parser, builder, validator, hex helpers, shared self-test (63 cases). Compiles both as kernel header and as userspace header. |
| <code>files/pciet_h2d.c</code> | kernel injector: state, SR0 post with packed producer advance, contract announce, bounded drain-poll, 7 sysfs attributes, per-endpoint instances. |
| <code>files/h2d_selftest.c</code> | userspace selftest + frame CLI (<code>build</code>, <code>validate</code>), no device needed. |
| <code>patches/0001-hi5622-pciet-h2d-inject-base.patch</code> | integrates the injector into the reference transport tree <code>../wifi-transport-port/build/</code> (new <code>pciet_h2d.c</code> + <code>pciet_h2d_frame.h</code>; edits <code>hi5622_pciet.h</code>, <code>pciet_main.c</code>, <code>Kbuild</code>). |
| <code>patches/0001-hi5622-pciet-h2d-inject-b3.patch</code> | same injector on the B3 instrument tree <code>../wifi-transport-port/b3/</code> -> <code>hi5622_pciet_b3.ko</code> with both <code>b3_*</code> and <code>h2d_*</code> attributes. |
| <code>tools/apply-edits.py</code> | the deterministic integration edits (each anchor asserted unique) used to regenerate the patches. |
| <code>tools/gen-patches.sh</code> | regenerates <code>patches/</code> from <code>files/</code> + the two reference trees. |
| <code>tools/build.sh</code> | host-only driver: runs the selftest, applies the patch to a copy, builds one out-of-tree module. Never an image build, never a device access. |

No vendor source text, object code, firmware blob, calibration data or per-unit value is
reproduced anywhere in this directory; every line is original text written from the decoded
contract.

---

## 2. The contract encoded (and how each field maps to code)

### 2.1 HCC frame — 12-byte header, little-endian, payload at +0x0c [V]

<code>hcc_msg_alloc</code> (plat.ko 0x11d9c) enforces <code>size <= 0x674</code>, sets
<code>u16 hdr+4 = size + 0xc</code>, sets the allocated bit (<code>hdr[1] |= 1</code>) and
<code>skb_put(size+12)</code> [V, hcc-re §3.1]. Byte layout as implemented in
<code>pciet_h2d_frame.h</code> / verified by the selftest:

| off | width | field | injector source |
| ---: | ---: | --- | --- |
| +0x00 | u8 | group/type: low nibble = group index | <code>group=</code> (0..15) |
| +0x01 | u8 | low nibble = alloc state (1 = allocated); high nibble = resource-group/thread = dst | <code>(dst&lt;&lt;4) OR 0x1</code> |
| +0x02 | u8 | per-message field A | <code>a=</code> (optional, 0..255) |
| +0x03 | u8 | per-message field B (chip id / vap id) | <code>b=</code> (optional) |
| +0x04 | u16 | **total length = payload + 12** | builder |
| +0x06 | u16 | **message id** (dispatch key in the group) | <code>id=</code> (0..0xffff, never 5) |
| +0x08 | u8 | per-message field C | <code>c=</code> (optional) |
| +0x09 | u8 | dst = hdr[1]&gt;&gt;4 (resource/thread selector) | <code>dst=</code> (0..15) |
| +0x0a | u16 | transport tag **0x5a5a** | builder (fixed) |
| +0x0c | ... | payload | <code>payload=</code> hex (0..0x674 B) |

<code>hcc_msg_tx</code> (0x11bf8) copies <code>hdr[1]&gt;&gt;4</code> into <code>hdr[9]</code>,
which is why the builder writes the destination in **both** places and the validator
re-checks the invariant on raw frames [V].

### 2.2 SR node + announce [V]

<code>shuangta_ete_sr_dscr_fill</code> (0x17858): <code>node[idx].word0</code> = buffer device
VA, <code>node[idx].word1 = (u16 len &lt;&lt; 16) | 0x6d2b</code> where <code>len</code> is the
**buffer total** (== frame total, <code>u16 [buf+4]</code>), then
<code>pcie_ete_ring_ptr_plus</code> commits the packed producer index
(<code>idx=(idx+1)&amp;0x3ff; if ((idx&amp;0x3ff)==depth) { idx&amp;=~0x3ff; idx^=0x400; } store</code>)
and <code>pcie_msg_send(chip, 3)</code> announces [V, hcc-re §3.3].

<code>pcie_msg_send</code> (0x160f4) for id 3 is: set pending bit, write the pending bitmap to
out[0], then <code>out[2] |= 1</code> (out[0] = CA 0x40039010, out[2] = CA 0x400392d4;
out[5] CA 0x400392f0 must never be written from the host) [V, hcc-re §3.3/§5.1]. The device
side (hcc-re §3.5) on the doorbell writes the ack <code>*(0x400392f0)=1</code>, reads+clears
out[0], writes <code>0x400392d4 = 8</code>, then dispatches the lowest set bit — **those
read-backs are exactly the injector's liveness observables**.

---

## 3. Sysfs interface

Attributes live in the endpoint's PCI device directory, e.g.
<code>/sys/bus/pci/devices/0000:00:00.0/</code>. The interface is per **endpoint instance**
(each of the two Hi5622 endpoints enumerating on this board gets its own instance and its own
SR0 ring state; use one endpoint per session — the B3 procedure uses
<code>0000:00:00.0</code>).

| attribute | mode | purpose |
| --- | --- | --- |
| <code>h2d_arm</code> | WO | **single-shot** SR0 programming: requires <code>windows=1</code> (iATU windows prepared first), calls the skeleton's <code>pciet_ete_ring_init(ctx,&amp;sr0,true)</code>, verifies base is inside the RC inbound DMA window <code>0x80000000..0x8fffffff</code> and depth-1 = 0x1f, records the producer. Firmware-owned registers (+0x00 enable, +0x08 ctrl, consumer) are **not** written. Reload the module to re-arm (safe only before the CPU release). |
| <code>h2d_inject</code> | WO | build one frame from <code>group=.. id=.. dst=.. [a=] [b=] [c=] [payload=..]</code>, validate, post on SR0, announce. |
| <code>h2d_raw</code> | WO | post an arbitrary 12+ byte frame given as hex (whitespace tolerated), fully re-validated. Escape hatch for frames derived from traces. |
| <code>h2d_check</code> | WO | parse/build/validate/print only — **no device write, no arm needed**; safe before the CPU release. Auto-detects builder text vs raw hex (<code>=</code> present => builder). |
| <code>h2d_clear</code> | WO | write 0 to out[0] only. Clears a stale *host-set* pending word after a documented announce timeout; cannot fake a device ack. |
| <code>h2d_selftest</code> | WO | run the shared 63-case frame-model self-test **on the target** (same code as the host selftest). |
| <code>h2d_stats</code> | RO | state + last request/frame + last announce read-backs (<code>out0 before/write/after</code>, <code>out2 before/write</code>, <code>out5_read</code>, SR0 <code>cns</code>, drain polls) + live <code>out0/out2/out5</code> + boot flags + contract constants + last frame hex head. |

### 3.1 Builder grammar (<code>h2d_inject</code>, whitespace-separated key=value)

    group=<0..15>        required  (header +0x00 low nibble)
    id=<0..65535>        required  (header +0x06; id 5 is REFUSED)
    dst=<0..15>          required  (header +0x09 and +0x01 high nibble)
    a=<0..255>           optional, default 0 (header +0x02)
    b=<0..255>           optional, default 0 (header +0x03)
    c=<0..255>           optional, default 0 (header +0x08)
    payload=<hex>        optional, default empty; even digit count, 0..0x674 bytes,
                         optional 0x prefix, case-insensitive

Exact bytes produced by the host tool (selftest/tool run [V]):

    group=15 id=0 dst=0                -> 0f010000 0c000000 00005a5a            (12 B)
    group=4  id=1 dst=0 payload=00     -> 04010000 0d000100 00005a5a00          (13 B)
    group=3  id=8 dst=2 a=1 b=2 c=3 payload=deadbeef
                                       -> 03210102 10000800 03025a5a deadbeef (16 B)

<code>h2d_raw</code> example (the liveness frame above as raw hex):

    0f0100000c00000000005a5a

### 3.2 Validation matrix (all enforced before any device access)

| rule | code | sysfs error | selftest coverage |
| --- | --- | --- | --- |
| payload &gt; 0x674 / total &gt; 0x680 | H2D_EBOUNDS | -ERANGE | builder + parser + raw + validator |
| HCC message id == 5 | H2D_EREFUSED | -EACCES | builder, parser, raw, validator |
| group &gt; 0x0f, dst &gt; 0x0f | H2D_EBOUNDS | -ERANGE | builder + parser |
| declared length != actual length | H2D_EARG | -EINVAL | validator + raw |
| transport tag != 0x5a5a | H2D_EARG | -EINVAL | validator + raw |
| hdr[9] != hdr[1]&gt;&gt;4 | H2D_EARG | -EINVAL | validator + raw |
| short frame (&lt; 12 B), odd/bad hex | H2D_EARG | -EINVAL | validator + raw |
| missing/duplicate/unknown key, bad number | H2D_EARG | -EINVAL | parser |
| frame does not fit caller buffer | H2D_EBUFFER | -ENOBUFS | builder |
| ring not armed / no SR0 space / out[0] not 0 | (device state) | -EBUSY | bench procedure |

Runtime refusals and timeouts are logged with <code>dev_warn</code> and counted in
<code>h2d_stats</code> (<code>refused</code>, <code>drain_timeouts</code>).

### 3.3 Delivery sequence per injection (one mutex; announce under doorbell_lock)

1. armed? <code>windows=1</code>? SR0 post slots below depth? **<code>out[0]</code> reads 0?**
   else <code>-EBUSY</code>.
2. resync the tracked SR0 producer with the live register (warn + self-heal if the base
   <code>send</code> path was used).
3. copy the frame to <code>sr0.bufs[idx]</code>, write
   <code>node[idx] = {buf_dma[idx], (len&lt;&lt;16)|0x6d2b}</code>, <code>wmb()</code>, commit
   the packed producer (<code>pcie_ete_ring_ptr_plus</code> semantics) via the single gated
   writer.
4. announce: under <code>doorbell_lock</code>, <code>out[0] |= 1&lt;&lt;3</code>, read back,
   <code>out[2] |= 1</code>, read back; keep the base module's deferred pending shadow clear.
5. bounded drain-poll of <code>out[0]</code> bit 3 (500 x 100 us = 50 ms); read
   <code>out[5]</code> **read-only** (device ack) and log the whole exchange;
   <code>h2d_stats</code> keeps the values.
6. success: <code>injected++</code>; timeout: frame posted but unacked ->
   <code>-ETIMEDOUT</code>, <code>drain_timeouts++</code>, no retry in the same session
   (section 7 stop rules).

---

## 4. Integration patch shape (small and reviewable)

Both patches change exactly 3 existing files and add 2:

1. <code>hi5622_pciet.h</code>: prototypes only — <code>pciet_writel_checked()</code>
   (exported so the injector's device writes still pass the skeleton's single
   never-write-out[5] gate) and <code>pciet_h2d_{probe,remove}()</code>. No struct change.
2. <code>pciet_main.c</code>: (a) drops <code>static</code> from
   <code>pciet_writel_checked()</code> (no behaviour change); (b) one
   <code>pciet_h2d_probe(ctx)</code> call at the end of <code>pciet_probe()</code>; (c) one
   <code>pciet_h2d_remove(ctx)</code> call at the start of <code>pciet_remove()</code>.
3. <code>Kbuild</code>: add <code>pciet_h2d.o</code> to the module's object list.
4. new <code>pciet_h2d.c</code>, <code>pciet_h2d_frame.h</code>.

The injector keeps its per-endpoint state in a small global list keyed by
<code>struct hi5622_pci *</code> (lookup via <code>pci_get_drvdata</code>), so no shared-struct
ABI change was needed and the B3 instrument tree can carry both instrument families side by
side (verified: the combined <code>hi5622_pciet_b3.ko</code> exposes <code>b3_*</code> **and**
<code>h2d_*</code> attributes) [V].

**Write gate:** the injector performs exactly four MMIO write sites — SR0 producer (ETE
block), out[0], out[2], and <code>h2d_clear</code>'s out[0] — and all four are calls to
<code>pciet_writel_checked()</code>, whose
<code>WARN_ON(bar0_off == BAR0(0x400392f0))</code> is the never-write gate. Verified in the
built ARM object [V]:

    arm-...-objdump -dr pciet_h2d.o | grep pciet_writel_checked
    -> 4 R_ARM_CALL relocations (SR0 PRD, out[0], out[2], out[0]-clear); no direct writel calls

**Why not route through <code>pciet_ete_sr_send()</code>?** That B2-proven helper advances the
SR producer **linearly** (<code>writel(r-&gt;producer + 1)</code>), which is only correct for
fewer than <code>depth</code> frames; the decoded engine wants the packed
<code>(idx&amp;0x3ff)|(phase&lt;&lt;10)</code> index (<code>pcie_ete_ring_ptr_plus</code> [V]).
The injector therefore has its own ~20-line post with the packed advance and leaves the
bench-proven B2 path untouched (section 8 cross-lane notes).

---

## 5. Testability: one frame model, two compilers

<code>files/pciet_h2d_frame.h</code> is deliberately pure C (no kernel API, no device):
<code>#ifdef __KERNEL__</code> pulls <code>&lt;linux/kernel.h&gt;</code> +
<code>&lt;linux/string.h&gt;</code>, otherwise <code>&lt;stdint.h&gt;</code>,
<code>&lt;stdarg.h&gt;</code>, <code>&lt;stdio.h&gt;</code>, <code>&lt;string.h&gt;</code> plus
four typedefs. The kernel module and the userspace selftest therefore compile the **same
logic** for the parser/builder/validator, and the <code>h2d_selftest</code> sysfs attribute
runs the same suite on the target (compiler/arch cross-check; needs no device state).

Host run [V]:

    $ opensource/docs/incoming/wifi-h2d-inject/tools/build.sh --selftest-only
    ... 63 PASS lines ...
    SUMMARY: 63 passed, 0 failed
    h2d selftest: OK (63 passed, 0 failed)

Coverage: every header field and its little-endian position; dst nibble duplication; optional
a/b/c; empty/4-B/0x674-B payloads; the 0x675 refusal; id-5 refusal on all four paths;
group/dst bounds; buffer-capacity and no-write-past-total checks; validator invariants
(length, tag, dst consistency, short frame, alloc-bit tolerance on raw frames); parser
(defaults, duplicates, unknown keys, 0x prefixes, mixed case, odd/bad hex, missing keys,
whitespace); parse-build-validate round trip; raw-hex round trip, whitespace tolerance,
declared-length/id-5 refusals. The self-test fails closed: any case failure returns exit 1 /
<code>-EIO</code>.

Userspace CLI (same header):

    h2d_selftest                                          # run the suite
    h2d_selftest build "group=4 id=1 dst=0 payload=00"    # show resulting frame hex
    h2d_selftest validate 0f0100000c00000000005a5a         # validate a raw frame

---

## 6. Build evidence (host-only; no device contact)

<code>tools/build.sh</code> copies a reference tree, applies the committed patch with
<code>patch --fuzz=0</code>, and builds exactly one out-of-tree module against the project's
already-built 6.18.60 tree (module-only compile — no image/world build). Both variants
compile with zero compiler warnings under the tree's <code>CONFIG_WERROR=y</code> [V]:

| variant | command | module | size | sha256 (local evidence only) | vermagic |
| --- | --- | --- | ---: | --- | --- |
| base transport + injector | <code>./tools/build.sh --module-only</code> | <code>hi5622_pciet.ko</code> | 213,516 B | ca5cedff80200aabf75d0078e3c72712fdb545c81e9906b10c4721a1bed66b72 | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| B3 instrument + injector | <code>./tools/build.sh --module-only --tree b3</code> | <code>hi5622_pciet_b3.ko</code> | 299,448 B | b8a016de26bc143f734cb6594cadce57ea195c609ef34231b832f60e7ff7274a | 6.18.60 SMP mod_unload ARMv7 p2v8 |

Symbol evidence: the base module exports 28 <code>h2d_*</code>/<code>dev_attr_h2d_*</code>
symbols (<code>pciet_h2d_probe/remove</code>, the seven attribute objects and the parser/
builder/validator/selftest functions); the combined B3 module carries both <code>b3_*</code>
and <code>h2d_*</code> symbols (verified with the ARM cross <code>nm</code>) [V].

Environment notes (not defects of this lane): the OpenWrt GCC wrapper wants
<code>STAGING_DIR</code> in the environment (<code>build.sh</code> sets it for the make
invocation), and the kernel has <code>CONFIG_MODULE_STRIPPED=y</code>, so modpost prints the
pre-existing <code>missing MODULE_DESCRIPTION()</code> warning for the reference module even
though <code>pciet_main.c</code> defines one — this is how the bench-built module was already
built, not a regression from this patch [V].

The <code>.ko</code> files are **not committed** (project rule); the hashes above are
host-build evidence for the Lead to compare after rebuilding on the bench host.

---

## 7. Bench procedure for the Lead (attended only)

**Read this section with the B3 plan and the boot-kick C1e/C1c procedures.** The one new
idea here is the H2D frame injection; everything else in the session is the existing
transport/boot sequence. No RAM/IO pokes are added by this lane.

### 7.1 Preconditions (all hard; from the B3 plan §1)

| # | gate | pass criterion |
| --- | --- | --- |
| G1 | recovery door armed | <code>tools/recovery-card.sh</code> READY (physical power-off + RESET + TFTP, human present) |
| G2 | bench link | <code>tools/bench-link.sh</code> PASS |
| G3 | UART capture alive | <code>tools/uart-capture.sh</code> to an absolute path; **read-only**, never write the tty |
| G4 | image + module | 6.18.60 image (W26a+ with dma-ranges), module vermagic <code>6.18.60 SMP mod_unload ARMv7 p2v8</code> |
| G5 | no vendor Wi-Fi modules | <code>lsmod</code> shows neither <code>hi5622v100_plat</code> nor <code>hi5622v100_wifi</code> |
| G6 | firmware blob | <code>/lib/firmware/hi_wifi/FIRMWARE.bin</code>, 928,920 B (local evidence only) |
| G7 | endpoint discipline | all steps on **<code>0000:00:00.0</code>**; only one instrument module loaded |
| G8 | post-release discipline | after <code>release</code>, **never <code>rmmod</code>** — power-cycle is the only reset |

### 7.2 Build and stage the module (host, no image build)

    cd opensource/docs/incoming/wifi-h2d-inject
    ./tools/build.sh --module-only --tree b3     # recommended: B3 instrument + injector
    #   -> /tmp/wifi-h2d-inject-b3/tree/hi5622_pciet_b3.ko
    # minimal alternative (base transport + injector only):
    ./tools/build.sh --module-only               # -> .../hi5622_pciet.ko
    tools/rscp.sh /tmp/wifi-h2d-inject-b3/tree/hi5622_pciet_b3.ko root@<device>:/tmp/

Record the module sha256 in the session log. The B3 plan's address table and step sequence
still apply; this section only inserts the H2D steps.

### 7.3 Session A — decisive first test: is the H2D mailbox alive? (recommended)

**One idea: does the parked/booted firmware service the H2D mailbox doorbell?** This is the
single question that decides whether the H2D protocol lane is open or the remaining blocker is
firmware-internal (boot-kick §19.4/§21). It writes only the two host-owned mailbox registers
plus one SR0 node, and it does **not** pick a known handler family (the frame uses group 15,
beyond every decoded HCC group table [I]), so the firmware's HCC dispatch, if reached, should
drop it after the mailbox ack — the ack itself is the signal.

Recommended module: **base transport + injector** (<code>hi5622_pciet.ko</code>) because it
is minimal and its <code>poll_ms</code> poll thread drains out[1]; or the combined B3 module
with <code>poll_ms=0</code> if the session is already a B3 session (keep the existing service
loop).

On the device shell (<code>B3_SYSFS=/sys/bus/pci/devices/0000:00:00.0</code>):

    # 0) load the chosen module (base default poll_ms=500 for Session A)
    # 1) transport bring-up exactly as B1/B2 (base module) or B3 steps 1-4:
    echo 1 > $B3_SYSFS/prepare        # 6/6 iATU viewports readback=OK, windows=1
    echo 1 > $B3_SYSFS/upload         # 928,920 B -> BAR0+0x6f8000
    echo 1 > $B3_SYSFS/verify         # diffs=0 match=YES   (STOP if not)
    # 2) [B3 variant only] echo 1 > $B3_SYSFS/b3_p0_ringinit   # 7/7 readback OK
    # 3) arm the injector SR0 ring BEFORE the release
    echo 1 > $B3_SYSFS/h2d_arm
    cat $B3_SYSFS/h2d_stats           # expect armed=1, sr0 base in 0x8xxxxxxx
    # 4) [optional, no device writes] pre-check the exact frame line on target
    echo 'group=15 id=0 dst=0' > $B3_SYSFS/h2d_check
    # 5) release + announce, and keep the 0xcece gate served for the observation
    #    window (a single 0xcece write was observed to be wiped on a cold boot:
    #    boot-kick §18.4 [V*]); reuse the C1e/C1c service tooling for the loop.
    echo 1 > $B3_SYSFS/release
    ( while :; do echo 1 > $B3_SYSFS/announce; sleep 0.001; done ) &
    GATE_PID=$!
    sleep 3                            # device posts READY ~3 s after release [V*]
    # 6) capture the pre-injection state, then INJECT ONCE
    cat $B3_SYSFS/h2d_stats ; cat $B3_SYSFS/status ; cat $B3_SYSFS/out1
    echo 'group=15 id=0 dst=0' > $B3_SYSFS/h2d_inject ; echo "inject rc=$?"
    # 7) immediately after: read the read-backs (no further writes)
    cat $B3_SYSFS/h2d_stats
    dmesg | tail -40
    kill $GATE_PID                     # end: power-cycle (never rmmod after release)

The injector's <code>dev_info</code> line names the whole exchange, e.g. (values illustrative,
not measured yet):

    hi5622_pciet h2d: inject ok: frame 12 B posted on SR0 (node idx 0, producer -> 0x1),
      announce out[0] 0x00000000->0x00000008 doorbell 0x00000001, drained after N us,
      out[5]=0x00000001 (read-only)

### 7.4 Decision matrix for Session A

| <code>h2d_stats</code> after the single injection | interpretation | next step |
| --- | --- | --- |
| <code>acked=1</code>, <code>out0_after=0x0</code>, <code>out5_read=0x1</code>, <code>drain_timeouts=0</code> | **H2D mailbox + device dispatch path executed**: the firmware is alive enough to service the doorbell and write its ack — the strongest result this instrument can produce. | New session: escalate to a documented frame (7.5); capture mask/state/out[1]/DR fills as in C1e for side effects. |
| <code>acked=1</code>, <code>out0_after=0</code>, <code>out5_read=0</code> | out[0] drained but no out[5] device ack observed — record exactly; the ack write may be conditional (device-side handler order [U]). | New session: repeat with the service loop active and read immediately; do not repeat in the same session. |
| <code>drain_timeouts=1</code>, <code>out0_after=0x8</code>, <code>out5_read=0x0</code> | firmware did **not** service the mailbox within 50 ms — still booting/parked at the 0xcece spin, or dead in the ETE/DR abort context. | Stop. Next session: same injection while the C1e service loop holds gate + D2H contract. If it still times out, the H2D lane is closed on this firmware path and boot-kick §19.4's firmware-internal conclusion stands for the mailbox too. |
| out[1]/mask/state/DR fills changed after the injection | the frame (or the announce) produced device-side work | Capture everything; next session decodes it. |
| abort/SError/hang on UART or dmesg | stop, capture, power-cycle (G1) | finding; no retries |

**Stop rules (hard):** any abort/SError/stall => stop, capture, power-cycle. One injection per
session in Session A (a second attempt after a timeout is a **new** idea;
<code>h2d_clear</code> exists only to clear a stale host-set word when the Lead explicitly
decides to retry). Never <code>rmmod</code> after release. Never write out[5]
(<code>0x400392f0</code>). Never read the RC misc window <code>0x10161000</code>.

### 7.5 Later-session frame candidates (only after Session A acked)

The device's own H2D HCC dispatch table is **not decoded** [U] — the
<code>smac_msg_proc_*</code> names exist in the image but their wire ids are not known
(firmware-re §4.3 [V]). Candidates, in increasing risk:

1. <code>group=4 id=1 dst=0 payload=00</code> — chip group 4 id 1 is the *host-side* boot
   catalogue's device-plat-ready message [V, hcc-re §2.4]; using it H2D is an experiment, not
   a contract-guaranteed-safe call.
2. <code>group=0 id=1 dst=0 ...</code> / <code>group=0 id=2 dst=0 ...</code> — the vendor's
   <code>hmac_config_send_event</code>/<code>hmac_config_alg_send_event</code> helpers target
   chip group 0 ids 1/2 (hcc-re §6.2 [V]); their payload blobs are the wal/hmac command
   formats, **not modelled here**.
3. Anything else only from a captured vendor trace (use <code>h2d_raw</code> so no rebuild is
   needed).

For every candidate: <code>h2d_check</code> first (no writes), then one injection, then stop
and record. Since the SR0 node is consumed by the firmware's id-3 handler before HCC
dispatch, the mailbox observables from Session A remain valid; the frame-level effect (if
any) shows up in out[1]/DR/mask/state.

### 7.6 Integrated variant (B3 instrument, if the session is already a B3 run)

Order: <code>insmod hi5622_pciet_b3.ko poll_ms=0</code> -> <code>prepare</code> ->
<code>upload</code> -> <code>verify</code> -> <code>b3_p0_ringinit</code> (7/7) ->
<code>h2d_arm</code> (re-programs **SR0 only** with the injector's ring; B3 never sends H2D
and does not use SR0 after P0, so this is safe — but <code>b3_rings</code> will then show the
injector's SR0 base) -> <code>h2d_check</code> -> <code>release</code>/sustained
<code>announce</code> -> start <code>b3_p1_watch N</code> in the background -> inject -> read
<code>h2d_stats</code>, <code>b3_p1_events</code>, <code>b3_p2_result</code>,
<code>b3_p3_hcc</code> after the window. **Do not** use the base
<code>poll</code>/<code>ringinit</code>/<code>send</code> attrs and do not run
<code>h2d_arm</code> before <code>prepare</code>/<code>windows=1</code>.

---

## 8. Limits and open questions

1. **Device H2D HCC dispatch table is [U].** The injector can post any validated frame, but
   which (group,id) pairs the firmware registered, and their payload formats, are not decoded.
   The first session therefore uses a deliberately out-of-range group (15) so the *mailbox*
   ack is measured without choosing a known handler.
2. **SR channel ctrl/enable are [U] and untouched.** The injector programs only the host-owned
   group-A fields via the skeleton's <code>pciet_ete_ring_init</code>; the vendor's
   <code>pcie_ete_sr_reg_init</code> ctrl write (+0x08) and the channel enable (+0x00,
   firmware-owned) are not reproduced. If the firmware acks the mailbox but never consumes the
   SR node, the next lever is the SR ctrl/enable sequence as a separate attended session (B3
   wrote SR ctrl = 0 during P0, so an integrated B3 session already has ctrl 0).
3. **Mailbox announce id is fixed to 3** (the SR announce consumer, hcc-re §3.3/§3.5 [V]).
   ids 6/7 (trigger-ETE-sending) and id 5 (BUG trap) have no path here by construction.
4. **Announce doorbell write value.** This injector follows the contract literally
   (<code>out[2] |= 1</code>). The earlier B2 H2D smoke wrote the pending-mask value (8) into
   the doorbell and passed host-side [V*]; if the device state after the first injection looks
   inconsistent, compare the two writes — but do not experiment with variants in one session.
5. **No host-side hcc/bal queue layer.** Frames bypass <code>hcc_msg_tx</code> queue
   selection, back-pressure and <code>hcc_msg_clone</code> broadcast; this is a
   transport/path debug injector, not the vendor queue stack. One frame per sysfs write,
   serialised.
6. **SR0 only** (the reference skeleton allocates SR0/DR0; the vendor has SR0-2). Reusing SR0
   is enough for control-plane probing; multi-SR distribution is a later driver-replica
   concern.
7. **D2H ack/re-arm** (<code>out[3]=1</code>, <code>out[4]=1</code>, hcc-re §5.4) is not done
   by the injector; the base module's poll thread drains out[1], the B3 instrument provides
   the full sequence.
8. **Twin word / out[5] semantics** stay untouched: out[5] is read only (device ack
   checkpoint); the twin write remains behind the skeleton's <code>twin_enabled=0</code> and
   is never exercised here.

Cross-lane notes (not changed by this lane, recorded for the Lead):

- the reference <code>pciet_ete_sr_send()</code> producer advance is linear and only safe for
  fewer than <code>depth</code> posts; the injector does not use it and does not modify it
  (B2 smoke provenance preserved);
- the B3 instrument and this injector can coexist in one module, with the SR0 re-programming
  caveat in 7.6;
- <code>out[2]</code> device-side read-back is <code>8</code> after dispatch according to the
  decode (hcc-re §3.5 [V]); <code>h2d_stats</code> prints it live, so a bench capture will
  confirm or refute that reading.

---

## 9. Evidence ledger

| claim | artifact / command | grade |
| --- | --- | --- |
| frame model compiles as kernel header and as userspace header; 63/63 cases pass on host | <code>tools/build.sh --selftest-only</code> output (<code>SUMMARY: 63 passed, 0 failed</code>) | [V] |
| builder produces the contract bytes (header fields, dst duplication, tag, lengths) | <code>h2d_selftest build ...</code> outputs quoted in §3.1; suite includes max payload | [V] |
| id 5 and over-bound payloads are refused on every path | selftest cases build/parse/raw/validate refused | [V] |
| base transport + injector module builds clean under CONFIG_WERROR=y for 6.18.60 armv7 | <code>tools/build.sh --module-only</code>; sha256 ca5cedff..., vermagic line | [V] |
| B3 instrument + injector module builds clean; both attribute families present | <code>tools/build.sh --module-only --tree b3</code>; sha256 b8a016de...; ARM nm shows b3_* + h2d_* | [V] |
| all injector MMIO writes go through the single never-write-out[5] gate | <code>objdump -dr pciet_h2d.o</code> -> 4 <code>R_ARM_CALL pciet_writel_checked</code>, no direct writel | [V] |
| patches apply with --fuzz=0 to both reference trees and change exactly 3 existing + 2 new files | <code>tools/build.sh</code> patch output (5 files) | [V] |
| contract: header layout, 0x674 bound, 0x6d2b node, out[0]/out[2] announce, device ack write to out[5], id-5 trap | hcc-re REPORT §3.1/§3.3/§3.5/§8 (decoded from plat.ko / firmware image) | [V] |
| device does/does not service the injected frame | **not measured** — bench procedure §7 | [U] |

---

## 10. Reproduction (host-only)

    cd opensource/docs/incoming/wifi-h2d-inject
    ./tools/build.sh --selftest-only            # host selftest, no kernel tree needed
    ./tools/build.sh --module-only              # base patch -> hi5622_pciet.ko
    ./tools/build.sh --module-only --tree b3    # b3 patch   -> hi5622_pciet_b3.ko
    ./tools/gen-patches.sh                      # regenerate patches/ from files/ + trees

No device contact, no image/world build, nothing committed outside this directory. Build
products land under <code>/tmp/wifi-h2d-inject-{base,b3}/</code> and are deliberately not
committed.
