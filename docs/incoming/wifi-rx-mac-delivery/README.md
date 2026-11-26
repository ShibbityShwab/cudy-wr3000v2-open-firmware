# wifi-rx-mac-delivery - the MAC-RX -> firmware -> D2H delivery decode (why client frames never reach the host)

**Lane:** `wifi-rx-mac-delivery` (shared task **task-220**), 2026-11-26.  **HOST-ONLY**:
the pinned local vendor firmware is decoded in place; no device contact, no flash, no
fire.  The Lead fires.  **One-line result.** The D2H ring is a **single shared firmware->host
HCC message channel** (deposit 0x102760 -> ETE node 0xC192A -> notify 0xC6170, ids 3/5/6):
the firmware's own deliverers (ready pair g4 id1/id2, g1 id3 config replies, HEARTBEAT
g4 id4) and the management-RX event **g1 id13** all use it, so "the firmware's own frames
deliver" proves the **deposit/notify/consume** half only - it says nothing about the
**MAC-RX intake**.  The intake is the per-frame consumer **0xB9780** (per-chip RX ctx
[chip+0x1a00], forward flag info[+6], total cap, then the g1 id13 builder); its frames
arrive as HCC message nodes popped at **0xBA0C0** (0x1024EA).  The **producer of those
nodes - the SMAC RX-completion -> descriptor -> node path - is the missing layer**; its
entry is not locatable in this image ([U]) and the bench shape (frames=0, no host write,
intermittent page loss) matches "intake never fed / consumer faulted at the first frame".
This lane delivers the decode, the corrected D2H route model, the read-only probe pack
(device-object counters + PPDU list + MAC live bank) and the fire/escalation recipe.  The
PPDU descriptor pool is present **and populated** (bench: 16 descriptors, chain in PKTRAM:
head 0x01019F80, tail 0x010186A8, count 0x10), so the F1 is not the list init.

## Deliverables

| path | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: D2H source selection, the RX consumer 0xB9780, the per-frame gates, the PPDU descriptor pool, where the path breaks, candidate host states |
| [TABLES.md](TABLES.md) | every address/opcode/callsite: T1-T11, the names table, the counter words, the conversion constants |
| [PROBES.md](PROBES.md) | **read-only probe pack P1-P11** (device-object counters, PPDU list self-check, MAC live bank) + the decision table |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | R0 preconditions, R1 probe-during-auth, R2 stock control, R3 escalation; PASS/FAIL/STOP |
| [tools/fw_rx_mac_delivery_re.py](tools/fw_rx_mac_delivery_re.py) | stdlib re-derivation tool: `--selftest` **95/95 PASS**, `--decode`, `--tables`, `--probes` |
| [evidence/](evidence/) | selftest/decode/tables/probes transcripts (host-only) |

## Quick start (host, no device)

    python3 tools/fw_rx_mac_delivery_re.py --selftest   # 95 checks, exit 0
    python3 tools/fw_rx_mac_delivery_re.py --decode     # the chain
    python3 tools/fw_rx_mac_delivery_re.py --probes     # the read-only pack

The tool reads `rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B, sha256
`7fc87e20...c3ef311b`) from the repo root (or `FIRMWARE_BIN`); it refuses any other
image.  Vendor artifacts stay local and untracked.

## Related lanes

../wifi-rx-crx-gating (the CRX wire contract + the host-decoder side, re-derived here),
../wifi-f1-from-capture (the stock H2D capture negative + the HEARTBEAT builder),
../wifi-rx-f1-thread (the "RX Thread" = H2D thread; D2H publish),
../wifi-ppdu-init (the PPDU descriptor-list init + the corrected probe), ../wifi-rx-format,
../wifi-rx-linux-parity, ../wifi-rx-completion-phase, ../wifi-rx-woc-class,
../wifi-h2d-catalog (the firmware dispatch tables), ../wifi-smac-start.
