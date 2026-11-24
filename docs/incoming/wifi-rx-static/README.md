# wifi-rx-static - MAC RX data path (task-137)

Host-only clean-room static RE of the Hi5622V100 MAC RX path plus a
**fail-closed RX enable patch**.

## The finding

The RX completion ring stayed at 0 captures because the driver polled the
**wrong register bank**: the live device-side ring pointers live at CA
**0x40050000** (+0x00 status, +0x08 norm rptr, +0x0c small rptr, +0x10
completion wptr, vendor machw array word 8), while config-bank
+0x4c/+0x50/+0x54 (CA 0x40040000) are words the vendor never reads and stay 0
even in a stock boot where the live rptrs read 0x20 (MAC prefetched 32 free
buffers).  The inherited capture decoder was also 4 bytes off: the device
writes a 36-byte header at **buffer+4**, length at **buffer+0x1e**, next
chain pointer at **buffer+0x24**, MPDU payload at **buffer+0x28**.

The 14-write MAC program is the whole *device* enable (the MAC prefetches as
soon as the free-ring producers are published); the missing host half was the
consumer: read the live bank, drain the 4-byte completion entries (each is the
device VA of buffer+4), parse the 36-byte header, refill up to the live rptr
delta and publish the completion-ring consumer index.

## Deliverables

| path | what |
| --- | --- |
| `REPORT.md` | full evidence-graded RE: machw bank map, live-vs-config split, vendor init/completion sequence with addresses, header layout, patch behaviour, fail-closed matrix, [U] list |
| `patches/0001-rx-live-enable.patch` | the patch (6 files) on the final combined clean-room tree |
| `src/` | the patch's resulting files (new portable model + kernel glue + Kbuild) |
| `tools/rx_selftest.c` | 51 host checks pinning the decode with literal vendor offsets (all pass) |
| `tools/build.sh` | reproduction: stage base, apply patch byte-for-byte, selftest, kernel build |
| `TEST-LADDER.md` + `ladder/rx-ladder.json` | the attended bench ladder the Lead fires (L0 fail-closed -> L5 soak) |
| `BUILD-NOTES.md` | base-tree identity, build result (0 warnings), artifact hashes |

## Safety

* `rx_enable=0` by default: `dp_rx_arm`/`dp_rx_drain` refuse; the old
  observe-only attributes are unchanged.
* arm is read-only; drain is the only pointer-moving RX action and only after
  arm + write gate.  Any bad entry/header/chain/publish closes the lane with
  no retry.
* Writes are limited to config +0x38/+0x3c/+0x40 (+0x44 only with
  `rx_clear_status=1`); the live bank is read-only; no IRQ, no firmware
  message, no word outside MAC band 0.
* No vendor bytes, firmware, calibration or per-unit data in tracked text;
  no bench contact from this lane.
