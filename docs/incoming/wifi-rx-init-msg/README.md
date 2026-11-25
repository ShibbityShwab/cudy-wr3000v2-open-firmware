# wifi-rx-init-msg - the vendor init chain's config-event senders and the one unaccounted live config key

**Lane:** `rx-init-decode` (shared task **task-170**). **Date:** 2026-11-26.
**Scope:** host-only, read-only decode of the unstripped vendor Wi-Fi module
`hi5622v100_wifi.ko` (local artifact) + the local firmware `FIRMWARE.bin`; **no device
contact, no build, no vendor bytes committed**.

## One-line result

The vendor init chain (`hmac_main_init -> hdpp_main_init -> hdpp_chip_init/var_init`) sends
**no H2D firmware message at all** (registration/resource init only). The vendor's entire
host-to-device config-event surface is **266 call sites / 244 distinct g0-id1 keys**; the
firmware supports exactly **8** of those keys, and **one of the 8 - key `0x0004` - has no
sender anywhere in the shipped vendor module**. That unaccounted live key is the deliverable's
candidate "firmware RX-init message": a safe single-byte context config write
(firmware worker CA `0x811CD`: `ctx[+0x190][0x52] = data[0]`), which our driver has never
sent. It is **not proven** to be the RX capture fix - the honest ranking is in the REPORT.

## Deliverables

| file | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: method, message constructors, sender enumeration, live-key cross-check, the candidate + risk analysis, honest verdict |
| [TABLES-h2d-builders.md](TABLES-h2d-builders.md) | all 77 vendor HCC message builders and the ids they write at header+6 |
| [TABLES-config-senders.md](TABLES-config-senders.md) | all 244 config-event keys with their builder function, call site and payload length; the 8 firmware-live keys marked |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the attended R2 fire: fresh boot -> PD4 -> AP-first -> `rx_init_msg=1` -> laptop auth -> PASS/FAIL |
| [patches/0001-rx-init-msg.patch](patches/0001-rx-init-msg.patch) | fail-closed integration patch (module params, one message per boot, consumption read-back, gates) against the committed `wifi-h2d-send/src` driver tree |
| `src/hi5622_rx_init_logic.{h,c}` | portable frame builder + gate/read-back model (compiles host and kernel) |
| `tools/rx_init_selftest.c`, `tools/run-host-tests.sh` | device-free host test (byte-exact frame, gate matrix, read-back, safety) |
| `tools/vendor_config_senders.py`, `tools/vendor_msg_scan.py` | reproducible extraction of the two tables from the local module (regenerates the TABLES files) |
| [evidence/DECODE-LOG.md](evidence/DECODE-LOG.md) | the exact commands / artifact hashes / symbol and section anchors used |

## Why this is the candidate (short form)

1. Firmware group-0 id-1 handler (CA `0x43D58`) dispatches on the u16 key at payload+8 through
   key table A (CA `0x10C23C`, 217 entries): only 8 keys are live -
   `0x0004, 0x0064, 0x0065, 0x0066, 0x0067, 0x0069, 0x00D8, 0x0320`.
2. The vendor module's config-event senders carry the other 243 keys; **none carries 0x0004**
   (neither builder has a `key=4` call site, and no function builds a g0 id1 frame with an
   immediate `hdr+6=1`/`hdr+20=4` pair).
3. Key 0x0004's firmware worker is a leaf write of one byte into the created context block
   (`ctx+0x190+0x52`), with a clean refusal (`0x8b2d`) when the context is absent - the
   cheapest safe host->firmware message left in the inventory.
4. Our driver already sends the vendor-validated `0x0064` (context create), `0x00D8` (TXQ
   sync; the captured first post-ready vendor frame) and `0x0320` (TXQ address sync) families,
   and the VAP keys `0x0064/0x0065`. It has never sent `0x0004`.

## What remains (honest)

* The candidate is **not** proven to fix the RX capture; the fire is the discriminator
  (PASS = `frames>0` + page golden; FAIL = bit-10 latch / page loss -> candidate refuted,
  fall back to the documented F4 status-forwarding class and the stock-boot H2D trace).
* The vendor never sends key 0x0004 in any code path present in the shipped modules; a stock
  boot H2D trace is the only way to know whether a working vendor boot sends it at all.
* The patch is delivered against the committed `wifi-h2d-send/src` tree; the fired association
  tree needs a 3-hunk port (documented in FIRE-RECIPE.md) because its H2D carrier is the
  later association/mgmt-carrier variant.
