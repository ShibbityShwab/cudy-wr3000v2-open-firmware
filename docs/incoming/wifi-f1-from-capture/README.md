# wifi-f1-from-capture - decode the stock H2D trace into the missing RX (F1) message

**Lane:** `fw-f1-capture` (shared task **task-198**), 2026-11-28. **Host-only**: decode of the
successful stock H2D capture + static decode of the local vendor modules/firmware. No device
contact. No vendor binary, firmware byte or raw capture is committed.

Companions: [REPORT.md](REPORT.md) (the decode and the F1 ranking),
[TABLES-families.md](TABLES-families.md) (the corrected family table with the builder/firmware
evidence), [FIRE-RECIPE.md](FIRE-RECIPE.md) (implementation path + bench ladder),
[tools/decode_f1_capture.py](tools/decode_f1_capture.py) (stdlib decoder, `--selftest` 5/5; run
`tools/run-host-tests.sh /tmp/h2d-capture.log`), [evidence/](evidence/).

## One-line result

The "18 families" of the raw sampler split are **not 18 messages**: 1112 of the 1716 records are
torn node reads (`node_len != frame total`), and the only **604 valid host->device sends** are
**g0-id1 config events (25 keys), g0 id29 (EDCA timeout, 329), g0 id12 (one scan request),
g0 id2 (two alg requests)**; `g12 id1` collapses onto `g0 id1 key 0x0127` (four stale leading
bytes). The **HEARTBEAT (g4 id4) is a device->host message**, byte-identical to the firmware's own
builder at CA `0xC6BB2` (payload copied from the `HEARTBEAT` literal at CA `0x106328`) - it is
never a host send, so our driver's `heartbeat off` is not the RX bug. The second big "unmatched"
family, `g2 id29`, is the **plat.ko HCC timer message** (payload = the timer's host context
pointer); it is not an RX enable either. **No stock H2D message carries an RX-ring/TRX enable**, so
the capture's strongest F1 result is a negative: F1 is not a missing H2D frame. The only
datapath-class stock message our driver never sends is the EDCA timeout - already built in our
driver (`edca`) - and the [FIRE-RECIPE.md](FIRE-RECIPE.md) fires it as the last host-side
discriminator before the F1 escalates to the firmware/internal path.

## The family table in one screen (details in TABLES-families.md)

| raw family (sampler group/id) | raw | valid H2D | decode | direction |
| --- | ---: | ---: | --- | --- |
| g0 id1 | 498 | 254 | config events, 25 keys (`hmac_config_send_event`/`hdpp_config_send_event`); 108 match our driver = 104 x key 0xD8 + 4 x key 0x65 | H2D |
| g0 id29 | 381 | 329 | `hmac_edca_opt_timeout_fn`, 72 B = 12 + 20 zero + 40 x 0xff | H2D |
| g2 id29 | 328 | 0 | `hcc_timer_timeout_proc` (plat.ko), 16 B, 4-byte host ctx pointer | H2D (torn only) |
| g4 id0 | 153 | 0 | firmware OAM/device-log blobs | D2H residue |
| g2 id0 | 136 | 0 | torn residue: config-event-style fragments + host pointers, no builder | [U] |
| g4 id4 | 83 | 0 | firmware HEARTBEAT builder (byte-identical) | D2H residue |
| g2 id9 | 60 | 0 | text-ish vap/SSID log residue | [I] D2H |
| g1 id37 | 28 | 0 | D2H event class (no host builder, no firmware group-1 receive table) | [I] D2H |
| g12 id1 | 21 | 18 | **alias of g0 id1 key 0x0127** (stale first 4 bytes; payload+total identical) | H2D |
| g1 id3 / g2 id37 / g9 id29 / g2 id8 / g1 id17 / g1 id38 | 12/4/4/2/1/1 | 0 | D2H event/aliased-timer residue | D2H residue |
| g0 id2 | 2 | 2 | `hmac_config_alg_send_event` key 0x0101, 39 B | H2D |
| g0 id12 | 1 | 1 | `hmac_enc_scan_send_req`, 520 B | H2D |

No vendor byte is committed: the decode quotes protocol bytes as text (allowed); the capture itself
stays at `/tmp/h2d-capture.log` (sha256 `970b8006...`).
