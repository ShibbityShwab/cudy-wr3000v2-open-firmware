# wifi-ctx-create

Firmware-side H2D **context-create** lane (shared task task-101), 2026-11-24. Host-only static
RE; the firmware blob and vendor modules are local evidence only and never committed.

**Single-line answer:** the H2D message that makes the firmware call the context-create helper
CA 0xFE6BC is a **group-0 / id-1 config event carrying `u16 key = 0x0064`**; its data block at
payload+0xC selects the module index to create (`data[4]`) and the module type (`data[0] in
{1,2,3}). For the captured TXQ config sync (`hdr[3] = 4`, key 0x00d8) the create must be sent
first with `data[4] = 4`; boot itself only creates module 18 with type 0 and no +0x190, so the
solo TXQ sync dereferences NULL (the PD4 data abort at CA 0x81040).

- [REPORT.md](REPORT.md) - the full chain, the corrected key-table decode, the worker/helper
  semantics, the synthesized frame spec, the bench ladder with exact read addresses, evidence
  table and open items.
- [recipes/ctx-create-g0id1-key64.json](recipes/ctx-create-g0id1-key64.json) - machine-readable
  frame recipe (HCC header fields, payload fields, node-buffer hex).
- [tools/ctx_create_re.py](tools/ctx_create_re.py) - verifier: key tables, handler/worker/helper
  disassembly, caller scan, bench read pack, `--selftest` (40/40 PASS).
- [tools/ctx_create_frame.py](tools/ctx_create_frame.py) - frame builder + recipe emitter,
  `--selftest` (26/26 PASS).

Verification reads for the bench (BAR0 = CPU + 0x6B8000, read-only):

| object | CA | BAR0 | expect |
| --- | ---: | ---: | --- |
| module-table entry 4 (`G+(0x18C+4)*4`) | 0x171468 | 0x829468 | 0x0011C458 |
| ctx4 body | 0x11C458 | 0x7D4458 | 0 before create / id+type after |
| **ctx4+0x190** | **0x11C5E8** | **0x7D45E8** | **0 before create, non-zero after** |
| G singleton | 0x169E90 | 0x821E90 | 0x00170E28 |
| exception mask | 0x400008 | 0x400008 | 0x00000000 |

Reproduce:

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=opensource/docs/incoming/wifi-ctx-create/tools
$PY $T/ctx_create_re.py --selftest     # 40/40 PASS
$PY $T/ctx_create_re.py --trace        # handler -> key table -> worker -> create helper
$PY $T/ctx_create_re.py --readback     # bench read-only pack
$PY $T/ctx_create_frame.py --frame     # the 48-byte create frame
$PY $T/ctx_create_frame.py --selftest  # 26/26 PASS
```

Status of the payload: **synthesized** from the firmware parser (not a vendor capture); the
vendor key numeric in the shipped overlay `wifi.ko` is unproven (see REPORT section 6/11).
Node length may be the header-consistent 48 B or the capture-style 72 B (24 x 0xFF tail).
