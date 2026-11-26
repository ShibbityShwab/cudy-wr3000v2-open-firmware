# wifi-rx-d2h-post - the D2H post path of the firmware group-2 id-3, and the host-bound g1 id-13 RX route

**Lane:** `wifi-rx-d2h-post` (shared task **task-226**). **HOST-ONLY static decode** of the pinned
`FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...c3ef311b`; **CA = file offset + 0x40000**).
No device contact, no flash, no fire in this lane. **The Lead fires.**

This lane closes the question left open by `wifi-rx-smac-complete` (task-221, the producer of
the group-2 id-3 message) and `wifi-rx-mac-delivery` (task-220, its consumer): **where does a
message posted through CA `0x102760` actually go, and why does the host D2H ring deliver the
`g1 id3` and `g4 id4` traffic but never the `g2 id3`?**

The answer (detail in [REPORT.md](REPORT.md)): **`0x102760` is not a D2H deposit - it is a
per-destination/per-group queue enqueue.** The group record's queue index decides which customer
drains the message (static HCC map CA `0x103FB8`, group table CA `0x10CF74`: g2 -> queue 1,
the local-dispatch queue; g1/g3 -> queue 0, g4 -> queue 2, the transport queues); the firmware
registers *local* dispatch tables only for groups **0 and 2**,
and the g2 id3 handler `0xBBC9C` is one of those local entries. The host-bound classes
(groups 1/3/4) are the ones whose messages are enqueued to the transport queues and deposit
through the ETE op `0xC61E0` -> ring commit `0xC192A` -> `d2h_notify(6)` `0xC6170`.
So the **g2 id3 is an internal firmware notification**; the host-bound RX copy is the
**g1 id13** built per received frame by `0xB9780` (posted through the same `0x102760` at
`0xBA17A`). The producer chain (F1: `band_obj+0xCC0` +105) therefore does **not** by itself
prove the host RX copy; the missing link is now pinned to three named gates and probe-decidable
in one read-only pass (see [PROBES.md](PROBES.md)).

## Files

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | verdict, `0x102760` decode, per-message-type routing, why g2 id3 is not host traffic, the g1 id13 route, ranked gates, escalation |
| [TABLES.md](TABLES.md) | instruction-level tables, builder field tables, dest/group/queue layout, host conversion, host D2H acceptance |
| [PROBES.md](PROBES.md) | read-only probe pack P0/P12-P19 (queue/item counters, band-object dest byte, CRX side) + decision matrix |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the one attended read pass and the four-way decision |
| [tools/fw_rx_d2h_post_re.py](tools/fw_rx_d2h_post_re.py) | stdlib re-derivation tool, `--selftest` **151/151 PASS** |
| [evidence/](evidence/) | selftest, decode, tables, probes output |

## Reproduction

```sh
# from the repo root (finds rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin)
.provision-venv/bin/python opensource/docs/incoming/wifi-rx-d2h-post/tools/fw_rx_d2h_post_re.py --selftest
.provision-venv/bin/python opensource/docs/incoming/wifi-rx-d2h-post/tools/fw_rx_d2h_post_re.py --decode
.provision-venv/bin/python opensource/docs/incoming/wifi-rx-d2h-post/tools/fw_rx_d2h_post_re.py --tables
.provision-venv/bin/python opensource/docs/incoming/wifi-rx-d2h-post/tools/fw_rx_d2h_post_re.py --probes
```

Grades used everywhere: **[V]** re-derived by the committed tool; **[V\*]** committed earlier
lane/bench; **[I]** inference; **[U]** needs a device read.
