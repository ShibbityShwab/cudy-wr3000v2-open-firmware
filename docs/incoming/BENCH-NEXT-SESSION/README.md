# BENCH-NEXT-SESSION - the consolidated next attended bench runbook

**Lane:** bench-runbook (shared task **task-173**). **HOST-ONLY**: this lane wrote the
runbook and never contacted the router, UART, TFTP, flash or the bench link.

**Audience:** the Lead (or the operator holding the console) plus the human who owns the
physical recovery door. The Lead fires; this document is the procedure.

**What this is:** ONE consolidated runbook for the next attended bench session. It covers
the seven remaining verifications, each with the exact commands, the expected output, the
PASS/FAIL criteria, the stop rules (golden page/mask, power-cycle on exception) and a time
estimate:

| # | phase | what it settles | status at writing |
| --- | --- | --- | --- |
| 1 | **P0** power-cycle + module deployment | fresh-chip state, session artifacts pushed, one clean boot | READY |
| 2 | **P1** RX-init fire | THE ONE OPEN BUG: the device-side RX capture (firmware RX-init message) | LANE DELIVERED (opensource e7d6cd4 + bf01da4); module sha/size PIN PENDING after the bench build |
| 3 | **P2** final image #2 install | rootfs-only flash of image #2 + post-boot receipts | READY (attended flash, never executed) |
| 4 | **P3** 5 GHz fire | one-radio/two-band AP on ch36 + external-observer beacon proof | DESIGNED, NOT FIRED (module 13366151) |
| 5 | **P4** STA-mode ladder | router as a Wi-Fi client: join + host-SME auth/assoc + CRX completion | DESIGNED, NOT FIRED (module 8c0adf93) |
| 6 | **P5** WPA3-SAE client association | a real client associates to the clean-room AP (after the RX fix) | BLOCKED ON P1 |
| 7 | **P6** stock-control re-run | only if the device-vs-ours control is needed again | ALREADY PASSED 2026-11-26; OPTIONAL |

**What this is not:** no new reverse engineering, no device contact, and **no result
claim**. A phase marked `DESIGNED, NOT FIRED` is a handoff; only an attended fire produces
evidence, and the ledger entry (not this file) is what upgrades a claim.

**Rule of precedence:** this runbook is a snapshot. `STATUS.md` (root), the newest dated
entry in `mem-entries.md`, and the per-lane deliverable it cites **win over any sentence
here** that has drifted. When a pin or a command disagrees, stop and reconcile before
firing; do not improvise at the bench.

**Repo/opensource state this runbook was written against:** the parent repo at the
"full remaining-work swarm" entry (2026-11-26 19:00) and `opensource` through the RX
lane commits (the last decoded RX commit 04c0521, plus the delivered rx-init docs
e7d6cd4/bf01da4). The 7 phases are the lanes named in the task description
(wifi-rx-init-msg, wifi-final-image-2, wifi-5ghz-bringup, wifi-sta-mode, wifi-assoc-e2e,
and the 2026-11-26 stock control); each phase links its source doc.

---

## 0. State this runbook assumes (read this before anything else)

### 0.1 What is hardware-proven (do not re-litigate it)

* **Boot ladder + firmware load**: `prepare/upload/verify/seed/hcc_arm/hcc_loop/release`
  (`0x5A5A` only) -> PD4 handshake page `0x86807FEF/7/2/0xABCD` (+8 == 2), state
  CA `0x118B60` = 1, mask CA `0x400008` = 0, staging CA `0x40000C` = 0, ready pair
  (group-4 id1 <= 2 s, id2 <= 20 s, phase `READY_TXQ_SYNC`).
* **C2**: `h2d_ctx_create` (g0 id1 key 0x0064) -> `h2d_txq_sync72` (g0 id1 0xD8), both
  consumed in 0 polls.
* **AP start**: the vendor 5-message order `vap_add -> ap_chan -> ap_ssid -> ap_cfg ->
  ap_vap_start`, hostapd `AP-ENABLED`, page/mask/staging clean.
* **WPA3-SAE AP**: a SAE-capable hostapd reached AP-ON-ENABLED with the SAE RSN/MFP
  fixture as its own build (GTK + IGTK + set_default_mgmt_key installed).
* **TX / mgmt-TX**: the corrected device-facing management-TX carrier is the raw HCC
  **group 0 / id 6** control message (12-B HCC header + 48-B device TX control + inline
  802.11 frame); hardware-accepted: **86 B posted, consumed in 0 polls**. The old
  host-internal WAL `0x01F2` carrier is superseded.
* **RF**: with the antennas attached the AP is heard by the workstation at **-32 dBm**
  (the no-antenna baseline was -63..-65 dBm). RF is no longer the blocker.
* **External observer**: the 2.4 GHz AP's WPA2 beacons were seen on air by the monitor
  capture (the 2.4 GHz on-air proof exists); the 5 GHz beacon variant is P3 below.

### 0.2 The ONE open bug: the device-side RX capture

The association blocker is **not** RF, not the client, not the host-side RX setup:

* **R0 parity (write-free)**: the device-side RX/MAC state matches the vendor stock
  capture exactly - all five device-owned live-bank words (`+0x14`, `+0x24`, `+0x28`,
  `+0x2c`, `+0x30`) DEVICE_OK and the config block 21/21. The only mismatch is
  device-owned live `+0x04` (ours 0x8000 vs stock 0).
* **R2 observe (write-free, with the laptop sending auth frames)**: the MAC **receives**
  the client's frames - status bit 10 (`rx_ppdu_desc_error`) latches, `0x182 -> 0x582` -
  but writes **NOTHING** to host memory (**frames=0**, completion ring empty,
  norm/small rptr stay 0x20), and the receive path intermittently **faults the
  firmware/DMAC** (page golden-loss, the `0x04000010` exception class).
* **All host-side RX aspects are vendor-correct or excluded**: the RX enable/register set
  (every vendor non-zero word replicated byte-for-byte), the exact ring-entry + 36-B
  device-header contract, the device-state live bank, the address transform (identity
  window - the honest negative), the WOC buffer class (tested, no change), and the
  completion-ring consumer phase (prepublish ticks bit 2 but does not make the frame
  land). CRX gating has no host enable (firmware builds the g1 id13 message itself).
* **Stock control (2026-11-26 10:00, PASS)**: booted the vendor pair-A image with its own
  AP on the same chip; the same laptop's kernel log shows `authenticated` +
  `associated` - **the device is proven fine; the fault is host/firmware-mediated**.
* **Converged cause (F1)**: the firmware's RX ring/PPDU-descriptor state is never
  (re)initialised by our minimal host message set; the vendor sends a pre-RX
  request (`smac_msg_proc_rx_ring_reset_req` / `enable_trx_req` class) inside its init
  chain that our driver never sends. **The rx-init-decode lane (task-170) delivered the
  decode + patch + fire recipe** (opensource e7d6cd4 + bf01da4,
  [`../wifi-rx-init-msg/`](../wifi-rx-init-msg/)): the candidate is the generic **g0 id1
  config event, key `0x0004`** (4-byte payload, u32 value 1), sent once through a new
  write-only `h2d_rx_init` attribute gated by `rx_init_enable=1`. **P1 is its fire.**

### 0.3 What is not yet done

* **Final image #2** (container `8ba1ad22...`, rootfs UBI `31e7a39c...`, kernel slice
  `13e81214...` unchanged) is built + verified host-only and **not flashed** - P2.
* The **5 GHz AP** and the **STA client mode** are implemented, host-built and
  host-tested, and have **never been fired** - P3, P4.
* A real **client association** (open/WPA2/WPA3) has never completed - it needs P1
  first; the client side is ready (P5).
* **Honest ceiling (unchanged):** the Wi-Fi path needs the **PD4-patched research
  firmware** (vendor mailbox dispatch is not host-programmable; the patch is
  consume-only and never ships); there is **no upstreamable Wi-Fi driver**; the vendor
  GPL source request is unanswered. The AP is a host-side AP; on-air beacon/association
  evidence beyond the 2.4 GHz beacon capture is still pending.

### 0.4 Current bench state (last recorded, 2026-11-26 19:00)

* The box runs **our 6.18.60 image on pair B** (the flashed ABI image `df6a78c3`:
  kernel slice `13e81214`, slot `1794a531`, rootfs UBI `62344e80`; native
  `iw`/`iwinfo`/regdb; hostapd deployed in the overlay).
* The last session loaded the WOC-class research module from `/tmp` and left the chip in
  the status-0x582 (bit 10 latched) state; the router is **offline and needs a power
  cycle**. The Wi-Fi chip only clears on a true power cycle - a SoC reboot does not reset
  it (BENCH-FACTS).
* `/lib/firmware/hi_wifi/` will need the two local firmware artifacts re-pushed if the
  overlay was wiped (P0 checks this; P2 wipes it by design).
* Antennas: attached (keep them attached for every RF rung; P5 needs them).

---

## 1. Hard rules and the stop contract

These are physical rules, not style. Breaking one has stranded the box.

### 1.1 Preconditions (every fire)

1. **A human at the bench.** No human = no fire. Recovery is physical.
2. **Recovery door READY**: `tools/recovery-card.sh` says READY and the TFTP
   `recovery.bin` digest matches the tested 2.5.24 image.
3. **Bench link PASS**: `tools/bench-link.sh` (router LAN `eth2` <-> host USB-Ethernet
   `enp195s0f3u1`, unmanaged, 192.168.1.88/24 TFTP + 192.168.10.44/24 vendor LAN).
4. **UART capture running READ-ONLY** to an absolute path:
   `tools/uart-capture.sh start --label <phase> --timeout <sec>` and `check`. The UART is
   read-only: **never transmit** (no `echo`/`printf` to the tty, no
   `screen`/`minicom`, no `3<>` read-write open, no U-Boot abort-key floods).
5. **Attended module load**: the module is loaded by hand in this session; nothing
   autoloads the chip.

### 1.2 Golden-state contract (check after EVERY rung, on any boot)

| item | address (CA) | golden value |
| --- | --- | --- |
| handshake page | `0x1BF000` (+0/+4/+8/+C) | `0x86807FEF / 0x00000007 / 0x00000002 / 0x0000ABCD`, and page[+8] == 2 |
| boot state | `0x118B60` | `1` |
| exception mask | `0x400008` | `0` |
| exception staging | `0x40000C` | `0` |
| H2D outstanding | out0 / out2 | `0` |

Reading it: after the module is loaded the transport exposes `boot`, `status`,
`hcc_ready`, `hcc_events`, `hcc_status`, `dp_rings`, `dp_status` and friends under
`/sys/bus/pci/devices/0000:00:00.0/` - use `cat <attr>` and the lane scripts'
`snapshot`/`--check` stages. The proven read tooling is committed; do not hand-roll raw
MMIO reads. The bench_session-generated scripts and the association router script both
carry the same golden checks.

**STOP on any deviation.** A nonzero mask / staging, an exception (`excp_announce` != 0,
vector mask set, dmesg data abort), a page change, or a `0xffffffff` raw read = **STOP,
power-cycle, capture the evidence, do not retry in the same boot.**

### 1.3 The stop / no-retry rules

* **One module load per boot.** No `rmmod` after the CPU release (the firmware keeps HCC
  state pointing at the host rings). A new test needs a new boot.
* **One AP profile per boot.** A hostapd restart after the data plane is armed is not
  part of any rung here; repeated hostapd kill/restart cycles are the stale-VAP crash
  class (an exception that needs a power cycle).
* **Never retry a failed rung in the same boot.** A STOP is analysed offline; the next
  attempt is a new attended boot with a fix.
* **Never write CA `0x400392F0`; never read the RC misc window `0x10161000`; never write
  `0xCECE` during boot; never write the active flash pair; no blind flash writes.**
* **Never run the boot ladder / hcc_arm / hcc_loop / release / dp_setup on endpoint
  `0001:00:00.0`.** It is the second host view of the same chip; a second ladder writes
  the same firmware SRAM through the other RC and re-releases a running CPU. The sysfs
  attributes existing on both devices is not permission.
* **The Wi-Fi chip needs a physical power cycle on an exception** (SoC reboot does not
  reset it).

### 1.4 Publication rules

* **No vendor binaries/firmware/ROM/calibration in tracked text.** The vendor PD4
  firmware and the `wifi_block.bin` PHY block are local-only, pushed by hand, never
  packaged and never committed. The `.ko` artifacts are local evidence (sizes/hashes
  recorded in text).
* **No secrets.** Only the project's throw-away lab identities appear
  (`test-passphrase-121`, `CHANGEME-lab-only`); replace them locally if desired, never
  paste a real credential.
* Evidence goes under `build/` (gitignored) or the lane's `evidence/` - `/tmp` is not
  evidence.

---

## 2. Session shape, order and time budget

### 2.1 Recommended order (one sitting if possible; split is normal)

```
P0  preflight + power-cycle + module deployment        ~30 min
P1  RX-INIT FIRE  (the one open bug)                   ~45-75 min   <-- the priority
    if P1 PASS: continue the same sitting with P5 on a fresh boot (the fix is the enabler)
P2  final image #2 install (attended flash)            ~45-60 min
P3  5 GHz fire (fresh boot + 5 GHz module)             ~30-45 min
P4  STA-mode ladder (B0-B6; one boot per rung)         ~60-90 min
P5  WPA3-SAE client association (after P1)             ~30-60 min
P6  stock-control re-run (only if needed)              ~25-40 min
                                                       -----------
                                          total        ~4.5-6.5 h attended
```

**The session's success criterion is P1.** Everything else is either independent of the
RX bug (P2, P3, P4, P6) or gated by it (P5). If P1 consumes the sitting, stop there and
schedule the rest; do not rush a flash at the end of a long session.

**Decision tree:**

* **P1 PASS** (`frames>0` + page golden) -> the RX bug is closed. Fill the session
  evidence, then either (a) fresh boot + P5 WPA3-SAE association if time allows, or
  (b) stop, commit/ledger, and book P5 as the next sitting. P2-P4 can follow any time.
* **P1 FAIL-unchanged** (`frames=0`, bit 10 latches, page golden-loss) -> capture
  everything, power-cycle, stop the RX line for the session, and use the remaining time
  for the independent phases P2/P3/P4 (or a diagnostic module run from the rx-* lanes -
  see sec. 11.3). The next static step for the RX lane is the vendor init/config-event
  decode and the stock H2D trace (sec. 12).
* **P1 FAIL-different** (e.g. module refuses to send the message, no bit 10, a clean
  frames=0 with the page intact) -> record the exact refusal/signature; the message
  placement/parameters are wrong, not the RX hypothesis. Hand it to the lane.
* **Anything touches the golden contract outside a planned rung** -> power-cycle, abort,
  report.

**Split guidance:** the natural split is *sitting A = P0 + P1 (+P5 if PASS)* and
*sitting B = P2 + P3 + P4*. The stock control (P6) is a separate optional sitting that
needs the recovery door and the stock pair.

### 2.2 Per-phase time estimates (measured class, not exact)

| phase | boot(s) | core work | estimate |
| --- | --- | --- | --- |
| P0 | 1 | preflight + deploy + golden read | 20-30 min |
| P1 | 1 | load + AP-first + dp + client auth + capture | 45-75 min |
| P2 | 3 (our->stock->our) | switch + provision + write + receipts | 45-60 min |
| P3 | 1-2 | regdomain + load + wiphy + AP + observer | 30-45 min |
| P4 | 3-7 | B0/B1/B2 dry + B3 live (+B4/B5/B6 optional) | 60-90 min |
| P5 | 1-2 | AP profile + client SAE attempt + snapshot | 30-60 min |
| P6 | 2 | stock boot + control attempt + switch back | 25-40 min |

### 2.3 Host artifact preparation (do this BEFORE the session; sec. 11 has the pins)

The session needs local artifacts that are **not in git**: the image container, the
modules, the PD4 firmware and the Wi-Fi PHY block. Verify/regenerate them first - a
missing pin mid-session costs the boot. The lane's pin checker:

```sh
cd opensource/docs/incoming/BENCH-NEXT-SESSION
./scripts/check-pins.sh              # report every pin: OK / MISSING / PENDING-PIN
./scripts/check-pins.sh --strict     # non-zero while any pin is missing or pending
```

Run it also once more **after** P0's push (it prints the on-box verification commands too).

---

## 3. Phase 0 - power-cycle + module deployment (all phases share this)

**Why:** the last session left the Wi-Fi chip in the status-0x582 (bit-10-latched) state.
The only reset is a true power cycle. P0 also puts the session's artifacts on the box and
proves one clean golden boot - every later phase starts from this recipe (fresh boot,
fresh module load).

### 3.1 Host preflight (read-only)

```sh
cd /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2

tools/bench-link.sh                          # must PASS
tools/recovery-card.sh                       # must be READY (test the physical door now)
tools/uart-capture.sh start --label next-session-p0 --timeout 7200
tools/uart-capture.sh check                  # live capture, absolute log path recorded

opensource/docs/incoming/BENCH-NEXT-SESSION/scripts/check-pins.sh   # host artifacts
```

The bench link and recovery card are physical prerequisites; if either is not PASS/READY,
fix it before touching power (a missing recovery door means no fire).

### 3.2 Physical power cycle (human)

1. **Power the router OFF** (mains switch/unplug), wait ~5 s, power it ON. Do **not** use
   the RESET-counter recovery path - this is a plain power cycle to clear the chip and
   boot our pair B image.
2. Watch the **read-only UART** during the boot: a successful boot shows the 6.18.60
   banner (our image). Record the log line that identifies the image (evidence).
3. Wait for the LAN and SSH on **192.168.1.1** (our image; the bench link carries the
   matching address). If the box comes up on the **stock** image (192.168.10.1,
   5.10.201 banner) instead, the pair selector flipped: boot-selector A/B is at
   `../../SELECTOR-0x21.md`; from stock the handover back to our image is
   `ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b; echo -n 0x11 >
   /sys/devices/platform/sysenv/boot_reg; sync; reboot` (proven 2026-11-26). Do not
   continue P1 until the box runs our image.

### 3.3 Post-boot state reads

```sh
tools/rsh.sh 'uname -r; cat /proc/cmdline; cat /sys/class/ubi/ubi0/mtd_num'
# expect: 6.18.60, "ubi.mtd=rootfsb ..." + the project cmdline marker, ubi0 -> mtd14

SEL=$(tools/rsh.sh 'ls -d /sys/devices/platform/*selector* 2>/dev/null | head -1' | tr -d '\r')
tools/rsh.sh "cat $SEL/current_pair; cat $SEL/raw"
# expect: b / 0x00000011 (the pair-B recorder re-records the running pair)

tools/rsh.sh 'ls /sys/bus/pci/devices/0000:00:00.0/ | head; dmesg | tail -5'
# expect: the Hi5622 endpoints enumerated; no vendor hi5622v100_* module loaded
```

### 3.4 Push the local firmware artifacts (never packaged, never committed)

The image ships **no** vendor firmware and no PHY block; they are pushed by hand and are
wiped by a rootfs flash (P2 re-pushes them). Verify/regenerate them on the host first:

```sh
# PD4-patched firmware (local, vendor-derived; NEVER committed). Rebuild if /tmp was cleaned:
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/opensource/docs/incoming/wifi-irq-unmask/tools
$PY $T/fw_pd_builder.py --build PD4 --out /tmp/hi5622-pd4
sha256sum /tmp/hi5622-pd4/FIRMWARE.PD4.bin
#   expect 928,920 B  cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1

# Wi-Fi PHY custom block (vendor config data; from a local stock capture; NEVER committed):
#   /tmp/blocks/wifi_block.bin  2,472 B  0b4ef9188fc1c9160b4b30cc20be0e704dd62412cbdb25dd51ea5ffb16145b32
#   If absent: regenerate from the local 618-word stock capture with
#   opensource/docs/incoming/wifi-driver-clean/tools/mkblocks.py --wifi-capture <file> --outdir /tmp/blocks
```

Push them (the firmware goes under `/lib/firmware/hi_wifi/`, the block is read through
`wifi_block_path=hi_wifi/wifi_block.bin`):

```sh
tools/rscp.sh /tmp/hi5622-pd4/FIRMWARE.PD4.bin root@192.168.1.1:/lib/firmware/hi_wifi/FIRMWARE.PD4.bin
tools/rscp.sh /tmp/blocks/wifi_block.bin        root@192.168.1.1:/lib/firmware/hi_wifi/wifi_block.bin
tools/rsh.sh 'sha256sum /lib/firmware/hi_wifi/FIRMWARE.PD4.bin /lib/firmware/hi_wifi/wifi_block.bin'
#   expect cfe24bfb... and 0b4ef918...
```

### 3.5 Push the session module(s)

Every phase below loads a **host-built module from `/root`** (never overwrite the image's
native `/lib/modules/6.18.60/hi5622_wifi.ko`; load the research build by path with
`insmod /root/hi5622_<phase>.ko`). Push + verify identity on the box before loading:

```sh
tools/rscp.sh /tmp/<phase-artifact>/hi5622_wifi.stripped.ko root@192.168.1.1:/root/hi5622_<phase>.ko
tools/rsh.sh 'sha256sum /root/hi5622_<phase>.ko; modinfo /root/hi5622_<phase>.ko 2>/dev/null | grep -c "^parm:"'
#   compare with the phase's pinned size/sha/param count (sec. 11) - STOP on any mismatch
```

Which module for which phase:

| phase | module | source / pin |
| --- | --- | --- |
| P1 | RX-init module | rx-init-decode (task-170), delivered; module sha/size **PIN PENDING** - sec. 4.1 |
| P2 | image #2's native module (in the flashed rootfs) | `/lib/modules/6.18.60/hi5622_wifi.ko` = `b4c076bf...` |
| P3 | 5 GHz module | wifi-5ghz-bringup, `13366151...` (sec. 6.1) |
| P4 | STA-mode module | wifi-sta-mode, `8c0adf93...` (sec. 7.1) |
| P5 | association module | image #2 native `b4c076bf...`, or the same RX-init module if it passes P1 (its base is the association tree - the lane's FIRE-RECIPE says whether it carries the association gates) |
| P6 | none (vendor image runs the vendor stack) | - |

### 3.6 The shared boot ladder (used by P1, P3, P4, P5)

Endpoint **`0000:00:00.0` ONLY**. This is the proven order; the exact gates are in the
lane scripts, the commands below are the manual equivalent for reference:

```sh
D=/sys/bus/pci/devices/0000:00:00.0

# 1) the firmware image + PHY block are read by the module at load/seed time
#    (already pushed in 3.4)
# 2) boot ladder - ONE ordered sequence, every write expects rc=0:
echo 1 > $D/prepare
echo 1 > $D/upload
echo 1 > $D/verify
echo 1 > $D/seed
echo 1 > $D/hcc_arm
echo 1 > $D/hcc_loop
echo 1 > $D/release        # 0x5A5A path only - NEVER 0xCECE here

# 3) golden receipt
cat $D/boot $D/status $D/hcc_ready $D/hcc_status
#   page 0x86807FEF/7/2/0xABCD (+8==2), state 1, mask 0, staging 0
#   ready: group-4 id1 <=2 s, id2 <=20 s, phase READY_TXQ_SYNC
```

The lane scripts wrap this with the fail-closed checks (use them when they exist for the
phase - they are the tested path):

| phase | script |
| --- | --- |
| P5 (and P1's AP/dp stages, if compatible) | `../wifi-assoc-e2e/scripts/router/assoc-router-fire.sh` (`--check`, `--load`, `--boot`, `--c2`, `--stage ap`, `--stage dp`, `--stage drain-start`, `--stage snapshot`) |
| P3 | `../wifi-5ghz-bringup/FIRE-RECIPE.md` G0-G7 (raw commands) |
| P4 | `../wifi-sta-mode/TEST-LADDER.md` H/B rungs (raw commands) |
| P1 | raw commands in sec. 4 + the lane's FIRE-RECIPE when it lands |

**If any rung deviates -> STOP** (sec. 1.3). The boot ladder is one-shot per boot: never
re-release, never re-load.

---

## 4. Phase 1 - RX-INIT FIRE (the one open bug)  [LANE DELIVERED; MODULE SHA PIN PENDING]

**Goal:** prove that sending the firmware's missing pre-RX init message makes the MAC
write received frames to host memory: **PASS = `frames>0` and the page stays golden.**
This is the one experiment that closes the Wi-Fi association chain; do it first, on the
freshest chip state.

**Source lane (DELIVERED):** rx-init-decode (task-170), opensource commits `e7d6cd4`
(decode docs) + `bf01da4` (patch/tools/fire recipe), lane path
[`../wifi-rx-init-msg/`](../wifi-rx-init-msg/):
[`REPORT.md`](../wifi-rx-init-msg/REPORT.md) (the decoded candidate + evidence),
[`FIRE-RECIPE.md`](../wifi-rx-init-msg/FIRE-RECIPE.md) (**authoritative** - parameter
names, the 3-content-hunk port note in sec. 0, the ladder and the gates),
[`patches/0001-rx-init-msg.patch`](../wifi-rx-init-msg/patches/0001-rx-init-msg.patch).
The lane cannot build the module (it needs the fired association tree); the Lead builds it
and pins the sha/size below.

### 4.1 The artifact (procedure PINNED; module sha/size PENDING after the build)

| field | value |
| --- | --- |
| base tree | the fired association tree (the `d2c608e7`-class build recipe) + the rx-init patch (3-content-hunk port, FIRE-RECIPE sec. 0: enum+name/parse, builder case, one-shot gate + WO attr) |
| patch | `../wifi-rx-init-msg/patches/0001-rx-init-msg.patch` - applies with `git apply -p1` at the src parent of the committed wifi-h2d-send tree; verified clean + patched source host-syntax-checked |
| file | `hi5622_wifi.stripped.ko` |
| size | **PIN PENDING** (built by the Lead from the fired tree) |
| sha256 | **PIN PENDING** |
| params | base 57 + `rx_init_enable` => ~58 (exact count PIN; check `modinfo ... \| grep -c '^parm:'`) |
| new param | `rx_init_enable` (uint, default 0 = fail-closed): allows ONE write to the new write-only attr `h2d_rx_init` per load |
| message | generic **g0 id1 config event**: sub-event key `0x0004`, payload len 4, data u32 `1`, fb = 4 |
| load line | `insmod /root/hi5622_rxinit.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin rx_enable=1 rx_buf_class=1 rx_refill_wm=75 h2d_send_budget=8 rx_init_enable=1` |

The message is **one-shot** and fail-closed: the lane's gates must refuse on
`no_ctx_create` / `rx_init_already_sent` / `out0_not_consumed`, and it must never be
re-sent in the same load. Record the built module's exact size/sha/param count here when
pinned (or in the session ledger).

### 4.2 The R2 protocol (fresh boot -> AP FIRST -> arm -> client auth -> capture)

Sequence (one boot, one load; AP profile = **open** so the client's auth frames are the
only variable - a SAE profile in P5 is a separate boot):

1. **P0 state**: fresh power-cycled boot (sec. 3), PD4 firmware + block pushed, module
   pushed and identity verified.
2. **Load** the module with the sec. 4.1 line. Expect `insmod rc=0` and the params echo.
3. **Boot ladder** (sec. 3.6) -> golden page, ready pair.
4. **C2**: `echo 1 > $D/h2d_ctx_create` -> read `h2d_status` (ctx created, consumed
   0 polls); `echo 1 > $D/h2d_txq_sync72` -> `h2d_status ok=2 timeout=0 refused=0`.
   (The proven wrapper is `assoc-router-fire.sh --c2`, which encodes the value; if
   this build's grammar differs the router script/the lane FIRE-RECIPE is authoritative.)
5. **AP FIRST** (the ordering is hardware-corrected - do not reorder):
   ```sh
   iw phy phy0 interface add wlan0 type __ap
   ip link set wlan0 up
   iw dev wlan0 scan freq 2412      # ONE scan = the mode-1 VAP base (vap_add+vap_start)
   hostapd -B -i wlan0 -P /tmp/hostapd-rxinit.pid /etc/hi5622/hostapd-open.proven.conf
   ```
   Expected: dmesg vendor 5-message order (`vap_add`, `ap_chan`, `ap_ssid`, `ap_cfg`,
   `ap_vap_start` - all posted, consumed 0 polls), hostapd `wlan0: AP-ENABLED`, golden
   snapshot. (If the image-2 configs are not present use the lane's open fixture; the
   open profile is the proven control.)
6. **Data plane first** (the lane's corrected order): `echo 1 > $D/dp_probe;
   echo 1 > $D/dp_setup; echo 1 > $D/h2d_txq_addr_sync`
   Expected: `DP setup: OK - rings 256/512/4095`, TXQ table hand-off.
7. **RX-INIT message** (one-shot, after the ctx/txq ladder is consumed and BEFORE the arm):
   ```sh
   printf 1 > $D/h2d_rx_init        # requires rx_init_enable=1 at load; ONE write per load
   ```
   Expected dmesg: `H2D rx_init: frame 32 B ... CONSUMED` (the lane's exact string is in
   FIRE-RECIPE). Record the line + the `h2d_status` counters.
   **Gate refusals** (`no_ctx_create` / `rx_init_already_sent` / `out0_not_consumed`) mean
   the ladder order was wrong: STOP, new boot, do not retry or raise gates.
8. **Arm the RX consumer**: `echo 1 > $D/dp_rx_arm`
   Expected: `DP rx_arm: ... status=0x00000182 ... mask=0xfffffcc1`, norm/small rptr 0x20.
   **STOP if status != 0x182** (AP-before-DP was not followed or the MAC re-initialised).
9. **Client stimulus** (workstation, sudo; one supplicant attempt = the client kernel's
   3 auth tries):
   ```sh
   cd opensource/docs/incoming/wifi-assoc-e2e
   sudo python3 scripts/assoc_e2e.py preflight
   sudo python3 scripts/assoc_e2e.py reset
   sudo python3 scripts/assoc_e2e.py scan --profile open        # RF gate: RSSI >= -70 (target -55)
   sudo python3 scripts/assoc_e2e.py assoc --profile open --timeout 45
   # repeat the single assoc attempt up to 3 times only if no CRX/mgmt activity appears
   ```
   The harness runs one dedicated `wpa_supplicant` (`-D nl80211`), classifies the result
   (`AUTH-TIMEOUT` is the expected shape if RX is still broken), and writes JSON + logs.
10. **Read-only capture, before/during/after each attempt** (the evidence that decides
    PASS/FAIL - no device writes):
    ```sh
    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/dp_rx_probe        # actions/frames/bad/chains (the PASS datum)
    cat $D/h2d_status $D/dp_plan $D/dp_status   # ladder/lane/status-word telemetry
    cat $D/boot $D/status $D/hcc_events         # page/mask/exception
    dmesg | tail -40          # 'H2D rx_init ... CONSUMED', mgmt RX g1 id13, refusals, faults
    ```
    Record: `frames=`, `bad=`, `chains=`, `actions=`, the status word, the norm/small
    rptr, the completion wptr, and the page/mask/staging pack after each attempt.
11. **Snapshot** the router evidence (use the script's `--stage snapshot` if compatible,
    else the above cats) and the client JSON/logs; power-cycle for cleanup (the chip is
    in an unknown state after any exception).

### 4.3 PASS / FAIL criteria (decisive)

| outcome | meaning | action |
| --- | --- | --- |
| **PASS**: `frames>0` in `dp_rx_probe` **and** the page stays golden:1 (no exception); ideally a CRX `g1 id13` / a mgmt-RX dmesg line | the firmware RX init was the missing link; the MAC now writes to host memory | record the `frames>0` line + per-frame fields + status; do **not** re-arm; power-cycle; the association chain is unblocked -> P5 |
| **FAIL-A**: `frames=0` + status bit 10 latches (`0x182 -> 0x582`) during the auth attempts | the message is not the fix (candidate refuted); the MAC still receives and aborts before the host write | STOP (no retry in the load), power-cycle, capture everything, hand the evidence to the lane; then either continue with independent phases or use the diagnostic modules (sec. 11.3) |
| **FAIL-B**: page golden:0 (gate 0xcece) / exception during the auth attempts | the RX path faulted again; the message did not change the fault | STOP, power-cycle, capture the vector frame + dmesg window; candidate refuted like FAIL-A |
| **FAIL-gate**: the `h2d_rx_init` write is refused (`no_ctx_create` / `rx_init_already_sent` / `out0_not_consumed`) or the frame is not consumed | ladder order / parameter problem, not the RX hypothesis | record the exact refusal; new boot (do not retry, do not raise the gates); hand to the lane |
| **UNKNOWN**: the client never sent (scan/harness failure) | not a device verdict | fix the client (see DIAGNOSIS.md) and repeat the attempt within the same boot only if no exception appeared |

**Expected dmesg/evidence shapes for PASS** (the lane's FIRE-RECIPE is authoritative):
`H2D rx_init: frame 32 B ... CONSUMED` (0 polls), then a received-frame line in
`dp_rx_probe` (actions/frames/bad/chains with frames>=1) and/or `cfg80211: mgmt RX
delivered (...)` / a CRX `g1 id13` in the catalogue; page golden:1 throughout. The exact
attribute names are the lane build's; read FIRE-RECIPE first and record the set present.

### 4.4 Stop rules specific to P1

* Any golden-contract deviation, `excp_announce`, or dmesg fault -> **STOP, power-cycle**
  (the chip keeps the exception across a SoC reboot).
* The init message is one-shot: if it was consumed, do not send it again in the same load
  (the lane's param is fail-closed and must refuse).
* Do not reload/re-insmod to retry; a retry is a new attended boot.
* Do not switch the AP profile mid-boot; if P5 is attempted, it is a fresh boot.
* UART read-only; no writes to the Wi-Fi CA windows outside the proven ladder/AP/dp
  sequence.

### 4.5 Time estimate

Boot + load + ladder ~8-12 min; AP + dp ~3-5 min; client reset/scan/one assoc attempt
~5-8 min; captures + snapshot + cleanup ~10 min; overhead for a second attempt ~10 min
(same boot only while no exception). **Total ~45-75 min.**

---

## 5. Phase 2 - FINAL IMAGE #2 install (rootfs-only flash) + post-boot receipts

**Goal:** put the host-verified final image #2 on pair B **without touching the kernel
slot**, then prove the post-boot identity and run the native (no /tmp) AP smoke. This is
the only destructive phase in the runbook; it needs the recovery door and the stock pair.

**Source docs:** [`../wifi-final-image-2/FLASH-FEASIBILITY.md`](../wifi-final-image-2/FLASH-FEASIBILITY.md)
(the exact rootfs-only sequence + the fit proof),
[`../wifi-abi-image/FLASH-RUNBOOK.md`](../wifi-abi-image/FLASH-RUNBOOK.md) (the validated
stock-assisted transport, sections 0-8),
[`../wifi-final-image-2/README.md`](../wifi-final-image-2/README.md) and
[`HASHES.txt`](../wifi-final-image-2/HASHES.txt) (the pinned identities).

### 5.1 Pin confirmation (do this before ANY write)

Image #2 is a **rootfs-only** successor: the kernel slice is **unchanged** from the
already-flashed ABI image, so only mtd14 (rootfsb) is written. Pinned identity:

| artifact | size | sha256 |
| --- | --- | --- |
| sysupgrade container | 7,188,799 B | `8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8` |
| rootfs UBI (container member `root`) | 3,801,088 B | `31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d` |
| kernel slice (unchanged; NOT written) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` |
| kernelb slot (mtd12; must already be this) | 8,650,752 B | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` |
| installed native module (image #2) | 328,788 B | `b4c076bf37099b21bcb5055f23678ffee38e86da9b3975e36d5c50025b31065e` |
| installed hostapd (SAE, image #2) | 1,032,951 B | `451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469` |

Local artifacts (not in git): `/home/shibbityshwab/wifi-final-image-2-artifacts/`
(`final-image-2-sysupgrade.bin`, `rootfs.ubi`, `kernel-slice.bin`, `SHA256SUMS`).

> **Do not substitute the other self-contained bake.** `wifi-image-integration`'s
> `ae5e28bf...` image (rootfs `ff9f3e1c...`, hostapd-mini, combined dp-air/AP-start v7
> driver) is a **different lineage**: its hostapd is SAE-less and its driver is not the
> association module. If the Lead wants that image instead, re-pin this section from
> [`../wifi-image-integration/DEPLOY.md`](../wifi-image-integration/DEPLOY.md) before any
> write - never mix the two.

### 5.2 Preconditions (all mandatory)

1. Human at the bench; **recovery card READY**; UART read-only capture running; bench link
   PASS.
2. **Stock must be the active pair** for this transport (the stock-telnet path writes
   pair b from the stock pair-A shell). P2 therefore starts by switching to stock A.
3. **Overlay loss accepted**: the rootfs write wipes `rootfs_data` (SSH host keys, uci,
   `/root` scripts, the pushed firmware/block). Re-provision after the boot (5.8).
4. **Rollback artefacts saved**: stock pair A is never written; the previous pair-B
   rootfs (ABI image container member `root`, sha `62344e80...`) is the rollback UBI;
   physical RESET+TFTP is the last resort.
5. Record the flash-contract witnesses read-only **before** the switch (running pair,
   selector, kernelb slot hash): `tools/rsh.sh 'dd if=/dev/mtd12 bs=131072 count=66
   2>/dev/null | sha256sum'` must print `1794a531...` already; if not, STOP - this image
   assumes the validated kernel slot.

### 5.3 Host preparation (verify, extract, serve)

```sh
cd /home/shibbityshwab/wifi-final-image-2-artifacts
sha256sum -c SHA256SUMS
sha256sum final-image-2-sysupgrade.bin rootfs.ubi kernel-slice.bin
#   expect 8ba1ad22... / 31e7a39c... / 13e81214...

# the rootfs member is already extracted in that directory; if re-extracting:
#   tar -xOf final-image-2-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root > rootfs.ubi

mkdir -p ~/stage-image2 && cp rootfs.ubi ~/stage-image2/
cd ~/stage-image2 && python3 -m http.server 8000 --bind 192.168.10.44 >/tmp/image2-http.log 2>&1 &
sleep 1; curl -s -o /dev/null -w '%{http_code}' http://192.168.10.44:8000/rootfs.ubi; echo
```

Keep the HTTP server up until the stock shell has fetched the UBI (then stop it). The
host address is the vendor-LAN address on the bench adapter (192.168.10.44).

### 5.4 Switch the box to stock pair A and get a stock root shell

From our running pair-B image, request pair A. Two documented ways:

* **Preferred - boot-selector write**: `echo a > /sys/devices/platform/boot-selector/next_pair`
  (read it back), then `sync; reboot`. On the 2026-11-26 session this sysfs write was
  permission-denied as root and a **plain reboot** came up on stock pair A (the loader's
  documented failover path), so both are listed.
* **Fallback - plain reboot from our image**, then confirm the booted image on the UART
  (stock 5.10.201 banner + `Uncompressing Linux`; our image 6.18.60 banner + the GP1
  endpoint line).

Confirm stock identity + LAN before provisioning:

```sh
ping -c 2 192.168.10.1                 # stock LAN up
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'id; uname -r; cat /proc/cmdline'
#   expect: uid=0(root) ... 5.10.201 ... 'ubi.mtd=rootfsa'
```

If the telnet is refused, the stock overlay lost the provisioning payload (pair A was
reverted or reformatted). Re-provision the stock root channel (the hardware-validated
no-SSH path):

```sh
tools/box-provision.sh --bdinfo dumps/router-20261108/full/mtd6-bdinfo.bin --stage base --host 192.168.10.1 --telnetd
# then re-check with tools/telnet-exec.py as above
```

(Alternative documented channel: the crafted encrypted config-backup via LuCI -
[`../cudy-backup-root/REPORT.md`](../cudy-backup-root/REPORT.md) and root
[`../../../../RUNBOOK.md`](../../../../RUNBOOK.md) section 6. Use only if box-provision's
MQTT channel is unavailable; both are equally privileged.)

### 5.5 Stock-side read-only checks (STOP if any differs)

```sh
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'cat /proc/cmdline'
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'cat /sys/class/ubi/ubi*/mtd_num 2>/dev/null; cat /sys/devices/platform/sysenv/boot_reg; fw_printenv bootflag 2>/dev/null'
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'
#   expect: ubi.mtd=rootfsa | ubi on mtd13 | cell 0x10 | bootflag=a | kernelb 1794a531...
```

Any deviation: fix the pair to a consistent A first (two-stage pair rule,
[`../../../../RUNBOOK.md`](../../../../RUNBOOK.md)) - do not write from an inconsistent
pair.

### 5.6 The rootfs-only write (from the stock pair-A root shell)

This is the exact [FLASH-FEASIBILITY](../wifi-final-image-2/FLASH-FEASIBILITY.md) section 4
sequence, re-derived for image #2. It never touches kernelb and never touches pair A.

```sh
# 1) record the kernel slot we must NOT change (expect 1794a531...)
dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum

# 2) fetch + verify the image-2 rootfs over the bench HTTP server
cd /tmp && wget -q -O /tmp/fi2-r.ubi http://192.168.10.44:8000/rootfs.ubi
sha256sum /tmp/fi2-r.ubi            # MUST be 31e7a39c2d7b022ee16b850a72508c2...

# 3) write ROOTFSB ONLY (mtd14); pair A / mtd13 is untouched
ubiformat /dev/mtd14 -y -f /tmp/fi2-r.ubi ; echo UBI=$?      # expect UBI=0

# 4) confirm the kernel slot is still the validated slice
dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # still 1794a531...

# 5) record the pair-A kernel slot for the rollback witness (unchanged)
dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum

# 6) handover env + cell for the SAME pair, no window between them
ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
fw_printenv bootflag                 # bootflag=b
echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
sync; reboot
```

Watch the read-only UART through the reboot: expect the 6.18.60 banner (our kernel slot
was never rewritten, so the validated kernel + new rootfs is the only change).

**STOP rules for the write (do not retry a failed write):**

* rootfs sha mismatch on the box -> STOP, re-serve/re-fetch; never ubiformat a bad file.
* `ubiformat` refuses/fails -> STOP (usually means the rootfs being written is the active
  one - re-check 5.5). Pair A still boots.
* kernel slot read-back changes -> STOP, recovery card (this must never happen).
* after the reboot the box does not answer within ~240 s: the loader should fall back to
  pair A; wait 2 min, then a plain power-cycle (no RESET hold) retries the loader path.
  Still silent -> physical RESET-hold + TFTP recovery (recovery card is armed).

### 5.7 Post-boot receipts (the point of the flash)

```sh
tools/rsh.sh 'uname -r; cat /proc/cmdline; cat /sys/class/ubi/ubi0/mtd_num'
#   6.18.60 | ubi.mtd=rootfsb + project marker | ubi0 -> mtd14

tools/rsh.sh 'dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum'
#   1794a531... (kernel slot untouched and still the validated slice)

tools/rsh.sh 'apk --repositories-file /dev/null --no-network query --fields version kernel'
#   6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1   (the kmod ABI pin)

tools/rsh.sh 'sha256sum /lib/modules/6.18.60/hi5622_wifi.ko /usr/sbin/hostapd'
#   b4c076bf...  (installed association+watermark module)
#   451dd49f...  (SAE-capable hostapd)

tools/rsh.sh 'ls /etc/hi5622/; head -3 /etc/config/hi5622'
#   the six WPA2/WPA3 fixtures + the uci policy; autoload=0

tools/rsh.sh 'cat /sys/devices/platform/boot-selector/current_pair; cat /sys/devices/platform/boot-selector/raw'
#   b / 0x00000011   (the image re-records the running pair)
```

If the kernel package pin is not `6f995278`, the rootfs written is not the verified
image-2 rootfs - STOP and investigate (the kernel slot is still the validated one; only
the rootfs identity is in question).

### 5.8 Re-push the local firmware + block, then the native AP smoke (no /tmp deployment)

The flash wiped the overlay; the vendor-derived firmware + PHY block are pushed by hand:

```sh
tools/rsh.sh 'mkdir -p /lib/firmware/hi_wifi'
tools/rscp.sh /tmp/hi5622-pd4/FIRMWARE.PD4.bin root@192.168.1.1:/lib/firmware/hi_wifi/FIRMWARE.PD4.bin
tools/rscp.sh /tmp/blocks/wifi_block.bin        root@192.168.1.1:/lib/firmware/hi_wifi/wifi_block.bin
tools/rsh.sh 'sha256sum /lib/firmware/hi_wifi/*'
#   cfe24bfb... and 0b4ef918...
```

Image #2 ships its own loader: `/usr/sbin/hi5622-load` (same as
`/etc/init.d/hi5622-wifi start`), which loads cfg80211 + the native module with explicit
insmod args, verifies the params, runs the C2 ladder and optionally starts hostapd. One
shot per boot:

```sh
tools/rsh.sh 'uci set hi5622.ap.enabled=1; uci set hi5622.ap.config=/etc/hi5622/hostapd-open.proven.conf; uci commit hi5622'
tools/rsh.sh '/usr/sbin/hi5622-load'
#   expect: module parameters verified; wiphy: phyN; C2 ladder OK (ok=2 timeout=0 refused=0);
#           AP up: wlan0 type AP (hostapd AP-ENABLED)
tools/rsh.sh 'iw dev wlan0 info; cat /sys/bus/pci/devices/0000:00:00.0/boot; dmesg | tail -20'
#   page 0x86807FEF/7/2/0xABCD, mask 0; no exception
```

**This combines the AP start with the module load** - do not run the loader twice in the
same boot (the C2 ladder is per-load; a second invocation would re-send it). For the
association phase (P5) use the same loader with the association uci gates + the WPA3
fixture on a **fresh boot**, not a second run.

### 5.9 Rollback (in order of preference)

1. **Stock pair A is untouched** and boots: from our image `echo a >
   /sys/devices/platform/boot-selector/next_pair; sync; reboot` (or the plain-reboot
   failover observed at the bench).
2. **Our image boots but the rootfs is wrong**: boot stock A, re-serve the rollback UBI
   (ABI rootfs `62344e80...`) or image #2 again, `ubiformat /dev/mtd14`, hand back to b.
3. **Boot failure with no fallback**: physical RESET-hold + TFTP recovery (last resort;
   costs the stock restore on the affected pair only).

### 5.10 Time estimate

Host verification + staging ~10 min; pair switch + stock boot ~5 min; stock provisioning
(if needed) ~5-10 min; write + reboot ~8-10 min; receipts + re-push + AP smoke ~15-20 min.
**Total ~45-60 min.**

---

## 6. Phase 3 - 5 GHz fire (one radio, two bands, ch36 AP + observer)

**Goal:** prove the single-wiphy dual-band profile: one wiphy carrying 2.4 GHz + the
vendor 29-channel 5 GHz table, and a 5 GHz AP-ENABLED + **beacon on air at 5180 MHz**.
The lane refuted the old "endpoint 0001 needs its own ladder" diagnosis; the 5 GHz wiphy
is the **primary endpoint's** wiphy with a dual-band profile, and the secondary host view
gets no wiphy. **Never run a second ladder on 0001.**

**Source doc:** [`../wifi-5ghz-bringup/FIRE-RECIPE.md`](../wifi-5ghz-bringup/FIRE-RECIPE.md)
(G0-G7, authoritative) and [`README.md`](../wifi-5ghz-bringup/README.md).

### 6.1 Artifact acquisition (host; local evidence only)

```sh
cd opensource/docs/incoming/wifi-5ghz-bringup
./tools/build-5ghz-ready.sh
sha256sum /tmp/wifi-5ghz-ready-artifact/hi5622_wifi.ko
#   expect 351,008 B  sha256 1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53
#          57 params (56 association + band5g)
tools/rscp.sh /tmp/wifi-5ghz-ready-artifact/hi5622_wifi.ko root@192.168.1.1:/root/hi5622_5ghz.ko
tools/rsh.sh 'sha256sum /root/hi5622_5ghz.ko; modinfo /root/hi5622_5ghz.ko | grep -c "^parm:"'
```

### 6.2 G0 - preconditions (read-only)

```sh
tools/rsh.sh 'uname -r; ls /lib/firmware/regulatory.db; ls /lib/firmware/hi_wifi/'
#   6.18.60 | regdb present | FIRMWARE.PD4.bin + wifi_block.bin (sec. 3.4)
tools/rsh.sh 'iw reg get'          # world "00": 5 GHz is NO_IR
tools/rsh.sh 'iw reg set US'       # any country allowing 36-48 IR
tools/rsh.sh 'iw reg get'          # must show a US/country rule set
# If iw reg set fails: record it and STOP the 5 GHz AP rung (cfg80211 would refuse NO_IR).
```

### 6.3 G1 - load the band5g module (one load, no retry)

```sh
insmod /lib/modules/6.18.60/cfg80211.ko          # if not already loaded
insmod /root/hi5622_5ghz.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 vap_start_module=0 hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 band5g=1
cat /sys/module/hi5622_wifi/parameters/band5g     # -> 1
```

Expected dmesg at load: both endpoints probed; endpoint 0000 "wiphy allocated ...
(2 band(s): 2.4GHz + 5GHz ...)"; endpoint 0001 "SECONDARY host view ... no wiphy
allocated here"; the band5g profile line ("29 channels ... HT cap 0x0062 ... VHT
0x00400064"). **STOP if the "SECONDARY host view" line is absent** - wrong module build.

### 6.4 G2 - boot ladder on endpoint 0000 ONLY

```sh
D=/sys/bus/pci/devices/0000:00:00.0
echo 1 > $D/prepare; echo 1 > $D/upload; echo 1 > $D/verify; echo 1 > $D/seed
echo 1 > $D/hcc_arm; echo 1 > $D/hcc_loop; echo 1 > $D/release
cat $D/boot $D/hcc_ready
#   page 0x86807FEF/7/2/0xABCD; ready pair g4 id1 + id2; phase READY_TXQ_SYNC
```

**DO NOT** write any of these to `0001:00:00.0` (second host view of the same chip; a
second ladder re-writes firmware SRAM and re-releases a running CPU).

### 6.5 G3 - wiphy assertions (read-only)

```sh
ls /sys/class/ieee80211/                     # exactly phy0 (one radio, both bands)
iw phy phy0 info | sed -n '/Band /,/^$/p'
cat /sys/bus/pci/devices/0000:00:00.0/cfg80211_status
#   bands=2 band5g=1; one wiphy lists Band 1 (2.4 GHz ch1..13) and Band 2
#   (5 GHz: 36..64, 100..144, 149..165, 184..196), HT 20/40 0x0062, VHT 0x00400064
```

If `iw` shows phy0 as 2.4-only: wrong module or `band5g=0` -> STOP.

### 6.6 G4 - C2 + the AP/beacon data-plane bring-up

```sh
echo 3 > $D/h2d_ctx_create; echo 1 > $D/h2d_txq_sync72
cat $D/h2d_status                     # phase=ACTIVE ok=2 timeout=0
echo 1 > $D/dp_probe                  # read-only MAC word read
echo 1 > $D/dp_setup                  # rings + MAC program (read-back)
echo 1 > $D/h2d_txq_addr_sync         # if the module/recipe includes it
```

### 6.7 G5 - the 5 GHz open AP on phy0, channel 36

```sh
iw phy phy0 interface add wlan5 type __ap
ip link set wlan5 up
# host: tools/rscp.sh ../wifi-5ghz-bringup/ladders/hostapd-5ghz-open.conf \
#         root@192.168.1.1:/tmp/hostapd-5ghz-open.conf
/usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-open.conf 2>&1 | tee /tmp/hostapd-5g.log
# (the lane config: hw_mode=a, channel=36, 20 MHz, OPEN, beacon_int 100,
#  country_code=US; substitute the interface name if the image names it differently)
```

PASS (all of):
* dmesg vendor order with band=1: `vap_add -> ap_chan (band=1 channel=36) -> ap_ssid ->
  ap_cfg -> ap_vap_start`; 2.4 GHz frames are NOT part of this run.
* hostapd `wlan5: AP-ENABLED`, SSID `cudy-ap-test-5g`, type AP.
* page golden, state 1, mask 0 after the start; no firmware exception line.
* known refusals to record (not to work around): start_ap refused "band 1 ... needs
  band5g=1" (wrong module/param), "Could not determine operating frequency" (no 5 GHz
  band = G3 failed), channel disabled/-EPERM (regdomain, G0).

### 6.8 G6 - external observer on-air proof (decisive)

On the observer host (same workstation; antennas attached), with the AP still running:

```sh
sudo iw dev <OBS_IFACE> scan freq 5180 | tee /tmp/scan-5g.txt
# or the lane harness: sudo opensource/docs/incoming/wifi-onair-observer/tools/scan-ap.sh --freq 5180
# optional definitive beacon capture (monitor): .../tools/capture-beacon.sh
```

PASS: the BSS appears on **5180 MHz (ch 36)** with SSID `cudy-ap-test-5g`, DS parameter
set 36, beacon interval 100, privacy=false for the open rung, strong RSSI at the bench;
beacon frames (subtype 8) at ~100 TU in the monitor path; no 2.4-only BSS on 5180.

**Expected-FAIL / out of scope:** client association/4-way on 5 GHz (the mgmt/data path is
P5's business; this rung is beacon-on-air + AP-start, exactly the 2.4 GHz proof scope).

### 6.9 G7 - optional 5 GHz WPA2 GTK rung (fresh boot/load, not a hostapd restart)

Choose the WPA2 fixture at G5 instead of restarting hostapd (the stale-VAP latch makes a
second AP start on one load unsafe): `/usr/sbin/hostapd -dd -t
<...>/hostapd-5ghz-wpa2.conf` (passphrase from the local fixture, never committed).
PASS: AP-ENABLED + dmesg key_add GTK (g0 id1 0x0078, 96 B, idx=1, cipher 0xfac04) +
set_default_key (0x0079, 68 B, idx=1 multi=1) consumed 0 polls.

### 6.10 Stop rules / time

Any exception mask, page change, or a 0xffffffff read -> **power-cycle**; keep
`/tmp/hostapd-5g.log` + the UART capture; no retry in place. Never rmmod. Never touch the
regulatory CA or the RC misc window. **Estimate: 30-45 min** (1-2 boots: G0-G6 on one,
G7 on a second).

---

## 7. Phase 4 - STA-mode ladder (the router as a Wi-Fi client)

**Goal:** the router joins an external AP as a station: host-SME auth/assoc requests on the
corrected carrier, CRX auth/assoc responses consumed, `cfg80211_connect_done()`; then
EAPOL/data on the inherited dp path, disconnect, and the negative controls.

**Source doc:** [`../wifi-sta-mode/TEST-LADDER.md`](../wifi-sta-mode/TEST-LADDER.md)
(authoritative) and [`README.md`](../wifi-sta-mode/README.md).

### 7.1 Artifact + host rungs (H0-H3, no device)

```sh
cd opensource/docs/incoming/wifi-sta-mode
./tools/build-sta-mode-module.sh
sha256sum /tmp/wifi-sta-mode-artifact/hi5622_wifi.ko
#   expect 364,572 B  sha256 8c0adf93995fd5014b73107de7fed77c825e3eee88fec8cc7a1c284f501c2059
#          58 params (56 inherited + sta_connect + sta_connect_timeout_ms)
tools/rscp.sh /tmp/wifi-sta-mode-artifact/hi5622_wifi.ko root@192.168.1.1:/root/hi5622_sta.ko
tools/rsh.sh 'sha256sum /root/hi5622_sta.ko; modinfo /root/hi5622_sta.ko | grep -c "^parm:"'
```

H-rungs before the fire (reproduce on the host): H0 selftest `checked 140, failed 0`;
H1 build 0 warnings / 58 params / strip-identical; H2 patch dry-run applies on the
association tree + sibling carrier + sta-join + this patch; H3 `git ls-files` shows text
only.

### 7.2 Topology and RF preconditions

The router is the **client**; the AP side must be controlled:

| option | how | notes |
| --- | --- | --- |
| A (preferred) | the workstation's Wi-Fi as a software AP (hostapd, open then WPA2-PSK) on ch6 | fully controlled; PCAP/observer available |
| B | a second router / dedicated test AP | record BSSID/frequency/security |
| C | phone hotspot | last resort (power save/scan variability) |

Attach the antennas (RF was the old blocker); keep the AP and the router <= 2 m; verify the
AP is visible from a normal client first. Record for every rung: BSSID, channel, SSID,
AP security, module sha256, dmesg, HCC events, page/mask/staging, client/observer capture.

### 7.3 Common boot (every rung starts fresh; one connect per load)

Every B-rung starts from a **fresh power-cycled boot** and a **fresh module load** - the
connect gates are one-shot (`0444` once a connect has been attempted). Load with the
rung's gates (the reference load from the TEST-LADDER):

```sh
insmod /root/hi5622_sta.ko fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 hcc_poll_ms=5 hcc_trace=1 rx_enable=1 sta_join=0 sta_connect=0 sta_connect_timeout_ms=4000 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 mgmt_tx=1 mgmt_rx=1 sta_ops=0 eapol_rx=0 data_tx=1 data_tx_peer=1 data_tx_protect=0 data_tx_timeout_ms=200
```

then the shared boot ladder (sec. 3.6) + C2, iface `wlan0 type managed`, `ip link set
wlan0 up`. STOP rules (hard): page golden `0x86807FEF/7/2/0xABCD`, state 1, mask
`0x400008` 0, staging `0x40000C` 0; any deviation/exception -> power-cycle, no retry.

### 7.4 The B-rungs (each on a fresh boot; one connect per load)

| rung | setup | command | PASS evidence |
| --- | --- | --- | --- |
| **B0** | load with `sta_join=0 sta_connect=0` + ladder + C2 | - | golden page/state; ready pair; wiphy registered; iface add works; **no new posts** |
| **B1** | STA iface | `iw phy phyX interface add wlan0 type managed; ip link set wlan0 up` | wlan0 UP/LOWER_UP, `iw dev wlan0 info` type managed |
| **B2** (dry) | fresh boot, load `sta_join=1 sta_connect=1`, ladder+C2, iface | `iw dev wlan0 connect <SSID> freq 2412 bssid <AP MAC>` | dmesg join frame built+validated, `sta mode DRY: auth request 30 B and assoc request ...`; **nothing posted**; page/mask clean |
| **B3** (live mgmt) | fresh boot, load `sta_join=2 sta_connect=2 allow_frames>=8`, ladder+C2, iface | same connect to the open AP | join post consumed; auth request posted; CRX auth response line; assoc request posted; CRX assoc response; `cfg80211_connect_done(status=0)`; `iw dev wlan0 link` Connected; AP-side observer shows the STA; page/mask clean |
| **B4** (data) | after B3: ping AP/gateway; then an optional WPA2-PSK connect | `ping`, or wpa_supplicant WPA2-PSK | data RX ring completions in `dp_rx_drain` (EAPOL/ARP/ping), TX completions advance; WPA2: 4-way complete (EAPOL msg2/4 on the ring + msg1/3 via ndo_start_xmit), key install lines |
| **B5** (disconnect) | after B3/B4 | `iw dev wlan0 disconnect` | WAL 0x00d0 post consumed, `cfg80211_disconnected` reported, state off, page/mask clean; one disconnect per load |
| **B6** (negatives) | (a) `sta_connect=0` default: connect refused, no posts; (b) `sta_join=2 sta_connect=1`: -EINVAL; (c) connect to a non-present BSSID: bounded timeout -> `cfg80211_connect_done(status=-1, timeout_reason=AUTH/ASSOC)`; (d) corrupt/other-BSSID response: refused by the SME (rx_bad) | each fails closed with a distinct log, page/mask clean |

Grep-able expected dmesg strings (from the TEST-LADDER):

```
sta mode: host-SME connect started (... g0 id6; waiting for the CRX auth response ...)
sta mode: auth response OK -> assoc request posted (state assoc_sent)
sta mode: assoc response OK (... cfg80211_connect_done(status=0) on the CRX path ...)
sta mode: connect timeout (state failed, timeout_reason=...) - failing the connect
sta mode: disconnect reported to cfg80211 (...)
```

### 7.5 Evidence / stop / time

Capture per rung: module sha + params, full dmesg (`hcc_trace=1`), HCC events/direct-reader
stats, `cfg80211_status`, frame-post counters (`frames_posted/refused`), page/state/mask/
staging pack, AP-side log/observer, the exact connect request (PSK placeholder only).
No rmmod, no repeated connect in one load, no retry in place.
**Estimate: 60-90 min** for B0/B1/B2/B3 (+ B4/B5/B6 if time; each rung is a fresh boot,
~4-6 min of boot/ladder each plus the client work). If the session is tight, B0-B3 is the
core deliverable; B4-B6 are the optional tail.

---

## 8. Phase 5 - WPA3-SAE client association (after the RX fix)

**Goal:** a real external client associates to the clean-room AP with WPA3-SAE (SAE auth,
MFP required, BIP-CMAC-256), completes the 4-way handshake and the data path.

**Gate:** this phase is only meaningful **after P1 PASS** (the RX capture works). If P1 is
still open (frames=0), mark this NOT-RUN and do not burn a boot on it: the client auth
frames would again get no RX path. (An open-profile control rung, A1 below, may still be
run as a diagnostic if the RX fire FAILed in a new way - record it as such, not as a pass.)

**Source docs:** [`../wifi-assoc-e2e/RUNBOOK.md`](../wifi-assoc-e2e/RUNBOOK.md) (router
rungs R1-R9 + host phases C0-C6, authoritative), [`MATRIX.md`](../wifi-assoc-e2e/MATRIX.md)
(the PASS/FAIL sheet), [`DIAGNOSIS.md`](../wifi-assoc-e2e/DIAGNOSIS.md) (symptom -> cause),
[`wifi-final-image-2/REPORT.md`](../wifi-final-image-2/REPORT.md) section 8 (the
association fire recipe for image #2).

### 8.1 Router side - one AP profile per boot

Preferred path on image #2 (native module + loader; no /tmp module):

```sh
# one boot, one load: association gates ON, WPA3 fixture, hostapd after the C2 ladder
uci set hi5622.main.mgmt_tx=1
uci set hi5622.main.mgmt_rx=1
uci set hi5622.main.sta_ops=1
uci set hi5622.main.eapol_rx=1
uci set hi5622.main.data_tx=1
uci set hi5622.main.rx_refill_wm=75
uci set hi5622.ap.enabled=1
uci set hi5622.ap.config=/etc/hi5622/hostapd-wpa3-sae.conf
uci commit hi5622
/usr/sbin/hi5622-load                  # load + C2 + AP; ONE invocation
```

**Passphrase**: the image's SAE fixture ships `wpa_passphrase=CHANGEME-lab-only` (a
placeholder). Substitute the same throw-away lab value into the on-box fixture or the
client harness (`ASSOC_PSK`), never a real credential.

Alternative: the association router script with a pushed module
([`assoc-router-fire.sh`](../wifi-assoc-e2e/scripts/router/assoc-router-fire.sh)):

```sh
tools/rscp.sh /tmp/<assoc-module>.ko root@192.168.1.1:/root/hi5622_assoc.ko
tools/rscp.sh opensource/docs/incoming/wifi-assoc-e2e/scripts/router/assoc-router-fire.sh root@192.168.1.1:/root/assoc-router-fire.sh
sh /root/assoc-router-fire.sh --check
sh /root/assoc-router-fire.sh --full --load /root/hi5622_assoc.ko --profile wpa3
# = check -> load -> boot -> C2 -> AP(wpa3) -> dp -> drain-start -> snapshot
```

If P1's RX-init module passes and its FIRE-RECIPE says it carries the association gates,
use that module here instead (a fresh boot with the same gates + the WPA3 fixture); the
corrected carrier (HCC g0 id6) and the mgmt-RX CRX g1 id13 path are already in that tree.

### 8.2 Router expected evidence chain (in order)

| # | where | evidence |
| --- | --- | --- |
| 1 | dmesg | vendor 5-message AP order `vap_add ap_chan ap_ssid ap_cfg ap_vap_start`; `start_ap beacon_static: 'ap_vap_start' posted after ap_cfg` |
| 2 | hostapd | `wlan0: AP-ENABLED` |
| 3 | dmesg | `frame post 'mgmt_tx': g0 id6 total 86 B ... consumed after 0 polls` (auth response; the corrected carrier) |
| 4 | dmesg | `cfg80211: mgmt RX delivered (subtype 0x0b ...)` (the SAE commit/confirm, then the assoc request) |
| 5 | dmesg | `sta_ops: STA <mac> added (AID ...)` + `frame post 'sta_user_add': g0 id7 ...` + `sta_asoc_syn ... 0x07d2` |
| 6 | dmesg | EAPOL-Key msg1/4 and msg3/4 `TX COMPLETED` (data_tx); `cfg80211: add_key posted ...` (PTK), `set_default_key ...` (GTK) |
| 7 | router pack | page golden, mask/staging 0, `cfg80211_status` refused=0 timeout=0 bad=0 |
| 8 | host | client stage COMPLETED; `iw link` Connected to the BSSID; 4-way complete |

(For WPA3 the auth exchange is SAE (auth subtype 0x0b, alg 3): 2 auth frames each way
before the assoc exchange; PMF/IGTK installed at AP start.)

### 8.3 Client side (workstation, sudo; canonical harness)

```sh
cd opensource/docs/incoming/wifi-assoc-e2e
python3 scripts/assoc_e2e.py verify                 # 41/41 self-checks
sudo python3 scripts/assoc_e2e.py preflight
sudo python3 scripts/assoc_e2e.py reset             # one supplicant, NM out of the way, EBUSY class removed
sudo python3 scripts/assoc_e2e.py scan --profile wpa3
#   RF gate: SSID found on 2437 MHz, RSN = SAE + MFP, RSSI >= -70 (target >= -55)
sudo python3 scripts/assoc_e2e.py assoc --profile wpa3 --timeout 90
sudo python3 scripts/assoc_e2e.py cleanup
```

Expected client strings: `SME: Trying to authenticate ...` -> SAE auth frames ->
`Trying to associate` -> `Associated with ...` -> `WPA: Key negotiation completed
[PTK=CCMP GTK=CCMP]` -> `CTRL-EVENT-CONNECTED` -> `wpa_state=COMPLETED`. The harness exits
0 only for COMPLETED and classifies every failure (AUTH-TIMEOUT, SAE-AUTH-TIMEOUT,
ASSOC-TIMEOUT, FOURWAY-FAILED, DISCONNECTED, NETWORK-NOT-FOUND, DRIVER-ERROR).

**Recommended rung order:** if the association machinery has never completed, run the
**open** profile control first (`assoc --profile open`, MATRIX row A1: proves mgmt
RX+TX + station ops + client sequence), then the SAE rung on a **fresh boot** (A3). If A1
already passes and time is short, go straight to A3; if A1 fails, do not attempt A3 - the
diagnosis is in DIAGNOSIS.md.

After each client attempt: router snapshot (`assoc-router-fire.sh --stage snapshot` or the
loader's post-state cats) + the harness JSON/log bundle; fill the MATRIX row.

### 8.4 PASS / FAIL and diagnosis

| result | PASS criteria | if FAIL, first checks |
| --- | --- | --- |
| A1 open control | client COMPLETED + mgmt RX/TX + sta_ops + hostapd AP-STA-CONNECTED | AUTH-TIMEOUT with no mgmt RX = the RX path again (P1 not fixed or regressed); mgmt RX but no TX = carrier; both but assoc timeout = sta_ops/assoc response |
| A2 WPA2 | A1 chain + PTK add_key + msg1/msg3 TX COMPLETED + EAPOL RX >=2 + hostapd "pairwise key handshake completed" | FOURWAY-FAILED: PSK, EAPOL TX/RX, add_key refusal |
| A3 WPA3-SAE | SAE auth mgmt TX/RX (2 each way) + assoc chain + 4-way + client COMPLETED | SAE-AUTH-TIMEOUT: mgmt path/RF; hostapd "invalid key_mgmt SAE" = IMAGE NOT-RUN (wrong hostapd) |
| A4 data (optional) | ping both ways on 192.168.77.0/24; data_tx posts advance; sta-ops RX counters advance | no ARP/RX: drain not running/rx_arm missing/eapol_rx=0; unicast refused: station unauthorized |

RF gate thresholds: >= -55 dBm good, -56..-70 marginal PASS (align antennas, re-run the
watch), < -70 FAIL (do not run the association rungs).

### 8.5 Stop rules / time

Any page/mask/staging deviation, exception, or H2D lane close -> STOP, power-cycle, no
retry in the same boot. One supplicant attempt per client rung; one AP profile per boot;
never reload the module to retry. **Estimate: 30-60 min** for one profile (boot + AP + dp
+ client scan/assoc + snapshot); A1+A3 on two fresh boots ~45-75 min.

---

## 9. Phase 6 - stock-control re-run (only if needed)

**Status: the control ALREADY PASSED on 2026-11-26 10:00 - do not re-run it for the same
question.** It proved the device fine: the vendor pair-A image ran its own AP on the same
chip and the same laptop's kernel log showed `authenticated` + `associated`. The only
reasons to run it again are (a) the Lead wants a fresh same-session control against a
changed client/AP state, or (b) the RX lane asks for the stock-side register/ring state
(CA 0x40050000 and live +0x04) before/after the client's auth frames.

If it is run, it is a separate sitting: it needs the stock pair, the stock root channel and
the recovery door for the switch back.

### 9.1 Switch to stock pair A (same procedure as 5.4)

From our image: boot-selector `next_pair=a` + reboot, or the plain-reboot failover; confirm
the 5.10.201 stock image on the UART and the stock LAN at `192.168.10.1`. The stock AP
comes up by itself (vendor userspace: the Cudy AP on ch7 with WPA1/WPA2 in the recorded
run) - check it with a normal client scan.

### 9.2 The control attempt (the same laptop, the same stimulus)

The control is the **client's own kernel log** showing the 802.11 auth+association against
the stock AP (the 4-way may fail for unknown credentials; that is irrelevant - the radio
path is already proven at auth/assoc). Use the laptop's normal NetworkManager connection
(or the assoc harness with the stock SSID/RSN); then:

```sh
sudo dmesg | grep -E 'wlan[01].*(authenticated|associated|send auth)' | tail
#   expect: 'wlan0: authenticated' + 'wlan0: associated'
```

If the Lead also wants the stock register/ring state: the vendor stack is running, so the
recompiled static devmem reader (pushed to /tmp) can read CA 0x40050000 live and +0x04
before/after the frames - read-only, and only if the tool build is staged; otherwise skip
(the client log is the decisive datum).

### 9.3 Switch back to our image pair B (proven sequence)

From the stock root shell:

```sh
ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
fw_printenv bootflag                    # bootflag=b
echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
sync; reboot
```

(Our 6.18 kernel does **not** expose `/sys/devices/platform/sysenv/` - the boot_reg write
only works from the stock shell; from our image use the boot-selector node instead.)
Confirm our image is back on the UART + SSH 192.168.1.1 before ending the session.

### 9.4 PASS / FAIL / stop / time

PASS = client `authenticated` + `associated` against the stock AP (device RX/TX fixed a
normal client on the real firmware) and the box is back on our image pair B. Any boot
trouble -> the pair switch rules in [`../../SELECTOR-0x21.md`](../../SELECTOR-0x21.md) /
[`../../../../RUNBOOK.md`](../../../../RUNBOOK.md); physical recovery if it stays silent.
No flashing in this phase (the stock image stays on pair A; our image stays on pair B).
**Estimate: 25-40 min** (2 boots + switch + control attempt).

---

## 10. Evidence, ledger and teardown (every session)

### 10.1 What to keep, per phase

* **Router side**: the boot/page/mask/staging packs, `hcc_ready`, `hcc_events`,
  `hcc_status`, `dp_status`, `dp_rings`, the RX capture/parity attrs (P1), the dmesg tail,
  the lane scripts' snapshot files. Write them under `build/<phase>/<UTC stamp>/` (the
  bench scripts already do) - `/tmp` is not evidence.
* **Client side**: the assoc harness's JSON + supplicant log + scan output +
  `dmesg | grep -i wlan` under the harness's `build/wifi-assoc-e2e/<stamp>/`.
* **Host side**: the artifact pin output (`check-pins.sh`), the UART log path, the flash
  transcripts (P2), the observer scans (P3).
* The lane deliverables' `evidence/` directories stay the reproducible host-side proof;
  live runs go to `build/`.

### 10.2 The verdict lines to copy into the ledger

The ledger entry (append-only, root repo `mem-entries.md`, the Lead's write scope) should
carry for each phase: phase, module sha/size/params, boot/page/mask/staging state, the
decisive output line(s), PASS/FAIL/STOP, the evidence path, and the one next action. Use
the existing entries' style; do not claim on-air unless the observer saw it.

### 10.3 Teardown (end of session)

```sh
tools/bench-teardown.sh --apply                  # drop stale log writers/HTTP servers; keep TFTP only if armed
tools/uart-capture.sh stop                       # after the box is stable
tools/recovery-card.sh --stop                    # only after all post-boot checks PASS
pkill -f 'http.server 8000' || true              # the P2 image server, if still running
```

Leave the box on a known image, the selector re-recorded, and `STATUS.md` regenerated (the
Lead's write scope). Commit only inside `opensource/`; the root repo's `STATUS.md` /
`mem-entries.md` are the Lead's files.

---

## 11. Artifact pin table (all local artifacts in one place)

### 11.1 Image / userspace / firmware

| artifact | size | sha256 | where |
| --- | --- | --- | --- |
| image #2 container | 7,188,799 B | `8ba1ad22...` (full in 5.1) | `/home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin` |
| image #2 rootfs UBI | 3,801,088 B | `31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d` | same dir, `rootfs.ubi` |
| kernel slice (unchanged) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` | same dir, `kernel-slice.bin` |
| installed image-2 module | 328,788 B | `b4c076bf37099b21bcb5055f23678ffee38e86da9b3975e36d5c50025b31065e` | in the flashed rootfs |
| image-2 hostapd (SAE) | 1,032,951 B | `451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469` | in the flashed rootfs |
| PD4 firmware | 928,920 B | `cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1` | regenerate via fw_pd_builder (sec. 3.4) |
| PD4 manifest | - | `d89f01a7b6f1a7eeafccb1036911ac2e32bd03a0422b84d92ac60b46142c288c` | same generator output |
| Wi-Fi PHY block | 2,472 B | `0b4ef9188fc1c9160b4b30cc20be0e704dd62412cbdb25dd51ea5ffb16145b32` | local capture -> `/tmp/blocks/wifi_block.bin` |

### 11.2 Session modules (all local, never committed)

| phase | file | size | sha256 | params | source / rebuild |
| --- | --- | --- | --- | --- | --- |
| P1 | RX-init module | PIN | PIN | ~58-60 | rx-init-decode task-170, `../wifi-rx-init-msg/` |
| P3 | 5 GHz module | 351,008 B | `1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53` | 57 | `../wifi-5ghz-bringup/tools/build-5ghz-ready.sh` |
| P4 | STA-mode module | 364,572 B | `8c0adf93995fd5014b73107de7fed77c825e3eee88fec8cc7a1c284f501c2059` | 58 | `../wifi-sta-mode/tools/build-sta-mode-module.sh` |
| P5 | association module (image-2 native) | 328,788 B installed / 347,388 B artifact | `b4c076bf...` / `d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6` | 57 | image #2 / `../wifi-rx-watermark/tools/build-final-module.sh` |
| P5 alt | corrected-carrier association artifact | 344,372 B | `ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e` | 56 | `../wifi-assoc-e2e/scripts/verify-association-module.sh --rebuild` |

### 11.3 Optional diagnostic modules (ONLY if P1 fails and the Lead wants the RX
instrumentation on the same day; each is a fresh boot, never combined)

| module | size | sha256 | params | source |
| --- | --- | --- | --- | --- |
| RX vendor-diff (parity + observe-only capture) | 361,872 B | `81ab72c1...` (lane rebuild `9a0d7ddf...`; .text identical) | 59 | `../wifi-rx-vendor-diff/` |
| RX format + PPDU/header conformance | 356,424 B | `0eadba35...` | 57 | `../wifi-rx-format/` |
| CRX gate read-only probe | 352,020 B | `a9d3835a...` | - | `../wifi-rx-crx-gating/` |
| WOC-class buffer experiment | 365,332 B | `3363903a...` | 60 | `../wifi-rx-woc-class/` |
| completion-phase prepublish | - | `bc942e13...` | 60 | `../wifi-rx-completion-phase/` |

Their fire recipes are in the respective lane dirs (R0-R4 rungs; read-only first). Use them
only for a documented follow-up question, not as a P1 retry.

### 11.4 Tool map (host / device)

| tool | purpose |
| --- | --- |
| `tools/bench-link.sh` / `tools/recovery-card.sh` / `tools/uart-capture.sh` | bench prerequisites |
| `tools/rsh.sh` / `tools/rscp.sh` | root SSH to our image (192.168.1.1) |
| `tools/telnet-exec.py` / `tools/box-provision.sh` | stock root channel (192.168.10.1) |
| `tools/stock-telnet-install.sh` | the stock-assisted full-container installer (reference; P2 uses the manual rootfs-only sequence) |
| `tools/bench-teardown.sh` | end-of-session cleanup |
| `../wifi-assoc-e2e/scripts/router/assoc-router-fire.sh` | association router rungs + snapshots |
| `../wifi-assoc-e2e/scripts/assoc_e2e.py` | canonical client sequence/classification |
| `../wifi-onair-observer/tools/scan-ap.sh` / `capture-beacon.sh` | external observer (P3 G6) |
| `../wifi-bench-tooling/tools/bench_session.py` | the general attended runner (sequences under `tools/sequences/`) |
| `scripts/check-pins.sh` (this lane) | host-only artifact pin check |

---

## 12. What remains after this runbook (honest notes)

* **If P1 PASSES:** the RX capture is closed; the immediate follow-up is P5 (WPA3-SAE
  association) and then the remaining data-path questions (EAPOL/data ring, TX
  completion), which P5's A4 rung covers. The bigger Wi-Fi ceiling does not move: the
  path still needs the **PD4-patched research firmware** and there is **no upstreamable
  Wi-Fi driver**.
* **If P1 FAILS:** the next static steps are (a) the rx-init lane's next candidate from
  the same init-chain decode, (b) the **stock H2D trace** (boot the vendor image and
  capture its driver's H2D message sequence - the decisive reference the host lacks),
  and (c) the firmware's H2D dispatch table to bind the `rx_ring_reset_req` /
  `enable_trx_req` handlers to exact group/id/sub-event values. The diagnostic modules in
  sec. 11.3 can discriminate the capture/format/CRX layers in one read-only fire.
* **P2** changes the bench baseline (image #2 native module + SAE hostapd): after it, the
  association phase no longer deploys a module from `/tmp` and the post-boot receipts are
  the new bench-state witnesses. The old ABI image rootfs `62344e80...` stays the rollback.
* **P3/P4** produce the band and client-mode evidence; neither is a prerequisite for the
  other phases, and neither claims upstream support.
* **P6** is a control, not a feature.
* **Publication/upstream track** is separate and explicitly out of this runbook: the
  Wi-Fi driver stays research-only/out of scope for the submission series until the vendor
  GPL source or a host-clean firmware boot path exists (see
  [`../wifi-closeout/PUBLICATION-NOTES.md`](../wifi-closeout/PUBLICATION-NOTES.md) and
  [`../upstream-v3/`](../upstream-v3/)).

**Known documentation hazards (why this file consolidates):** several lane docs carry
dates and claims from different sessions, and the image lineage has two "final" bakes
(5.1). The ledger (append-only, newest dated entry) is the arbiter; this runbook points at
the exact per-lane deliverable that owns each procedure so nothing is re-derived at the
bench.

---

## Appendix A - golden state, status word and exception classes

**Golden state** (after the boot ladder / before and after every rung):

| item | CA | value |
| --- | --- | --- |
| handshake page | 0x1BF000 | 0x86807FEF 0x00000007 0x00000002 0x0000ABCD (+8==2) |
| boot state | 0x118B60 | 1 |
| exception mask | 0x400008 | 0 |
| exception staging | 0x40000C | 0 |
| H2D out0/out2 | - | 0 |

**MAC status word** (CA 0x40050000, read via the module attrs): `0x182` = bits 1 + 8
(armed, vendor-consistent; rx=0); `0x186` = bit 2 set (RX completion pending - the
prepublish result); `0x582` = bit 10 set (`rx_ppdu_desc_error`: the MAC received a frame
and the PPDU/completion-descriptor write failed) - the open-bug signature; mask
`0xfffffcc1` is the vendor RX mask.

**Exception classes seen at the bench:** mask CA 0x400008 = `0x04000010` (vector 4 = data
abort, the E2/stale-VAP and RX-frame classes) + page golden:0 (gate 0xcece) - always a
physical power-cycle; the exception vector frame words live at CA 0x40000C..0x400044
(record them, do not interpret in the session).

**Page/exception read policy:** read-only through the module/script attrs; never write CA
0x400392F0; never read the RC misc window 0x10161000; never write 0xCECE during boot.

## Appendix B - quick command index

| need | command / doc |
| --- | --- |
| host preflight | `tools/bench-link.sh`, `tools/recovery-card.sh`, `tools/uart-capture.sh start/check` |
| artifact pins | `opensource/docs/incoming/BENCH-NEXT-SESSION/scripts/check-pins.sh` |
| boot ladder (manual) | sec. 3.6 |
| RX-init fire | sec. 4 + rx-init-decode FIRE-RECIPE |
| image #2 install | sec. 5 + [FLASH-FEASIBILITY](../wifi-final-image-2/FLASH-FEASIBILITY.md) |
| 5 GHz fire | sec. 6 + [FIRE-RECIPE](../wifi-5ghz-bringup/FIRE-RECIPE.md) |
| STA ladder | sec. 7 + [TEST-LADDER](../wifi-sta-mode/TEST-LADDER.md) |
| WPA3-SAE association | sec. 8 + [assoc-e2e RUNBOOK](../wifi-assoc-e2e/RUNBOOK.md) |
| stock control | sec. 9 + [SELECTOR-0x21](../../SELECTOR-0x21.md) |
| teardown | `tools/bench-teardown.sh --apply` |

## Appendix C - known failure modes and what they mean

| symptom | likely cause | action |
| --- | --- | --- |
| page/mask/staging nonzero, exception, page golden:0 | firmware exception (RX frame / stale-VAP / unknown) | power-cycle, capture the vector frame + dmesg, no retry in place |
| ready pair missing after release | chip boot disturbed (polled too early / bad firmware) | power-cycle; check the 2 s initial poll delay; do not re-release |
| H2D frame not consumed / lane closed | firmware fault or param order | STOP; record h2d_status; new boot |
| hostapd AP-ENABLED but client AUTH-TIMEOUT with no mgmt RX | the RX capture bug (P1) or RF | check P1 state; check RSSI/RF gate; do not chase the client first |
| mgmt RX delivered but no mgmt TX / AUTH unanswered | mgmt-TX carrier/gate | check the corrected g0 id6 carrier is in the module; record the post line |
| association ok but 4-way fails | EAPOL-TX/RX, keys, PSK/SAE mismatch | DIAGNOSIS.md; check add_key/set_default_key + EAPOL counters |
| scan sees no AP / RSSI < -70 | RF, channel, antennas, regdomain | fix RF first; the no-antenna baseline was -63..-65 dBm |
| `iw reg set` fails for 5 GHz | no regdb/country rule | record; 5 GHz AP refused by NO_IR - do not fight it |
| module refuses `band5g`/`rx_init` param | wrong module build | check sha/param count vs sec. 11; do not improvise |
| kernel package pin mismatch after P2 | wrong rootfs written | STOP, investigate, rollback possible (kernel slot untouched) |

## Appendix D - where each procedure comes from (source map)

| phase | authoritative lane deliverable | what it owns |
| --- | --- | --- |
| P1 | `../wifi-rx-init-msg/` (delivered; module sha pin pending) + the rx-* lane reports | the message decode, the module, the fire recipe |
| P2 | `../wifi-final-image-2/{FLASH-FEASIBILITY,README,HASHES}.md` + `../wifi-abi-image/FLASH-RUNBOOK.md` | image identity, rootfs-only sequence, post-boot receipts, rollback |
| P3 | `../wifi-5ghz-bringup/FIRE-RECIPE.md` | G0-G7, module, refusals |
| P4 | `../wifi-sta-mode/TEST-LADDER.md` | H/B rungs, module, expected strings |
| P5 | `../wifi-assoc-e2e/{RUNBOOK,MATRIX,DIAGNOSIS}.md` + `../wifi-final-image-2/REPORT.md` sec. 8 | router rungs, client harness, PASS/FAIL |
| P6 | ledger 2026-11-26 10:00/11:00 + `../../SELECTOR-0x21.md` | the proven stock-control sequence and pair switch |
| rules | `../../../../BENCH-FACTS.md`, `../../../../RUNBOOK.md`, `../../../../LESSONS.md` | physical invariants, recovery, procedural rules |
| state | `../../../../STATUS.md`, `../../../../mem-entries.md` (newest entry wins) | live state, the ledger |

*Written by the bench-runbook lane (task-173). Host-only composition; no device was
contacted. The per-lane deliverables above remain the authority for their procedures; this
file is the single-session index and checklist.*





