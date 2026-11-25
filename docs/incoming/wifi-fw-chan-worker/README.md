# wifi-fw-chan-worker - the firmware 0x0124 channel worker, the ctx index truth, and the 5 GHz RF-gate readback

**Lane:** wifi-fw-chan-worker (shared task **task-180**), 2026-11-27.
**HOST-ONLY, read-only**: the lane decoded the pinned local vendor firmware
image and vendor host module in place. It never contacted the router, never
wrote firmware, never flashed and never fired. **The Lead fires.**

## The one-paragraph result

The firmware 0x0124 channel-config worker (CA 0x841B8, reached from the g0 id-1
dispatcher CA 0x43D58 via key table A entry A[108]) validates
(band, channel) against the firmware's own per-band channel tables (band 0:
14 channels 1..14 at CA 0x117B48; band 1: 29 channels 36..196 at CA 0x117AD4)
and, on success, stores **channel -> ctx+0x10, band -> ctx+0x11,
body[1] -> ctx+0x12 and the CHANNEL-TABLE INDEX -> ctx+0x13**, then runs the
post-store device work. **The byte at ctx+0x13 is not a validity flag.** Band 1
channel 36 is index 0 by construction, so the previous lane's "valid must be 1"
gate could never pass for ch36: the G6b REFUSED line
(`word=0x00000124`, 2026-11-27 05:30) is a **host-side gate bug**, not a
firmware negative, and the G6b timing candidate has never actually been tested.
The worker's 5 GHz RF programme (CA 0x101890, called from CA 0x40684) is
gated silently on `module[4]==2 && module+0x440==1 && bandobj+0xEC8==1`; the
read-only BAR0 pack in TABLES T7 decides at the next fire whether it ran.

## Files

| file | what |
| --- | --- |
| `REPORT.md` | the decode: dispatch, worker, validation tables, ctx word, post-store gates, band-0 difference, the vendor's synchronous transaction and the candidate host trigger, limits |
| `TABLES.md` | T1-T8: addresses, channel tables, store map, the G6b proof, the silent gates, the vendor transaction, the read-only BAR0 pack, the fire lines explained |
| `FIRE-RECIPE.md` | the one-shot rungs: **CW-0 zero-rebuild control (ch40)**, CW-1 corrected gate, CW-2 read-only RF-gate pack; decision tree; safety rules |
| `tools/fw_chan_worker_re.py` | the reproduction: `--decode`, `--tables`, `--readback`, `--selftest` (89/89 PASS). Reads the pinned local blobs in place; copies no vendor byte |
| `evidence/` | the captured outputs: `selftest.txt`, `decode.txt`, `tables.txt`, `readback.txt` |

## Reproduce (host only)

    cd opensource/docs/incoming/wifi-fw-chan-worker
    .provision-venv/bin/python tools/fw_chan_worker_re.py --selftest \
        --pd4 /home/shibbityshwab/pd4-rebuilt.bin    # -> 89/89 PASS
    .provision-venv/bin/python tools/fw_chan_worker_re.py --tables
    .provision-venv/bin/python tools/fw_chan_worker_re.py --readback
    .provision-venv/bin/python tools/fw_chan_worker_re.py --decode

The corrected module (host build; the .ko is local evidence only):

    ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh   # the pinned base tree (once)
    ./tools/build-chan-worker.sh                        # stage+patch+build+contract
    # -> /tmp/chanworker-artifact/hi5622_wifi.ko 358,256 B
    #    sha256 9a5b048e...775fa6, 59 params (not bench-fired)

The tool needs only the Python standard library plus the local optional
artifacts: the firmware image is the pinned `FIRMWARE.bin`
(928,920 B, sha256 `7fc87e20...311b`), the vendor module is the pinned 2.4.15
`hi5622v100_wifi.ko` (3,581,748 B, sha256 `22fa789a...c7d2`); both are local
evidence and are never committed. `--pd4` additionally proves the bench copy
`pd4-rebuilt.bin` differs from the pinned image only in the documented boot
epilogue and the 0x108240 stub cave (no address this lane decodes differs).

## Relation to the earlier lanes (what is corrected here)

* `../wifi-beacon-static/REPORT.md` - the worker body map is confirmed, but its
  `1 -> ctx+0x13 (valid)` reading is corrected: it is the channel-table index.
* `../wifi-5ghz-beacon/REPORT.md` - the corrected readback constants and the
  synchronous-ap_chan insight stand; its `ap_chan_wait_ms` gate function
  (`hi5622_beacon5g_chan_ctx_ready`) compares the index with 1 and is the bug
  this lane explains. The lane is asked for the fire, and FIRE-RECIPE CW-0
  works around the gate with no rebuild (ch40 -> index 1).
* `../wifi-ctx-create/REPORT.md` / `../wifi-boot-gate/REPORT.md` - the
  dispatcher, key table A and the band objects/publish words are reused; this
  lane only re-asserts the windows it needs.

## Standing rules

A lane's fire recipe is not trusted until the host tests run from the
committed files and the recipe matches the proven bench protocol
(BENCH-FACTS/LESSONS: recovery armed, UART read-only, one AP per load,
power-cycle on the exception mask/page change, no UART transmit). This lane's
`--selftest` is that host test; the fire is the Lead's.
