# GPL request and PD4 research-patch status (2026-11-27)

**What this file is.** The final, honest status of the two things that keep the Wi-Fi half out of
any upstream submission: the **unanswered GPLv2 source request**, and the **PD4 research firmware
patch**. It also records the decision tree and the exact statements a submission or publication
must (and must not) make. Host-only; no vendor bytes are carried here.

## 1. GPLv2 source request - status unchanged: sent, no reply, no receipt copy

| item | state |
| --- | --- |
| request sent | **2026-10-01** (recorded in the parent ledger, `PORT-SURFACE.md`, `PORT-PLAN.md`, and [../../GPL-REQUEST.md](../../GPL-REQUEST.md)) |
| channel | email to `support@cudy.com` (Cudy support/GPL compliance; contact facts verified 2026-11-18) |
| reply held | **none** as of **2026-11-27** |
| evidence in repository | **no sent copy, no delivery receipt, no reply** - a known evidence gap; the dated record exists |
| same-SoC obligor letter (Jeton) | separate **unsent draft** (`JETON-GPL-EMAIL.txt`, parent repo) |
| project decision (2026-11-18) | **not a project gate**; treat the vendor source as an optional accelerator, continue by reverse engineering; keep the page as provenance and a ready escalation lever |
| follow-up pack | prepared in [../../GPL-REQUEST.md](../../GPL-REQUEST.md) (canonical request text, tracking table, follow-up draft, contact facts) |

**Why the request is binding, not a courtesy.** The stock image ships 58 vendor kernel modules
under `/lib/hisilicon/ko` that declare `license=GPL` in their modinfo (e.g. `hi_kmac.ko`,
`hi_kmdio.ko`, `hi_kphy.ko`, `hi_lsw_pfe.ko`, `hi_swa.ko`, and the Wi-Fi stack
`hi5622v100_plat`/`hi5622v100_wifi`) and are linked against the Linux 5.10.201 kernel. Under
GPLv2 section 3, distributing them obliges the distributor to provide the corresponding source
to recipients. The request also asks for the kernel tree/board DTS, the GPL userspace components,
and a written statement on the licensing of the Wi-Fi firmware blob itself.

**What the absence of a reply means for us (and does not mean).**
* It does **not** block the project: everything here is interoperability research on hardware we
  own, and our own drivers are our own GPL-2.0 work (derived by reverse engineering - state that
  provenance, do not claim "clean-room" for transcribed code).
* It **does** block claiming a complete open port, and it leaves the Wi-Fi redistribution/
  support question unresolved: no vendor firmware, module or extracted table may be published.
* Do not describe the request as "draft, ready to send" - it was sent. The follow-up is the draft.

**Owner actions if escalation is ever wanted (not taken):** store the sent copy + receipt (or a
private path) in the tree and update the tracking table; re-send the follow-up; verify the
current intake route of a GPL compliance programme (the parent `TIMELINE-AND-WAY-FORWARD.md`
item 7 name is not a verified address); do not silently rewrite the original request.

## 2. PD4 - the research patch, and why it is not a submission artefact

**Necessity.** On this board the firmware's H2D mailbox consumer is reachable only through the
image's event-0x4C dispatcher path; event 0x4C cannot be raised from the host (the event-
controller/GIC window is not host-visible), and the unpatched image never polls the mailbox, so
the delivered vendor blob cannot service any host frame. **PD4** redirects the boot-task success
epilogue (file `0x0916A`, 4 bytes) into a 64-byte guarded mailbox-poll stub in a genuinely dead
static-data gap (CPU `0x108240`). The patched image (`cfe24bfb...`) differs from the pinned
vendor blob in exactly two windows, length unchanged 928,920 B, 68 patched bytes.

**Audit results** (full argument and harness: [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md);
`verify_pd4.py` 31 checks + 14/14 selftest, `pd4_variants.py` 14/14 - re-run this session):

* **Identity:** rebuilds byte-identically from the pinned blob. **Minimality:** one 4-byte branch +
  one 64-byte stub; no other byte differs.
* **Safety:** the stub cave (637-byte all-zero gap CPU `0x108197..0x108413`) is outside both boot
  memsets and the exception-staging frame, between two referenced read-only tables, with an
  exhaustive Thumb census finding zero references into it (0 hits). The stub only **reads**
  mailbox `out[0]` and calls the firmware's own wrapper -> dispatcher; every device write is the
  firmware's. The host rule "never write CA `0x400392F0`" is unchanged.
* **H1 settled (2026-11-25 21:30, read-only pack):** the handler-table pointer CPU `0x10C1B0` is
  the static image value `0x00118D68`, the live table at CPU `0x118D68` is **all zero** at 2 s
  and 48 s after release, and the sanity anchor is correct. Therefore the sentinel compare always
  takes the consume-only branch, **the firmware's mailbox dispatcher handlers never execute under
  PD4**, and the working duplex (calibration, VAP frames, AP start) is delivered by the
  **non-mailbox ETE/polled H2D path** with the mailbox ack/clear/doorbell as bookkeeping.
* **Vendor-clean alternatives:** none proven. The vendor mailbox sequence is already reproduced;
  ETE 0x2D/0x2E do not deliver; the ROM has no mailbox service; the image has no other out[0]
  reader. The one remaining host-only candidate is a spare **inbound iATU viewport onto device CA
  `0x40160000`** plus a one-shot raise of event 0x4C - a **new device-write class** (firmware-
  owned interrupt controller), never attempted, requiring its own attended design, recovery card
  and IRQ-storm bounds.

**Verdict:** PD4 is required for the closed vendor blob **as delivered** (conditional only on the
untested event-controller aperture experiment), and it is **not fundamental to the hardware**.
It is a **research instrument only** - never a shipping change, never in the kernel series,
never part of a target/image, and its patched image is never committed or redistributed. The
public artefact is the recipe + verifier + hashes, not firmware bytes.

## 3. The exact blockers (legal / technical) for the submission package

| blocker | class | status | consequence |
| --- | --- | --- | --- |
| vendor firmware blob is closed; source request unanswered | legal/licensing | open, outside kernel series | no upstream Wi-Fi driver; no redistribution of firmware; support claim impossible |
| PD4 needed for the H2D path on the vendor blob | technical | open (research artifact) | the working bench stack is not a shipping stack; a host-clean path is unproven |
| no upstreamable Wi-Fi driver | technical | open | Wi-Fi must be stated **out of scope** for the platform series |
| vendor MAC / no mainline mac80211 data path | technical | open | the reverse-engineered protocol is readiness evidence, not a driver |
| platform series "clean-room" wording | provenance/licensing | fix required before publication | say **GPL-2.0 provenance by reverse engineering of the vendor's GPL binaries**; do not say "clean-room" for transcribed code |
| our own kernel drivers' licence | legal | GPL-2.0 (SPDX + MODULE_LICENSE), **no COPYING/DCO yet** | publication checklist item ([PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md)) |
| history carries vendor `.o` files | publication | open, owner-only purge/curated repos | do not publish the legacy half as-is |

The platform kernel series itself contains only our own GPL-2.0 code and no vendor binary, so the
unanswered request does not block posting the **platform** series. It blocks every Wi-Fi claim
and the publication of the legacy repository.

## 4. Decision tree for a future maintainer

**Option A - GPL source arrives.** Re-send/escalate the request; if the vendor provides the
kernel/module/firmware sources, re-evaluate the Wi-Fi driver against the actual source (and the
firmware's licence statement). The reverse-engineering record remains the interoperability
evidence; attribution/licence compliance becomes the new constraint.

**Option B - host-clean boot path proven.** Run the event-controller aperture experiment
(attended, new device-write class). If it works, the unpatched vendor firmware can service host
frames and PD4 is no longer needed for bench work - **but** the firmware is still closed and
proprietary, so no distributor/release story changes; this only strengthens the research record.
**Option C - full clean-room firmware-free operation.** Only if a mainline mac80211 driver with
real data path and firmware replacement is built; not planned, not on the roadmap, do not imply it.
**Option D - ship the platform without Wi-Fi (the honest current path).** Submit/land the
platform target + drivers with Wi-Fi explicitly out of scope; keep the Wi-Fi work published as
research. This is the recommended path for the near term.

**Never do:** ship or publish a PD4-patched firmware; commit vendor firmware/modules/calibration/
ROM bytes; add the research driver or PD4 recipe to the kernel series or target tree; claim
Wi-Fi/AP/Ethernet support in a cover letter; attach a Tested-by/Reviewed-by that does not exist.

## 5. Required statements (copy-ready wording)

For the platform series cover letter / target PR:

> "Wi-Fi is out of scope for this series. The board's Hi5622 radio stack is closed-firmware and
> the vendor GPLv2 source request (sent 2026-10-01) is unanswered; the clean-room work is
> published separately as interoperability research and is not part of this submission. The
> internal switch/PIE patches are RFC bring-up support behind an attended gate, not a claimed
> data path. The included drivers are GPL-2.0; parts were derived by reverse engineering of the
> vendor's GPL-licensed binaries, with the provenance documented in the project notebook."

For the Wi-Fi research publication: same boundary, plus
[AP-MILESTONE-HONEST.md](AP-MILESTONE-HONEST.md) section 6 wording (AP-ENABLED is a bench result
on a research firmware; no support/association/data-plane claim).

## 6. Evidence

| claim | evidence |
| --- | --- |
| request sent 2026-10-01, no reply, no receipt copy, project decision | [../../GPL-REQUEST.md](../../GPL-REQUEST.md) (status header + contact/tracking sections); parent ledger |
| 58 vendor modules declare GPL | [../../GPL-REQUEST.md](../../GPL-REQUEST.md) "Why this request is binding" (the module list); `NOTICE.md` GPL note |
| PD4 identity/minimality/safety/H1/variants/alternatives | [../wifi-fw-audit/REPORT.md](../wifi-fw-audit/REPORT.md); [../wifi-fw-audit/README.md](../wifi-fw-audit/README.md); parent-ledger 2026-11-25 21:30 |
| PD4 harness selftests 14/14 + 14/14 (re-run 2026-11-27) | [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) |
| the duplex path is non-mailbox ETE/polled H2D | [../wifi-re-report/REPORT.md](../wifi-re-report/REPORT.md) F10/6.6; parent ledger 2026-11-25 21:30 |
| licence/provenance publication items | [../../PUBLISH-READINESS.md](../../PUBLISH-READINESS.md), [../../PUBLISH-PREP.md](../../PUBLISH-PREP.md) |
