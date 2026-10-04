# TIMELINE AND WAY FORWARD (handoff for a new agent, 2026-10-04)

Purpose: one document that gives a fresh agent the whole project - what happened, in order, with
the evidence that matters, the environment it can use, and the concrete ways forward. Every claim
here is in a `docs/phaseNN/*.md` report or a live measurement recorded in
`notes/facts/2026-10.md` in the omo memory repo; where a claim is the opposite of an older claim,
this document says so explicitly.

The device: Cudy WR3000 v2.0. SoC HiSilicon Hi5671Y (platform `hsan luofu`, board R116, dual
Cortex-A9), Wi-Fi chip Hi5622V100 on PCIe (two endpoints), 128 MB NAND + 128 MB DDR3. Stock
firmware = OpenWrt 22.03.6 (vendor-private target `hisilicon/luofu`) + kernel 5.10.201 + ~60
binary-only modules + a closed 928,920-byte Wi-Fi firmware blob.

The goal: a custom, current OpenWrt firmware for this board, and/or enough reversal of the Wi-Fi
stack to run our own driver.

---

## 1. Timeline, phase by phase (what each stage established)

**phases 2-3 - the config API.** The vendor's `alg` command table
(`g_ast_alg_cfg_process_info_table`, 414 entries x 12 B, `{name, cfg_id, dir}`) was fully
extracted (`docs/phase2/alg-dispatch.md`, `docs/phase3/alg-commands.md`). This is the vendor's
own control API (`get_2g_power_param = 0x0dae` etc.) and remains the configuration surface a real
driver must implement.

**phases 4-6 - the firmware opened.** The Wi-Fi firmware image (928,920 B, md5
`0e530b976d5a20e87358671f1a577695`) was disassembled and symbolised
(`docs/phase4/firmware-disasm.md`, `docs/phase6/firmware-symbols.md`). The HCC message envelope
was decoded (`docs/phase5/message-decode.md`). Runtime convention: file offset + `0x40000`.

**phases 7-8 - calibration.** The per-unit radio calibration store and its buffer populator were
mapped (`docs/phase7/calibration-store.md`, `docs/phase8/cali-buffer-populator.md`). Calibration
is precious: `MANIFEST.sha256` must be verified before any restore.

**phases 9-10 - diffs and one control path.** Version diff of stock images
(`docs/phase9/version-diff.md`), and the `cca-th` handler traced end to end
(`docs/phase10/cca-th-handler.md`).

**phases 11-13 - the BAR map and calibration I/O.** `barmap` (`docs/phase11/barmap.md`), and the
`calread`/`calwrite` tools built and run against the live device (`docs/phase12/calread.md`,
`docs/phase13/calwrite.md`).

**phase 14 - ABI match.** Our CI-built module matched the vendor vermagic (`abi-match.md`).

**phases 15-16 - bring-up and takeover.** The driver/plat binary inventory and the first endpoint
takeover (`docs/phase15/bringup.md`, `docs/phase16/boot-takeover.md`,
`endpoint-init.md`).

**phase 17 - the ETE engine.** The ETE SR/DR ring engine and the message-service entry points were
mapped (`docs/phase17/ete-engine.md`).

**phase 18 - the inbound map.** The six inbound iATU viewports and the BAR0 window layout pinned
(`docs/phase18/inbound-map.md`). `PCI_COMMAND = 7` proven safe.

**phase 19 - the release.** Writing `0x00005a5a` to device CA `0x40000108` (host
`BAR0+0x3b8108`) releases the Wi-Fi CPU. The release-attempt series is here
(`docs/phase19/fw-boot.md`).

**phase 20 - the H2D accept gate is NAMED.** `docs/phase20/fw-accept.md` located the firmware's
H2D dispatcher (file `0x818a8`) and showed it is never entered by a takeover. The host half was
recovered (`msg-host-half.md`, `message-service.md`), the TX path and RX loop mapped
(`tx-path.md`, `rx-loop.md`), and the outbound host-window matched (`host-window.md`). The
endpoint never asserts INTA in a takeover.

**phase 21 - both endpoints.** `docs/phase21/both-eps.md` is the pivotal run: claiming BOTH PCIe
functions, boot 2 achieved **both halves** - the engine fetched (`SR+0x1c 0x10 -> 0x400`) AND
irq 209 fired **52,507** times - and the chip still did not answer. Also: the vendor's message
ISR is on **irq 209 (ep1)**, while ep0's line 207 takes **zero** interrupts even on a working
vendor boot. `live-binding.md` captured the vendor's live boot state with kprobes.

**phase 22 - 20 hypotheses, one boot.** `docs/phase22/h2d-accept.md` ran **20 hypotheses in one
boot** (glue arm, ETE-interrupt sweep, SR producer-commit variants) plus the CA `0x400392f0 <= 8`
synchronous arm: **no H2D accept**. `fw-hostmem.md` killed the memory hypothesis (the device reads
exactly two host-memory structures: the SR ring and the buffers its nodes point at) and ranked the
remaining hypotheses H1-H4 with confirm/kill observables.

**phase 23 - THE ROOT CAUSE, and the firmware speaks.** 17 reports. The message window base was
**one page low** (`BAR0+0x3f0000` vs `0x3f1000`), masked by an interrupt register that landed
correctly by accident (`ROOT-CAUSE-window-base.md`). With it fixed: **9/9 CPU-start signature
registers changed - the chip left ROM state** - and the firmware emitted its first mailbox word
(`firmware-speaks.md`).

**phase 24 - the host half, proven.** 23 reports. The port's dispatch, ack, clear and re-arm were
verified live; the **SR descriptor post is the proven id-6 trigger**; the handler tables were
recovered (id 2 = `device_plat_ready_msg_process`, id 3 = `host_ready_msg_process`); `plat.ko` is a
TX shim; `wifi.ko` hardcodes none of the mailbox CAs. `DR` producer commit measured not to be the
gate.

**phase 25 - the message itself, from a new instrument.** 22 reports. The breakthrough: **an
announce is only read if it is pending at release** - the firmware clears the message registers
during its own boot (`in-post-announce-consumed.md`), the first host->device write ever consumed.
A **write-atomic bound capture** of the live vendor SR ring was built (one ssh call for the index
+ all descriptors, the next for the just-published slot's buffer). From it: the port's own message
type with the single differing field `+0x06`. Both REAL vendor frames (replica A len72/id29/zero
body, replica B len48/id1/recorded body) were posted byte-exact - both silent. The announce-to-
release offset was made an explicit variable and swept - inert. The full event vocabulary (six
registered message tables, 77 named handlers) was enumerated; the wal layer fronts cfg80211. The
open-source request was written (`OPEN-SOURCE-REQUEST.md`).

**phase 26 - the firmware half, reframed.** 4 reports. The vendor firmware IS OpenWrt 22.03.6 with
a private target over **stock upstream arm_cortex-a9 musl packages**; a **24.10.5 binary runs on
the vendor 5.10.201 kernel** (md5-verified), and the **24.10 ubus/ubox IPC stack completed a round
trip** on it (`how-current-userland.md`, `ubus-ipc-on-vendor-kernel.md`). But the rootfs cannot be
simply swapped: the vendor base carries the `hi_*` app framework, the Wi-Fi calibration scripts,
and the device's own flashing path as `hi_hi_upgrade_*` ubus calls (`rootfs-swap-verdict.md`).
The library closure is the real constraint on in-place upgrades (`library-closure-constraint.md`).

**phase 27 - the reversal's host side closes.** 6 reports. H1 CONFIRMED: with `sr_announce=0` (the
mailbox completely untouched) the SR ring **alone** still elicits the full id-6 then id-2 dialogue
and 9/9 CPU-start - the whole phase-25 announce line of work was characterising a signal the
firmware does not need (`h1-ring-alone-elicits-dialogue.md`). The vendor's live ring shape was
read (ch0 only, DR depth matches SR). The DR ring was shown **free-running** (index advances
uncoupled from reception). A traffic experiment was run invalidly TWICE (a wired bridge path; a
netdev counter that never increments) and both failures were recorded as such - the instruments
lied, not the chip. Finally, with a real 5 GHz station (a laptop) the DR run was VALID
(1 MB / 1,508 packets over the air, 152 slots, 0 filled) but remains **UNMEASURED rather than
negative**: the vendor's own consumer drains the ring continuously, so a caught-up ring with
empty slots looks identical whether the device deposits or not.

**phase 28 - the firmware half settles.** The curated in-place upgrade is a measured **no-op**
(433 packages installed, **0 upgradable** within 22.03.6). Every firmware path is now
measured-closed (in-place), declined by the user (wholesale modern rootfs), or waiting on the
vendor (from-source).

**Cross-cutting deployment track (through all phases).** Custom firmware `omo-minimal-0.2` then
**0.3** was built (vendor rootfs + our layer: closes the systime RCE, adds our SSH lane, restores
opkg) and flashed to **slot B**; slot A stays stock forever. Verified three ways: the marker
`omo-minimal-0.3 stock-2.5.24-20260727-122111`, `/dev/ubiblock0_0` sha256
`55f5c5b40ca6f46f19302dcc9605c963` matching the local artifact, and the injected services. The
A/B slot switch and the flashing procedure (`ubiformat` the inactive slot, verify the volume hash,
UART as last resort) are documented and tested (`docs/FLASH-PLAN.md`).

---

## 2. The current state, compressed

**What works, measured.** The port claims EP0 (or both functions), programs the viewports and
rings, loads and verifies the firmware, releases the CPU (**9/9 signature - the chip leaves ROM
state**), posts SR descriptors the device consumes, and **elicits the firmware's full dialogue
(id 6 = `pcie_wkup_thread` = "wake the host's receive thread", then id 2)**. All of that is
independent of the mailbox announce AND of the payload content.

**What never happens.** The firmware's H2D dispatcher (file `0x818a8`, whose signature is a write
of 1 to the ack CA `0x400392f0`) is **never entered** by a takeover. The device's own sequence
stops at the pending-word stage; on a working vendor boot ep1 (irq 209) carries the interrupts and
ep0 carries zero.

**What is eliminated, by evidence (see `CUSTOM-FIRMWARE-PLAN.md` section 6 for the table):**
host registers (20 hypotheses), host-memory structures, the interrupt, the descriptor fetch, the
mailbox/announce, ring geometry, DR as a carrier (unmeasured, not negative), payload content
(14 candidates including byte-exact copies of both real vendor frames).

**What is blocked and by whom.** (1) The vendor's source - request sent 2026-10-01 to
support@cudy.com, no reply; needs the human to escalate or the vendor to answer. (2) A device-side
trace - needs JTAG (no evidence the board exposes it) or the firmware source. (3) A from-source
firmware build - needs (1).

---

## 3. The environment (what a new agent can actually use)

- **Router** `192.168.10.1` - healthy vendor stack (`W=2 I=6 OFF=0 VEND=4`), SSH as root through
  `.sshwrap/rsh.sh` (password auth; live value in gitignored `.omo-secrets/`). Calibration
  `[SUCC]` on both bands. **Do not rmmod the vendor modules; never write CA `0x400392f0`; never
  read the RC misc window `0x10161000` (a read-only devmem there panicked the box).**
- **Live instruments that work:**
  - **Read-only devmem through BAR0 in NORMAL operation** (vendor stack running) - the message
    window, the SR/DR ring registers and node buffers. Base changes per boot; read it each time
    (`0x403f2410` for SR ch0 base in one boot, per `docs/phase25/live-vendor-ring-ground-truth.md`).
  - **The detached takeover cycle** - `tools/wifidrv1-detached.sh` (module
    `opensource/lab/wifidrv1/wifidrv1.ko`, CI-built in GitHub Actions). Params:
    `hw=1 program=1 wr=1 fw=1 release=1 srpost=1 [sr_announce=0|1] msgsvc=1 pollms=25 polldur=4000`.
    Never foreground; the watchdog self-recovery is mandatory.
- **This Windows host** `192.168.10.28` ("slowdesktop") - the repo, pyenv python (capstone,
  pycryptodome, pyelftools), gcc (WinLibs), gh CLI, node/bun. **No remote on the root repo - the
  `opensource/` submodule is the published half** (github.com/ShibbityShwab/cudy-wr3000v2-open-
  firmware, branch `omo/phase22-hccaccept`).
- **The user's laptop** `192.168.10.174` ("slowlaptop") - a real 5 GHz client on `vap8`
  (SSID `Cudy-1C73-5G`, key `78424982`), signal -41 dBm. The user offered SSH access to it; it is
  the way to generate genuine station traffic.
- **Transfer discipline (learned the hard way):** `/usr/bin/scp` on the router is a symlink to
  **dropbear, which ACKs files and exits 0 without writing them**. Push over the ssh command lane
  (`cat local | ssh 'cat > remote'` - note `.sshwrap/rsh.sh` nulls stdin) with an **md5 check both
  ends**. The device has **no base64**; `openssl base64 -d -A` works. The device has **no WAN
  route** and this host is firewalled inbound, so host->device push is the only direction.
- **Validity instruments that work:** `iw dev vap8 station dump` per-station rx/tx counters
  (reflect real radio traffic). `rx_packets` on the netdev does NOT increment for received frames
  on this chip - do not use it as a traffic check.

---

## 4. Ways forward, ranked (using THIS environment, no new hardware)

> **Updated 2026-10-04, by the human's decision:** the vendor-source routes (the Jeton request and
> the Cudy escalation) are SET ASIDE - "we will finish reversing ourselves without Jeton's help".
> The active path is the self-contained reversal: the phase-32 static lanes and the live
> experiments they name. Items 1 and 7 below remain available in parallel but are no longer the
> critical path.

**1. THE JETON LEAD - same SoC, possibly same board, different brand.** A web search found the
**Jeton Tech AX3000 Core** router is built on the **HiSilicon Hi5671**. If Jeton publishes a GPL
tarball for it (many rebrands of the same HiSilicon reference design do), that tarball IS the
vendor SDK source Cudy will not provide - kernel patches for `luofu`, the `hi_*` driver set, and
possibly the Wi-Fi driver build tree. **This is the single highest-value untried lead: fetch
Jeton's download/GPL page, search for "Jeton AX3000 GPL source", and if a tarball exists, diff its
kernel against 5.10.201 and its drivers against `hi5622v100_wifi.ko`/`hi5622v100_plat.ko`
(md5s on record).** Environment-free; no device risk.

**2. The vendor's own CLI on the running router.** The rootfs carries an unexplored app
framework: `hipriv`, `hi_cfm`, `hi_appm`, `hi_mw`, `hi_ipc`, `hi_md`, plus `hsan_*` init scripts
and the `hi_hi_*` ubus services. Running `--help`/usage on each is read-only and may expose a
debug or message-test interface the project never tried. The `alg` API (phases 2-3) was found
exactly this way.

**3. The SDK documentation that names the build flow.** `chear/SWNote` describes the
`hi5671y`/`luofu`/`wrt_ax3000` vendor SDK build flow (identifiers `hisi_trunk`, `opal22`). Fetch
that documentation in full - it names the SDK components and may name where the source is
distributed or which ODM actually built the board (the ODM is the GPL obligor, not just Cudy).

**4. Can the firmware's own RAM be read in a takeover?** The firmware stores its message-context
pointer in a firmware global (file `0x172130`), and its ctx table (`ctx+0x20`/`ctx+0x34`) lives in
**device** RAM. The record says the device region "based at 0x80000000" is "proven bytes; aperture
reading inferred" - i.e. nobody has established whether/which viewport makes the firmware's RAM
host-readable. If it is, a takeover could read the live handler table and the pcie_msg ctx, which
is the "reproduce the vendor's message-context/ISR binding" path `phase22/h2d-accept.md` names as
the alternative to a trace. Static-first: trace what viewport the firmware/vendor programs for
device RAM.

**5. The laptop as a real test client.** With SSH enabled on the laptop (user offered), run
iperf3 client/server against the router for proper load tests, and use it for any experiment that
needs a second host (TFTP staging for UART sessions, etc.).

**6. UART.** `PLAN-UART.md` documents the console (ttyS0, 115200 8N1, 3.3V USB-TTL cable ~USD 8-12,
VCC NOT connected). It gives u-boot (bootdelay 1, TFTP update bootmenu) - the de-risking safety
net, not a tracing tool. Requires opening the case.

**7. The Cudy escalation (human action, ready to go).** The GPL request was sent 2026-10-01 and
unanswered. Escalations in order: a formal follow-up asking for a download link **or a written
offer valid at least three years** (GPLv2 section 3 is a requirement, not a favour); the
seller/importer (the obligation attaches to whoever distributed the binary); Software Freedom
Conservancy's compliance program (a documented unanswered request is exactly their evidence).

**8. Do NOT re-run these, they are closed:** more mailbox announces, more payload variants, more
register pokes on the H2D gate, any DR-ring sampling that cannot win the race with the vendor's
consumer.

---

## 5. The one-line answers to the three deliverable questions

| question | answer |
| --- | --- |
| upstream/target support exists? | **No.** No OpenWrt target/subtarget/profile for Hi5671; no `luofu`/`hsan` anywhere; no public Hi5622 driver. Re-verified 2026-10-03. |
| kernel/module ABI constraints? | Vendor = OpenWrt 22.03.6 userland (stock arm_cortex-a9 musl) on kernel 5.10.201, ~60 binary modules. **No stable module ABI - vendor modules load only into 5.10.201.** A 24.10 userland RUNS on the vendor kernel (measured). |
| bring-up + flashing path? | De-risked: A/B slots, one always stock, `ubiformat` the inactive slot, verify volume hash, UART last resort. Deployed build 0.3 live on slot B, verified three ways. |
