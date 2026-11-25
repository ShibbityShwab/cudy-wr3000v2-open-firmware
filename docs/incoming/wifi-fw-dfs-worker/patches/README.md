# patches/ - source-level recipe for the field-B fix

`0001-fieldb-context.patch` changes the two hard-coded header-field-B values
that made the 0x01F7 and 0x00F7 frames address the wrong module context:

* `hi5622_dfs_mode.h`: `HI5622_DFS_MODE_FB 0x04u -> 0x00u` (the 0x01F7 carrier)
* `hi5622_desc_table.c`: `b[3] = 0x04u -> 0x00u` (the 0x00F7 carrier)

**Base:** the fired `hi5622_wifi.ko` source tree of lane ../wifi-dfs-mode-send
(the beacon5g + r1-band + 0x00F7 desc-table chain + `patches/0001-dfs-mode-send.patch`,
artifact sha256 `f5408567...66ac`). Apply with `git apply` / `patch -p1` from
that tree root.

**Status:** this lane is a host-only decode lane; it did **not** rebuild the
module and did **not** update the lane-190 golden-frame host tests (they
currently assert `hdr[3] = 0x04`). The implementation lane must (a) apply this,
(b) ideally derive the value from `hi5622_vap_start_module` instead of
hard-coding 0, (c) update the golden-frame tests to the fixed hex
`000100001d00010000005a5a0000000001000000f701010003ffffffff` and the 68-B
0x00F7 frame with `hdr[3]=0`, and (d) re-run build + host tests before the Lead
fires F2.

The fixed bytes are re-derived by `../tools/fw_dfs_worker_re.py --frame`;
the decode behind them is in ../REPORT.md sections 2-6.
