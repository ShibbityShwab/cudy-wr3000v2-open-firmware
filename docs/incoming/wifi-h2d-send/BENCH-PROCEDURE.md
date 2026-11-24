# BENCH-PROCEDURE - ordered context-create -> TXQ config sync (g0 id1, key 0x64 -> 0xd8), and the calibration alg set_* C1 ladder (task-103)

Host-only lane deliverable (wifi-h2d-send; task-98 ladder A, task-99 ladder B,
**task-102 ladder C = the CURRENT procedure**).  This is the attended ladder for
the next session: golden PD4 boot -> ready pair -> read-only verification pack
-> ONE context-create frame -> re-read the pack -> ONE TXQ sync frame ->
capture-and-stop.  One idea per fire, one boot, no retries.

**Where the lane stands (fired history, do not repeat):**

* Ladder A (task-98, 48-byte node) fired on a PD3 golden boot: the frame
  reached the device ring and the ETE engine consumed the node, but the
  firmware faulted (mask 0x12) and never consumed the mailbox announce; the
  boot stayed golden.
* Control fire (g0 id0 `stub`): consumed in 0 polls, mask 0, no exception ->
  the real post-ready H2D transport path is proven; the fault is specific to
  the g0 id1 handler path.
* Ladder B (task-99, 72-byte live-node variant) fired twice (PD3 2026-11-25
  00:20 and PD4 02:10): same fault (mask 0x12, announce not consumed), the PD4
  cave stayed intact (stub survived), and the exception record holds the
  firmware's *secondary* cascade frame, not the first data abort.
* The ctx-create lane (task-101) identified the missing precondition: the
  TXQ-sync worker CA 0x81038 does `ldr r3,[ctx+0x190]; strb r1,[r3+0x4c]`,
  and `ctx+0x190` is written ONLY by the context-create helper CA 0xFE6BC,
  reachable only from the **g0 id1 config event with u16 key = 0x0064**.  Boot
  creates module 18 with type 0 and takes the no-alloc path, so a solo TXQ
  sync always faults.  Ladder C sends the create first.
* **FIRST ORDERED LAP RESULT (type 3, module 399,816 B sha b11ffea6...):**
  ctx_create was consumed in 0 polls and wrote `ctx[0]=4` (the helper ran),
  **but `ctx+0x190` stayed 0** and the create-buffer signatures never
  appeared; the TXQ sync then faulted again (mask 0x12, exception staging
  0x00119448).  The post-create PACK also read `refcnt=0` - which is expected:
  the key-0x64 worker bumps G+0x6C8+id at entry (CA 0xBC726) and drops it at
  exit (CA 0x943CC), so the net after a fire is 0 (the older "refcnt=1"
  expectation was wrong).
* **Static type-dispatch analysis (REPORT.md sec 12, tool
  `tools/ctx_type_dispatch.py`):** the helper's `data[0]` type is NOT the
  allocation switch - **types 1, 2 and 3 all call the same
  `0xBF128(0x96, 0x327, 1, 0x3a8)` allocation** (types 1/2 run the 0xFE804
  defaults block first); types 0/4/5/6 take no-alloc tails.  Type 1 alone adds
  a second small allocation (`ctx+0x434`) and a post-fill path that can
  return 0x3e8 (which makes the worker free the buffer).  The observed
  "ctx[0] written, ctx+0x190 zero, no signatures" therefore points at the
  **allocator returning 0** (failure path, helper returns 0x67), not at a
  type-3 no-alloc branch.  The next lap fires **type 1** and adds the
  allocator/no-alloc diagnostics below, which settle the cause.

Global rules unchanged: recovery door armed first, UART **read-only** capture
to an absolute path, no blind flash writes, no `rmmod` after the release, no
CECE during boot, never write CA 0x400392F0 (out[5]), never mailbox id 5.

## Preconditions

* recovery door READY (`tools/recovery-card.sh`), bench link PASS
  (`tools/bench-link.sh`), UART read-only capture running to an absolute path;
* the **PD4-patched firmware** staged locally as
  `/lib/firmware/hi_wifi/FIRMWARE.PD4.bin`, sha256
  `cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1`
  (local evidence only - never committed);
* the Wi-Fi PHY custom block `/lib/firmware/hi_wifi/wifi_block.bin` staged
  from the local stock capture with `tools/mkblocks.py` (never committed);
* the module from THIS tree on the rootfs: `hi5622_wifi.ko`, **400,732 B**,
  sha256 `774c4f1f094d6b5565e0463d9186a21ddeea3c3485755e603a27392d1f0d4310`
  (build-path dependent - record the value printed by `./tools/build.sh` of
  the build you deploy), vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`;
* Lead decision recorded: the session fires the **ordered pair** -
  `h2d_ctx_create` then `h2d_txq_sync72` - and nothing else.

### One module load with `h2d_send_budget=2` (decision, with reasons)

**Load the module with `h2d_send_budget=2`** (the supervised multi-message
setting; it is declared before the first fire, never raised afterwards).

Why not two module loads:

1. The created context lives in **firmware RAM**.  A reboot re-runs the
   firmware and loses it, so the TXQ sync must follow the create in the same
   firmware boot - a "fresh boot between the two messages" cannot work.
2. Reloading the module on the same boot to obtain a fresh one-frame budget
   would require `rmmod` after the release, which is forbidden (project stop
   rule: no `rmmod` after the release; the firmware keeps hcc state pointing
   at the host rings).
3. The cleanest allowed path is therefore ONE load / ONE budget table of two
   frames, fired in order with only read-only observation between them.  Each
   fire keeps its own safety gate: `h2d_ctx_create` is READY-only and
   once-per-load; `h2d_txq_sync72` is once-per-load and, in the ACTIVE phase
   the create produces, is allowed only when this load sent the create
   (`ctx_created=1`).  Nothing else is enabled by the budget.

Smallest-step alternative: load with `h2d_send_budget=1` and fire only
`h2d_ctx_create`.  That answers "is the create consumed, and does it allocate
ctx+0x190", but the TXQ question then needs another session with a fresh PD4
boot and budget=2 anyway.  It does not reduce the number of boots.

## Sysfs surface used here

Each attribute lives in the endpoint device directory
`/sys/bus/pci/devices/<bdf>/` (`<bdf>` = the Hi5622V100 function):

    transport: prepare upload verify seed release gate_repair status boot dev_status
    service:   hcc_arm hcc_loop hcc_service hcc_clear hcc_status hcc_ready
               hcc_events hcc_rings hcc_plan
    hcc:       hcc_sr_announce hcc_cece_kick hcc_dr_republish
    h2d:       h2d_ctx_create (WO)  h2d_txq_sync (WO)  h2d_txq_sync72 (WO)
               h2d_cfg_cal (WO)  h2d_send (WO)  h2d_status (RO)  h2d_plan (RO)
    d2h:       rd_poll rd_advance rd_status rd_decode rd_catalog

`h2d_send` grammar now also accepts `ctx_create` (type 3) plus the type
variants `ctx_create_t1` / `ctx_create_t2`.  The WO `h2d_ctx_create` attribute
takes the **write value 1|2|3 = the create helper's data[0] type** (preferred
one-shot form; any other value is refused).  The three selectors emit the same
48-byte frame - only wire byte 24 (data[0]) differs.

The WO h2d_cfg_cal attribute (task-103) fires ONE calibration config event
(HCC group 0 / id 2, config key 0x0101, 270-byte alg_cfg blob, frame total
298 B, SR node length = declared total; field B defaults to 4 = the created
module).  Write value: N or "N fb=M" where N = 1..6 in vendor script order
(1=2g power 3502, 2=2g low power 3503, 3=5g power 3504, 4=5g low power 3505,
5=xo ppm cali 3506, 6=rssi 3507) or a literal cfg id 3502..3507; M = header
field B (0..0x12).  The h2d_send grammar token is cfg_cal [cfg=N] [fb=M].
The value area is the zero-valued structural probe (per-unit calibration
values are local-only and not committed); the expected D2H answer is one
298-byte g1 id3 config-syn frame with syn_id 0x0101, body_len 270, the
request cfg id echoed and dev_ret 0.

## Ladder C (CURRENT) - ordered create -> TXQ sync

| step | action | expected / stop rule |
| --- | --- | --- |
| C0 | `insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin h2d_send_budget=2` | probe only (no device writes); dmesg "HCC service ready"; `cat h2d_status` shows `budget=2 frames_sent=0 txq_sent=0 ctx_sent=0` and gate `ctx_create=not_armed`. STOP if the `h2d_*` attrs are absent (wrong module build) |
| C1 | `echo 1 > prepare` | six endpoint-iATU viewports, 6/6 readback OK |
| C2 | `echo 1 > upload ; echo 1 > verify` | PD4 image (same 928,920 B layout), verify diffs=0 |
| C3 | `echo 1 > seed` | platform descriptor (built-in) + wifi block (local file) written and read back; STOP on readback diff |
| C4 | `echo 1 > hcc_arm` | vendor-exact 7-channel ETE init, readback OK; NEVER write CECE here |
| C5 | `echo 1 > hcc_loop` | service thread started (core tick + direct reader); start BEFORE the release |
| C6 | `echo 1 > release` | 0x5A5A only; refused unless windows+upload+verify+seed are done |
| C7 | watch `boot hcc_ready hcc_events rd_status` | LATCHED golden page 0x86807FEF/7/2/0xABCD, state CA 0x118B60=1, mask CA 0x40008=0; ready pair g4 id1 (<=2.0 s) then g4 id2 (<=20.0 s); core phase `READY_TXQ_SYNC`. STOP if the ready pair does not arrive (different experiment) |
| C8 | **pre-create read pack** (read-only, one shot; table below) | ctx4+0x190 = 0, table entry 4 = 0x0011C458, ctx4 body = 0, G = 0x00170E28, mask = 0, staging = 0; baseline `ctx+0x187` = 0, `ctx+0x434` = 0; **pool-1 descriptor CPU 0x1722BC** recorded (see "allocator diagnostics" below). STOP if mask != 0 or the table/G values differ (the expected boot state is not there) |
| C9 | **dry-run preflight (no write)**: `cat h2d_plan ; cat h2d_status` | `h2d_plan` prints `msg=txq_sync wire_total=48 wire=000100043000010000005a5a...ffffffff`, `msg=txq_sync72 node_len=72 node_word1=0x00486d2b`, the **`msg=ctx_create wire_total=48 wire=000100043000010000005a5a0000000001000000640014000304000004000000000000000000000000000000ffffffff` with `node_len=48 node_word1=0x00306d2b`**, and the ctx_create-variants line (t1/t2 differ only in wire[24]); `h2d_status` gates show `ctx_create(t1/t2/t3)=ok/ok/ok`. **STOP if the gate is not ok** - do not fire, capture the reason, do not raise the budget |
| C10 | **fire ONE create, TYPE 1 first**: `printf 1 > h2d_ctx_create` (or `printf ctx_create_t1 > h2d_send`); note `echo $?` | write value 1 = data[0]=type 1 (the variant with the full defaults block + type-1 extra path). Success only if out[0] was consumed within the bounded window. On success dmesg shows `H2D ctx_create_t1: frame 48 B in a 48 B node committed ... CONSUMED after N polls`. **Do NOT repeat the command.** (Type 3 was already fired: consumed, ctx[0]=4, ctx+0x190 stayed 0 - do not repeat type 3.) |
| C11 | **post-create read pack** (read-only; table below + per-type table) | **ctx4+0x190 = V non-zero** (allocated), V+0x10=0x64, V+0x14=3, V+0x238=0x92A, V+0x52&0xff=0x01, ctx4 body byte0=4 / byte4=3; **type 1 additionally**: ctx+0x434 (second alloc) non-zero, ctx+0xF=0x0A; refcount G+0x6C8+4 = **0 (net, inc then dec)**; mask still 0, staging still 0/unmodified; ctx+0x187 = 0. **If V stays 0:** read the allocator diagnostics (below) and STOP - do NOT fire the TXQ; do not try another type blindly |
| C12 | **dry-run preflight for the TXQ**: `cat h2d_status ; cat h2d_plan` | `h2d_status` state: `phase=ACTIVE ... ctx_created=1`, `frames_sent=1 txq_sent=0 ctx_sent=1 ok=1`, `mbox_inflight=0x00000000 out[0]=0x00000000`, gate `txq_sync72=ok`. STOP if the gate is not ok (in-flight/pending announce is a stop, not a retry) |
| C13 | **fire ONE TXQ sync**: `printf 1 > h2d_txq_sync72`; note `echo $?` | success only if out[0] was consumed. Expected: the g0 id1 **key-0xd8** worker CA 0x81038 now finds `ctx+0x190 = V` and stores one byte; **no new exception**. **Do NOT repeat** |
| C14 | **capture (read-only)**: `cat h2d_status h2d_plan hcc_events hcc_ready hcc_status boot rd_status rd_catalog` + `dmesg | tail -60` + the UART log; then STOP | see the observables table; one boot, one ordered pair |
| C15 | only if C13/C10 faulted: read-only exception record at BAR0 **0x6F800C** (CPU 0x0004000C, 17 words r0-r12/sp/lr/PC/SPSR) and cave BAR0 0x7C0240 | record and power-cycle; never write this area, never retry |

## Ladder D (task-103, next calibration fire) - one calibration alg set_* message after the working duplex lap

Purpose: after the working ordered duplex lap (create -> TXQ sync) the firmware
accepts the vendor calibration config events.  D fires ONE of the six
alg set_* messages (h2d_cfg_cal), starting with set_rssi_param (3507)
(zero-value structural probe, field B 4) because its device handler only
copies a 40-byte RAM block - the lowest blast radius of the six.  The five
other cfg ids are selectable with the same attribute on later boots.

Bundle: PD4-patched firmware + the driver built by this lane
(hi5622_wifi.ko, see BUILD-NOTES; task-103 build: size, sha256 and vermagic
there).  One module load, h2d_send_budget=3 (create + TXQ + one calibration
frame; the budget is the only thing that changes for multi-message runs).

| step | action | expected / stop rule |
| --- | --- | --- |
| D0 | insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin h2d_send_budget=3 | probe only; dmesg "HCC service ready"; cat h2d_status shows budget=3 ... cfg_cal_sent=0 and gate cfg_cal=not_armed; STOP if the h2d_cfg_cal attribute is absent (wrong module build: record sha256sum of the .ko) |
| D1 | echo 1 > prepare | six endpoint-iATU viewports, 6/6 readback OK |
| D2 | echo 1 > upload ; echo 1 > verify | PD4 image, verify diffs=0 |
| D3 | echo 1 > seed | platform descriptor + wifi block written and read back; STOP on readback diff |
| D4 | echo 1 > hcc_arm | vendor-exact 7-channel ETE init; NEVER write CECE here |
| D5 | echo 1 > hcc_loop | service thread started BEFORE the release |
| D6 | echo 1 > release | 0x5A5A only; refused unless windows+upload+verify+seed are done |
| D7 | watch boot hcc_ready hcc_events rd_status | LATCHED golden page 0x86807FEF/7/2/0xABCD, state CA 0x118B60=1, mask CA 0x40008=0; ready pair g4 id1 then g4 id2; core phase READY_TXQ_SYNC. STOP if the ready pair does not arrive |
| D8 | printf 1 > h2d_ctx_create (one shot) | consumed in 0 polls; ctx4+0x190 non-zero per the Ladder C read pack. STOP on any mask/page change (see Ladder C C10/C11 rules) |
| D9 | printf 1 > h2d_txq_sync72 (one shot) | consumed; mask stays 0; no exception staging frame. STOP on timeout or mask != 0 |
| D10 | dry-run preflight: cat h2d_plan ; cat h2d_status | h2d_plan prints msg=cfg_cal wire_total=298 node_len=298 node_word1=0x012a6d2b header=00010004 key=0x0101 blob_len=270 cfg_id=3507 fb=4; h2d_status shows cfg_cal_sent=0, state phase=ACTIVE ... ctx_created=1, frames_sent=2, and gate cfg_cal=ok. STOP if the gate is not ok - do not fire, capture the reason |
| D11 | fire ONE calibration frame: printf 3507 > h2d_cfg_cal (equivalent: printf "cfg_cal" > h2d_send) ; note echo $? | success only if out[0] was consumed within the bounded window: dmesg "H2D cfg_cal: frame 298 B in a 298 B node committed ... CONSUMED after N polls". Do NOT repeat. The same rule applies to any other cfg id: one message per boot until the answer class is proven |
| D12 | capture (read-only): cat h2d_status hcc_events hcc_ready rd_catalog ; dmesg | tail -60 + UART log | expected NEW D2H frame: g1 id3, total 298 B (12-byte header + 286-byte payload): payload syn_id 0x0101, body_len 270, body u16 @0 = 3507, body u32 @4 (dev_ret) = 0; handshake page 0x86807FEF/7/2/0xABCD, state 1, mask CA 0x40008 = 0, staging BAR0 0x6F800C unchanged. Then STOP (one calibration message per boot for the first fire) |
| D13 | fallback probes (only if D12 shows no answer) | next FRESH boot, same D1-D9, then ONE variant: printf "3507 fb=0" > h2d_cfg_cal (field B 0) or printf "3507 fb=18" > h2d_cfg_cal (field B 0x12 = the boot-created module 18). One variable per boot; never two field-B values on one boot |

Command sequence, exactly as run on the bench host (D0-D12):

    insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin h2d_send_budget=3
    BDF=$(basename $(readlink -f /sys/bus/pci/devices/*/driver | head -1))
    D=/sys/bus/pci/devices/$BDF
    echo 1 > $D/prepare ; echo 1 > $D/upload ; echo 1 > $D/verify ; echo 1 > $D/seed
    echo 1 > $D/hcc_arm ; echo 1 > $D/hcc_loop ; echo 1 > $D/release
    sleep 1 ; cat $D/boot $D/hcc_ready $D/hcc_events
    printf 1 > $D/h2d_ctx_create
    cat $D/h2d_status
    printf 1 > $D/h2d_txq_sync72
    cat $D/h2d_plan $D/h2d_status
    printf 3507 > $D/h2d_cfg_cal ; echo "cfg_cal rc=$?"
    sleep 1 ; cat $D/h2d_status $D/hcc_events $D/rd_catalog ; dmesg | tail -60

Stop rules for Ladder D (in addition to Ladder C):

* h2d_cfg_cal refused by the gate: do not fire; the gate name says why
  (budget_exhausted -> the budget is 3 and create+TXQ already used 2).
* announce NOT consumed (-ETIMEDOUT) or mask != 0: STOP, capture out[0]/out[2]/
  out[5], SR0 producer/consumer, page/mask/state, dmesg; power-cycle before any
  new attempt.  Do not re-fire on the same boot.
* consumed but no 298-byte g1 id3 answer: the field-B object lookup likely
  failed (g0 id2 returns 0x64 with no frame).  Use the D13 fallback on a fresh
  boot; if both field-B variants fail the key path needs review.
* answer present but syn_id != 0x0101 / cfg id mismatch / dev_ret != 0:
  capture the frame hex and STOP - that is a protocol finding, not a retry.
* the 36-byte g1 id3 vap-MIB event from the working lap may also appear: it is
  a different syn_id (0x07E0) and is informational only.

## Read-only verification pack (C8 / C11)

Reads are host reads of the endpoint BAR0 (`/sys/bus/pci/devices/<bdf>/resource0`),
the same read path the bench tooling uses.  BAR0 = CPU + 0x6B8000 for firmware
RAM; BAR0 = 0x3B8000 + (CA - 0x40000000) for the IO window.  Read-only - no
write is needed for any of this.  Example helper (ran on the box):

    R0=/sys/bus/pci/devices/<bdf>/resource0
    rd32() { dd if="$R0" bs=4 skip=$(( $1 / 4 )) count=1 2>/dev/null | od -An -tx4 | tr -d ' 
'; echo "  <- $1"; }
    # pre-create (C8)
    rd32 0x821E90   # G singleton,            expect 0x00170e28
    rd32 0x829468   # module-table entry 4,   expect 0x0011c458
    rd32 0x829504   # u16 G+0x6DC common off, expect 0x0000 (16-bit read)
    rd32 0x7D45E8   # ctx4+0x190,             expect 0x00000000
    rd32 0x7D4458   # ctx4 body word 0,       expect 0x00000000
    rd32 0x6F8008   # exception mask,         expect 0x00000000
    rd32 0x6F800C   # staging frame word 0,   expect 0x00000000
    rd32 0x7C0240   # PD4 cave (integrity),   expect the PD4 stub words

After create (C11), with V = the value read back from BAR0 0x7D45E8:

    rd32 $(( 0x7D45E8 ))                          # V: must be non-zero
    rd32 $(( V + 0x6B8010 ))                      # create buffer +0x10,  expect 0x00000064
    rd32 $(( V + 0x6B8014 ))                      # create buffer +0x14,  expect 0x00000003
    rd32 $(( V + 0x6B8238 ))                      # create buffer +0x238, expect 0x0000092A
    rd32 $(( V + 0x6B8052 ))                      # low byte expect 0x01
    rd32 0x7D4458                                 # ctx4 body: low byte = 4 (user id)
    rd32 0x7D445C                                 # ctx4[4]: low byte = 3 (type)
    rd32 0x8294F4                                 # G+0x6C8+4 refcount: expect 0 (net inc+dec)
    rd32 0x6F8008                                 # mask stays 0
    rd32 0x6F800C                                 # staging stays 0/unmodified
    # allocator / no-alloc diagnostics (read-only; decisive when V == 0)
    rd32 0x7D45DC                                 # top byte = ctx+0x187: 0x2B = no-alloc tail ran; 0 = alloc-failure path
    rd32 0x7D488C                                 # ctx+0x434: type-1 second alloc pointer (nonzero only for type 1)
    rd32 0x8212BC                                 # pool-1 descriptor: low16 = max block size (need >= 0x3B0); byte2 = block count (>0)
    rd32 0x821134                                 # pool-0 descriptor (baseline comparison)
    rd32 $(( V + 0x6B80F0 ))                      # create buffer +0xF0 region (optional; type-1 extra init)

Note: V is the allocator's return; firmware-RAM pointers are CPU addresses, so
the buffer's BAR0 address is V + 0x6B8000.  If V reads in the device-CA window
(0x40xxxxxx) instead, the pointer convention differs - report it as an open
item and use the device-IO mapping for the buffer reads; do not guess.

### Allocator / no-alloc diagnostics (why V can be 0)

The task-102 follow-up analysis (REPORT.md sec 12, [`tools/ctx_type_dispatch.py`](tools/ctx_type_dispatch.py))
derived the helper's type dispatch from the pinned firmware:

| data[0] type | branch | shared 0x3a8 alloc | notes |
| ---: | --- | --- | --- |
| 1 | CA 0xFE804 defaults -> CA 0xFE7CA | **yes** | + data[5]==0 gate -> extra 0xc alloc to ctx+0x434; post-fill 0xD4048 path can return 0x3e8 (worker then frees + clears ctx+0x190) |
| 2 | CA 0xFE804 defaults -> CA 0xFE7CA | **yes** | keeps the buffer; returns 0 via CA 0xFF940 |
| 3 | CA 0xFE7CA directly | **yes** | keeps the buffer; returns 0 via CA 0xFF940 |
| 0 | CA 0xFE7C4 -> CA 0xFE7CA | no (alloc dispatch rejects) | sets ctx+0x187=0x2B |
| 4, 6 | CA 0xFE7CA | no (alloc dispatch rejects) | -> CA 0xFF940, sets ctx+0x187=0x2B |
| 5, >6 | CA 0xFE892 | no | log + return 0x3e8 |

So type is **not** the allocation switch: 1/2/3 all call
`0xBF128(0x96, 0x327, 1, 0x3a8)` and store its return at ctx+0x190.  The shared
fill (`CA 0xFE8BC`, after a successful alloc) writes the +0x10/+0x14/+0x238/
+0x52 signatures for all three.  The type-3 lap wrote ctx[0]=4 but left
ctx+0x190 = 0, so the leading explanation is **the allocator returned 0**
(helper error path CA 0xFE800 returns 0x67; worker then clears ctx+0x190,
which was already 0) - not a type-3 no-alloc branch.  Consequences:

* `ctx+0x187 == 0x2B` -> a no-alloc tail ran (types 0/4/6, or the
  types-2/3 success tail at CA 0xFF940 - but that tail is reached *after* a
  successful allocation, so it would come with ctx+0x190 != 0).
* `ctx+0x187 == 0` and ctx+0x190 == 0 -> the helper returned via the
  alloc-failure path (0x67) or never reached the fill; read the pool-1
  descriptor to see whether the pool manager is initialised:
  `[0x1722BC] & 0xffff >= 0x3B0` and `byte2 > 0` are required for the
  allocation to get past its first checks (allocator CA 0xBF128 checks
  `u16 pool[base+0x188*pool] >= size+8`).  Zeros there mean the firmware's
  memory-pool manager (initialised by the boot-init code at CA 0x47696) is not
  usable on this boot - report it; a different data[0] type cannot fix that.
* Type 1 caveat: even a successful allocation can end with ctx+0x190 cleared
  if the type-1 post-fill path returns 0x3e8.  Then ctx+0x434 (the type-1
  second allocation) is the evidence that the buffer had been allocated; read
  it before concluding "alloc failed".

## Expected observables

### C10 - the create frame

| # | observable | pass / meaning |
| --- | --- | --- |
| 1 | the C10 write returned success | frame committed and the id-3 announce was consumed within the bounded window |
| 2 | `h2d_status`: `frames_sent=1 txq_sent=0 ctx_sent=1 ok=1 timeout=0 refused=0 desync=0`, `last_out0=0x00000000`, small `max_polls`, gate line `ctx_create(t1/t2/t3)=ctx_already_created/...`, state `ctx_created=1`, `last attempt msg=ctx_create_t1` | the create went through the full carrier path |
| 3 | dmesg `H2D ctx_create_t1: frame 48 B in a 48 B node committed on SR0 slot N devva 0x... node word1 0x00306d2b producer ... -> ...; coalesced announce id3 out[0]=0x00000000 CONSUMED after N polls; core phase=ACTIVE` (type-1 name shows in the line) | exact commit evidence (record slot/devva/N) |
| 4 | `hcc_events`: `h2d{pub=1 done=1 ...}`, `h2d_complete data=0x8`, lane open | core consumed the announce |
| 5 | the core phase is now **ACTIVE** (it keys on group/id; ctx_create is also g0/id1) and hcc_events may show a `txq_sync` event - **expected artifact, not a pass signal** (see risk R1) | expected |
| 6 | the C11 pack (above) shows ctx4+0x190 non-zero + the create-buffer signatures | the firmware called the create helper and allocated the context |
| 7 | mask CA 0x40008 = 0 and staging BAR0 0x6F800C unchanged/zero | no exception was taken |
| 8 | boot page/state still golden | no boot-state damage |

### C13 - the TXQ config sync, now with its context

| # | observable | pass / meaning |
| --- | --- | --- |
| 1 | the C13 write returned success | frame committed + announce consumed |
| 2 | `h2d_status`: `frames_sent=2 txq_sent=1 ctx_sent=1 ok=2 timeout=0 refused=0 desync=0`, `last attempt msg=txq_sync72 gate=ok wire_total=48 node_len=72` | ordered pair completed with the 72-byte node |
| 3 | dmesg `H2D txq_sync72: frame 48 B in a 72 B node committed on SR0 slot N devva 0x... node word1 0x00486d2b ... CONSUMED after N polls; core phase=ACTIVE` | exact commit evidence |
| 4 | **no new exception**: mask CA 0x40008 = 0, staging BAR0 0x6F800C unchanged/zero | the key-0xd8 worker found `ctx+0x190` and did not fault (the whole point) |
| 5 | SR0 producer/consumer from `hcc_rings`: consumer advanced past the node | the ETE engine took the node |
| 6 | boot page/state still golden; out[5] read-only unchanged behaviour | no boot damage |
| 7 | any NEW D2H frame (new g4 id, g3, or a g1/g2 catalogue id) | **discovery**: capture the raw frame with `rd_decode` and stop |
| 8 | consumed with a silent firmware | valid positive result: the ordered H2D pair reached the firmware dispatch path; no response is data, not failure |

## Stop rules (all mandatory)

* **One idea per fire, one ordered pair per boot, no retry.**  A repeat needs a
  fresh boot and a Lead decision.
* C9/C12 gate not `ok`: do not fire; capture the reason; never raise the
  budget after a refusal.
* C11: create consumed but `ctx4+0x190` still 0 -> **STOP**, do not fire the
  TXQ.  Capture the C8/C11 packs, `h2d_status`, dmesg.  The create worker
  failed a parser gate (type/parent) - a payload question for the ctx-create
  lane, not something to retry.
* C11: `ctx4+0x190` non-zero but the +0x10/+0x14/+0x238/+0x52 signatures
  mismatch -> report; do not fire the TXQ.
* C10 or C13 returns `-ETIMEDOUT` / dmesg "announce NOT consumed": STOP.
  Capture out[0], out[2], out[5], `hcc_rings` (SR0 producer/consumer), boot
  page/mask/state.  Do not re-fire, do not reload to retry the same boot.
* Any data abort / SError / wedged register read in dmesg or on UART: capture
  dmesg + UART, then **power-cycle**.  Do not `rmmod` after the release.
* Any unexpected D2H burst or new message id: capture and stop (discovery).
* The golden page / state / mask change at any point: STOP and capture.

## Fallback matrix (host-side, after the session)

| symptom | most likely meaning | next host-only step |
| --- | --- | --- |
| create consumed, ctx4+0x190 stays 0, ctx+0x187 == 0 | the leading hypothesis (type 3 lap): the firmware's 0xBF128 pool allocator returned 0 - helper error path 0x67; the create frame/type is not the switch (types 1/2/3 share the same alloc) | read the pool-1 descriptor BAR0 0x8212BC; if zero, the pool manager is not usable on this boot -> report, do not try more types. If the pool looks sane, capture dmesg and [U] the allocator's runtime state |
| create consumed, ctx4+0x190 stays 0, ctx+0x187 == 0x2B | a no-alloc tail ran (not expected for a valid type 1/2/3 frame) | verify the frame's data[0] byte with `h2d_plan` and the wire hex; report the type/bytes actually emitted |
| create consumed, ctx4+0x190 stays 0, ctx+0x434 != 0 | type-1 only: the buffer WAS allocated but the type-1 post-fill path returned 0x3e8 and the worker freed it (ctx+0x190 cleared) | the context precondition is closer than the type-3 lap; report the dmesg + PACK, next step is a firmware-side look at 0xD4048, not another frame |
| create consumed, ctx4+0x190 stays 0, mask != 0 | the create path itself faulted | capture dmesg/UART + exception record; power-cycle; do not resend |
| create consumed, ctx+0x190 non-zero, signatures correct, TXQ consumed + no exception | **the ordered ladder worked**: the context precondition was the fault cause | next question is the firmware's response policy (new D2H?); continue the vendor startup order in a supervised session |
| TXQ consumed, new exception appears (mask != 0) | a different handler-stage fault after the context exists (the context is no longer the blocker) | read-only exception record at BAR0 0x6F800C; capture the PC; do not retry |
| create itself faults | a payload/config error (out-of-range module index or a parent whose byte 1 is non-zero) - the parser has no other dereference | stop; report the exception record; the frame is synthesized and may need a corrected instance |
| gate `phase_not_ready` / `not_latched` at C9 | the boot did not reach the latched ready state | capture boot/hcc_ready; the ready-pair experiment is the gate, not these frames |
| gate `budget_exhausted` at C12 | the module was loaded with the default budget=1 | the ordered ladder needs budget=2 declared at insmod; do not raise it mid-session |

## Risk flags specific to this ladder

* **R1 - g0 id1 collision.**  Both messages are group 0 / id 1, so the portable
  core's phase machine latches ACTIVE and sets its `txq_sync_submitted` flag on
  the *create* commit (it keys on group/id only).  The driver keeps the
  semantic distinction: `h2d_status` reports `ctx_sent` vs `txq_sent`,
  `txq_submitted` is derived from the real TXQ counter, and the TXQ gate allows
  ACTIVE only when this load sent the create.  A `txq_sync` core event on the
  create is expected bookkeeping, not evidence the TXQ ran.  Do not interpret
  phase=ACTIVE before C13 as "TXQ already sent".
* **R2 - one load / budget=2** (above).  Do not try two loads; do not rmmod
  after the release.
* **R3 - synthesized payload.**  The create payload is derived from the
  firmware parser (type 3 / vap 4 / user 4), not a vendor capture; the vendor
  key-0x64 sender is unproven in the analysed teardown .ko.  The C11 read pack
  is what decides whether the instance was accepted.
* **R4 - node length.**  The ladder fires the header-consistent 48-byte node
  for the create and the vendor-captured 72-byte node for the TXQ.  If the
  create is consumed with no allocation, node length is the next
  one-variable experiment (72-byte capture-style buffer), not a payload change.
* **R5 - read-pack convention.**  Use BAR0 0x6F8008 for the exception mask and
  0x6F800C for the staging frame (CPU 0x40008/0x4000C + 0x6B8000).  The
  ctx-create lane README's readback table lists these two rows with the BAR0
  column equal to the CPU value (0x00400008/0x0040000C); that is a
  documentation slip in the readback tool, not a different window.  The
  exception record and the cave row (0x7C0240) are correct.
* **R6 - no expected D2H response.**  Consumed + silent firmware is a valid
  positive result.

## History ladders A/B (fired; kept for the record - do not repeat)

* **A (task-98, 48-byte node, PD3):** fired one `h2d_txq_sync`; node reached
  the device ring (ETE consumed it) but the firmware faulted (mask 0x12) and
  the announce was not consumed; boot stayed golden.
* **Control (g0 id0 `stub`):** consumed in 0 polls, no exception, mask 0 -
  transport proven; g0 id1 fault is handler-path-specific.
* **B (task-99, 72-byte live-node variant):** fired on PD3 (00:20) and PD4
  (02:10); same fault (mask 0x12, not consumed).  PD4 proved the safe cave
  (stub survived) and that the exception record holds the image's secondary
  cascade frame; the first data abort at the g0 id1 worker is evidenced by the
  staged `0x00081039`/`0x00043DF5` words.  Full analysis:
  `REPORT-G0ID1-HANDLER.md`, `../wifi-irq-unmask/REPORT-PD4.md`.
* **The old A/B expected-observables tables are superseded by Ladder C** -
  the only change is that the create must run first.

## Read-out cheat sheet

* `h2d_status` - the H2D send path's own view: budget/frames/txq/ctx counts,
  phase/latched/lane/ctx_created, the gate verdicts (`txq_sync`,
  `txq_sync72`, `ctx_create`, `real_msg`), ok/timeout/refused/desync
  counters, last out[0], max polls, last attempt (msg/gate/wire_total/node_len).
* `h2d_plan` - read-only preflight: the canned TXQ-48 / TXQ-72 / ctx_create
  frames (wire hex, node lengths, node word1), the ctx_create gate and the TXQ
  post-plan (producer before/after, mailbox id-3 announce).  Safe to read at
  any time; it performs no write.
* `hcc_events` / `hcc_status` / `hcc_ready` - core view: phase, mailbox
  pub/done/timeouts, CECE policy, ready pair, direct-reader state.
* `boot` - handshake page + gate + state/mask + the six mailbox words
  (out[5] read-only).
* `rd_catalog` / `rd_decode` / `rd_status` - device->host frames decoded
  directly from the device group-A rings.

Recovery is physical: power-off + RESET held through the counter + TFTP
(`tools/recovery-card.sh`).  An agent cannot recover the box alone.
