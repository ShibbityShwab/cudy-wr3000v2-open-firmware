/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dp_frames.c - data-plane frame builders for the Hi5622V100 clean-room host.
 * Original code (lane wifi-datapath, task-105). See dp_frames.h + REPORT.md.
 */
#include "dp_frames.h"

#include <string.h>

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void dp_frame_reset(struct dp_frame *f)
{
    if (f == NULL)
        return;
    memset(f->buf, 0, sizeof(f->buf));
    f->len = 0;
}

/* Walk the 12-byte HCC header of one frame. The firmware-side hard checks are:
 * tag at +0x0a == 0x5a5a and declared total at +0x04 != 0 (and <= 0x680). */
int dp_frame_validate(const uint8_t *buf, size_t len)
{
    uint32_t total;

    if (buf == NULL || len < DP_HDR_LEN)
        return DP_ESHORT;
    if (get_le16(buf + 0x0a) != DP_TAG)
        return DP_ETAG;
    total = get_le16(buf + 0x04);
    if (total == 0u || total > DP_MAX_FRAME || total < DP_HDR_LEN)
        return DP_ELEN;
    if (len < total)
        return DP_ELEN;
    if ((buf[0] & 0x0fu) > 2u)
        return DP_EGROUP; /* only groups 0 and 2 are H2D-receivable */
    if ((buf[1] & 0x0fu) != 1u)
        return DP_EALLOC; /* alloc state nibble must be 1 on H2D */
    if (buf[9] != (uint8_t)(buf[1] >> 4))
        return DP_EDST; /* firmware copies hdr[1]>>4 to hdr[9] */
    return DP_OK;
}

int dp_frame_decode(const uint8_t *buf, size_t len, uint8_t *group,
                    uint16_t *id, uint16_t *total)
{
    int err = dp_frame_validate(buf, len);

    if (err != DP_OK)
        return err;
    if (group != NULL)
        *group = (uint8_t)(buf[0] & 0x0fu);
    if (id != NULL)
        *id = get_le16(buf + 0x06);
    if (total != NULL)
        *total = get_le16(buf + 0x04);
    return DP_OK;
}

/* Group-0 config event: the vendor shape used by hdpp_config_send_event (id 1) and
 * hmac_config_alg_send_event (id 2): total = 0x1c + len, payload = token[8] | u16
 * key/cmd | u16 len | data[len] | 4 inherited pad bytes. */
int dp_build_g0_cfg(struct dp_frame *f, uint8_t id, uint8_t fb, uint16_t key,
                    const uint8_t *data, uint16_t len)
{
    uint8_t *p;
    uint32_t total;

    if (f == NULL || (len != 0u && data == NULL))
        return DP_EARG;
    if (id != 1u && id != 2u)
        return DP_EID;
    if (len > DP_MAX_PAYLOAD - DP_CFG_PAYLOAD)
        return DP_EBOUNDS;
    total = DP_CFG_TOTAL(len);
    if (total > DP_MAX_FRAME)
        return DP_EBOUNDS;

    dp_frame_reset(f);
    p = f->buf;
    p[0x00] = 0x00u;            /* group 0 */
    p[0x01] = 0x01u;            /* alloc state 1, dst nibble 0 */
    p[0x02] = 0x00u;            /* field A (hdpp: descriptor[6], builder 0) */
    p[0x03] = fb;               /* field B: module/vap index the fw handler resolves */
    put_le16(p + 0x04, (uint16_t)total);
    put_le16(p + 0x06, id);
    p[0x08] = 0x00u;            /* field C (wal sequence counter) */
    p[0x09] = 0x00u;            /* dst = hdr[1] >> 4 = 0 */
    put_le16(p + 0x0a, DP_TAG);
    /* payload at +0x0c: token[8] inherited (zeros in a fresh buffer) */
    put_le16(p + 0x14, key);
    put_le16(p + 0x16, len);
    if (len != 0u)
        memcpy(p + 0x18, data, len);
    /* 4 pad bytes after the data stay zero for a fresh buffer; a capture replay can
     * still export with hd2 pad_byte = 0xff via dp_frame_export once the caller
     * copies the captured pad; the builders never invent 0xff. */
    f->len = total;
    return DP_OK;
}

/* Bench-validated context-create: g0 id1 key 0x64, data[0]=type {1,2,3},
 * data[1]=parent module, data[4]=module created (4 for the TXQ sync's fb).
 * data len 0x14 (0x13 is the firmware's highest read). */
int dp_build_ctx_create(struct dp_frame *f, uint8_t type, uint8_t parent,
                        uint8_t user, uint8_t fb)
{
    uint8_t data[0x14];

    if (type < 1u || type > 3u)
        return DP_EARG;
    if (user > 0x12u)
        return DP_EARG;
    memset(data, 0, sizeof(data));
    data[0] = type;
    data[1] = parent;
    data[4] = user;
    {
        int err = dp_build_g0_cfg(f, 1u, fb, DP_KEY_CTX_CREATE, data, sizeof(data));

        if (err != DP_OK)
            return err;
    }
    /* token word1 = 1 and the 4 inherited pad bytes = 0xff: exact bytes of the
     * validated create frame (wifi-ctx-create lane, bench lap 2026-11-25 05:10). */
    f->buf[0x10] = 0x01u;
    f->buf[f->len - 4u] = 0xffu;
    f->buf[f->len - 3u] = 0xffu;
    f->buf[f->len - 2u] = 0xffu;
    f->buf[f->len - 1u] = 0xffu;
    return DP_OK;
}

/* The vendor's first post-ready H2D in the live capture (phase20 A.5 / phase38):
 * group 0 id 1, fb 4, key 0x00d8, len 0x14, data = {01 00 00 00, 16 x 00}, pad ff*4.
 * The 0xd8 worker (fw CA 0x81039) stores data[0] to [created_ctx+0x190]+0x4c, so the
 * create must precede this frame. 48-byte frame; the capture node declared 72 B. */
int dp_build_txq_sync_capture(struct dp_frame *f)
{
    static const uint8_t data[0x14] = { 0x01, 0x00, 0x00, 0x00 };
    uint8_t *p;
    int err;

    err = dp_build_g0_cfg(f, 1u, 4u, DP_KEY_TXQ_SYNC, data, sizeof(data));
    if (err != DP_OK)
        return err;
    p = f->buf;
    p[0x10] = 0x01u;            /* token word1 = 1 (inherited, as captured) */
    p[0x2c] = 0xffu;            /* inherited pad, reproduced from the capture */
    p[0x2d] = 0xffu;
    p[0x2e] = 0xffu;
    p[0x2f] = 0xffu;
    f->len = 0x30u;
    return DP_OK;
}

/* TXQ-table address sync: decoded from the shipped 2.5.24 host module
 * (hmac_txq_table_addr_sync -> hdpp_config_send_event(dev, 0x320, 4, &devva)).
 * The payload is the device VA of the 0x2524-byte host-DRAM txq table. */
int dp_build_txq_addr_sync(struct dp_frame *f, uint32_t txq_devva, uint8_t fb)
{
    uint8_t data[4];

    put_le32(data, txq_devva);
    return dp_build_g0_cfg(f, 1u, fb, DP_KEY_TXQ_ADDR, data, sizeof(data));
}

int dp_frame_export(const struct dp_frame *f, uint8_t *out, size_t cap,
                    size_t pad_to, uint8_t pad_byte, size_t *out_len)
{
    size_t n;

    if (f == NULL || out == NULL)
        return DP_EARG;
    n = pad_to > f->len ? pad_to : f->len;
    if (n < f->len || n > cap)
        return DP_EBOUNDS;
    memcpy(out, f->buf, f->len);
    if (n > f->len)
        memset(out + f->len, pad_byte, n - f->len);
    if (out_len != NULL)
        *out_len = n;
    return DP_OK;
}

/* ---- TX data ring ---------------------------------------------------------------- */
/* 8-byte MSDU-info ring entry. ctl field map (from hmac_tx_msdu_ring_push and
 * hmac_tx_frame_control_dscr_set):
 *   [11:0]  frame length (vendor copies skb->len)
 *   [14:12] tx path class (3-bit; set from the skb path-class byte)
 *   [15]    tx-header flag (from 802.11 header info)
 *   [19:16] tid / priority nibble (from the tx frame header)
 *   [21:20] vap type (vendor writes 0x10/0x20/0x30 -> 1/2/3 in these two bits)
 * Higher bits were not observed to be written by the analyzed builder. */
int dp_tx_msdu_entry_encode(uint8_t entry[DP_TX_ENTRY_LEN], uint32_t buf_devva,
                            uint16_t frame_len, uint8_t path_class,
                            uint8_t hdr_flag, uint8_t tid, uint8_t vap_type)
{
    uint32_t ctl;

    if (entry == NULL || frame_len > DP_TXE_LEN_MASK)
        return DP_EARG;
    if (path_class > 7u || tid > 0x0fu || vap_type > 3u)
        return DP_EARG;
    ctl = (uint32_t)frame_len;
    ctl |= ((uint32_t)path_class << DP_TXE_PATH_SHIFT) & DP_TXE_PATH_MASK;
    if (hdr_flag)
        ctl |= DP_TXE_FLAG15;
    ctl |= ((uint32_t)tid << 16) & DP_TXE_TID_MASK;
    ctl |= ((uint32_t)vap_type << DP_TXE_VAP_SHIFT) & DP_TXE_VAP_MASK;
    put_le32(entry, buf_devva);
    put_le32(entry + 4, ctl);
    return DP_OK;
}

uint32_t dp_tx_msdu_entry_len(uint32_t ctl)
{
    return ctl & DP_TXE_LEN_MASK;
}

uint32_t dp_tx_msdu_entry_buf(const uint8_t entry[DP_TX_ENTRY_LEN])
{
    return get_le32(entry);
}

uint32_t dp_tx_idx_pack(uint32_t index, unsigned phase)
{
    return (index & DP_TXIDX_INDEX_MASK) | (phase ? DP_TXIDX_PHASE : 0u);
}

void dp_tx_idx_unpack(uint32_t packed, uint32_t *index, unsigned *phase)
{
    if (index != NULL)
        *index = packed & DP_TXIDX_INDEX_MASK;
    if (phase != NULL)
        *phase = (packed & DP_TXIDX_PHASE) ? 1u : 0u;
}

/* Vendor pcie_ete_ring_ptr_plus / hmac_tx_msdu_ring_push arithmetic: increment the
 * low index field; when it reaches the ring depth, clear it and toggle the phase bit. */
uint32_t dp_tx_idx_next(uint32_t packed, uint32_t depth)
{
    uint32_t index = packed & DP_TXIDX_INDEX_MASK;
    uint32_t phase = packed & DP_TXIDX_PHASE;

    if (depth == 0u)
        return packed;
    index++;
    if (index >= depth) {
        index = 0;
        phase ^= DP_TXIDX_PHASE;
    }
    return (index & DP_TXIDX_INDEX_MASK) | phase;
}

/* ---- H2D message catalogue ---------------------------------------------------------
 * group/id + vendor sender + firmware receive handler (CA, image fact) + fixed payload
 * size where the vendor builder allocs one. Source: wifi-h2d-catalog 5.1/Appendix A
 * (image handler tables) and phase37 sr-message-catalog (vendor builders). */
static const struct dp_msg_row dp_msgs[] = {
    { 0, 0x00, "safe stub (payload-free probe)",          0x00081695u,  0u,    DP_CTRL  },
    { 0, 0x01, "hdpp_config_send_event",                  0x00043D59u,  0u,    DP_CFG   },
    { 0, 0x02, "hmac_config_alg_send_event",              0x000D6069u,  0u,    DP_CFG   },
    { 0, 0x03, "hmac_sdt_recv_reg_cmd",                   0x000B5FE1u,  0u,    DP_CTRL  },
    { 0, 0x07, "hmac_user_add",                           0x0009832Du,  0xA8u, DP_MGMT  },
    { 0, 0x08, "hmac_user_add_notify_alg",                0x00096225u,  0xA8u, DP_MGMT  },
    { 0, 0x09, "hmac_del_user_notify_dmac",               0x000974F9u,  0x0Cu, DP_MGMT  },
    { 0, 0x0A, "hmac_mgmt_rx_delba",                      0x0005FF65u,  0x20u, DP_MGMT  },
    { 0, 0x0B, "hmac_mgmt_tx_ampdu_start",                0x0005E2F5u,  0u,    DP_MGMT  },
    { 0, 0x0C, "hmac_enc_scan_send_req",                  0x0006494Du,  0x1FCu, DP_MGMT },
    { 0, 0x0E, "hmac_mgmt_reset_psm",                     0x000A4A65u,  0u,    DP_CTRL  },
    { 0, 0x0F, "hmac_sta_sync_join_req_params",           0x00062EC1u,  0x40u, DP_MGMT  },
    { 0, 0x10, "hmac_sta_wait_join(_rx_beacon)",          0x0005E28Du,  0x10u, DP_MGMT  },
    { 0, 0x11, "hmac_send_connect_result_to_dmac_sta",    0x00062E1Du,  0u,    DP_MGMT  },
    { 0, 0x12, "hmac_sta_up_update_edca_params_machw",    0x0005DE7Du,  0x74u, DP_DATA  },
    { 0, 0x13, "hmac_sta_up_update_mu_edca_params_machw", 0x000968A5u,  0u,    DP_DATA  },
    { 0, 0x15, "hmac_config_receive_all_sta_rssi",        0x00063A2Du,  0u,    DP_DATA    },
    { 0, 0x19, "hmac_chan_restart_network_after_switch",  0x00061649u,  0u,    DP_CTRL  },
    { 0, 0x1D, "hmac_edca_opt_timeout_fn",                0x000B568Du,  0x3Cu, DP_DATA  },
    { 0, 0x20, "hmac_config_h2d_send_app_ie",             0x00085631u,  0u,    DP_MGMT  },
    { 0, 0x22, "hmac_mgmt_send_deauth_frame_notify",      0x0008F669u,  0x18u, DP_MGMT  },
    { 0, 0x23, "hmac_cfg_tx_sched_switch",                0x0008D4BDu,  0u,    DP_DATA  },
    { 0, 0x24, "hmac_scan_config_bss_color",              0x000B55E1u,  0u,    DP_MGMT  },
    { 0, 0x2B, "hmac_vip_frame_event_post",               0x000A1521u,  0u,    DP_DATA  },
    { 0, 0x2C, "hmac_multiap_handle_steering_req_msg",    0x0004AAE5u,  0x1Cu, DP_MGMT  },
    { 0, 0x31, "hmac_multiap_parse_chan_sel_req_msg",     0x0004A9E1u,  0x1Du, DP_MGMT  },
    { 2, 0x01, "wal_acs_netlink_recv",                    0x00000000u,  0u,    DP_CFG   },
    { 2, 0x02, "hmac_handle_scan_rsp_sta",                0x000B9781u,  0u,    DP_MGMT  },
    { 2, 0x03, "hmac_handle_asoc_rsp_sta",                0x000BBC9Du,  0x1Cu, DP_MGMT  },
    { 2, 0x04, "hmac_sta_handle_disassoc_rsp",            0x0008F52Du,  0u,    DP_MGMT  },
    { 2, 0x05, "hmac_handle_connect_rsp_ap",              0x000B9781u,  0x10u, DP_MGMT  },
    { 2, 0x06, "hmac_handle_disconnect_rsp_ap",           0x00092889u,  0u,    DP_MGMT  },
    { 2, 0x08, "hmac_acs_init_scan_hook",                 0x000627FDu,  0u,    DP_MGMT  },
    { 2, 0x09, "hmac_send_mgmt_to_host",                  0x0008CF85u,  0x1Cu, DP_MGMT  },
    { 2, 0x0D, "hmac_mgmt_tx_event_status",               0x000956A9u,  0u,    DP_MGMT  },
    { 2, 0x15, "hmac_config_receive_all_sta_rssi",        0x00060B4Du,  0x108u, DP_DATA },
    { 2, 0x16, "hmac_spectral_scan_complete",             0x0008E2ADu,  0x08u, DP_DATA  },
    { 2, 0x17, "hmac_csi_complete",                       0x000608BDu,  0x2Cu, DP_DATA  },
    { 2, 0x1C, "hmac_tx_ba_notify",                       0x000845E1u,  0x14u, DP_DATA  },
    { 2, 0x1E, "hmac_report_external_auth_req",           0x0004C083u,  0x38u, DP_MGMT  },
    { 2, 0x1F, "hmac_virtual_fill_tx_packet",             0x0004B1B1u,  0x10u, DP_DATA  },
    { 2, 0x21, "hmac_tx_complete_read_error_process",     0x000811E3u,  0x18u, DP_DATA  },
    { 2, 0x22, "hmac_mgmt_send_deauth_frame_notify",      0x0008785Du,  0x18u, DP_MGMT  },
    { 2, 0x24, "hmac_rx_data_send_disasoc_frame_notify",  0x000C5ED1u,  0x10u, DP_MGMT  },
    { 2, 0x25, "hmac_multiap_event_report",               0x0008F669u,  0u,    DP_MGMT  },
    { 2, 0x26, "hmac_hsan_event_report",                  0x0008785Du,  0u,    DP_DATA  },
};

const struct dp_msg_row *dp_msg_catalogue(size_t *count)
{
    if (count != NULL)
        *count = sizeof(dp_msgs) / sizeof(dp_msgs[0]);
    return dp_msgs;
}

const struct dp_msg_row *dp_msg_lookup(uint8_t group, uint16_t id)
{
    size_t i;

    for (i = 0; i < sizeof(dp_msgs) / sizeof(dp_msgs[0]); i++) {
        if (dp_msgs[i].group == group && dp_msgs[i].id == id)
            return &dp_msgs[i];
    }
    return NULL;
}