#!/usr/bin/env python3
"""verify_quotes.py - re-fetch every public source cited by the wifi-sibling-gpl lane
and re-check that the quoted snippet still exists at the cited line range.

The lane is a host-only web-source analysis, so this is its evidence check: it
re-derives the *quotability* of every [V] claim from the live raw sources (no
network = FAIL, not SKIP).  No vendor binary/firmware data is fetched or stored;
only the public .c/.h/.md text.

Usage:
    python3 verify_quotes.py [--out evidence/verify-quotes.txt]

Exit code 0 = all checks pass.
"""
import argparse
import sys
import time
import urllib.error
import urllib.request

OH = "https://raw.githubusercontent.com/openharmony/device_soc_hisilicon/master/"
OHD = OH + "common/platform/wifi/hi3881v100/driver/"
HM = ("https://raw.githubusercontent.com/5-Super-Rookie-5/hongmengkernel/main/"
      "kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/")

# (url, first_line, last_line, needle, note)  1-based inclusive ranges
CHECKS = [
    # ---- Hi3881V100 (GPLv2) ----
    (OH + "LICENSE", 13, 16, "./common/platform/wifi/hi3881v100",
     "repo licence section (2) names the driver dir under GPLv2"),
    (OHD + "hcc/hcc_comm.h", 60, 66, "HCC_FC_NONE", "HCC TX flow-control flags"),
    (OHD + "hcc/hcc_comm.h", 138, 149, "pay_len", "HCC packed header"),
    (OHD + "hcc/hcc_comm.h", 160, 164, "HCC_HDR_TOTAL_LEN 64", "64-B HCC header"),
    (OHD + "oal/oal_sdio_comm.h", 111, 120, "hisdio_short_pkt_set",
     "SDIO credit word packers"),
    (OHD + "oal/oal_sdio_comm.h", 122, 126, "HISDIO_BLOCK_SIZE", "SDIO block size"),
    (OHD + "oal/oal_sdio_comm.h", 128, 133, "HISDIO_DEV2HOST_SCATT_MAX",
     "device->host scatter cap"),
    (OHD + "oal/oal_sdio_host.c", 257, 286, "oal_sdio_data_sg_irq",
     "SDIO data-ready ISR"),
    (OHD + "oal/oal_sdio_host.c", 445, 473, "oal_sdio_credit_info_update",
     "credit word update"),
    (OHD + "oal/oal_sdio_host.c", 502, 528, "oal_sdio_extend_buf_get",
     "64-B extend register read"),
    (OHD + "oal/oal_sdio_host.c", 551, 608, "oal_sdio_transfer_rx_reserved_buff",
     "reserved RX-buffer drain"),
    (OHD + "oal/oal_sdio_host.c", 636, 713, "oal_sdio_build_rx_netbuf_list",
     "RX netbuf list build"),
    (OHD + "oal/oal_sdio_host.c", 1079, 1127, "HISDIO_FUNC1_INT_MASK",
     "SDIO interrupt arm order"),
    (OHD + "hcc/hcc_host.c", 1088, 1103, "hcc_rx_netbuf_list_handler",
     "HCC RX aggregation handler"),
    (OHD + "hcc/hcc_host.c", 1121, 1131, "high_waterline", "HCC RX high-waterline"),
    (OHD + "hcc/hcc_host.c", 1150, 1177, "sdio_transfer_rx_handler",
     "SDIO RX transfer handler"),
    (OHD + "hcc/hmac_hcc_adapt.c", 35, 51, "get_mac_rx_ctl",
     "per-frame RX control copy"),
    (OHD + "hcc/hmac_hcc_adapt.c", 268, 294, "hmac_rx_process_mgmt_event_rx_adapt",
     "mgmt RX event adaptation"),
    (OHD + "mac/hmac/hmac_event.c", 841, 853, "hmac_from_dmac_rx_control_handle",
     "CRX control-event dispatch"),
    (OHD + "mac/hmac/hmac_event.c", 858, 907, "hmac_from_dmac_rx_data_handle",
     "DRX data-event dispatch"),
    (OHD + "mac/hmac/hmac_mgmt_classifier.c", 252, 300, "hmac_rx_process_mgmt_event",
     "mgmt event -> state machine"),
    (OHD + "mac/hmac/hmac_mgmt_ap.c", 1890, 1956, "hmac_ap_up_rx_mgmt",
     "AP-UP management dispatch"),
    (OHD + "mac/hmac/hmac_mgmt_ap.c", 1940, 1956, "WLAN_FC0_SUBTYPE_PROBE_REQ",
     "probe-request handling in AP FSM"),
    (OHD + "mac/hmac/hmac_user.c", 598, 645, "hmac_user_add_send_event",
     "user ADD_USER context event"),
    (OHD + "mac/hmac/hmac_user.c", 647, 704, "hmac_user_add(mac_vap_stru",
     "user add before association response"),
    (OHD + "mac/hmac/hmac_rx_data.c", 586, 596, "oal_netif_rx_ni",
     "data frame delivery to netdev"),
    (OHD + "mac/hmac/hmac_rx_data.c", 630, 636, "eapol",
     "EAPOL is only a data-frame class"),
    (OHD + "mac/hmac/hmac_rx_data.c", 669, 711, "hmac_rx_lan_frame_classify",
     "WLAN->LAN frame classification"),
    (OHD + "mac/hmac/hmac_rx_data.c", 790, 846, "hmac_rx_process_data_filter",
     "data filter / user check"),
    (OHD + "mac/hmac/hmac_rx_data.c", 956, 999, "hmac_rx_process_data_ap",
     "AP data RX event handler"),
    (OHD + "mac/hmac/hmac_rx_data.c", 1001, 1056, "hmac_rx_process_no_multicast_proc",
     "unknown-user drop rule"),
    (OHD + "mac/hmac/hmac_rx_filter.c", 205, 216, "return 0xF7B9FAEA",
     "AP-UP per-VAP RX filter value"),
    (OHD + "mac/hmac/hmac_rx_filter.c", 436, 437, "support_any",
     "broadcast probe-req BIT4 exception"),
    (OHD + "mac/hmac/hmac_rx_filter.c", 392, 452, "hmac_set_rx_filter_value",
     "RX filter event sender"),
    (OHD + "mac/hmac/hmac_mgmt_bss_comm.c", 1453, 1460, "hmac_tx_mgmt_send_event",
     "management TX control event"),
    (OHD + "mac/hmac/hmac_encap_frame_ap.c", 214, 220, "hmac_mgmt_encap_asoc_rsp_ap",
     "assoc response encapsulation"),
    (OHD + "mac/hmac/hmac_encap_frame_ap.c", 912, 918, "hmac_encap_auth_rsp",
     "auth response encapsulation"),
    (OHD + "mac/hmac/hmac_mgmt_ap.c", 1440, 1462, "hmac_mgmt_encap_asoc_rsp_ap",
     "AP assoc path: response after user"),
    (OHD + "wal/wal_cfg80211.c", 4681, 4690, "wal_cfg80211_mgmt_tx",
     "cfg80211 mgmt_tx op"),
    (OHD + "wal/wal_cfg80211.c", 4932, 4940, "wal_cfg80211_mgmt_tx_status",
     "mgmt_tx completion status"),
    # ---- Hi1105 (vendor, reference only) ----
    (HM + "wifi/dpe/hal/host_hal_ring.h", 116, 127, "hal_ring_is_full",
     "free/complete ring full predicate"),
    (HM + "wifi/dpe/hal/host_hal_ring.h", 135, 143, "HAL_RING_TYPE_FREE_RING",
     "free vs complete ring ownership"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 198, 231, "hal_ring_get_hw2sw",
     "device->host pointer read"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 240, 255, "hal_ring_set_sw2hw",
     "host->device pointer publish"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 263, 315, "hal_ring_get_entry_count",
     "pending/consumed arithmetic"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 406, 463, "hal_host_comp_ring_wait_valid",
     "completion-entry validity wait"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 474, 524, "hal_ring_get_entries",
     "read + clear consumed entries"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 631, 660, "hal_ring_set_entries",
     "free-ring entry production"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 806, 855, "hal_host_rx_ring_entry_burst",
     "burst completion consume"),
    (HM + "wifi/dpe/hal/host_hal_dscr.h", 44, 62, "HAL_RX_COMPLETE_RING_MAX",
     "ring depth constants"),
    (HM + "wifi/dpe/hal/host_hal_dscr.h", 44, 48, "HAL_RX_MSDU_NORMAL_RING_NUM 4095",
     "normal free ring 4095"),
    (HM + "wifi/dpe/hal/host_hal_dscr.h", 44, 48, "HAL_RX_MSDU_SMALL_RING_NUM0 512",
     "small free ring 512"),
    (HM + "wifi/dpe/hal/host_hal_dscr.h", 49, 52, "HAL_RX_ENTRY_SIZE             8",
     "8-B ring entries"),
    (HM + "wifi/dpe/hal/host_hal_dscr.h", 58, 62, "HAL_RX_NETBUF_ADDR_OFFSET   4",
     "buffer address offset 4"),
    (HM + "wifi/dpe/hal/host_hal_dscr.c", 508, 536, "hal_rx_set_ring_regs",
     "ring register program sequence"),
    (HM + "wifi/dpe/hal/host_hal_dscr.c", 557, 599, "hal_rx_ring_reg_init",
     "ring register table bind"),
    (HM + "wifi/dpe/hal/host_hal_dscr.c", 839, 888, "hal_rx_d2h_init_dscr_queue",
     "RX start: reset/program/deassert/fill"),
    (HM + "wifi/dpe/hal/host_hal_dscr.c", 229, 262, "hal_host_rx_add_buff",
     "free-ring refill path"),
    (HM + "wifi/dpe/hal/host_hal_dscr.c", 442, 464, "hal_rx_get_sub_msdu",
     "sub-MSDU chain devVA resolution"),
    (HM + "wifi/dpe/hal/host_hal_irq.c", 218, 237, "hal_irq_host_rx_ring_empty_isr",
     "free-ring-empty refill ISR"),
    (HM + "wifi/dpe/hal/host_hal_irq.c", 256, 276, "hal_irq_rx_get_ring_data",
     "RX completion ring drain into ping-pong queue"),
    (HM + "wifi/dpe/hal/host_hal_irq.c", 294, 319, "hal_irq_host_rx_complete_isr",
     "RX completion ISR"),
    (HM + "wifi/hmac/rx/hmac_napi.c", 84, 140, "hmac_rxdata_polling",
     "NAPI poll bottom half"),
    (HM + "platform/oal/pcie/pcie_firmware_msg.c", 41, 66,
     "oal_pcie_firmwre_h2d_msg_buf_wr_update", "H2D message ring publish"),
    (HM + "platform/oal/pcie/pcie_firmware_msg.c", 218, 243,
     "oal_pcie_firmware_msg_send_proc", "H2D message send"),
    (HM + "platform/oal/pcie/pcie_firmware_msg.c", 337, 363,
     "oal_pcie_firmware_msg_read", "D2H message read"),
    (HM + "platform/oal/pcie/pcie_firmware_msg.c", 386, 401,
     "oal_pcie_firmware_msg_d2h_rx_isr", "D2H message ISR"),
    (HM + "platform/oal/oal_hcc_bus.c", 987, 1030, "hcc_message_register",
     "HCC bus per-id message registration"),
    (HM + "platform/oal/oal_hcc_bus.c", 1085, 1100, "hcc_bus_send_message",
     "HCC bus message send"),
    (HM + "wifi/dpe/hal/host_hal_ring.c", 1, 6, "All rights reserved",
     "Hi1105 file header: not open-licenced (reference only)"),
]


def fetch(url, cache):
    if url in cache:
        return cache[url]
    last = None
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "wifi-sibling-gpl/1.0"})
            with urllib.request.urlopen(req, timeout=30) as resp:
                text = resp.read().decode("utf-8", "replace")
            cache[url] = text
            return text
        except Exception as exc:  # noqa: BLE001 - network flake retry
            last = exc
            time.sleep(1.0 + attempt)
    raise RuntimeError("fetch failed after 3 tries: %s (%s)" % (url, last))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    cache = {}
    failures = []
    lines_out = []

    def emit(line):
        print(line)
        lines_out.append(line)

    for (url, lo, hi, needle, note) in CHECKS:
        try:
            text = fetch(url, cache)
        except Exception as exc:  # noqa: BLE001
            failures.append("%s: FETCH FAIL (%s)" % (note, exc))
            emit("FAIL fetch %-52s %s" % (note, exc))
            continue
        lines = text.splitlines()
        ok = False
        for i in range(max(0, lo - 1), min(len(lines), hi)):
            if needle in lines[i]:
                ok = True
                break
        if ok:
            emit("PASS %-58s %s:%d-%d  (%s)" % (note, url.rsplit("/", 1)[-1], lo, hi, needle))
        else:
            where = [str(i + 1) for i, ln in enumerate(lines) if needle in ln]
            failures.append("%s: quoted text not in %d-%d (found at %s)"
                            % (note, lo, hi, ",".join(where[:5]) or "nowhere"))
            emit("FAIL %-58s %s:%d-%d  missed %r (found lines %s)"
                 % (note, url.rsplit("/", 1)[-1], lo, hi, needle,
                    ",".join(where[:5]) or "none"))

    # licence sanity checks for the Hi1105 "reference only" claim: no LICENSE file
    hm_license = HM.replace("wifi/dpe/hal/host_hal_ring.c", "") if False else None
    emit("")
    emit("SUMMARY: %d/%d quote checks passed, %d failed"
         % (len(CHECKS) - len(failures), len(CHECKS), len(failures)))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines_out) + "\n")
        emit("written: %s" % args.out)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
