# wifi-irq-unmask — IRQ-unmask transition and the H2D mailbox delivery gap

**Lane:** `wifi-irq-unmask` (shared tasks **task-90** and **task-91**), host-only static RE, 2026-11-24.
No device was contacted; the vendor firmware blob and the mask-ROM dump stay local and are
never copied into this directory.

**Deliverables**

| file | content |
| --- | --- |
| `REPORT.md` | full reversal: the post-boot continuation map, the exact unmask sites (task-entry trampoline 0xC2B88 -> 0xC2B7A `cpsie i` at 0xC2B80, resume/idle paths), the ROM IRQ vector -> image trampoline 0x40064 -> dispatcher 0xC2EFC path, the event-0x4C H2D mailbox registration/enable/priority/target decode, the delivery-prerequisite table (what is met vs open), the patch candidates with exact CPU/file offsets and byte encodings (PA/PB/PC/PD), expected bench observables, and the host-only conclusion |
| `tools/irq_map.py` | capstone-based read-only census/disassembler: `rom`, `map`, `unmask`, `mailbox`, `patch`, `selftest` |
| `tools/patch_builder.py` | verifies the local vendor blob + each source window by sha256 and writes patched copies into a non-repo directory (default `/tmp/hi5622-patched`); `--list`, `--patch NAME`, `--all`, `--selftest` |
| `REPORT-PD2.md` | PD2 iteration (task-91): the precise H2D dispatcher path and calling convention (wrapper 0x40294 -> dispatcher 0xC18AC; instance fields +4/+0xC/+0x10 = CA out[0]/ack/out[2], +0x20 = handler table; event-0x4C registration/enable), the reconstruction of PD's data abort (the patched 0x47610 is the *handshake retry* body, called before `pcie_msg_init` initialises the instance -> first fault at 0xC18B8, nested ABT frames -> the exact CA 0x1BF000 page overwrite), the PD2 design (boot-epilogue redirect + guarded out[0] poll stub in the dead config-3 block CPU 0x47432, exact file offsets/bytes/hashes), expected bench observables, the SR-announce latch policy fix and open items |
| `tools/pd2_re.py` | read-only verifier for the dispatcher contract, the PD abort chain/ABT-frame arithmetic and the PD2 code cave; `dispatcher`, `fault`, `cave`, `selftest` (33/33) |
| `tools/pd2_patch_builder.py` | PD2 builder/verifier: pinned blob + per-window sha256, patched `FIRMWARE.PD2.bin` + manifest written only outside the repo, machine-readable recipe for the bench tooling; `--list`, `--build`, `--recipe`, `--selftest` (42/42) |
| `PD2.recipe.json` | the machine-readable PD2 recipe (`hi5622-fw-patch-recipe/1`, `based_on: null`) for `wifi-bench-tooling/tools/fw_patch_builder.py --build PD2 --recipe-file ...`; offsets/hashes/our patch bytes only |
| `REPORT-PD3.md` | PD3 iteration (task-91 follow-up): proof that PD2-A failed because its config-3 cave CPU 0x47432 is LIVE on our boots (bench page 0x6F analysis + PD1/PD2 differential), PD3 relocation of the guarded poll stub to the image-header exception-record area CPU 0x4000C (unreachable: no branch/literal reference, zero-fill, only crash-path writer), exact bytes/hashes, the no-cave PD3B alternative (event-0x4C raise loop), expected observables, the module latch fix and ranks |
| `tools/fw_pd_builder.py` | PD2/PD3/PD3B builder+verifier: pinned blob, per-window sha256, patched images + manifests written outside the repo, machine-readable recipes; `--list`, `--build NAME`, `--recipe NAME`, `--selftest` (56/56) |
| `tools/pd3_re.py` | verifier for the PD3 cave proof (header layout, record-area zero/census, crash-path writer) and the PD2-A failure reconstruction; `header`, `pd2-fail`, `selftest` (15/15) |
| `PD3.recipe.json`, `PD3B.recipe.json` | machine-readable PD3 and no-cave PD3B recipes (`hi5622-fw-patch-recipe/1`) for `wifi-bench-tooling/tools/fw_patch_builder.py` |

**Headline result.** The missing transition is *not* the CPU IRQ mask: the image executes
`cpsie i` (0xC2B80) before every task body via the task-entry trampoline 0xC2B88 -> 0xC2B7A,
and again on every resume through the scheduler-switch exit 0xC2694. What is missing is the
device-side **assertion/delivery of event 0x4C** (the only H2D mailbox notification) for a
host out[0]/out[2] doorbell write; CA 0x4016xxxx is not host-visible, so no host-only action
can supply it. The recommended experiment is the firmware patch **PD**, which redirects the
boot task's success epilogue into a permanent poll loop calling the firmware's own H2D
dispatcher wrapper 0x40294: expected after one host mailbox post — out[0] bit3 clears,
out[5] reads 1, out[2] reads 8.

**PD3 (2026-11-24, task-91 follow-up): PD2-A failed on the bench - its cave (entry A config-3 block
0x47432) is live on our boots; PD3 moves the same guarded out[0] poll stub to the image-header
exception-record area CPU 0x4000C (globally unreachable, entered only by our epilogue patch).
PD3 image sha256 `eea9f2ff...3905ce`; zero-cave alternative PD3B (event-0x4C raise loop +
zero handler table) sha256 `ef0683db...45f2`; see `REPORT-PD3.md`. Earlier iteration:**

**PD bench outcome and PD2 (2026-11-24 16:10).** PD did reach the image
exception path, but it was the *wrong* call site: 0x47610 is the entry-A handshake poll
retry (executed before `pcie_msg_init` initialised the hcc instance), so the dispatcher
data-aborted on a NULL mailbox pointer, and four nested ABT frames stacked down onto the
handshake page (0x0010C190/0/0/0x1BEE50 = frame words {r0,r1,r2,r3}). PD2 keeps only the
boot-epilogue redirect (CPU 0x4916A -> CPU 0x47432) and runs a guarded out[0] poll loop from
a genuinely dead code block; when the dispatcher handler table still holds its static
uninitialised value (0x118D68, image code) it substitutes the ROM RAZ window 0x2000 so the
ack/consume/doorbell contract runs without an unverified `blx`. Patched image sha256
`7a691807...1b7728`; see `REPORT-PD2.md`.

**Reproduce** (host-only; local blob read in place):

    PY=.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-irq-unmask/tools
    $PY $T/irq_map.py selftest
    $PY $T/irq_map.py unmask
    $PY $T/patch_builder.py --list
    $PY $T/patch_builder.py --selftest
    $PY $T/patch_builder.py --patch PD
    # PD2 (task-91)
    $PY $T/pd2_re.py --selftest          # 33/33 PASS
    $PY $T/pd2_re.py --dispatcher        # dispatcher contract
    $PY $T/pd2_re.py --fault             # PD abort chain + page clobber proof
    $PY $T/pd2_re.py --cave              # PD2 dead-cave proof
    $PY $T/pd2_patch_builder.py --selftest   # 38/38 PASS
    $PY $T/pd2_patch_builder.py --recipe     # bench-tooling recipe
    $PY $T/pd2_patch_builder.py --build /tmp/hi5622-pd2
    # PD3 (task-91 follow-up)
    $PY $T/pd3_re.py --selftest          # 15/15 PASS
    $PY $T/pd3_re.py --header            # record-area cave proof
    $PY $T/pd3_re.py --pd2-fail          # why PD2-A failed on the bench
    $PY $T/fw_pd_builder.py --selftest   # 56/56 PASS (PD2/PD3/PD3B)
    $PY $T/fw_pd_builder.py --recipe PD3
    $PY $T/fw_pd_builder.py --build PD3 --out /tmp/hi5622-pd3
