# BUILD-NOTES - wifi-dp-txrx (task-122)

Host-only lane. No device was contacted while writing, building or testing.

## Environment used (authoring host, 2026-11-26)

* kernel tree: /home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/
  target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
  (configured; the build script falls back to the other configured luofu
  6.18.60 trees if this one was cleaned)
* toolchain: staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi
* host python: .provision-venv/bin/python (pyelftools + capstone for the
  static-decode assertions)
* base source: ../wifi-datapath-bringup/src (E1), copied to /tmp for the
  patched build; the base lane is never edited on disk
* vendor artifacts (read in place, never committed): rootfs-2.4.15/lib/
  hisilicon/ko/hi5622v100_wifi.ko (md5 1745ef77...) and
  /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
  (sha256 7fc87e20..., 928,920 B)

## Measured results (./tools/build.sh)

    == 1. host portable TX/RX selftest (device-free) ==
    SUMMARY: 254 passed, 0 failed
    == 2. static-decode assertions (local vendor artifacts, host-only) ==
    SUMMARY: 87 passed, 0 failed, 0 skipped
    == 3. protocol-sequence validation (bench tooling, no device) ==
    SUMMARY: 12 passed, 0 failed  (21 steps, 205 rendered lines)
    == 4. kernel integration: scratch copy + patch + module build ==
    patch_builder: committed patch matches the builder output (no drift)
    size_bytes  : 540936
    sha256      : aae8487b4e4dad0c8ec0e20b33ef718e68a4e8a92d78f7f4c11afe1b5c12c502
    vermagic    : 6.18.60 SMP mod_unload ARMv7 p2v8
    warnings    : 0 (compiler+modpost)
    == 5. device-free source/publication checks ==
    OK

The E1 base module (unchanged code) was 462,952 B sha 8fc693ad...; this lane's
build adds the two new objects (portable dp_txrx.o + kernel hi5622_dp_txrx.o)
and is otherwise the same E1 driver: dp_probe/dp_setup/dp_rings/dp_txq_sync
are byte-for-byte the bringup implementation (only the bind/release/attr-group
patch differs).

## Handoffs

* **bench tooling (allowlist for the ladder):** add
  `dp_tx_frame, dp_tx_status, dp_rx_poll, dp_rx_capture` to
  bench_guards.DP_ATTRS/ALL_KNOWN_ATTRS; add `dp_tx_frame, dp_rx_poll` to
  DP_WO_ATTRS/WO_ATTRS (tools/validate_sequence.py prints the exact line).
  No timing/reboot change is needed: ladder F is one boot (F0-F3) and the
  post-run packs are the existing read pack shape.
* **integration lane (air TX):** merge this lane's `src/hi5622_dp_txrx.[ch]` +
  `src/dp_txrx.[ch]` into the scan-ap/vap-start provider module build (same
  base tree), apply the same two-line bind/release patch and the attribute
  group entries, keep the provider's mode-1 VAP create, then run F1 after the
  vap_add/vap_start pair. The module will then exercise an on-air frame.
* **lead / STATUS:** this lane is host-only and not fired; it does not move the
  bench state machine. The mode-1 VAP create (2026-11-26 14:00) remains the
  current bench milestone; this lane is the next data-plane step after it.

## Notes for the next builder

* The patch must only ever touch `src/hi5622_dp.c` and `src/Kbuild`; the build
  fails if it touches more (publication/scope check).
* The four attributes are deliberately separate from the E1 names so the
  existing E1 automation is untouched; promote them to a phase only after the
  tooling allowlist handoff.
* Never reuse a posted MSDU entry before completion: the driver refuses a
  second post after a table read-back mismatch and caps the load at 8 posts.
