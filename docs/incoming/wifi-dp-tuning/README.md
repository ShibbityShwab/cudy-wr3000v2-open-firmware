# wifi-dp-tuning - device TX consumer + RX: RE and tuning vs the vendor

**Lane:** wifi-dp-tuning (shared task task-134), **host-only**, 2026-11-27.
**Not fired at the device.** This lane found the host-side step the earlier
data-plane lanes never implemented, implemented it in a new module, and wrote
the attended ladder that settles whether it is the device TX consumer's
trigger.

## What was found

The host publishes a frame to the MAC TX MSDU-info ring and the device-RAM
table producer index, but the device consumer never advances rptr
(POST-ONLY). The 14-register MAC band-0 program is complete and matches the
vendor init exactly. The difference is the **TXQ-table queue-state publish**:
after pushing ring entries the vendor also writes, into the 0x2524-byte
host-DRAM TXQ table (the table whose device VA the driver sends with the
g0 id1 key-0x0320 sync), a timestamp, the per-TID queued MSDU count, start
sequence number and byte count, and a per-user TID-non-empty bitmap
(hmac_update_user_tid_txq_table, called from hmac_tx_skb_pop_tid right after
hmac_tx_msdu_ring_push). The previous clean-room driver sent the table
address and left the table zero.

## Artifact (local evidence only, never committed)

    hi5622_wifi.ko (debug-stripped, path-independent)
    size   251924 B
    sha256 175a4617f208ac7fb67cbd9074cb4eac18876ebf973d905d650f185eed7cc425
    base   wifi-dp-air/src + ap-start v7 + patches/0001 (5 files)
           (the fired combined lineage was 242,572 B sha256 44c6cc12)

## Files

| file | contents |
| --- | --- |
| REPORT.md | the full RE: vendor enqueue tail, 14-reg comparison, role-5 tail 0x58..0xac, suspect matrix, the tuning, verification, [U] list |
| BUILD-NOTES.md | environment, exact build/verify numbers, parameters, deployment order, handoffs |
| ladder/LADDER-G-TXQ.md | the attended ladder (COMPLETED vs POST-ONLY vs NO-RX, stop rules) |
| ladder/bringup-g-txq-consumer.json | machine form of the ladder (33 steps / 366 lines, bench-tooling validated) |
| patches/0001-txq-consumer-tuning.patch | the tuning as one patch against the combined tree (5 files, 27,265 B sha256 22579b74...) |
| src/ | the five changed files (byte-identical to the patched tree) |
| tools/build.sh | canonical host+kernel verification (16 checks) |
| tools/txq_selftest.c | 37 host checks pinning the TXQ-table layout with literal offsets |
| tools/validate_sequence.py | validates the ladder JSON against the bench tooling (attrs injected in memory) |

## Quick start (host-only)

    cd opensource/docs/incoming/wifi-dp-tuning && ./tools/build.sh

Then attend the ladder with the built module from /tmp/wifi-dp-tuning-artifact/
(record sha256sum on the box first). The tuning adds four sysfs attributes:

* dp_txq_table (RO) - TXQ table + software per-TID state;
* dp_tx_complete_poll (WO 1) - one bounded poll, verdict COMPLETED/POST-ONLY;
* dp_mac_ext (RO) - role-5 tail 0x58..0xac (vendor live: 0x5c=0x6c=4);
* dp_mac_tail_init (WO 1, opt-in mac_tail_writes=1) - [U] experiment, default
  refused.

Module parameters added: txq_publish (default 1), tx_complete_ms (default
2000), mac_tail_writes (default 0).

## Honesty limits

The consumer-trigger claim is [I], not [V]: no shipped-artifact disassembly
shows which TXQ-table fields the firmware reads. Only the attended ladder can
turn POST-ONLY into COMPLETED (MAC); an on-air claim additionally needs an
external observer (broadcast management frames have no ACK). RX remains
capture-only until the completion-word format is decoded from a bench capture.

Read-first context: REPORT.md, ../wifi-dp-air/LADDER-F-AIR.md,
../wifi-dp-txrx/REPORT.md, ../../mem-entries.md (newest).

No vendor binary, firmware byte, calibration data or per-unit value is
committed. All C code is GPL-2.0; docs are original writing. This is a
bench-reversal instrument, not an upstream artifact.
