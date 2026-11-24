# Combined module: wifi-dp-air data plane + wifi-ap-start v7 (single boot)

**Lane:** wifi-ap-start (task-121), host-only, 2026-11-26.
Purpose: ONE module load for the last air-TX milestone - data-plane TX/RX
(with a radio that is actually up) on top of the proven AP-start path.

## 1. Patch stack (no unresolved conflicts)

| # | tree / patch | identity |
| --- | --- | --- |
| 1 | `../wifi-dp-air/src` (= wifi-scan-complete/src + dp-txrx merge rev2) | canonical dp module 886,864 B sha256 ca32aec4... (unstripped) |
| 2 | `provider/apstart-provider.patch` (this lane, v7) | applies to the dp tree with only a 3-line Kbuild offset; every other hunk exact |
| 3 | `src/hi5622_ap_frames.{h,c}` (this lane) | copied into the staged tree |

Only shared file is `Kbuild` (dp adds its objects, AP adds hi5622_ap_frames.o);
it applies cleanly. Reproducible build: `tools/build-combined-dp-ap-module.sh`.

## 2. Module identity (canonical run 2026-11-26)

| artifact | size | sha256 |
| --- | --- | --- |
| deployable, debug-stripped (path-independent) | **242,572 B** | **44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b** |
| unstripped reference | 925,192 B | 21d051613b57177557f7ab75b6d3ea1d11ccd104cad1bf5dd05a7963235ae61d |

`vermagic=6.18.60 SMP mod_unload ARMv7 p2v8`, `depends=cfg80211`, 0
warnings/errors, `.text` byte-identical stripped vs unstripped. Symbols:
AP-start (vapscan_ap_start, connect/disconnect, mgmt_tx, mgmt_stypes,
change_beacon, VAP-base helper, carrier-aware labels) + data plane
(hi5622_dp_probe/setup/txrx, dp_tx_frame). Attributes present: dp_probe,
dp_setup, dp_txq_sync, h2d_txq_addr_sync, dp_tx_frame, dp_tx_status,
dp_rx_poll, dp_rx_capture, dp_status, dp_rings, h2d_status, cfg80211_status.

## 3. Single-boot command sequence (the milestone)

    # sysfs root of the endpoint
    D=/sys/bus/pci/devices/0000:00:00.0

    # 0. cold boot + cfg80211 first (bench image), then ONE module load:
    insmod /tmp/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin \
        allow_frames=12 frame_gap_ms=100 h2d_send_budget=4 \
        vap_start_mode=1 vap_start_module=0
    #    (vap_start_first is NOT required: .start_ap posts the mode-1 VAP
    #     base itself, once per load. If a scan runs later it will not re-post it.)

    # 1. C2 ladder (unchanged): h2d ctx-create t1 + txq_sync72
    echo 3 > $D/h2d_ctx_create_t1 ; cat $D/h2d_status   # gates ok
    echo 1 > $D/h2d_txq_sync72     ; cat $D/h2d_status   # ok=2 timeout=0

    # 2. AP interface + hostapd (proven v7 flow)
    iw dev wlan0 del 2>/dev/null
    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up
    hostapd -B -t /tmp/hostapd-open.conf      # 4 processes, AP-ENABLED

    # 3. data plane on the SAME load, radio already up
    cat  $D/dp_probe                 # ONE read-only MAC word; probe_ok=1
    echo 1 > $D/dp_setup             # rings + device-RAM table + MAC program
    echo 1 > $D/h2d_txq_addr_sync    # key-0x0320 TXQ-table address hand-off
    echo 1 > $D/dp_tx_frame          # first data-plane TX (32-B bcast mgmt)
    cat  $D/dp_tx_status             # expect COMPLETED now that the radio is up
    cat  $D/dp_rx_poll               # RX poll evidence

## 4. Expected gate values

| gate | expected |
| --- | --- |
| `h2d_ctx_create_t1` / `h2d_txq_sync72` | ok=2, timeout=0, lane open |
| `dp_probe` | MAC band-0 word read (vendor boot 0x00000100 expected; any value recorded), `probe_ok=1` |
| `dp_setup` | `setup_done=1 failed=0 mac_regs=14 table_entries=8` |
| `dp_rings` | +0x38/+0x3c=0x8000, +0x48=0xfffffcc1, 8 entries |
| `h2d_txq_addr_sync` | `txq_addr_sent=1 ok=3 timeout=0 frames_sent=3` |
| `dp_tx_frame` | posted=1 (frame + MSDU-info ring committed) |
| `dp_tx_status` | **COMPLETED** (not POST-ONLY: firmware VAP/radio is up via the AP start) |
| `dp_rx_poll` | poll evidence (RX ring state; capture optional) |

## 5. Stop rules / caveats

* One `dp_probe`, one `dp_setup`, one `h2d_txq_addr_sync`, one `dp_tx_frame`
  per module load; a read-back mismatch closes the data-plane lane for the
  load (no retry).
* `h2d_send_budget=4` covers ctx-create + txq_sync72 + key-0x0320.
  `allow_frames=12` covers add/notify (2) + AP start (4).
* The AP path needs `vap_start_mode=1` (g0 id1 static VAP base). Posting AP
  frames before that base would make the firmware worker NULL-deref at
  module_ctx+0x190 (power-cycle required) - v7 now always posts the base first.
* If the chip ever enters the exception state (mask != 0, page != golden):
  power-cycle; do not reuse the boot.
* Data-plane TX completion is the milestone read; management/data RX payloads
  remain firmware-side work for later lanes.
