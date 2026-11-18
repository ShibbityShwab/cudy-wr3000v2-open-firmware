# First-install lane - integration record (task-38, agent first-install)

Date: 2026-11-18. Host-only. **No device, router, UART, TFTP, flash or bench tool was
touched; nothing was booted or flashed; no install, selector or recovery claim is made.**

This directory is the incoming package for the first-install-from-stock lane
(TARGET-DESIGN.md section 5.5 option 2). It holds the shared-docs integration patches;
the design document is [`docs/FIRST-INSTALL.md`](../../../../docs/FIRST-INSTALL.md)
(delivered directly in the working tree), and the fail-closed tool + mocked harness live
in the parent (private) repository as
`tools/first-install-stock.sh` and `tools/tests/first-install.test.sh`.

## 1. What was delivered

| artifact | path | role | sha256 |
| --- | --- | --- | --- |
| first-install design doc | `opensource/docs/FIRST-INSTALL.md` (new, working tree) | preconditions, sequence, expected loader/UART output, rollback/recovery cost, risks, attended checklist, [U] list | `cab4f008d8518d6ec1f93cbce482c69488dd6274bfcef0bd94406be3c5fd5ac8` |
| fail-closed host tool | parent `tools/first-install-stock.sh` | read-only by default; writes only behind attended flags; pair selection by label; full-region read-backs; selector handover last; env/active/rootfs_data never written | `1aeb0b5051e150f701a90fe8b9374214d7b22a36c5171c4f7ba979836bde09f1` |
| mocked test harness | parent `tools/tests/first-install.test.sh` | fixture device tree + fake rsh/rscp + synthetic container; 144/144 assertions | `3e5d23928cdbe1df7e390ff7fdeaa49714c1a3e55148e860d9b00b84179ef6f3` |
| incoming shared patch | `docs/incoming/firstinstall/shared.patch` (this directory) | TARGET-DESIGN 5.5/8.4/12.3, opensource README, SKELETON-SMOKE session, TARGET-SKELETON index, target-skeleton README | `b26f6c83fb82211064818f6c066a026b2f68a2782550b4bac195aa17c222c273` |
| incoming parent patch | `docs/incoming/firstinstall/RUNBOOK.patch` (this directory) | parent `RUNBOOK.md` pointer section + `tools/AGENTS.md` WHERE-TO-LOOK row | `6f7b2a8225d544f15ac5e1bcbe54e4fc4580ce8ceeab1df57fe06cd2e04771e1` |
| this record | `docs/incoming/firstinstall/SUMMARY.md` | integration notes, validation results, [U] list | - |
| local test log | parent `build/first-install/first-install-tests-25691118T122208Z.log` (gitignored) | full 144-assertion run | `de897d0a645bafaf1f036a67cd2dc4d7bb063740bfab5733ebf53dfd1f4bdb0b` |

No vendor binaries, blobs, dumps or credentials are introduced; the patches are text only.

## 2. What the design establishes (summary; full detail in FIRST-INSTALL.md)

- **Path:** the measured vendor-root install (option 2 of TARGET-DESIGN 5.5): from a root
  shell on the stock firmware, write the kernel slice with `mtd` and the rootfs with
  `ubiformat` into the **inactive** pair, verify, hand over the selector, reboot.
  The stock TFTP recovery image's write path is closed/uncharacterized and is not used;
  physical RESET+TFTP stays the fallback.
- **Writes only the inactive pair**: kernel slot by `/proc/mtd` label (erase + write +
  full-slot sha256 read-back), rootfs slot by `ubiformat` (+ full-partition sha256
  read-back, bad-block report is a hard abort). The active pair, rootfs_data (mtd15),
  upgrade (mtd16), enva/envb (mtd3/mtd4) and all vendor data partitions are never written.
- **Handover uses only the measured selector semantics** (SELECTOR-0x21.md): the env
  `bootflag` selects the pair and the SYSCTRL cell is the loader's opposite-pair
  record. The tool writes the written pair's recorded low byte (A = 0x10, B = 0x11) into
  `/sys/devices/platform/sysenv/boot_reg` as the last device write, verifies a
  whole-cell read-back, verifies the env is unchanged, and syncs. **The env blocks are
  never written** - the loader's own mismatch path re-stamps them. **0x21 is not used**
  (it is the project's bench marker, not a vendor value, and never persists).
- **Expected boot (captured vocabulary):** `bootflag a bootreg 11` ->
  `boot from kernela failed, change to kernelb` -> env save lines ->
  `bootreg offset: c08` / `boot regval: 10` -> our uImage
  (`Image Name: Linux-...` + our Data Size) -> our kernel console. Only the stock
  4,294,096-byte image prints `Uncompressing Linux`.
- **Recovery cost, stated honestly:** if our image reaches userspace, its recorder makes the
  pair persistent and the current image has **no usable wired/Wi-Fi network**, so leaving it
  again needs the physical RESET+TFTP recovery. A trial-boot (recorder-inhibited) variant is
  proposed in the doc but **not implemented**. This must be accepted in the attended
  checklist before any reboot.
- **Risks and [U] list** are enumerated in FIRST-INSTALL.md sections 9-11 (novel-trailer
  acceptance, env re-stamp, first boot, rootfs mount/boot-args, NAND read-backs, recovery
  after our image).

## 3. The tool (parent private repository)

Modes: `--inspect` (default, read-only), `--write`, `--handover`,
`--reboot` (higher includes lower). Write modes refuse without `--attended` **and**
`--recovery-armed` **and** a validated `--image`; `--reboot` also requires an
armed read-only UART capture. The host checks bench-link/recovery-card, validates the
container with `luofu-image.py check-container` plus independent byte checks (identity,
member magics, manifest-vs-bytes, gate magic, slot budgets, PEB geometry), probes the stock
box read-only (stock identity/health, slot table/sizes, booted pair vs env bootflag, cell
record state, tools, /tmp space, fingerprints) and refuses on any mismatch. Every failure
prints the recovery plan plus the safe state; the reboot clause exits 3 with a
classification (stock fallback / no reboot / unverified) instead of claiming success.

Test evidence: `bash tools/tests/first-install.test.sh` -> **144/144 assertions pass**
(2026-11-18). Coverage includes pair selection both directions, kernel-before-rootfs write
order, both full read-backs, active-pair/rootfs_data/upgrade/env untouched, image
refusals before any device contact, device-state refusals, read-back/bad-block aborts with
selector untouched and recovery plan printed, env-never-written, and the reboot
classification. The same harness exercises the **real built container**
(6,185,262 B sha256 `362ca4ece4df2b77b0e527ad5d948bbd997201ccb3bf6e2518d5ddaccbb59899`)
through the real `luofu-image.py check-container` in a dry run: PASS, no write.

## 4. What shared.patch changes (apply from the opensource/ root)

Generated against opensource HEAD `f10ffec` (clk session). Verified with
`git apply --check` against the current HEAD `dd11d26` too (the publish
commit did not touch any of these files):

    cd opensource
    git apply target-skeleton/docs/incoming/firstinstall/shared.patch

| file | change |
| --- | --- |
| `docs/TARGET-DESIGN.md` | 5.5 status: first-install designed + implemented host-side (first-install doc + tool + tests, what it writes, never executed, [U] list); 8.4 install bullet updated; 12.3 open question updated (designed/implemented host-side, still [U] on hardware, TFTP recovery path still [U]) |
| `docs/SKELETON-SMOKE.md` | new "2026-11-18 (first-install session)" validation section (deliverables, tool behaviour, 144/144 tests, real-container dry-run, what proves/not) |
| `docs/TARGET-SKELETON.md` | first-install pointer in the remaining-[U] paragraph and the reading order |
| `docs/README.md` (opensource root) | design-layer blockquote gains the FIRST-INSTALL.md pointer |
| `target-skeleton/README.md` | new first-install status paragraph; "not done" sentence corrected (design/tooling exists, never executed); reading order gains FIRST-INSTALL.md |

Validation of the patch itself: `git apply --check` RC=0 in the current opensource
worktree; `patch -p1 --dry-run` RC=0 on a fresh `git archive` of the base tree;
after apply, only the five intended files differ from the base tree.

## 5. What RUNBOOK.patch changes (apply from the parent repo root)

    cd <parent repo root>
    git apply opensource/target-skeleton/docs/incoming/firstinstall/RUNBOOK.patch

| file | change |
| --- | --- |
| `RUNBOOK.md` | new section "First install from stock (host-side design; not a fire lane)" with pointers to `opensource/docs/FIRST-INSTALL.md` and `tools/first-install-stock.sh`, the write gates, and the statement that it has never run on hardware and a reboot is fire-class |
| `tools/AGENTS.md` | WHERE-TO-LOOK row for the first-install design + tool + mocked tests (read-only default; not a fire lane) |

Validated: `git apply --check` RC=0 against the current parent worktree;
`patch -p1 --dry-run` RC=0 on a copy of the two files.

## 6. What is NOT claimed / still [U]

- No device execution of any kind: no write, no selector move, no boot, no recovery.
- FIRST-INSTALL.md sections 8-11 are the authoritative [U] list. In one line: one attended
  end-to-end install (write -> handover -> first boot), the loader env re-stamp, the first
  boot of the written pair, rootfs mount/boot-argument hand-off, NAND read-back behaviour
  under the degraded flash, the recovery cost after a successful boot, and the
  recorder-inhibited trial-boot variant are all unmeasured.
- The tool requires the stock root shell over the project SSH transport (the post-recovery
  vendor image has no SSH until the key is provisioned); the inspect mode fails closed if
  the box is unreachable.
- Nothing here is an OpenWrt submission, a release or a flashable factory image; the image
  container still carries the NOT FLASHABLE provenance marker (this attended path is what
  would close it).

## 7. Integration notes for the lead

- `opensource/docs/FIRST-INSTALL.md` is a **new file already written in the
  working tree**; it is not inside shared.patch. Commit it with this package.
- No shared document was edited in place by this lane; both patches are independent and
  can be applied in either order (submodule patch first if committing one tree).
- Recommended next action: a **read-only** `tools/first-install-stock.sh --inspect`
  run is safe and useful as soon as the box is reachable (it stages nothing and writes no
  flash); the first `--write` is a fire-class attended bench step that needs
  `STATUS.md` to name the delivery plan, the recovery door armed and a read-only UART
  capture.
- Suggested follow-up (not part of this task): implement the recorder-inhibited trial-boot
  image variant so the first boot automatically falls back to stock on the next reset; and
  add the tool to any future gate list only after it has one attended execution.
