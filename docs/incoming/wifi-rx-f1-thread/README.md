# wifi-rx-f1-thread - the firmware RX delivery internals, the EDCA auth fire, and the fallback probe pack

**Lane:** `wifi-rx-f1-thread` (shared task **task-216**), 2026-11-26.  **HOST-ONLY**:
the pinned local vendor firmware + the committed stock dump are decoded in place; no
device contact, no flash, no fire.  The Lead fires.

**One-line result.** The firmware's `HCC RX Thread` (CA 0x10CFF8, entry 0x102552) is
the **host-message (H2D) receive thread**, not a Wi-Fi MAC RX thread; the D2H publish
path (notify CA 0xC6170 / ETE node fill CA 0xC192A) is complete and proven by the
ready pair; the **PPDU descriptor list** the f1-capture lane pointed at is located
statically (name literal CA 0x1060F7, init CA 0xC016C, caller 0xD3BAC, fields
+0x66C/+0x670/+0x674/+0x67C) and its runtime address is now a computable read-only
probe via the per-chip context.  The last host-message candidate is the **EDCA
timeout (g0 id29)**, fired by [FIRE-RECIPE.md](FIRE-RECIPE.md); the fallback is the
firmware-internal RX consumer with [PROBES.md](PROBES.md) + the escalation path.

## Deliverables

| path | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: thread table, H2D dispatch, D2H publish, PPDU list, what the firmware expects from the host, the F1 verdict |
| [TABLES.md](TABLES.md) | every address/word: thread records, dispatch, D2H notify, PPDU list, per-chip ctx formula, EDCA handler, stock reference words, negatives |
| [PROBES.md](PROBES.md) | **R7 read-only probe pack**: D2H publish state, RX ring/DR state, PPDU list + EDCA ctx, SMAC words, decision table |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the **EDCA auth test**: preconditions, builder delta, fire ladder, expected dmesg/probe values, PASS/FAIL/STOP, escalation |
| [patches/0001-edca-payload-ff.patch](patches/0001-edca-payload-ff.patch) | the one-line 0xff-tail conformance delta for the EDCA builder |
| [tools/fw_rx_f1_thread_re.py](tools/fw_rx_f1_thread_re.py) | stdlib re-derivation tool: `--selftest` 86/86 PASS, `--decode`, `--tables`, `--probes`; no vendor byte is printed |
| [evidence/](evidence/) | selftest + decode/tables/probes transcripts (host-only) |

## Quick start (host, no device)

    python3 tools/fw_rx_f1_thread_re.py --selftest   # 86 checks, exit 0
    python3 tools/fw_rx_f1_thread_re.py --decode
    python3 tools/fw_rx_f1_thread_re.py --probes

The tool reads `rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B,
sha256 `7fc87e20...c3ef311b`) and `opensource/dumps/reg_all.txt` from the repo
root; both are local artifacts, never copied into the repository.

## Related lanes

../wifi-f1-from-capture (the H2D trace decode + EDCA ranking; this lane corrects its
"RX Thread" interpretation), ../wifi-rx-format, ../wifi-rx-linux-parity,
../wifi-rx-completion-phase (ring/buffer conformance and the R2b fault shape),
../wifi-d2h-direct + ../wifi-h2d-send (the direct D2H reader and the contract
constants used by the probes), ../wifi-tbtt-timer (firmware-RAM probe convention and
the per-band SMAC words).
