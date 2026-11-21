# Blocker 5 - cudy compatible ordering + OpenWrt metadata

**STATUS: ADOPTED after hardware boot-test image c3ef2a50 (2026-11-21).**  The target DTS starts with cudy,wr3000-v2 and SUPPORTED_DEVICES includes it; boot test shows board_name=cudy,wr3000-v2 and the compatible order.  Draft retained for provenance.

## Proposal (upstream)

Root compatible, most-specific first:

    compatible = "cudy,wr3000-v2", "hisilicon,luofu-r116", "hisilicon,luofu";

The machine descriptor in kernel patch 001 already lists all three in that
order, so no kernel change is needed.  The current conservative DTS lacks
`cudy,wr3000-v2`; the DTS draft adds it first (`dts-compatible-order.diff`).

## OpenWrt-side mapping

At runtime OpenWrt's `board_name` is the first root compatible, so it becomes
`cudy,wr3000-v2`.  The target must agree on that string:

- `target/linux/hisilicon/image/luofu.mk`: add `cudy,wr3000-v2` to
  `SUPPORTED_DEVICES` (draft `openwrt-supported-devices.diff`).  The existing
  hisilicon entries stay for compatibility with older DTBs.
- `base-files/etc/board.d/02_network`: the case already matches
  `hisilicon,luofu-r116|cudy,wr3000-v2`; update the stale TODO comment
  (`openwrt-board-name-comment.diff`).
- `DEVICE_DTS`/image build are unchanged; sysupgrade metadata is generated
  into the container from `SUPPORTED_DEVICES`, so the new board_name passes the
  image's own device check.

## Verification

1. Host-side: rebuild the image metadata and confirm it lists
   `cudy,wr3000-v2 hisilicon,luofu-r116 hisilicon,luofu`; a mocked
   `sysupgrade -T` check should accept the board.
2. Hardware: the compatible-order change is an **active DTS delta** - boot-test
   the installed image, confirm `/tmp/sysinfo/board_name` is
   `cudy,wr3000-v2`, board.d still applies the network/MAC setup, and a
   sysupgrade check on the unit accepts the image.  Do not bundle with unrelated
   active-DTS deltas; one boot-test per delta.

## Draft artifacts

- `dts-compatible-order.diff`
- `openwrt-supported-devices.diff`
- `openwrt-board-name-comment.diff`

The OpenWrt files are outside the kernel series and are proposed target-tree
changes only.
