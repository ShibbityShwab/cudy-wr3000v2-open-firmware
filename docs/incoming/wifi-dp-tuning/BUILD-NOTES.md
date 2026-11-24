# BUILD-NOTES - wifi-dp-tuning (task-134), host-only, 2026-11-27

## 1. Environment (authoring host)

* kernel tree: /home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
  (override with KDIR_OVERRIDE/STAGING_OVERRIDE; a fallback tree changes the
  unstripped .ko sha, not the stripped artifact's .text)
* toolchain: openwrt-lab-upstream-target/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi
* host cc: gcc (C99, -Werror -pedantic for the host selftests)
* project python: /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
* base trees (never edited on disk): ../wifi-dp-air/src, ../wifi-ap-start
  (provider patch v7 + src/hi5622_ap_frames.*), ../wifi-dp-txrx (inherited
  portable selftest)

## 2. Canonical build

    cd opensource/docs/incoming/wifi-dp-tuning && ./tools/build.sh

Measured on the authoring host (all PASS, 16/16 checks):

    == stage + patch      combined base staged, ap-start v7 + patches/0001 applied
    == provenance         5 changed files reproduce byte-for-byte; patch scope 5
    == host tests         TXQ layout 37/37 PASS; inherited portable TX/RX 254/254 PASS
    == module build       0 compiler/modpost warnings
    == vermagic           6.18.60 SMP mod_unload ARMv7 p2v8
    == depends            cfg80211
    == contract           symbols dp_txrx_txq_publish/user_block/flags_set +
                          hi5622_dp_{txq_show,tx_complete_poll_store,mac_ext_show,
                          mac_tail_init_store}; attrs dp_txq_table,
                          dp_tx_complete_poll, dp_mac_ext, dp_mac_tail_init
    == params             txq_publish/tx_complete_ms/mac_tail_writes recorded
    == strip              debug strip leaves .text byte-identical
    == sequence           G-TXQ ladder 33 steps / 366 lines; 16/16 checks PASS
    == publication        no objects/binaries/blobs in the lane; SPDX on all C/H

## 3. Artifact (canonical run)

| item | value |
| --- | --- |
| module | hi5622_wifi.ko (debug-stripped, path-independent) |
| size | **251,924 B** |
| sha256 | **175a4617f208ac7fb67cbd9074cb4eac18876ebf973d905d650f185eed7cc425** |
| unstripped | 947,432 B sha256 dc35e03802aa44a49ff3abb9659b289059262598532876fe50df8b357bb114e5 |
| ELF | 32-bit LSB relocatable ARM EABI5 |
| base | wifi-dp-air/src + ap-start v7 provider patch + hi5622_ap_frames.{c,h} + patches/0001 |
| prior fired combined module | 242,572 B sha256 44c6cc12 (wifi-ap-start combined, task-121) |
| merge patch | patches/0001-txq-consumer-tuning.patch, 27,265 B sha256 22579b747b3f7cc271b09f6f2c0d5050e8b4fe1f4b88afe87da3ebb4daffeb13 |

The .ko is local evidence only (never committed). The stripped artifact is
build-path independent (debug info removed; .text compared byte-identical
before/after strip); still record sha256sum/modinfo of the exact file you
deploy.

## 4. Module parameters (added by this lane)

| param | default | meaning |
| --- | --- | --- |
| txq_publish | 1 | publish the vendor TXQ-table queue state after each TX post (0 = pre-tuning wptr-only behaviour) |
| tx_complete_ms | 2000 | bounded dp_tx_complete_poll window |
| mac_tail_writes | 0 | opt-in [U] experiment: dp_mac_tail_init writes the vendor-live 4 to MAC band0 +0x5c/+0x6c |

Inherited parameters unchanged: fw_path, wifi_block_path, hcc_poll_ms,
h2d_send_budget, allow_frames, frame_gap_ms, vap_start_mode,
vap_start_module, scan_timeout_ms, scan_dwell_ms, cfg80211_auto, band_force ...

Load parameters for the G-TXQ ladder (ladder JSON pins them):

    fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local, never committed>
    allow_frames=12 frame_gap_ms=100 h2d_send_budget=4
    vap_start_mode=1 vap_start_module=0
    txq_publish=1 tx_complete_ms=3000 mac_tail_writes=0

## 5. Deployment order (attended; see ladder/LADDER-G-TXQ.md)

1. insmod cfg80211.ko first (image), then the tuned hi5622_wifi.ko with the
   parameters above (insmod, not modprobe - module params are not passed by
   modprobe on the bench image [V*]);
2. boot ladder (prepare/upload/verify/seed/hcc_arm/hcc_loop/release), golden
   page 0x86807FEF/7/2/0xABCD, state 1, mask 0;
3. C2: h2d_ctx_create_t1 + h2d_txq_sync72 (the 0xd8 context-consumer enable
   is data[0]=1);
4. iface + one scan (firmware VAP/channel precondition), dp_probe, dp_setup,
   h2d_txq_addr_sync;
5. cat dp_mac_ext; cat dp_txq_table; echo 1 > dp_tx_frame; cat dp_tx_status;
   cat dp_txq_table; echo 1 > dp_tx_complete_poll; cat dp_tx_status;
   echo 1 > dp_rx_poll; cat dp_rx_capture; post packs;
6. optional same-boot rung (only if G6 returned POST-ONLY): rebuild is not
   needed - mac_tail_writes must be set at load time, so this needs its own
   boot: load with mac_tail_writes=1, then after dp_setup run
   echo 1 > dp_mac_tail_init before the TX post.

One boot = one ctx-create + one txq_sync72 + one scan + one dp_setup + one
0x0320 + at most the post budget of 8 TX frames + one completion poll + one
RX poll; never retry a failed step; never rmmod after the 0x5A5A release.

## 6. Handoffs

* **bench-session / bench tooling:** the new attribute names must be added to
  bench_guards.DP_ATTRS (and dp_tx_complete_poll/dp_mac_tail_init to
  DP_WO_ATTRS) before the G-TXQ sequence can run as a phase; the lane's
  tools/validate_sequence.py injects them in memory only (no shared file
  edited).
* **wifi-dp-air / ap-start:** this lane is additive on the combined tree; the
  dp-air F-AIR ladder remains valid for the untuned module.
* **lead / STATUS:** host-only, not fired; does not move the bench state
  machine. The next attended data-plane rung is G-TXQ with the 251,924 B
  artifact.
* **future lane:** firmware-side TX consumer RE (which TXQ-table fields the
  firmware reads; the smac_msg_proc trx-enable family) is the decisive
  follow-up if G6 stays POST-ONLY.
