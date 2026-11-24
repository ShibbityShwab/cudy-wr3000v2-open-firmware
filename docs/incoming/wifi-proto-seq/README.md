# wifi-proto-seq - decode of the first D2H responses and the calibration alg set_* startup messages

Host-only deliverable of the wifi-proto-seq lane (shared task **task-103**, 2026-11-24).
It decodes the two NEW device->host frames the firmware sent after the working duplex lap
(g1 id3, g1 id0x12), identifies the **next vendor startup messages** after the TXQ config sync -
the six calibration `alg set_*` config events (HCC group 0 / id 2, key 0x0101) - builds them
byte-structurally, defines the expected device responses, and specifies the ordered bench ladder.

| file | what |
| --- | --- |
| REPORT.md | the lane report: both response decodes, the config-syn response contract, the calibration message family (carrier + 270-byte alg blob + cfg ids + value layouts), expected responses, the ordered bench ladder with observables/stop rules, the cfg80211 mapping rows, evidence table |
| tools/proto_seq_frames.py | frame builders + response decoder/checker (+ selftest, CLI): builds the 298-byte g0 id2 calibration frames and validates the g1 id3 config-syn responses |
| tools/fw_proto_static.py | firmware static assertions from the pinned local vendor blob (config-syn sender, g0 id2 dispatch, key 0x101 response path, equipment-param handlers/value layout); 30+ assertions |
| tools/alg_cmd_table.py | reads the local vendor `hi5622v100_wifi.ko` in place: recovers the 414-entry alg process table names from `.rodata.str1.4` offsets (the "custom fixup" myth), the 6 calibration cfg ids, their parsers, and the vendor default value blobs (local only, never printed into tracked files) |
| recipes/cal-commands.json | the six calibration commands: cfg id, value size, value offset, source-of-values policy |
| recipes/cal-alg-ladder.json | the ordered bench ladder (machine-readable), current state -> calibration messages, with expected observables and stop rules |

Quick start (host-only, no device):

```sh
P=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=opensource/docs/incoming/wifi-proto-seq/tools
$P $T/proto_seq_frames.py --selftest
$P $T/fw_proto_static.py --selftest
$P $T/alg_cmd_table.py --selftest
$P $T/proto_seq_frames.py --emit 3507            # zero-value structural frame, 298 B hex
$P $T/fw_proto_static.py --decode-id3 <frame-hex> # decode a captured g1 id3 payload
```

No vendor binary, firmware image, calibration value, or secret is committed. The tools read the
local pinned vendor artifacts in place and print derived annotations only; the calibration
*default* value tables are extracted to the local bench only (never written to the repo).
