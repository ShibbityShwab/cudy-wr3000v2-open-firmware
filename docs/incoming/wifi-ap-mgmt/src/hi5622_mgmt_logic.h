/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_mgmt_logic.h - portable management-frame TX/RX wire logic for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-ap-mgmt, shared task task-143).
 * No vendor source/object code, firmware byte, calibration value or secret is
 * reproduced.  Every constant is a derived fact with the evidence marker used
 * in REPORT.md/TABLES.md:
 *
 *   [V]  decoded from a local vendor artifact (vendor host module 2.4.15 /
 *        2.5.24 or the PD4 firmware image; the artifacts stay local/gitignored)
 *   [V*] measured by an attended bench lane, quoted from the project reports
 *   [I]  inference; [U] unknown / not pinned
 *
 * THIS FILE IS DEVICE-FREE.  It builds/validates bytes only; the kernel half
 * (hi5622_mgmt.c) copies frames into DMA memory and posts them through the
 * fail-closed hi5622_frame_post() gate.  The host selftest
 * (tools/mgmt_selftest.c) compiles this file with a C99 compiler.
 *
 * WHAT IT DECODES
 * ---------------
 * 1. WAL cfg-event mgmt-tx carrier (host -> firmware), the vendor
 *    wal_cfg80211_mgmt_tx path [V]: HCC group 2, header id 0, WAL cfg-event
 *    id 1, one TLV {u16 cmd 0x01F2; u16 len 12; 12-byte tx descriptor}.
 *    The descriptor is {u32 channel; u8 cookie_idx; u8 pad; u16 len;
 *    u32 frame_addr} where frame_addr is a DEVICE-VISIBLE host-DRAM address
 *    (the vendor passes the cfg80211 frame pointer because its 2 GiB/2 GiB
 *    VMSPLIT makes that pointer fall in the 0x80000000..0x8FFFFFFF host
 *    window; this kernel is VMSPLIT_3G, so the driver must pass the DMA
 *    address of its own coherent copy - exactly the project rule already
 *    documented in hi5622_driver.h for the ETE/HCC buffers).
 * 2. D2H management-frame RX event (firmware -> host), vendor HCC group 1
 *    id 13 handler hmac_rx_process_mgmt_event [V]: the frame is staged after
 *    a 60-byte prefix (12-byte HCC header + 48-byte RX info).  Decoded RX
 *    info fields: +7 channel, +17 u16 frame length, +39 s8 RSSI.
 */
#ifndef _HI5622_MGMT_LOGIC_H
#define _HI5622_MGMT_LOGIC_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8  himg_u8;
typedef u16 himg_u16;
typedef u32 himg_u32;
typedef s8  himg_s8;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  himg_u8;
typedef uint16_t himg_u16;
typedef uint32_t himg_u32;
typedef int8_t   himg_s8;
#endif

/* ===================================================================
 * 1. HCC / WAL envelope constants [V]
 * =================================================================== */
#define HIMG_HCC_HDR_LEN	12u
#define HIMG_HCC_TAG		0x5a5au
#define HIMG_HCC_GROUP_WAL	2u	/* group-2 WAL/chip config events */
#define HIMG_HCC_ID_WAL		0u	/* header id of the WAL cfg family */
#define HIMG_ALLOC_NIBBLE	0x01u	/* hcc_msg_alloc host nibble [V] */
/* WAL cfg-event envelope after the 12-byte HCC header [V]:
 * cookie[4], wal{id[4], pad[2], data_len[2], seq[4]}, {cmd[2], len[2]}, body */
#define HIMG_WAL_OVERHEAD	32u
#define HIMG_WAL_MSG_CFG	1u	/* wal_cfg_event type/op byte 1 [V] */
/* Vendor WAL cfg command for cfg80211 .mgmt_tx [V: 2.4.15 and 2.5.24 emit
 * the identical TLV; the firmware g0 static config-key table A has no row
 * for it, so this carrier is the vendor WAL path, not the g0 path]. */
#define HIMG_WAL_CMD_MGMT_TX	0x01F2u
#define HIMG_WAL_CMD_MGMT_TX_BODY 12u	/* 12-byte descriptor [V] */
#define HIMG_MAX_FRAME_TX	588u	/* vendor memcpy_s bound 0x24c [V] */
#define HIMG_MIN_MGMT_FRAME	24u	/* 802.11 header only */

/* ===================================================================
 * 2. D2H management-frame RX (group 1 id 13) [V]
 * =================================================================== */
#define HIMG_RX_GROUP		1u
#define HIMG_RX_ID		13u	/* hmac_rx_process_mgmt_event [V] */
#define HIMG_RX_INFO_LEN	48u	/* rx info copied to hcc_msg->cb [V] */
#define HIMG_RX_FRAME_OFF	(HIMG_HCC_HDR_LEN + HIMG_RX_INFO_LEN) /* 60 */
#define HIMG_RX_INFO_CHANNEL	7u	/* msg->cb+7 -> channel number [V] */
#define HIMG_RX_INFO_FRAME_LEN	17u	/* u16, == wire total - 60 [V] */
#define HIMG_RX_INFO_RSSI	39u	/* s8, 100+raw clamped by vendor [V] */

/* ===================================================================
 * 3. Neutral parameter/result structs
 * =================================================================== */
struct himg_tx_desc {
	himg_u32	channel;	/* channel number the AP is on */
	himg_u8		cookie_idx;	/* vendor cookie-array slot [I] */
	himg_u8		pad;
	himg_u16	len;		/* 802.11 frame length */
	himg_u32	frame_addr;	/* device-visible host-DRAM address */
};

struct himg_tx_params {
	himg_u32	channel;
	himg_u8		cookie_idx;
	himg_u16	len;
	himg_u32	frame_addr;
	himg_u32	seq;		/* WAL cfg-event sequence number */
	himg_u8		dst;		/* HCC dst nibble (0 for the AP VAP) */
};

struct himg_rx_frame {
	const himg_u8	*frame;		/* points into the caller's payload */
	himg_u32	frame_len;
	himg_u16	freq;		/* MHz */
	himg_s8		rssi;		/* vendor raw s8 field */
	himg_u8		channel;
	himg_u8		subtype;	/* 802.11 management subtype */
};

/* ===================================================================
 * 4. API (implemented in hi5622_mgmt_logic.c)
 * =================================================================== */
/* Channel number -> centre frequency (MHz), vendor hmac_rx_mgmt_send_to_host
 * formula [V].  0 = valid, -EINVAL on a channel this band plan cannot map. */
int himg_chan_to_freq(himg_u8 channel, himg_u16 *freq);

/* 12-byte mgmt-tx descriptor; 0 = valid. */
int himg_build_tx_desc(himg_u8 *desc, size_t cap,
		       const struct himg_tx_desc *d);

/* Full vendor HCC wire frame for the WAL mgmt-tx carrier; 0 = valid. */
int himg_build_wal_mgmt_tx(himg_u8 *wire, size_t cap, size_t *out_len,
			   const struct himg_tx_params *p);

/* 802.11 management-frame sanity (protocol 0, type management, sane length).
 * 0 = acceptable to hand to the TX carrier. */
int himg_mgmt_frame_ok(const himg_u8 *frame, size_t len);

/* Decode one D2H group-1 id-13 mgmt event.  hdr is the 12-byte HCC header,
 * payload the bytes after it, avail how many of them are readable (the direct
 * reader clamps to its snapshot), wire_len the node total from the header.
 * 0 = decoded; -ENODATA = not this event; -EINVAL/-EPROTO = malformed
 * (always fail-closed, never a partial frame). */
int himg_parse_rx_mgmt(const himg_u8 *hdr, const himg_u8 *payload,
		       himg_u32 avail, himg_u32 wire_len,
		       struct himg_rx_frame *out);

#endif /* _HI5622_MGMT_LOGIC_H */
