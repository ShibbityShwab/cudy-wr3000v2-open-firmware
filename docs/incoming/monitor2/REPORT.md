# REPORT - monitor mode + spectral: materialised, verified, and one decode further

**Lane:** monitor2 (shared task **task-246**), **HOST-ONLY**, 2026-11-26. No device, no UART,
no network to the router, no flash, no fire. Companion files: [VERIFY.md](VERIFY.md),
[CFG80211-MONITOR.md](CFG80211-MONITOR.md), [SPECTRAL-S1.md](SPECTRAL-S1.md),
[BUILD-NOTES.md](BUILD-NOTES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md).

**Grades:** [V] verified here from a named local artifact (source, disassembly, build log);
[V\*] committed earlier lane and re-checked; [M] bench-measured earlier; [I] inference;
[U] unknown.

## 0. Verdict (read this first)

1. **The task-241 deliverable did not exist as artifacts.** Commit 1a2d068 committed six
   markdown files; its `src/`, `patches/`, `tools/`, `evidence/` are empty and its
   BUILD-NOTES.md is absent. The prose claims a "prototype patch, host-tested, compile-clean"
   that cannot be reproduced from the repository [V].
2. **Monitor mode's cfg80211 half was always complete; the capture half is one feature + one
   fix away.** The IMAGE #3 driver advertises MONITOR, creates the radiotap netdev and stores
   iftype changes [V], but has no `.set_monitor_channel` and no RX delivery. This lane
   **materialised the missing monitor v1**: a 543-line, 9-file patch against the IMAGE #3
   merged tree, host-tested **34/34**, cross-built **0 warnings/errors** (65 params vs 64
   control, depends=cfg80211, vermagic 6.18.60) [V].
3. **Two errors in the task-241 design are corrected here** [V]: the 6.18.60
   `.set_monitor_channel` takes **three** arguments (wiphy, dev, chandef), and
   `SET_CHANNEL` on a monitor iface requires **`cfg80211_has_monitors_only()`**, so the
   monitor netdev must be the only RUNNING iface - the FIRE-RECIPE ordering was fixed
   accordingly.
4. **Monitor capture is still gated by the F1.** Zero 802.11 frames reach the host until the
   host-bound `g1 id13` RX copy is fixed; the mirror hooks sit at the two existing sinks
   (`hi5622_mgmt_deliver`, `hi5622_sta_ops_rx_deliver`) before their AP-policy gates, so
   the F1 fix lights them up with no further monitor work [V/V\*].
5. **The vendor driver has no monitor implementation to port** [V]: in 2.5.24
   `wal_cfg80211_add_virtual_intf` @0x103a80 is a fail-stub returning -11 and
   `change_virtual_intf` @0x103958 only stores the iftype; in 2.4.15 the only `monitor`
   string is the unrelated `hmac_fbt_start_monitor_timer`, with zero `promis`/`radiotap`
   symbols. The GPL-2.0 hi3881 family remains a shape reference only - its cfg id 522 is a
   different chip's message and must not be sent.
6. **Spectral (PSD) S-1 is now half-closed** [V]: the pointer chain
   `state_band+0x150 -> 64-byte object -> [obj+0x10] -> per-band mapped CA window` is
   decoded, with the two mapping helpers (10-word and 6-word tables), the per-band init
   order and the complete default register set. The last check is the .rodata relocation
   addend naming which 10-word table is word-2 (two candidate families: 0x40046000/0x40066000
   vs 0x40044000/0x40064000) - see SPECTRAL-S1 §2. **No PSD register write is allowed until
   that check + an attended read-only probe.**

## 1. What this lane changed vs task-241

| item | task-241 | monitor2 |
| --- | --- | --- |
| patch / src / tools / evidence | described, not committed | **real**: patch + 4 source files + selftest + evidence, committed |
| host tests | claimed | **34 checks, 0 failures** (pinned radiotap vectors, policy, channel map, bounds) |
| build | claimed "compile-clean" | **patched vs unpatched control builds, 0 warnings, 65 vs 64 params, sha256s recorded** |
| .set_monitor_channel signature | 2 args (wrong) | **3 args (6.18.60), implemented correctly** |
| channel-set preconditions | not mentioned | **monitors-only rule documented; ladder ordering corrected** |
| PSD accessor counts | "19+17" / "37" | **21 set + 16 get = 37** |
| PSD bank (S-1) | unknown | **pointer chain + helpers + defaults decoded; one reloc check left** |

## 2. The materialised monitor v1 (design summary)

* `monitor=0` (default): inert; `set channel` = -EOPNOTSUPP; nothing mirrored.
* `monitor=1`: the frames that already reach the two RX sinks are copied to the first
  monitor netdev as radiotap + 802.11 MPDU (`mutex_trylock` only; drop + count on busy;
  never blocks the RX path). Radiotap: R0 8 B (no metadata), R1 12 B (CHANNEL, flags 0 -
  never guessed), R2 13 B (CHANNEL | DBM_ANTSIGNAL). Data uses the stored chandef when
  present; mgmt uses the decoded event's freq/RSSI when plausible.
* `monitor>1`: refused.
* **No firmware write anywhere in the monitor path.** The channel op stores a chandef for
  radiotap metadata only. Full details in [CFG80211-MONITOR.md](CFG80211-MONITOR.md),
  build facts in [BUILD-NOTES.md](BUILD-NOTES.md).

## 3. Limits / honesty

* The prototype is **compile-clean and host-tested, not bench-proven**. R1 (cfg80211 only)
  is fireable once a human attends; **R2 capture cannot pass until the F1 lands**.
* The mirror is not promiscuous mode; frames the firmware filters out before the two sinks
  are not visible. Monitor TX is not implemented.
* Spectral: static decode only; nothing was read from or written to silicon by this lane.
  S-1 is not fully closed (the relocation check is outstanding) and S1+ stay blocked.
* The 2.5.24 vendor module file was not available during this lane (only its nm/objdump
  artifacts); the 2.4.15 module is local. The relocation check should use whichever .ko is
  available (same code family).
* The earlier task-241 files remain in `docs/incoming/monitor-mode/` as design prose; treat
  **this directory as the artifact source of truth** and its VERIFY.md as the errata list.
