# wifi-sta-static - STA connect/auth/assoc + 5 GHz second endpoint + HT

**Lane:** `wifi-sta-static` (shared task **task-141**), host-only clean-room
static analysis. **Written:** 2026-11-24. **No bench-device contact; no vendor
binary/firmware/calibration byte in this directory.**

## Why

Our clean-room Hi5622 driver advertises STA mode but had no connect path, the
second PCI endpoint's 5 GHz wiphy was never statically justified, and HT was
HT20-only. This lane reverses the vendor 2.5.24/2.4.15 driver for those three
questions and delivers a fail-closed implementation + host tests + a bench
ladder.

## Deliverables

| path | content |
| --- | --- |
| [REPORT.md](REPORT.md) | full reversal: STA state machine and WAL messages, 5 GHz band/endpoint model, HT/VHT capability words, corrections to earlier notes, open [U] list |
| [TABLES.md](TABLES.md) | T1-T10: op addresses, WAL opcodes/bodies, connect body map, band tables, capability words, join-message maps, new module params |
| [TEST-LADDER.md](TEST-LADDER.md) | host rungs H0-H3 (run here) and bench rungs B0-B6 (designed, not fired) |
| [patches/0001-sta-static.patch](patches/0001-sta-static.patch) | applies on the combined AP+DP+scan+stability chain: STA logic/frames/kernel path, HT/VHT/band parameters, WAL allowlist |
| [src/](src/) | the added clean-room sources (portable logic + frames + kernel STA path) |
| [tools/sta_re.py](tools/sta_re.py) | independent re-derivation of every vendor claim from a local vendor .ko |
| [tools/sta_logic_selftest.c](tools/sta_logic_selftest.c), [tools/sta_frames_selftest.c](tools/sta_frames_selftest.c) | host selftests |
| [tools/selftest.sh](tools/selftest.sh) | runs the host selftests (+ optional staged-tree allowlist check) |
| [tools/build-sta-static.sh](tools/build-sta-static.sh) | stages the full chain, applies the lane patch, compile-verifies on 6.18.60, strips and asserts the artifact, runs the selftests |
| [evidence/](evidence/) | reproduction output: sta_re on 2.5.24 + 2.4.15, build log, selftest log, artifact identity |

## Headline results (details in REPORT.md)

1. cfg80211 `.connect`/`.disconnect` are **WAL cfg-events** on the same
   group-2/envelope family the bench-proven AP-start frames use:
   **connect = opcode 0x0123 / 108 B body**, **disconnect = opcode 0x00d0 /
   16 B body**. The 108-byte body map is fully decoded (T4).
2. The firmware performs auth/assoc; the completion is a D2H management event
   our RX path does not decode yet - so the driver must not fake
   `cfg80211_connect_done` (it does not).
3. The 5 GHz "second endpoint" is **one wiphy per band** (single host MAC,
   board band list, two RF chips) - bench-consistent with stock `WIPHY=2`.
4. Vendor band tables: 2.4 GHz 14 ch, 5 GHz **29 ch** (incl. ch144 + 4.9 GHz);
   HT cap **0x0062** (20/40 + SGI), VHT 2 SS MCS0-9, 5 GHz VHT **0x00400064**
   (160 MHz/SGI) - matching the live 160 MHz ch44 readout.
5. Patch adds `sta_join` (0 fail-closed / 1 dry-run / 2 post), `ht_mode`,
   `vht_cap`, `band_vendor` parameters; defaults change nothing on the bench.
6. Compile-verify: zero-warning build on 6.18.60, stripped artifact
   **278,044 B sha256 `333c72a5…`**, host selftests PASS.

## Quick reproduction

```sh
# host only, no device:
./tools/selftest.sh
./tools/build-sta-static.sh          # needs the project 6.18.60 tree + staging dir
python3 tools/sta_re.py /path/to/vendor/hi5622v100_wifi.ko
```
