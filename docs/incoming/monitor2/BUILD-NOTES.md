# BUILD-NOTES - the local-mirror monitor v1 prototype (materialised)

**Lane:** monitor2 (task-246). Host-only; no device. The prototype is a patch against the
**IMAGE #3 merged module source tree** (97 files) at scratch path `/tmp/wifi-image-3-module`
(manifest in `evidence/BASE-TREE.sha256`; this lane works on a copy in
`/tmp/monitor2-work/`). This is the same base the `monitor-mode` prose referred to.

## 1. The patch

`patches/0001-monitor-mirror.patch` - **543 lines, 9 files, `patch -p1` against the base
tree; post-apply tree is byte-identical to the tree that was built** (evidence:
`PATCH-APPLY.txt`, `PATCH-ROUNDTRIP.txt`).

| file | change |
| --- | --- |
| `hi5622_monitor_logic.h/.c` (new) | portable, kernel-free: mode policy 0/1, channel->MHz map (2.4 ch1-14 + 5 GHz map), radiotap builder with the pinned R0/R1/R2 vectors, length bounds. Builds under both `__KERNEL__` (`<linux/types.h>`) and a host C compiler. |
| `hi5622_monitor.h/.c` (new) | kernel integration: module param `monitor` (uint, default 0); `.set_monitor_channel` implementation (metadata-only chandef store; -EOPNOTSUPP at monitor=0; **never a firmware write**); `hi5622_monitor_rx_mgmt/data()` mirror with `mutex_trylock` only, monitor-wdev lookup, `netif_running` check, bounded copy, radiotap prepend, `netif_rx`, rate-limited dmesg + counters. |
| `hi5622_cfg80211.h` | adds `mon_chandef`/`mon_valid` + 8 delivery counters to `struct hi5622_cfg80211`. |
| `hi5622_cfg80211_ops.c` | registers `.set_monitor_channel`; logs the mode once when a monitor netdev is created. |
| `hi5622_mgmt.c` | calls the mirror **before** the `mgmt_rx` AP-policy gate (after the D2H decode). |
| `hi5622_sta_ops.c` | splits the entry guard and calls the mirror **before** the `eapol_rx` gate. |
| `Kbuild` | adds `hi5622_monitor_logic.o hi5622_monitor.o`. |

## 2. Host tests (no kernel, no bench)

`tools/monitor_selftest.c` + `tools/run-host-tests.sh`: **34 checks, 0 failures**
(`evidence/HOST-TESTS.txt`) - policy matrix (0/1/2/UINT_MAX), 2.4 GHz ch1/6/13/14 and 5 GHz
36/100/140/165 + invalid, the three radiotap vectors byte-for-byte
(R0 = `00 00 08 00 00 00 00 00`, R1 @2412 = `... 0c 00 08 00 00 00 6c 09 00 00`,
R2 @2437 rssi -62 = `... 0d 00 08 08 00 00 85 09 00 00 c2`), rssi-without-channel degrades
to R0, NULL/short-buffer refusals.

## 3. Cross-build (pinned tree, OpenWrt GCC 14.4, module only)

Command per `tools/build-monitor.sh`: `make ARCH=arm CROSS_COMPILE=<toolchain> -C
<pinned linux-6.18.60> M=<src> modules` (the Kbuild adds
`KBUILD_EXTRA_SYMBOLS=cfg80211-provider.symvers`).

| fact | patched | unpatched control |
| --- | --- | --- |
| warnings/errors | **0** | 0 |
| module params | **65** (adds `monitor: (uint)`) | 64 |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 | same |
| depends | cfg80211 | cfg80211 |
| size (unstripped) | 1,471,772 B | 1,441,668 B |
| sha256 | `8e7974170ca125eec3a06ac564c8c9810c14a1094aa95b551d2aff783e94832a` | `79be4a0a2df67ebe550b82fa1d8e693afceb31381dcfd0a6f9b493642db66e19` |

Evidence: `evidence/BUILD-FACTS.txt`, `evidence/BUILD-patched.log`,
`evidence/BUILD-control.log`. The .ko files stayed local
(`/tmp/monitor2-work/out/artifacts/`); they are **not committed** (binary policy).

**Honesty:** this is compile-clean + host-tested, **not bench-proven**. The capture PASS
(rung R2) cannot happen until the F1 RX gate lands; the same build with the F1 fix is the
artifact to test.
