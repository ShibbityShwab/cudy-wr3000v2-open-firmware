# wifi-5g-smac-keys - the 20 stock-only g0-id1 config keys vs the band-1 SMAC device chain

**Lane:** wifi-5g-smac-keys (shared task **task-223**), 2026-11-26. **HOST-ONLY**: static analysis of
the local firmware image and the local unstripped vendor modules; no device contact, no fire, no
vendor bytes committed. The Lead fires; this lane decodes and ranks.

**One-line answer:** none of the 20 stock-only config keys is the per-band SMAC activation. The
per-band SMAC attach/enable path is the **boot-only** SMAC init loop CA `0xD1B68` (single caller
CA `0x48314`), which writes the *same* per-band fields for band0 and band1 (chain A base at
obj+672 = 0x40042000 / 0x40062000; RAM block at obj+640 = 0x114F18). Of the 20 keys only **0x0000
(set_mac_addr)** writes the SMAC device chain through [obj+672] (per-chain MAC registers), and only
**0x07E7 (dfs_cac)** writes the band-1 SMAC sub-base (0x4010E000 block, DFS). The band-1 status word
0x00A00096 is a **symptom** (the +0xE4 word is written only by five SMAC ISR bodies and the image
contains no 0xA00096 constant), not a start condition. The 20 keys are VAP/channel/regulatory/state
syncs; the only ones that can affect band-1 chain liveness indirectly are the VAP state/mode sync
trio (0x0073, 0x0069) and the per-channel TX-cali sync (0x07E8), all sent by the stock and missing
from our ladder.

- [REPORT.md](REPORT.md) - the full decode: dispatcher, the 20 handlers, the chain/status model, the
  candidate ranking, negatives and open items.
- [TABLES.md](TABLES.md) - T1 dispatcher, T2 the 20 keys (worker + vendor emitter + body + effect),
  T3 the 9 ladder keys, T4 the 0x84xxx family, T5 chain/status model, T6 probe pack, T7 stock counts,
  T8 candidate bodies/order.
- [FIRE-RECIPE.md](FIRE-RECIPE.md) - R0 read-only premise probes (settle obj+640 vs obj+672), then
  one candidate rung per fresh boot (0x0073 -> 0x0069 -> 0x0000 -> 0x07E8) with probes and abort rules.
- [tools/fw_5g_smac_keys_re.py](tools/fw_5g_smac_keys_re.py) - re-derivation tool, `--selftest`
  (**129/129 PASS**) and `--dump`.
- evidence/ - [selftest.txt](evidence/selftest.txt), [decode.txt](evidence/decode.txt).

## Reproduce

    .provision-venv/bin/python tools/fw_5g_smac_keys_re.py \
        /path/to/FIRMWARE.bin --selftest --dump

Firmware: 928,920 B sha256 `7fc87e20...311b`; runtime CA = file offset + 0x40000 (the mapping the
committed firmware lanes use). The vendor emitter names/grades are **[V\*]** from the *local*
unstripped `hi5622v100_wifi.ko` (3,581,748 B sha256 `22fa789a...c7d2`); the module and its
disassembly stay local and are never committed.
