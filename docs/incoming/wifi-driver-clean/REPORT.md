# REPORT - clean driver restructure: transport + service merged, corrected protocol baked in

**Lane:** driver-clean (shared task **task-94**, host-only).  **Date:** 2026-11-24.
**Inputs (all committed lanes, read in place, no build-time patch):**
wifi-transport-port/build (transport skeleton),
wifi-hcc-service/src (host service, corrected boot protocol),
wifi-d2h-direct/src (direct device group-A receive service),
wifi-hcc-core/src (portable protocol state machine, 528-check selftest),
wifi-rom-dump / wifi-s1-cpu1 / wifi-fw-postboot / wifi-irq-unmask reports.
**Nothing in this lane contacted the device.**

## 0. One-line result

The four research trees are now **one self-contained driver**, `hi5622_wifi.ko`
(GPL, 305,276 B, sha256 `ad334f7f...`, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`,
**0 compiler/modpost warnings** under the tree's -Werror), with the corrected
boot protocol and the hcc-core F1-F9 fixes baked into the source (no patch step):
release 0x5A5A only, custom-block pre-seed before release, LATCHED boot gate,
direct device group-A reader as the primary receive path, vendor-order mailbox,
coalesced H2D announce, evidence-driven post-boot CECE, real region-3 address
translation.  Device-free selftest **133/133 PASS**; the baked-in protocol core
still passes its own **528/528** selftest.

## 1. Deliverable tree

    src/
      hi5622_contract.h      hardware/protocol contract (single source of truth)
      hi5622_driver.h        shared state + transport/ETE API + CA->BAR0 helpers
      hi5622_d2h.h           direct device group-A receive service state/API
      hi5622_hcc.h           HCC service state, core adapter, module-facing API
      main.c                 PCI lifecycle, module params, sysfs group wiring
      transport.c            six iATU windows, firmware loader upload/verify,
                             custom-block pre-seed, release-only, gate repair,
                             dev_status + read-only boot discriminators
      ete.c                  host-DRAM ring mirrors + packed SR post helper
      d2h.c                  direct device group-A DR reader (primary receive)
      hcc.c                  protocol-core glue: init/tick/release/deliver,
                             H2D liveness carrier, CECE kick, DR +0x38 republish
      service.c              ETE arm sequence, engine-DR secondary path, loop,
                             probe/remove, read-only status/ready/events surface
      hcc_core.{c,h}         baked-in portable protocol core (from hcc-core lane)
      hcc_core_kernel.{c,h}  its kernel adapter (readl/writel, jiffies, dev_info)
      Kbuild / Makefile
    tools/build.sh           canonical build + artifact evidence + selftest
    tools/selftest.py        133 device-free contract/logic checks
    tools/mkblocks.py        local staging of the Wi-Fi PHY custom block
    README.md  REPORT.md  BUILD-NOTES.md  BENCH-PROCEDURE.md

Module name: `hi5622_wifi` (driver name `hi5622_wifi`; PCI alias
`pci:v000059E7d00000005sv*sd*bc*sc*i*`).  This replaces the bench modules
`hi5622_pciet` / `hi5622_pciet_hcc` (same sysfs `hcc_*`/`rd_*` names kept for
bench-procedure continuity; the unsafe legacy attrs are gone, see section 4).

## 2. Corrected protocol baked in

### 2.1 Boot: release-only + custom-block pre-seed

* `release` writes **0x5A5A to CA 0x40000108 only**; it is refused unless the
  iATU windows are programmed, the upload is verified, and both custom blocks
  are seeded (unless the diagnostic `allow_unseeded_release=1` is set).
* CA 0x4000010C is a dual-use 16-bit register: the mask ROM's boot stage owns
  **0xCEED** until CPU1's S1 completes (handshake page +8 == 2); the image's
  exception/service handler expects 32-bit **0xCECE** only after boot.  The
  driver has **no blind gate write at all**; the only CEED write is the
  explicitly guarded `gate_repair` (pre-S1, ROM-magic state, 16-bit store) from
  the ROM reconstruction, and CECE writes are owned by the core policy.
* Custom blocks, vendor order (kprobe-verified 2026-11-24 07:20), before the
  release: platform descriptor CPU 0x1B2800 = BAR0 0x86A800 (24 words / 0x60 B)
  and Wi-Fi PHY custom block CPU 0x1B2860 = BAR0 0x86A860 (618 words / 0x9A8 B),
  both read back word-for-word.
  - The platform descriptor is **built in** from the device-INI contract
    (TCM section bounds 0x117BC4/0xA968/0x12252C/0x76C, self/wifi addresses and
    sizes, boot config words), verified against the read-only stock capture of
    2026-11-24 05:00.  It is boot configuration, not PHY calibration.
  - The Wi-Fi PHY block is vendor configuration data: it is loaded through the
    kernel firmware loader from a **local** `/lib/firmware/hi_wifi/wifi_block.bin`
    (`wifi_block_path` param) and is **never committed** (publication rule:
    no vendor firmware/calibration bytes in tracked text).  `tools/mkblocks.py`
    converts the local text capture into the exact 0x9A8-byte binary.
* The boot handshake gate is **latched** (hcc-core F1): once page+8 == 2 is
  seen, a later overwrite of the page (observed with the PD patched boot) does
  not lose the gate; `page_lost_after_latch` counts it.

### 2.2 Receive: direct device group-A reader is primary

The firmware stages D2H HCC frames as 8-byte nodes in the DEVICE group-A DR
rings (PKTRAM) and the bodies in device ACP SRAM.  The ETE engine's A->B copy
is firmware-triggered and idle in the reachable boot, so the driver's primary
receive path reads both structures directly through the windows (region 2 / 4)
with the firmware record+0xC packed producer shadow as authority.  This is the
path proven end-to-end on 2026-11-24 14:00 (g4 id1 + id2 ready pair, EXCP_INFO,
OAM logs, heartbeats).  Every decoded frame is delivered into the protocol core
with `HCC_SRC_DIRECT` (F2); the engine group-B path is kept as a secondary
diagnostic path (`HCC_SRC_ENGINE`) and a fill watchdog.

### 2.3 Mailbox / H2D / CECE / gates (hcc-core F1-F9)

| fix | implemented here by |
| --- | --- |
| F1 latched boot gate | `hcc_core_boot_check` via `hi5622_hcc_note_release` after the transport release; `page_lost_after_latch` observability |
| F2 direct-read feeding | `hi5622_d2h.c` delivers each decoded frame to `hi5622_hcc_deliver(HCC_SRC_DIRECT,...)` |
| F3 CECE policy | core `hcc_core_cece_service`: never before latch; after latch only on out[1] bit2 / non-id6 handler / exception-mask change; one write per event; mode 1 = never; manual kick only mode 2, <=3/load |
| F4 CA->BAR0 | region-3 translation `BAR0 = 0x3b8000 + (CA - 0x40000000)` with range checks; out[3] CA 0x40101438 -> 0x4B9438, out[4] -> 0x4B9414; **compile-time asserts** for every mailbox/D2H/out/ETE CA and the never-write word; selftest mirrors them |
| F5 H2D coalescing | core `hcc_core_mbox_send/_tick`: pending/in-flight bitmaps, out[0] publish, out[2] RMW doorbell, bounded out[0] poll; never waits on out[2]; never writes out[5] |
| F6 SR commit | packed producer (low10 + 0x400 phase, depth 32) + node word1 `(total<<16)|0x6d2b`; one coalesced id-3 announce per batch (core allowlist) |
| F7 write allowlist | core `hcc_write()`: only out[0..4] + gate writable; out[5] and the transport release word refused and counted (`bad_writes`) |
| F8 deadlines | core: boot 5 s, plat-ready 2 s, device-ready 20 s from the latch, PM 500 ms, heartbeat 10 s; FAILED keeps servicing D2H and refuses H2D |
| F9 frame validation | core: tag 0x5a5a, total == wire length, total in [12,0x680]; H2D groups 0/2 only, group 4 device->host only, id1 total 12 / chip id 0, id2 total 13..111 |

## 3. What changed relative to the research modules

| # | change | why |
| --- | --- | --- |
| 1 | modules merged into one `hi5622_wifi.ko`; no `-DHI5622_HCC_BUILD` switches, no build-time patch | self-contained, single probe/lifecycle |
| 2 | the three legacy TSX device-write attrs removed: `announce` (blind 0xCECE), `ringinit` (2-channel plain init), `send` (zero-header SR frame), and the unordered `poll` | all incompatible with the corrected protocol / already refused in the hcc-service build |
| 3 | invalid IRQ/event-controller probe removed | GIC/event controller CA 0x40160000 is **not host-visible** through the six inbound regions; the old `HI5622_BAR0_IO` mask wrapped those reads into CA 0x4006xxxx (fw-postboot correction; hcc-core F4). The driver now states this explicitly and keeps a region-checked read helper that refuses unmapped CAs. |
| 4 | region-3 translation replaced with the true 21-bit region formula | inherited 20-bit mask mis-mapped out[3]/out[4] (F4) |
| 5 | region 0 (ROM_WRAM) BAR0 limit corrected `0x001bfff -> 0x001bffff` (size 0x1C000 -> 0x1C0000) | inherited header typo truncated the ROM window by 0x1B4000; phase18 inbound-map A.6 gives 0x1c0000 B |
| 6 | protocol state machine replaced by the baked-in, self-tested `hcc_core` | one tested state machine for boot latch/ready/mailbox/CECE/F1-F9 instead of hand-rolled fragments |
| 7 | custom-block pre-seed is a first-class gated step with a local-file policy for the Wi-Fi PHY block | GP3..GP8 contract; no vendor bytes in the tree |
| 8 | boot-stage gates + read-only discriminators | release requires prepared windows + verified upload + seeded blocks; `boot` shows page/gate/state/mask/out words; no speculative writes |
| 9 | module/param/attr surface consolidated and documented | see section 5; sysfs `hcc_*`/`rd_*` names kept |

## 4. Module parameters and sysfs surface

Module params (`/sys/module/hi5622_wifi/parameters/`):

| param | type | default | meaning |
| --- | --- | --- | --- |
| fw_path | charp | hi_wifi/FIRMWARE.bin | firmware image name relative to /lib/firmware |
| wifi_block_path | charp | hi_wifi/wifi_block.bin | local Wi-Fi PHY custom block (0x9a8 B) relative to /lib/firmware |
| plat_block_path | charp | "" | optional 0x60 B platform descriptor override (empty = built-in INI descriptor) |
| allow_unseeded_release | uint | 0 | diagnostic: allow release without the wifi-custom block (unproven configuration) |
| hcc_poll_ms | uint | 5 | service cadence |
| hcc_cece_mode | uint | 0 | CECE policy: 0 evidence-driven, 1 never, 2 + bounded manual kick (read at probe) |
| rd_ab_write | uint | 0 | default-OFF experimental device group-A consumer (+0x1C) write |

Sysfs attributes (device directory; groups wired by main.c):

* transport: `prepare upload verify seed release gate_repair` (WO),
  `status boot dev_status` (RO)
* service: `hcc_arm hcc_loop hcc_service hcc_clear` (WO),
  `hcc_status hcc_ready hcc_events hcc_rings hcc_plan` (RO)
* hcc protocol: `hcc_sr_announce hcc_cece_kick hcc_dr_republish` (WO)
* direct D2H: `rd_poll rd_advance` (WO), `rd_status rd_decode rd_catalog` (RO)

## 5. Build + verification evidence

See BUILD-NOTES.md for the full record.  Summary:

| item | value |
| --- | --- |
| kernel tree | `.../linux-hisilicon_luofu/linux-6.18.60` (6.18.60) |
| toolchain | OpenWrt GCC 14.4.0 musl eabi, `arm-openwrt-linux-muslgnueabi-` |
| module | hi5622_wifi.ko |
| size | 305,276 B |
| sha256 | ad334f7fe1600b4c1acbcfa3cd4b11314573f73ea86fe112b01f068ded71639e |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0 (compiler + modpost; CONFIG_MODULE_STRIPPED handled for module metadata) |
| driver selftest | 133/133 PASS |
| baked-in protocol core selftest | 528/528 PASS (lane tool, run from wifi-hcc-core) |

## 6. What stays experimental / unproven (honest limits)

* **No device execution in this lane.**  The next attended session is the
  first load of this exact driver; everything below is host-only verified.
* **Wi-Fi PHY custom block staging is mandatory by default.**  The GP8 golden
  boot used the two captured blocks; a release without the PHY block
  (`allow_unseeded_release=1`) is a diagnostic and **unproven** - do not use it
  for a bring-up run.  The platform descriptor is built in and read-back
  verified.
* **ETE engine A->B copy remains [U]**; the direct reader is the working
  substitute and is now the primary path.  The engine group-B walk is kept
  only as a secondary diagnostic.
* **First post-ready H2D (group-0 id-1 TXQ sync)** payload is dynamic and not
  reconstructed; the driver refuses to synthesize it.  The H2D path here is the
  bounded liveness carrier only (one frame/load, safe group-0 id-0 stub).
* **PM state machine and heartbeat are disabled by default** (vendor boot path
  leaves them off); they can be enabled through the core config only.
* **The image IRQ/thread layer** (why the firmware's H2D dispatcher is dead in
  our boot) is firmware/ROM-internal per wifi-fw-postboot; this driver only
  services the host side and does not fabricate access to the GIC.
* No cfg80211/mac80211 integration, no data path, no firmware API beyond the
  transport/service protocol: this is still a bench instrument.
* The protocol core is single-threaded by design (the service loop is the only
  caller).  The manual `rd_poll`/`hcc_service` attrs are intended for
  loop-stopped diagnostic sessions; with `hcc_loop` running they add
  telemetry races only (no device-write duplication: core decisions are
  idempotent state transitions).

## 7. Safety + publication boundary

* Encoded hard rules: CA 0x400392F0 (out[5]) is never writable (core allowlist
  + compile-time asserts + selftest); the transport release word is written
  only by the transport; the RC misc window 0x10161000 is untouched; no UART
  I/O; no device RAM writes (only the vendor-owned DR +0x38 republish behind an
  explicit gated attr and the default-OFF +0x1C experiment).
* No vendor binary/object/firmware/ROM/calibration bytes are committed in this
  directory.  The Wi-Fi PHY custom block is local evidence staged at bench
  time; the firmware image is a local /lib/firmware input.  All source here is
  original GPL-2.0 clean-room code from this repository's own lanes.
* The baked-in `hcc_core` files are byte-identical to the wifi-hcc-core lane
  deliverable (the driver selftest checks this when the lane tree is present).

## 8. Reproduction

    # module build + driver selftest (host, no device)
    cd opensource/docs/incoming/wifi-driver-clean
    ./tools/build.sh
    .provision-venv/bin/python tools/selftest.py
    .provision-venv/bin/python tools/mkblocks.py --selftest
    # baked-in protocol core selftest (its own lane tool)
    (cd ../wifi-hcc-core && ./tools/build.sh --no-kernel)

## 9. Next steps / handover

1. Attended session with this module on the golden-boot image: follow
   BENCH-PROCEDURE.md S0-S7; expect a latched golden boot and the direct-reader
   ready pair.
2. If READY: supply the real group-0 id-1 TXQ-sync payload from the
   driver-replica lane (never synthesized); then the post-ready H2D catalogue
   work applies.
3. If the ready pair still does not arrive: the next lane is firmware-side
   (event 0x4C / GIC / H2D dispatcher), matching the wifi-fw-postboot and
   wifi-irq-unmask conclusions - no host message can synthesize it.
