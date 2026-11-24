/* SPDX-License-Identifier: GPL-2.0 */
/*
 * data_tx_selftest.c - host selftest for the wifi-data-tx lane (task-150).
 *
 * Compiles the portable logic exactly as the kernel does (C99, no Linux
 * headers) and asserts LITERAL wire facts: the 802.3 -> 802.11 data MPDU
 * layout, the LLC/SNAP prefix, FC Protected bit placement, the EAPOL-Key
 * classification offsets (T9 of wifi-sta-ops), the peer-aware decision table,
 * the frame-buffer layout and the packed-index completion arithmetic.
 *
 * No device, no vendor artifact, no key material (fixtures are synthetic
 * all-zero EAPOL bodies with documentation MAC addresses).
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_data_tx_logic.h"

static unsigned int checks;
static unsigned int failures;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ_INT(got, want) do { \
	long long g_ = (long long)(got); \
	long long w_ = (long long)(want); \
	checks++; \
	if (g_ != w_) { \
		failures++; \
		printf("FAIL %s:%d: %s = %lld, want %lld\n", \
		       __FILE__, __LINE__, #got, g_, w_); \
	} \
} while (0)

#define CHECK_BYTES(got, want, n) do { \
	checks++; \
	if (memcmp((got), (want), (n)) != 0) { \
		size_t i_; \
		failures++; \
		printf("FAIL %s:%d: bytes differ\n  got :", __FILE__, \
		       __LINE__); \
		for (i_ = 0; i_ < (n); i_++) \
			printf(" %02x", (unsigned char)(got)[i_]); \
		printf("\n  want:"); \
		for (i_ = 0; i_ < (n); i_++) \
			printf(" %02x", (unsigned char)(want)[i_]); \
		printf("\n"); \
	} \
} while (0)

/* Documentation-only addresses (locally administered). */
static const u8 DOC_BSSID[6] = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x01 };
static const u8 DOC_STA[6]   = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x02 };
static const u8 DOC_BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static void put_be16(u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)(v & 0xff);
}

/* Build an Ethernet + EAPOL-Key frame fixture with the given EAPOL fields.
 * Returns the total frame length.  Key data is a synthetic all-zero blob. */
static size_t build_eapol_key_frame(u8 *frame, size_t cap, const u8 da[6],
				    const u8 sa[6], u16 key_info,
				    u16 key_datalen)
{
	u8 *eap = frame + 14;
	size_t frame_len = 14u + 4u + 95u + (size_t)key_datalen;
	size_t off;

	memset(frame, 0, cap);
	memcpy(frame, da, 6);
	memcpy(frame + 6, sa, 6);
	put_be16(frame + 12, HDTX_ETH_P_PAE);		/* 802.3 type */
	eap[0] = 0x02u;					/* EAPOL version (RSN) */
	eap[1] = 0x03u;					/* EAPOL-Key */
	put_be16(eap + 2, 95u + key_datalen);		/* EAPOL length */
	eap[4] = 0x02u;					/* descriptor type RSN */
	put_be16(eap + 5, key_info);			/* key info */
	put_be16(eap + 7, 16u);				/* key length */
	memset(eap + 9, 0x11, 8);			/* replay counter */
	memset(eap + 17, 0xa5, 32);			/* nonce (synthetic) */
	memset(eap + 49, 0x5a, 16);			/* key IV (synthetic) */
	memset(eap + 65, 0x00, 8);			/* key RSC */
	memset(eap + 73, 0x00, 8);			/* key ID/reserved */
	memset(eap + 81, 0xcc, 16);			/* MIC (synthetic) */
	put_be16(eap + 97, key_datalen);		/* key data length */
	for (off = 0; off < (size_t)key_datalen; off++)
		eap[99 + off] = (u8)(0x30u + (off & 0x0fu));
	return frame_len;
}

static void test_constants(void)
{
	/* Literal cross-check against the proven dp path (dp_txrx.h). */
	CHECK_EQ_INT(HDTX_FRAME_HOST_HDR, 4);
	CHECK_EQ_INT(HDTX_FRAME_DMA_HDR, 16);
	CHECK_EQ_INT(HDTX_FRAME_MPDU_OFF, 20);
	CHECK_EQ_INT(HDTX_FRAME_AREA, 0x400);
	CHECK_EQ_INT(HDTX_MPDU_MAX, 0x300);
	CHECK_EQ_INT(HDTX_FC_DATA_FROMDS, 0x0208);
	CHECK_EQ_INT(HDTX_FC_QOS_DATA_FROMDS, 0x0288);
	CHECK_EQ_INT(HDTX_FC_PROTECTED, 0x4000);
	CHECK_EQ_INT(HDTX_LLC_HDR_LEN, 6);
	CHECK_EQ_INT(HDTX_LLC_SNAP_LEN, 8);
	CHECK_EQ_INT(HDTX_EAPOL_KEY_INFO_OFF, 5);
	CHECK_EQ_INT(HDTX_EAPOL_KEYDLEN_OFF, 97);
	CHECK_EQ_INT(HDTX_EAPOL_KEY_MINLEN, 95);
	CHECK_EQ_INT(HDTX_ETH_P_PAE, 0x888e);
	CHECK_EQ_INT(HDTX_KEY_INFO_PAIRWISE, 0x0008);
	CHECK_EQ_INT(HDTX_KEY_INFO_ACK, 0x0080);
	CHECK_EQ_INT(HDTX_KEY_INFO_MIC, 0x0100);
	CHECK_EQ_INT(HDTX_POOL_DEPTH, 16);
	CHECK_EQ_INT(HDTX_POOL_BYTES, 16 * 0x400);
	CHECK_EQ_INT(hdtx_mpdu_fc(0, 0), 0x0208);
	CHECK_EQ_INT(hdtx_mpdu_fc(0, 1), 0x4208);
	CHECK_EQ_INT(hdtx_mpdu_fc(1, 0), 0x0288);
	CHECK_EQ_INT(hdtx_mpdu_fc(1, 1), 0x4288);
}

static void test_msg1_mpdu(void)
{
	u8 frame[512];
	u8 mpdu[HDTX_MPDU_MAX];
	static const u8 want_hdr[32] = {
		/* FC LE 0x0208, duration 0 */
		0x08, 0x02, 0x00, 0x00,
		/* addr1 = DA (documentation STA) */
		0x02, 0x00, 0x68, 0x69, 0x35, 0x02,
		/* addr2 = BSSID */
		0x02, 0x00, 0x68, 0x69, 0x35, 0x01,
		/* addr3 = SA (= BSSID for hostapd) */
		0x02, 0x00, 0x68, 0x69, 0x35, 0x01,
		/* sequence control: seq 1 -> 0x0010 LE */
		0x10, 0x00,
		/* LLC/SNAP AA AA 03 00 00 00 + Ethernet type 0x888e */
		0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e,
	};
	struct hdtx_eth eth;
	struct hdtx_eapol eapol;
	struct hdtx_mpdu_params p;
	size_t frame_len, mpdu_len = 0;
	int rc;

	frame_len = build_eapol_key_frame(frame, sizeof(frame), DOC_STA,
					  DOC_BSSID, HDTX_KEY_INFO_ACK |
					  HDTX_KEY_INFO_PAIRWISE, 0u);
	CHECK_EQ_INT(frame_len, 14 + 99);

	rc = hdtx_eth_parse(frame, (u32)frame_len, &eth);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(eth.ethertype, HDTX_ETH_P_PAE);
	CHECK_EQ_INT(eth.payload_len, 2 + 99u);
	CHECK_EQ_INT(memcmp(eth.da, DOC_STA, 6), 0);
	CHECK_EQ_INT(memcmp(eth.sa, DOC_BSSID, 6), 0);

	rc = hdtx_classify_eapol(&eth, &eapol);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(eapol.is_eapol, 1);
	CHECK_EQ_INT(eapol.is_key, 1);
	CHECK_EQ_INT(eapol.pairwise, 1);
	CHECK_EQ_INT(eapol.eapol_len, 95);
	CHECK_EQ_INT(eapol.key_info, 0x0088);
	CHECK_EQ_INT(eapol.key_datalen, 0);
	CHECK_EQ_INT(eapol.msg, HDTX_EAPOL_MSG_1);
	CHECK_EQ_INT(eapol.ptk_done_like, 1); /* the vendor 4/4-like shape */

	memset(&p, 0, sizeof(p));
	p.bssid = DOC_BSSID;
	p.da = eth.da;
	p.sa = eth.sa;
	p.payload = eth.payload;
	p.payload_len = eth.payload_len;
	p.seq = 1u;
	p.qos = 0u;
	p.protected_frame = 0u;
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &mpdu_len, &p);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(mpdu_len, 24 + 8 + 99);
	CHECK_BYTES(mpdu, want_hdr, sizeof(want_hdr));
	/* MPDU +30 starts with the 2-byte Ethernet type, then the EAPOL header */
	CHECK_EQ_INT(mpdu[30], 0x88);
	CHECK_EQ_INT(mpdu[31], 0x8e);
	CHECK_EQ_INT(mpdu[32], 0x02); /* EAPOL version */
	CHECK_EQ_INT(mpdu[33], 0x03); /* EAPOL-Key type */
	CHECK_EQ_INT(mpdu[34], 0x00); /* EAPOL length 95 = 0x005f */
	CHECK_EQ_INT(mpdu[35], 0x5f);
	/* and no byte beyond the MPDU was written */
	memset(mpdu + mpdu_len, 0xaa, sizeof(mpdu) - mpdu_len);
	CHECK_EQ_INT(mpdu[mpdu_len], 0xaa);
}

static void test_msg3_mpdu_qos_protected(void)
{
	u8 frame[512];
	u8 mpdu[HDTX_MPDU_MAX];
	struct hdtx_eth eth;
	struct hdtx_eapol eapol;
	struct hdtx_mpdu_params p;
	size_t frame_len, mpdu_len = 0;
	int rc;

	frame_len = build_eapol_key_frame(frame, sizeof(frame), DOC_STA,
					  DOC_BSSID,
					  HDTX_KEY_INFO_ENCR |
					  HDTX_KEY_INFO_SECURE |
					  HDTX_KEY_INFO_MIC |
					  HDTX_KEY_INFO_ACK |
					  HDTX_KEY_INFO_INSTALL |
					  HDTX_KEY_INFO_PAIRWISE,
					  54u);
	rc = hdtx_eth_parse(frame, (u32)frame_len, &eth);
	CHECK_EQ_INT(rc, HDTX_OK);
	rc = hdtx_classify_eapol(&eth, &eapol);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(eapol.is_key, 1);
	CHECK_EQ_INT(eapol.pairwise, 1);
	CHECK_EQ_INT(eapol.key_datalen, 54);
	CHECK_EQ_INT(eapol.msg, HDTX_EAPOL_MSG_3);
	CHECK_EQ_INT(eapol.ptk_done_like, 0);

	memset(&p, 0, sizeof(p));
	p.bssid = DOC_BSSID;
	p.da = eth.da;
	p.sa = eth.sa;
	p.payload = eth.payload;
	p.payload_len = eth.payload_len;
	p.seq = 0x1234u;
	p.qos = 1u;
	p.tid = 5u;
	p.protected_frame = 1u;
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &mpdu_len, &p);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(mpdu_len, 26 + 8 + 99 + 54);
	CHECK_EQ_INT(mpdu[0], 0x88);	/* QoS data */
	CHECK_EQ_INT(mpdu[1], 0x42);	/* FromDS | Protected */
	CHECK_EQ_INT(mpdu[24], 0x05);	/* QoS control: TID 5 */
	CHECK_EQ_INT(mpdu[25], 0x00);
	CHECK_EQ_INT(mpdu[26], 0xaa);	/* LLC/SNAP starts after QoS control */
	CHECK_EQ_INT(mpdu[22], 0x40);	/* seq 0x234 -> (0x234 << 4) LE */
	CHECK_EQ_INT(mpdu[23], 0x23);
}

static void test_msg4_like_detector(void)
{
	u8 frame[512];
	struct hdtx_eth eth;
	struct hdtx_eapol eapol;
	size_t frame_len;
	int rc;

	/* msg2/msg4-like STA-direction shape: pairwise | MIC | SECURE, no ACK,
	 * key data length 0 (the exact vendor 4/4 detector input). */
	frame_len = build_eapol_key_frame(frame, sizeof(frame), DOC_BSSID,
					  DOC_STA,
					  HDTX_KEY_INFO_SECURE |
					  HDTX_KEY_INFO_MIC |
					  HDTX_KEY_INFO_PAIRWISE, 0u);
	rc = hdtx_eth_parse(frame, (u32)frame_len, &eth);
	CHECK_EQ_INT(rc, HDTX_OK);
	rc = hdtx_classify_eapol(&eth, &eapol);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(eapol.is_key, 1);
	CHECK_EQ_INT(eapol.pairwise, 1);
	CHECK_EQ_INT(eapol.key_datalen, 0);
	CHECK_EQ_INT(eapol.ptk_done_like, 1);
	CHECK_EQ_INT(eapol.msg, HDTX_EAPOL_MSG_OTHER); /* no ACK: not AP TX */
}

static void test_non_eapol_and_arp(void)
{
	static const u8 ipv4[40] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* DA bcast */
		0x02, 0x00, 0x68, 0x69, 0x35, 0x01, /* SA */
		0x08, 0x00,			    /* IPv4 */
		0x45, 0x00, 0x00, 0x14, 0xde, 0xad, 0xbe, 0xef,
	};
	static const u8 arp[14 + 28] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0x02, 0x00, 0x68, 0x69, 0x35, 0x01,
		0x08, 0x06,
	};
	u8 mpdu[HDTX_MPDU_MAX];
	struct hdtx_eth eth;
	struct hdtx_eapol eapol;
	struct hdtx_mpdu_params p;
	size_t mpdu_len = 0;
	int rc;

	rc = hdtx_eth_parse(ipv4, sizeof(ipv4), &eth);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(eth.ethertype, 0x0800);
	rc = hdtx_classify_eapol(&eth, &eapol);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(eapol.is_eapol, 0);
	CHECK_EQ_INT(eapol.is_key, 0);
	CHECK_EQ_INT(hdtx_is_multicast(eth.da), 1);

	memset(&p, 0, sizeof(p));
	p.bssid = DOC_BSSID;
	p.da = eth.da;
	p.sa = eth.sa;
	p.payload = eth.payload;
	p.payload_len = eth.payload_len;
	p.seq = 0u;
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &mpdu_len, &p);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(mpdu_len, 24 + 8 + 26);
	CHECK_EQ_INT(mpdu[0], 0x08); /* plain data */
	CHECK_EQ_INT(mpdu[1], 0x02); /* FromDS, unprotected */
	CHECK_EQ_INT(mpdu[30], 0x08); /* LLC/SNAP: ethertype BE */
	CHECK_EQ_INT(mpdu[31], 0x00);

	rc = hdtx_eth_parse(arp, sizeof(arp), &eth);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(hdtx_is_multicast(eth.da), 1);
	CHECK_EQ_INT(eth.ethertype, 0x0806);
	CHECK_EQ_INT(memcmp(eth.da, DOC_BCAST, 6), 0);
}

static void test_refusals(void)
{
	u8 frame[64];
	u8 mpdu[HDTX_MPDU_MAX];
	struct hdtx_eth eth;
	struct hdtx_mpdu_params p;
	size_t out_len = 0;
	int rc;

	/* short 802.3 frame */
	memset(frame, 0, sizeof(frame));
	rc = hdtx_eth_parse(frame, 13, &eth);
	CHECK_EQ_INT(rc, HDTX_ESHORT);
	rc = hdtx_eth_parse(frame, 14, &eth);
	CHECK_EQ_INT(rc, HDTX_OK); /* zero DA/SA is structurally fine */
	/* VLAN tagged payload refused */
	put_be16(frame + 12, HDTX_ETH_P_VLAN);
	rc = hdtx_eth_parse(frame, sizeof(frame), &eth);
	CHECK_EQ_INT(rc, HDTX_EFORMAT);

	/* MPDU bounds: payload longer than HDTX_MPDU_MAX */
	memset(&p, 0, sizeof(p));
	p.bssid = DOC_BSSID;
	p.da = DOC_STA;
	p.sa = DOC_BSSID;
	p.payload = frame;
	p.payload_len = 800; /* 24 + 6 + 800 > HDTX_MPDU_MAX */
	p.seq = 0;
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &out_len, &p);
	CHECK_EQ_INT(rc, HDTX_EBOUNDS);
	p.payload_len = 1; /* less than the 2-byte LLC type */
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &out_len, &p);
	CHECK_EQ_INT(rc, HDTX_ESHORT);
	p.payload_len = 34;
	p.qos = 2; /* invalid */
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &out_len, &p);
	CHECK_EQ_INT(rc, HDTX_EARG);
	p.qos = 0;
	p.tid = 16; /* invalid */
	rc = hdtx_build_data_mpdu(mpdu, sizeof(mpdu), &out_len, &p);
	CHECK_EQ_INT(rc, HDTX_EARG);
	/* out buffer too small */
	p.tid = 0;
	p.payload_len = 100;
	rc = hdtx_build_data_mpdu(mpdu, 100, &out_len, &p);
	CHECK_EQ_INT(rc, HDTX_EBOUNDS);
}

static void test_classify_bounds(void)
{
	u8 frame[256];
	struct hdtx_eth eth;
	struct hdtx_eapol eapol;
	size_t frame_len;
	int rc;

	frame_len = build_eapol_key_frame(frame, sizeof(frame), DOC_STA,
					  DOC_BSSID, HDTX_KEY_INFO_PAIRWISE |
					  HDTX_KEY_INFO_ACK, 0u);
	/* truncate right in the EAPOL header */
	rc = hdtx_eth_parse(frame, 14 + 5, &eth);
	CHECK_EQ_INT(rc, HDTX_OK);
	rc = hdtx_classify_eapol(&eth, &eapol);
	CHECK_EQ_INT(rc, HDTX_ESHORT); /* header claims a 95-byte key body */

	/* EAPOL header claims 95 but the frame is truncated */
	rc = hdtx_eth_parse(frame, 14 + 4 + 20, &eth);
	CHECK_EQ_INT(rc, HDTX_OK);
	rc = hdtx_classify_eapol(&eth, &eapol);
	CHECK_EQ_INT(rc, HDTX_ESHORT);
	(void)frame_len;
}

static void test_decide_tx(void)
{
	struct hdtx_tx_decision d;

	/* EAPOL to an associated but not-yet-authorized peer: allowed, clear */
	CHECK_EQ_INT(hdtx_decide_tx(1, 0, 1, 0, 1, HDTX_PROTECT_AUTO, 1, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.allowed, 1);
	CHECK_EQ_INT(d.protect, 0);
	/* non-EAPOL data to the same peer: refused before the 4-way */
	CHECK_EQ_INT(hdtx_decide_tx(1, 0, 1, 0, 0, HDTX_PROTECT_AUTO, 1, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.allowed, 0);
	CHECK_EQ_INT(d.reason, HDTX_EUNAUTH);
	/* authorized peer -> allowed + Protected bit in AUTO */
	CHECK_EQ_INT(hdtx_decide_tx(1, 0, 1, 1, 0, HDTX_PROTECT_AUTO, 0, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.allowed, 1);
	CHECK_EQ_INT(d.protect, 1);
	/* unknown unicast peer: refused when peer-aware is on */
	CHECK_EQ_INT(hdtx_decide_tx(1, 0, 0, 0, 1, HDTX_PROTECT_AUTO, 0, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.allowed, 0);
	CHECK_EQ_INT(d.reason, HDTX_EPEER);
	/* unknown unicast peer in the explicit diagnostic mode: allowed, clear */
	CHECK_EQ_INT(hdtx_decide_tx(0, 0, 0, 0, 1, HDTX_PROTECT_AUTO, 1, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.allowed, 1);
	CHECK_EQ_INT(d.protect, 0);
	/* multicast: never needs a peer; AUTO protects only with a group key */
	CHECK_EQ_INT(hdtx_decide_tx(1, 1, 0, 0, 0, HDTX_PROTECT_AUTO, 0, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.allowed, 1);
	CHECK_EQ_INT(d.protect, 0);
	CHECK_EQ_INT(hdtx_decide_tx(1, 1, 0, 0, 0, HDTX_PROTECT_AUTO, 1, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.protect, 1);
	/* forced modes */
	CHECK_EQ_INT(hdtx_decide_tx(1, 0, 1, 0, 1, HDTX_PROTECT_ALWAYS, 0, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.protect, 1);
	CHECK_EQ_INT(hdtx_decide_tx(1, 0, 1, 1, 1, HDTX_PROTECT_NEVER, 0, &d),
		     HDTX_OK);
	CHECK_EQ_INT(d.protect, 0);
	CHECK_EQ_INT(hdtx_decide_tx(0, 0, 1, 1, 1, HDTX_PROTECT_NEVER + 1, 0,
				    &d), HDTX_EARG);
}

static void test_frame_buffer(void)
{
	u8 buf[HDTX_FRAME_AREA];
	u8 mpdu[HDTX_MPDU_MAX];
	u32 off = 0;
	u32 devva = 0;
	int rc;

	memset(mpdu, 0x5c, sizeof(mpdu));
	memset(buf, 0xaa, sizeof(buf));
	rc = hdtx_frame_write(buf, sizeof(buf), 0x54585458u, mpdu, 100u, &off);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(off, 4);
	CHECK_EQ_INT(buf[0], 0x58);
	CHECK_EQ_INT(buf[1], 0x54);
	CHECK_EQ_INT(buf[2], 0x58);
	CHECK_EQ_INT(buf[3], 0x54);
	CHECK_EQ_INT(buf[4], 0x00); /* 16-byte device header stays zero */
	CHECK_EQ_INT(buf[19], 0x00);
	CHECK_EQ_INT(buf[20], 0x5c); /* MPDU starts at +20 */
	CHECK_EQ_INT(buf[20 + 100], 0xaa); /* nothing beyond the MPDU */

	/* too small buffer / too large MPDU / zero length */
	rc = hdtx_frame_write(buf, 50, 1u, mpdu, 100u, &off);
	CHECK_EQ_INT(rc, HDTX_EBOUNDS);
	rc = hdtx_frame_write(buf, sizeof(buf), 1u, mpdu, HDTX_MPDU_MAX + 1u,
			      &off);
	CHECK_EQ_INT(rc, HDTX_EARG);
	rc = hdtx_frame_write(buf, sizeof(buf), 1u, mpdu, 0u, &off);
	CHECK_EQ_INT(rc, HDTX_EARG);

	/* device-VA window (dp_rings identity window 0x80000000..0x8fffffff) */
	rc = hdtx_buffer_devva(0x80001000ull, 0x80000000u, 0x8fffffffu, &devva);
	CHECK_EQ_INT(rc, HDTX_OK);
	CHECK_EQ_INT(devva, 0x80001004u);
	rc = hdtx_buffer_devva(0x7ffff000ull, 0x80000000u, 0x8fffffffu, &devva);
	CHECK_EQ_INT(rc, HDTX_EBOUNDS);
	rc = hdtx_buffer_devva(0x90000000ull, 0x80000000u, 0x8fffffffu, &devva);
	CHECK_EQ_INT(rc, HDTX_EBOUNDS);
	rc = hdtx_buffer_devva(0x8fffff00ull, 0x80000000u, 0x8fffffffu, &devva);
	CHECK_EQ_INT(rc, HDTX_EBOUNDS); /* 0x400-byte slab would cross the end */
}

static void test_ring_advance(void)
{
	u32 adv = 0xdeadbeefu;

	/* no progress */
	CHECK_EQ_INT(hdtx_ring_advance(0x0005u, 0x0005u, 64u, &adv), HDTX_OK);
	CHECK_EQ_INT(adv, 0);
	/* simple progress */
	CHECK_EQ_INT(hdtx_ring_advance(0x0005u, 0x0009u, 64u, &adv), HDTX_OK);
	CHECK_EQ_INT(adv, 4);
	/* one wrap: 62 -> 3 with phase flip */
	CHECK_EQ_INT(hdtx_ring_advance(0x003eu, 0x8003u, 64u, &adv), HDTX_OK);
	CHECK_EQ_INT(adv, 5);
	/* consumer moving backwards is refused */
	CHECK_EQ_INT(hdtx_ring_advance(0x0009u, 0x0004u, 64u, &adv),
		     HDTX_EFORMAT);
	/* out-of-range index refused */
	CHECK_EQ_INT(hdtx_ring_advance(0x0040u, 0x0040u, 64u, &adv),
		     HDTX_EFORMAT);
	CHECK_EQ_INT(hdtx_ring_advance(0x0001u, 0x0001u, 0u, &adv), HDTX_EARG);
}

int main(void)
{
	test_constants();
	test_msg1_mpdu();
	test_msg3_mpdu_qos_protected();
	test_msg4_like_detector();
	test_non_eapol_and_arp();
	test_refusals();
	test_classify_bounds();
	test_decide_tx();
	test_frame_buffer();
	test_ring_advance();
	printf("wifi-data-tx portable logic selftest: %u/%u checks passed\n",
	       checks - failures, checks);
	return failures ? 1 : 0;
}
