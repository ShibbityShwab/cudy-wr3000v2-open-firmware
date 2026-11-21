# PUBLISH-PREP - public-release strategy, blockers, and non-destructive plans

**Status: NOT READY (release quality). Blocker B0 stands: the legacy open-firmware repository is
public and its ten vendor TR6560 BSP object files are downloadable from it right now.**

**Updated:** 2026-11-18 (original review); 2026-11-21 (purge runbook + hygiene re-audit; see the update block below).

**Update 2026-11-21 - history-purge runbook prepared and dry-run validated; the purge is still OPEN and owner-only.**

- Live legacy refs re-audited 2026-11-21: `master` = `5b401c1230d416ccd0bfe21b102963b60798cc2a` (tree clean, 0 vendor objects) and `omo/phase22-hccaccept` = `15703f5ee222b898191ccd90ae1fcb0e24c179b5` (tree still serves all ten `.o` files; raw URL HTTP 200 `[V]`). Full history (final 2026-11-21 snapshot): 795 commits; eleven binary blobs - the ten vendor `.o` plus one accidental Python-bytecode blob (`target-skeleton/tools/__pycache__/fip-slice.cpython-314.pyc`, found in the re-audit and now included in the recommended purge set). `PUBLISH-READINESS.md` item 1 remains unchecked, correctly.
- Execution procedure: [PUBLISH-PURGE-RUNBOOK.md](PUBLISH-PURGE-RUNBOOK.md) - copy-paste phases P0-P9 with explicit HOLD/ABORT points, verification gates V1-V9, branch delete-or-rewrite options, force-with-lease push, fresh-clone verification, GitHub Support cache purge, rollback and evidence checklist. It was **dry-run validated on 2026-11-21** in throwaway `/tmp` clones only (the final pass used the eleven-path set against the current refs; the master tip advanced from `0c82cb8...` to `6e35c29...` and then `5b401c12...` during the audit, so P0 re-audits live refs and updates the expected values if they move again): the rewrite preserved all 795 commits and all non-vendor content, all eleven old blob ids became unreachable and absent after reflog expire + gc, both branch options were exercised against local bare remotes, and fresh clones verified clean. **No push, remote change, branch operation, cache purge or commit occurred.**
- Still owner-only and still blocking B0/B1: force-push `master`, branch delete-or-rewrite, GitHub Support cache purge. B0/B1 close only after that runs and a fresh clone of the public remote verifies clean.
- Publication-hygiene re-audit (2026-11-21): root notebook HEAD at the re-audit snapshot `c6a75be447c481363b800cc3561f7e7e72edcff9` (the root HEAD has advanced since; re-check before citing) has 0 tracked binary files and 0 private-key/token/literal-password patterns `[V]`, but carries 12 host-path lines (username paths) and 12 real-MAC occurrences in ledger/evidence pages - a policy regression against PUBLISH.md's "HEAD sanitized" claim; redaction is the notebook lane's action, not this page's. Curated `cudy-wr3000v2-openwrt` `4e2f256` (41 commits, 0 binary blobs anywhere, no `/home/` paths, 110 parent-relative `build/linux-kbuild` evidence references, `LICENSE-MIT` + `LICENSE-GPL-2.0` + `NOTICE.md`; `PROVENANCE.md`/`CONTRIBUTING.md` not yet present) and `cudy-wr3000v2-cve` `9b1e7d1` (1 commit, 13 files, 0 binaries, no secrets or host paths, MIT) are clean for their purpose `[V]`.

**Companions:** `PUBLISH-READINESS.md` (the legacy checklist), `GPL-REQUEST.md`,
`../tools/publish-prep-check.sh` (read-only checker). This page is about public
releases; it proposes **no device step** - the router, UART, TFTP and the fire lane are not
involved anywhere below, and the bench stays idle with fires stopped.

**Evidence grades:** `[V]` verified by a command output or a fetched public record in this
review; `[I]` inferred from verified facts; `[U]` unknown / not verifiable from here;
`[X]` refuted. Re-run the audit before acting; this is a live-state page.

**Nothing in this document has been executed.** No rewrite, push, branch deletion, tag, commit,
repository creation, visibility change or repository deletion. `publish-prep-check.sh` is
read-only and is the only artefact from this package that has run.

---

## 0. TL;DR

1. **The exposure finding stands (B0) [V].** The legacy repository
   `ShibbityShwab/cudy-wr3000v2-open-firmware` has been public since 2026-09-30. Its public
   `master` tip (`7ff9242`) and its second branch `omo/phase22-hccaccept` both contain
   the ten vendor `.o` files, and all 694 public commits are reachable. Removing them from a
   local HEAD (as the working clone did in `fbd4a72`) changed nothing on the public surface.
   The root notebook `ShibbityShwab/cudy-wr3000v2` is also public (since 2026-10-16) and its
   public root history carries vendor binaries too: two vendor `.ko` plus vendor-derived Lua
   bytecode; its HEAD still tracks four vendor-derived `.lua` bytecode files (section 5) [V].
2. **Owner decision (2026-11-18): publish the novel work as fresh, curated public repositories.**
   No history rewrite is needed for that path. The curated repositories are
   `cudy-wr3000v2-openwrt` (the clean port work) and `cudy-wr3000v2-cve` (the security
   research). They are new repositories with new, clean histories, born with their license,
   provenance and disclosure files in the first commit.
3. **The legacy repositories are then superseded, not necessarily rewritten.** The
   open-firmware repository is archived read-only (or made private) after the curated repositories
   exist; the root notebook stays public, its tracked vendor-derived Lua is removed from HEAD and
   its stale "private research material" wording is reconciled. History purge/containment is kept
   as an **appendix** for later cleanup, not executed now.
4. **No destructive action in this task.** The recommended path creates new content; it does not
   touch the legacy refs. Force-pushes, filter-repo, reflog/gc, branch deletion, visibility
   changes, repository deletion and GitHub Support requests are all owner-only actions, listed in
   section 9.
5. The checker is delivered and was run on the current submodule (section 8): it fails on the
   expected legacy blockers and on the two cosmetic malformed links; those failures are documented,
   not patched away.

---

## 1. What was audited, and how to reproduce it

Read-only; network only for public GitHub/CVE metadata. The local submodule is a **shallow clone**
(27 visible commits, boundaries `a0091e7c` and `a2d4c8a`) while the legacy public history
has **694 commits**, so the full-history figures come from a throw-away full clone outside the
repository [V].

```sh
git -C opensource remote -v
git -C opensource ls-remote origin
git -C opensource rev-parse --is-shallow-repository          # true
cat .git/modules/opensource/shallow

git clone --bare https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git /tmp/pub-audit.git
git -C /tmp/pub-audit.git rev-list --count --all              # 694
git -C /tmp/pub-audit.git rev-list --objects --all | grep -E '\.(o|ko)( |$)'    # 10
git -C /tmp/pub-audit.git for-each-ref --format='%(refname) %(objectname:short)'

git -C ~/Documents/GitHub/cudy-wr3000v2 remote -v
git -C ~/Documents/GitHub/cudy-wr3000v2 ls-files patched tools/vendor-mods
```

### 1.1 Public surface at audit time [V]

| repository | visibility | created | pushed | refs | notes |
| --- | --- | --- | --- | --- | --- |
| `cudy-wr3000v2-open-firmware` (legacy submodule remote) | **public** | 2026-09-30 | 2026-11-14 | `master` = `7ff9242`; `omo/phase22-hccaccept` = `15703f5`; 0 tags; 0 releases; 0 PRs; 0 forks | 5 stars, 2 watchers; two public watch events (`omasanori` 2026-10-20, `longkoon88-mdy` 2026-11-14) |
| `cudy-wr3000v2` (root notebook) | **public** | 2026-10-16 | 2026-11-15 | `master` = `359ee59` on the remote; local 58 commits ahead; 0 tags/releases/PRs; 0 forks; 2 stars | no LICENSE/NOTICE at all; public submodule pointer `opensource` = `7ff9242` (contaminated commit) |
| curated repos | do not exist yet | - | - | - | names are free in the user's repository list [V] |

### 1.2 Legacy open-firmware history [V]

| metric | value |
| --- | --- |
| commits reachable from all refs | 694 (public `master` = 694; stale branch = 631, an ancestor of master) |
| refs | 2 branches, 0 tags; master not protected |
| reachable objects / unique blobs / blob bytes | 4,416 / 1,113 / 45,428,627 |
| **binary blobs** | **10 - all ten are the vendor `.o` objects in appendix A** |
| vendor objects introduced | `cb5c480`, 2026-10-09, "BSP prebuilt tri_fmc/tri_nand stack linked in" |
| vendor objects deleted in public history | **never** |
| commits with `Signed-off-by` | **0 of 694** |
| `.omo-secrets` paths / credential material | none / none found |
| large blobs | text only (largest `dumps/reg_all_run2.txt`, 709,133 B) |

### 1.3 Legacy root-notebook history and HEAD [V]

| metric | value |
| --- | --- |
| commits / refs | 698; `master` only; no tags |
| HEAD tracked binary content | **4 files: `patched/backup.lua`, `patched/rpc.lua`, `patched/servicectl.lua`, `patched/system.lua` (Lua 5.1 bytecode)**; no tracked `.ko/.o/.bin/.dtb` at HEAD |
| history binary blobs | **11**: 2 vendor `.ko` (`tools/vendor-mods/hi_kmdio.ko` 18,900 B, `hi_swa.ko` 11,432 B), 4 vendor-derived Lua bytecode files (`patched/*.lua`), 5 of our build outputs (`luofu-r116.dtb` and `crg.o`/`luofu.o`/`luofu-gmac.o`/`luofu-mdio.o` under `tools/lab-src/`) |
| credential material in any reachable blob | none found |
| device-unique identifiers in tracked text | yes - e.g. `d4:0d:ab:64:1c:73` in `mem-entries.md`, `10:ff:e0:c7:f0:26` in `PLAN-FULL-REVERSAL.md` and an archive page |
| licensing files at HEAD | none (no LICENSE, COPYING, NOTICE, CONTRIBUTING) |

The public root branch's submodule pointer is `7ff9242`, i.e. the contaminated legacy
open-firmware commit; the local root branch points at `f10ffec` (the local cleanup commit,
which is itself unpublished) [V].

### 1.4 CVE records (fetched 2026-11-18) [V]

| CVE | state | published | assigner | affected (CVE record) | references |
| --- | --- | --- | --- | --- | --- |
| CVE-2026-71960 | PUBLISHED | 2026-08-19T14:27:57Z | VulnCheck | Cudy WR3000 2.0, firmware < 2.5.24 (also lists P5 V-series products) | Cudy download centre page; `vulncheck.com/advisories/cudy-wr3000-hard-coded-jwt-secret-authentication-bypass-via-mqtt` |
| CVE-2026-71961 | PUBLISHED | 2026-08-19T14:28:45Z | VulnCheck | Cudy WR3000 2.0, firmware < 2.5.24 | Cudy download centre page; `vulncheck.com/advisories/cudy-wr3000-os-command-injection-via-mesh-mqtt-command-handler` |

The owner's MQTT/JWT writeup (root `gist-cudy-wr3000v2-mqtt-jwt.md`) is **not among the public
gists list** at audit time (the public systime gist is; the JWT writeup is not) [V], so the curated
CVE repository would publish that writeup for the first time. The writeup states the vendor was not
contacted by the owner and that it found no fixed release; the CVE records say "before 2.5.24".
That discrepancy is a disclosure-status fact to state, not to resolve by guessing [V]/[U].

---

## 2. Owner decision (2026-11-18) - the strategy this page follows

Recorded from the owner via the delegating agent:

| # | decision | consequence for this page |
| --- | --- | --- |
| D1 | The work is intentionally public (career/clout) and novel work should be published. | Publicity itself is accepted; the problem is quality/contamination of the legacy trees, not visibility. |
| D2 | Separate curated public repositories are approved: `cudy-wr3000v2-openwrt` (clean port) and `cudy-wr3000v2-cve` (security research). | Section 4 is the primary release path; fresh histories mean **no filter-repo is required**. |
| D3 | The existing `cudy-wr3000v2-open-firmware` repository is to be archived/superseded (or made private) after the curated repositories exist. | Section 5: archive/supersede is the default; history purge moves to appendix A, not executed. |
| D4 | The root `cudy-wr3000v2` notebook stays public, but vendor binaries must be removed from HEAD and the docs reconciled ("private research material" wording must change). | Section 5.3 lists the exact HEAD removals and wording corrections. |
| D5 | Keep a history-purge/containment appendix for the legacy repositories in case of later cleanup; no destructive action without the owner; do not rewrite history in this task. | Appendix A; section 9 approval table; no rewrite is proposed as the primary path. |

The legacy "not ready to publish" framing in `PUBLISH-READINESS.md` is therefore replaced by
a clearer statement: *the legacy trees are public but not release-quality; the release-quality
outputs are the curated repositories.*

---

## 3. Blockers (exact, with close conditions)

### B0 - legacy open-firmware repository is public with vendor object files [V]

- Public since 2026-09-30; vendor `.o` in the current public tree and in all history, on both
  branches; downloadable via the public API/raw URLs; two public watchers already observed.
- Close condition under the owner strategy: the curated repositories are live and verified; the
  legacy repository is archived read-only or made private; its description/README say it is
  superseded and that its history contains vendor binaries. Optional later: appendix-A purge.
- Action: sections 4 (curated repos), 5.1-5.2 (legacy handling), appendix A (later purge).

### B1 - the ten vendor `.o` blobs survive in the legacy full history [V]

- Appendix A lists blob ids and sizes; introduced in `cb5c480` (2026-10-09), never deleted in
  public history; reachable from `master` and from `omo/phase22-hccaccept`.
- Close condition (if the legacy repo is ever cleaned rather than archived): fresh full clone,
  appendix-A rewrite, the ten blob ids absent from all history and from the object database, both
  branches handled, remote verified, caches purged. **Not executed in this task.**
- Action: appendix A only.

### B2 - license split / DCO / provenance are incomplete everywhere [V]

- Legacy open-firmware: MIT `LICENSE` only; no `COPYING`; 42 of 108 `lab/` files
  without SPDX; 52 of 55 C/H/S files without Copyright; no `CONTRIBUTING.md`; 0/694 commits
  signed off; `PROVENANCE.md` absent.
- Root notebook: **no license files at all** [V].
- Close condition: **curated repositories are born with** `LICENSE` (MIT) + `COPYING`
  (GPL-2.0) + `CONTRIBUTING.md` (DCO/SPDX) + `PROVENANCE.md` + `NOTICE.md` and
  clean histories; the openwrt curated repo's copied kernel sources get the SPDX/copyright header
  pass before the first commit; the DCO policy applies to the fresh repositories from commit one.
- Action: sections 4 and 6.

### B3 - provenance and scope statements must travel with the curated repos [V]

- The legacy README/NOTICE/cover letter already say "GPL-2.0 derived works, not clean-room" [V], but
  the root README still calls the upstream track "clean-room drivers" [V] and the root instructions
  still call the root repository "private research material" [V] - both stale and contradicted by
  the public reality.
- Close condition: curated repos ship `PROVENANCE.md` and a scope statement; the root notebook's
  wording is reconciled in HEAD (section 5.3).
- Action: sections 5.3 and 6.2-6.3.

### B4 - Wi-Fi / LAN-switch licensing statement [V]

- The legacy README/NOTICE have no dedicated statement (the checker fails it); the curated openwrt
  repo must state vendor-only Wi-Fi/switch status and that no complete open port is claimed.
- Close condition: statement committed in the curated repo README/NOTICE (draft in section 6.4).
- Action: sections 4/6.4.

### B5 - GPL source-request evidence gap (owner decision) [V]

- `docs/GPL-REQUEST.md` holds the sent record, dated no-reply tracking table and NOTICE
  cross-reference; the sent copy/receipt is absent. The checklist's "dated no-reply record"
  alternative is met textually; the gap is evidence, not a code change.
- Close condition: either store the sent message/receipt (or its private path) and update the
  table, or record the dated-record acceptance. Applies to both curated repos (copy the record in).
- Action: section 7, item 3.

### B6 - content selection: what must NOT be copied into the curated repos [V]

- Exclude every vendor binary/object/firmware blob, calibration data, the fixed 40,732-byte
  vendor-derived trailer, vendor-derived Lua bytecode (`patched/*.lua`), and the three legacy
  vendor-derived text remnants (`lab/luofu-kernel/nand/triductor/Makefile`,
  `.../nfc_tr6560/Makefile`, `lab/luofu-kernel/nand/parsers/ofpart_triductor.h`) unless
  rewritten with provenance.
- Exclude device-unique identifiers: the real MACs `d4:0d:ab:64:1c:73` and
  `10:ff:e0:c7:f0:26`, the UPnP UUID, and anything else that identifies the owner's unit.
- Exclude live secrets/tokens/passwords and any re-runnable token for the owner's broker; the
  writeup already uses placeholders and the PoC scripts take the secret as a CLI argument [V].
- Close condition: content manifest (section 4.2/4.3) is implemented and the checker finds no
  vendor binary, secret, or unexpectedly device-unique content.
- Action: sections 4.2, 4.3, 6.5.

### B7 - honest scope for the curated openwrt repo [V]

- The port work is host-validated, **never booted on hardware**; several drivers are refusing
  drafts; there is no working upstream target/DTS accepted by OpenWrt; the internal switch has no
  driver; the Wi-Fi stack is vendor-only. The curated README must say this in the first screen.
- Reproducibility: `lab/luofu-kernel/kernel-tree.patch` publishes the machine delta,
  decompressor breadcrumbs and UBI compat=5 change; the private-mirror module_alloc/JFFS2/initramfs
  deltas are not published; the FIP trailer generator is structural and never booted; four `.dts`
  copies exist (single-source decision needed) [V].
- Close condition: the curated repo's status section is accurate; the single-source DTS choice is
  made; the unreproducible private deltas are either published or explicitly out of scope.
- Action: sections 4.2 and 7 item 6/7/8.

### B8 - legacy/public-surface hygiene [V/U]

- Root notebook HEAD still tracks 4 vendor-derived Lua bytecode files; its history has 2 vendor
  `.ko` and 4 Lua blobs; its text has device-unique MACs; its docs contradict the public
  reality; its public submodule pointer is the contaminated commit.
- The legacy open-firmware repository description says "No vendor binaries" while its tree has
  ten [V]; update the description after the curated repos exist.
- GitHub caches: archive/private does not delete old objects; cached views and third-party clones
  persist. Record as accepted residual risk (appendix A.3) [I].
- Close condition: section 5.1-5.3 actions recorded as done; descriptions/wikis/artifacts audited.
- Action: section 5.

---

## 4. Recommended path: fresh curated public repositories (primary plan)

Two new repositories, new history, no rewrite of anything that exists:

| repo | purpose | licence | first content |
| --- | --- | --- | --- |
| `cudy-wr3000v2-openwrt` | the clean port: design documents, target skeleton, kernel patch series, upstream submission series, tooling | MIT (docs/tools) + GPL-2.0 (drivers/patches), split stated in README/NOTICE | curated subset of the local `opensource/` tree (manifest in 4.2) |
| `cudy-wr3000v2-cve` | security research: CVE-2026-71960 / CVE-2026-71961 writeups, PoCs, disclosure status | MIT | curated subset of the root notebook's security material (manifest in 4.3) |

The names are free in the owner's repository list at audit time [V]. Repository creation, push and
any content publication are owner actions; an agent may prepare the local content and run the
verification, but must not create or push repositories without an explicit, dated instruction.

### C0 - owner decisions before creating anything [OWNER]

1. Confirm the two repository names and visibility (public from birth is intended; if the owner
   prefers "create private, verify, make public", that is one settings change per repo).
2. Confirm the licence split (MIT docs/tools, GPL-2.0-only kernel code) and the DCO policy
   (section 6.2/6.3). Curated repos start with `git commit -s`, so the DCO question of the
   legacy history does not arise for them.
3. Confirm the disclosure posture for the CVE repo: both CVEs are PUBLISHED (2026-08-19) [V];
   the owner's MQTT writeup is not yet public [V]; the writeup says the vendor was not contacted.
   Decide whether to include the systime finding (separate, no CVE, already public as a gist, fixed
   in 2.5.24 per the writeup) as an extra section or leave it to the gist.
4. Confirm that the curated openwrt repo may reference the legacy gists/repos by URL only - no
   copied vendor content.

### C1 - prepare a curated content tree outside any git repository [agent-safe]

Create two staging directories under `build/publish-prep/` (parent private tree, gitignored;
`/tmp` is not evidence) and copy only from the local working trees. This step touches no
device and no remote.

```sh
mkdir -p build/publish-prep/curated-openwrt build/publish-prep/curated-cve
# copy the subsets listed in 4.2 / 4.3, then apply the cleanups in section 6
```

### C2 - curated openwrt repository: content manifest (include / exclude)

Include (from local `opensource/`, branch `f10ffec` unless noted):

| path in curated repo | source | why |
| --- | --- | --- |
| `README.md`, `LICENSE`, `COPYING`, `NOTICE.md`, `CONTRIBUTING.md`, `PROVENANCE.md` | new (section 6 drafts) | release-facing licensing/provenance/status |
| `docs/TARGET-DESIGN.md` | `opensource/docs/TARGET-DESIGN.md` | target/DTS/image design |
| `docs/ROADMAP-OPENWRT.md`, `docs/TARGET-SKELETON.md` | same | phase plan / skeleton index |
| `docs/REMAINING-DRIVERS.md`, `docs/LSW-SPEC.md`, `docs/PIE-DATAPATH.md`, `docs/MISC-SOC-BLOCKS.md`, `docs/SMP-RE.md`, `docs/SELECTOR-0x21.md`, `docs/SKELETON-SMOKE.md` | same | the technical design layer |
| `docs/PUBLISH-READINESS.md`, `docs/PUBLISH-PREP.md` | same (this task) | publication status is part of the release |
| `docs/GPL-REQUEST.md` | same | provenance/licensing record |
| `target-skeleton/**` | same | schematic target, DTS, patches 001-019, image recipe, FIP tools |
| `upstream/series-6.18/**` | same | review-form kernel submission series (19 patches + messages + validation) |
| `lab/luofu-kernel/` (source + patch + fragments + drivers), `lab/luofu-*/` drivers as selected | same | published kernel code (SPDX/copyright pass first - section 6.2/6.3) |
| `tools/publish-prep-check.sh` and any tools referenced by the included docs | same | the checker travels with the release |

Exclude from the curated openwrt repo:

- every `.o/.ko/.a/.so`, the fixed 40,732-byte vendor trailer, firmware blobs, calibration data,
  `dumps/` register captures unless deliberately kept (owner decision B6);
- the three vendor-derived text remnants unless rewritten with a provenance note;
- the legacy phase-by-phase lab experiment directories (`lab/hello`, `lab/wifidrv*`,
  `lab/hccaccept`, ... ) that are bench instruments, not port work - keep them in the legacy
  repo only;
- every `docs/phase*/` file that is not referenced by the curated docs (they are already public in
  the legacy repo; copying all 285 files may re-import vendor-derived details and makes the curated
  repo unfocused);
- any device-unique MAC, UUID, credential or private path.

Minimum required `docs/` links in the curated README: `TARGET-DESIGN.md`,
`REMAINING-DRIVERS.md`, `ROADMAP-OPENWRT.md`, `PUBLISH-READINESS.md`,
`PUBLISH-PREP.md`, and the upstream series README.

### C3 - curated CVE repository: content manifest (include / exclude)

Include (from the root notebook, local branch; adapt filenames and dates):

| path | source | why |
| --- | --- | --- |
| `README.md` | new (section 6.6 draft) | CVE table, status, responsible-use statement |
| `SECURITY.md` | new (section 6.7 draft) | disclosure policy and contact/status |
| `docs/CVE-2026-71960.md` | rework of `gist-cudy-wr3000v2-mqtt-jwt.md` (JWT half) | published CVE writeup, placeholders only |
| `docs/CVE-2026-71961.md` | rework of `gist-cudy-wr3000v2-mqtt-jwt.md` (mesh command half) | published CVE writeup, placeholders only |
| `docs/system-time-command-injection.md` (optional) | `gist-cudy-wr3000v2-systime-rce.md` and `opensource/docs/systime-rce-writeup.md` | separate finding, no CVE, already public; include only if owner wants a complete research record |
| `tools/` PoCs (sanitised): `mqtt_takeover.py`, `forge_jwt.py`, `decrypt_bdinfo.py`, `extract_firmware.py`, `luadump.py` | root `tools/` | reproduction of the published research; input-only secrets |
| `LICENSE`, `NOTICE.md`, `CONTRIBUTING.md` | new (section 6) | MIT; provenance note (independently verified against public firmware; CVE records by VulnCheck) |
| `docs/VERIFICATION.md` | new | firmware versions tested, hash placeholders/values already public (plugin sha256 is in the gist), scope [V] |

Exclude from the curated CVE repo:

- any actual fleet secret, forged token, admin password, per-unit device id or live host/port
  arrangement; the writeups already use placeholders and the tools take the secret as an argument
  [V]; keep it that way;
- the owner's device-unique MACs and the local bench IP addresses where they would break the
  placeholder rule; the writeup already uses generic `192.168.10.1` - that is a vendor default
  and acceptable, but state it is the default;
- vendor `.lua` bytecode, vendor `.ko`, firmware images, dumps, calibration;
- any exploit that is fully weaponised beyond the published CVE scope; the PoC default mode is
  passive and `--cmd` only runs against owned hardware [V] - keep those guards and document them.

Disclosure facts to state in the curated repo (all [V] unless marked):

| fact | value |
| --- | --- |
| CVE records | CVE-2026-71960 and CVE-2026-71961, PUBLISHED 2026-08-19 by VulnCheck |
| CVE references | Cudy download-centre page + VulnCheck advisory pages (URLs in section 1.4) |
| affected versions per CVE record | Cudy WR3000 2.0, firmware < 2.5.24 |
| owner's verification | 2.4.15-20251030-114751 (R116), own unit, 2026-11-08 |
| owner's vendor contact | none (per the writeup) |
| fix evidence held by owner | none for the MQTT issues; writeup says "treat as unpatched until the vendor says otherwise" |
| relationship to the VulnCheck advisories | independent verification; assignment history [U] |

### C4 - create and seed the repositories [OWNER]

Because these are new repositories, there is no history to rewrite: create them via the GitHub UI
(or `gh repo create`), then push the prepared tree. Recommended: one signed initial commit
per repo (`git commit -s`) with the whole curated tree, so the fresh history is exactly the
published content; a second commit for any later curated addition. No vendor binaries enter any
commit - verify before the first push (C5).

### C5 - verify before the first push (agent-safe, read-only)

Run on the curated staging clone (a local git repo created in the staging directory):

```sh
cd build/publish-prep/curated-openwrt
git init -q && git add -A && git commit -q -s -m "curated port: design, target skeleton, patch series, tooling"
# vendor-object / binary scan (before any push)
git rev-list --objects --all | grep -E '\.(o|ko|a|so|bin|img|dtb|elf|ubi|squashfs)$'    # expect empty
git rev-list --objects --all | grep -E '^[0-9a-f]+ ' | wc -l
# the checker: required files, SPDX, links, secrets, history (expect PASS)
cp ../opensource/tools/publish-prep-check.sh tools/
./tools/publish-prep-check.sh
# explicitly: no device-unique MACs beyond placeholders, no secrets
git grep -nE '([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}' -- '*.md' | head
```

For the CVE repo, run the same checker and read the N/A sections as documented in section 8: there
is no `lab/` kernel tree, no DTS and no target image, so section H is N/A and sections B/part
of C apply only to the MIT text; the load-bearing checks are A (files/licence), E (no binaries in
history), F (no secrets) and G (links). Add `SECURITY.md` and the CVE writeups before the
first commit so required-file checks are meaningful.

### C6 - publish [OWNER]

Push the initial commit(s), set the repository description (section 6.8 templates), and only then
announce. The checker must PASS on a fresh clone of each new remote before the announcement; record
the run in the private release log.

### C7 - legacy handling after the curated repos are live (section 5)

Archive/supersede `cudy-wr3000v2-open-firmware` (GitHub Settings -> Archive is reversible;
private is also possible), point its description/README at the curated repos and at this page,
remove vendor-derived content from the root notebook's HEAD, reconcile the root docs, update the
root submodule pointer only when the legacy repository's fate is final. No history rewrite.

### C8 - records

Private release log (section 10): owner decisions, staging trees + hashes, checker logs, first-push
transcripts, fresh-clone verification, archived legacy state, residual risk (old public copies and
caches). Append a parent-repo ledger entry and regenerate `STATUS.md` when this is actually
executed, per the project convention.

---

## 5. Legacy repositories: handling now, containment appendix for later

Owner decision D3/D4: archive/supersede the legacy open-firmware repository after the curated
repositories exist; keep the root notebook public but fix its HEAD and wording. **No history
rewrite now.** Appendix A keeps the full purge procedure in case the owner later wants the legacy
repositories cleaned rather than archived.

### 5.1 `cudy-wr3000v2-open-firmware` (legacy submodule remote) - archive/supersede

Order (owner actions, after the curated openwrt repo is live and verified):

1. Update the legacy repository README (top block) and description to say it is **superseded** by
   `cudy-wr3000v2-openwrt` and that its public history still contains ten vendor TR6560 BSP
   object files from 2026-10-09 until/unless appendix A is executed. Link this page.
2. Archive the repository (Settings -> Archive this repository) or make it private. Archive is
   reversible and keeps the historical record; private removes it from anonymous view but old
   objects/caches may persist either way [I].
3. Do **not** force-push or rewrite anything unless the owner later chooses appendix A. Archiving
   leaves the vendor objects reachable to anyone who already fetched them; record that as residual
   risk (section 10).
4. Keep the local submodule working clone as the development source; after the curated repo exists,
   point the root notebook's last published submodule pointer at a sensible state when the legacy
   repository's fate is final (owner action; the public pointer currently resolves to `7ff9242`,
   a contaminated commit [V]).
5. Remove the stale claim in the legacy description ("No vendor binaries, no credentials") - it is
   false while the history is unpurged [V].

### 5.2 Root notebook `cudy-wr3000v2` - stay public, fix HEAD and wording

Current facts [V]: public since 2026-10-16; 698 commits; no licence files; HEAD tracks **four
vendor-derived Lua 5.1 bytecode files** (`patched/backup.lua`, `patched/rpc.lua`,
`patched/servicectl.lua`, `patched/system.lua`); HEAD tracks no `.ko/.o/.dtb`;
history contains 11 binary blobs (section 1.3); tracked text contains device-unique MACs; public
submodule pointer = `7ff9242`; local branch is 58 commits ahead of the public branch.

Minimum HEAD cleanup (normal commits, owner/agent with approval - not done by this task):

| # | action | exact paths/decision |
| --- | --- | --- |
| 1 | remove the four vendor-derived Lua bytecode files from HEAD; keep them in the private evidence tree | `git rm --cached` the four `patched/*.lua` files, extend `.gitignore` with `patched/*.lua`, keep `patched/AGENTS.md` (text) |
| 2 | keep vendor `.ko` untracked (already true [V]) | confirm `tools/vendor-mods/README.md` documents the local-fixture rule; the ignore entry already exists |
| 3 | add a root `LICENSE` + `NOTICE.md` + `PROVENANCE.md` (drafts in section 6) | state MIT for the notebook's own text, GPL-2.0 for included driver sources, provenance from vendor GPL binaries, no clean-room claim |
| 4 | reconcile wording | `AGENTS.md` "The root repo is private research material and must stay private ..." -> "The root repo is a public research notebook; release-quality outputs live in the curated repositories and the legacy opensource submodule must not be cited as release-ready until PUBLISH-PREP closes it"; `EVIDENCE-INDEX.md` "Publish boundary (2026-11-17) ... root repo ... must not be published" -> public-reality + curated-repo pointer; `README.md` "clean-room drivers" -> "reverse-engineered, GPL-2.0-compatible drivers (see PROVENANCE)" |
| 5 | redact device-unique identifiers in HEAD | replace `d4:0d:ab:64:1c:73` (mem-entries) and `10:ff:e0:c7:f0:26` (PLAN-FULL-REVERSAL + archive page) with placeholders; keep a private note of the real values |
| 6 | ignore build objects globally | add `*.o`, `*.ko`, `*.mod`, `*.mod.c`, `*.cmd`, `Module.symvers`, `modules.order` to the root `.gitignore` |
| 7 | link the curated repos from the root README when they are live | one "publications" block at the top |

Optional later (appendix A.2): rewrite the root history to remove the 11 binary blobs and scrub
MACs. Not required for the "keep public, fix HEAD" decision; the residual is documented.

### 5.3 What "docs reconciled" means (exact sentences to change)

- `AGENTS.md`: the "private research material" paragraph (quoted above) is contradicted by
  the public repository; replace it.
- `EVIDENCE-INDEX.md` section "Publish boundary (2026-11-17)": replace the private-repo
  premise with the public-notebook + curated-repo strategy and point at this page.
- `README.md`: "clean-room drivers" and "not on track as a submission" remain roughly true,
  but "clean-room" must be replaced by the provenance wording (GPL-2.0 reverse engineering).
- `STATUS.md` and `CURRENT-TRUTHS.md`: any "published half NOT READY for publication"
  sentence should become "legacy tree public but not release-quality; curated repositories are the
  release path (PUBLISH-PREP)".
- `PUBLISH-READINESS.md`: apply the correction block in section 6.9.

---

## 6. File drafts (where they go, exact proposed text)

These are drafts for the owner to review and apply. They are written for the **curated
repositories**; the legacy trees get only the minimal HEAD/wording fixes in section 5. Nothing here
was created by this review except this page and the checker.

### 6.1 `COPYING` - GPL-2.0 license text (repository root of both curated repos that contain kernel code)

The curated openwrt repo contains GPL-2.0 kernel code, so it must carry the GPL-2.0 text from its
first commit. The CVE repo contains no kernel code (MIT only), but including `COPYING` is cheap
and avoids a future split. Use the canonical text verbatim:

```sh
curl -fsSL https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt -o COPYING
head -3 COPYING
grep -c 'Copyright (C) 1989, 1991 Free Software Foundation, Inc.' COPYING   # expect 1
sha256sum COPYING      # record in the private release log; do not commit a .sha256 file
```

Expected `head -3`:
first line is "                    GNU GENERAL PUBLIC LICENSE", second is
"                       Version 2, June 1991". If the workstation is offline, copy the text from a
trusted local distro package (Debian/Ubuntu `/usr/share/common-licenses/GPL-2`; it was absent
on this workstation at review time [V]) and verify the same header lines. Never paraphrase or
shorten the license text.

### 6.2 `CONTRIBUTING.md` (curated repos)

```markdown
# Contributing

## License of contributions

This repository is deliberately split: scripts, tools and documentation are MIT (see LICENSE);
kernel driver code, kernel patches and the target/DT tree are GPL-2.0-only (see COPYING, SPDX
headers in every source file). Derived material states its provenance in PROVENANCE.md.

Every new source file starts with an SPDX identifier and a copyright line:

    C/H/S:     /* SPDX-License-Identifier: GPL-2.0-only */   (or //)
    Makefile:  # SPDX-License-Identifier: GPL-2.0-only
    Kconfig:   # SPDX-License-Identifier: GPL-2.0-only

## Sign-off (Developer Certificate of Origin)

Every commit must carry a sign-off, added with "git commit -s":

    Signed-off-by: Real Legal Name <email@example.com>

By signing off you certify the Developer Certificate of Origin 1.1
(https://developercertificate.org): that you created the contribution or have the right to submit
it under this repository's licenses, and that you understand it is public and recorded. Do not use
a tool identity in a Signed-off-by trailer; automation-authored commits are reviewed and signed off
by the human maintainer who commits them.

## Provenance

The driver and target work is derived by reverse engineering the vendor's GPL-2.0-declared
binaries and devicetrees; this is not a clean-room project. Do not submit vendor code, binary
objects, firmware blobs, calibration data or device-unique identifiers as your own work. Describe
any dependency on such material in PROVENANCE.md first.

## Before you push

    tools/publish-prep-check.sh          # read-only; must PASS for a release branch
```

### 6.3 `PROVENANCE.md` (curated repos)

```markdown
# Provenance

This repository is a reverse-engineering and reimplementation project for the Cudy WR3000 V2.0
(HiSilicon Hi5671Y "luofu", board R116). It is **not** a clean-room implementation and does not
claim to be one.

## What was derived, and from what

The kernel drivers, DTS conversions and patch series were derived by reverse engineering the
vendor's GPL-2.0-declared kernel modules, the vendor devicetree and the vendor userspace behaviour
shipped on the device: static disassembly, symbol/string analysis, DWARF and container structure
decoding, and read-only live register/console observation. The vendor modules declare
"license=GPL" in their modinfo; a GPLv2 section 3 source request was sent on 2026-10-01 and no reply
is held (see docs/GPL-REQUEST.md).

Derived artefacts include the lab/ kernel drivers and build material, the target-skeleton patch
series, the FIP/trailer logic, and the target/userland glue. They are GPL-2.0-only derived works
with attribution to the vendor binaries they were derived from.

## What is not included

No vendor firmware image, kernel module, .o/.ko object, Wi-Fi firmware blob, calibration data,
device-unique identifier or credential is included. The recipes operate on material the user
extracts from their own device.

## License position

Our own scripts, tools and documentation are MIT. The derived kernel code and patch series are
GPL-2.0-only. See LICENSE, COPYING and NOTICE.md.
```

### 6.4 `NOTICE.md` / `README.md` additions for the curated openwrt repo

Scope statement (the B4 requirement), fitted to a curated release:

```markdown
## Wi-Fi and LAN-switch status (scope statement, 2026-11-18)

The Wi-Fi stack and the internal LAN switch are **vendor-only today**. The vendor's Wi-Fi driver
stack ships as GPL-2.0-declared binaries with no published source, the Wi-Fi firmware blob has no
known redistribution licence, the internal switch has no open driver in this tree, and the GPLv2
section 3 source request (sent 2026-10-01) is unanswered (record: docs/GPL-REQUEST.md). This
repository therefore does not contain, and does not claim, a complete open port of the device. All
driver and image status claims are labelled in docs/TARGET-DESIGN.md,
docs/REMAINING-DRIVERS.md and docs/ROADMAP-OPENWRT.md.

## Status

Host-validated only, and the hosts are not routers: the target image chain builds (make world
RC=0 in an isolated OpenWrt clone), the kernel patch series compiles for 6.18, and several drivers
are deliberately refusing drafts. **Nothing here has booted on hardware, been flashed, or been
accepted by OpenWrt.** The legacy repository's history contained vendor object files; this curated
repository is built from source and documentation only and carries no vendor binary content.
```

README top block (curated openwrt):

```markdown
> **Status: host-validated research, not a working OpenWrt target and not an upstream submission.**
> The target/DTS/image design, the schematic target skeleton, the 6.18 kernel patch series and the
> review-form submission series are here. Nothing has booted on hardware; Wi-Fi and the internal
> LAN switch remain vendor-only. Licensing: MIT (docs/tools) + GPL-2.0-only (kernel code);
> provenance: [PROVENANCE.md](PROVENANCE.md); scope: [NOTICE.md](NOTICE.md).
```

### 6.5 `.gitignore` for both curated repos

```gitignore
# never publish build objects or vendor material
*.o
*.ko
*.a
*.so
*.mod
*.mod.c
*.cmd
Module.symvers
modules.order
*.bin
*.img
*.dtb
*.ubi
*.squashfs
```

### 6.6 `README.md` draft for the curated CVE repo

```markdown
# Cudy WR3000 v2.0 - MQTT security research (CVE-2026-71960, CVE-2026-71961)

Independent security research against the Cudy WR3000 V2.0 router (HiSilicon Hi5671Y "luofu",
firmware 2.4.15-20251030-114751, board R116), verified on hardware owned by the researcher.

| CVE | issue | status |
| --- | --- | --- |
| CVE-2026-71960 | hard-coded JWT HMAC secret in the Mosquitto auth plugin, broker authentication bypass | PUBLISHED 2026-08-19 (VulnCheck advisory) |
| CVE-2026-71961 | unsanitised mesh MQTT command payload, root command execution | PUBLISHED 2026-08-19 (VulnCheck advisory) |
| - | authenticated command injection in the LuCI system-time page (no CVE; fixed in 2.5.24 per the writeup) | published as a gist; optional extra here |

What is here: technical writeups, non-destructive PoCs that take the fleet secret as an input
argument, verification notes, and the disclosure status as of the dates in docs/DISCLOSURE.md.
What is **not** here: any fleet secret, forged token, admin password, per-unit device id, vendor
firmware or calibration data, or vendor binaries of any kind. The PoCs are for hardware you own;
the writeups keep the exploitation details at the level already published in the CVE records and
advisories.

Licence: MIT. See NOTICE.md, PROVENANCE.md and SECURITY.md.
```

### 6.7 `SECURITY.md` / `docs/DISCLOSURE.md` for the curated CVE repo

```markdown
# Security policy and disclosure status

## Status (2026-11-18)

- CVE-2026-71960 (hard-coded JWT HMAC secret, CWE-798): PUBLISHED 2026-08-19; assigned by
  VulnCheck; affected product per the record: Cudy WR3000 2.0 firmware below 2.5.24.
- CVE-2026-71961 (OS command injection via the mesh MQTT command handler, authenticated):
  PUBLISHED 2026-08-19; same assigner and affected range.
- These writeups are independent verification on 2.4.15. The owner has not contacted the vendor;
  the writeup states no fixed release was evidenced for the MQTT issues. The CVE records say
  "before 2.5.24"; that difference is recorded, not resolved by guessing.
- The separate system-time command injection was fixed in 2.5.24 per the published gist; it has no
  CVE identifier.

## Reporting issues in this repository

Open an issue for documentation/tooling problems. For a new device vulnerability, contact the
vendor and a CNA; this repository publishes post-disclosure research, not a live vulnerability
disclosure channel.

## Responsible use

The PoCs are written for hardware you own and are non-destructive by default. Do not use them on
networks or devices you do not own or have written permission to test. Keep the radio and network
regulations of your jurisdiction.
```

### 6.8 GitHub repository descriptions (owner settings)

- openwrt curated: `Research port of OpenWrt/Linux for the Cudy WR3000 v2.0 (HiSilicon Hi5671Y luofu): target/DTS/image design, 6.18 kernel patch series and review-form submission series. Host-validated, never booted; Wi-Fi and LAN switch remain vendor-only. See README and PROVENANCE.`
- CVE curated: `Independent security research on the Cudy WR3000 v2.0: CVE-2026-71960 (hard-coded JWT secret, MQTT auth bypass) and CVE-2026-71961 (mesh MQTT command injection). Published writeups, non-destructive PoCs, disclosure status.`
- legacy open-firmware (after curated repos exist): `SUPERSEDED by ShibbityShwab/cudy-wr3000v2-openwrt. Historical research tree, public but not release-quality; its history contains vendor TR6560 BSP object files (see PUBLISH-PREP). Archived.`

### 6.9 Correction block for `docs/PUBLISH-READINESS.md` (legacy tree; apply if it stays public as a record)

```markdown
## Update 2026-11-18 (strategy correction; state corrected)

The origin repository has been **public since 2026-09-30**; ten vendor TR6560 BSP .o files are
reachable in the public master and omo/phase22-hccaccept trees and in all 694 commits of history.
They were introduced in commit cb5c480 (2026-10-09), not in "root commit a2d4c8a" (the true root is
adc77a3; a2d4c8a is the shallow local clone's boundary). The 2026-11-17 untracking commit in the
local working clone is not pushed. This repository is therefore a public legacy tree, not a
pre-publication tree.

Owner decision 2026-11-18: the release path is two fresh curated repositories,
cudy-wr3000v2-openwrt and cudy-wr3000v2-cve; this legacy repository is superseded/archived after
they exist, and no history rewrite is performed as part of that path. The full strategy, blockers,
curated-repo manifests, file drafts and the optional legacy purge procedure are in
PUBLISH-PREP.md.
```

### 6.10 Root notebook wording changes (section 5.3) - proposed exact replacements

```diff
- The published half is the opensource/ submodule. The root repo is private research
- material and must stay private until the publish-boundary cleanup in
- EVIDENCE-INDEX.md is done (it tracks vendor-derived binaries/Lua and dumps).
+ The opensource/ submodule is the legacy published half. The root repository is a public
+ research notebook; release-quality outputs live in the curated repositories
+ (cudy-wr3000v2-openwrt, cudy-wr3000v2-cve). The legacy submodule's history still contains
+ vendor object files and must not be cited as release-ready until opensource/docs/PUBLISH-PREP.md
+ says so.
```

```diff
- 2. **Upstream OpenWrt submission** ... own target, clean-room drivers,
+ 2. **Upstream OpenWrt submission** ... own target, reverse-engineered GPL-2.0-compatible drivers,
```

---

## 7. Pre-publication checklist (mapped to PUBLISH-READINESS)

Two gates: (G1) curated repositories; (G2) legacy-state honesty. PUBLISH-READINESS's nine items are
re-mapped because its premise ("before this repository is made public") is replaced by the curated
path (section 2).

| # | requirement | curated-repo gate (G1) | legacy state (G2) | verify |
| --- | --- | --- | --- | --- |
| 0 | vendor objects not on a release surface | curated repos contain no vendor object/bytecode/firmware blob; fresh history has none | legacy history still has 10 blobs + root history 11; legacy archived/superseded (or appendix-A purge) | fresh-clone scan + checker [E] |
| 1 | history purge on the legacy repo | not required for curated repos (new histories) | owner-only runbook prepared and dry-run validated (see PUBLISH-PURGE-RUNBOOK.md); if executed, ten blob ids absent from all history, both branches, remote verified | runbook phases P5-P7 |
| 2 | no other vendor binaries/blobs/calibration | curated content manifest excludes them; no patched Lua, no dumps unless decided | root HEAD: remove 4 Lua bytecode files; dumps decision recorded | checker [E]; manifest review |
| 3 | GPL request sent/reply evidence | copy docs/GPL-REQUEST.md into curated openwrt repo; either add sent copy/receipt or record dated acceptance | same record stays | checker [D] |
| 4 | license split + DCO | curated repos born with LICENSE + COPYING + CONTRIBUTING + SPDX/copyright headers; all commits signed off | legacy: record as historical limitation (0/694 sign-offs); not retroactively fixed unless appendix A | checker [A] [B] |
| 5 | provenance statement | PROVENANCE.md in both curated repos; README/NOTICE wording | root wording reconciled (section 5.3) | checker [C] |
| 6 | reproducible artifact | openwrt curated repo: kernel-tree.patch, target-skeleton tools, honest statement of what is NOT reproducible (module_alloc/JFFS2/initramfs deltas, never-booted trailer) | legacy docs corrected as in 6.9 | checker [H]; TRAILER-FORMAT review |
| 7 | single-source DTS | choose one DTS in the curated repo; archive/regen the others | four copies remain in the legacy tree (record only) | checker [H] |
| 8 | target/linux tree | curated repo carries the target-skeleton tree, labelled SCHEMATIC/UNTESTED | legacy tree keeps it as historical record | checker [H]; target-skeleton/README |
| 9 | Wi-Fi/LAN-switch statement | NOTICE/README statement in the curated openwrt repo; scope statement in README | legacy NOTICE updated if it stays public | checker [C] |
| 10 | records/rollback | this page + release log | residual risk recorded | section 10 |

Publication of the curated repositories is justified only when G1 is clean (checker PASS on a fresh
clone of each new repository, with N/A sections documented) and G2 actions are either done or
explicitly deferred by the owner.

---

## 8. `publish-prep-check.sh` - usage and recorded output

**Recorded-output note:** the block below is verbatim except the single `repo:` line, where the local absolute path is replaced by `<repo-root>` for publication hygiene.

```sh
tools/publish-prep-check.sh [--strict] [--no-history] [--no-links]
```

Read-only by construction: no file created/modified/deleted, no network, no git command that writes
(no gc/reflog/fetch/commit/tag/config-set/push/filter-repo), output to stdout only. Exit 0 when
there is no FAIL, 1 on FAIL (or WARN under `--strict`), 2 on usage/not-a-repo. Sections:
A files/license texts; B SPDX/copyright/DCO; C provenance + Wi-Fi/LAN-switch statement; D GPL
request evidence; E full git-history scan (vendor objects, binary blobs, large blobs, per-ref and
tag audit, unreachable objects); F secrets (tree + every reachable blob); G markdown links;
H items 6-8 artefact presence. It skips the history scan under `--no-history` and says
explicitly that this is not valid for a release decision.

**Run against the current legacy submodule at 2026-11-18 19:20 +07 (12:20Z), HEAD `f10ffec`.**
Expected failures are the documented legacy blockers; the walk-through of each line follows the
block.

```
publish-prep-check - read-only publish-readiness check (dry run)
repo: <repo-root>/opensource  # local absolute path redacted in this copy
HEAD: f10ffec  branch: master
origin: https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git
time: 2026-11-18T12:21:41Z
mode: DRY RUN - no file is written, no network, no push, no git mutation
INFO  visible history: 27 commits reachable from 3 refs (shallow=true)
WARN  worktree is dirty; a release candidate must be a clean checkout (git status)
FAIL  shallow clone: the history scan cannot see commits before a0091e7caa91958be8b2285d552fb7bbf3ae1c33 a2d4c8aaab01ba4f57045966be97dd0615e7e2f0 ; run 'git fetch --unshallow' (network, owner) before trusting any PASS in section E
WARN  fetch refspec is narrow (+refs/heads/master:refs/remotes/origin/master); remote branches other than the tracked one are invisible to this offline audit - see git ls-remote in PUBLISH-PREP appendix B
WARN  HEAD is 24 commit(s) ahead of origin/master and 0 behind: publish plan must account for unpushed work (curated path: PUBLISH-PREP section 4; legacy purge: appendix A)

== A. required release files and license texts ==
FAIL  missing required publish files: COPYING CONTRIBUTING.md (see docs/PUBLISH-PREP.md section 3, blocker B2)
PASS  LICENSE is the MIT text
FAIL  COPYING (GPL-2.0 license text) missing: the lab/ kernel code is GPL-2.0 but the repository only carries MIT text (PUBLISH-READINESS item 4)
WARN  NOTICE.md does not reference: docs/PUBLISH-PREP.md (link them once the files are in the tree)
PASS  README.md points at the publish checklist

== B. license split: SPDX headers, per-file copyright, DCO ==
FAIL  42 of 108 lab/ kernel-source files lack SPDX-License-Identifier (first: lab/abiprobe/Makefile lab/barmap/Makefile lab/barsnap/Makefile ) - PUBLISH-READINESS item 4
PASS  SPDX ids under lab/ are GPL-2.0/GPL-2.0-only
FAIL  52 of 55 lab/ C/H/S files have no Copyright line (PUBLISH-READINESS item 4; add the standard header from CONTRIBUTING.md)
FAIL  CONTRIBUTING.md (DCO + SPDX + provenance convention) missing (PUBLISH-READINESS item 4)
FAIL  27 of 27 commits lack a Signed-off-by trailer (DCO evidence; PUBLISH-READINESS item 4). Policy: PUBLISH-PREP section 6.2 (curated repos) and section 5.2 (legacy noted limitation)
PASS  README.md states the MIT / GPL-2.0 split

== C. provenance and Wi-Fi/LAN-switch statements ==
PASS  provenance wording present in README.md/NOTICE.md (GPL-2.0 reverse engineering, not clean-room)
WARN  no dedicated PROVENANCE.md / lab/PROVENANCE.md; the statement exists only inside README/NOTICE (draft in PUBLISH-PREP section 6.3)
PASS  no un-negated 'clean-room' claim in README/NOTICE
FAIL  missing Wi-Fi + LAN-switch status statement in README.md/NOTICE.md (PUBLISH-READINESS item 9: vendor-only today, firmware-blob license unresolved, GPL source request unanswered; the submission cannot claim a complete open port)

== D. GPL source request evidence ==
PASS  docs/GPL-REQUEST.md records the request as sent
PASS  dated no-reply record present in docs/GPL-REQUEST.md
WARN  no sent copy / delivery receipt is stored; only the dated record exists. The checklist's dated-record alternative is met; the owner decides whether to store the sent message (or its private path) - PUBLISH-PREP blocker B5
PASS  NOTICE.md cross-references the GPL request record

== E. git history: vendor objects, binaries, refs, tags ==
FAIL  vendor object blobs reachable from refs: 10 lab/luofu-kernel/nand/parsers/ofpart_triductor.o | lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_hal_nand.o | lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand.o | lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_bbt.o | lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_check.o | lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_drv_yyxxxx.o | lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_mtd.o | lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_spi_nand_drv.o | lab/luofu-kernel/nand/triductor/tri_fmc.o | lab/luofu-kernel/nand/triductor/tri_mtd_parts.o (PUBLISH-READINESS item 1; purge required for the legacy repo; PUBLISH-PREP appendix A)
PASS  no .bin/.img/.fw/.dtb/.elf/.ubi/.cal/.key-style paths in any history
FAIL  ref refs/heads/master reaches 10 vendor .o/.ko blob path(s)
FAIL  ref refs/remotes/origin/HEAD reaches 10 vendor .o/.ko blob path(s)
FAIL  ref refs/remotes/origin/master reaches 10 vendor .o/.ko blob path(s)
PASS  no tags exist (nothing to re-point)
FAIL  all-history binary-blob count is 10; every one is a vendor .o path listed above
PASS  no high-confidence credential pattern in any reachable blob
PASS  no blob larger than 1 MiB in reachable history
WARN  vendor-derived build glue/header still tracked: lab/luofu-kernel/nand/parsers/ofpart_triductor.h | lab/luofu-kernel/nand/triductor/Makefile | lab/luofu-kernel/nand/triductor/nfc_tr6560/Makefile (remove or rewrite with a provenance note; PUBLISH-PREP blocker B6)
INFO  history scan: 27 reachable commits visible from 3 refs; 1217 objects, 804 blobs
WARN  59 unreachable object(s) in the local object database; a purge leaves old objects here until reflog expiry + gc (owner-approved; PUBLISH-PREP appendix A). They are not reachable from refs.

== F. secrets: tracked key material and current-tree patterns ==
PASS  no tracked private-key / credential filenames
PASS  no high-confidence credential pattern in the tracked tree
PASS  no quoted literal password assignment in the tree (env-var references are expected and allowed)

== G. relative markdown links ==
FAIL  2 broken relative markdown link(s) in 2 file(s): docs/phase17/ete-engine.md: handle, ... | docs/phase20/rx-loop.md: ete, bit

== H. reproducible artifact / DTS / target-tree presence (items 6-8) ==
PASS  kernel delta patch published (lab/luofu-kernel/kernel-tree.patch)
PASS  trailer/slice tool present (target-skeleton/tools/fip-slice.py; generator is structural, never booted)
FAIL  4 tracked .dts copies (docs/soc/luofu-r116-pinned.dts docs/soc/luofu-r116.dts lab/luofu-kernel/dts/luofu-r116.dts target-skeleton/dts/luofu-r116.dts): PUBLISH-READINESS item 7 requires one upstream-style single source
PASS  schematic target/linux tree present (target-skeleton/target/linux/hisilicon/)
WARN  target tree is SCHEMATIC/UNTESTED and host-build only (target-skeleton/README.md); it is not an upstream target/linux/luofu/ submission
WARN  device profile file is not active (only profiles/cudy_wr3000-v2.mk.unused); the image device stanza lives in image/luofu.mk - decide what upstream keeps

== summary ==
PASS=15 FAIL=8 WARN=9
PUBLISH-PREP-CHECK: FAIL
mode: DRY RUN - nothing was written, fetched, pushed or rewritten.
blocker map / owner-approval gates / release sequence: docs/PUBLISH-PREP.md
```

Reading the recorded result:

- `FAIL shallow clone`: the offline scan sees 27 of 694 public commits; re-run in a fresh
  full clone. This is also why the legacy PUBLISH-READINESS text thought `a2d4c8a` was the root.
- `FAIL missing required publish files: COPYING CONTRIBUTING.md`: B2. This page exists, so only
  the licence/DCO files are missing on the current tree.
- `FAIL 42 of 108 ... SPDX` / `52 of 55 ... Copyright` / `CONTRIBUTING.md missing` /
  `27 of 27 commits lack Signed-off-by`: B2.
- `FAIL missing Wi-Fi + LAN-switch status statement`: B4.
- `FAIL vendor object blobs reachable from refs: 10` and the three `FAIL ref ...` lines:
  B1; expected on the legacy tree, must be absent on the curated repos.
- `FAIL all-history binary-blob count is 10`: same ten B1 objects (no other binary exists).
- `FAIL 2 broken relative markdown links`: the two malformed pseudo-links listed in B6;
  cosmetic fixes, not a release blocker for the curated repos (do fix them in the legacy HEAD).
- `WARN` lines are advisory: no PROVENANCE.md (B3), sent-copy gap (B5), vendor text remnants
  (B6), narrow fetch refspec / unpushed commits / unreachable objects (local clone state), schematic
  target tree, inactive profile.
- On a curated repository: section H is N/A for the CVE repo (no kernel/DTS/target tree) and
  sections B/H are N/A where no kernel code is copied; A, C (provenance/scope), E (no binary),
  F (secrets) and G (links) are the load-bearing ones. The checker is deliberately generic - do
  not add per-repo special cases to make it green; record N/A in the release log instead.

---

## 9. What must not be executed without explicit owner approval

None of these was executed by this review.

| action | why | phase |
| --- | --- | --- |
| creating `cudy-wr3000v2-openwrt` / `cudy-wr3000v2-cve` or pushing to them | public publication in the owner's name | C4/C6 |
| archiving or making private the legacy open-firmware repo | public-surface change | 5.1 |
| any repository deletion/recreation | irreversible; loses history/stars | appendix A (not recommended now) |
| `git filter-repo` / `git filter-branch` on a legacy repo | rewrites history | appendix A |
| `git reflog expire` / `git gc --prune=now` | removes the last local copy of pre-purge objects | appendix A |
| `git push --force-with-lease` / `git push origin --delete` | replaces/deletes public refs | appendix A |
| GitHub Support cached-view purge request | outbound request in the owner's name | appendix A.3 |
| `git rm` / commit of the four vendored Lua files and the MAC redactions in the root notebook | changes the public root tree | 5.2 |
| editing AGENTS.md / EVIDENCE-INDEX.md / README.md wording | root-repo instructions live in other agents' scope; owner applies | 5.3 |
| Actions artifact deletion / workflow disable / description updates | public metadata and CI | 5.1, 6.8 |
| sending the GPL follow-up / vendor contact | human-only (GPL-REQUEST.md) | - |
| any network download in the checker path | the checker is offline by design | - |

Not on the list because it is **not in this plan at all**: any router/UART/TFTP/fire action. The
publication path is host-only; the box remains stock and idle and the bench-fire stop stands.

---

## 10. Records, rollback and residual risk

Record privately (parent `build/publish-prep/` is gitignored, or an offline directory):
owner decision text and date; staging-tree contents and hashes; checker before/after logs; first-push
transcripts; fresh-clone verification of each curated repo; archive/private action confirmation for
the legacy repo; root HEAD cleanup commit ids; the accepted residual-risk statement. Append a dated
parent-ledger entry and regenerate `STATUS.md` when the plan is executed.

Rollback for the curated path is trivial: a new repository can be deleted or its content reverted
before or after publication; nothing in the legacy repositories was rewritten. No vendor material
was ever placed in a curated repository, so there is no contaminated history to re-clean.

Residual risk (accepted, recorded): the legacy open-firmware history has been public since 2026-10-09
and may already have been downloaded by the five stargazers/two watchers or automated crawlers; the
root history since 2026-10-16; archiving/private does not retract copies, forks (0), search-engine or
archive caches, or GitHub cached views; the 0-fork/0-PR state limits the GitHub-side spread [V].
The owner should keep that sentence in any public statement about this cleanup - it is honest, and
it does not overclaim erasure.

---

## 11. Owner decisions still needed

1. Confirm repository names/visibility/creation order and who creates them (C0).
2. Confirm the curated openwrt content manifest (C2): include the schematic target tree and which
   lab drivers; keep or drop register dumps; single-source DTS choice.
3. Confirm the curated CVE content manifest (C3): include the no-CVE system-time finding? include the
   PoC tools? which disclosure-status wording.
4. Confirm licence/DCO posture for the curated repos (6.2/6.3; signed-off initial commit).
5. Confirm the legacy handling order (archive vs private; description text; root HEAD cleanup timing;
   root submodule pointer update timing).
6. Confirm whether the legacy purge (appendix A) is ever wanted; default is archive/supersede.

---

## Appendix B - audit evidence and reproducibility

Commands that produced the numbers in section 1 (read-only; network reads only for public
metadata):

```sh
# public surface
git -C opensource remote -v
git -C opensource ls-remote origin
curl -s https://api.github.com/repos/ShibbityShwab/cudy-wr3000v2-open-firmware | head -c 400
curl -s https://api.github.com/repos/ShibbityShwab/cudy-wr3000v2 | head -c 400

# legacy full history (throw-away clone; /tmp is not evidence)
git clone --bare https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git /tmp/pub-audit.git
git -C /tmp/pub-audit.git rev-list --count --all
git -C /tmp/pub-audit.git rev-list --objects --all | grep -E '\.(o|ko)$'
git -C /tmp/pub-audit.git for-each-ref --format='%(refname) %(objectname:short)'

# root notebook history/HEAD
git -C ~/Documents/GitHub/cudy-wr3000v2 rev-list --count --all
git -C ~/Documents/GitHub/cudy-wr3000v2 ls-files patched tools/vendor-mods
git -C ~/Documents/GitHub/cudy-wr3000v2 rev-list --objects --all \
  | grep -Ei '\.(o|ko|bin|img|elf|a|so|dtb|ubi|squashfs)$'

# CVE records (public JSON)
curl -s https://cveawg.mitre.org/api/cve/CVE-2026-71960
curl -s https://cveawg.mitre.org/api/cve/CVE-2026-71961

# checker (recorded run in section 8)
cd opensource && ./tools/publish-prep-check.sh
```

## Appendix C - why the local clone is not an oracle

The local submodule is shallow with boundaries `a0091e7c` and `a2d4c8a`; it shows 27
commits while the legacy public history has 694 [V]. Its `rev-list --objects --all` cannot
prove that the full public history is clean, and the "root commit `a2d4c8a`"
statement in PUBLISH-READINESS is an artifact of that boundary. The local branch is also 24 commits
ahead, which is where the 2026-11-17 untracking commit lives. Any purge audit and any "all
history" claim must be made from a fresh full clone, never from this working clone.

## Appendix D - the ten vendor blobs (legacy purge target set)

All ten are reachable from every current public ref of the legacy open-firmware repository; each is
an ARM ELF object from the TR6560 BSP build, with DWARF paths under
`/home1/develop/release/3.1.0-rc7/release_THG6500-TAX2_3.1.0-rc7/.../linux-5.10.138` [V].
Blob ids are Git SHA-1 object ids, i.e. exactly what the appendix-A verification compares.

| blob id | bytes | path |
| --- | --- | --- |
| `9338af226b58a7dc25d00114fca91e194484d32d` | 6,084 | `lab/luofu-kernel/nand/parsers/ofpart_triductor.o` |
| `3bea6e81f8605d36e90f04e281d83ee49d311f4b` | 13,892 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_hal_nand.o` |
| `2ccc6e8ae1c2961a4445ae1cb02d438d5afe1106` | 6,740 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_bbt.o` |
| `c26f09bd73c7dd3be271b2f2dbf3733a813f016a` | 13,364 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_check.o` |
| `5a9ffba570bb44444c11a493e5ec19858b8f9c1b` | 29,844 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_drv_yyxxxx.o` |
| `98dac5fe55aeb7088f03793ebb9d8473769aca42` | 38,652 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_mtd.o` |
| `e37cec2e70b83b758af6d98c8189d237885b43bb` | 36,036 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand.o` |
| `0b836d1b5d67e76ccfcd677e8683ae5fa27f9455` | 28,888 | `lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_spi_nand_drv.o` |
| `4f7b1fae2d1fb0b90ae2da426f03ce4812dc2247` | 30,592 | `lab/luofu-kernel/nand/triductor/tri_fmc.o` |
| `a97f4e69895991b7b6b7ecb3265ad8c7da3c5648` | 11,324 | `lab/luofu-kernel/nand/triductor/tri_mtd_parts.o` |

Always copy the ids from `vendor-objects-before.txt` generated in appendix A rather than from
prose.


---

## Appendix A - history purge / containment plan for the legacy repositories (not executed)

**Execution procedure:** [PUBLISH-PURGE-RUNBOOK.md](PUBLISH-PURGE-RUNBOOK.md) is the authoritative owner-run procedure (prepared and dry-run validated 2026-11-21). This appendix remains the strategy-level plan and has not been executed.

This is the procedure if the owner later decides the legacy repositories must be *cleaned*, not
just archived/superseded. It is destructive; it requires an explicit owner go and a verified
offline backup first. Nothing here was run in this task.

### A.1 Legacy open-firmware repository (10 vendor blobs, 694 commits, 2 branches)

**Backup (sensitive - contains the vendor objects; offline/private, never pushed):**

```sh
mkdir -p /offline/publish-prep-legacy-20261118
git clone --mirror https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git \
  /offline/publish-prep-legacy-20261118/public-mirror.git
git -C /offline/publish-prep-legacy-20261118/public-mirror.git bundle create \
  /offline/publish-prep-legacy-20261118/pre-purge-all.bundle --all
git -C /offline/publish-prep-legacy-20261118/public-mirror.git bundle verify \
  /offline/publish-prep-legacy-20261118/pre-purge-all.bundle
git -C /offline/publish-prep-legacy-20261118/public-mirror.git for-each-ref \
  --format='%(refname) %(objectname)' > /offline/publish-prep-legacy-20261118/refs-before.txt
git -C /offline/publish-prep-legacy-20261118/public-mirror.git rev-list --objects --all \
  | awk '/\.(o|ko)$/ {print $1, $2}' | sort -k2 \
  > /offline/publish-prep-legacy-20261118/vendor-objects-before.txt
```

**Rewrite (fresh full clone; filter-repo primary; exact ten paths):**

```sh
git clone --no-single-branch https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git \
  /offline/publish-prep-legacy-20261118/rewrite
cd /offline/publish-prep-legacy-20261118/rewrite
git filter-repo --force --invert-paths \
  --path lab/luofu-kernel/nand/parsers/ofpart_triductor.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_hal_nand.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_bbt.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_check.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_drv_yyxxxx.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_nand_mtd.o \
  --path lab/luofu-kernel/nand/triductor/nfc_tr6560/tri_spi_nand_drv.o \
  --path lab/luofu-kernel/nand/triductor/tri_fmc.o \
  --path lab/luofu-kernel/nand/triductor/tri_mtd_parts.o
# optional (owner decision, recommended): also remove the three vendor-derived text remnants
#   --path lab/luofu-kernel/nand/triductor/Makefile
#   --path lab/luofu-kernel/nand/triductor/nfc_tr6560/Makefile
#   --path lab/luofu-kernel/nand/parsers/ofpart_triductor.h
# fallback without filter-repo: git filter-branch --prune-empty --index-filter
#   'git rm --cached --ignore-unmatch -q <the same ten paths>' -- --all
```

**The load-bearing verification - "the ten vendor objects are gone from ALL history":**

```sh
cd /offline/publish-prep-legacy-20261118/rewrite
git rev-list --objects --all | grep -E '\.(o|ko)$'          # expect empty
# per-ref: every current ref must reach 0 vendor paths (master, the other branch, any tag)
for ref in $(git for-each-ref --format='%(refname)'); do
  echo "$ref $(git rev-list --objects "$ref" | grep -cE '\.(o|ko)$')"
done
# each of the ten old blob ids must be unreachable and then absent from the object database
cut -d' ' -f1 ../vendor-objects-before.txt > ../vendor-shas-before.txt
git rev-list --objects --all | awk '{print $1}' | sort -u > ../reachable-after.txt
comm -12 ../vendor-shas-before.txt ../reachable-after.txt    # expect empty
while read sha; do git cat-file -e "$sha" 2>/dev/null && echo "STILL PRESENT $sha"; done \
  < ../vendor-shas-before.txt                                 # expect empty after reflog expire + gc
# history shape: 694 commits, only the purged paths differ from the backup
git rev-list --count --all
git -C ../public-mirror.git diff 7ff9242 "$(git rev-parse master)" -- . \
  ':(exclude)lab/luofu-kernel/nand/triductor' ':(exclude)lab/luofu-kernel/nand/parsers'
# record the commit map, then clean local objects and force-push
cp .git/filter-repo/commit-map ../commit-map.txt
git reflog expire --expire=now --all && git gc --prune=now
git remote add origin https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git
git push --force-with-lease=refs/heads/master:7ff92424462730b09f1217c762c9694bccd92257 \
  origin refs/heads/master:refs/heads/master
git push origin --delete omo/phase22-hccaccept          # stale branch is a second reachable path
# rewrite it instead of deleting if it must survive:
#   NEW_B=$(grep '^15703f5ee222b898191ccd90ae1fcb0e24c179b5' ../commit-map.txt | cut -d' ' -f2)
#   git push --force origin "$NEW_B":refs/heads/omo/phase22-hccaccept
# verify from a fresh clone; then ask GitHub Support to purge cached views of old SHAs
```

### A.2 Root notebook (11 binary blobs in history) - optional purge

History binary blobs [V]: `tools/vendor-mods/hi_kmdio.ko`, `tools/vendor-mods/hi_swa.ko`,
`patched/backup.lua`, `patched/rpc.lua`, `patched/servicectl.lua`,
`patched/system.lua`, `tools/lab-src/arch/arm/boot/dts/luofu-r116.dtb`,
`tools/lab-src/arch/arm/mach-luofu/crg.o`, `tools/lab-src/arch/arm/mach-luofu/luofu.o`,
`tools/lab-src/drivers/net/ethernet/luofu/luofu-gmac.o`,
`tools/lab-src/drivers/net/ethernet/luofu/luofu-mdio.o`, plus the two `built-in.a`
files under the same directories. The vendor-sensitive subset is the two `.ko` and the four
vendor-derived Lua bytecode files; the rest are our build outputs (safer, but still not
reviewer-friendly in a public notebook).

If executed later: same backup/filter-repo/verification/force-push shape as A.1, with
`--invert-paths` over the paths above; a text replacement pass
(`--replace-text` mapping the two real MACs and the UPnP UUID to placeholders) for the
device-unique identifiers; then update the root's public submodule pointer (the `opensource`
gitlink) to the legacy repository's final state; verify the HEAD tree still differs from the goal
only in the intended ways (the 698-commit count becomes fewer only if `--prune-empty` drops
commits that became empty - record and justify any count change); do not rewrite tags because the
root has none [V].

### A.3 Both legacy repos - rule set

- The pre-purge backup contains vendor GPL object code. It stays offline/private; never committed,
  pushed, attached or shared.
- Re-run the "ten objects gone from ALL history" verification from a **fresh clone of the remote**,
  not from the rewrite clone, before declaring success.
- Force-push is `--force-with-lease` against the audited old SHA; delete/rewrite every branch
  first or in the same window.
- After the push, ask GitHub Support to purge cached views of the old commits/blobs; treat a
  persistent 200 on an old blob URL after the support reply as a failed cleanup.
- The archived legacy repository can simply stay archived; cleaning it is optional.

---
