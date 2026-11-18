# First install from stock firmware - design and fail-closed tooling

**Status: 2026-11-18. Host-only design + tooling. NOTHING BELOW HAS RUN ON THE ROUTER.**
No device, UART, TFTP, flash or bench tool was touched for this document or for the
tool it describes; no write, boot, selector move or recovery claim is made. Every
hardware statement is re-read from artifacts already in the repository or the parent
(private) repository and carries an evidence marker. The install path itself is
**[I] implemented host-side, [U] unexecuted**.

- **Task:** shared task-38, 2026-11-18. Deliverable: this design document, the
  fail-closed host tool (parent `tools/first-install-stock.sh`) and its mocked test
  harness (parent `tools/tests/first-install.test.sh`), plus an incoming shared-docs
  package.
- **Design source of truth:** [TARGET-DESIGN.md](TARGET-DESIGN.md) sections 3.3, 3.4, 5.4,
  5.5; selector reconstruction [SELECTOR-0x21.md](SELECTOR-0x21.md); measured flash
  recipe [FLASH-PLAN.md](FLASH-PLAN.md); build/image status [SKELETON-SMOKE.md](SKELETON-SMOKE.md)
  and [../target-skeleton/README.md](../target-skeleton/README.md); parent
  `BENCH-FACTS.md`, `LESSONS.md`, `RUNBOOK.md`.
- **Markers:** **[V]** verified from a named local artifact or console capture;
  **[I]** inferred/implemented but not hardware-executed; **[U]** unmeasured/open;
  **[X]** contradicted by local evidence.

---

## 1. Scope, and what this document does not do

In scope: the **first-install path from the stock vendor firmware to our generated
image**. "Stock" means the vendor OpenWrt 22.03.6 userspace on kernel 5.10.201 with the
vendor module stack, as it comes back from the Cudy TFTP recovery image. The path writes
our image's kernel slot and rootfs slot into the **inactive** A/B pair, then hands the
boot selector to that pair, then reboots.

Out of scope / not claimed:

- no replacement bootloader work (U-Boot source is unpublished; the design targets the
  stock loader) [V] TARGET-DESIGN.md section 3.1;
- no use of the stock TFTP recovery container as an install vehicle: its write path is
  closed and uncharacterized, and our `platform_check_image` deliberately refuses vendor
  recovery images [V] SELECTOR-0x21.md section 6; [U] what that image writes at runtime;
- no rootfs/sysupgrade upgrade path (that is TARGET-DESIGN.md section 5.4 and the A/B
  installer in the target's `platform.sh` - a different, already-implemented path that runs
  from **our** image, not from stock) [V];
- no device execution. This is a written procedure to be executed by an attended human
  under the project's fire rules, not a release.

## 2. Why this path, and why the tool is a host-side driver

TARGET-DESIGN.md section 5.5 opens the install question and lists the options. The
project's own history answers it: builds 0.1-0.3 were installed from the **stock vendor
userspace** by (a) writing the kernel slot with `mtd`, (b) `ubiformat`-ing the
inactive rootfs slot, and (c) moving the selector - measured on hardware several times
[V] FLASH-PLAN.md "0.2 result", "0.3 result", "Slot switch recipe". The stock system has
`mtd` and `ubiformat` [V] FLASH-PLAN.md 0.2/0.3 result; the root shell is the
project's normal transport (parent `tools/rsh.sh` over the vendor LAN
192.168.10.1, key in the gitignored `.omo-secrets/`) [V] parent BENCH-FACTS.md section 2.

The alternative - "boot a TFTP rescue image and install" - is not available on this
bench: the UART has **no input direction** (host-to-router TX is dead), so the U-Boot
prompt cannot be driven, and the vendor recovery image's write path is closed
[V] parent BENCH-FACTS.md section 3; SELECTOR-0x21.md section 6; [U] its runtime writes.
The physical RESET+TFTP recovery therefore stays the **fallback**, never the installer.

Consequences that shape the tool:

1. It runs on the host and drives the box over the existing rsh/rscp helpers; it never
   embeds a payload in a downloadable file and never serves the repository over HTTP.
2. It is **read-only by default** and refuses to write without explicit attended flags.
3. It carries its own fail-closed checks instead of trusting the caller: partitions are
   re-derived **by label** from `/proc/mtd` (never a bare number - LESSONS.md rule 21),
   sizes are re-measured, the image container is re-validated from its own bytes, and the
   selector state is derived from the measured loader contract.
4. Every failure path prints the recovery plan before exiting.

## 3. The artifact being installed

The tool consumes **our generated sysupgrade container** - the build product of
target-skeleton `image/luofu.mk` (validated by
`target-skeleton/tools/luofu-image.py`) [V] SKELETON-SMOKE.md image-recipe +
selector-fix sessions. Container members:

| member | content | limit / check |
| --- | --- | --- |
| `CONTROL` | `BOARD=cudy_wr3000-v2`, `TARGET=hisilicon/luofu` | must match |
| `kernel` | FIP slice: legacy uImage + generated trailer | 8,650,752 B max; uImage magic `0x27051956`; FIP gate `0xd7b7ab1e` at the manifest `gate-offset`; manifest `slice-sha256` must equal the bytes |
| `root` | squashfs in a static UBI volume `squashfs` | 24,313,856 B max; whole number of 131,072 B PEBs; `UBI#` EC + `UBI!` VID headers at the measured offsets |
| `fip-manifest.txt` | slice size/hash, gate offset/magic, slot limit | cross-checked against the bytes, not trusted |
| `INSTALL-STATUS.txt` | the NOT FLASHABLE provenance marker | required by the check; this attended run is what closes it |
| appended fwtool metadata | standard OpenWrt metadata on the file | ignored by the installer |

Measured example (the current final build, local artifact, never flashed) [V]:
6,185,262 B sha256 `362ca4ece4df2b77b0e527ad5d948bbd997201ccb3bf6e2518d5ddaccbb59899`;
kernel slice 3,285,206 B (gate at `0x321fb0`, load/entry 0x80608000); rootfs UBI
2,883,584 B; independently re-read by `luofu-image.py check-container` PASS
(SKELETON-SMOKE.md selector-fix session; EVIDENCE-INDEX image row).

Partition map used for the write (stock vendor DTB / parent dumps) [V] TARGET-DESIGN.md 3.3;
parent `dumps/router-20261108/full/proc_mtd.txt`:

| pair | kernel slot | rootfs slot | notes |
| --- | --- | --- | --- |
| A | `mtd11 kernela` (8,650,752 B) | `mtd13 rootfsa` (24,379,392 B) | stock vendor image |
| B | `mtd12 kernelb` (8,650,752 B) | `mtd14 rootfsb` (24,379,392 B) | stock vendor image |
| shared | `mtd15 rootfs_data` (22 MB, JFFS2) | `mtd16 upgrade` (vendor) | **never written** by this path |
| bootloader | `mtd3 enva` / `mtd4 envb` | `mtd1 uboota` / `mtd2 ubootb` | **never written** (the loader owns the env) |
| vendor data | `mtd0 esbc`, `mtd5 fac`, `mtd6 bdinfo`, `mtd7/8 cfga/b`, `mtd9 log`, `mtd10 pstore` | | **never written** |

The installer writes **only** the inactive pair's two slots:

1. kernel slot: `mtd erase kernelX` then `mtd write <slice> kernelX` (both by label),
   followed by a full-slot sha256 read-back (slice bytes + erased `0xFF` tail);
2. rootfs slot: `ubiformat /dev/mtdN -y -f <ubi>`, followed by a full-partition sha256
   read-back (UBI bytes + erased `0xFF` tail) and a fail-closed refusal if `ubiformat`
   reports a bad block.

Both read-backs are full-region comparisons, not header peeks. The expected hashes are
computed on the host from the validated image bytes; the device re-hashes its own flash
region after the write.

## 4. The boot-selector handover (measured semantics only)

The reconstruction is in [SELECTOR-0x21.md](SELECTOR-0x21.md); the parts this path
depends on:

- the loader selects the pair from the **env variable `bootflag`**:
  `bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}` [V] env dump
  + captures. The installer **never writes the env blocks** - the loader's own mismatch
  path re-stamps them.
- the SYSCTRL cell at `0x10100C08` is the loader's **record**: low byte `0x10` = A
  pair recorded, `0x11` = B pair recorded; `0x00` is the true-POR value; on every
  normal boot `bootfip` stores the **opposite** pair's low byte (low-byte
  read-modify-write), and a true power-on clears the cell [V] disassembly in
  SELECTOR-0x21.md sections 3.2/3.3, 4.1.
- if the recorded cell contradicts the env on the next boot, the loader prints
  `boot from kernelX failed, change to kernelY`, writes the new `bootflag`, saves
  the env and boots Y [V] SELECTOR-0x21.md 3.2/4.4.
- a successful boot is only stable if userspace re-records the running pair after boot
  (vendor `boot_done` at ~16-30 s; our target image's
  `base-files/etc/init.d/luofu_record_pair` at START=96) [V] SELECTOR-0x21.md 4.6,
  7.2; [I] for our image (never run on hardware).

So the handover step is: **write the written pair's recorded low byte into the cell**
(A pair -> `0x10`, B pair -> `0x11`) as the very last device write, verify it with a
whole-cell read-back, verify the env is unchanged, then sync. On the next boot the loader
takes its mismatch path and boots the written pair. `0x21` is the project's bench
handover value and is **not** used here: it is not a vendor value, the loader ignores the
`0x20` bit and normalises it on the same boot, and it never persists [V]/[X]
SELECTOR-0x21.md 2.1/2.2/7.1.

The tool uses the **stock vendor sysfs shim** for this write - the measured
`/sys/devices/platform/sysenv/boot_reg` interface (vendor `hi_ksysenv.ko`: store
replaces only the low byte and preserves the upper 24 bits) [V] SELECTOR-0x21.md 5.2 -
because the stock system does not have our patch-017 `luofu-selector` interface. This is
the same cell and the same low-byte semantics; it is not a second selector.

State table for the handover on the actual write (all [V] loader logic, [I] end-to-end):

| before | action | next boot |
| --- | --- | --- |
| env `a`, cell `0x10` (normal stock A boot) | write cell `0x11` (B pair recorded) | mismatch -> env := b, saveenv -> boots kernelb/rootfsb; bootfip then records `0x10` |
| the written image boots to userspace | its recorder writes the running pair (`0x11`) | cell matches env; later boots stay on B |
| the written image never reaches userspace | cell stays `0x10` (opposite marker for b) | next boot: env b + cell `0x10` -> mismatch -> env := a -> **stock A boots again** |
| any true power-on | cell cleared to `0x00` | loader initialises the cell from the env and boots that pair |

Same in the other direction with A/B swapped and value `0x10`.

**Cancellation before reboot:** while the box is still running stock, writing the active
pair's record value back (`0x10` for A, `0x11` for B) restores the "normal boot"
state; a **true power cycle** also returns to the active pair because POR clears the
cell, so the loader initialises from the (still active) env.

## 5. The tool

Parent repo: `tools/first-install-stock.sh` (executable, fail-closed; no device
execution by its authors). It drives the box through the existing
`tools/rsh.sh`/`tools/rscp.sh` helpers (project key, BatchMode, no
StrictHostKeyChecking) and feeds the device-side half over SSH **stdin** (`sh -s <sub>`),
so a read-only inspection writes nothing at all to the device. The image members are
staged with `scp -O` to `/tmp/first-install-<tag>-*` (the measured multi-MB transfer
we know works: FLASH-PLAN.md transfer lesson), re-hashed on the device, and removed on a
successful run.

Modes (higher includes the lower; each invocation is one explicit step):

| mode | what it does | writes flash? |
| --- | --- | --- |
| `--inspect` (default) | host checks + image validation + read-only device probe + readiness report + the exact plan | no |
| `--write` | stages and writes kernel+rootfs to the inactive pair, both full read-backs, re-probe; selector and env untouched, no reboot | inactive pair only |
| `--handover` | `--write` then the selector move + read-back + env-unchanged check + sync; no reboot | inactive pair + selector cell |
| `--reboot` | `--handover` then sync/reboot and poll for a return, classifying the outcome | as above + reboot |

Hard gates (any missing -> usage refusal before device access; write modes):

- `--attended` - a human is at the bench, able to power-cycle the box and hold RESET
  through the counter;
- `--recovery-armed` - the operator asserts the recovery door is armed, **and** the tools
  verify it: `tools/recovery-card.sh` must PASS (recovery.bin md5
  `421114c8ea999c925b7aa79cc548af81`, 20,975,775 B, TFTP on UDP 69, host 192.168.1.88) and
  `tools/bench-link.sh` must PASS [V] parent BENCH-FACTS.md sections 2/4;
- `--image PATH` - a validated sysupgrade container; the host runs
  `luofu-image.py check-container` **and** its own independent byte checks
  (identity, member magics, manifest-vs-bytes, gate magic, slot budgets, PEB geometry);
- `--reboot` additionally requires a running read-only UART capture
  (`tools/uart-capture.sh`, or a configured check command).

Device-state refusals (no write; the tool lists the blockers):

- not the stock vendor userspace: kernel must be 5.10.201, vendor init marker
  `/etc/init.d/hsan_appm` present, `/lib/hisilicon/ko` with at least 10 vendor modules,
  at least 5 `hi_*` modules loaded, and **no** `/etc/custom-firmware-version` marker;
- unhealthy stock: fewer than 2 wiphys, Wi-Fi calibration `[SUCC]` missing on either band,
  or `.omo-off` leftovers;
- inconsistent selector state: booted rootfs pair (`/sys/class/ubi/ubi0/mtd_num` + its
  `/proc/mtd` label) must equal env `bootflag`; an explicit `--expect-active` must agree;
- wrong slot sizes/table: kernel slots must be exactly 8,650,752 B, rootfs slots exactly
  24,379,392 B, env blocks 0x40000 B, all labels uniquely present;
- handover-only: the cell must equal the **running-pair record** (`0x10` on A,
  `0x11` on B). An opposite-pair value means the vendor "boot_done" window has not
  completed (or the box is fresh from a failed attempt); a manual `0x21`, `0x00` or any
  other value refuses the handover. `--write` is still allowed in that state - the
  selector is not moved - which is what makes staged operation possible;
- device prerequisites: `mtd`, `ubiformat`, `sha256sum`, `dd`, `hexdump`, `sync`,
  `fw_printenv`, `dmesg` present; the sysenv interface readable/writable; at least
  8,192 KiB free in device `/tmp`;
- boot_id/active-pair/fingerprint re-checks immediately before the write and before the
  handover: a reboot or state change during the run aborts before the next step.

Failure behaviour:

- **kernel write/read-back fails** -> abort before the rootfs write; selector untouched;
  recovery plan printed;
- **rootfs write/read-back fails, or ubiformat reports a bad block** -> abort; selector
  untouched; the active pair still boots; recovery plan printed;
- **handover fails after the cell may have been written** -> the tool marks the selector
  state as moved/uncertain in the recovery plan (conservative: it tells the operator to
  cancel and check), and the device restores the active record itself when its own
  read-back/env check fails; it never reboots on a failed handover;
- the **reboot clause** never claims success from silence: it exits 3 with a
  classification (stock fallback / no reboot detected / unverified) and the recovery plan.

Exit codes: 0 success/inspection-ready; 1 failed safely; 2 usage; 3 completed but the
outcome needs the human/console.

No secrets: the tool prints paths, sizes and sha256 values only; it never prints the SSH
key, credentials or image-member contents.

## 6. Sequence (attended)

Preconditions (all checked by the tool; human confirms the bench): parent
`BENCH-FACTS.md` read, recovery door armed, read-only UART capture optionally armed,
`STATUS.md` names the delivery plan, box is stock and healthy.

Step 0 - inspection (read-only, no flags needed):

    tools/first-install-stock.sh --inspect --image <built sysupgrade.bin>

Expected: "INSPECTION PASS", the device state block (booted pair, env bootflag, cell,
slot sizes, target pair, image hashes) and the plan with the selector value the handover
would write. No flash write happens.

Step 1 - write the inactive pair (attended, recovery armed):

    tools/first-install-stock.sh --write --attended --recovery-armed --image <image>

Expected: image validation PASS; device probe and readiness PASS; staged member hashes
verified; `mtd erase kernelX`/`mtd write` + full-slot read-back OK; `ubiformat` +
full-partition read-back OK; the active pair fingerprints unchanged; "WRITE COMPLETE".
The box still boots stock (selector unchanged).

Step 2 - handover (attended, recovery armed):

    tools/first-install-stock.sh --handover --attended --recovery-armed --image <image>

Expected: it re-verifies both written regions, the active pair and the boot_id, then
writes the cell record, reads it back, verifies the env is unchanged, syncs and prints
"HANDOVER COMPLETE" with the next-boot explanation. The box is still running stock; do
not warm-reboot before the attended reboot step if you want the option to cancel.

Step 3 - reboot into the written pair (attended, UART capture armed):

    tools/first-install-stock.sh --reboot --attended --recovery-armed --image <image>

Expected UART/loader output (see section 7) and a console-side verification. The tool
polls SSH only to classify an early return; our target image is not expected to accept
the host key (and currently has no usable wired network - section 8), so "no SSH return"
is the normal, expected case and is reported as UNVERIFIED, not as a failure to fix by
writing again.

Staged operation is deliberate: a failed or refused later step never requires repeating a
step that succeeded. Each invocation re-validates the state it depends on.

## 7. Expected UART/loader output (and how to read it)

The console is **read-only** (parent BENCH-FACTS.md section 3): capture with
`tools/uart-capture.sh start --label firstinstall`; never type into the tty.

A **successful handover boot** (A active -> B written), console vocabulary from the
measured captures [V] SELECTOR-0x21.md 3.5/4.2/4.4:

    bootflag a bootreg 11
    boot from kernela failed, change to kernelb
    Saving Environment to NAND... Erasing (redundant) NAND... Erasing at 0x280000/0x240000...
    Reading 8650752 byte(s) (4224 page(s)) at offset 0x00000000
    bootreg offset: c08
    boot regval: 10
    Image Name: Linux-<our version>
    Data Size: <our size>

The env-save lines above are the loader's mismatch re-stamp: they are the first hardware
evidence that the selected pair moved (the env block itself is never written by our tool)
[V] SELECTOR-0x21.md 3.2; captures 4.4. Then our kernel's own console (kernel version,
luofu driver probes) and userspace must follow.

**Reading rules** (parent LESSONS.md 20/24, RUNBOOK.md section 4):

- ours is an **uncompressed uImage**: U-Boot prints `Image Name: Linux-...` and a
  `Data Size` in the ~3 MB class and then our kernel output; only the stock
  4,294,096-byte image prints `Uncompressing Linux`;
- a silent reset after the uImage read, with no kernel output, is a pre-console crash, not
  a success (LESSONS.md 25);
- if the console shows the stock 4,294,096-byte image and the vendor stack instead, the
  loader fell back to the active pair; collect the capture and stop (section 8).

Console-side success markers to look for (the expected set, [I] for our image):
kernel version banner from our build, our luofu driver probes (CRG/watchdog/FMC etc.;
the GEMAC probe refuses by design, the switch node is disabled), and - if the rootfs
mounts - OpenWrt userspace. Rootfs mount/overlay and the boot-argument hand-off are
still [U] (TARGET-DESIGN.md 5.6): our trailer base DTB is ours, but whether the stock
loader overrides `/chosen/bootargs` has never been observed.

## 8. Rollback, recovery and the honest cost of a successful boot

Failure before the handover is cheap: the selector is untouched, the active pair boots,
and the inactive pair may be partially written (re-run the write, or leave it - it is
never selected). Failure during a handover is safe if the cell read-back/env check fails:
the tool/device restores the active record. A true power cycle also returns to the
active pair because POR clears the cell [V] SELECTOR-0x21.md 4.1.

The dangerous-in-cost case is the **opposite**: our image reaches userspace, its recorder
runs (S96, after `/etc/init.d/done`), and the cell becomes the running pair. From
then on the box boots our image again. Our current target image has **no usable wired
network and no Wi-Fi** (GEMAC is a refusing resource/binding half, the internal LSW driver
does not exist, the Wi-Fi stack is out of scope [X]) [V] SKELETON-SMOKE.md, TARGET-DESIGN.md
8.1/8.3, so there is no SSH/LAN path back. The way back is the **physical RESET-counter ->
TFTP recovery** of the whole unit (parent BENCH-FACTS.md section 4, RUNBOOK.md section 6):
hands, a true POR, and the vendor recovery image; it restores the stock firmware and
leaves an empty overlay. That is the price of a successful-but-unwanted first boot, and it
must be accepted in the attended checklist before the reboot.

A better design for the next iteration (proposed, [I], **not implemented**): a
**trial-boot** variant in which the image's boot-time pair recorder is inhibited (or
gated by a value the FIP trailer's own base DTB can carry), so a successful first boot
still leaves the loader's opposite-pair marker in place: the next reboot automatically
fails back to the untouched stock pair, and the operator can verify the image from UART
before committing a persistent build. The current image records the running pair
unconditionally, so the current path is a one-way trial unless the physical recovery is
used.

Recovery card (parent `tools/recovery-card.sh --arm`): the physical step is power
OFF, hold RESET, power ON still holding until the console prints
`RESET button is pressed for: 2 second(s)` / `Resetting...` /
`recovery button press`, then the loader TFTPs `recovery.bin` from 192.168.1.88
[V] parent BENCH-FACTS.md section 4. After recovery: stock LAN back at 192.168.10.1, empty
overlay (reinstall the project key / dropbear per RUNBOOK.md), verify 2 wiphys,
calibration `[SUCC]` both bands, no `.omo-off` leftovers.

## 9. Risks

| # | risk | evidence | mitigation / status |
| --- | --- | --- | --- |
| R1 | A successful-but-unwanted first boot is only reversible by physical RESET+TFTP (target image has no network) | [V] TARGET-DESIGN 8.1/8.3; [I] no restored LAN | attended only, recovery armed; trial-boot variant proposed but not implemented |
| R2 | The generated FIP trailer has never booted (novel-trailer acceptance still [U]) | [V] SKELETON-SMOKE.md; TARGET-DESIGN 5.2 | this install is also the attended trailer acceptance; capture and classify, do not retry blindly |
| R3 | NAND is degraded (ECC/JFFS2 failures observed); every fire is a wear-limited write | [V] parent BENCH-FACTS.md 5 | full-region read-back after each write; ubiformat bad-block report is a hard abort |
| R4 | The env re-stamp/failover behavior on a real install is decoded but unmeasured end-to-end | [V] SELECTOR-0x21.md 3.2; [U] 4/U6 | expected console lines in section 7; attended validation owed |
| R5 | The boot-time recorder (required for stability) has never run on hardware; before it runs, the next boot flips back | [V] SELECTOR-0x21.md 4.6/7.2; [U] | first boot classification: stock fallback is safe; a userspace boot without the recorder would show the opposite cell and fail back |
| R6 | Rootfs mount, overlay format and the boot-argument hand-off are [U] | [V] TARGET-DESIGN 5.3/5.6 | captured boot is the closing experiment; rootfs_data is never touched by this path |
| R7 | Stock root shell availability is a precondition (the vendor image ships no SSH after recovery; the key must be installed via the project's provisioning path) | [V] parent BENCH-FACTS 2; [I] provisioning lanes | inspect mode fails closed with "device probe failed" if unreachable |
| R8 | A partial inactive-pair write could confuse a later attempt | [I] | the write is idempotent (erase + write + ubiformat again); selector never moved on failure |
| R9 | Ubiformat-ing the inactive stock slot destroys the second stock copy | [V] partition map | the active stock pair remains the rollback while the selector is untouched; after a successful boot the physical recovery is the rollback |
| R10 | The image carries the NOT FLASHABLE provenance marker (never booted) | [V] build artifact | accepted deliberately for this attended step; the tool warns |

Also out of scope here: writing `rootfs_data` / overlay migration (JFFS2 decision remains
TARGET-DESIGN 5.3), env-block repair, any `0x20` / "manual A" value (the loader ignores
that bit - do not use it), and any UART transmit.

## 10. Attended checklist

Before (human + host):

- [ ] parent `BENCH-FACTS.md` read; bench link PASS (`tools/bench-link.sh`);
- [ ] recovery door READY (`tools/recovery-card.sh --arm`): recovery.bin size/md5
      verified, TFTP server on UDP 69, host 192.168.1.88 present; hands free to
      power-cycle and hold RESET;
- [ ] read-only UART capture armed (`tools/uart-capture.sh start --label firstinstall`)
      for the reboot step;
- [ ] box stock and healthy (inspection PASS: 2 wiphys, calibration `[SUCC]` both
      bands, no `.omo-off`, vendor stack loaded, consistent pair/selector state);
- [ ] the image container's identity and hashes recorded (inspection output); the image
      is the intended build;
- [ ] `STATUS.md` names the delivery plan (a fire without a written plan is forbidden);
- [ ] the cost in section 8 is accepted: a successful boot may require physical recovery
      to leave the unit again;
- [ ] signed: operator name / date / image sha256: ______________________

During: run the steps in section 6 in order; do not skip the inspection; never run two
installs at once; on any refusal or failure, stop and read the printed recovery plan
before touching the box. Never warm-reboot after a handover you want to cancel.

After: record the boot result from the UART capture (our image markers vs stock fallback),
then decide: keep the written pair, or recover physically. Update the parent ledger,
`STATUS.md` and the EVIDENCE-INDEX the same session.

## 11. What is [U] / [I]

Everything hardware-side of this path is unexecuted. The concrete open items:

| # | unknown | why it matters | closing experiment |
| --- | --- | --- | --- |
| U1 | Does the stock loader accept our generated trailer on hardware? | the whole image | one attended first-install boot with UART capture |
| U2 | Does the loader take the env mismatch/failover path exactly as decoded (re-stamp, save, boot the written pair)? | the handover | the same boot; expected lines in section 7 |
| U3 | Does our image's recorder run and record the running pair (and at what time)? | stability across reboots | userspace console/log in the same boot |
| U4 | Does the rootfs mount, and does the boot-argument hand-off work? | userspace reachability | console/dmesg in the same boot |
| U5 | Read-back reliability of both slots under the degraded NAND's ECC/bad blocks | write integrity | the full-region hashes of the first attended write |
| U6 | The env block's exact re-stamp timing/copy (primary vs redundant) | reading the console, not the install | capture the Erasing at 0x280000/0x240000 lines |
| U7 | Whether a partially written inactive pair affects the loader's fallback (it should not: it is not selected) | retry cost | a second attended write after a deliberate failure if needed |
| U8 | The physical recovery restores stock after our image ran | the only way back in R1 | recovery is a project-verified procedure (parent BENCH-FACTS 4); the *after our image* case is [U] until exercised |
| U9 | Whether the recorder-less trial-boot variant (section 8) is worth building | safety of the first trial | design decision after this install's evidence |

## 12. Validation record (host-only)

Executed 2026-11-18, host only, no device/router/UART/TFTP:

- `bash tools/tests/first-install.test.sh` - **144/144 assertions pass** against a
  fixture tree (fake `/proc/mtd`, fake `/sys`, fake mtd devices, shims for
  `mtd`/`ubiformat`/`sync`/`reboot`/`fw_printenv`/`dmesg`/`df`, fake
  rsh/rscp, synthetic sysupgrade container). It exercises: read-only inspection and
  dry-run; the attended/recovery/image gates; pair selection both directions (A -> writes
  B, B -> writes A); kernel-before-rootfs write order; both full-region read-backs; active
  pair/rootfs_data/upgrade/env devices untouched; image refusals (tampered manifest,
  wrong board, oversize member, external check refusal) before any device contact;
  device-state refusals (custom marker, wrong kernel, wrong slot sizes, selector/booted
  pair mismatch, manual `0x21`, missing sysenv interface, little `/tmp`); read-back and
  bad-block failure paths (selector untouched, recovery plan printed, no reboot); env
  never written; reboot classification (stock fallback / no reboot / unverified / target
  response).
- The same harness ran the **real built container**
  (6,185,262 B sha256 `362ca4ec...`) through the real
  `luofu-image.py check-container` in a dry-run: **PASS**, no write. The tool also
  independently re-derived the container identity, member magics, manifest hashes and
  gate offset from the same bytes.
- `bash -n tools/first-install-stock.sh` clean; the embedded device script passes
  `sh -n`; the device-side code is POSIX sh (busybox-ash compatible by construction,
  [I] until run on the box).
- Local test log: parent `build/first-install/first-install-tests-<timestamp>.log`
  (gitignored local evidence; /tmp is not evidence).

Proven host-side: the design is encoded in a fail-closed tool, every refusal and ordering
rule above is exercised, no flash write happens without the explicit attended flags, the
selector handling matches the measured semantics, and a real image container passes the
validation the installer relies on.

Not proven: **any hardware behaviour**. No write, no boot, no selector move, no recovery.

## 13. Evidence map

| claim | evidence |
| --- | --- |
| stock slot table, sizes, env blocks | TARGET-DESIGN.md 3.3; parent `dumps/router-20261108/full/proc_mtd.txt`; parent BENCH-FACTS.md 5 |
| selector cell/record/failover semantics | [SELECTOR-0x21.md](SELECTOR-0x21.md) 3.2/3.3/4.4 (loader disassembly + read-only captures) |
| low-byte store, vendor sysfs interface, boot_done | SELECTOR-0x21.md 4.6/5.2; vendor `hi_ksysenv.ko` |
| env layout crc32-LE / flags / data | SELECTOR-0x21.md 1.2 + appendix B |
| measured vendor-root install recipe (mtd + ubiformat) | FLASH-PLAN.md 0.1-0.3 results and "Slot switch recipe" |
| image container members/validation | SKELETON-SMOKE.md image-recipe/selector-fix sessions; [../target-skeleton/README.md](../target-skeleton/README.md); `tools/luofu-image.py` |
| current image artifact size/hashes | SKELETON-SMOKE.md selector-fix session (6,185,262 B sha256 `362ca4ec...`) |
| recovery is physical (RESET counter + TFTP) | parent BENCH-FACTS.md 4; parent RUNBOOK.md 6; `tools/recovery-card.sh` |
| UART is read-only | parent BENCH-FACTS.md 3; parent LESSONS.md 17 |
| no wired/Wi-Fi network in the current target image | TARGET-DESIGN.md 8.1/8.3; SKELETON-SMOKE.md |
| tool + mocked tests | parent `tools/first-install-stock.sh`; parent `tools/tests/first-install.test.sh`; test log above |

Regeneration commands (host-only, read-only):

    bash tools/tests/first-install.test.sh
    bash tools/first-install-stock.sh --inspect --image <built sysupgrade.bin>   # needs the box, read-only
    opensource/target-skeleton/tools/luofu-image.py check-container <image>

*End of document. Host-only design; no device was touched. Any execution of this path
requires a written delivery plan, the recovery door armed, a read-only console capture and
a human at the bench - never an unattended write.*
