# wifi-irq-unmask — IRQ-unmask transition and the H2D mailbox delivery gap

**Lane:** `wifi-irq-unmask` (shared task **task-90**), host-only static RE, 2026-11-24.
No device was contacted; the vendor firmware blob and the mask-ROM dump stay local and are
never copied into this directory.

**Deliverables**

| file | content |
| --- | --- |
| `REPORT.md` | full reversal: the post-boot continuation map, the exact unmask sites (task-entry trampoline 0xC2B88 -> 0xC2B7A `cpsie i` at 0xC2B80, resume/idle paths), the ROM IRQ vector -> image trampoline 0x40064 -> dispatcher 0xC2EFC path, the event-0x4C H2D mailbox registration/enable/priority/target decode, the delivery-prerequisite table (what is met vs open), the patch candidates with exact CPU/file offsets and byte encodings (PA/PB/PC/PD), expected bench observables, and the host-only conclusion |
| `tools/irq_map.py` | capstone-based read-only census/disassembler: `rom`, `map`, `unmask`, `mailbox`, `patch`, `selftest` |
| `tools/patch_builder.py` | verifies the local vendor blob + each source window by sha256 and writes patched copies into a non-repo directory (default `/tmp/hi5622-patched`); `--list`, `--patch NAME`, `--all`, `--selftest` |

**Headline result.** The missing transition is *not* the CPU IRQ mask: the image executes
`cpsie i` (0xC2B80) before every task body via the task-entry trampoline 0xC2B88 -> 0xC2B7A,
and again on every resume through the scheduler-switch exit 0xC2694. What is missing is the
device-side **assertion/delivery of event 0x4C** (the only H2D mailbox notification) for a
host out[0]/out[2] doorbell write; CA 0x4016xxxx is not host-visible, so no host-only action
can supply it. The recommended experiment is the firmware patch **PD**, which redirects the
boot task's success epilogue into a permanent poll loop calling the firmware's own H2D
dispatcher wrapper 0x40294: expected after one host mailbox post — out[0] bit3 clears,
out[5] reads 1, out[2] reads 8.

**Reproduce** (host-only; local blob read in place):

    PY=.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-irq-unmask/tools
    $PY $T/irq_map.py selftest
    $PY $T/irq_map.py unmask
    $PY $T/patch_builder.py --list
    $PY $T/patch_builder.py --selftest
    $PY $T/patch_builder.py --patch PD
