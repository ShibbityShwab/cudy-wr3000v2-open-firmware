# wifi-ap-start - AP start RE + builders (task-121)

**Lane:** wifi-ap-start (shared task task-121), **host-only**, 2026-11-26.
**Result:** the vendor WAL AP-config command 0x0127 (156-byte body) and the
full host AP-start sequence are reversed and implemented as byte-exact C
builders on both carriers (static g0 id-1 config keys and vendor group-2 WAL
envelopes), with a compile-ready provider extension (caps AP_START) and an
attended bench ladder.  **Nothing was sent to the firmware; the bench router
was never contacted.**
**v2/v3 (after the bench's real hostapd fires):** the patch fixes the hostapd
integration gaps (NL80211_CMD_CONNECT advertisement via a fail-closed .connect
op, AP iftype changes with correct wdev->iftype update and use_4addr=-1
handling, AP-start sequence inside the real .start_ap) and v3 rebuilds from
the hardware-fired provider base (wifi-scan-complete/src, 748,888 B
b42ff4ec).  Deployable module **206,708 B sha256
fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020**; exact
hostapd flow, expected logs and corrected readback in
[HOSTAPD-BRINGUP.md](HOSTAPD-BRINGUP.md).

Headline facts:

1. AP start is a four-message sequence: set SSID (WAL cmd 0x0005, body 36) ->
   auth algs (0x006D, body 4) -> AP config (0x0127, body 156) -> VAP start
   (0x0065, body 12).  SSID/auth are not inside the 156-byte body. [V]
2. The 156-byte body is fully mapped: beacon interval (+0x00), DTIM (+0x04),
   RSN/WMM-present (+0x08), 64-byte RSN/vendor-IE copies (+0x09/+0x49),
   hidden SSID (+0x89), HT/VHT capability bits + wireless mode
   (+0x8A..+0x8E), start/change flag (+0x93), mobility domain
   (+0x94..+0x98), reserved (+0x8F..0x92/+0x99..0x9B). [V]
3. Firmware side: config-key table A (CA 0x10C23C) statically dispatches key
   0x0005 -> worker 0x82878 (set SSID) and key 0x0127 -> worker 0x84DA4 (AP
   config) from the group-0 id-1 carrier; the vendor's group-2 WAL path has
   no static group-2 id-0 slot (dynamic handler, [U]).  Both carriers are
   implemented; the static one is the default ladder path. [V]/[I]
4. Verification: host selftest 128/128, static RE probes 63/63, generated
   frames fresh, module build against 6.18.60 770,304 B sha256 11511980...
   with zero warnings.

Deliverables (this directory):

| path | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | full report: vendor sequence, body decode, firmware dispatch, builders, ladder, limits |
| [TABLES.md](TABLES.md) | T1-T11 evidence tables |
| [src/hi5622_ap_frames.h](src/hi5622_ap_frames.h) / [.c](src/hi5622_ap_frames.c) | portable C99 builders (bodies, both envelopes, sequence, wire validator) |
| [src/ap_frames_selftest.c](src/ap_frames_selftest.c) | 128 device-free checks + --emit |
| [tools/build.sh](tools/build.sh) | host strict compile + selftest + emit |
| [tools/ap_start_re.py](tools/ap_start_re.py) | 63 static probes (firmware + vendor module) |
| [tools/gen_frames_md.py](tools/gen_frames_md.py) | regenerate/check [recipes/frames.md](recipes/frames.md) |
| [tools/kernel-check.sh](tools/kernel-check.sh) | kernel-context module compile (zero warnings) |
| [provider/apstart-provider.patch](provider/apstart-provider.patch) | caps AP_START + AP-start sequence loop + ap_start_mode param |
| [recipes/ap_start_ladder.json](recipes/ap_start_ladder.json) | attended ladder: steps, readback pack, stop rules, claim rules |
| [recipes/frames.md](recipes/frames.md) | generated synthetic bench frames |
| [HOSTAPD-BRINGUP.md](HOSTAPD-BRINGUP.md) | v2: real-AP-fire gaps, module identity, exact hostapd flow, expected logs, readback, failure table |
| [tools/build-bench-module.sh](tools/build-bench-module.sh) | build the deployable v2 bench module (size/sha) |
| [tools/push-hostapd-ucode.sh](tools/push-hostapd-ucode.sh) | bench workaround for the missing hostapd ucode files |
| [image/wifi-scripts-ucode.md](image/wifi-scripts-ucode.md) | image fix: CONFIG_PACKAGE_wifi-scripts=y |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact commands and results |

Quick start:

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    ./tools/build.sh && ./tools/ap_start_re.py --selftest
    ./tools/build-bench-module.sh      # deployable v3 module: size/sha
    ./tools/kernel-check.sh            # zero-warning module build

Cross-references: [../wifi-vap-start](../wifi-vap-start/) (mode-1 VAP base +
WAL envelope), [../wifi-scan-ap](../wifi-scan-ap/) (provider contract and
frame-post gate), [../wifi-datapath](../wifi-datapath/) (firmware table A),
[../wifi-proto-seq](../wifi-proto-seq/) (g0 id1 config carrier),
[../wifi-vendor-userspace](../wifi-vendor-userspace/) (vendor hostapd flow).
