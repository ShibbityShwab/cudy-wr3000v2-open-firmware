/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_data_tx_logic.h - portable, kernel-free data-TX logic for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-data-tx, shared task task-150,
 * 2026-11-29).  No vendor source, object, firmware or calibration byte is
 * reproduced; only wire facts that earlier lanes decoded from the local
 * (gitignored) vendor artifacts appear here, each with an evidence marker:
 *
 *   [V]   decoded from a named vendor artifact (host .ko / PD4 firmware)
 *   [V*]  device-measured/previously bench-proven by an earlier project lane
 *   [I]   inference - must be confirmed on the bench
 *   [U]   unknown - the code fails closed instead of guessing
 *
 * WHAT THIS LAYER DOES
 *   Hostapd runs the AP SME in host software.  Its EAPOL-Key msg1/msg3 leave
 *   through the AP netdev's ndo_start_xmit as plain 802.3 frames (hostapd's
 *   l2_packet socket).  This layer turns such a frame into the 802.11
 *   data MPDU the proven dp TX path expects, decides whether the peer may be
 *   transmitted to at all (peer-aware / fail-closed), picks the 802.11
 *   Protected bit, and classifies the EAPOL-Key shape (including the
 *   vendor's tx-completion "4/4-like" detector, hmac_is_eapol_key_ptk_4_4).
 *
 *   802.11 -> MPDU rules (AP transmit, FromDS=1 / ToDS=0) [V] (802.11 spec,
 *   mirror of the decoded vendor RX rules in wifi-sta-ops TABLES T8):
 *     FC = 0x0208 (data, FromDS) or 0x0288 (QoS data, FromDS); the Protected
 *     bit is FC bit 14 (0x4000).
 *     addr1 = DA (the client), addr2 = BSSID/TA (the AP netdev address),
 *     addr3 = SA (the Ethernet source address; for hostapd's own frames it
 *     equals the BSSID).
 *     24-byte header (26 with a QoS control field), then LLC/SNAP
 *     AA AA 03 00 00 00 + the Ethernet type big-endian + payload.
 *     No FCS - the MAC appends it.
 *
 *   Frame buffer layout handed to the MSDU ring [V] (wifi-dp-txrx
 *   FRAME-SPECS.md, hmac_tx_msdu_dscr_add 0x19b40): the ring entry points at
 *   [16 zero device-visible header bytes][MPDU]; this lane keeps the vendor
 *   host bookkeeping word in front: [owner u32][16 B zero][MPDU], so the
 *   device VA of the MPDU area is buffer + 20 (DP_TXRX_FRAME_MPDU_OFF) and
 *   the ring entry's buf_devva is buffer + 4 (DP_TXRX_FRAME_HOST_HDR).
 *
 *   MSDU ring entry ctl [V] (hmac_tx_msdu_ring_push 0x18f18 /
 *   hmac_tx_frame_control_dscr_set 0x18b9c): length[11:0], tx path class
 *   [14:12], header flag[15], tid[19:16], vap type[21:20].  A data frame is
 *   vap type 2 [I] (the same mapping the hardware-fired ARP frame used).
 *
 * FAIL-CLOSED POLICY (the kernel glue enforces the order):
 *   - the master module parameter data_tx defaults to 0: the netdev xmit
 *     keeps the old accounted drop, nothing is built or posted;
 *   - unicast frames are only posted to a peer that exists in the host
 *     station table (sta-ops, task-149) unless data_tx_peer=0;
 *   - a non-EAPOL frame to a peer that is not authorized is refused
 *     (pre-4-way data cannot leak);
 *   - the master lane is one-shot per module load after the first post
 *     failure/timeout (the dp lane's "no retry" rule).
 */
#ifndef HI5622_DATA_TX_LOGIC_H
#define HI5622_DATA_TX_LOGIC_H

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifndef HI5622_HDTX_HOST_TYPES
#define HI5622_HDTX_HOST_TYPES
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif
#endif

/* ------------------------------------------------------------------ */
/* 1. Frame buffer / MPDU constants (must match the proven dp path)    */
/* ------------------------------------------------------------------ */
#define HDTX_FRAME_HOST_HDR	4u	/* [V] vendor host word */
#define HDTX_FRAME_DMA_HDR	16u	/* [V] zero device-visible header */
#define HDTX_FRAME_MPDU_OFF	(HDTX_FRAME_HOST_HDR + HDTX_FRAME_DMA_HDR)
#define HDTX_FRAME_AREA		0x400u	/* one slab per in-flight frame */
#define HDTX_MPDU_MAX		0x300u	/* DP_TXRX_MAX_MPDU (proven ctl bound) */

#define HDTX_ETH_HDR_LEN	14u
#define HDTX_ETH_P_PAE		0x888eu
#define HDTX_ETH_P_VLAN		0x8100u
#define HDTX_LLC_HDR_LEN	6u	/* AA AA 03 00 00 00            */
#define HDTX_LLC_SNAP_LEN	8u	/* LLC header + the 2-byte type */
#define HDTX_80211_HDR_LEN	24u
#define HDTX_80211_QOS_HDR_LEN	26u
#define HDTX_80211_SEQ_SHIFT	4u

/* Frame control, little-endian u16 as stored on the wire.
 * bits: 0-1 version, 2-3 type (2=data), 4-7 subtype, 8 ToDS, 9 FromDS,
 * 14 Protected, 15 Order. */
#define HDTX_FC_DATA		0x0008u	/* type data, subtype 0 */
#define HDTX_FC_QOS_DATA	0x0088u	/* type data, subtype 8 */
#define HDTX_FC_FROMDS		0x0200u	/* FromDS bit 9 */
#define HDTX_FC_PROTECTED	0x4000u	/* bit 14 */
#define HDTX_FC_DATA_FROMDS	(HDTX_FC_DATA | HDTX_FC_FROMDS)
#define HDTX_FC_QOS_DATA_FROMDS	(HDTX_FC_QOS_DATA | HDTX_FC_FROMDS)

/* EAPOL / EAPOL-Key wire offsets (inside the EAPOL frame; identical to the
 * rules wifi-sta-ops TABLES T9 re-derived from mac_is_eapol_key_ptk 0x23094
 * and hmac_is_eapol_key_ptk_4_4 0x18750) [V].  Offsets are relative to the
 * first EAPOL byte (the 802.3 type field position + 2 in an Ethernet frame). */
#define HDTX_EAPOL_HDR_LEN	4u
#define HDTX_EAPOL_TYPE_KEY	3u
#define HDTX_EAPOL_KEY_MINLEN	95u	/* EAPOL header length field, > 0x5e */
#define HDTX_EAPOL_KEY_INFO_OFF	5u	/* u16 big-endian key info */
#define HDTX_EAPOL_KEYDLEN_OFF	97u	/* u16 big-endian key data length */
#define HDTX_KEY_INFO_PAIRWISE	0x0008u	/* bit 3 key type = pairwise */
#define HDTX_KEY_INFO_INSTALL	0x0040u
#define HDTX_KEY_INFO_ACK	0x0080u
#define HDTX_KEY_INFO_MIC	0x0100u
#define HDTX_KEY_INFO_SECURE	0x0200u
#define HDTX_KEY_INFO_ENCR	0x1000u

/* vendor 4/4-like detector message classes (AP transmit perspective) */
#define HDTX_EAPOL_MSG_NONE	0u
#define HDTX_EAPOL_MSG_1	1u	/* ACK, no MIC: the AP's msg1/4 */
#define HDTX_EAPOL_MSG_3	3u	/* ACK + MIC: the AP's msg3/4 */
#define HDTX_EAPOL_MSG_OTHER	5u	/* some other EAPOL-Key frame */

/* result codes (kernel glue maps them to errno) */
#define HDTX_OK		0
#define HDTX_ESHORT	1
#define HDTX_EARG	2
#define HDTX_EBOUNDS	3
#define HDTX_EFORMAT	4
#define HDTX_EPEER	5	/* unicast to a peer not in the station table */
#define HDTX_EUNAUTH	6	/* non-EAPOL data to an unauthorized peer  */
#define HDTX_EGATE	7	/* dp TX gate closed / post failed          */
#define HDTX_ETIMEOUT	8	/* bounded completion timeout               */

/* protect mode module parameter values */
#define HDTX_PROTECT_AUTO	0u	/* protect data to authorized peers     */
#define HDTX_PROTECT_ALWAYS	1u	/* force the Protected bit (bounds test) */
#define HDTX_PROTECT_NEVER	2u	/* never set it (4-way msg1/msg3 form)  */

/* ------------------------------------------------------------------ */
/* 2. 802.3 frame -> 802.11 data MPDU                                  */
/* ------------------------------------------------------------------ */
struct hdtx_eth {
	int	ok;
	const u8 *da;
	const u8 *sa;
	u16	ethertype;	/* host order */
	const u8 *payload;	/* frame + 12: big-endian type + data */
	u32	payload_len;	/* frame_len - 12 */
	u32	frame_len;
};

/* Parse an Ethernet frame (14-byte header).  Refuses short frames, a
 * non-linear/odd VLAN tag payload and the max-size overflow. */
int hdtx_eth_parse(const u8 *frame, u32 len, struct hdtx_eth *out);

struct hdtx_mpdu_params {
	const u8 *bssid;	/* AP address (addr2)                          */
	const u8 *da;		/* destination (addr1)                         */
	const u8 *sa;		/* source (addr3; FromDS=1)                    */
	const u8 *payload;	/* LLC payload: ethertype BE + data            */
	u32	payload_len;
	u16	seq;		/* 12-bit sequence number                      */
	u8	tid;		/* 0..15, used when qos != 0                   */
	u8	qos;		/* 0 = 24-byte header, 1 = 26 + QoS control    */
	u8	protected_frame; /* 1 = set the 802.11 Protected bit (FC bit 14) */
};

/* Build the complete MPDU (no FCS).  Bounds: MPDU <= HDTX_MPDU_MAX. */
int hdtx_build_data_mpdu(u8 *out, size_t cap, size_t *out_len,
			 const struct hdtx_mpdu_params *p);

/* FC word (little-endian, as stored) for the data frame shape. */
u16 hdtx_mpdu_fc(u8 qos, u8 protected_frame);

/* Frame buffer layout: [owner u32][16 zero][MPDU]; *devva_off receives
 * HDTX_FRAME_HOST_HDR (the offset the MSDU ring entry points at). */
int hdtx_frame_write(u8 *buf, size_t cap, u32 owner, const u8 *mpdu,
		     u32 mpdu_len, u32 *devva_off);

/* Device VA for a coherent DMA address; enforces the driver's identity
 * window (dp_rings DP_HOST_DMA_LO..HI) and adds the host-word offset. */
int hdtx_buffer_devva(u64 dma, u32 lo, u32 hi, u32 *devva);

/* ------------------------------------------------------------------ */
/* 3. EAPOL / EAPOL-Key classification (TX side)                       */
/* ------------------------------------------------------------------ */
struct hdtx_eapol {
	int	is_eapol;
	int	is_key;
	int	pairwise;
	int	ptk_done_like;	/* vendor hmac_is_eapol_key_ptk_4_4 shape  */
	u16	key_info;
	u16	key_datalen;
	u16	eapol_len;
	u8	eapol_type;
	u8	msg;		/* HDTX_EAPOL_MSG_*                       */
};

/* Classify the EAPOL payload of a parsed Ethernet frame.  Never reads past
 * frame_len; every field is bounds-checked.  Only derived metadata is
 * returned (no key material is ever examined or logged). */
int hdtx_classify_eapol(const struct hdtx_eth *eth, struct hdtx_eapol *out);

/* ------------------------------------------------------------------ */
/* 4. Peer-aware TX decision + Protected bit                           */
/* ------------------------------------------------------------------ */
struct hdtx_tx_decision {
	int	allowed;
	int	reason;		/* HDTX_OK or the refusal code            */
	int	protect;	/* 802.11 Protected bit for this frame     */
};

/* peer_required = data_tx_peer; group_keys = "a group key is installed"
 * (hostapd's WPA2 GTK install happened); protect_mode = data_tx_protect.
 * Rules (fail-closed):
 *   - multicast/broadcast: allowed (no peer context needed);
 *   - unicast: a missing station-table entry is refused when peer_required,
 *     otherwise allowed (explicit bench diagnostic mode);
 *   - unicast to an existing but unauthorized peer: only EAPOL is allowed;
 *   - protect: AUTO = authorized unicast peers / group keys for multicast,
 *     ALWAYS = 1, NEVER = 0. */
int hdtx_decide_tx(unsigned int peer_required, int is_multicast,
		   int peer_found, int peer_authorized, int is_eapol,
		   unsigned int protect_mode, int group_keys,
		   struct hdtx_tx_decision *out);

int hdtx_is_multicast(const u8 addr[6]);

/* ------------------------------------------------------------------ */
/* 5. Completion bookkeeping                                           */
/* ------------------------------------------------------------------ */
/* Packed ring index = index[14:0] | phase<<15 (dp_txrx_idx_*).
 * hdtx_ring_advance() returns the number of device-consumed entries
 * between two packed consumer values (0..depth); it refuses an impossible
 * backwards/wrapped-too-far pair with HDTX_EFORMAT (-style return). */
int hdtx_ring_advance(u32 prev_rptr, u32 now_rptr, u32 depth, u32 *advance);

/* Pool depth / slot bookkeeping is kernel-side; the host test covers the
 * ordering rule: a completion frees the oldest in-flight frame first. */
#define HDTX_POOL_DEPTH		16u	/* in-flight TX frames per wiphy */
#define HDTX_POOL_BYTES		(HDTX_POOL_DEPTH * HDTX_FRAME_AREA)
#define HDTX_SEQ_MASK		0x0fffu

#endif /* HI5622_DATA_TX_LOGIC_H */
